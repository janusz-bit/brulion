// The chat formats of a model of the user's own (ai/local.h; LFM3's own is in local.cpp): the turns
// as llama.cpp's built-in templates write them (src/llama-chat.cpp of the pinned release —
// tst_ai_llama compares) and tool calls in the Hermes convention as Qwen's templates describe it:
// the tools as JSON schemas in the system text, a call as <tool_call>{"name": …, "arguments": …}
// </tool_call>, its results as <tool_response> in a user's turn. Thoughts as <think>…</think>.
//
// The turn tokens are control tokens of the model's vocabulary (a `special` piece: one token, else
// the file is not of the format detected). The tags of calls and thoughts are tokens in some
// vocabularies only (Qwen's) and letters in others (Llama's): `loose` pieces in the prompt, and in
// what the model writes either — so the answer is read from its letters, a tag's token included.
// A note's text is never read as a tag: it is a text piece, tokenized as letters (the runner).

#include "ai/local_p.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::ai::local::detail {

namespace {

const QString kThink = u"<think>"_s;
const QString kThinkEnd = u"</think>"_s;
const QString kTools = u"<tools>"_s;
const QString kToolsEnd = u"</tools>"_s;
const QString kCall = u"<tool_call>"_s;
const QString kCallEnd = u"</tool_call>"_s;
const QString kResponse = u"<tool_response>"_s;
const QString kResponseEnd = u"</tool_response>"_s;
const QString kThinkSuffix = u"+think"_s;

/// A format's family ("chatml" of "chatml+think") and whether its model thinks first.
struct Kind {
    QString family;
    bool thinks = false;
};

Kind kindOf(const QString &format)
{
    if (format.endsWith(kThinkSuffix))
        return Kind{format.chopped(kThinkSuffix.size()), true};
    return Kind{format, false};
}

const QStringList &families()
{
    static const QStringList list{u"chatml"_s, u"phi4"_s,    u"llama3"_s,   u"gemma"_s,
                                  u"phi3"_s,   u"mistral"_s, u"deepseek3"_s};
    return list;
}

/// Families whose model may think (a "+think" format): their templates have the <think> block.
bool canThink(const QString &family)
{
    return family == u"chatml" || family == u"deepseek3";
}

/// The words of the Hermes convention after the system text (Qwen3's template, verbatim).
const QString kToolsIntro = u"# Tools\n\nYou may call one or more functions to assist with the user query.\n\n"
                            u"You are provided with function signatures within "_s;
const QString kToolsMiddle = u" XML tags:\n"_s;
const QString kCallsIntro =
        u"\n\nFor each function call, return a json object with function name and arguments within "_s;
const QString kCallsExample = u"\n{\"name\": <function-name>, \"arguments\": <args-json-object>}\n"_s;

/// The tools as the Hermes convention lists them: the introduction, a JSON line per tool, how a
/// call is written. After the system text and a blank line (none without one).
void writeTools(Writer &out, const QString &system, const QList<Tool> &tools)
{
    if (!system.isEmpty())
        out.text(system + u"\n\n"_s);
    out.text(kToolsIntro);
    out.looseTag(kTools);
    out.looseTag(kToolsEnd);
    out.text(kToolsMiddle);
    out.looseTag(kTools);
    for (const Tool &tool : tools)
        out.text(u"\n{\"type\": \"function\", \"function\": {\"name\": %1, \"description\": %2, \"parameters\": %3}}"_s
                         .arg(jsonText(tool.name), jsonText(tool.description), spacedJson(tool.schema)));
    out.text(u"\n"_s);
    out.looseTag(kToolsEnd);
    out.text(kCallsIntro);
    out.looseTag(kCall);
    out.looseTag(kCallEnd);
    out.text(kToolsMiddle);
    out.looseTag(kCall);
    out.text(kCallsExample);
    out.looseTag(kCallEnd);
}

/// An assistant's tool calls as the Hermes convention writes them (Qwen3's template): each on its
/// own lines, after a line break when something comes before it.
void writeCalls(Writer &out, const Turn &turn)
{
    for (qsizetype c = 0; c < turn.calls.size(); ++c) {
        const Call &call = turn.calls.at(c);
        if (c > 0 || !turn.content.isEmpty())
            out.text(u"\n"_s);
        out.looseTag(kCall);
        const QString arguments = call.arguments.trimmed().isEmpty() ? u"{}"_s : call.arguments;
        out.text(u"\n{\"name\": \"%1\", \"arguments\": %2}\n"_s.arg(call.name, arguments));
        out.looseTag(kCallEnd);
    }
}

/// A turn's thoughts as Qwen3's template keeps them (in the turns after the last question).
void writeThoughts(Writer &out, const QString &thoughts)
{
    out.looseTag(kThink);
    QString inner = thoughts;
    while (inner.startsWith(u'\n'))
        inner.remove(0, 1);
    while (inner.endsWith(u'\n'))
        inner.chop(1);
    out.text(u"\n"_s + inner + u"\n"_s);
    out.looseTag(kThinkEnd);
    out.text(u"\n\n"_s);
}

/// A message of tool results, in the order of the calls they answer: each as <tool_response>
/// lines (`first`: a line break before the first one too).
void writeResults(Writer &out, const Message &m, const QStringList &callIds, bool lineFirst)
{
    QList<ToolResult> results = m.results;
    std::stable_sort(results.begin(), results.end(), [&](const ToolResult &a, const ToolResult &b) {
        const qsizetype ia = callIds.indexOf(a.id);
        const qsizetype ib = callIds.indexOf(b.id);
        return (ia < 0 ? callIds.size() : ia) < (ib < 0 ? callIds.size() : ib);
    });
    for (qsizetype i = 0; i < results.size(); ++i) {
        const ToolResult &result = results.at(i);
        if (i > 0 || lineFirst)
            out.text(u"\n"_s);
        out.looseTag(kResponse);
        out.text(u'\n' + (result.isError ? u"Error: "_s + result.content : result.content) + u'\n');
        out.looseTag(kResponseEnd);
    }
}

QString trimmed(const QString &text)
{
    return text.trimmed();
}

// ---- The families --------------------------------------------------------------------------

/// The body of a turn of `m` (no turn tokens): a user's pictures and words, an assistant's words
/// (with its thoughts after the last question) and calls, or tool results. `trim`: the family's
/// template trims a message (Llama 3, Gemma).
void writeBody(Writer &out, const Message &m, qsizetype index, qsizetype lastUser, const QStringList &callIds,
               bool trim, bool resultsLineFirst)
{
    if (m.role == u"assistant") {
        Turn turn = turnOf(m);
        if (turn.thought && index > lastUser && !turn.thoughts.trimmed().isEmpty()) {
            writeThoughts(out, turn.thoughts);
            QString content = turn.content;
            while (content.startsWith(u'\n'))
                content.remove(0, 1);
            turn.content = content;
        }
        out.text(trim ? trimmed(turn.content) : turn.content);
        writeCalls(out, turn);
    } else if (!m.results.isEmpty()) {
        writeResults(out, m, callIds, resultsLineFirst);
    } else {
        for (const Image &image : m.images)
            out.image(image.data);
        out.text(trim ? trimmed(m.content) : m.content);
    }
}

/// The calls' ids of an assistant's message (results that follow are put in their order).
QStringList callIdsOf(const Message &m)
{
    QStringList ids;
    for (const Call &call : turnOf(m).calls)
        ids << call.id;
    return ids;
}

/// ChatML (Qwen, SmolLM, Hermes …) and Phi-4's variant of it.
Prompt chatmlPrompt(const Request &request, bool thinkingModel, bool phi4)
{
    const QString start = u"<|im_start|>"_s;
    const QString end = u"<|im_end|>"_s;
    const QString sep = u"<|im_sep|>"_s;
    Prompt prompt;
    prompt.bos = true;
    prompt.stops = {start};
    Writer out;
    const auto open = [&](const QString &role) {
        out.tag(start);
        if (phi4) {
            out.text(role);
            out.tag(sep);
        } else {
            out.text(role + u"\n"_s);
        }
    };
    const auto close = [&] {
        out.tag(end);
        if (!phi4)
            out.text(u"\n"_s);
    };
    if (!request.tools.isEmpty()) {
        open(u"system"_s);
        writeTools(out, request.system, request.tools);
        close();
    } else if (!request.system.isEmpty()) {
        open(u"system"_s);
        out.text(request.system);
        close();
    }
    prompt.stable = int(out.size());

    const qsizetype lastUser = lastQuestion(request);
    QStringList callIds;
    for (qsizetype i = 0; i < request.messages.size(); ++i) {
        const Message &m = request.messages.at(i);
        if (m.role == u"assistant") {
            open(u"assistant"_s);
            writeBody(out, m, i, lastUser, callIds, false, false);
            close();
            callIds = callIdsOf(m);
        } else if (!m.results.isEmpty()) {
            // (Qwen3: "<|im_start|>user" and each response on lines of its own.)
            out.tag(start);
            out.text(u"user"_s);
            if (phi4)
                out.tag(sep);
            writeBody(out, m, i, lastUser, callIds, false, !phi4);
            close();
        } else {
            open(u"user"_s);
            writeBody(out, m, i, lastUser, callIds, false, false);
            close();
        }
    }
    open(u"assistant"_s);
    // A model that thinks unless told not to (Qwen3's enable_thinking): an empty block tells it.
    if (thinkingModel && !thinks(request)) {
        out.looseTag(kThink);
        out.text(u"\n\n"_s);
        out.looseTag(kThinkEnd);
        out.text(u"\n\n"_s);
    }
    prompt.pieces = out.pieces();
    return prompt;
}

/// Llama 3 (3.0 … 3.3): headers with the role, each message trimmed.
Prompt llama3Prompt(const Request &request)
{
    const QString header = u"<|start_header_id|>"_s;
    const QString headerEnd = u"<|end_header_id|>"_s;
    const QString end = u"<|eot_id|>"_s;
    Prompt prompt;
    prompt.bos = true;
    prompt.stops = {header};
    Writer out;
    const auto open = [&](const QString &role) {
        out.tag(header);
        out.text(role);
        out.tag(headerEnd);
        out.text(u"\n\n"_s);
    };
    if (!request.tools.isEmpty() || !request.system.isEmpty()) {
        open(u"system"_s);
        if (request.tools.isEmpty())
            out.text(trimmed(request.system));
        else
            writeTools(out, trimmed(request.system), request.tools);
        out.tag(end);
    }
    prompt.stable = int(out.size());
    const qsizetype lastUser = lastQuestion(request);
    QStringList callIds;
    for (qsizetype i = 0; i < request.messages.size(); ++i) {
        const Message &m = request.messages.at(i);
        open(m.role == u"assistant" ? u"assistant"_s : u"user"_s);
        writeBody(out, m, i, lastUser, callIds, true, false);
        out.tag(end);
        if (m.role == u"assistant")
            callIds = callIdsOf(m);
    }
    open(u"assistant"_s);
    prompt.pieces = out.pieces();
    return prompt;
}

/// Gemma (2, 3): no system turn — its text goes before the first user's message —, the assistant
/// is "model", each message trimmed.
Prompt gemmaPrompt(const Request &request)
{
    const QString start = u"<start_of_turn>"_s;
    const QString end = u"<end_of_turn>"_s;
    Prompt prompt;
    prompt.bos = true;
    prompt.stops = {start};
    Writer out;
    bool systemDue = !request.tools.isEmpty() || !request.system.isEmpty();
    const qsizetype lastUser = lastQuestion(request);
    QStringList callIds;
    for (qsizetype i = 0; i < request.messages.size(); ++i) {
        const Message &m = request.messages.at(i);
        const bool assistant = m.role == u"assistant";
        out.tag(start);
        out.text(assistant ? u"model\n"_s : u"user\n"_s);
        if (systemDue && !assistant) {
            if (request.tools.isEmpty())
                out.text(trimmed(request.system));
            else
                writeTools(out, trimmed(request.system), request.tools);
            out.text(u"\n\n"_s);
            // (The system text and the tools are the stable start: a piece of their own, so the
            // runner can keep the model's state after them.)
            out.split();
            prompt.stable = int(out.size());
            systemDue = false;
        }
        writeBody(out, m, i, lastUser, callIds, true, false);
        out.tag(end);
        out.text(u"\n"_s);
        if (assistant)
            callIds = callIdsOf(m);
    }
    out.tag(start);
    out.text(u"model\n"_s);
    prompt.pieces = out.pieces();
    return prompt;
}

/// Phi-3 (3, 3.5): a token per role, a line after it.
Prompt phi3Prompt(const Request &request)
{
    const QString end = u"<|end|>"_s;
    Prompt prompt;
    prompt.bos = true;
    prompt.stops = {u"<|user|>"_s, u"<|system|>"_s};
    Writer out;
    const auto open = [&](const QString &role) {
        out.tag(u"<|%1|>"_s.arg(role));
        out.text(u"\n"_s);
    };
    const auto close = [&] {
        out.tag(end);
        out.text(u"\n"_s);
    };
    if (!request.tools.isEmpty() || !request.system.isEmpty()) {
        open(u"system"_s);
        if (request.tools.isEmpty())
            out.text(request.system);
        else
            writeTools(out, request.system, request.tools);
        close();
    }
    prompt.stable = int(out.size());
    const qsizetype lastUser = lastQuestion(request);
    QStringList callIds;
    for (qsizetype i = 0; i < request.messages.size(); ++i) {
        const Message &m = request.messages.at(i);
        open(m.role == u"assistant" ? u"assistant"_s : u"user"_s);
        writeBody(out, m, i, lastUser, callIds, false, false);
        close();
        if (m.role == u"assistant")
            callIds = callIdsOf(m);
    }
    open(u"assistant"_s);
    prompt.pieces = out.pieces();
    return prompt;
}

/// Mistral's v7 template (Mistral Small 3, Ministral …), as llama.cpp writes it: a space after
/// each opening tag, an assistant's turn ended with </s>.
Prompt mistralPrompt(const Request &request)
{
    Prompt prompt;
    prompt.bos = true;
    prompt.stops = {u"[INST]"_s};
    Writer out;
    if (!request.tools.isEmpty() || !request.system.isEmpty()) {
        out.tag(u"[SYSTEM_PROMPT]"_s);
        out.text(u" "_s);
        if (request.tools.isEmpty())
            out.text(request.system);
        else
            writeTools(out, request.system, request.tools);
        out.tag(u"[/SYSTEM_PROMPT]"_s);
    }
    prompt.stable = int(out.size());
    const qsizetype lastUser = lastQuestion(request);
    QStringList callIds;
    for (qsizetype i = 0; i < request.messages.size(); ++i) {
        const Message &m = request.messages.at(i);
        if (m.role == u"assistant") {
            out.text(u" "_s);
            writeBody(out, m, i, lastUser, callIds, false, false);
            out.tag(u"</s>"_s);
            callIds = callIdsOf(m);
        } else {
            out.tag(u"[INST]"_s);
            out.text(u" "_s);
            writeBody(out, m, i, lastUser, callIds, false, false);
            out.tag(u"[/INST]"_s);
        }
    }
    prompt.pieces = out.pieces();
    return prompt;
}

/// DeepSeek V3 and R1 (its distilled Qwen and Llama models too): the system text first, then a
/// token for each side; R1 thinks in a <think> block the prompt opens.
Prompt deepseekPrompt(const Request &request, bool thinkingModel)
{
    const QString user = u"<｜User｜>"_s;
    const QString assistant = u"<｜Assistant｜>"_s;
    const QString end = u"<｜end▁of▁sentence｜>"_s;
    Prompt prompt;
    prompt.bos = true;
    prompt.stops = {user};
    Writer out;
    if (!request.tools.isEmpty() || !request.system.isEmpty()) {
        if (request.tools.isEmpty())
            out.text(request.system);
        else
            writeTools(out, request.system, request.tools);
        out.text(u"\n\n"_s);
    }
    prompt.stable = int(out.size());
    QStringList callIds;
    for (qsizetype i = 0; i < request.messages.size(); ++i) {
        const Message &m = request.messages.at(i);
        if (m.role == u"assistant") {
            out.tag(assistant);
            // (R1's template drops every turn's thoughts.)
            writeBody(out, m, i, request.messages.size(), callIds, false, false);
            out.tag(end);
            callIds = callIdsOf(m);
        } else {
            out.tag(user);
            writeBody(out, m, i, request.messages.size(), callIds, false, false);
        }
    }
    out.tag(assistant);
    if (thinkingModel) {
        out.looseTag(kThink);
        if (thinks(request)) {
            out.text(u"\n"_s);
            prompt.thinking = true; // (the answer begins inside its thoughts)
        } else {
            out.text(u"\n\n"_s);
            out.looseTag(kThinkEnd);
            out.text(u"\n\n"_s);
        }
    }
    prompt.pieces = out.pieces();
    return prompt;
}

// ---- What the model wrote ------------------------------------------------------------------

struct Written {
    bool thinking = false; ///< the model may have thought (a "+think" format)
    QString thoughts;
    QString content;
    QList<Call> calls;
};

/// A call's body in the Hermes convention: {"name": …, "arguments": {…}} (also "parameters", the
/// arguments as a text of JSON, a ```json fence around it) — or as LFM3 and Qwen3.5 write one,
/// <function=name> with <parameter=key> lines.
std::optional<Call> callOf(const QString &body)
{
    QString text = body.trimmed();
    if (text.startsWith(u"```"_s)) {
        const qsizetype line = text.indexOf(u'\n');
        text = line < 0 ? QString() : text.mid(line + 1);
        if (text.endsWith(u"```"_s))
            text.chop(3);
        text = text.trimmed();
    }
    Call call;
    if (text.startsWith(u"<function="_s)) {
        static const QRegularExpression head(uR"(^<function=([^>\n]+)>)"_s);
        static const QRegularExpression parameter(uR"(<parameter=([^>\n]+)>\n?([\s\S]*?)\n?</parameter>)"_s);
        const QRegularExpressionMatch name = head.match(text);
        if (!name.hasMatch())
            return std::nullopt;
        call.name = name.captured(1).trimmed();
        QJsonObject arguments;
        for (auto it = parameter.globalMatch(text); it.hasNext();) {
            const QRegularExpressionMatch p = it.next();
            arguments.insert(p.captured(1).trimmed(), p.captured(2));
            call.parameters.append({p.captured(1).trimmed(), p.captured(2)});
        }
        call.arguments = QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact));
        return call.name.isEmpty() ? std::nullopt : std::optional<Call>(call);
    }
    const qsizetype open = text.indexOf(u'{');
    const qsizetype close = text.lastIndexOf(u'}');
    if (open < 0 || close <= open)
        return std::nullopt;
    text = text.mid(open, close - open + 1);
    const QList<std::pair<QString, QString>> members = orderedMembers(text);
    QString arguments;
    bool named = false;
    for (const auto &[key, value] : members) {
        if (key == u"name" && !named) {
            call.name = value.trimmed();
            named = true;
        } else if ((key == u"arguments" || key == u"parameters") && arguments.isNull()) {
            arguments = value;
        }
    }
    if (call.name.isEmpty())
        return std::nullopt;
    if (!QJsonDocument::fromJson(arguments.toUtf8()).isObject())
        arguments = u"{}"_s; // (none, or no object: the tool says what it lacks)
    call.arguments = arguments;
    call.parameters = orderedMembers(arguments);
    return call;
}

