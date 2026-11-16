#include "ai/ai.h"

#include "ai/local.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::ai {

namespace {

const QString kAnthropic = u"anthropic"_s;
const QString kLocal = u"local"_s;
const QStringList kEfforts{u"low"_s, u"medium"_s, u"high"_s, u"xhigh"_s, u"max"_s};

/// The models that take Anthropic's server-side fallback (a declined request retried on another
/// model): the current Opus, Fable and Sonnet 5.5 (not Haiku, which has none).
bool takesFallbacks(const QString &model)
{
    static const QStringList models{u"claude-fable-5-1"_s, u"claude-opus-5-5"_s, u"claude-opus-5"_s, u"claude-sonnet-5-5"_s};
    return models.contains(model);
}

QString modelOf(const Settings &settings, const Request &request)
{
    if (!request.model.isEmpty())
        return request.model;
    return settings.model.isEmpty() ? defaultModel(settings.provider) : settings.model;
}

QString baseOf(const Settings &settings)
{
    QString base = settings.baseUrl.trimmed();
    if (base.isEmpty())
        base = settings.provider == kAnthropic ? u"https://api.anthropic.com"_s : u"https://api.openai.com/v1"_s;
    while (base.endsWith(u'/'))
        base.chop(1);
    return base;
}

/// A tool's input schema from a script's table: maps and lists as JSON objects and arrays; an
/// empty `properties` (a script's `{}`, which reads as a list) an object.
QJsonValue schemaJson(const QVariant &value, const QString &key = QString())
{
    if (value.metaType().id() == QMetaType::QVariantMap) {
        QJsonObject out;
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            out.insert(it.key(), schemaJson(it.value(), it.key()));
        return out;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        if (list.isEmpty() && key == u"properties")
            return QJsonObject();
        QJsonArray out;
        for (const QVariant &v : list)
            out.append(schemaJson(v));
        return out;
    }
    return QJsonValue::fromVariant(value);
}

/// The text of an assistant's turn of the other provider (a conversation that changed provider):
/// Anthropic's text blocks, or an OpenAI-compatible message's content.
QString rawText(const QJsonDocument &raw)
{
    if (raw.isObject())
        return raw.object().value(u"content"_s).toString();
    QString text;
    for (const QJsonValue block : raw.array()) {
        if (block.toObject().value(u"type"_s).toString() == u"text")
            text += block.toObject().value(u"text"_s).toString();
    }
    return text;
}

QString oneLine(QString text)
{
    text = text.simplified();
    return text.size() > 400 ? text.left(399) + u'…' : text;
}

/// What a failed HTTP exchange means to a user (the provider's own message when it has one).
QString httpError(int status, const QString &message)
{
    QString what;
    switch (status) {
    case 400: what = u"the request was refused"_s; break;
    case 401: what = u"the API key was refused"_s; break;
    case 402: what = u"no credit left on the API account"_s; break;
    case 403: what = u"not allowed for this key"_s; break;
    case 404: what = u"no such model or address"_s; break;
    case 413: what = u"the request is too large"_s; break;
    case 429: what = u"too many requests, or no credit left: try again later"_s; break;
    case 500: case 502: case 503: what = u"the provider had an error: try again later"_s; break;
    case 529: what = u"the provider is overloaded: try again later"_s; break;
    default: what = u"the provider answered with an error"_s;
    }
    what += u" (HTTP %1)"_s.arg(status);
    return message.isEmpty() ? what : what + u": "_s + oneLine(message);
}

} // namespace

bool Settings::usable(QString *why) const
{
    QString problem;
    if (provider == kLocal) {
        // The model's file is the app's to check (it may still be downloading).
        if (!localModel(model.trimmed()))
            problem = u"no such model on this device: \"%1\" (Settings → Extensions → AI)"_s.arg(model.trimmed());
    } else if (provider != kAnthropic && provider != u"openai")
        problem = u"unknown provider \"%1\""_s.arg(provider);
    else if (provider == kAnthropic && apiKey.trimmed().isEmpty() && baseUrl.trimmed().isEmpty())
        problem = u"no API key (Settings → Extensions → AI)"_s;
    else if (provider == u"openai" && model.trimmed().isEmpty())
        problem = u"no model (Settings → Extensions → AI)"_s;
    if (why)
        *why = problem;
    return problem.isEmpty();
}

bool Settings::unencrypted() const
{
    if (provider == kLocal)
        return false; // nothing travels: an address left from another provider is not used
    const QUrl url(baseOf(*this));
    if (url.scheme().compare(u"http"_s, Qt::CaseInsensitive) != 0)
        return false;
    const QString host = url.host().toLower();
    return host != u"localhost" && !host.endsWith(u".localhost") && !QHostAddress(host).isLoopback();
}

