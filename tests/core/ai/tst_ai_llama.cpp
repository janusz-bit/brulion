// The model runner over llama.cpp (src/core/ai/llamarunner.h) with a real model: skipped unless
// BRULION_LOCAL_MODEL names an LFM3 GGUF file (BRULION_LOCAL_PROJECTOR its picture part, for the
// tests with pictures), e.g. the files Settings → Extensions → AI downloads:
//
//   export BRULION_LOCAL_MODEL=~/…/LFM3-VL-2B-QAD-Q4_0.gguf
//   export BRULION_LOCAL_PROJECTOR=~/…/mmproj-LFM3-VL-2B-Q8_0.gguf
//   ctest --test-dir build -R tst_ai_llama --output-on-failure
//
// What is checked is the runner, not the model's wit: that what it kept of a conversation — in
// memory, or on disk from a model opened before — gives the very answer a fresh start gives, that
// a note's text never turns into control tokens, that a long request is refused with its size,
// that it stops when told.

#include "ai/llamarunner.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using ai::LocalRunner;
using ai::local::Piece;

namespace {

const QString kFormat = u"lfm3"_s;

QString modelPath()
{
    return qEnvironmentVariable("BRULION_LOCAL_MODEL");
}
QString projectorPath()
{
    return qEnvironmentVariable("BRULION_LOCAL_PROJECTOR");
}

std::unique_ptr<LocalRunner> openRunner(int context = 8192, bool pictures = false, const QString &stateDir = QString())
{
    LocalRunner::Options options;
    options.modelPath = modelPath();
    if (pictures)
        options.projectorPath = projectorPath();
    options.contextTokens = context;
    options.stateDir = stateDir;
    options.imageTokens = 256;
    options.backendDir = QStringLiteral(BRULION_LOCAL_AI_OUTPUT_DIR);
    QString error;
    std::unique_ptr<LocalRunner> runner = ai::openLlamaRunner(options, &error);
    if (!runner)
        qWarning("cannot open the model: %s", qPrintable(error));
    return runner;
}

ai::Tool searchTool()
{
    return ai::Tool{u"search_notes"_s,
                    u"Full-text search over the user's notes. Call it before answering a question about them."_s,
                    QJsonDocument::fromJson(R"({"type": "object", "properties": {"query": {"type": "string", )"
                                            R"("description": "Words to look for"}}, "required": ["query"]})")
                            .object()};
}

ai::Request question(const QString &words, bool thinking = false)
{
    ai::Request request;
    request.system = u"You are an assistant in a notes app. You answer briefly."_s;
    request.messages << ai::textMessage(u"user"_s, words);
    if (!thinking)
        request.effort = u"low"_s;
    return request;
}

QString textOf(const LocalRunner::Result &result)
{
    QString out;
    for (const Piece &piece : result.pieces)
        out += piece.text;
    return out;
}

LocalRunner::Params params(int maxTokens = 96)
{
    LocalRunner::Params p;
    p.maxTokens = maxTokens;
    p.reserveTokens = 256;
    return p;
}

/// A picture file as the app writes one: a red square on white.
QByteArray redSquare()
{
    QImage image(320, 320, QImage::Format_RGB888);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.fillRect(60, 60, 200, 200, QColor(220, 20, 20));
    painter.end();
    QByteArray file;
    QBuffer buffer(&file);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return file;
}

} // namespace

