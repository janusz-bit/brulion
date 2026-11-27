// The chat formats of a model of the user's own (src/core/ai/localformats.cpp) against llama.cpp
// itself: a conversation without tools is written letter for letter as llama.cpp's built-in
// templates write it (llama_chat_apply_template of the pinned release), and the grammar of every
// format's tool calls (local::toolGrammar) is one llama.cpp's GBNF parser takes — with the tags as
// tokens and as letters both. No model needed: llama.cpp's library only.
//
// With BRULION_OWN_MODEL naming a GGUF file of a model that is not LFM3 (e.g. Qwen3 0.6B), it is
// also read as a model of the user's own (ai/ownmodels.h) and asked, through the runner, a question
// it answers with a tool call — kept to the tools by the grammar — and one it answers in words.
//
//   export BRULION_OWN_MODEL=~/…/Qwen3-0.6B-Q8_0.gguf
//   ctest --test-dir build -R tst_ai_llama_formats --output-on-failure

#include "ai/gguf.h"
#include "ai/llamarunner.h"
#include "ai/local.h"
#include "ai/ownmodels.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>

#include <llama.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace Qt::StringLiterals;
using namespace brulion;
using ai::local::Piece;

namespace {

/// A prompt's pieces as one text: control tokens as their letters.
QString joined(const QList<Piece> &pieces)
{
    QString out;
    for (const Piece &piece : pieces)
        out += piece.text;
    return out;
}

/// What llama.cpp's built-in template `name` writes of `request` (its system text and its
/// messages' texts), with the assistant's turn opened.
QString applied(const char *name, const ai::Request &request)
{
    std::vector<std::string> roles;
    std::vector<std::string> contents;
    if (!request.system.isEmpty()) {
        roles.push_back("system");
        contents.push_back(request.system.toStdString());
    }
    for (const ai::Message &m : request.messages) {
        roles.push_back(m.role.toStdString());
        contents.push_back(m.content.toStdString());
    }
    std::vector<llama_chat_message> chat;
    for (size_t i = 0; i < roles.size(); ++i)
        chat.push_back(llama_chat_message{roles[i].c_str(), contents[i].c_str()});
    std::vector<char> buffer(64 * 1024);
    const int32_t n =
            llama_chat_apply_template(name, chat.data(), chat.size(), true, buffer.data(), int32_t(buffer.size()));
    if (n < 0 || n > int32_t(buffer.size()))
        return u"(llama.cpp has no template %1)"_s.arg(QLatin1String(name));
    return QString::fromUtf8(buffer.data(), n);
}

ai::Tool tool(const QString &name, const QString &description, const char *schema)
{
    return ai::Tool{name, description, QJsonDocument::fromJson(schema).object()};
}

/// Tools like the assistant's: strings, an integer, an enum, a required key and an optional one.
QList<ai::Tool> tools()
{
    return {tool(u"search_notes"_s, u"Full-text search of every note."_s,
                 R"({"type": "object", "properties": {"query": {"type": "string", "description": "Words"}},
                     "required": ["query"]})"),
            tool(u"read_page"_s, u"Returns a page's text."_s,
                 R"({"type": "object", "properties": {"name": {"type": "string"}, "from": {"type": "integer"}},
                     "required": ["name"]})"),
            tool(u"update_block"_s, u"Changes a task's status."_s,
                 R"({"type": "object", "properties": {"ref": {"type": "string"},
                     "status": {"type": "string", "enum": ["todo", "doing", "done", "cancelled"]},
                     "urgent": {"type": "boolean"}, "weight": {"type": "number"}}, "required": ["ref"]})"),
            tool(u"list_pages"_s, u"Lists the pages."_s, R"({"type": "object", "properties": {}})")};
}

/// A grammar's rules with its tags written for llama.cpp's parser: `asTokens` each as a token id
/// (as the runner writes a tag its vocabulary has), else as its letters (a loose tag it lacks).
QString rulesFor(const ai::local::Grammar &grammar, bool asTokens)
{
    QString rules = grammar.rules;
    for (qsizetype i = 0; i < grammar.tags.size(); ++i) {
        QString literal = u"\""_s;
        for (const QChar c : grammar.tags.at(i).text)
            literal += c == u'"' || c == u'\\' ? QString(u'\\') + c : QString(c);
        literal += u'"';
        rules.replace(u"<[@%1]>"_s.arg(i), asTokens ? u"<[%1]>"_s.arg(1000 + i) : literal);
    }
    return rules;
}

