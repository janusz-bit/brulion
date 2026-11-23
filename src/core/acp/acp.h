#pragma once
// The Agent Client Protocol (ACP, https://agentclientprotocol.com, version 1) from the client's
// side: Brulion talks to an AI agent the user runs on this computer — Claude Code (through
// @agentclientprotocol/claude-agent-acp), Codex (codex-acp), Gemini CLI (`gemini --acp`), OpenCode
// (`opencode acp`) or any other — as a partner of the AI chat (docs/UI.md "AI agents (ACP)"). The
// agent is a program of its own, started by the app (src/app/shell/workspaceagents.h): JSON-RPC
// 2.0, one message per line on its standard input and output. The agent signs in by itself (a
// subscription its own program knows how to use, an API key of its own), so the app never holds
// the agent's credentials.
//
//   acp::Client client([&](const QByteArray &line) { process.write(line); }, version);
//   client.setCapabilities({.files = true, .terminals = true});   // served through setHandler()
//   QObject::connect(&process, &QProcess::readyReadStandardOutput, [&] { client.received(…); });
//   client.prompt(u"AI/2026-11-20 Pytanie.md"_s, input);   // a turn of a conversation
//   → progress(key, text, thoughts) … status(key, "Read Dom.md") … activityChanged(key)
//   → permissionAsked(key, …) → answer(key, optionId)
//   → finished(key, text, stopReason) | failed(key, failure)
//
// What the client does (Client):
//
// * `initialize` once per agent: the client offers the file system and terminals when the app
//   serves them (setCapabilities(); their requests go to setHandler(), the app answers with
//   respond()/respondError()).
// * A session per conversation (key): `session/new` with the notebook's folder as `cwd` and
//   Brulion's MCP server — or, given the id of a session of an earlier run (TurnInput::resume),
//   `session/resume` (no history replayed) or `session/load` (its replay not heard) where the agent
//   offers them, `session/new` again when that fails. The options a session takes (its model,
//   mode, effort … — `configOptions`, or the older `modes` and `models` as options of their own)
//   are kept per session (session()); TurnInput::config sets them on a session that is new before
//   its first prompt, setOption() at any time (`session/set_config_option`, `session/set_mode`,
//   `session/set_model`). sessionStarted() tells the app the session's id (to resume it later).
// * `session/prompt` per turn: the first prompt of a session that is new (TurnInput::firstPrompt:
//   the context, the conversation so far) or of one that goes on (TurnInput::prompt). Pictures and
//   embedded text go as they are where the agent takes them, else as links to their files.
// * What the agent says in a turn (TurnState): its answer — the message chunks put together, a
//   new message or a tool call between them starting a new paragraph —, its thoughts, the title
//   of the tool call in progress (status()), its tool calls with their state and output, its plan
//   (activityChanged()); of the session: the slash commands it offers, how much of its context is
//   used and what it cost (sessionChanged()).
// * A question of the agent (session/request_permission) waits for answer() — the user's choice
//   among its options — or is answered "cancelled" when the turn is cancelled. An agent that asks
//   the user to sign in first (`auth_required`) gets authRequired() with its sign-in methods, and
//   authenticate() goes on with the turn.
// * cancel() ends a turn: session/cancel, and nothing more is said of it; a turn sent meanwhile in
//   the same conversation waits for the agent to end the cancelled one. Each conversation has one
//   turn at a time; several conversations answer at once.
//
// Requests of the agent the client does not offer are answered "method not found". A line longer
// than MaxLineBytes is dropped. Everything here is pure protocol over a `send` function and
// received() bytes (tests play the agent); the app's side starts the process, serves files and
// terminals and words the failures for the user.

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <utility>