class TestAiLlama : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        if (modelPath().isEmpty())
            QSKIP("BRULION_LOCAL_MODEL is not set: no model to run");
    }

    void answers()
    {
        const std::unique_ptr<LocalRunner> runner = openRunner();
        QVERIFY(runner);
        QCOMPARE(runner->contextTokens(), 8192);
        QVERIFY(!runner->sees());
        QVERIFY2(runner->device().contains(u"ggml-cpu"), qPrintable(runner->device()));
        // Its threads on the fast cores, where the processor tells which they are (ai/cpus.h).
        const QList<int> cores = ai::computeCores(ai::cpuCores());
        if (!cores.isEmpty())
            QVERIFY2(runner->device().endsWith(u"%1 threads on cores %2"_s.arg(cores.size()).arg(ai::coreList(cores))),
                     qPrintable(runner->device()));

        const ai::Request request = question(u"Say OK."_s);
        const LocalRunner::Result r = runner->generate(ai::local::prompt(kFormat, request), params());
        QVERIFY2(r.status == LocalRunner::Ok, qPrintable(r.error));
        QVERIFY(r.complete);
        QVERIFY(r.promptTokens > 10 && r.promptTokens < 80);
        QVERIFY(r.outputTokens > 0);
        QCOMPARE(r.reusedTokens, 0);
        const ai::Reply reply = ai::local::reply(kFormat, r.pieces, request, r.complete);
        QVERIFY2(reply.text.contains(u"OK", Qt::CaseInsensitive), qPrintable(reply.text));
        qInfo("%s: %d prompt tokens in %lld ms, %d answer tokens in %lld ms: %s", qPrintable(runner->device()),
              r.promptTokens, r.promptMs, r.outputTokens, r.outputMs, qPrintable(reply.text));

        // With thoughts first: they end with the model's own token, then the answer.
        const ai::Request thinking = question(u"What is 17 + 25? Answer with the number."_s, true);
        const LocalRunner::Result t = runner->generate(ai::local::prompt(kFormat, thinking), params(400));
        QVERIFY2(t.status == LocalRunner::Ok, qPrintable(t.error));
        QVERIFY(t.pieces.contains(Piece{u"</think>"_s, true, {}}));
        QVERIFY2(ai::local::reply(kFormat, t.pieces, thinking, t.complete).text.contains(u"42"), qPrintable(textOf(t)));
    }

    void keepsWhatItRead()
    {
        // A conversation with tools, turn by turn, twice: on a runner that keeps the conversation
        // in memory and on one that starts each turn afresh. Where both read the same tokens in
        // the same steps the answers are the same to the letter; a turn continued from the
        // model's own words (read one token at a time while it wrote them) may differ by
        // rounding, never in what it is about.
        const std::unique_ptr<LocalRunner> keeping = openRunner();
        QVERIFY(keeping);
        ai::Request request =
                question(u"Search my notes for the word \"szarlotka\" and tell me what you found."_s, true);
        request.tools << searchTool();

        int firstPrompt = 0;
        int firstOutput = 0;
        for (int turn = 0; turn < 3; ++turn) {
            const ai::local::Prompt prompt = ai::local::prompt(kFormat, request);
            const LocalRunner::Result kept = keeping->generate(prompt, params(300));
            QVERIFY2(kept.status == LocalRunner::Ok, qPrintable(kept.error));
            const std::unique_ptr<LocalRunner> fresh = openRunner();
            QVERIFY(fresh);
            const LocalRunner::Result afresh = fresh->generate(prompt, params(300));
            QVERIFY2(afresh.status == LocalRunner::Ok, qPrintable(afresh.error));
            QCOMPARE(afresh.reusedTokens, 0);
            QCOMPARE(kept.promptTokens, afresh.promptTokens);
            if (turn == 0) {
                QCOMPARE(textOf(kept), textOf(afresh));
                QCOMPARE(kept.pieces, afresh.pieces);
            } else {
                QVERIFY2(textOf(kept).contains(u"szarlotka", Qt::CaseInsensitive), qPrintable(textOf(kept)));
                QVERIFY2(textOf(afresh).contains(u"szarlotka", Qt::CaseInsensitive), qPrintable(textOf(afresh)));
            }

            const ai::Reply reply = ai::local::reply(kFormat, kept.pieces, request, kept.complete);
            qInfo("turn %d: %d prompt tokens (%d kept), %d written, %lld + %lld ms: %s", turn, kept.promptTokens,
                  kept.reusedTokens, kept.outputTokens, kept.promptMs, kept.outputMs,
                  qPrintable(reply.toolCalls.isEmpty() ? reply.text.left(100) : u"calls "_s + reply.toolCalls[0].name));
            if (turn == 0) {
                QCOMPARE(kept.reusedTokens, 0);
                firstPrompt = kept.promptTokens;
                firstOutput = kept.outputTokens;
            } else if (turn == 1) {
                // The first prompt and the model's own turn, as it wrote it, are still there.
                QVERIFY2(kept.reusedTokens >= firstPrompt, qPrintable(QString::number(kept.reusedTokens)));
                QCOMPARE(kept.reusedTokens, firstPrompt + firstOutput);
            }
            if (reply.toolCalls.isEmpty())
                break;
            ai::Message turnBack = ai::textMessage(u"assistant"_s, QString());
            turnBack.raw = reply.raw;
            ai::Message results = ai::textMessage(u"user"_s, QString());
            for (const ai::ToolCall &call : reply.toolCalls)
                results.results << ai::ToolResult{call.id, u"- [[Przepisy]]: szarlotka babci — 1 kg jabłek, cynamon"_s,
                                                  false};
            request.messages << turnBack << results;
        }
        QVERIFY2(request.messages.size() >= 3, "the model never called the tool");

        // Another question after the same system text and tools: those are kept, the rest read
        // anew — the answer again as from a fresh start.
        ai::Request other = question(u"Say only the word: kot"_s);
        other.tools << searchTool();
        const ai::local::Prompt prompt = ai::local::prompt(kFormat, other);
        const LocalRunner::Result kept = keeping->generate(prompt, params());
        const std::unique_ptr<LocalRunner> fresh = openRunner();
        const LocalRunner::Result afresh = fresh->generate(prompt, params());
        QVERIFY2(kept.status == LocalRunner::Ok, qPrintable(kept.error));
        QVERIFY2(kept.reusedTokens > 60 && kept.reusedTokens < kept.promptTokens,
                 qPrintable(QString::number(kept.reusedTokens)));
        QCOMPARE(kept.pieces, afresh.pieces);
        qInfo("another question: %d of %d prompt tokens kept", kept.reusedTokens, kept.promptTokens);

        // The same request again: all but its last token kept.
        const LocalRunner::Result again = keeping->generate(prompt, params());
        QCOMPARE(again.pieces, afresh.pieces);
        QVERIFY2(again.reusedTokens > 60, qPrintable(QString::number(again.reusedTokens)));
    }

    void keepsTheStartOnDisk()
    {
        // The system text and the tools are read once: a model opened afterwards finds the state
        // after them on disk, reads only the rest — and answers as the first one did.
        QTemporaryDir states;
        QVERIFY(states.isValid());
        ai::Request request = question(u"Search my notes for the word \"szarlotka\"."_s);
        for (int i = 1; i <= 30; ++i)
            request.system +=
                    u"\nRule %1: answer in the language of the question, ąćęłńóśźż kept as they are."_s.arg(i);
        request.tools << searchTool();
        const ai::local::Prompt prompt = ai::local::prompt(kFormat, request);
        const auto stateFiles = [&] { return QDir(states.path()).entryInfoList({u"*.state"_s}, QDir::Files); };

        LocalRunner::Result first;
        {
            const std::unique_ptr<LocalRunner> runner = openRunner(8192, false, states.path());
            QVERIFY(runner);
            first = runner->generate(prompt, params());
            QVERIFY2(first.status == LocalRunner::Ok, qPrintable(first.error));
            QCOMPARE(first.reusedTokens, 0);
            QCOMPARE(first.storedTokens, 0);
        }
        QCOMPARE(stateFiles().size(), 1);
        const qint64 bytes = stateFiles().first().size();

        const auto again = [&](const QString &what) {
            const std::unique_ptr<LocalRunner> runner = openRunner(8192, false, states.path());
            const LocalRunner::Result r = runner->generate(prompt, params());
            if (r.status != LocalRunner::Ok)
                qWarning("%s: %s", qPrintable(what), qPrintable(r.error));
            qInfo("%s: %d prompt tokens, %d from the disk, read in %lld ms", qPrintable(what), r.promptTokens,
                  r.storedTokens, r.promptMs);
            return r;
        };
        const LocalRunner::Result second = again(u"opened again"_s);
        QVERIFY2(second.status == LocalRunner::Ok, qPrintable(second.error));
        QCOMPARE(second.reusedTokens, 0);
        QVERIFY2(second.storedTokens > 256 && second.storedTokens < second.promptTokens,
                 qPrintable(QString::number(second.storedTokens)));
        QCOMPARE(second.pieces, first.pieces);
        QCOMPARE(stateFiles().size(), 1);

        // Another question after the same system text and tools: its start from the disk too.
        ai::Request other = question(u"Say only the word: kot"_s);
        other.system = request.system;
        other.tools = request.tools;
        const ai::local::Prompt otherPrompt = ai::local::prompt(kFormat, other);
        const std::unique_ptr<LocalRunner> fresh = openRunner();
        const LocalRunner::Result afresh = fresh->generate(otherPrompt, params());
        const std::unique_ptr<LocalRunner> stored = openRunner(8192, false, states.path());
        const LocalRunner::Result fromDisk = stored->generate(otherPrompt, params());
        QVERIFY2(fromDisk.status == LocalRunner::Ok, qPrintable(fromDisk.error));
        QCOMPARE(fromDisk.storedTokens, second.storedTokens);
        QCOMPARE(fromDisk.pieces, afresh.pieces);
        QCOMPARE(afresh.storedTokens, 0); // (a runner without the folder keeps nothing)

        // A damaged file is read anew — the same answer — and kept again.
        {
            QFile file(stateFiles().first().absoluteFilePath());
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.resize(bytes / 2));
        }
        const LocalRunner::Result damaged = again(u"a damaged file"_s);
        QVERIFY2(damaged.status == LocalRunner::Ok, qPrintable(damaged.error));
        QCOMPARE(damaged.storedTokens, 0);
        QCOMPARE(damaged.pieces, first.pieces);
        QCOMPARE(stateFiles().size(), 1);
        QCOMPARE(stateFiles().first().size(), bytes);
        QCOMPARE(again(u"kept again"_s).storedTokens, second.storedTokens);

        // A state is its prompt's own: a system text one letter apart is read anew, and kept
        // beside the first.
        ai::Request changed = request;
        changed.system.replace(u"Rule 7"_s, u"Rule 8"_s);
        const std::unique_ptr<LocalRunner> runner = openRunner(8192, false, states.path());
        const LocalRunner::Result r = runner->generate(ai::local::prompt(kFormat, changed), params());
        QVERIFY2(r.status == LocalRunner::Ok, qPrintable(r.error));
        QCOMPARE(r.storedTokens, 0);
        QCOMPARE(stateFiles().size(), 2);
    }

    void anotherRequestInBetween()
    {
        // A conversation, a request about something else, the conversation again: the other
        // request takes the second lane and the conversation is still in memory.
        const std::unique_ptr<LocalRunner> runner = openRunner();
        QVERIFY(runner);
        ai::Request talk = question(u"Remember the number 731. Reply: noted."_s);
        talk.tools << searchTool();
        const LocalRunner::Result first = runner->generate(ai::local::prompt(kFormat, talk), params());
        QVERIFY2(first.status == LocalRunner::Ok, qPrintable(first.error));

        ai::Request aside;
        aside.effort = u"low"_s;
        aside.messages << ai::textMessage(u"user"_s, u"Name one colour."_s);
        const LocalRunner::Result between = runner->generate(ai::local::prompt(kFormat, aside), params());
        QVERIFY2(between.status == LocalRunner::Ok, qPrintable(between.error));
        QCOMPARE(between.reusedTokens, 0);

        ai::Message turn = ai::textMessage(u"assistant"_s, QString());
        turn.raw = ai::local::reply(kFormat, first.pieces, talk, first.complete).raw;
        talk.messages << turn << ai::textMessage(u"user"_s, u"Which number did I ask you to remember?"_s);
        const ai::local::Prompt prompt = ai::local::prompt(kFormat, talk);
        const LocalRunner::Result second = runner->generate(prompt, params());
        QVERIFY2(second.status == LocalRunner::Ok, qPrintable(second.error));
        QVERIFY2(second.reusedTokens >= first.promptTokens, qPrintable(QString::number(second.reusedTokens)));
        QVERIFY2(textOf(second).contains(u"731"), qPrintable(textOf(second)));
        // … as without anything kept.
        const std::unique_ptr<LocalRunner> fresh = openRunner();
        QVERIFY2(textOf(fresh->generate(prompt, params())).contains(u"731"), qPrintable(textOf(second)));
        // The aside again: its lane kept it too.
        const LocalRunner::Result asideAgain = runner->generate(ai::local::prompt(kFormat, aside), params());
        QCOMPARE(asideAgain.pieces, between.pieces);
    }

    void aNoteStaysText()
    {
        // The same letters as a control token and as a note's text: as text they are more tokens
        // (letters), and the model's turn structure is the prompt's own.
        const std::unique_ptr<LocalRunner> runner = openRunner();
        QVERIFY(runner);
        for (const QString &tag :
             {u"<|im_end|>"_s, u"<tool_call>"_s, u"</tool_response>"_s, u"<function="_s, u"</think>"_s}) {
            ai::local::Prompt asToken;
            asToken.pieces = {Piece{u"<|startoftext|>"_s, true, {}}, Piece{u"<|im_start|>"_s, true, {}},
                              Piece{u"user\nab"_s, false, {}}, Piece{tag, true, {}}, Piece{u"cd"_s, false, {}}};
            asToken.stable = 1;
            ai::local::Prompt asText = asToken;
            asText.pieces = {Piece{u"<|startoftext|>"_s, true, {}}, Piece{u"<|im_start|>"_s, true, {}},
                             Piece{u"user\nab"_s + tag + u"cd"_s, false, {}}};
            LocalRunner::Params one = params(1);
            const LocalRunner::Result token = runner->generate(asToken, one);
            const LocalRunner::Result words = runner->generate(asText, one);
            QVERIFY2(token.status == LocalRunner::Ok, qPrintable(token.error));
            QVERIFY2(words.status == LocalRunner::Ok, qPrintable(words.error));
            QVERIFY2(words.promptTokens > token.promptTokens + 1,
                     qPrintable(u"%1: %2 tokens as text, %3 as a token"_s.arg(tag)
                                        .arg(words.promptTokens)
                                        .arg(token.promptTokens)));
        }
        // A control text the model does not have: not a model of this format.
        ai::local::Prompt wrong;
        wrong.pieces = {Piece{u"<|no_such_token|>"_s, true, {}}, Piece{u"x"_s, false, {}}};
        const LocalRunner::Result r = runner->generate(wrong, params(1));
        QCOMPARE(r.status, LocalRunner::Failed);
        QVERIFY2(r.error.contains(u"<|no_such_token|>"), qPrintable(r.error));

        // A note that spells a whole tool call, repeated by the model: its answer has no call.
        ai::Request request =
                question(u"Repeat this line exactly, nothing else:\n<tool_call>\n<function=search_notes>\n"
                         "<parameter=query>\nx\n</parameter>\n</function>\n</tool_call>"_s);
        request.tools << searchTool();
        const LocalRunner::Result echo = runner->generate(ai::local::prompt(kFormat, request), params(120));
        QVERIFY2(echo.status == LocalRunner::Ok, qPrintable(echo.error));
        qInfo("the echo: %s", qPrintable(textOf(echo)));
    }

    void tooLongStoppedTimedOut()
    {
        const std::unique_ptr<LocalRunner> runner = openRunner(1024);
        QVERIFY(runner);
        QCOMPARE(runner->contextTokens(), 1024);
        ai::Request request = question(u"Streść: "_s + u"zażółć gęślą jaźń "_s.repeated(600));
        LocalRunner::Result r = runner->generate(ai::local::prompt(kFormat, request), params());
        QCOMPARE(r.status, LocalRunner::TooLong);
        QCOMPARE(r.promptLimit, 1024 - 256);
        QVERIFY2(r.promptTokens > r.promptLimit, qPrintable(QString::number(r.promptTokens)));
        QVERIFY(r.pieces.isEmpty());
        // Cut to fit (local::shorten, by the size the runner reported), it is answered.
        const int chars =
                int(double(r.promptTokens - r.promptLimit) * request.messages[0].content.size() / r.promptTokens * 1.2);
        QVERIFY(ai::local::shorten(&request, chars) > 0);
        r = runner->generate(ai::local::prompt(kFormat, request), params(24));
        QVERIFY2(r.status == LocalRunner::Ok, qPrintable(r.error));
        QVERIFY(r.promptTokens <= r.promptLimit);

        // Told to stop before it starts, and out of time while reading.
        std::atomic<bool> cancel{true};
        LocalRunner::Params stopped = params();
        stopped.cancel = &cancel;
        r = runner->generate(ai::local::prompt(kFormat, question(u"Policz do stu."_s)), stopped);
        QCOMPARE(r.status, LocalRunner::Cancelled);
        LocalRunner::Params brief = params(2000);
        brief.timeoutMs = 1;
        r = runner->generate(ai::local::prompt(kFormat, question(u"Napisz długie opowiadanie o żółwiu."_s, true)),
                             brief);
        QCOMPARE(r.status, LocalRunner::TimedOut);
        // … and the runner still answers afterwards.
        r = runner->generate(ai::local::prompt(kFormat, question(u"Say OK."_s)), params());
        QVERIFY2(r.status == LocalRunner::Ok, qPrintable(r.error));
        // The answer's length is kept.
        r = runner->generate(ai::local::prompt(kFormat, question(u"Write a long story about a turtle."_s)), params(12));
        QVERIFY2(r.status == LocalRunner::Ok, qPrintable(r.error));
        QCOMPARE(r.outputTokens, 12);
        QVERIFY(!r.complete);
    }

    void aTightContext()
    {
        // Two conversations taking turns in a context that barely holds them: every request is
        // answered — the other conversation gives way when there is no room for both.
        const std::unique_ptr<LocalRunner> runner = openRunner(2048);
        QVERIFY(runner);
        ai::Request first =
                question(u"Zapamiętaj: "_s + u"kot Filemon lubi mleko. "_s.repeated(40) + u"Odpowiedz: ok."_s);
        ai::Request second;
        second.effort = u"low"_s;
        second.messages << ai::textMessage(u"user"_s, u"Policz: "_s + u"jeden dwa trzy cztery pięć. "_s.repeated(45)
                                                              + u"Odpowiedz: ok."_s);
        for (int round = 0; round < 4; ++round) {
            for (ai::Request *request : {&first, &second}) {
                const LocalRunner::Result r = runner->generate(ai::local::prompt(kFormat, *request), params(24));
                QVERIFY2(r.status == LocalRunner::Ok, qPrintable(u"round %1: %2"_s.arg(round).arg(r.error)));
                QVERIFY(r.outputTokens > 0);
                // The conversation goes on: the model's turn and another question.
                ai::Message turn = ai::textMessage(u"assistant"_s, QString());
                turn.raw = ai::local::reply(kFormat, r.pieces, *request, r.complete).raw;
                request->messages << turn
                                  << ai::textMessage(u"user"_s, u"Jeszcze raz: "_s + u"tak tak tak. "_s.repeated(20)
                                                                        + u"Odpowiedz: ok."_s);
                if (r.promptTokens > 1500)
                    return; // the context's end: enough rounds
            }
        }
    }

    void seesPictures()
    {
        if (projectorPath().isEmpty())
            QSKIP("BRULION_LOCAL_PROJECTOR is not set: no picture part");
        const std::unique_ptr<LocalRunner> runner = openRunner(8192, true);
        QVERIFY(runner);
        QVERIFY(runner->sees());
        ai::Request request;
        request.effort = u"low"_s;
        ai::Message message = ai::textMessage(u"user"_s, u"What colour is the square? Answer with one word."_s);
        message.images << ai::Image{redSquare(), u"image/png"_s};
        request.messages << message;
        const ai::local::Prompt prompt = ai::local::prompt(kFormat, request);
        const LocalRunner::Result r = runner->generate(prompt, params(24));
        QVERIFY2(r.status == LocalRunner::Ok, qPrintable(r.error));
        // The picture's cells are part of the prompt (at most the 256 asked for, and its two tokens).
        QVERIFY2(r.promptTokens > 60 && r.promptTokens < 320, qPrintable(QString::number(r.promptTokens)));
        QVERIFY2(textOf(r).contains(u"red", Qt::CaseInsensitive), qPrintable(textOf(r)));
        qInfo("a picture: %d prompt tokens in %lld ms: %s", r.promptTokens, r.promptMs, qPrintable(textOf(r)));

        // Asked about the same picture again, it is not looked at again.
        request.messages[0].content = u"Is the square red? Answer yes or no."_s;
        const LocalRunner::Result again = runner->generate(ai::local::prompt(kFormat, request), params(24));
        QVERIFY2(again.status == LocalRunner::Ok, qPrintable(again.error));
        QVERIFY2(again.reusedTokens > 60, qPrintable(QString::number(again.reusedTokens)));
        QVERIFY2(textOf(again).contains(u"yes", Qt::CaseInsensitive), qPrintable(textOf(again)));

        // A runner without the picture part refuses a picture; a file that is no picture too.
        const std::unique_ptr<LocalRunner> blind = openRunner();
        LocalRunner::Result refused = blind->generate(prompt, params(24));
        QCOMPARE(refused.status, LocalRunner::Failed);
        QVERIFY2(refused.error.contains(u"does not see"), qPrintable(refused.error));
        request.messages[0].images[0].data = "not a picture";
        refused = runner->generate(ai::local::prompt(kFormat, request), params(24));
        QCOMPARE(refused.status, LocalRunner::Failed);
    }

    void aMissingFile()
    {
        LocalRunner::Options options;
        options.modelPath = u"/nonexistent/model.gguf"_s;
        options.backendDir = QStringLiteral(BRULION_LOCAL_AI_OUTPUT_DIR);
        QString error;
        QVERIFY(!ai::openLlamaRunner(options, &error));
        QVERIFY2(error.contains(u"/nonexistent/model.gguf"), qPrintable(error));
    }
};

QTEST_GUILESS_MAIN(TestAiLlama)
#include "tst_ai_llama.moc"
