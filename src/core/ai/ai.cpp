#include "ai/ai.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::ai {

namespace {

const QString kAnthropic = u"anthropic"_s;
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
    if (provider != kAnthropic && provider != u"openai")
        problem = u"unknown provider \"%1\""_s.arg(provider);
    else if (provider == kAnthropic && apiKey.trimmed().isEmpty() && baseUrl.trimmed().isEmpty())
        problem = u"no API key (Settings → Extensions → AI)"_s;
    else if (provider == u"openai" && model.trimmed().isEmpty())
        problem = u"no model (Settings → Extensions → AI)"_s;
    if (why)
        *why = problem;
    return problem.isEmpty();
}

QVariantMap Reply::toVariant() const
{
    if (!ok)
        return {{u"error"_s, error.isEmpty() ? u"no answer"_s : error}};
    return {{u"text"_s, text},
            {u"model"_s, model},
            {u"stopReason"_s, stopReason},
            {u"inputTokens"_s, inputTokens},
            {u"outputTokens"_s, outputTokens}};
}

QString defaultModel(const QString &provider) { return provider == kAnthropic ? u"claude-opus-5-5"_s : QString(); }

std::optional<Request> requestFrom(const QVariantMap &table, QString *error)
{
    const auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return std::nullopt;
    };
    Request r;
    qint64 chars = 0;
    const QVariant prompt = table.value(u"prompt"_s);
    const QVariant messages = table.value(u"messages"_s);
    if (prompt.isValid() && !prompt.isNull()) {
        if (prompt.metaType().id() != QMetaType::QString)
            return fail(u"prompt must be a text"_s);
        r.messages << Message{u"user"_s, prompt.toString()};
    } else if (messages.metaType().id() == QMetaType::QVariantList) {
        for (const QVariant &m : messages.toList()) {
            const QVariantMap map = m.toMap();
            const QString role = map.value(u"role"_s).toString();
            if (role != u"user" && role != u"assistant")
                return fail(u"a message's role must be \"user\" or \"assistant\""_s);
            if (map.value(u"content"_s).metaType().id() != QMetaType::QString)
                return fail(u"a message's content must be a text"_s);
            r.messages << Message{role, map.value(u"content"_s).toString()};
        }
        if (r.messages.isEmpty() || r.messages.last().role != u"user")
            return fail(u"the last message must be the user's"_s);
    } else {
        return fail(u"the request needs a prompt (or messages)"_s);
    }
    for (const Message &m : std::as_const(r.messages))
        chars += m.content.size();
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

std::optional<sync::HttpRequest> httpRequest(const Settings &settings, const Request &request, QString *error)
{
    QString why;
    if (!settings.usable(&why)) {
        if (error)
            *error = why;
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
        for (const Message &m : request.messages)
            messages.append(QJsonObject{{u"role"_s, m.role}, {u"content"_s, m.content}});
        body.insert(u"max_tokens"_s, request.maxTokens > 0 ? request.maxTokens : DefaultMaxTokens);
        body.insert(u"messages"_s, messages);
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
        for (const Message &m : request.messages)
            messages.append(QJsonObject{{u"role"_s, m.role}, {u"content"_s, m.content}});
        body.insert(u"messages"_s, messages);
        // OpenAI's own API takes max_completion_tokens (its newer models refuse max_tokens);
        // compatible servers take max_tokens.
        if (request.maxTokens > 0)
            body.insert(http.url.host() == u"api.openai.com" ? u"max_completion_tokens"_s : u"max_tokens"_s,
                        request.maxTokens);
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
        for (const QJsonValue block : json.value(u"content"_s).toArray()) {
            const QJsonObject b = block.toObject();
            if (b.value(u"type"_s).toString() == u"text")
                text += b.value(u"text"_s).toString();
        }
        reply.text = text;
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
        const QJsonObject usage = json.value(u"usage"_s).toObject();
        reply.inputTokens = usage.value(u"prompt_tokens"_s).toInteger();
        reply.outputTokens = usage.value(u"completion_tokens"_s).toInteger();
    }
    reply.ok = true;
    return reply;
}

void Client::send(const Settings &settings, const Request &request, std::function<void(const Reply &)> done)
{
    QString error;
    const std::optional<sync::HttpRequest> http = httpRequest(settings, request, &error);
    if (!http || !m_transport) {
        Reply reply;
        reply.error = error.isEmpty() ? u"no network here"_s : error;
        // From the event loop, as an answer would come.
        QTimer::singleShot(0, [done = std::move(done), reply] { done(reply); });
        return;
    }
    m_transport->send(*http, [settings, done = std::move(done)](const sync::HttpResponse &response) {
        done(readReply(settings, response));
    });
}

} // namespace brulion::ai