/// Is `text` a tool call written without its tags — a JSON object naming one of `tools`, as Llama
/// 3 writes them unasked?
std::optional<Call> bareCall(const QString &text, const QList<Tool> &tools)
{
    const QString t = text.trimmed();
    if (!t.startsWith(u'{') || !t.endsWith(u'}'))
        return std::nullopt;
    const QJsonDocument doc = QJsonDocument::fromJson(t.toUtf8());
    if (!doc.isObject())
        return std::nullopt;
    const QJsonObject object = doc.object();
    const QString name = object.value(u"name"_s).toString();
    const bool known = std::ranges::any_of(tools, [&](const Tool &tool) { return tool.name == name; });
    if (!known || !(object.value(u"arguments"_s).isObject() || object.value(u"parameters"_s).isObject()))
        return std::nullopt;
    return callOf(t);
}

/// Is the end of `text` the start of `tag` (a tag being written)? How many of its letters.
qsizetype tagStart(const QString &text, const QString &tag)
{
    for (qsizetype n = std::min(text.size(), tag.size() - 1); n > 0; --n) {
        if (text.endsWith(QStringView(tag).left(n)))
            return n;
    }
    return 0;
}

Written written(const QString &format, const QList<Piece> &generated, const Request &request, bool complete,
                bool partial)
{
    const Kind kind = kindOf(format);
    QString all;
    for (const Piece &piece : generated)
        all += piece.text;

    Written out;
    out.thinking = kind.thinks;
    qsizetype pos = 0;
    // Its thoughts: from the start (a prompt that opened them) or from a <think> it writes first.
    bool inThoughts = kind.thinks && kind.family == u"deepseek3" && thinks(request);
    if (!inThoughts) {
        qsizetype at = 0;
        while (at < all.size() && all.at(at).isSpace())
            ++at;
        if (QStringView(all).mid(at).startsWith(kThink)) {
            inThoughts = true;
            pos = at + kThink.size();
        } else if (partial && tagStart(all.mid(at), kThink) == all.size() - at && at < all.size()) {
            return out; // (its first letters may be the tag)
        }
    }
    if (inThoughts) {
        const qsizetype end = all.indexOf(kThinkEnd, pos);
        if (end < 0) {
            QString thoughts = all.mid(pos);
            if (partial)
                thoughts.chop(tagStart(thoughts, kThinkEnd));
            if (complete && !partial) {
                out.content = thoughts; // (it never left its thoughts: what it wrote is the answer)
            } else {
                out.thoughts = thoughts;
            }
            return out;
        }
        out.thoughts = all.mid(pos, end - pos);
        out.thinking = true;
        pos = end + kThinkEnd.size();
    }

    QString rest = all.mid(pos);
    QString content;
    while (true) {
        const qsizetype at = rest.indexOf(kCall);
        if (at < 0) {
            if (partial)
                rest.chop(tagStart(rest, kCall));
            if (out.calls.isEmpty() || !rest.trimmed().isEmpty())
                content += rest;
            break;
        }
        if (out.calls.isEmpty() || !rest.left(at).trimmed().isEmpty())
            content += rest.left(at);
        if (partial)
            break; // (a call being written: not shown)
        const qsizetype bodyStart = at + kCall.size();
        const qsizetype end = rest.indexOf(kCallEnd, bodyStart);
        QString body;
        if (end < 0) {
            if (!complete)
                break;                  // cut off inside the call: not a call
            body = rest.mid(bodyStart); // (it ended its turn without the closing tag)
            rest.clear();
        } else {
            body = rest.mid(bodyStart, end - bodyStart);
            rest = rest.mid(end + kCallEnd.size());
        }
        if (const std::optional<Call> call = callOf(body))
            out.calls << *call;
        if (end < 0)
            break;
    }
    if (!partial && out.calls.isEmpty()) {
        if (const std::optional<Call> call = bareCall(content, request.tools)) {
            out.calls << *call;
            content.clear();
        }
    }
    out.content = content;
    return out;
}

