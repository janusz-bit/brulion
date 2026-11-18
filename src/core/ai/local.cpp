#include "ai/local.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::ai {

namespace {

// Liquid AI's models on Hugging Face, each pinned to one revision of its repository (the files
// behind a revision never change) with the SHA-256 the download is checked against.
QUrl liquidFile(const QString &repository, const QString &revision, const QString &file)
{
    return QUrl(u"https://huggingface.co/LiquidAI/%1/resolve/%2/%3"_s.arg(repository, revision, file));
}

QList<LocalModel> knownModels()
{
    const QString license = u"LFM Open License v1.0"_s;

    // Sees pictures: the language model (4-bit, trained for that precision) and its projector — the
    // weakest of them at working through a notebook.
    LocalModel vision;
    vision.id = u"lfm3-vl-2b"_s;
    vision.name = u"LFM3-VL 2B"_s;
    vision.maker = u"Liquid AI"_s;
    vision.format = u"lfm3"_s;
    const QString visionRevision = u"1add221a5ea6c047fcf1eaaf2d247939d7cf8849"_s;
    vision.file = {u"LFM3-VL-2B-QAD-Q4_0.gguf"_s,
                   liquidFile(u"LFM3-VL-2B-GGUF"_s, visionRevision, u"LFM3-VL-2B-QAD-Q4_0.gguf"_s),
                   "621c455376cb96a20891697deb9975c6fb47dd5ea98ed3ad1f3b7e8f24f0b1e6", 1049615648};
    vision.projector = {u"mmproj-LFM3-VL-2B-Q8_0.gguf"_s,
                        liquidFile(u"LFM3-VL-2B-GGUF"_s, visionRevision, u"mmproj-LFM3-VL-2B-Q8_0.gguf"_s),
                        "c5c5b58d544a18da919172d2d0368090ea1a4a351b6feb7f8a326a264a5918ab", 203031872};
    vision.license = license;
    vision.licenseUrl = QUrl(u"https://huggingface.co/LiquidAI/LFM3-VL-2B-GGUF/blob/%1/LICENSE"_s.arg(visionRevision));
    vision.pageUrl = QUrl(u"https://huggingface.co/LiquidAI/LFM3-VL-2B"_s);

    // Text only: better than the vision model at working through a notebook with tools.
    LocalModel text;
    text.id = u"lfm3-1.7b"_s;
    text.name = u"LFM3 1.7B"_s;
    text.maker = u"Liquid AI"_s;
    text.format = u"lfm3"_s;
    const QString textRevision = u"cd0c74c575b5c416f3011b5b4c298077f26ca8c2"_s;
    text.file = {u"LFM3-1.7B-QAD-Q4_0.gguf"_s,
                 liquidFile(u"LFM3-1.7B-GGUF"_s, textRevision, u"LFM3-1.7B-QAD-Q4_0.gguf"_s),
                 "43a7d09a0e9cba91b63eb7052488b9564569bddfd65754d2e426465e572a7452", 1049615680};
    text.license = license;
    text.licenseUrl = QUrl(u"https://huggingface.co/LiquidAI/LFM3-1.7B-GGUF/blob/%1/LICENSE"_s.arg(textRevision));
    text.pageUrl = QUrl(u"https://huggingface.co/LiquidAI/LFM3-1.7B"_s);

    // The text model trained further for the built-in assistant, without thinking (SFT, then
    // GRPO: docs/FINETUNE.md): a LoRA adapter over the text model's very file — downloaded once
    // for both. The best of them with the assistant, so the default (first). Liquid AI's model under its licence still (a Derivative Work), not made by them.
    // Brulion's own repository on Hugging Face (docs/FINETUNE.md "Publishing"), pinned the same
    // way: one revision for the adapter and the LICENSE beside it.
    const QString assistantRepository = u"https://huggingface.co/janusz-bit/brulion-assistant-1.7b"_s;
    const QString assistantRevision = u"4764e842038721cc7cc52cda67e9feff6474baa6"_s;
    LocalModel assistant;
    assistant.id = u"brulion-assistant-1.7b"_s;
    assistant.name = u"Brulion Assistant 1.7B"_s;
    assistant.label = u"Brulion"_s; // (the three names side by side would not fit a phone of 360 px)
    assistant.maker = u"Brulion"_s;
    assistant.baseName = text.name;
    assistant.baseMaker = text.maker;
    assistant.format = text.format;
    assistant.file = text.file;
    const QString adapter = u"brulion-assistant-1.7b-lora.gguf"_s;
    assistant.adapter = {adapter, QUrl(u"%1/resolve/%2/%3"_s.arg(assistantRepository, assistantRevision, adapter)),
                         "abd6fff7106c375c6f1d28927f3e77c96be36f09211a58585d755e7d5777b452", 96654336};
    assistant.license = license;
    assistant.licenseUrl = QUrl(u"%1/blob/%2/LICENSE"_s.arg(assistantRepository, assistantRevision));
    assistant.pageUrl = QUrl(assistantRepository);
    assistant.thinks = false; // (trained to answer at once)

    return {assistant, text, vision};
}

} // namespace

