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
// * Pairing without a code (setPairTrust(); acptrust.h): in a tailnet (--tailscale) a device of the
//   computer's own user pairs with nothing typed, tailscaled vouching for who it is.
// * The app's live tools for the agent (acp/apptunnel.h; unless --no-app-tools or --no-tools): a
//   device whose stream asks for them (POST streams {features: [app]}) names, in a session's
//   request, what that session may do with the app on the device — its live tools, its chat —
//   and the session's Brulion MCP server gets `--app-control` (or `--app-chat`) with grants of
//   this server's tunnel: a control socket of its own (ipc::ControlServer, owner-only, in the
//   runtime folder; BRULION_APP_SOCKET and the grants' tokens only in the server's environment).
//   Each call of those tools becomes a request on the device's stream (`_brulion/app`), and the
//   device's answer — the app's, answered in process — goes back to the tool; a screenshot's
//   picture is written here, where the tool reads it. A call the device does not answer in time
//   fails with why; a few calls of a stream wait at once at most; a device gone fails them at once.
//
// * Code mode's threads (--projects; acp/codehost.h): the projects this computer's user added in
//   Code mode (the desktop app's agent sharing writes the file, read again when it changes) are
//   offered to the devices (GET projects), and a session whose request names one (`_meta`
//   "brulion/code") works in its folder or a worktree of it — the cwd, files and terminals
//   answered there and nowhere else, Brulion's tools without the notebook's (`mcp --no-notes`) —
//   with a git checkpoint taken before each prompt reaches the agent (the prompt waits for it),
//   told to the device (`_brulion/checkpoint`) and kept with the turn. A device may also make a
//   worktree of a project for a new thread (POST projects/<id>/worktrees) and read the changes
//   since a checkpoint (GET projects/<id>/changes, …/diff), commit, push, open a pull request (gh
//   here) and revert a turn there (POST projects/<id>/commit | push | pr | revert). A worktree a
//   device names is taken only under the project's worktrees root or when `git worktree list` of
//   the project lists it.
//
// * The app's own conversations (core acp/appchats.h; the desktop app's agent sharing starts this
//   server with a grant of its own: BRULION_APP_SOCKET and BRULION_APP_SHARE_TOKEN in its
//   environment, taken out of it at once — the agents and their terminals never see them): a lasting
//   asynchronous link to the app's control socket (ipc::AsyncControlClient — hello with the grant,
//   requests multiplexed, connected again with a backoff while the app is gone or not listening yet,
//   every request waiting failed when it drops). While it is up, GET info names acp::ChatsFeature
//   and a paired device follows the conversations the app runs and steers them: GET chats (a long
//   poll of the app's chatLive, its answer sanitized through acp::RemoteChats), GET chats/page (the
//   conversation's page read here, never an encrypted one, MaxRemotePageBytes at most), POST
//   chats/send | start | stop | answer (chatSend, chatStart, chatStop, chatAnswer). The link down: 503
//   app_unavailable; the app's errors mapped (not_found 404, busy and no_question 409, bad_option,
//   bad_params and ambiguous_ref 400, refused 403, not_ready 503, others 502) with its message; a
//   device's long poll dropped when its connection closes, maxChatPolls of them at once
//   (maxChatPollsPerDevice a device), the rest 429 too_many_requests.
//
// Plain HTTP on 127.0.0.1 behind a reverse proxy that adds HTTPS (in the user's own network, a
// tailnet — the agents can run commands here), plain HTTP on this computer's tailnet address
// (--tailscale: WireGuard encrypts it), or HTTPS itself with --tls-cert / --tls-key. Host
// is checked (DNS rebinding); a request with an Origin (a browser's page) is refused, unless that
// origin is the app's own in a browser named with --allowed-origin (CORS).
#pragma once

#include "http.h"

#include "acp/apptunnel.h"
#include "acp/codehost.h"

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
#include <QPointer>
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
namespace brulion::gitcli {
class Client;
}
namespace brulion::ipc {
class AsyncControlClient;
class ControlServer;
class ControlReply;
struct ControlRequest;
} // namespace brulion::ipc

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
/// root], env []} — `root` "": a thread's, [mcp, --no-notes] (the app's tools alone).
QJsonObject toolsServer(const QString &program, const QString &root);
/// A request of the agent this server answers itself (fs/*, terminal/*).
bool servedHere(const QJsonObject &message);
/// A file a thread's agent names in its folder `folder` (canonical): absolute, or relative to the
/// folder — its clean absolute path, "" when it is not inside the folder (`..`, a link that leads
/// out, a NUL, too long) or, for `write`, inside its `.git`.
QString codeFilePath(const QString &folder, const QString &file, bool write);

