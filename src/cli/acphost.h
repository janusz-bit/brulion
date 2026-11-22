// `brulion-cli acp serve`: AI agents of this computer for the user's other devices (docs/CLI.md
// "AI agents for other devices"). The app on a phone cannot start a program like Claude Code;
// this server starts it here, in this computer's copy of the notebook, and relays the Agent Client
// Protocol between it and the app (core acp/remote.h, the other side): the app keeps the
// conversation as with an agent of its own, while the agent's work happens here.
//
// * The agents are this computer's (--agent id=command, name and command from here only): a device
//   asks for one by its id, never for a program.
// * A device pairs once with a code of `acp pair` (shown here, valid for minutes, one use, a few
//   tries) and gets a token; `acp clients` lists the devices, `acp revoke` ends one at once.
// * A stream is one agent's process for one device. What the agent says is kept until the device
//   fetches it (a long poll: a phone that sleeps or loses the network for a while misses nothing),
//   at most maxBufferBytes; a stream nobody fetches from, without a turn running, ends after
//   streamIdleMs.
// * Rewritten on the way to the agent: `initialize` offers files and terminals (served here),
//   `session/new` / `load` / `resume` get this notebook's folder as `cwd` and Brulion's MCP server
//   (`brulion-cli mcp --vault <root>`) rather than anything the device names. Served here, never
//   relayed: the agent's `fs/read_text_file` and `fs/write_text_file` (files of the notebook only,
//   written with the version history, never an encrypted page) and `terminal/*` (its commands run
//   here, in the notebook's folder by default).
// * With --nextcloud the notebook here is kept in sync with Nextcloud (hostsync.h): before a turn
//   starts (the device's latest notes are here), after files are written and after a turn.
// * The last turn of each session a device prompted is kept here (HostTurn: how it ended, the
//   agent's answer): a device that lost its stream — Android ended the app, the network was gone
//   for long — asks for it (GET sessions/<id>/turn) and writes the answer into the conversation. A
//   stream that runs a turn is never cut off for a new one of its device: it ends after the turn.
// * A question of the agent (session/request_permission) the device has not answered waits for it
//   with the turn, also when the device lost the stream: the turn kept carries it, and the device
//   answers it there (POST sessions/<id>/turn). Only once nobody fetched the stream or the turn for
//   questionWaitMs is it answered "cancelled" (0: at once, when the device lost the stream).
//
// Plain HTTP on 127.0.0.1 behind a reverse proxy that adds HTTPS (in the user's own network, a
// tailnet — the agents can run commands here), or HTTPS itself with --tls-cert / --tls-key. Host
// is checked (DNS rebinding); a request with an Origin (a browser's page) is refused, unless that
// origin is the app's own in a browser named with --allowed-origin (CORS).
#pragma once

#include "http.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

class QProcess;
class QTimer;

namespace brulion::notebook {
class Notebook;
}