QString ownModelPath()
{
    return qEnvironmentVariable("BRULION_OWN_MODEL");
}

} // namespace

class TestAiLlamaFormats : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void asLlamaCppWritesThem_data()
    {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("llamaName");
        QTest::addColumn<QString>("after"); // what the format writes after llama.cpp's opening of the answer
        QTest::newRow("ChatML") << u"chatml"_s << u"chatml"_s << QString();
        QTest::newRow("ChatML, thinking") << u"chatml+think"_s << u"chatml"_s << QString();
        QTest::newRow("Phi-3") << u"phi3"_s << u"phi3"_s << QString();
        QTest::newRow("Phi-4") << u"phi4"_s << u"phi4"_s << QString();
        QTest::newRow("Llama 3") << u"llama3"_s << u"llama3"_s << QString();
        QTest::newRow("Gemma") << u"gemma"_s << u"gemma"_s << QString();
        QTest::newRow("Mistral v7") << u"mistral"_s << u"mistral-v7"_s << QString();
        QTest::newRow("DeepSeek 3") << u"deepseek3"_s << u"deepseek3"_s << QString();
        // (R1 opens its thoughts: the answer begins inside them.)
        QTest::newRow("DeepSeek R1") << u"deepseek3+think"_s << u"deepseek3"_s << u"<think>\n"_s;
    }
    void asLlamaCppWritesThem()
    {
        QFETCH(QString, format);
        QFETCH(QString, llamaName);
        QFETCH(QString, after);
        QVERIFY(ai::local::knowsFormat(format));
        // Conversations without tools: one question; a system text; turns back and forth, with
        // Polish letters, lines and spaces at the ends (the templates that trim, trim them).
        QList<ai::Request> requests;
        ai::Request one;
        one.messages << ai::textMessage(u"user"_s, u"Co mam jutro?"_s);
        requests << one;
        ai::Request withSystem = one;
        withSystem.system = u"Jesteś asystentem notatek. Odpowiadasz krótko."_s;
        requests << withSystem;
        ai::Request turns = withSystem;
        turns.messages << ai::textMessage(u"assistant"_s, u"Jutro: zażółć gęślą jaźń 📅 2026-11-26."_s)
                       << ai::textMessage(u"user"_s, u"  A pojutrze?\nI w sobotę?  "_s);
        requests << turns;
        for (const ai::Request &request : std::as_const(requests)) {
            const ai::local::Prompt prompt = ai::local::prompt(format, request);
            const QString expected = applied(qPrintable(llamaName), request) + after;
            QCOMPARE(joined(prompt.pieces), expected);
            // Its system text is the stable start, the turns' tokens control pieces.
            QVERIFY(prompt.stable >= 0 && prompt.stable <= prompt.pieces.size());
            QVERIFY(std::any_of(prompt.pieces.cbegin(), prompt.pieces.cend(),
                                [](const Piece &p) { return p.special; }));
        }
    }

    void theirGrammarsParse_data()
    {
        QTest::addColumn<QString>("format");
        QTest::addColumn<bool>("asTokens");
        for (const QString &format : {u"lfm3"_s, u"chatml"_s, u"chatml+think"_s, u"phi3"_s, u"phi4"_s, u"llama3"_s,
                                      u"gemma"_s, u"mistral"_s, u"deepseek3"_s, u"deepseek3+think"_s}) {
            QTest::addRow("%s, tags as tokens", qPrintable(format)) << format << true;
            if (format != u"lfm3") // (LFM3's tags are tokens of its vocabulary, always)
                QTest::addRow("%s, tags as letters", qPrintable(format)) << format << false;
        }
    }
    void theirGrammarsParse()
    {
        QFETCH(QString, format);
        QFETCH(bool, asTokens);
        const ai::local::Grammar grammar = ai::local::toolGrammar(format, tools());
        QVERIFY(!grammar.isEmpty());
        QVERIFY(!grammar.triggers.isEmpty());
        for (const int trigger : grammar.triggers)
            QVERIFY(trigger >= 0 && trigger < grammar.tags.size());
        const QByteArray rules = rulesFor(grammar, asTokens).toUtf8();
        QVERIFY2(!rules.contains("<[@"), rules.constData());
        // (No vocabulary: a tag as a token is its id; llama.cpp's parser checks the rest.)
        llama_sampler *sampler = llama_sampler_init_grammar(nullptr, rules.constData(), "root");
        QVERIFY2(sampler, rules.constData());
        llama_sampler_free(sampler);
        // The names of the tools are in it, and no other.
        for (const ai::Tool &t : tools())
            QVERIFY2(rules.contains(t.name.toUtf8()), qPrintable(t.name));
        QVERIFY(rules.contains("todo") && rules.contains("cancelled")); // (the enum's values)
        // Without tools: no grammar.
        QVERIFY(ai::local::toolGrammar(format, {}).isEmpty());
    }

    void lfm3sGrammarLetsEveryLetterThrough()
    {
        // LFM3's call under the grammar the runner keeps it to (lfm3Grammar): every token of a
        // parameter's text is let through — a letter the vocabulary writes in pieces too, an
        // emoji's first bytes a token with no whole letter. (Under `free ::= ( !<[@5]> )*` llama.cpp
        // refused such a token, and the model wrote "⭐" for "⏰": a reminder that never rang.) The
        // model's vocabulary only (BRULION_LOCAL_MODEL, LFM3-1.7B-QAD-Q4_0.gguf).
        const QString path = qEnvironmentVariable("BRULION_LOCAL_MODEL");
        if (path.isEmpty())
            QSKIP("BRULION_LOCAL_MODEL is not set: no LFM3 vocabulary to try the grammar on");
        QString error;
        QVERIFY2(ai::probeLlamaModel(path, QStringLiteral(BRULION_LOCAL_AI_OUTPUT_DIR), &error), qPrintable(error));
        llama_model_params params = llama_model_default_params();
        params.vocab_only = true;
        llama_model *model = llama_model_load_from_file(path.toUtf8().constData(), params);
        QVERIFY(model);
        const llama_vocab *vocab = llama_model_get_vocab(model);
        const auto tokens = [vocab](const QString &text, bool special) {
            const QByteArray utf8 = text.toUtf8();
            std::vector<llama_token> out(size_t(utf8.size()) + 8);
            const int32_t n = llama_tokenize(vocab, utf8.constData(), int32_t(utf8.size()), out.data(),
                                             int32_t(out.size()), false, special);
            out.resize(size_t(std::max(n, 0)));
            return out;
        };
        const ai::Tool journal = tool(u"append_to_journal"_s, u"Adds Markdown to a day's journal."_s,
                                      R"({"type": "object", "properties": {"markdown": {"type": "string"}}})");
        const ai::local::Grammar grammar = ai::local::toolGrammar(u"lfm3"_s, {journal});
        QString rules = grammar.rules;
        for (qsizetype i = 0; i < grammar.tags.size(); ++i) {
            const std::vector<llama_token> tag = tokens(grammar.tags.at(i).text, true);
            QCOMPARE(tag.size(), size_t(1));
            rules.replace(u"<[@%1]>"_s.arg(i), u"<[%1]>"_s.arg(tag.front()));
        }
        const QByteArray utf8 = rules.toUtf8();
        llama_sampler *sampler = llama_sampler_init_grammar(vocab, utf8.constData(), "root");
        QVERIFY2(sampler, utf8.constData());
        // The call as the model writes it: tags as their tokens, texts as the vocabulary splits them.
        const QList<std::pair<QString, bool>> call{
                {u"<tool_call>"_s, true}, {u"\n"_s, false}, {u"<function="_s, true}, {u"append_to_journal>\n"_s, false},
                {u"<parameter="_s, true},
                {u"markdown>\n- [ ] Zadzwonić do mamy ⏰ 2026-11-17 18:00 ✅ 📅 ⏳ żółć\n"_s, false},
                {u"</parameter>"_s, true}, {u"\n"_s, false}, {u"</function>"_s, true}, {u"\n"_s, false},
                {u"</tool_call>"_s, true}};
        for (const auto &[text, special] : call) {
            for (const llama_token t : tokens(text, special)) {
                llama_token_data data{t, 0.0f, 0.0f};
                llama_token_data_array candidates{&data, 1, -1, false};
                llama_sampler_apply(sampler, &candidates);
                char piece[64] = {};
                llama_token_to_piece(vocab, t, piece, sizeof piece - 1, 0, true);
                QVERIFY2(!std::isinf(data.logit), qPrintable(u"token %1 (%2) of “%3” refused"_s.arg(t).arg(
                                                          QString::fromUtf8(QByteArray(piece).toHex(' ')), text)));
                llama_sampler_accept(sampler, t);
            }
        }
        llama_sampler_free(sampler);
        llama_model_free(model);
    }

    void anOwnModelCallsATool()
    {
        if (ownModelPath().isEmpty())
            QSKIP("BRULION_OWN_MODEL is not set: no model of one's own to run");
        const ai::GgufInfo info = ai::readGgufFile(ownModelPath());
        QVERIFY2(info.ok, qPrintable(info.error));
        QString why;
        const std::optional<ai::OwnModel> own = ai::ownModelFrom(ownModelPath(), info, &why);
        QVERIFY2(own, qPrintable(why));
        QVERIFY2(own->format != u"lfm3", qPrintable(own->format));
        const ai::LocalModel model = ai::localModelOf(*own);
        qInfo("%s: %s, %s, format %s, %lld parameters, context %d", qPrintable(model.name),
              qPrintable(own->architecture), qPrintable(own->quantization), qPrintable(model.format), model.parameters,
              model.trainedContext);
        QString error;
        QVERIFY2(ai::probeLlamaModel(ownModelPath(), QStringLiteral(BRULION_LOCAL_AI_OUTPUT_DIR), &error),
                                     qPrintable(error));

        ai::LocalRunner::Options options;
        options.modelPath = ownModelPath();
        options.contextTokens = 8192;
        options.backendDir = QStringLiteral(BRULION_LOCAL_AI_OUTPUT_DIR);
        // (BRULION_OWN_MODEL_THREADS: fewer threads on a machine busy with other work.)
        options.threads = qEnvironmentVariableIntValue("BRULION_OWN_MODEL_THREADS");
        const std::unique_ptr<ai::LocalRunner> runner = ai::openLlamaRunner(options, &error);
        QVERIFY2(runner, qPrintable(error));

        // A question about the notes: a call of one of the tools, its arguments of its schema.
        ai::Request request;
        request.system = u"You are the assistant of a notes app. Search the user's notes with your tools "
                         u"before answering a question about them."_s;
        request.effort = u"low"_s;
        request.tools = tools();
        request.messages << ai::textMessage(u"user"_s, u"What did I write about the bees in my notes?"_s);
        ai::LocalRunner::Result run;
        ai::Reply reply = ai::answerLocally(runner.get(), model, request, nullptr, &run);
        qInfo("tool call: %d prompt tokens in %lld ms, %d written in %lld ms (%s): %s", run.promptTokens, run.promptMs,
              run.outputTokens, run.outputMs, qPrintable(runner->device()), reply.raw.constData());
        QVERIFY2(reply.error.isEmpty(), qPrintable(reply.error));
        QVERIFY2(!reply.toolCalls.isEmpty(), qPrintable(reply.text));
        QStringList names;
        for (const ai::Tool &t : tools())
            names << t.name;
        for (const ai::ToolCall &call : reply.toolCalls)
            QVERIFY2(names.contains(call.name), qPrintable(call.name));
        QCOMPARE(reply.stopReason, u"tool_calls"_s);

        // Its result back, and an answer in words (the call written back as the model wrote it).
        ai::Request next = request;
        next.messages << ai::Message{u"assistant"_s, reply.text, {}, reply.raw, {}};
        ai::Message results;
        results.role = u"user"_s;
        for (const ai::ToolCall &call : reply.toolCalls)
            results.results << ai::ToolResult{call.id,
                                              u"Pszczoły.md: - The bees swarmed on 2026-05-12; "
                                              u"the queen is marked blue."_s,
                                              false};
        next.messages << results;
        reply = ai::answerLocally(runner.get(), model, next, nullptr, &run);
        qInfo("answer: %d prompt tokens (%d reused) in %lld ms, %d written in %lld ms: %s", run.promptTokens,
              run.reusedTokens, run.promptMs, run.outputTokens, run.outputMs, qPrintable(reply.text));
        QVERIFY2(reply.error.isEmpty(), qPrintable(reply.error));
        QVERIFY2(!reply.text.trimmed().isEmpty() || !reply.toolCalls.isEmpty(), "no answer");
        QVERIFY(run.reusedTokens > 0);
    }
};

QTEST_GUILESS_MAIN(TestAiLlamaFormats)
#include "tst_ai_llama_formats.moc"