/// The --projects file (acp/codehost.h HostProject), read again when it changed on disk; a missing
/// or broken file: no projects (said once in the log).
class HostProjectsStore {
public:
    explicit HostProjectsStore(QString path);
    QString path() const { return m_path; }
    const QList<acp::HostProject> &projects();
    std::optional<acp::HostProject> find(const QString &id);
    std::function<void(const QString &line)> log;

private:
    void refresh();
    QString m_path;
    QList<acp::HostProject> m_projects;
    QDateTime m_modified;
    qint64 m_size = -2;
};

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
    /// A thread's turn: the checkpoint taken before it and when ("" none, or not yet).
    QString checkpoint;
    QString checkpointAt;
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
    /// question (while one waits), checkpoint {id, at} (a thread's), started, ended}.
    QJsonObject toJson() const;
};
/// The most of an answer and of its tool calls a turn keeps; the turns kept for one device (the
/// oldest ended go first); how long an ended one is kept.
inline constexpr qsizetype kMaxTurnTextChars = 1024 * 1024;
inline constexpr int kMaxTurnTools = 100;
inline constexpr int kMaxTurnsPerDevice = 64;
inline constexpr qint64 kTurnKeptMs = 24LL * 60 * 60 * 1000;

/// The answer to a pairing request without a code (AcpHost::setPairTrust(); acptrust.h: the
/// device's identity in a tailnet).
struct PairTrust {
    bool trusted = false;
    QString login; ///< whose device it is ("" unknown)
    QString node;  ///< its name in the tailnet ("" unknown)
    QString why;   ///< English, for the log and the device: how it was trusted, or why not
};

struct AcpHostOptions {
    QHostAddress address = QHostAddress(QHostAddress::LocalHost);
    quint16 port = 8790;
    QString publicUrl;  ///< "" = http(s)://<publicHost, else address>:<port>
    QString publicHost; ///< the host name of that default ("nixos.tail1ea20e.ts.net"; "" the address)
    /// A plain http:// public URL beyond this computer and a tailnet (a LAN address: not encrypted
    /// on the way, --allow-http).
    bool allowHttp = false;
    QString stateFile;
    QList<HostAgent> agents;
    bool tools = true;    ///< sessions get Brulion's MCP server
    QString toolsProgram; ///< brulion-cli (default: this program)
    /// Code mode's projects of this computer (--projects, acp/codehost.h: [{id, name, path,
    /// worktreesRoot}], read again when it changes; "" none — no thread runs here).
    QString projectsFile;
    /// git for the threads' checkpoints, worktrees and changes ("" the one on PATH).
    QString gitProgram;
    /// The GitHub CLI for a device's pull requests ("" the one on PATH).
    QString ghProgram;
    /// The checkpoints a thread's session keeps here at most (the older ones' refs dropped).
    int maxCheckpoints = 30;
    /// A device that answers the app's calls gets them from its sessions' tools (the tunnel,
    /// acp/apptunnel.h; --no-app-tools: never).
    bool appTools = true;
    /// The tunnel's socket ("" a name of its own in the user's runtime folder).
    QString tunnelKey;
    /// How long a call of the app waits for the device's answer (more for waitFor, events,
    /// chatWait by their own wait, eval and load three times as long and 10 s), and the calls of
    /// one stream that wait at once at most.
    int appCallTimeoutMs = 15000;
    int maxAppCalls = 4;
    /// The app that started this server (its agent sharing): its control socket and the grant it gave
    /// (BRULION_APP_SOCKET, BRULION_APP_SHARE_TOKEN; "" none — no conversations of the app here).
    QString appSocket;
    QString appToken;
    /// GET chats waiting for the app at once at most, and of one device.
    int maxChatPolls = 32;
    int maxChatPollsPerDevice = 4;
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
    /// A pairing request without a code is judged by `check` (the device's tailnet identity,
    /// acptrust.h): trusted, it pairs; else it is refused with 403 `code_needed`. Unset: a code
    /// is always needed.
    using PairTrustCheck =
            std::function<void(const http::Request &request, std::function<void(const PairTrust &verdict)> done)>;
    void setPairTrust(PairTrustCheck check) { m_pairTrust = std::move(check); }

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
    /// The tunnel's socket ("" when the app's calls are not tunnelled: --no-app-tools, no tools,
    /// no private runtime folder).
    QString tunnelSocket() const;
    /// The link to the app that started it is up (its hello accepted: GET info names ChatsFeature).
    bool appLinked() const;
    /// GET chats waiting for the app now.
    int chatPolls() const { return int(m_chatPolls.size()); }
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