namespace brulion::cli {

class Context;
class HostSync;

/// An agent this computer offers (--agent).
struct HostAgent {
    QString id;      ///< letters, digits, "-" and "_" ("claude")
    QString name;    ///< as the chat shows it ("Claude Code")
    QString command; ///< the program and its arguments, as a shell would split them
    friend bool operator==(const HostAgent &, const HostAgent &) = default;
};

/// "claude=claude-agent-acp" or "claude:Claude Code=npx -y @agentclientprotocol/claude-agent-acp"
/// (id, an optional name after ':', the command after '='); nullopt with `error`.
std::optional<HostAgent> parseHostAgent(const QString &spec, QString *error);

// ---- The state file (acp-remote.json in the notebook's device state)

struct PairedDevice {
    QString id;        ///< "dev_…"
    QString name;      ///< as the device named itself ("Pixel 9 Pro")
    QString tokenHash; ///< SHA-256 of its token, hex
    QDateTime created;
    QDateTime lastUsed;
};

struct HostState {
    QList<PairedDevice> devices;
    /// The pairing code waiting to be used (`acp pair`): its hash, when it expires, the wrong
    /// codes tried against it.
    QString pairingHash;
    QDateTime pairingExpires;
    int pairingAttempts = 0;
    /// The address the devices reach the server at (written by `acp serve` when it starts, shown
    /// by `acp pair`).
    QString publicUrl;
    QJsonObject toJson() const;
    static HostState fromJson(const QJsonObject &o);
};

/// The state file, read again when it changed on disk, written under a lock file (the server and
/// `acp pair` / `acp revoke` change it side by side), owner-only.
class HostStateStore {
public:
    explicit HostStateStore(QString path);
    QString path() const { return m_path; }
    /// Read the file when it changed (a missing file is an empty state).
    bool refresh(QString *error = nullptr);
    const HostState &state() const { return m_state; }
    /// Read, change (false: nothing to write), write.
    bool update(const std::function<bool(HostState &)> &change, QString *error = nullptr);

private:
    bool load(QString *error);
    QString m_path;
    HostState m_state;
    QDateTime m_modified;
    qint64 m_size = -1;
};

/// A pairing code's length (letters and digits that cannot be mistaken for one another), how long
/// it is valid, the wrong tries it takes.
inline constexpr int kPairingCodeLength = 8;
inline constexpr int kPairingMinutes = 10;
inline constexpr int kPairingAttempts = 5;
/// A new pairing code ("K7QM2XRP"; shown as "K7QM-2XRP").
QString newPairingCode();
QString formatPairingCode(const QString &code);
/// The brulion:// address that opens the app's pairing filled in: brulion://pair?url=…&code=…
/// ("" without a server address).
QString pairingLink(const QString &url, const QString &code);
/// `text` as a QR code of block characters for a terminal (two rows a line, the light modules
/// drawn, its quiet zone around); "" when it does not fit one.
QString terminalQrCode(const QString &text);
/// A new device token ("bat_" + 43 characters) and the hash kept of tokens and codes (SHA-256 hex).
QString newDeviceToken();
QString secretHash(const QString &secret);

// ---- What goes to the agent and what comes from it (pure)

/// A message of the device on its way to the agent: `initialize` offers files and terminals,
/// a session's `cwd` is `root` and its MCP servers `mcpServers` (whatever the device said).
/// nullopt: no JSON-RPC message (dropped).
std::optional<QJsonObject> rewriteForAgent(const QJsonObject &message, const QString &root,
                                           const QJsonArray &mcpServers);
/// Brulion's MCP server for a session: {name "brulion", command `program`, args [mcp, --vault,
/// root], env []}.
QJsonObject toolsServer(const QString &program, const QString &root);
/// A request of the agent this server answers itself (fs/*, terminal/*).
bool servedHere(const QJsonObject &message);

/// The last turn of a session a device prompted (GET sessions/<id>/turn).
struct HostTurn {
    enum class State { Running, Done, Failed };
    State state = State::Running;
    QString device;      ///< "dev_…"
    QString session;     ///< the agent's session id
    QString stream;      ///< the stream it runs in
    QString prompt;      ///< the key of its session/prompt request's id
    QString id;          ///< the device's id of the turn (its prompt's _meta "brulion/turn"), "" none
    QString text;        ///< what the agent said (agent_message_chunk), at most kMaxTurnTextChars
    QStringList tools;   ///< the titles of its tool calls, at most kMaxTurnTools
    QString stopReason;  ///< Done: the agent's ("end_turn", "cancelled" …)
    QString error;       ///< Failed: why
    bool asking = false; ///< the agent waits on a question of it to the device
    /// The first question of the agent that waits for the device's answer: {id, toolCall, options}
    /// (session/request_permission's params, `id` the server's for answering it); {} none.
    QJsonObject question;
    /// The titles of its tool calls by their ids (a question names its call, often without one).
    QHash<QString, QString> toolTitles;
    QDateTime started;
    QDateTime ended;
    /// {session, turn, state: "running" | "done" | "failed", text, tools, stopReason, error, asking,
    /// question (while one waits), started, ended}.
    QJsonObject toJson() const;
};
/// The most of an answer and of its tool calls a turn keeps; the turns kept for one device (the
/// oldest ended go first); how long an ended one is kept.
inline constexpr qsizetype kMaxTurnTextChars = 1024 * 1024;
inline constexpr int kMaxTurnTools = 100;
inline constexpr int kMaxTurnsPerDevice = 64;
inline constexpr qint64 kTurnKeptMs = 24LL * 60 * 60 * 1000;

struct AcpHostOptions {
    QHostAddress address = QHostAddress(QHostAddress::LocalHost);
    quint16 port = 8790;
    QString publicUrl; ///< "" = http(s)://<address>:<port>
    QString stateFile;
    QList<HostAgent> agents;
    bool tools = true;    ///< sessions get Brulion's MCP server
    QString toolsProgram; ///< brulion-cli (default: this program)
    QString tlsCertificate;
    QString tlsKey;
    QStringList allowedHosts;
    /// The app in a browser at these origins may call it (CORS): "https://notes.example.com".
    QStringList allowedOrigins;
    int maxStreams = 16;
    int maxStreamsPerDevice = 8;
    qint64 streamIdleMs = 30LL * 60 * 1000;
    /// How long a turn of a stream nobody fetches from (retired) may still run (the time its
    /// questions wait for the device not counted).
    qint64 retiredTurnMs = 60LL * 60 * 1000;
    /// How long a question of the agent waits for a device that is gone — nobody fetched its stream
    /// or its turn meanwhile — before it is answered "cancelled" (--question-wait; 0: at once, when
    /// the device lost the stream).
    qint64 questionWaitMs = 60LL * 60 * 1000;
    qint64 maxBufferBytes = 32LL * 1024 * 1024;
    int maxWaitSeconds = 30;
    qint64 maxBodyBytes = 16LL * 1024 * 1024;
    /// How long a turn waits for the sync before it (then it starts anyway).
    int syncBeforeTurnMs = 60000;
    int maintenanceIntervalMs = 30000;
    http::ServerLimits limits;
};

class AcpHost : public QObject {
    Q_DECLARE_TR_FUNCTIONS(brulion::cli)
public:
    AcpHost(Context &context, notebook::Notebook *notebook, AcpHostOptions options, QObject *parent = nullptr);
    ~AcpHost() override;