QVariantMap Reply::toVariant() const
{
    if (!ok)
        return {{u"error"_s, error.isEmpty() ? u"no answer"_s : error}};
    QVariantMap out{{u"text"_s, text},
                    {u"model"_s, model},
                    {u"stopReason"_s, stopReason},
                    {u"inputTokens"_s, inputTokens},
                    {u"outputTokens"_s, outputTokens},
                    {u"raw"_s, QString::fromUtf8(raw)}};
    if (!toolCalls.isEmpty()) {
        QVariantList calls;
        for (const ToolCall &call : toolCalls)
            calls << QVariantMap{{u"id"_s, call.id}, {u"name"_s, call.name}, {u"input"_s, call.input}};
        out.insert(u"toolCalls"_s, calls);
    }
    return out;
}

QString defaultModel(const QString &provider)
{
    if (provider == kLocal)
        return localModels().isEmpty() ? QString() : localModels().first().id;
    return provider == kAnthropic ? u"claude-opus-5-5"_s : QString();
}

std::optional<Request> requestFrom(const QVariantMap &table, QString *error, const ImageLoader &images)
{
    const auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return std::nullopt;
    };
    Request r;
    qint64 chars = 0;
    int pictures = 0;
    // A message's pictures, loaded: false (with the error set) when one cannot be sent.
    const auto loadImages = [&](const QVariant &names, Message *message) {
        if (!names.isValid() || names.isNull())
            return true;
        if (names.metaType().id() != QMetaType::QVariantList) {
            fail(u"images must be a list of picture names"_s);
            return false;
        }
        for (const QVariant &v : names.toList()) {
            const QString name = v.toString().trimmed();
            if (v.metaType().id() != QMetaType::QString || name.isEmpty()) {
                fail(u"images must be a list of picture names"_s);
                return false;
            }
            if (++pictures > MaxRequestImages) {
                fail(u"at most %1 pictures in a request"_s.arg(MaxRequestImages));
                return false;
            }
            if (!images) {
                fail(u"pictures cannot be sent from here"_s);
                return false;
            }
            QString why;
            const std::optional<Image> image = images(name, &why);
            if (!image || image->data.isEmpty()) {
                fail(u"the picture \"%1\": %2"_s.arg(name, why.isEmpty() ? u"cannot be read"_s : why));
                return false;
            }
            message->images << *image;
        }
        return true;
    };
    const QVariant prompt = table.value(u"prompt"_s);
    const QVariant messages = table.value(u"messages"_s);
    if (prompt.isValid() && !prompt.isNull()) {
        if (prompt.metaType().id() != QMetaType::QString)
            return fail(u"prompt must be a text"_s);
        Message message = textMessage(u"user"_s, prompt.toString());
        if (!loadImages(table.value(u"images"_s), &message))
            return std::nullopt;
        r.messages << message;
    } else if (messages.metaType().id() == QMetaType::QVariantList) {
        for (const QVariant &m : messages.toList()) {
            const QVariantMap map = m.toMap();
            const QString role = map.value(u"role"_s).toString();
            if (role != u"user" && role != u"assistant")
                return fail(u"a message's role must be \"user\" or \"assistant\""_s);
            Message message = textMessage(role, QString());
            const QVariant raw = map.value(u"raw"_s);
            const QVariant results = map.value(u"results"_s);
            if (role == u"assistant" && raw.isValid() && !raw.isNull()) {
                // A reply's turn sent back verbatim (its thinking blocks are checked by the API).
                QJsonParseError parseError;
                message.raw = raw.toString().toUtf8();
                const QJsonDocument doc = QJsonDocument::fromJson(message.raw, &parseError);
                if (raw.metaType().id() != QMetaType::QString || parseError.error != QJsonParseError::NoError
                    || (!doc.isArray() && !doc.isObject()))
                    return fail(u"an assistant's raw must be a reply's raw, unchanged"_s);
                chars += message.raw.size();
            } else if (role == u"user" && results.isValid() && !results.isNull()) {
                if (results.metaType().id() != QMetaType::QVariantList || results.toList().isEmpty())
                    return fail(u"results must be a list of {id, content, isError?}"_s);
                for (const QVariant &v : results.toList()) {
                    const QVariantMap result = v.toMap();
                    const QString id = result.value(u"id"_s).toString();
                    if (id.isEmpty() || result.value(u"content"_s).metaType().id() != QMetaType::QString)
                        return fail(u"a result needs the call's id and a content text"_s);
                    message.results << ToolResult{id, result.value(u"content"_s).toString(),
                                                  result.value(u"isError"_s).toBool()};
                    chars += message.results.last().content.size();
                }
            } else {
                if (map.value(u"content"_s).metaType().id() != QMetaType::QString)
                    return fail(u"a message's content must be a text"_s);
                message.content = map.value(u"content"_s).toString();
                if (const QVariant names = map.value(u"images"_s); names.isValid() && !names.isNull()) {
                    if (role != u"user")
                        return fail(u"only a user's message has pictures"_s);
                    if (!loadImages(names, &message))
                        return std::nullopt;
                }
            }
            r.messages << message;
        }
        if (r.messages.isEmpty() || r.messages.last().role != u"user")
            return fail(u"the last message must be the user's"_s);
    } else {
        return fail(u"the request needs a prompt (or messages)"_s);
    }
    for (const Message &m : std::as_const(r.messages))
        chars += m.content.size();
    if (const QVariant tools = table.value(u"tools"_s); tools.isValid() && !tools.isNull()) {
        static const QRegularExpression name(u"^[A-Za-z0-9_-]{1,64}$"_s);
        if (tools.metaType().id() != QMetaType::QVariantList)
            return fail(u"tools must be a list of {name, description, input}"_s);
        for (const QVariant &v : tools.toList()) {
            const QVariantMap map = v.toMap();
            Tool tool{map.value(u"name"_s).toString(), map.value(u"description"_s).toString(), {}};
            if (!name.match(tool.name).hasMatch())
                return fail(u"a tool's name must be letters, digits, _ or - (at most 64): \"%1\""_s.arg(tool.name));
            if (std::ranges::any_of(r.tools, [&](const Tool &t) { return t.name == tool.name; }))
                return fail(u"two tools are named \"%1\""_s.arg(tool.name));
            const QVariant input = map.value(u"input"_s);
            tool.schema = input.isValid() && !input.isNull() ? schemaJson(input).toObject() : QJsonObject();
            if (!tool.schema.contains(u"type"_s))
                tool.schema.insert(u"type"_s, u"object"_s);
            if (tool.schema.value(u"type"_s).toString() != u"object")
                return fail(u"a tool's input must be a schema of type \"object\""_s);
            if (!tool.schema.contains(u"properties"_s))
                tool.schema.insert(u"properties"_s, QJsonObject());
            chars += tool.description.size() + QJsonDocument(tool.schema).toJson(QJsonDocument::Compact).size();
            r.tools << tool;
        }
        if (r.tools.size() > 64)
            return fail(u"at most 64 tools"_s);
    }
    if (const QVariant system = table.value(u"system"_s); system.isValid() && !system.isNull()) {
        if (system.metaType().id() != QMetaType::QString)
            return fail(u"system must be a text"_s);
        r.system = system.toString();
        chars += r.system.size();
    }
    if (chars > MaxRequestChars)
        return fail(u"the request is too long (%1 characters; at most %2)"_s.arg(chars).arg(MaxRequestChars));
    if (const QVariant model = table.value(u"model"_s); model.isValid() && !model.isNull())
        r.model = model.toString().trimmed();
    if (const QVariant max = table.value(u"maxTokens"_s); max.isValid() && !max.isNull()) {
        bool ok = false;
        const double n = max.toDouble(&ok);
        if (!ok || !std::isfinite(n) || n < 1 || n > 128000 || n != std::floor(n))
            return fail(u"maxTokens must be a whole number from 1 to 128000"_s);
        r.maxTokens = int(n);
    }
    if (const QVariant effort = table.value(u"effort"_s); effort.isValid() && !effort.isNull()) {
        r.effort = effort.toString();
        if (!kEfforts.contains(r.effort))
            return fail(u"effort must be one of: "_s + kEfforts.join(u", "_s));
    }
    return r;
}