    void pair(const http::Request &request, const http::Reply &reply);
    http::Response pairWithCode(const QString &code, const QString &name, const QString &peer, const QDateTime &t);
    /// A device paired without a code (its identity trusted: `how`).
    http::Response addTrustedDevice(const QString &name, const QString &how);
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

    // The app's calls of the agent's tools (acp/apptunnel.h).
    /// The MCP servers of a session of `s` that asked for `asked` (the tunnel's grants made); `code`:
    /// a thread's (Brulion's tools without the notebook's: none when the app's are not there either).
    QJsonArray sessionServers(Stream *s, const std::optional<acp::AppSession> &asked, bool code = false);
    /// A request on the tunnel's socket: a call of the app's tools, handed to the device.
    void tunnelRequest(const ipc::ControlRequest &request, const ipc::ControlReply &reply);
    /// The device's answer to a call of `s` (true: it was one, now answered).
    bool appAnswered(Stream *s, const QJsonObject &message);
    /// The call `key` of the stream `stream` waited too long.
    void appCallTimedOut(const QString &stream, const QString &key);
    /// Every call of `s` waiting fails (`code`, `why`).
    void failAppCalls(Stream *s, const QString &code, const QString &why);
    void revokeAppGrants(Stream *s);
    /// A message of this server for the device, among the agent's.
    void toDevice(Stream *s, const QJsonObject &message);
    /// The name `device` (dev_…) was paired by ("" none).
    QString pairedName(const QString &device) const;

    // Code mode's threads (acp/codehost.h).
    /// Where a thread's session works: the project, its folder (canonical: the project's or a
    /// worktree of it), the branch the thread names.
    struct CodePlace {
        QString project;
        QString folder;
        QString branch;
    };
    gitcli::Client *git();
    /// The place of `code` (done at once when it can be told, else once git listed the project's
    /// worktrees): `error` ("" fine) for the device.
    void codePlace(const acp::CodeSession &code, std::function<void(const CodePlace &place, const QString &error)> done);
    /// The place of a thread's request (`project`, query `worktree`), as codePlace().
    void requestPlace(const QString &project, const QString &worktree,
                      std::function<void(const CodePlace &place, const http::Response &refusal)> done);
    void projectsRequest(const QStringList &parts, const http::Request &request, const http::Reply &reply);
    http::Response projectList();
    void createWorktree(const QString &project, const http::Request &request, const http::Reply &reply);
    void changes(const QString &project, const http::Request &request, const http::Reply &reply);
    void diff(const QString &project, const http::Request &request, const http::Reply &reply);
    /// POST projects/<id>/commit | push | pr | revert: git there for a device's thread.
    void gitRequest(const QString &project, const acp::RemoteGitRequest &request, const http::Reply &reply);
    /// The request in the repository's top level `top`.
    void gitIn(const QString &top, const acp::RemoteGitRequest &request, const http::Reply &reply);
    void gitCommit(const QString &top, const acp::RemoteGitRequest &request, const http::Reply &reply);
    void gitPush(const QString &top, const http::Reply &reply);
    void gitPullRequest(const QString &top, const acp::RemoteGitRequest &request, const http::Reply &reply);
    void gitRevert(const QString &top, const acp::RemoteGitRequest &request, const http::Reply &reply);
    /// A thread's session request of `s` (its CodeSession taken out): held until its place is known,
    /// then rewritten for it — or refused to the device.
    void codeSessionRequest(Stream *s, const QJsonObject &message, const std::optional<acp::CodeSession> &code,
                            const std::optional<acp::AppSession> &appAsked);
    /// The prompt `message` of the thread's session `session` of `s`: held until its checkpoint is
    /// taken (and told to the device), then delivered.
    void checkpointThenPrompt(Stream *s, const QJsonObject &message, const QString &session);
    /// A thread's file or terminal request: the place of its session ("" folder: none — the notebook's).
    CodePlace codeSessionOf(const Stream *s, const QJsonObject &params) const;

