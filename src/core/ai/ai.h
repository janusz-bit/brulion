#pragma once
// AI for extensions (docs/EXTENSIONS.md "AI"): the requests of b.ai to a model the user chose on
// this device — Anthropic's Messages API (Claude), any OpenAI-compatible chat API (OpenAI, or a
// server of the user's own: Ollama, llama.cpp, LM Studio, OpenRouter …), or a model that runs on
// the device itself (the provider "local": ai/local.h, no network). The key is the user's
// (Settings → Extensions → AI, kept with the app's secrets, never in the notebook). A consumer
// subscription (Claude Pro/Max, ChatGPT Plus) is not an API key: the providers bill the API
// separately; a server of one's own and the model on the device need no key.
//
//   QString error;
//   const std::optional<ai::Request> request = ai::requestFrom(table, &error);   // b.ai's argument
//   ai::Client client(&transport);
//   client.send(settings, *request, [](const ai::Reply &reply) { … reply.toVariant() … });
//
// Building the HTTP request and reading the answer are pure functions; Client sends through a
// sync::HttpTransport (NetworkHttpTransport in the app, a fake in the tests), and hands a request
// of the provider "local" to a LocalBackend (the app's model runner; a fake in the tests).
//
// Pictures: a user's message may carry pictures of the notebook (b.ai's `images`, named as in the
// notes). The app loads each one through an ImageLoader — found in the notebook, decoded by Qt,
// scaled down and written again as a JPEG — so no provider and no model reader ever sees a
// picture file as it came from a sync or an import.
//
// Tools (an agent's loop, docs/EXTENSIONS.md "AI"): a request may offer tools ({name,
// description, input = a JSON schema}); a reply that calls some has `toolCalls` ({id, name,
// input}) and `raw`, the assistant's turn as the provider wrote it. The caller runs the tools and
// asks again with the conversation so far: the turn sent back as `raw` — verbatim, thinking blocks
// included, which the providers check — and then a user message of `results` ({id, content,
// isError}), all of one turn's calls in one message.
//
// As it is written (the chat, docs/UI.md "AI chat"): a request sent with a `partial` gets what the
// model has written so far — its answer and its thoughts, PocketPal's way — while it writes: the
// network's providers then stream their answer (server-sent events, StreamReader), the model on
// the device hands on its tokens. The reply at the end is the same as without. send() returns a
// Pending: its stop() ends the request on its way — the connection is closed, the model on the
// device stops writing — so a stopped answer costs nothing more and holds up no other.

#include "sync/http.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace brulion::ai {

/// The longest request (system text and messages together, in characters): a script cannot send
/// a whole notebook by accident (cost).
inline constexpr int MaxRequestChars = 200000;
/// Anthropic's default answer length (non-streaming requests).
inline constexpr int DefaultMaxTokens = 16000;
/// The time one request may take (a long answer is written before anything arrives); a request
/// whose answer is shown while it comes may take longer as long as it keeps coming — this long
/// without anything new ends it.
inline constexpr int TimeoutMs = 300000;
/// The pictures one request may carry.
inline constexpr int MaxRequestImages = 8;

class LocalBackend;

struct Settings
{
    /// "anthropic" | "openai" (any compatible API) | "local" (a model on the device, ai/local.h)
    QString provider = QStringLiteral("anthropic");
    QString apiKey; ///< may be empty for a local server
    QString model;  ///< "" = defaultModel(provider); "local": a LocalModel's id
    QString baseUrl; ///< "" = the provider's own (https://api.anthropic.com, https://api.openai.com/v1)
    bool browser = false; ///< sent from a web page (Anthropic's API then needs a header for CORS)

    /// Can a request be sent? `why`: what is missing ("no API key", "no model").
    bool usable(QString *why = nullptr) const;
    /// Do requests travel unencrypted over a network: a plain http address that is not this
    /// device's own (localhost, 127.0.0.1, ::1)? The notes sent along and the key can then be
    /// read on the way — fine for a server at home, said in the settings all the same. Never for
    /// the model on the device: nothing travels.
    bool unencrypted() const;
};

/// A tool the model may call: its name (letters, digits, "_" and "-", at most 64), what it does
/// and when to call it, and its input's JSON schema (an object).
struct Tool {
    QString name;
    QString description;
    QJsonObject schema;
};