namespace brulion::acp {

/// The version of the protocol the client speaks.
inline constexpr int ProtocolVersion = 1;
/// The key of a turn's id in its prompt's `_meta` (TurnInput::turnId).
inline constexpr QLatin1StringView TurnMetaKey{"brulion/turn"};
/// The longest line read from the agent (a whole session's history replayed stays far below).
inline constexpr qsizetype MaxLineBytes = 16 * 1024 * 1024;
/// The longest details of a question shown to the user (a diff, a command's input).
inline constexpr int MaxDetailChars = 4000;
/// The longest text of a file's change kept for a diff (the old and the new each).
inline constexpr int MaxDiffChars = 200000;
/// The end of a tool call's output kept for the chat.
inline constexpr int MaxToolOutputChars = 2000;
/// The tool calls of a turn kept (the newest).
inline constexpr int MaxToolCalls = 60;
/// JSON-RPC error codes the protocol names.
inline constexpr int MethodNotFound = -32601;
inline constexpr int InvalidParams = -32602;
inline constexpr int AuthRequired = -32000;
inline constexpr int ResourceNotFound = -32002;

/// An MCP server the agent starts for a session (the stdio transport): Brulion's own tools.
struct McpServer
{
    QString name;
    QString command; ///< an absolute path
    QStringList args;
    /// Environment variables the agent sets for it (McpServerStdio's `env: [{name, value}]`): a
    /// grant's token for the app's live tools (docs/CLI.md "Live changes").
    QList<std::pair<QString, QString>> env{};
};

/// A block of a prompt.
struct Block
{
    enum class Kind {
        Text,
        Link,     ///< a file:// link
        Image,    ///< a picture; a link to its file (uri, name) where the agent takes no pictures
        Resource, ///< a file's text embedded (uri, text); a link where the agent takes no embedded context
    };
    Kind kind = Kind::Text;
    QString text;     ///< Text, Resource
    QString uri;      ///< Link, Resource, Image (its file, for the link instead)
    QString name;     ///< Link, Image: the file's name
    QByteArray data;  ///< Image: the picture's bytes
    QString mimeType; ///< Image, Link and Resource (optional)

    static Block textBlock(const QString &text);
    static Block link(const QString &uri, const QString &name, const QString &mimeType = QString());
    static Block image(const QByteArray &data, const QString &mimeType, const QString &uri = QString(),
                       const QString &name = QString());
    static Block resource(const QString &uri, const QString &name, const QString &text,
                          const QString &mimeType = QString());
    /// As the agent takes it: `images` / `embedded` — its prompt capabilities.
    QJsonObject toJson(bool images = true, bool embedded = true) const;
};

/// What one turn sends: the prompt of a session that is new (the context, the conversation so
/// far) and of one that goes on (only the newest message).
struct TurnInput
{
    QString cwd; ///< absolute: the notebook's folder
    QList<McpServer> mcpServers;
    QList<Block> firstPrompt;
    QList<Block> prompt;
    /// The id of this conversation's session in an earlier run of the agent ("" none): resumed or
    /// loaded where the agent can, its turn then sends `prompt`.
    QString resume;
    /// Options set on a session that is new (or resumed) before its first prompt ({option id →
    /// value: a choice's value, or a bool}); one the session does not offer, or set already, is
    /// left out.
    QHash<QString, QJsonValue> config;
    /// This turn's own id ("" none), sent in the prompt's `_meta` as "brulion/turn": a computer
    /// relaying the protocol (`brulion-cli acp serve`) names the turn it kept by it
    /// (acp/remote.h RemoteTurn::id).
    QString turnId;
};

/// A way to sign in that the agent offers (initialize's authMethods).
struct AuthMethod
{
    QString id;
    QString name;
    QString description;
    /// The user runs a command in a terminal to sign in (`type: "terminal"`): the agent's own
    /// program with `args`. Else the agent signs in by itself when asked (authenticate()).
    bool terminal = false;
    QStringList args;
};

/// One answer the agent offers to its question.
struct PermissionOption
{
    QString id;
    QString name;
    QString kind; ///< "allow_once" | "allow_always" | "reject_once" | "reject_always"
};

/// A change of a file an agent makes or proposes (a tool call's `diff` content).
struct Diff
{
    QString path;
    QString oldText; ///< "" a new file
    QString newText;
    friend bool operator==(const Diff &, const Diff &) = default;
};

/// The agent asks the user whether it may do something (session/request_permission).
struct Permission
{
    QString title;   ///< the tool call's title ("Write Dom.md", "Run git status")
    QString details; ///< what it would do: a file's new text, the tool's input as JSON ("" none)
    QList<PermissionOption> options;
    QList<Diff> diffs{}; ///< the changes of files it would make (shown as a diff)
    /// Its tool call's kind ("edit", "execute", "switch_mode" …: the request's, else the one the
    /// turn knows for the call; "" not said).
    QString kind{};
    /// A plan the agent asks the user to approve (Claude Code leaving its plan mode: the tool
    /// input's `plan`, Markdown), "" none — then it is the details too.
    QString plan{};
};

/// One value a session option may take.
struct Choice
{
    QString value;
    QString name;
    QString description;
    QString group; ///< its group's name ("" none)
    friend bool operator==(const Choice &, const Choice &) = default;
};

/// An option of a session (its model, mode, effort …).
struct ConfigOption
{
    /// How it is set: `session/set_config_option`, or the older `session/set_mode` /
    /// `session/set_model` (an agent's `modes` / `models` without an option of their kind).
    enum class Via { Config, Mode, Model };
    QString id;
    QString name;
    QString description;
    QString category; ///< "model" | "mode" | "thought_level" | … ("" none)
    bool boolean = false;
    QJsonValue current; ///< a choice's value (a string), or a bool
    QList<Choice> choices;
    Via via = Via::Config;
    friend bool operator==(const ConfigOption &, const ConfigOption &) = default;

