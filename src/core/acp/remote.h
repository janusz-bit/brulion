#pragma once
// The Agent Client Protocol to an agent on another computer (docs/CLI.md "AI agents for other
// devices"): `brulion-cli acp serve` there (src/cli/acphost.h) starts the agent in its copy of the
// notebook, serves the agent that copy's files and its terminals, gives each session Brulion's
// tools, and relays every other message of the protocol; this side — the app on a phone — keeps the
// conversation with acp::Client exactly as with an agent of its own, only the bytes go over HTTPS
// (the host behind a reverse proxy, reachable in the user's own network, a tailnet) instead of a
// program's standard input and output.
//
// The host's API, JSON over HTTP (RemoteApiPrefix):
//
//   POST   pair {code, device}                → {token, device: {id, name}}   (no token: a pairing code)
//   GET    info                               → {protocol, version, host, root, tools,
//                                                agents: [{id, name, command}], sync}
//   POST   streams {agent}                    → {stream, root} — the agent's process started for this device
//   POST   streams/<id>/send {from, messages} → {received} — messages to the agent; `from` is the number
//          of the first one among all this stream sent, so a batch sent again (its answer lost) is not
//          delivered twice
//   GET    streams/<id>/receive?after=N&wait=S → {messages: [{seq, message}], closed, why} — the agent's
//          messages after N (those up to N are dropped on the host), at once or within S seconds (a
//          long poll)
//   DELETE streams/<id>                       → the agent's process ends
//   GET    sessions/<id>/turn?wait=S          → {state, text, tools, stopReason, error, asking} — the last
//          turn of a session, for a device that lost the stream it ran in (RemoteTurn)
//   DELETE sessions/<id>/turn                 → the agent stops that turn
//
// Every request but pair carries "Authorization: Bearer <token>". A stream survives a phone that
// sleeps or loses the network for a while: the host keeps what the agent says until it is fetched.
//
//   RemoteStream stream(&transport, base, token, u"claude"_s);
//   acp::Client client([&](const QByteArray &line) { stream.write(line); });
//   connect(&stream, &RemoteStream::received, &client, &acp::Client::received);
//   connect(&stream, &RemoteStream::ended, …);   // → client.closed(the reason in words)
//   stream.start();                               // → started(root): the host's notebook folder
//
// Pure protocol over sync::HttpTransport (tests play the host); single-threaded.

#include "sync/http.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <optional>

namespace brulion::acp {

/// The version of the host's API this side speaks (RemoteInfo::protocol).
inline constexpr int RemoteProtocol = 1;
/// Where the API is under the host's address ("https://agent.example.com" → …/acp/v1/info).
inline constexpr QLatin1StringView RemoteApiPrefix{"acp/v1/"};
/// The longest a receive waits on the host for something to say (seconds).
inline constexpr int RemoteWaitSeconds = 25;
/// The messages sent in one request at most.
inline constexpr int RemoteMaxBatch = 64;

/// The address of `endpoint` ("info", "streams/s_1/send") under the host's address `base` (its path
/// kept: "https://example.com/brulion" → https://example.com/brulion/acp/v1/info).
QUrl remoteUrl(const QUrl &base, const QString &endpoint);
/// The host's address as the user typed it, made one: a scheme added ("agent.example.com" →
/// https://agent.example.com), the path's last "/" and any "/acp/v1/…" dropped. Invalid: an empty
/// QUrl (also for a scheme other than http and https, user info, a query).
QUrl normalizeRemoteBase(const QString &typed);
/// A pairing code as the user typed it, made one: letters upper case, spaces and dashes left out
/// ("abcd-efgh" → "ABCDEFGH").
QString normalizePairingCode(const QString &typed);

/// An agent the host offers.
struct RemoteAgent {
    QString id;      ///< the host's id of it ("claude")
    QString name;    ///< as the chat shows it ("Claude Code")
    QString command; ///< what the host runs (for a sign-in in a terminal there)
    friend bool operator==(const RemoteAgent &, const RemoteAgent &) = default;
};

/// What the host says of itself (GET info).
struct RemoteInfo {
    int protocol = 0;
    QString version;    ///< brulion-cli's
    QString host;       ///< the computer's name
    QString root;       ///< the host's notebook folder (absolute, the agent's cwd)
    bool tools = false; ///< sessions get Brulion's MCP server there
    QList<RemoteAgent> agents;
    /// The host's sync with Nextcloud: {enabled, running, last (ISO time), ok, error}.
    QJsonObject sync;
    static std::optional<RemoteInfo> fromJson(const QJsonObject &o);
};

/// Why a request to the host failed, for the user's words.
struct RemoteError {
    enum class Kind {
        None,
        Network,      ///< no answer (detail: the transport's words)
        Unauthorized, ///< 401: the device is not paired (any more)
        Refused,      ///< another 4xx (detail: the host's message)
        Server,       ///< 5xx (detail: the host's message)
        Malformed,    ///< an answer that is not the API's (detail: what)
        Version,      ///< the host speaks another version of the API (detail: its version)
    };
    Kind kind = Kind::None;
    int status = 0;
    QString code; ///< the host's error code ("bad_code", "unknown_agent" …)
    QString detail;
    bool failed() const { return kind != Kind::None; }
    friend bool operator==(const RemoteError &, const RemoteError &) = default;
};

/// The error of an answer (RemoteError::Kind::None for a 2xx answer that is a JSON object; the
/// object in `object`).
RemoteError remoteErrorOf(const sync::HttpResponse &response, QJsonObject *object = nullptr);

/// A device paired with the host (POST pair).
struct RemotePairing {
    QString token;
    QString deviceId;
    QString deviceName;
};

/// POST pair: `code` (normalizePairingCode()) for a token; `device` the name the host lists it by.
void pairRemote(sync::HttpTransport *transport, const QUrl &base, const QString &code, const QString &device,
                std::function<void(const RemotePairing &pairing, const RemoteError &error)> done);
/// GET info.
void fetchRemoteInfo(sync::HttpTransport *transport, const QUrl &base, const QString &token,
                     std::function<void(const RemoteInfo &info, const RemoteError &error)> done);

/// The last turn of a session on the host (GET sessions/<id>/turn): how a turn went on whose stream
/// this device lost — Android ended the app meanwhile, the network was gone for long.
struct RemoteTurn {
    enum class State { Running, Done, Failed };
    State state = State::Running;
    QString text;       ///< what the agent said
    QStringList tools;  ///< the titles of its tool calls
    QString stopReason; ///< Done: the agent's ("end_turn", "cancelled" …)
    QString error;      ///< Failed: why (the host's words)
    bool asking = false;
    static std::optional<RemoteTurn> fromJson(const QJsonObject &o);
};

/// GET sessions/<session>/turn, waiting up to `waitSeconds` (≤ RemoteWaitSeconds) while it runs. The
/// host takes it that this device lost the stream of the turn: questions of the agent in it are
/// answered "cancelled" from now on, and the stream ends with the turn. A turn the host does not
/// know (it was started again, an old host): RemoteError::Kind::Refused, status 404.
void fetchRemoteTurn(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &session,
                     int waitSeconds, std::function<void(const RemoteTurn &turn, const RemoteError &error)> done);
/// DELETE sessions/<session>/turn: the agent stops it.
void cancelRemoteTurn(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &session);

/// One agent's process on the host and the protocol's lines to and from it.
class RemoteStream : public QObject {
    Q_OBJECT
public:
    /// Why a stream ended.
    enum class End {
        AgentEnded,   ///< the agent's process ended on the host (detail: its last words)
        Unauthorized, ///< the device is not paired any more
        Gone,         ///< the host no longer knows the stream (it was restarted, or it gave the stream up)
        Unreachable,  ///< the host could not be reached for longer than unreachableMs() (detail: the last failure)
        Refused,      ///< the host refused to start the agent (detail: its message; code: its error code)
        Version,      ///< the host speaks another version of the API
    };
    Q_ENUM(End)

