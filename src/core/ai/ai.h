#pragma once
// AI for extensions (docs/EXTENSIONS.md "AI"): the requests of b.ai to a model the user chose on
// this device — Anthropic's Messages API (Claude) or any OpenAI-compatible chat API (OpenAI, or a
// server of the user's own: Ollama, llama.cpp, LM Studio, OpenRouter …). The key is the user's
// (Settings → Extensions → AI, kept with the app's secrets, never in the notebook). A consumer
// subscription (Claude Pro/Max, ChatGPT Plus) is not an API key: the providers bill the API
// separately; a server of one's own needs no key.
//
//   QString error;
//   const std::optional<ai::Request> request = ai::requestFrom(table, &error);   // b.ai's argument
//   ai::Client client(&transport);
//   client.send(settings, *request, [](const ai::Reply &reply) { … reply.toVariant() … });
//
// Building the HTTP request and reading the answer are pure functions; Client sends through a
// sync::HttpTransport (NetworkHttpTransport in the app, a fake in the tests).

#include "sync/http.h"

#include <QList>
#include <QString>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace brulion::ai {

/// The longest request (system text and messages together, in characters): a script cannot send
/// a whole notebook by accident (cost).
inline constexpr int MaxRequestChars = 200000;
/// Anthropic's default answer length (non-streaming requests).
inline constexpr int DefaultMaxTokens = 16000;
/// The time one request may take (a long answer is written before anything arrives).
inline constexpr int TimeoutMs = 300000;

struct Settings
{
    QString provider = QStringLiteral("anthropic"); ///< "anthropic" | "openai" (any compatible API)
    QString apiKey; ///< may be empty for a local server
    QString model; ///< "" = defaultModel(provider)
    QString baseUrl; ///< "" = the provider's own (https://api.anthropic.com, https://api.openai.com/v1)
    bool browser = false; ///< sent from a web page (Anthropic's API then needs a header for CORS)

    /// Can a request be sent? `why`: what is missing ("no API key", "no model").
    bool usable(QString *why = nullptr) const;
};

struct Message
{
    QString role; ///< "user" | "assistant"
    QString content;
};

struct Request
{
    QString system;
    QList<Message> messages;
    QString model; ///< "" = the settings' model
    int maxTokens = 0; ///< 0 = the provider's default (Anthropic: DefaultMaxTokens)
    QString effort; ///< "" | "low" | "medium" | "high" | "xhigh" | "max" (Anthropic's output_config.effort)
};

struct Reply
{
    bool ok = false;
    QString text;
    QString error; ///< one line: the provider's message when it gave one
    QString model;
    QString stopReason; ///< "end_turn", "max_tokens", "stop", "length" …
    qint64 inputTokens = 0;
    qint64 outputTokens = 0;

    /// b.ai's answer: {text, model, stopReason, inputTokens, outputTokens} or {error}.
    QVariantMap toVariant() const;
};

/// The model used when the settings name none: "claude-opus-5-5" for Anthropic, "" (the user
/// names one) for an OpenAI-compatible API.
QString defaultModel(const QString &provider);

/// A request from b.ai's argument: {prompt = "…"} or {messages = {{role, content}, …}}, with
/// system, model, maxTokens (1–128000) and effort; nothing when it is not one (`error` says why).
std::optional<Request> requestFrom(const QVariantMap &table, QString *error);

/// The HTTP request for `request` with `settings`; nothing when it cannot be made (`error`).
std::optional<sync::HttpRequest> httpRequest(const Settings &settings, const Request &request, QString *error);

/// The answer to an httpRequest(): the text (Anthropic: the text blocks; OpenAI: the first
/// choice), or an error — refusals, HTTP errors with the provider's message, no connection.
Reply readReply(const Settings &settings, const sync::HttpResponse &response);

class Client
{
public:
    /// `transport` is not owned and must outlive the requests.
    explicit Client(sync::HttpTransport *transport) : m_transport(transport) { }

    /// Sends `request`; `done` is called once, from the event loop (also for errors before sending).
    void send(const Settings &settings, const Request &request, std::function<void(const Reply &)> done);

private:
    sync::HttpTransport *m_transport;
};

} // namespace brulion::ai