std::optional<sync::HttpRequest> httpRequest(const Settings &settings, const Request &request, QString *error,
                                             bool stream)
{
    QString why;
    if (!settings.usable(&why)) {
        if (error)
            *error = why;
        return std::nullopt;
    }
    if (settings.provider == kLocal) {
        // Answered on the device (Client::send hands it to the LocalBackend): nothing to send.
        if (error)
            *error = u"the model on this device takes no network request"_s;
        return std::nullopt;
    }
    const QString model = modelOf(settings, request);
    sync::HttpRequest http;
    http.method = "POST";
    http.timeoutMs = TimeoutMs;
    http.headers.append(std::pair<QByteArray, QByteArray>("content-type", "application/json"));
    QJsonArray messages;
    QJsonObject body{{u"model"_s, model}};
    if (settings.provider == kAnthropic) {
        // Anthropic's Messages API (docs: platform.claude.com, "Messages").
        http.url = QUrl(baseOf(settings) + u"/v1/messages"_s);
        if (!settings.apiKey.isEmpty())
            http.headers.append(std::pair<QByteArray, QByteArray>("x-api-key", settings.apiKey.trimmed().toUtf8()));
        http.headers.append(std::pair<QByteArray, QByteArray>("anthropic-version", "2026-10-21"));
        if (settings.browser)
            http.headers.append(std::pair<QByteArray, QByteArray>("anthropic-dangerous-direct-browser-access", "true"));
        for (const Message &m : request.messages) {
            if (!m.raw.isEmpty()) {
                // The assistant's content blocks as they came (thinking blocks included).
                const QJsonDocument raw = QJsonDocument::fromJson(m.raw);
                messages.append(QJsonObject{
                        {u"role"_s, u"assistant"_s},
                        {u"content"_s, raw.isArray() ? QJsonValue(raw.array()) : QJsonValue(rawText(raw))}});
            } else if (!m.results.isEmpty()) {
                // Every result of one turn in one message.
                QJsonArray blocks;
                for (const ToolResult &result : m.results) {
                    QJsonObject block{{u"type"_s, u"tool_result"_s},
                                      {u"tool_use_id"_s, result.id},
                                      {u"content"_s, result.content}};
                    if (result.isError)
                        block.insert(u"is_error"_s, true);
                    blocks.append(block);
                }
                messages.append(QJsonObject{{u"role"_s, u"user"_s}, {u"content"_s, blocks}});
            } else if (!m.images.isEmpty()) {
                // The pictures first, then the text about them.
                QJsonArray blocks;
                for (const Image &image : m.images)
                    blocks.append(QJsonObject{
                            {u"type"_s, u"image"_s},
                            {u"source"_s, QJsonObject{{u"type"_s, u"base64"_s},
                                                      {u"media_type"_s, image.mediaType},
                                                      {u"data"_s, QString::fromLatin1(image.data.toBase64())}}}});
                if (!m.content.isEmpty())
                    blocks.append(QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, m.content}});
                messages.append(QJsonObject{{u"role"_s, m.role}, {u"content"_s, blocks}});
            } else {
                messages.append(QJsonObject{{u"role"_s, m.role}, {u"content"_s, m.content}});
            }
        }
        body.insert(u"max_tokens"_s, request.maxTokens > 0 ? request.maxTokens : DefaultMaxTokens);
        body.insert(u"messages"_s, messages);
        if (!request.tools.isEmpty()) {
            QJsonArray tools;
            for (const Tool &tool : request.tools)
                tools.append(QJsonObject{{u"name"_s, tool.name},
                                         {u"description"_s, tool.description},
                                         {u"input_schema"_s, tool.schema}});
            body.insert(u"tools"_s, tools);
        }
        if (!request.system.isEmpty())
            body.insert(u"system"_s, request.system);
        if (!request.effort.isEmpty())
            body.insert(u"output_config"_s, QJsonObject{{u"effort"_s, request.effort}});
        // A request a model's safeguards decline is answered by the model Anthropic recommends
        // for that kind of request instead of coming back as a refusal.
        if (takesFallbacks(model)) {
            http.headers.append(std::pair<QByteArray, QByteArray>("anthropic-beta", "server-side-fallback-2026-07-01"));
            body.insert(u"fallbacks"_s, u"default"_s);
        }
    } else {
        // An OpenAI-compatible chat API.
        const QString base = baseOf(settings);
        http.url = QUrl(base + u"/chat/completions"_s);
        if (!settings.apiKey.trimmed().isEmpty())
            http.headers.append(std::pair<QByteArray, QByteArray>("authorization", "Bearer " + settings.apiKey.trimmed().toUtf8()));
        if (!request.system.isEmpty())
            messages.append(QJsonObject{{u"role"_s, u"system"_s}, {u"content"_s, request.system}});
        for (const Message &m : request.messages) {
            if (!m.raw.isEmpty()) {
                // The assistant's message as it came (its tool_calls included).
                const QJsonDocument raw = QJsonDocument::fromJson(m.raw);
                messages.append(raw.isObject()
                                        ? raw.object()
                                        : QJsonObject{{u"role"_s, u"assistant"_s}, {u"content"_s, rawText(raw)}});
            } else if (!m.results.isEmpty()) {
                for (const ToolResult &result : m.results)
                    messages.append(QJsonObject{
                            {u"role"_s, u"tool"_s},
                            {u"tool_call_id"_s, result.id},
                            {u"content"_s, result.isError ? u"Error: "_s + result.content : result.content}});
            } else if (!m.images.isEmpty()) {
                // The pictures (data: addresses) first, then the text about them.
                QJsonArray parts;
                for (const Image &image : m.images)
                    parts.append(QJsonObject{
                            {u"type"_s, u"image_url"_s},
                            {u"image_url"_s,
                             QJsonObject{{u"url"_s,
                                          u"data:%1;base64,%2"_s.arg(image.mediaType,
                                                                     QString::fromLatin1(image.data.toBase64()))}}}});
                if (!m.content.isEmpty())
                    parts.append(QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, m.content}});
                messages.append(QJsonObject{{u"role"_s, m.role}, {u"content"_s, parts}});
            } else {
                messages.append(QJsonObject{{u"role"_s, m.role}, {u"content"_s, m.content}});
            }
        }
        body.insert(u"messages"_s, messages);
        if (!request.tools.isEmpty()) {
            QJsonArray tools;
            for (const Tool &tool : request.tools)
                tools.append(QJsonObject{{u"type"_s, u"function"_s},
                                         {u"function"_s, QJsonObject{{u"name"_s, tool.name},
                                                                     {u"description"_s, tool.description},
                                                                     {u"parameters"_s, tool.schema}}}});
            body.insert(u"tools"_s, tools);
        }
        // OpenAI's own API takes max_completion_tokens (its newer models refuse max_tokens);
        // compatible servers take max_tokens.
        if (request.maxTokens > 0)
            body.insert(http.url.host() == u"api.openai.com" ? u"max_completion_tokens"_s : u"max_tokens"_s,
                        request.maxTokens);
        // (The counts come in a last event of their own.)
        if (stream)
            body.insert(u"stream_options"_s, QJsonObject{{u"include_usage"_s, true}});
    }
    if (stream) {
        body.insert(u"stream"_s, true);
        http.timeoutWhileIdle = true; // (a long answer goes on as long as it keeps coming)
    }
    if (!http.url.isValid() || (http.url.scheme() != u"https" && http.url.scheme() != u"http")) {
        if (error)
            *error = u"not a web address: "_s + baseOf(settings);
        return std::nullopt;
    }
    http.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return http;
}