/// The result of one tool call, sent back in a user message.
struct ToolResult {
    QString id; ///< the call's id (Reply::toolCalls)
    QString content;
    bool isError = false;
};

/// A picture sent with a message: the bytes of a JPEG (or PNG) file the app wrote itself.
struct Image {
    QByteArray data;
    QString mediaType = QStringLiteral("image/jpeg");
    friend bool operator==(const Image &, const Image &) = default;
};

/// Loads the picture a script named (as in the notes: "paragon.jpg", "assets/skan.png") ready to
/// send; nothing, with `why`, when there is no such picture or it cannot be read.
using ImageLoader = std::function<std::optional<Image>(const QString &name, QString *why)>;

struct Message
{
    QString role; ///< "user" | "assistant"
    QString content;
    /// A user's message: its pictures, which the model sees before its text.
    QList<Image> images;
    /// An assistant turn as the provider wrote it (Reply::raw: Anthropic's content blocks, an
    /// OpenAI-compatible API's message), sent back verbatim instead of `content`.
    QByteArray raw;
    /// A user message of tool results instead of `content`.
    QList<ToolResult> results;
};

/// A message of text.
inline Message textMessage(const QString &role, const QString &content)
{
    Message message;
    message.role = role;
    message.content = content;
    return message;
}

struct Request
{
    QString system;
    QList<Message> messages;
    QString model; ///< "" = the settings' model
    int maxTokens = 0; ///< 0 = the provider's default (Anthropic: DefaultMaxTokens)
    QString effort; ///< "" | "low" | "medium" | "high" | "xhigh" | "max" (Anthropic's output_config.effort)
    QList<Tool> tools;
    /// The model's thinking shown where it can be (b.ai's `thoughts = true`): Claude's models
    /// that think anyway and take `thinking.display` send a summary of it (showsThinking()); a
    /// server's reasoning and the model on the device's thoughts come without asking.
    bool thoughts = false;
};

/// Does Anthropic's `model` think by itself and take `thinking: {display: "summarized"}`: Claude
/// Opus 5 and later, Sonnet 5 and later, Haiku 5.5, Fable, Mythos (Opus 4.7 and 4.8 take
/// `display` but think only when asked to; older models refuse it)?
bool showsThinking(const QString &model);

/// A tool the model asked for (Reply::toolCalls).
struct ToolCall {
    QString id;
    QString name;
    QVariantMap input; ///< the arguments ({} when the model wrote none, or no JSON object)
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
    QList<ToolCall> toolCalls; ///< the tools the model asked for (a request with tools)
    QByteArray raw;            ///< the assistant's turn as the provider wrote it (compact JSON)
    /// What the model thought before it answered, as it shows it (Claude's summary of its
    /// thinking blocks, a server's reasoning_content, the model on the device's): "" none.
    QString thoughts;

    /// b.ai's answer: {text, model, stopReason, inputTokens, outputTokens, raw, toolCalls = {{id,
    /// name, input}} (only when there are some), thoughts (only when there are some)} or {error}.
    QVariantMap toVariant() const;
};

/// What a model has written so far of an answer on its way (Client::send's `partial`).
struct Partial {
    QString text;     ///< of its answer (a tool call it writes is no part of it)
    QString thoughts; ///< what it thinks before it answers (a model that shows its thinking)
    /// The tokens written so far — thoughts and calls too — and the time that took (the model on
    /// the device: its speed; 0: not known, a provider on the network).
    int tokens = 0;
    qint64 writingMs = 0;
    friend bool operator==(const Partial &, const Partial &) = default;
};
using PartialCallback = std::function<void(const Partial &)>;

/// A request on its way (Client::send). stop() — from the thread that sent it — ends it: nothing
/// more of its answer comes, `done` gets the error "stopped" (from the event loop), the
/// provider's connection is closed and the model on the device stops writing.
class Pending {
public:
    void stop();
    bool stopped() const { return m_state && m_state->stopped->load(); }

private:
    friend class Client;
    struct State {
        std::function<void(const Reply &)> done;
        PartialCallback partial;
        bool over = false; ///< done was called (or is on its way)
        std::shared_ptr<std::atomic<bool>> stopped = std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<sync::HttpAbort> abort;
        void finish(const Reply &reply);
    };
    std::shared_ptr<State> m_state;
};