    /// The current choice's name (a bool: "" — the option's own name says it).
    QString currentName() const;
    QJsonObject toJson() const;
    static std::optional<ConfigOption> fromJson(const QJsonObject &o);
};

/// A session's options from an answer to session/new, /load, /resume or /set_config_option
/// ({configOptions, modes, models}): its configOptions, then the modes and models as options of
/// their own where no option of their kind is among them.
QList<ConfigOption> readOptions(const QJsonObject &result);

/// A slash command the agent offers.
struct Command
{
    QString name; ///< without the "/"
    QString description;
    QString hint; ///< what to type after it ("" takes nothing)
    friend bool operator==(const Command &, const Command &) = default;
};

/// One step of the agent's plan.
struct PlanEntry
{
    QString content;
    QString status;   ///< "pending" | "in_progress" | "completed"
    QString priority; ///< "high" | "medium" | "low"
    friend bool operator==(const PlanEntry &, const PlanEntry &) = default;
};

/// A tool call of a turn as it goes.
struct ToolCall
{
    QString id;
    QString title;
    QString kind;   ///< "read" | "edit" | "delete" | "move" | "search" | "execute" | "think" | "fetch" | "other"
    QString status; ///< "pending" | "in_progress" | "completed" | "failed"
    QString terminal; ///< the id of a terminal it shows ("" none)
    QString output;   ///< the end of its text content (a command's output, a result)
    std::optional<Diff> diff{}; ///< the change of a file it shows (its first)
    /// The files it works on: its `locations` and its diffs' paths (absolute, as the agent
    /// names them), each once.
    QStringList paths{};
    friend bool operator==(const ToolCall &, const ToolCall &) = default;
};

/// How much of the session's context is used, and what it cost so far.
struct Usage
{
    qint64 used = 0;
    qint64 size = 0;
    double cost = -1; ///< < 0: not said
    QString currency;
    friend bool operator==(const Usage &, const Usage &) = default;
};

/// Why a turn failed.
struct Failure
{
    enum class Kind {
        Closed,      ///< the agent's process ended (detail: why)
        Version,     ///< the agent speaks another version of the protocol (detail: its version)
        Error,       ///< the agent answered a request with an error (detail: its message)
        Refusal,     ///< the agent refused to answer (stopReason "refusal")
        Empty,       ///< the turn ended without a word of an answer (detail: its stopReason)
        Malformed,   ///< the agent's answer was no answer of the protocol (detail: what)
    };
    Kind kind = Kind::Error;
    QString detail;
    friend bool operator==(const Failure &, const Failure &) = default;
};

/// `text`'s lines from `line` (1-based; 0: the first) on, at most `limit` (0: all) — as
/// fs/read_text_file asks for them.
QString textLines(const QString &text, int line, int limit);
/// `output` kept to its last `limit` bytes, cut at a character's start (terminal/output's
/// outputByteLimit); `cut` says whether anything was left out.
QByteArray keepLast(const QByteArray &output, qint64 limit, bool *cut = nullptr);

/// JSON-RPC 2.0 over newline-delimited JSON: requests with their callbacks, notifications, the
/// other side's requests answered by the owner.
class Connection
{
public:
    struct Error
    {
        int code = 0;
        QString message;
    };
    /// A request's answer: its result, or its error (code 0: none).
    using Done = std::function<void(const QJsonValue &result, const Error &error)>;
    using Send = std::function<void(const QByteArray &line)>;