    RemoteStream(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &agent,
                 QObject *parent = nullptr);
    ~RemoteStream() override;

    /// Starts the agent on the host (POST streams), then receives from it.
    void start();
    /// A line of the protocol to the agent (a JSON-RPC message, its "\n" or not): sent in order,
    /// in batches (the lines written before the event loop goes on go together), again after a
    /// failure — once each.
    void write(const QByteArray &line);
    /// Ends the stream (DELETE: the agent's process ends on the host); nothing more is heard.
    void stop();

    bool isStarted() const { return !m_id.isEmpty(); }
    bool isEnded() const { return m_ended; }
    QString id() const { return m_id; }
    /// The host's notebook folder ("" before started()).
    QString root() const { return m_root; }

    /// How long the host may stay unreachable before the stream ends (default 15 minutes).
    void setUnreachableMs(qint64 ms) { m_unreachableMs = ms; }
    qint64 unreachableMs() const { return m_unreachableMs; }
    /// The waits between tries after a failure (default 1, 2, 5, 10, 30 s; the last one repeats).
    void setRetryDelays(const QList<int> &ms) { m_retryDelays = ms; }
    /// How long a receive waits on the host (seconds, default RemoteWaitSeconds).
    void setWaitSeconds(int seconds) { m_waitSeconds = seconds; }
    /// Tests: the time now (ms since some point; default a monotonic clock).
    void setClock(std::function<qint64()> clock) { m_clock = std::move(clock); }

Q_SIGNALS:
    /// The agent's process runs on the host; `root` its notebook folder there.
    void started(const QString &root);
    /// What the agent said: whole lines, each ending with "\n" (for acp::Client::received()).
    void received(const QByteArray &bytes);
    /// The stream is over (never after stop()); `code`: the host's error code where it gave one.
    void ended(brulion::acp::RemoteStream::End why, const QString &detail, const QString &code);

private:
    sync::HttpRequest request(const QByteArray &method, const QString &endpoint, const QJsonObject *body) const;
    void open();
    void flush();
    void poll();
    void failedTry(const RemoteError &error, const std::function<void()> &again, bool *retrying);
    int retryDelay(int failures) const;
    void end(End why, const QString &detail, const QString &code = QString());
    qint64 now() const;

    sync::HttpTransport *m_transport = nullptr;
    QUrl m_base;
    QString m_token;
    QString m_agent;
    QString m_id;
    QString m_root;
    bool m_ended = false;
    bool m_opening = false;
    // Sending: every line written, those the host has (m_sent) dropped from the front.
    QList<QByteArray> m_queue;
    qint64 m_sent = 0; ///< lines the host confirmed (the `from` of the next batch)
    bool m_sending = false;
    bool m_flushDue = false; ///< a flush() waits for the event loop
    // Receiving.
    qint64 m_after = 0; ///< the last message received
    bool m_polling = false;
    int m_failures = 0;         ///< failed requests in a row
    qint64 m_failingSince = -1; ///< the first failure of a run of them (-1: none)
    QList<int> m_retryDelays{1000, 2000, 5000, 10000, 30000};
    qint64 m_unreachableMs = 15 * 60 * 1000;
    int m_waitSeconds = RemoteWaitSeconds;
    std::function<qint64()> m_clock;
    /// Guards the callbacks of requests on their way against a stream deleted meanwhile.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace brulion::acp