Reply readReply(const Settings &settings, const sync::HttpResponse &response)
{
    Reply reply;
    if (response.status == 0) {
        reply.error = response.timedOut ? u"no answer in %1 minutes"_s.arg(TimeoutMs / 60000)
                                        : u"no connection: "_s + oneLine(response.networkError);
        return reply;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &parseError);
    const QJsonObject json = doc.object();
    if (response.status < 200 || response.status >= 300) {
        // Anthropic: {"type": "error", "error": {"type", "message"}}; OpenAI: {"error": {"message"}}.
        const QJsonValue err = json.value(u"error"_s);
        const QString message = err.isObject() ? err.toObject().value(u"message"_s).toString() : err.toString();
        reply.error = httpError(response.status, message);
        return reply;
    }
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        reply.error = u"the answer is not JSON"_s;
        return reply;
    }
    reply.model = json.value(u"model"_s).toString();
    if (settings.provider == kAnthropic) {
        reply.stopReason = json.value(u"stop_reason"_s).toString();
        if (reply.stopReason == u"refusal") {
            reply.error = u"the model declined to answer"_s;
            return reply;
        }
        QString text;
        const QJsonArray content = json.value(u"content"_s).toArray();
        for (const QJsonValue block : content) {
            const QJsonObject b = block.toObject();
            const QString type = b.value(u"type"_s).toString();
            if (type == u"text")
                text += b.value(u"text"_s).toString();
            else if (type == u"tool_use")
                reply.toolCalls << ToolCall{b.value(u"id"_s).toString(), b.value(u"name"_s).toString(),
                                            b.value(u"input"_s).toObject().toVariantMap()};
        }
        reply.text = text;
        reply.raw = QJsonDocument(content).toJson(QJsonDocument::Compact);
        const QJsonObject usage = json.value(u"usage"_s).toObject();
        reply.inputTokens = usage.value(u"input_tokens"_s).toInteger();
        reply.outputTokens = usage.value(u"output_tokens"_s).toInteger();
    } else {
        const QJsonArray choices = json.value(u"choices"_s).toArray();
        if (choices.isEmpty()) {
            reply.error = u"the answer has no text"_s;
            return reply;
        }
        const QJsonObject first = choices.first().toObject();
        reply.stopReason = first.value(u"finish_reason"_s).toString();
        const QJsonValue content = first.value(u"message"_s).toObject().value(u"content"_s);
        if (first.value(u"message"_s).toObject().contains(u"refusal"_s)
            && !first.value(u"message"_s).toObject().value(u"refusal"_s).isNull()) {
            reply.error = u"the model declined to answer: "_s
                          + oneLine(first.value(u"message"_s).toObject().value(u"refusal"_s).toString());
            return reply;
        }
        reply.text = content.toString();
        const QJsonObject message = first.value(u"message"_s).toObject();
        for (const QJsonValue v : message.value(u"tool_calls"_s).toArray()) {
            const QJsonObject call = v.toObject();
            const QJsonObject function = call.value(u"function"_s).toObject();
            // The arguments are a JSON text (an object), not an object.
            const QJsonDocument arguments = QJsonDocument::fromJson(function.value(u"arguments"_s).toString().toUtf8());
            reply.toolCalls << ToolCall{call.value(u"id"_s).toString(), function.value(u"name"_s).toString(),
                                        arguments.isObject() ? arguments.object().toVariantMap() : QVariantMap()};
        }
        reply.raw = QJsonDocument(message).toJson(QJsonDocument::Compact);
        const QJsonObject usage = json.value(u"usage"_s).toObject();
        reply.inputTokens = usage.value(u"prompt_tokens"_s).toInteger();
        reply.outputTokens = usage.value(u"completion_tokens"_s).toInteger();
    }
    reply.ok = true;
    return reply;
}