    // The app's own conversations (acp/appchats.h), over the link to the app.
    /// onHeaders() of `chats/…`: refused (404, 405), or the body it may have.
    static std::optional<http::Response> chatsHeaders(const QStringList &parts, const QByteArray &method,
                                                      qint64 *maxBody);
    void chatsRequest(const PairedDevice &device, const QStringList &parts, const http::Request &request,
                      const http::Reply &reply);
    /// `method` of the app with `params`: `answer` makes the device's answer of the app's result, or
    /// the app's error is mapped (appError()); nothing is sent to a device that left.
    void relay(const QString &method, const QJsonObject &params, int timeoutMs, const http::Reply &reply,
               std::function<http::Response(const QJsonObject &result)> answer);
    http::Response chatPage(const http::Request &request) const;
    /// chats/send, chats/start (`pictures` checked here: acp::remotePicturesFromJson()), chats/stop,
    /// chats/answer, chats/option, chats/resume: the app's chatSend … chatResume.
    void chatSend(const PairedDevice &device, const QString &path, const QJsonObject &body, const http::Reply &reply);
    void chatStart(const PairedDevice &device, const QJsonObject &body, const http::Reply &reply);
    void chatStop(const PairedDevice &device, const QString &path, const http::Reply &reply);
    void chatAnswer(const PairedDevice &device, const QString &path, const QJsonObject &body, const http::Reply &reply);
    void chatOption(const PairedDevice &device, const QString &path, const QJsonObject &body, const http::Reply &reply);
    void chatResume(const PairedDevice &device, const QString &path, const http::Reply &reply);
    int chatTimeout() const;
    /// `method` of the app for `path`, its answer's `flag` (stopped, resumed) passed on.
    void chatFlag(const QString &method, const QString &path, const QString &flag, const http::Reply &reply);
    /// The device's long polls whose connection closed: dropped.
    void dropLeftPolls();

    void startAgent(Stream *s);
    void fromAgent(Stream *s, const QByteArray &line);
    void toAgent(Stream *s, const QJsonObject &message);
    void deliver(Stream *s, const QJsonObject &message);
    /// `message` to the agent after the messages held before it (or at once when none are).
    void deliverInOrder(Stream *s, const QJsonObject &message);
    /// The held messages that may go now (in order, up to the first one that still waits).
    void flushHeld(Stream *s);
    void releaseHeld(Stream *s);
    void respond(Stream *s, const QJsonValue &id, const QJsonValue &result);
    void respondError(Stream *s, const QJsonValue &id, int code, const QString &message);
    void serve(Stream *s, const QJsonObject &request);
    /// The notebook's path of a file the agent names ("" outside the notebook or off limits).
    QString notebookPath(const QString &file) const;
    void readFile(Stream *s, const QJsonValue &id, const QJsonObject &params);
    void writeFile(Stream *s, const QJsonValue &id, const QJsonObject &params);
    void readCodeFile(Stream *s, const QJsonValue &id, const CodePlace &place, const QJsonObject &params);
    void writeCodeFile(Stream *s, const QJsonValue &id, const CodePlace &place, const QJsonObject &params);
    void terminalRequest(Stream *s, const QJsonValue &id, const QString &method, const QJsonObject &params);
    void endTerminals(Stream *s);

    Context &m_ctx;
    notebook::Notebook *m_notebook = nullptr;
    AcpHostOptions m_options;
    HostSync *m_sync = nullptr;
    PairTrustCheck m_pairTrust;
    std::unique_ptr<http::Server> m_http;
    std::unique_ptr<HostStateStore> m_store;
    std::unique_ptr<HostProjectsStore> m_projects; ///< Code mode's projects (--projects; null none)
    std::unique_ptr<gitcli::Client> m_git;
    int m_nextTicket = 1; ///< of held messages that wait for a check or a checkpoint
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
    std::unique_ptr<ipc::ControlServer> m_tunnel; ///< the app's calls of the agents' tools
    QString m_tunnelKey;
    QString m_tunnelPrefix; ///< of the calls' ids (random: an agent's ids never take one)
    qint64 m_nextAppCall = 1;
    /// The tunnel's grants: token → the stream, the device's handle of the session, the scope.
    struct TunnelGrant {
        QString stream;
        QString handle;
        QString scope;
        QString client;
    };
    QHash<QString, TunnelGrant> m_tunnelGrants;
    std::unique_ptr<ipc::AsyncControlClient> m_app; ///< the link to the app (its agent sharing)
    struct ChatPoll {
        QString device;
        http::Reply reply;
    };
    QHash<quint64, ChatPoll> m_chatPolls; ///< GET chats waiting for the app, by a serial
    quint64 m_nextPoll = 1;
    QTimer *m_pollCheck = nullptr;
    QTimer *m_maintenance = nullptr;
    QDateTime m_certModified;
    QDateTime m_keyModified;
    QList<QDateTime> m_pairTries; ///< the last minute's pairing requests (a limit on guessing)
    std::function<QDateTime()> m_clock;
    int m_nextTerminal = 1;
};

} // namespace brulion::cli