    /// Keep the notebook in sync with Nextcloud (before start()); not owned.
    void setSync(HostSync *sync) { m_sync = sync; }

    bool start(QString *error);
    quint16 port() const;
    /// The API's address (<public URL>/acp/v1/).
    QString endpoint() const;
    /// The address the devices are given (<public URL>).
    QString publicUrl() const { return m_publicUrl; }
    http::Server *server() { return m_http.get(); }
    int streamCount() const { return int(m_streams.size()); }
    /// The turn kept of `device`'s session `session` (nullptr none).
    const HostTurn *turn(const QString &device, const QString &session) const;
    std::function<void(const QString &line)> log;

    /// Tests: the time now (default the system's).
    void setClock(std::function<QDateTime()> clock) { m_clock = std::move(clock); }

private:
    struct Terminal;
    struct Stream;

    void say(const QString &line) const;
    bool loadTls(QString *error);
    QDateTime now() const;
    QString route(const QByteArray &path) const;
    bool hostAllowed(const QByteArray &host) const;
    std::optional<http::Response> onHeaders(const http::Request &head, qint64 *maxBody);
    void onRequest(const http::Request &request, const http::Reply &reply);
    /// The device of a request's token (nullptr: none; `refusal` the answer then).
    const PairedDevice *authenticate(const http::Request &request, http::Response *refusal);
    void maintain();