// ---- The grammar ---------------------------------------------------------------------------

} // namespace

QString gbnfLiteral(const QString &text)
{
    QString out = u"\""_s;
    for (const QChar c : text) {
        if (c == u'"')
            out += u"\\\""_s;
        else if (c == u'\\')
            out += u"\\\\"_s;
        else if (c == u'\n')
            out += u"\\n"_s;
        else if (c == u'\r')
            out += u"\\r"_s;
        else if (c == u'\t')
            out += u"\\t"_s;
        else
            out += c;
    }
    return out + u'"';
}

QString ruleName(const QString &tool)
{
    QString out;
    for (const QChar c : tool) {
        if ((c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9'))
            out += c;
        else
            out += u'-';
    }
    return out;
}

QStringList jsonRules()
{
    return {
            uR"(value ::= object | array | string | number | "true" | "false" | "null")"_s,
            uR"(object ::= "{" ws ( string ws ":" ws value ( ws "," ws string ws ":" ws value )* )? ws "}")"_s,
            uR"(array ::= "[" ws ( value ( ws "," ws value )* )? ws "]")"_s,
            uR"(string ::= "\"" ( [^"\\\x7F\x00-\x1F] | "\\" ( ["\\/bfnrt] | "u" [0-9a-fA-F]{4} ) )* "\"")"_s,
            uR"(number ::= integer ( "." [0-9]{1,16} )? ( [eE] [-+]? [0-9]{1,4} )?)"_s,
            uR"(integer ::= "-"? ( "0" | [1-9] [0-9]{0,15} ))"_s,
            uR"(boolean ::= "true" | "false")"_s,
            uR"(ws ::= [ \t\n]{0,16})"_s,
    };
}