// ---- An answer read while it comes ---------------------------------------------------------------

StreamReader::StreamReader(const QString &provider) : m_anthropic(provider == kAnthropic) { }

bool StreamReader::feed(QByteArrayView piece)
{
    // Server-sent events: lines ending in \n, \r\n or \r; "data: …" lines, joined, make an event's
    // data, an empty line ends it; "event:", "id:", comments (":") and the rest are not needed.
    bool changed = false;
    const auto line = [&](QByteArrayView text) {
        if (text.isEmpty()) {
            if (!m_data.isEmpty())
                changed = event(std::exchange(m_data, {})) || changed;
            return;
        }
        if (!text.startsWith("data:"))
            return;
        QByteArrayView value = text.sliced(5);
        if (value.startsWith(' '))
            value = value.sliced(1);
        if (!m_data.isEmpty())
            m_data += '\n';
        m_data += value;
    };
    for (const char c : piece) {
        if (c == '\r' || (c == '\n' && !m_afterCr)) {
            line(m_line);
            m_line.clear();
        } else if (c != '\n') {
            m_line += c;
        }
        m_afterCr = c == '\r'; // (the \n of a \r\n ends no second line)
    }
    return changed;
}

bool StreamReader::event(const QByteArray &data)
{
    if (data == "[DONE]")
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return false;
    const QJsonObject event = doc.object();
    // Anthropic: {"type": "error", "error": {"type", "message"}}; OpenAI: {"error": {"message"}}.
    if (const QJsonValue err = event.value(u"error"_s); !err.isUndefined() && !err.isNull()) {
        m_error = err.isObject() ? err.toObject().value(u"message"_s).toString() : err.toString();
        if (m_error.isEmpty())
            m_error = u"the provider stopped the answer"_s;
        return false;
    }
    return m_anthropic ? anthropicEvent(event) : openAiEvent(event);
}