    explicit Connection(Send send);

    /// Sends a request; `done` gets its answer — or, when the connection closes first, an error
    /// with `closedCode`.
    void request(const QString &method, const QJsonObject &params, Done done);
    void notify(const QString &method, const QJsonObject &params);
    void respond(const QJsonValue &id, const QJsonValue &result);
    void respondError(const QJsonValue &id, int code, const QString &message);

    /// Bytes from the other side: each line they finish is read.
    void receive(const QByteArray &bytes);
    /// The other side is gone: every request waiting gets `ClosedCode` with `why`.
    void close(const QString &why);

    static constexpr int ClosedCode = -32099;

    std::function<void(const QJsonValue &id, const QString &method, const QJsonObject &params)> onRequest;
    std::function<void(const QString &method, const QJsonObject &params)> onNotification;
    /// A line that is no JSON-RPC message (for the log).
    std::function<void(const QString &why)> onMalformed;

private:
    void readLine(const QByteArray &line);
    void write(const QJsonObject &message);

    Send m_send;
    QByteArray m_buffer;
    bool m_skipping = false; // a line too long: dropped up to its end
    qint64 m_nextId = 1;
    QHash<qint64, Done> m_waiting;
};

/// What the agent did in a turn so far (pure: one session/update at a time).
struct TurnState
{
    QString text;
    QString thoughts;
    QString status; ///< the title of the tool call in progress ("" none)
    QList<ToolCall> tools;
    QList<PlanEntry> plan;
    /// The titles of the turn's tool calls by id (a question names its call by id only).
    QHash<QString, QString> titles;
    bool afterTool = false; // a tool call came after the text: the next text is a new paragraph
    QString messageId;      // the message the text comes from ("" not said)