const QList<LocalModel> &localModels()
{
    static const QList<LocalModel> models = knownModels();
    return models;
}

const LocalModel *localModel(const QString &id)
{
    const QList<LocalModel> &models = localModels();
    if (id.isEmpty())
        return models.isEmpty() ? nullptr : &models.first();
    for (const LocalModel &model : models) {
        if (model.id == id)
            return &model;
    }
    return nullptr;
}

namespace local {

namespace {

// ---- LFM3 (Liquid AI): ChatML turns, the tools as JSON lines in the system turn, tool calls as
// XML. Written after the model's own chat template (chat_template.jinja of LiquidAI/LFM3-1.7B and
// LFM3-VL-2B; tests/core/ai compares with what llama.cpp renders from it). Every tag below is one
// token of the model's vocabulary.
const QString kLfm3 = u"lfm3"_s;
const QString kBos = u"<|startoftext|>"_s;
const QString kTurnStart = u"<|im_start|>"_s;
const QString kTurnEnd = u"<|im_end|>"_s;
const QString kThink = u"<think>"_s;
const QString kThinkEnd = u"</think>"_s;
const QString kTools = u"<tools>"_s;
const QString kToolsEnd = u"</tools>"_s;
const QString kCall = u"<tool_call>"_s;
const QString kCallEnd = u"</tool_call>"_s;
const QString kFunction = u"<function="_s;
const QString kFunctionEnd = u"</function>"_s;
const QString kParameter = u"<parameter="_s;
const QString kParameterEnd = u"</parameter>"_s;
const QString kResponse = u"<tool_response>"_s;
const QString kResponseEnd = u"</tool_response>"_s;

/// What marks a text shorten() cut.
const QString kCut = u"\n[… cut: the model on this device cannot read more at once …]\n"_s;
/// shorten() leaves at least this much of a text.
constexpr qsizetype MinKept = 400;

/// A JSON string literal of `text` (non-ASCII letters as they are).
QString jsonText(const QString &text)
{
    const QByteArray array = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(array.mid(1, array.size() - 2));
}

/// The text of a JSON string literal.
QString unquoted(QStringView literal)
{
    return QJsonDocument::fromJson((u'[' + literal + u']').toUtf8()).array().at(0).toString();
}

/// JSON as the model's template writes it (Python's json.dumps: ", " and ": ").
QString spacedJson(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Bool: return value.toBool() ? u"true"_s : u"false"_s;
    case QJsonValue::Double: {
        const double d = value.toDouble();
        if (std::isfinite(d) && d == std::floor(d) && std::abs(d) < 9.0e15)
            return QString::number(qint64(d));
        return QString::number(d, 'g', QLocale::FloatingPointShortest);
    }
    case QJsonValue::String: return jsonText(value.toString());
    case QJsonValue::Array: {
        QStringList parts;
        for (const QJsonValue element : value.toArray())
            parts << spacedJson(element);
        return u'[' + parts.join(u", "_s) + u']';
    }
    case QJsonValue::Object: {
        QStringList parts;
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            parts << jsonText(it.key()) + u": "_s + spacedJson(it.value());
        return u'{' + parts.join(u", "_s) + u'}';
    }
    case QJsonValue::Null:
    case QJsonValue::Undefined: break;
    }
    return u"null"_s;
}

/// The members of a JSON object's text in the order written, each name with its value as the
/// model writes a tool's parameter: a string's text, anything else as it stands.
QList<std::pair<QString, QString>> orderedMembers(const QString &json)
{
    QList<std::pair<QString, QString>> out;
    const qsizetype n = json.size();
    qsizetype i = 0;
    const auto skipSpace = [&] {
        while (i < n && json.at(i).isSpace())
            ++i;
    };
    // Past the closing quote of the string literal that starts at `from`; -1 when it never ends.
    const auto stringEnd = [&](qsizetype from) -> qsizetype {
        for (qsizetype j = from + 1; j < n; ++j) {
            if (json.at(j) == u'\\')
                ++j;
            else if (json.at(j) == u'"')
                return j + 1;
        }
        return -1;
    };
    skipSpace();
    if (i >= n || json.at(i) != u'{')
        return out;
    ++i;
    while (true) {
        skipSpace();
        if (i >= n || json.at(i) != u'"')
            break; // the closing brace, or not JSON
        const qsizetype keyEnd = stringEnd(i);
        if (keyEnd < 0)
            break;
        const QString key = unquoted(QStringView(json).mid(i, keyEnd - i));
        i = keyEnd;
        skipSpace();
        if (i >= n || json.at(i) != u':')
            break;
        ++i;
        skipSpace();
        if (i >= n)
            break;
        QString value;
        if (json.at(i) == u'"') {
            const qsizetype end = stringEnd(i);
            if (end < 0)
                break;
            value = unquoted(QStringView(json).mid(i, end - i));
            i = end;
        } else {
            // A number, true, false, null, a list or an object: up to the comma or brace after it.
            const qsizetype start = i;
            int depth = 0;
            while (i < n) {
                const QChar c = json.at(i);
                if (c == u'"') {
                    const qsizetype end = stringEnd(i);
                    i = end < 0 ? n : end;
                    continue;
                }
                if (c == u'{' || c == u'[') {
                    ++depth;
                } else if (c == u'}' || c == u']') {
                    if (depth == 0)
                        break;
                    --depth;
                } else if (c == u',' && depth == 0) {
                    break;
                }
                ++i;
            }
            value = json.mid(start, i - start).trimmed();
        }
        out.append({key, value});
        skipSpace();
        if (i >= n || json.at(i) != u',')
            break;
        ++i;
    }
    return out;
}

/// A tool call of an assistant's turn: its parameters in the order written, as the model's text.
struct Call {
    QString id;
    QString name;
    QList<std::pair<QString, QString>> parameters;
};

/// An assistant's turn as the prompt writes it.
struct Turn {
    bool thought = false; ///< it has thoughts (possibly none written)
    QString thoughts;
    QString content;
    QList<Call> calls;
};

/// A message's turn: from a reply's raw (an OpenAI-style message — this module's own, or an
/// OpenAI-compatible provider's), from another provider's blocks (their text), or its plain text.
Turn turnOf(const Message &message)
{
    Turn turn;
    if (message.raw.isEmpty()) {
        turn.content = message.content;
        return turn;
    }
    const QJsonDocument raw = QJsonDocument::fromJson(message.raw);
    if (!raw.isObject()) {
        for (const QJsonValue block : raw.array()) {
            if (block.toObject().value(u"type"_s).toString() == u"text")
                turn.content += block.toObject().value(u"text"_s).toString();
        }
        return turn;
    }
    const QJsonObject object = raw.object();
    turn.content = object.value(u"content"_s).toString();
    if (const QJsonValue thoughts = object.value(u"reasoning_content"_s); thoughts.isString()) {
        turn.thought = true;
        turn.thoughts = thoughts.toString();
    }
    for (const QJsonValue value : object.value(u"tool_calls"_s).toArray()) {
        const QJsonObject function = value.toObject().value(u"function"_s).toObject();
        const QJsonValue arguments = function.value(u"arguments"_s);
        Call call;
        call.id = value.toObject().value(u"id"_s).toString();
        call.name = function.value(u"name"_s).toString();
        // A text of JSON (OpenAI's way); some servers send the object itself.
        call.parameters = orderedMembers(
                arguments.isObject()
                        ? QString::fromUtf8(QJsonDocument(arguments.toObject()).toJson(QJsonDocument::Compact))
                        : arguments.toString());
        turn.calls << call;
    }
    return turn;
}

/// A prompt's pieces as they are added: texts next to each other are one piece (a model's
/// tokenizer reads the text between two control tokens as a whole).
class Writer {
public:
    void tag(const QString &token) { m_pieces.append(Piece{token, true, {}}); }
    void text(const QString &text)
    {
        if (text.isEmpty())
            return;
        if (!m_pieces.isEmpty() && !m_pieces.last().special && m_pieces.last().image.isEmpty())
            m_pieces.last().text += text;
        else
            m_pieces.append(Piece{text, false, {}});
    }
    void image(const QByteArray &data) { m_pieces.append(Piece{QString(), false, data}); }
    qsizetype size() const { return m_pieces.size(); }
    QList<Piece> pieces() const { return m_pieces; }

private:
    QList<Piece> m_pieces;
};

Prompt lfm3Prompt(const Request &request)
{
    Prompt prompt;
    prompt.thinking = thinks(request);
    prompt.stops = {kTurnStart, kBos};
    Writer out;
    out.tag(kBos);

    // The system turn: the system text, then every tool as a line of JSON.
    if (!request.system.isEmpty() || !request.tools.isEmpty()) {
        out.tag(kTurnStart);
        out.text(u"system\n"_s + request.system);
        if (!request.tools.isEmpty()) {
            QStringList lines;
            for (const Tool &tool : request.tools)
                lines << u"{\"name\": %1, \"description\": %2, \"parameters\": %3}"_s.arg(
                        jsonText(tool.name), jsonText(tool.description), spacedJson(tool.schema));
            if (!request.system.isEmpty())
                out.text(u"\n\n"_s);
            out.tag(kTools);
            out.text(u'\n' + lines.join(u'\n') + u'\n');
            out.tag(kToolsEnd);
        }
        out.tag(kTurnEnd);
        out.text(u"\n"_s);
    }
    prompt.stable = int(out.size());

    // The model's thoughts stay only in the turns after the user's last message (the tool loop of
    // the question being answered): earlier ones are dropped, as its template does.
    qsizetype lastUser = -1;
    for (qsizetype i = 0; i < request.messages.size(); ++i) {
        const Message &m = request.messages.at(i);
        if (m.role == u"user" && m.results.isEmpty())
            lastUser = i;
    }

    QStringList lastCallIds; // of the assistant's turn before a message of results
    for (qsizetype i = 0; i < request.messages.size(); ++i) {
        const Message &m = request.messages.at(i);
        if (m.role == u"assistant") {
            const Turn turn = turnOf(m);
            out.tag(kTurnStart);
            out.text(u"assistant\n"_s);
            out.tag(kThink);
            if (turn.thought && i > lastUser)
                out.text(turn.thoughts);
            out.tag(kThinkEnd);
            out.text(turn.content);
            lastCallIds.clear();
            for (qsizetype c = 0; c < turn.calls.size(); ++c) {
                const Call &call = turn.calls.at(c);
                lastCallIds << call.id;
                if (c > 0)
                    out.text(u"\n\n"_s);
                out.tag(kCall);
                out.text(u"\n"_s);
                out.tag(kFunction);
                out.text(call.name + u">\n"_s);
                for (const auto &[name, value] : call.parameters) {
                    out.tag(kParameter);
                    out.text(name + u">\n"_s + value + u'\n');
                    out.tag(kParameterEnd);
                    out.text(u"\n"_s);
                }
                out.tag(kFunctionEnd);
                out.text(u"\n"_s);
                out.tag(kCallEnd);
            }
            out.tag(kTurnEnd);
            out.text(u"\n"_s);
        } else if (!m.results.isEmpty()) {
            // Each result a turn of its own, in the order of the calls it answers.
            QList<ToolResult> results = m.results;
            std::stable_sort(results.begin(), results.end(), [&](const ToolResult &a, const ToolResult &b) {
                const qsizetype ia = lastCallIds.indexOf(a.id);
                const qsizetype ib = lastCallIds.indexOf(b.id);
                return (ia < 0 ? lastCallIds.size() : ia) < (ib < 0 ? lastCallIds.size() : ib);
            });
            for (const ToolResult &result : std::as_const(results)) {
                out.tag(kTurnStart);
                out.text(u"tool\n"_s);
                out.tag(kResponse);
                out.text(u'\n' + (result.isError ? u"Error: "_s + result.content : result.content) + u'\n');
                out.tag(kResponseEnd);
                out.tag(kTurnEnd);
                out.text(u"\n"_s);
            }
        } else {
            out.tag(kTurnStart);
            out.text(u"user\n"_s);
            for (const Image &image : m.images)
                out.image(image.data);
            out.text(m.content);
            out.tag(kTurnEnd);
            out.text(u"\n"_s);
        }
    }

    // Where the answer starts: inside its thoughts, or after none.
    out.tag(kTurnStart);
    out.text(u"assistant\n"_s);
    out.tag(kThink);
    if (!prompt.thinking)
        out.tag(kThinkEnd);
    prompt.pieces = out.pieces();
    return prompt;
}

/// The type a tool's schema gives a parameter ("string", "integer" …; "" when it gives none).
QString parameterType(const QList<Tool> &tools, const QString &tool, const QString &parameter)
{
    for (const Tool &t : tools) {
        if (t.name != tool)
            continue;
        const QJsonValue type = t.schema.value(u"properties"_s).toObject().value(parameter).toObject().value(u"type"_s);
        if (type.isString())
            return type.toString();
        for (const QJsonValue v : type.toArray()) {
            if (v.toString() != u"null")
                return v.toString();
        }
    }
    return QString();
}

/// A parameter's text as a JSON value of the type its tool's schema names: a text stays a text
/// ("5" for a string parameter), a number, a truth value, a list or an object is read as one —
/// and written back as the model wrote it. Without a type, what reads as JSON is JSON.
QString valueJson(const QString &text, const QString &type)
{
    const QString trimmed = text.trimmed();
    const QJsonDocument doc = QJsonDocument::fromJson((u'[' + trimmed + u']').toUtf8());
    const bool valid = doc.isArray() && doc.array().size() == 1;
    const QJsonValue value = valid ? doc.array().at(0) : QJsonValue();
    if (type == u"string")
        return jsonText(text);
    if (type == u"integer" || type == u"number")
        return valid && value.isDouble() ? trimmed : jsonText(text);
    if (type == u"boolean") {
        const QString lower = trimmed.toLower();
        return lower == u"true" || lower == u"false" ? lower : jsonText(text);
    }
    if (type == u"array")
        return valid && value.isArray() ? trimmed : jsonText(text);
    if (type == u"object")
        return valid && value.isObject() ? trimmed : jsonText(text);
    return valid && !value.isString() ? trimmed : jsonText(text);
}

/// What the model wrote, read: its thoughts, its words, the tools it calls.
struct Written {
    bool thinking = false; ///< the request let it think
    QString thoughts;
    QString content;
    QList<Call> calls;
};

Written lfm3Written(const QList<Piece> &generated, const Request &request, bool complete)
{
    const bool thinking = thinks(request);
    bool inThoughts = thinking;
    QString thoughts;
    QString content;
    QList<Call> calls;

    const qsizetype n = generated.size();
    const auto isTag = [&](qsizetype i, const QString &tag) {
        return i < n && generated.at(i).special && generated.at(i).text == tag;
    };
    const auto isText = [&](qsizetype i) { return i < n && !generated.at(i).special; };

    qsizetype i = 0;
    while (i < n) {
        const Piece &piece = generated.at(i);
        if (!piece.special) {
            if (inThoughts)
                thoughts += piece.text;
            else if (calls.isEmpty() || !piece.text.trimmed().isEmpty())
                content += piece.text; // not the blank lines between two calls
            ++i;
            continue;
        }
        if (piece.text == kThinkEnd) {
            inThoughts = false;
            ++i;
            continue;
        }
        if (piece.text == kThink) {
            // Thinking again before anything was said.
            if (content.trimmed().isEmpty() && calls.isEmpty())
                inThoughts = true;
            ++i;
            continue;
        }
        if (piece.text != kCall)
            break; // a turn's start, a tool's response …: the model left its turn; nothing more is read
        inThoughts = false;

        // <tool_call> <function= NAME> ( <parameter= NAME> VALUE </parameter> )* </function> </tool_call>
        qsizetype j = i + 1;
        if (isText(j) && generated.at(j).text.trimmed().isEmpty())
            ++j;
        Call call;
        bool closed = false;
        if (isTag(j, kFunction)) {
            ++j;
            if (isText(j)) {
                const QString &head = generated.at(j).text;
                const qsizetype gt = head.indexOf(u'>');
                call.name = (gt < 0 ? head : head.left(gt)).trimmed();
                ++j;
            }
            while (j < n) {
                if (isTag(j, kParameter)) {
                    ++j;
                    QString name;
                    QString value;
                    if (isText(j)) {
                        const QString &body = generated.at(j).text;
                        const qsizetype gt = body.indexOf(u'>');
                        name = (gt < 0 ? body : body.left(gt)).trimmed();
                        value = gt < 0 ? QString() : body.mid(gt + 1);
                        // The value stands on lines of its own: "<parameter=name>\nvalue\n</parameter>".
                        if (value.startsWith(u'\n'))
                            value.remove(0, 1);
                        if (value.endsWith(u'\n'))
                            value.chop(1);
                        ++j;
                    }
                    if (isTag(j, kParameterEnd))
                        ++j;
                    else if (j >= n)
                        break; // cut off inside the value: not a call
                    if (!name.isEmpty())
                        call.parameters.append({name, value});
                } else if (isText(j) && generated.at(j).text.trimmed().isEmpty()) {
                    ++j;
                } else if (isTag(j, kFunctionEnd)) {
                    ++j;
                    closed = true;
                    break;
                } else {
                    break;
                }
            }
        }
        if (isText(j) && generated.at(j).text.trimmed().isEmpty() && isTag(j + 1, kCallEnd))
            ++j;
        if (isTag(j, kCallEnd))
            ++j;
        if (closed && !call.name.isEmpty())
            calls << call;
        // (A call that was never finished — the answer ran out of tokens — is dropped.)
        i = std::max(j, i + 1);
    }

    // It ended its turn without ever leaving its thoughts: what it wrote is the answer.
    if (inThoughts && complete && calls.isEmpty()) {
        content = thoughts;
        thoughts.clear();
    }
    return Written{thinking, thoughts, content, calls};
}

Reply lfm3Reply(const QList<Piece> &generated, const Request &request, bool complete)
{
    const Written written = lfm3Written(generated, request, complete);
    const bool thinking = written.thinking;
    const QString &thoughts = written.thoughts;
    const QString &content = written.content;
    const QList<Call> &calls = written.calls;

    Reply reply;
    reply.ok = true;
    reply.text = content.trimmed();
    reply.stopReason = !calls.isEmpty() ? u"tool_calls"_s : complete ? u"stop"_s : u"length"_s;

    QJsonArray rawCalls;
    for (qsizetype c = 0; c < calls.size(); ++c) {
        const Call &call = calls.at(c);
        QStringList members;
        for (const auto &[name, value] : call.parameters)
            members << jsonText(name) + u": "_s + valueJson(value, parameterType(request.tools, call.name, name));
        const QString arguments = u'{' + members.join(u", "_s) + u'}';
        // Unique in the conversation: the turn's number (its messages so far) and the call's.
        const QString id = u"call_%1_%2"_s.arg(request.messages.size()).arg(c + 1);
        reply.toolCalls << ToolCall{id, call.name, QJsonDocument::fromJson(arguments.toUtf8()).object().toVariantMap()};
        rawCalls.append(QJsonObject{{u"id"_s, id},
                                    {u"type"_s, u"function"_s},
                                    {u"function"_s, QJsonObject{{u"name"_s, call.name}, {u"arguments"_s, arguments}}}});
    }
    QJsonObject raw{{u"role"_s, u"assistant"_s}, {u"content"_s, content}};
    if (thinking)
        raw.insert(u"reasoning_content"_s, thoughts);
    if (!rawCalls.isEmpty())
        raw.insert(u"tool_calls"_s, rawCalls);
    reply.raw = QJsonDocument(raw).toJson(QJsonDocument::Compact);
    return reply;
}

/// `count` characters from the start of `text`, never half of a letter outside the basic plane.
QString leftOf(const QString &text, qsizetype count)
{
    count = std::clamp<qsizetype>(count, 0, text.size());
    if (count > 0 && count < text.size() && text.at(count - 1).isHighSurrogate())
        --count;
    return text.left(count);
}

QString rightOf(const QString &text, qsizetype count)
{
    count = std::clamp<qsizetype>(count, 0, text.size());
    if (count > 0 && count < text.size() && text.at(text.size() - count).isLowSurrogate())
        --count;
    return text.right(count);
}

/// A tool's result cut to `keep` characters: its end goes.
QString cutEnd(QString text, qsizetype keep)
{
    if (text.endsWith(kCut))
        text.chop(kCut.size());
    return leftOf(text, keep) + kCut;
}

/// A message cut to `keep` characters: its middle goes (its start and its end — the question —
/// stay); a message cut before loses more around the same place.
QString cutMiddle(const QString &text, qsizetype keep)
{
    const qsizetype at = text.indexOf(kCut);
    if (at < 0) {
        const qsizetype head = keep * 6 / 10;
        return leftOf(text, head) + kCut + rightOf(text, keep - head);
    }
    const QString head = text.left(at);
    const QString tail = text.mid(at + kCut.size());
    const qsizetype total = std::max<qsizetype>(1, head.size() + tail.size());
    const qsizetype keptHead = keep * head.size() / total;
    return leftOf(head, keptHead) + kCut + rightOf(tail, keep - keptHead);
}

} // namespace