namespace {

/// A JSON value of the type a property's schema names, where it is simple.
QString valueRule(const QJsonObject &property)
{
    const QJsonArray choices = property.value(u"enum"_s).toArray();
    if (!choices.isEmpty()) {
        QStringList alternatives;
        for (const QJsonValue choice : choices) {
            if (!choice.isString())
                return u"value"_s;
            alternatives << gbnfLiteral(jsonText(choice.toString()));
        }
        return u"( "_s + alternatives.join(u" | "_s) + u" )"_s;
    }
    const QString type = property.value(u"type"_s).toString();
    if (type == u"string" || type == u"integer" || type == u"number" || type == u"boolean")
        return type;
    return u"value"_s;
}

} // namespace

QString argumentsRule(const Tool &tool, const QString &prefix, QStringList *rules)
{
    const QJsonObject properties = tool.schema.value(u"properties"_s).toObject();
    if (properties.isEmpty()) {
        rules->append(u"%1-args ::= object"_s.arg(prefix));
        return prefix + u"-args"_s;
    }
    QStringList members;
    for (auto it = properties.begin(); it != properties.end(); ++it)
        members << gbnfLiteral(jsonText(it.key())) + u" ws \":\" ws "_s + valueRule(it.value().toObject());
    rules->append(u"%1-member ::= %2"_s.arg(prefix, members.join(u" | "_s)));
    rules->append(u"%1-args ::= \"{\" ws ( %1-member ( ws \",\" ws %1-member )* )? ws \"}\""_s.arg(prefix));
    return prefix + u"-args"_s;
}