    http::Response pair(const http::Request &request);
    http::Response info() const;
    http::Response openStream(const PairedDevice &device, const http::Request &request);
    http::Response send(Stream *s, const http::Request &request);
    void receive(Stream *s, const http::Request &request, const http::Reply &reply);
    void answerWaiter(Stream *s);
    void closeStream(Stream *s, const QString &why);
    void removeStream(Stream *s);
    /// Nobody fetches from `s` any more (a new stream of its device for the agent, or its device
    /// lost it): it ends now, or after its turns — its agent's questions wait for the device with
    /// their turns (questionWaitMs 0: answered "cancelled").
    void retire(Stream *s, const QString &why);
    /// Removes `s` once the event loop goes on (not while its process's output is read).
    void removeLater(Stream *s);

    // The turns kept (HostTurn).
    void sessionTurn(const PairedDevice &device, const QString &session, const http::Request &request,
                     const http::Reply &reply);
    void turnStarted(Stream *s, const QJsonObject &params, const QString &prompt);
    void turnUpdate(Stream *s, const QJsonObject &params);
    void turnEnded(Stream *s, const QString &prompt, const QJsonObject &response);
    /// Every running turn of `s` failed: `why`.
    void turnsFailed(Stream *s, const QString &why);
    void turnChanged(const QString &key);
    /// The turn of `session` in `s` carries its first question waiting (HostTurn::asking, question).
    void questionsChanged(Stream *s, const QString &session);
    /// Answers the agent's question `index` of `s` (its outcome: {outcome: selected, optionId} or
    /// {outcome: cancelled}) and forgets it.
    void answerQuestion(Stream *s, qsizetype index, const QJsonObject &outcome);
    /// The device's answer to a question of its turn (POST sessions/<id>/turn {question, optionId}).
    http::Response answerTurnQuestion(const QString &key, const http::Request &request);
    /// Questions nobody fetched for questionWaitMs: answered "cancelled".
    void expireQuestions(Stream *s);
    void answerTurnWaiter(QTimer *timer);
    void forgetTurns();
    bool originAllowed(const QByteArray &origin) const;
    http::Headers corsHeaders(const QByteArray &origin) const;

    void startAgent(Stream *s);
    void fromAgent(Stream *s, const QByteArray &line);
    void toAgent(Stream *s, const QJsonObject &message);
    void deliver(Stream *s, const QJsonObject &message);
    void releaseHeld(Stream *s);
    void respond(Stream *s, const QJsonValue &id, const QJsonValue &result);
    void respondError(Stream *s, const QJsonValue &id, int code, const QString &message);
    void serve(Stream *s, const QJsonObject &request);
    /// The notebook's path of a file the agent names ("" outside the notebook or off limits).
    QString notebookPath(const QString &file) const;
    void readFile(Stream *s, const QJsonValue &id, const QJsonObject &params);
    void writeFile(Stream *s, const QJsonValue &id, const QJsonObject &params);
    void terminalRequest(Stream *s, const QJsonValue &id, const QString &method, const QJsonObject &params);
    void endTerminals(Stream *s);

    Context &m_ctx;
    notebook::Notebook *m_notebook = nullptr;
    AcpHostOptions m_options;
    HostSync *m_sync = nullptr;
    std::unique_ptr<http::Server> m_http;
    std::unique_ptr<HostStateStore> m_store;
    QString m_root;
    QString m_publicUrl;
    QString m_basePath;
    QStringList m_hosts;
    QJsonArray m_mcpServers;
    QHash<QString, Stream *> m_streams;
    QHash<QString, HostTurn> m_turns; ///< by device + "\n" + session
    struct TurnWaiter {
        QString key;
        http::Reply reply;
        QTimer *timer = nullptr;
    };
    QList<TurnWaiter> m_turnWaiters;
    QStringList m_origins; ///< allowed Origin values (--allowed-origin)
    QTimer *m_maintenance = nullptr;
    QDateTime m_certModified;
    QDateTime m_keyModified;
    QList<QDateTime> m_pairTries; ///< the last minute's pairing requests (a limit on guessing)
    std::function<QDateTime()> m_clock;
    int m_nextTerminal = 1;
};

} // namespace brulion::cli