    enum Change { None = 0, Text = 1, Status = 2, Activity = 4 };
    /// Reads one update of a turn ({sessionUpdate, …}): what changed (Change flags).
    int apply(const QJsonObject &update);
};

/// What a session keeps between turns: its options, commands, usage.
struct SessionInfo
{
    QList<ConfigOption> options;
    QList<Command> commands;
    Usage usage;
    QString title; ///< the agent's own name for the session ("" none)
    friend bool operator==(const SessionInfo &, const SessionInfo &) = default;
    /// Reads one update of a session; true when something changed.
    bool apply(const QJsonObject &update);
};

/// A question of the agent as the user sees it (pure): its title (the call's, else one known from
/// the turn), what it would do, its options; nullopt when it offers none.
std::optional<Permission> readPermission(const QJsonObject &params, const QHash<QString, QString> &titles,
                                         const QHash<QString, QString> &kinds = {});

/// The agent's sign-in methods (initialize's authMethods).
QList<AuthMethod> readAuthMethods(const QJsonValue &methods);

/// What the client offers the agent besides the protocol itself (served by the app).
struct Capabilities
{
    bool files = false;     ///< fs/read_text_file, fs/write_text_file
    bool terminals = false; ///< terminal/create, /output, /wait_for_exit, /kill, /release
};

/// The agent behind one connection: the handshake, its sessions, the turns of conversations.
class Client : public QObject
{
    Q_OBJECT
public:
    /// `version`: the app's, for the agent's log (clientInfo).
    explicit Client(Connection::Send send, const QString &version = QString(), QObject *parent = nullptr);
    ~Client() override;

    /// What the client offers (before the first turn: the handshake says it).
    void setCapabilities(const Capabilities &capabilities) { m_capabilities = capabilities; }
    /// The agent's requests for files and terminals: (the conversation, the request's id, its
    /// method, its params) — answered with respond() / respondError(), at once or later. Only for
    /// a session the client knows; others are refused.
    using Handler = std::function<void(const QString &key, const QJsonValue &id, const QString &method,
                                       const QJsonObject &params)>;
    void setHandler(Handler handler) { m_handler = std::move(handler); }
    void respond(const QJsonValue &id, const QJsonValue &result) { m_connection.respond(id, result); }
    void respondError(const QJsonValue &id, int code, const QString &message)
    {
        m_connection.respondError(id, code, message);
    }

    /// Bytes the agent wrote.
    void received(const QByteArray &bytes);
    /// The agent's process ended (`why` for the user): every turn fails (Failure::Closed).
    void closed(const QString &why);

    /// Starts the turn of the conversation `key` (its session made first when it has none). False
    /// when one runs in it already.
    bool prompt(const QString &key, const TurnInput &input);
    /// Makes the session of `key` without a turn (its options and commands known then:
    /// sessionChanged(), opened()); nothing when it has one or a turn runs. False: the agent failed.
    bool open(const QString &key, const TurnInput &input);
    /// Ends the turn of `key` without a word more of it (finished/failed do not come).
    void cancel(const QString &key);
    /// The turn of `key` runs (cancelled ones not).
    bool running(const QString &key) const;
    /// Answers the question of `key`'s turn with the option `optionId` (Permission::options).
    void answer(const QString &key, const QString &optionId);
    /// Signs in with `methodId` (authRequired's) and goes on with the turn of `key`.
    void authenticate(const QString &key, const QString &methodId);
    /// Sets an option of `key`'s session (`value`: a choice's value or a bool); before the session
    /// is there, it is set when it comes. False: no such option.
    bool setOption(const QString &key, const QString &id, const QJsonValue &value);
    /// The session of `key` is forgotten (not while a turn runs): its next turn makes a new one.
    void forget(const QString &key);
    /// The conversation `key` is gone: its turn cancelled, its session closed where the agent
    /// closes sessions (`session/close`), and forgotten.
    void close(const QString &key);
    /// The conversation `from` is now `to` (a page renamed): its session goes with it.
    void rename(const QString &from, const QString &to);