Grammar otherGrammar(const QString &format, const QList<Tool> &tools)
{
    Q_UNUSED(format) // (every other format writes its calls the same way)
    Grammar grammar;
    if (tools.isEmpty())
        return grammar;
    Piece open{kCall, true, {}};
    open.loose = true;
    Piece close{kCallEnd, true, {}};
    close.loose = true;
    grammar.tags = {open, close};
    grammar.triggers = {0};
    QStringList names;
    QStringList toolRules;
    for (qsizetype i = 0; i < tools.size(); ++i) {
        const QString prefix = u"t%1"_s.arg(i);
        QStringList own;
        const QString arguments = argumentsRule(tools.at(i), prefix, &own);
        names << prefix;
        // (Each tool: its name with its arguments, then their rules.)
        toolRules << u"%1 ::= %2 ws \",\" ws \"\\\"arguments\\\"\" ws \":\" ws %3"_s.arg(
                prefix, gbnfLiteral(jsonText(tools.at(i).name)), arguments)
                  << own;
    }
    QStringList rules;
    rules << u"root ::= call ( ws call )*"_s;
    rules << u"call ::= <[@0]> ws \"{\" ws \"\\\"name\\\"\" ws \":\" ws ( %1 ) ws \"}\" ws <[@1]>"_s.arg(
            names.join(u" | "_s));
    rules << toolRules << jsonRules();
    grammar.rules = rules.join(u'\n') + u'\n';
    return grammar;
}