bool knowsFormat(const QString &format)
{
    return format == kLfm3;
}

bool thinks(const Request &request)
{
    return request.effort != u"low";
}

bool hasImages(const Request &request)
{
    return std::ranges::any_of(request.messages, [](const Message &m) { return !m.images.isEmpty(); });
}

Prompt prompt(const QString &format, const Request &request)
{
    if (format == kLfm3)
        return lfm3Prompt(request);
    return Prompt();
}

Reply reply(const QString &format, const QList<Piece> &generated, const Request &request, bool complete)
{
    if (format == kLfm3)
        return lfm3Reply(generated, request, complete);
    Reply reply;
    reply.error = u"unknown model format \"%1\""_s.arg(format);
    return reply;
}

Partial partial(const QString &format, const QList<Piece> &generated, const Request &request)
{
    if (format != kLfm3)
        return Partial();
    const Written written = lfm3Written(generated, request, /*complete*/ false);
    // (A text in front of a call that is being written may be its start: the blank before it.)
    return Partial{written.content.trimmed(), written.thoughts.trimmed()};
}

int shorten(Request *request, int chars)
{
    qsizetype removed = 0;
    while (removed < chars) {
        // The longest text that may go: a tool's result, or a message's own text.
        QString *longest = nullptr;
        bool result = false;
        for (Message &m : request->messages) {
            for (ToolResult &r : m.results) {
                if (!longest || r.content.size() > longest->size()) {
                    longest = &r.content;
                    result = true;
                }
            }
            if (m.raw.isEmpty() && m.results.isEmpty() && (!longest || m.content.size() > longest->size())) {
                longest = &m.content;
                result = false;
            }
        }
        if (!longest)
            break;
        const qsizetype before = longest->size();
        const qsizetype keep = std::max(MinKept, before - (chars - removed) - kCut.size());
        if (keep + kCut.size() >= before)
            break; // nothing long enough is left
        *longest = result ? cutEnd(*longest, keep) : cutMiddle(*longest, keep);
        if (longest->size() >= before)
            break;
        removed += before - longest->size();
    }
    return int(removed);
}

} // namespace local

} // namespace brulion::ai