    /// After the handshake: the agent's name ("" before), its version, whether it takes pictures,
    /// embedded text, a session resumed or loaded.
    QString agentName() const { return m_agentName; }
    QString agentVersion() const { return m_agentVersion; }
    bool takesImages() const { return m_images; }
    bool takesEmbedded() const { return m_embedded; }
    bool resumes() const { return m_resume || m_load; }
    bool ready() const { return m_phase == Phase::Ready; }
    /// A session of `key` exists (its next turn sends TurnInput::prompt), and its id.
    bool hasSession(const QString &key) const;
    QString sessionId(const QString &key) const;
    /// The conversation of a session ("" none).
    QString keyOf(const QString &sessionId) const;
    /// What the session of `key` keeps (empty: none).
    SessionInfo session(const QString &key) const;
    /// What the turn of `key` did so far (nullptr: none runs).
    const TurnState *turn(const QString &key) const;
    /// What the last finished turn of `key` did (nullptr: none).
    const TurnState *lastTurn(const QString &key) const;

Q_SIGNALS:
    /// The answer and the thoughts so far of `key`'s turn.
    void progress(const QString &key, const QString &text, const QString &thoughts);
    /// What the agent does now in `key`'s turn ("" nothing to say).
    void status(const QString &key, const QString &text);
    /// The tool calls or the plan of `key`'s turn changed (turn()).
    void activityChanged(const QString &key);
    /// The options, commands or usage of `key`'s session changed (session()).
    void sessionChanged(const QString &key);
    /// The session of `key` is there (`sessionId`; `resumed`: one of an earlier run).
    void sessionStarted(const QString &key, const QString &sessionId, bool resumed);
    /// open() of `key` made its session (or failed: failed() came).
    void opened(const QString &key);
    /// The agent asks the user (answer() with one of the options).
    void permissionAsked(const QString &key, const brulion::acp::Permission &permission);
    /// The agent wants the user signed in before `key`'s turn can go on (authenticate()); its
    /// message ("" none).
    void authRequired(const QString &key, const QList<brulion::acp::AuthMethod> &methods, const QString &message);
    void finished(const QString &key, const QString &text, const QString &stopReason);
    void failed(const QString &key, const brulion::acp::Failure &failure);

private:
    struct Turn
    {
        TurnInput input;
        bool openOnly = false;  // open(): no prompt
        bool cancelled = false;
        bool prompting = false; // session/prompt sent
        bool fresh = false;     // its session was made for it: the first prompt goes
        TurnState state;
        /// The agent's questions waiting, the first one shown ({its request's id, the question}).
        QList<std::pair<QJsonValue, Permission>> questions;
    };
    struct Session
    {
        QString id;                 // "" before session/new answered
        bool creating = false;      // session/new (or authenticate, load, resume) on its way
        std::optional<Turn> turn;   // the turn running (or cancelled, its answer still to come)
        std::optional<TurnInput> next; // a turn sent while a cancelled one ends
        SessionInfo info;
        QHash<QString, QJsonValue> wanted; // options set before the session came
        std::optional<TurnState> last;     // the last turn that finished
    };

    void start();
    void startTurn(const QString &key);
    void newSession(const QString &key, bool tryResume);
    void sessionReady(const QString &key, const QString &id, const QJsonObject &result, bool fresh, bool resumed);
    void applyOptions(const QString &key);
    void sendPrompt(const QString &key);
    void sendOption(const QString &key, const ConfigOption &option, const QJsonValue &value,
                    std::function<void()> then);
    void fail(const QString &key, const Failure &failure);
    void failAll(const Failure &failure);
    void endTurn(const QString &key);
    void onRequest(const QJsonValue &id, const QString &method, const QJsonObject &params);
    void onNotification(const QString &method, const QJsonObject &params);
    QJsonObject sessionParams(const TurnInput &input) const;

    Connection m_connection;
    QString m_version;
    Capabilities m_capabilities;
    Handler m_handler;
    enum class Phase { None, Starting, Ready, Failed };
    Phase m_phase = Phase::None;
    std::optional<Failure> m_startFailure;
    QString m_agentName;
    QString m_agentVersion;
    bool m_images = false;
    bool m_embedded = false;
    bool m_resume = false;
    bool m_load = false;
    bool m_close = false;
    QList<AuthMethod> m_authMethods;
    QHash<QString, Session> m_sessions; // by conversation
};

} // namespace brulion::acp

Q_DECLARE_METATYPE(brulion::acp::Permission)
Q_DECLARE_METATYPE(brulion::acp::Failure)