bool knowsOtherFormat(const QString &format)
{
    const Kind kind = kindOf(format);
    return families().contains(kind.family) && (!kind.thinks || canThink(kind.family));
}

Prompt otherPrompt(const QString &format, const Request &request)
{
    const Kind kind = kindOf(format);
    Prompt prompt;
    if (kind.family == u"chatml")
        prompt = chatmlPrompt(request, kind.thinks, false);
    else if (kind.family == u"phi4")
        prompt = chatmlPrompt(request, false, true);
    else if (kind.family == u"llama3")
        prompt = llama3Prompt(request);
    else if (kind.family == u"gemma")
        prompt = gemmaPrompt(request);
    else if (kind.family == u"phi3")
        prompt = phi3Prompt(request);
    else if (kind.family == u"mistral")
        prompt = mistralPrompt(request);
    else if (kind.family == u"deepseek3")
        prompt = deepseekPrompt(request, kind.thinks);
    else
        return prompt;
    prompt.grammar = otherGrammar(format, request.tools);
    return prompt;
}

Reply otherReply(const QString &format, const QList<Piece> &generated, const Request &request, bool complete)
{
    const Written w = written(format, generated, request, complete, false);
    Reply reply;
    reply.ok = true;
    reply.text = w.content.trimmed();
    reply.thoughts = w.thoughts.trimmed();
    reply.stopReason = !w.calls.isEmpty() ? u"tool_calls"_s : complete ? u"stop"_s : u"length"_s;
    QJsonArray rawCalls;
    for (qsizetype c = 0; c < w.calls.size(); ++c) {
        const Call &call = w.calls.at(c);
        const QString id = u"call_%1_%2"_s.arg(request.messages.size()).arg(c + 1);
        reply.toolCalls << ToolCall{id, call.name,
                                    QJsonDocument::fromJson(call.arguments.toUtf8()).object().toVariantMap()};
        rawCalls.append(
                QJsonObject{{u"id"_s, id},
                            {u"type"_s, u"function"_s},
                            {u"function"_s, QJsonObject{{u"name"_s, call.name}, {u"arguments"_s, call.arguments}}}});
    }
    QJsonObject raw{{u"role"_s, u"assistant"_s}, {u"content"_s, w.content}};
    if (w.thinking || !w.thoughts.isEmpty())
        raw.insert(u"reasoning_content"_s, w.thoughts);
    if (!rawCalls.isEmpty())
        raw.insert(u"tool_calls"_s, rawCalls);
    reply.raw = QJsonDocument(raw).toJson(QJsonDocument::Compact);
    return reply;
}