bool StreamReader::anthropicEvent(const QJsonObject &event)
{
    const QString type = event.value(u"type"_s).toString();
    const qsizetype index = event.value(u"index"_s).toInteger(-1);
    if (type == u"message_start") {
        m_message = event.value(u"message"_s).toObject();
        return false;
    }
    if (type == u"content_block_start" && index >= 0 && index < 4096) {
        while (m_blocks.size() <= index) {
            m_blocks.append(QJsonObject());
            m_inputs.append(QString());
        }
        m_blocks[index] = event.value(u"content_block"_s).toObject();
        const QString kind = m_blocks[index].value(u"type"_s).toString();
        if (kind == u"text" || kind == u"thinking") {
            const QString key = kind == u"text" ? u"text"_s : u"thinking"_s;
            const QString start = m_blocks[index].value(key).toString();
            if (start.isEmpty())
                return false;
            (kind == u"text" ? m_partial.text : m_partial.thoughts) += start;
            return true;
        }
        return false;
    }
    if (type == u"content_block_delta" && index >= 0 && index < m_blocks.size()) {
        QJsonObject &block = m_blocks[index];
        const QJsonObject delta = event.value(u"delta"_s).toObject();
        const QString kind = delta.value(u"type"_s).toString();
        const auto append = [&](const QString &key, const QString &more) {
            block.insert(key, QString(block.value(key).toString() + more));
        };
        if (kind == u"text_delta") {
            const QString more = delta.value(u"text"_s).toString();
            append(u"text"_s, more);
            m_partial.text += more;
            return !more.isEmpty();
        }
        if (kind == u"thinking_delta") {
            const QString more = delta.value(u"thinking"_s).toString();
            append(u"thinking"_s, more);
            m_partial.thoughts += more;
            return !more.isEmpty();
        }
        if (kind == u"signature_delta")
            append(u"signature"_s, delta.value(u"signature"_s).toString());
        else if (kind == u"input_json_delta")
            m_inputs[index] += delta.value(u"partial_json"_s).toString();
        else if (kind == u"citations_delta") {
            QJsonArray citations = block.value(u"citations"_s).toArray();
            citations.append(delta.value(u"citation"_s));
            block.insert(u"citations"_s, citations);
        }
        return false;
    }
    if (type == u"content_block_stop" && index >= 0 && index < m_blocks.size()) {
        QJsonObject &block = m_blocks[index];
        if (block.value(u"type"_s).toString() == u"tool_use" || block.value(u"type"_s).toString() == u"server_tool_use") {
            // The input as one JSON object ("" when the call takes no arguments).
            const QJsonDocument input = QJsonDocument::fromJson(m_inputs[index].toUtf8());
            if (input.isObject())
                block.insert(u"input"_s, input.object());
            else if (m_inputs[index].trimmed().isEmpty() && !block.value(u"input"_s).isObject())
                block.insert(u"input"_s, QJsonObject());
        }
        return false;
    }
    if (type == u"message_delta") {
        const QJsonObject delta = event.value(u"delta"_s).toObject();
        for (auto it = delta.constBegin(); it != delta.constEnd(); ++it)
            m_message.insert(it.key(), it.value());
        // The counts so far (output_tokens: all of the answer's).
        QJsonObject usage = m_message.value(u"usage"_s).toObject();
        const QJsonObject more = event.value(u"usage"_s).toObject();
        for (auto it = more.constBegin(); it != more.constEnd(); ++it) {
            if (!it.value().isNull())
                usage.insert(it.key(), it.value());
        }
        m_message.insert(u"usage"_s, usage);
    }
    return false;
}