/// The model used when the settings name none: "claude-opus-5-5" for Anthropic, "" (the user
/// names one) for an OpenAI-compatible API, the first LocalModel's id for "local".
QString defaultModel(const QString &provider);

/// A request from b.ai's argument: {prompt = "…"} or {messages = {…}}, with system, model,
/// maxTokens (1–128000), effort, thoughts (a boolean) and tools ({{name, description, input =
/// schema}}); nothing when it
/// is not one (`error` says why). A message is {role, content}, an assistant's {role = "assistant",
/// raw = reply.raw} or a user's {role = "user", results = {{id, content, isError?}}}; the last one
/// is the user's. Pictures: `images = {"name", …}` next to `prompt`, or in a user's message with
/// content — at most MaxRequestImages in a request, each loaded by `images` (without a loader a
/// request with pictures is refused).
std::optional<Request> requestFrom(const QVariantMap &table, QString *error, const ImageLoader &images = {});

/// The HTTP request for `request` with `settings`; nothing when it cannot be made (`error`).
/// `stream`: the answer comes as server-sent events while it is written (StreamReader).
std::optional<sync::HttpRequest> httpRequest(const Settings &settings, const Request &request, QString *error,
                                             bool stream = false);

/// The answer to an httpRequest(): the text (Anthropic: the text blocks; OpenAI: the first
/// choice), or an error — refusals, HTTP errors with the provider's message, no connection.
Reply readReply(const Settings &settings, const sync::HttpResponse &response);

/// An answer that comes as server-sent events (httpRequest(…, stream = true)), read while it
/// comes: feed() takes the body's pieces as they arrive (an event may span pieces), partial() is
/// what the model has written so far, and message() the answer as the provider gives it at once —
/// Anthropic's message with its content blocks, an OpenAI-compatible API's completion with its
/// first choice's message (tool calls, thoughts) —, which readReply() reads.
class StreamReader {
public:
    explicit StreamReader(const QString &provider);
    /// The next piece of the body; true when partial() changed.
    bool feed(QByteArrayView piece);
    const Partial &partial() const { return m_partial; }
    /// The answer so far as one JSON object (after the last event: all of it).
    QJsonObject message() const;
    /// An error the provider sent in the stream ("": none).
    QString error() const { return m_error; }

private:
    bool event(const QByteArray &data);
    bool anthropicEvent(const QJsonObject &event);
    bool openAiEvent(const QJsonObject &event);

    bool m_anthropic = false;
    QByteArray m_line;           ///< a line not ended yet
    bool m_afterCr = false;      ///< the last character was a \r
    QByteArray m_data;           ///< the data of the event being read
    Partial m_partial;
    QString m_error;
    QJsonObject m_message;       ///< Anthropic: its message_start's message, then its deltas
    QList<QJsonObject> m_blocks; ///< Anthropic: the content blocks, by index
    QList<QString> m_inputs;     ///< Anthropic: a tool_use block's input JSON so far, by index
    // OpenAI-compatible: the first choice.
    QString m_model;
    QString m_text;
    QString m_refusal;
    QString m_thoughtsKey; ///< the server's name for them: "reasoning_content" | "reasoning"
    QString m_finish;
    QJsonObject m_usage;
    struct OpenAiCall {
        QString id;
        QString name;
        QString arguments;
    };
    QList<OpenAiCall> m_calls;
};

/// The answer to a streamed httpRequest(): its events read (StreamReader) as readReply() reads an
/// answer that came at once — which it also reads as such when a server answered at once (an
/// error, a server that does not stream).
Reply readStream(const Settings &settings, const sync::HttpResponse &response);

class Client
{
public:
    /// `transport` is not owned and must outlive the requests.
    explicit Client(sync::HttpTransport *transport) : m_transport(transport) { }

    /// What answers the provider "local" (not owned; nullptr: this build runs no model itself).
    void setLocal(LocalBackend *backend) { m_local = backend; }

    /// Sends `request`; `done` is called once, from the event loop (also for errors before sending,
    /// and after Pending::stop()). `partial`, when given, gets the answer while it is written,
    /// from the event loop, as often as it changes (a provider on the network then streams it).
    Pending send(const Settings &settings, const Request &request, std::function<void(const Reply &)> done,
                 PartialCallback partial = {});

private:
    sync::HttpTransport *m_transport;
    LocalBackend *m_local = nullptr;
};

} // namespace brulion::ai
