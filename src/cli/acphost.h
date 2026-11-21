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
//
// Plain HTTP on 127.0.0.1 behind a reverse proxy that adds HTTPS (in the user's own network, a
// tailnet — the agents can run commands here), or HTTPS itself with --tls-cert / --tls-key. Host
// is checked (DNS rebinding); a request with an Origin (a browser's page) is refused.
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
    int maxStreams = 16;
    int maxStreamsPerDevice = 8;
    qint64 streamIdleMs = 30LL * 60 * 1000;
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
    QTimer *m_maintenance = nullptr;
    QDateTime m_certModified;
    QDateTime m_keyModified;
    QList<QDateTime> m_pairTries; ///< the last minute's pairing requests (a limit on guessing)
    std::function<QDateTime()> m_clock;
    int m_nextTerminal = 1;
};

} // namespace brulion::cli