bool StreamReader::openAiEvent(const QJsonObject &event)
{
    if (const QString model = event.value(u"model"_s).toString(); !model.isEmpty())
        m_model = model;
    if (const QJsonValue usage = event.value(u"usage"_s); usage.isObject())
        m_usage = usage.toObject();
    const QJsonArray choices = event.value(u"choices"_s).toArray();
    bool changed = false;
    for (const QJsonValue value : choices) {
        const QJsonObject choice = value.toObject();
        if (choice.value(u"index"_s).toInteger(0) != 0)
            continue; // (the first choice is the answer)
        const QJsonObject delta = choice.value(u"delta"_s).toObject();
        if (const QString more = delta.value(u"content"_s).toString(); !more.isEmpty()) {
            m_text += more;
            m_partial.text += more;
            changed = true;
        }
        if (const QString more = delta.value(u"refusal"_s).toString(); !more.isEmpty())
            m_refusal += more;
        // Thoughts: llama.cpp's server, vLLM and DeepSeek say reasoning_content, others reasoning.
        for (const QString &key : {u"reasoning_content"_s, u"reasoning"_s}) {
            const QString more = delta.value(key).toString();
            if (more.isEmpty())
                continue;
            m_thoughtsKey = key;
            m_partial.thoughts += more;
            changed = true;
        }
        for (const QJsonValue v : delta.value(u"tool_calls"_s).toArray()) {
            const QJsonObject call = v.toObject();
            const QJsonObject function = call.value(u"function"_s).toObject();
            // A call by its index; a server that gives none: a new call when it has an id.
            qsizetype at = call.value(u"index"_s).toInteger(-1);
            if (at < 0)
                at = call.contains(u"id"_s) || m_calls.isEmpty() ? m_calls.size() : m_calls.size() - 1;
            if (at >= 256)
                continue;
            while (m_calls.size() <= at)
                m_calls.append(OpenAiCall());
            OpenAiCall &c = m_calls[at];
            if (const QString id = call.value(u"id"_s).toString(); !id.isEmpty())
                c.id = id;
            if (const QString name = function.value(u"name"_s).toString(); !name.isEmpty())
                c.name += name;
            c.arguments += function.value(u"arguments"_s).toString();
        }
        if (const QString finish = choice.value(u"finish_reason"_s).toString(); !finish.isEmpty())
            m_finish = finish;
    }
    return changed;
}

QJsonObject StreamReader::message() const
{
    if (m_anthropic) {
        QJsonObject message = m_message;
        // After a fallback in the middle of the answer (the server's: the same request on another
        // model, on the same stream), what came before its last `fallback` block is sent back
        // without the declined part's thinking and tool calls — its text is the answer's start.
        qsizetype boundary = -1;
        for (qsizetype i = 0; i < m_blocks.size(); ++i) {
            if (m_blocks.at(i).value(u"type"_s).toString() == u"fallback")
                boundary = i;
        }
        QJsonArray content;
        for (qsizetype i = 0; i < m_blocks.size(); ++i) {
            const QJsonObject &block = m_blocks.at(i);
            if (block.isEmpty())
                continue;
            const QString type = block.value(u"type"_s).toString();
            if (i < boundary && type != u"text" && type != u"fallback")
                continue;
            content.append(block);
        }
        message.insert(u"content"_s, content);
        return message;
    }
    QJsonObject message{{u"role"_s, u"assistant"_s}};
    message.insert(u"content"_s, !m_text.isEmpty() || m_calls.isEmpty() ? QJsonValue(m_text) : QJsonValue());
    if (!m_partial.thoughts.isEmpty())
        message.insert(m_thoughtsKey, m_partial.thoughts);
    if (!m_refusal.isEmpty())
        message.insert(u"refusal"_s, m_refusal);
    if (!m_calls.isEmpty()) {
        QJsonArray calls;
        for (const OpenAiCall &call : m_calls)
            calls.append(QJsonObject{{u"id"_s, call.id},
                                     {u"type"_s, u"function"_s},
                                     {u"function"_s, QJsonObject{{u"name"_s, call.name}, {u"arguments"_s, call.arguments}}}});
        message.insert(u"tool_calls"_s, calls);
    }
    QJsonObject completion{{u"model"_s, m_model},
                           {u"choices"_s, QJsonArray{QJsonObject{{u"index"_s, 0},
                                                                 {u"message"_s, message},
                                                                 {u"finish_reason"_s, m_finish}}}}};
    if (!m_usage.isEmpty())
        completion.insert(u"usage"_s, m_usage);
    return completion;
}