Partial otherPartial(const QString &format, const QList<Piece> &generated, const Request &request)
{
    const Written w = written(format, generated, request, false, true);
    return Partial{w.content.trimmed(), w.thoughts.trimmed()};
}

} // namespace brulion::ai::local::detail

namespace brulion::ai::local {

using namespace Qt::StringLiterals;

QString detectFormat(const QString &chatTemplate, const QString &architecture, const QStringList &vocabularyTags)
{
    const auto has = [&](QStringView needle) { return chatTemplate.contains(needle); };
    const QString arch = architecture.toLower();
    const bool think = has(u"<think>");
    const auto withThinking = [&](const QString &family) {
        return think && detail::knowsOtherFormat(family + u"+think"_s) ? family + u"+think"_s : family;
    };
    if (arch == u"lfm3" || (arch.startsWith(u"lfm") && has(u"<function=")))
        return u"lfm3"_s;
    if (!chatTemplate.isEmpty()) {
        // In llama.cpp's order (llm_chat_detect_template), of the families written here.
        if (has(u"<|im_start|>")) {
            if (has(u"<|im_sep|>"))
                return u"phi4"_s;
            if (has(u"<end_of_utterance>"))
                return QString(); // (SmolVLM: not ChatML)
            return withThinking(u"chatml"_s);
        }
        if (chatTemplate.startsWith(u"mistral") || has(u"[INST]"))
            return has(u"[SYSTEM_PROMPT]") ? u"mistral"_s : QString();
        if (has(u"<|assistant|>") && has(u"<|end|>"))
            return u"phi3"_s;
        if (has(u"<start_of_turn>"))
            return u"gemma"_s;
        if (has(u"<|start_header_id|>") && has(u"<|end_header_id|>"))
            return u"llama3"_s;
        if (has(u"<｜Assistant｜>") && has(u"<｜User｜>") && has(u"<｜end▁of▁sentence｜>"))
            return withThinking(u"deepseek3"_s);
        return QString();
    }
    // No template: the tags its vocabulary has.
    if (vocabularyTags.contains(u"<|im_start|>"_s) && vocabularyTags.contains(u"<|im_end|>"_s))
        return vocabularyTags.contains(u"<|im_sep|>"_s) ? u"phi4"_s : u"chatml"_s;
    if (vocabularyTags.contains(u"<|start_header_id|>"_s) && vocabularyTags.contains(u"<|eot_id|>"_s))
        return u"llama3"_s;
    if (vocabularyTags.contains(u"<start_of_turn>"_s))
        return u"gemma"_s;
    return QString();
}

QString formatName(const QString &format)
{
    static const QHash<QString, QString> names{
            {u"lfm3"_s, u"LFM3"_s},       {u"chatml"_s, u"ChatML"_s},      {u"phi4"_s, u"Phi-4"_s},
            {u"llama3"_s, u"Llama 3"_s},  {u"gemma"_s, u"Gemma"_s},        {u"phi3"_s, u"Phi-3"_s},
            {u"mistral"_s, u"Mistral"_s}, {u"deepseek3"_s, u"DeepSeek"_s},
    };
    QString family = format;
    if (family.endsWith(u"+think"_s))
        family.chop(6);
    return names.value(family);
}

} // namespace brulion::ai::local