Reply readStream(const Settings &settings, const sync::HttpResponse &response)
{
    if (response.timedOut) {
        Reply reply;
        reply.error = u"nothing more came for %1 minutes"_s.arg(TimeoutMs / 60000);
        return reply;
    }
    // An error's answer (JSON), no answer at all, or a server that answered at once.
    if (response.status < 200 || response.status >= 300 || response.body.trimmed().startsWith('{'))
        return readReply(settings, response);
    StreamReader reader(settings.provider);
    reader.feed(response.body);
    reader.feed("\n\n"); // (a last event without its empty line)
    if (!reader.error().isEmpty()) {
        Reply reply;
        reply.error = u"the provider stopped the answer: "_s + oneLine(reader.error());
        return reply;
    }
    sync::HttpResponse whole = response;
    whole.body = QJsonDocument(reader.message()).toJson(QJsonDocument::Compact);
    return readReply(settings, whole);
}

// ---- Requests -----------------------------------------------------------------------------------

void Pending::State::finish(const Reply &reply)
{
    if (over)
        return;
    over = true;
    partial = {};
    if (std::function<void(const Reply &)> f = std::exchange(done, {}))
        f(reply);
}

void Pending::stop()
{
    if (!m_state || m_state->over)
        return;
    m_state->stopped->store(true);
    m_state->partial = {};
    // Said at once — whatever the transport or the model do now is not waited for.
    std::function<void(const Reply &)> done = std::exchange(m_state->done, {});
    m_state->over = true;
    if (m_state->abort)
        m_state->abort->abort();
    Reply reply;
    reply.error = u"stopped"_s;
    QTimer::singleShot(0, [done = std::move(done), reply] {
        if (done)
            done(reply);
    });
}

Pending Client::send(const Settings &settings, const Request &request, std::function<void(const Reply &)> done,
                     PartialCallback partial)
{
    Pending pending;
    pending.m_state = std::make_shared<Pending::State>();
    const std::shared_ptr<Pending::State> state = pending.m_state;
    state->done = std::move(done);
    state->partial = std::move(partial);
    const auto fail = [&](const QString &why) {
        Reply reply;
        reply.error = why;
        // From the event loop, as an answer would come.
        QTimer::singleShot(0, [state, reply] { state->finish(reply); });
        return pending;
    };
    // (Partial answers go through `state`: none after the end or a stop.)
    PartialCallback onPartial;
    if (state->partial)
        onPartial = [state](const Partial &p) {
            if (state->over || !state->partial)
                return;
            const PartialCallback f = state->partial; // (it may stop the request)
            f(p);
        };

    QString error;
    if (settings.provider == kLocal && settings.usable(&error)) {
        if (!m_local)
            return fail(u"this build of Brulion runs no model on the device"_s);
        m_local->send(settings, request, [state](const Reply &reply) { state->finish(reply); }, onPartial,
                      state->stopped);
        return pending;
    }
    const bool stream = bool(onPartial);
    std::optional<sync::HttpRequest> http = httpRequest(settings, request, &error, stream);
    if (!http || !m_transport)
        return fail(error.isEmpty() ? u"no network here"_s : error);
    state->abort = std::make_shared<sync::HttpAbort>();
    http->abort = state->abort;
    if (stream) {
        auto reader = std::make_shared<StreamReader>(settings.provider);
        http->onData = [reader, onPartial](QByteArrayView piece) {
            if (reader->feed(piece))
                onPartial(reader->partial());
        };
    }
    m_transport->send(*http, [settings, state, stream](const sync::HttpResponse &response) {
        state->finish(stream ? readStream(settings, response) : readReply(settings, response));
    });
    return pending;
}

} // namespace brulion::ai
