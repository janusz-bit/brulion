#pragma once
// WorkspaceAgents — AI agents of the user's own as partners of the AI chat (docs/UI.md "AI agents
// (ACP)"; Workspace.chats): programs that speak the Agent Client Protocol (core acp/acp.h) —
// Claude Code through @agentclientprotocol/claude-agent-acp, Codex through codex-acp, Gemini CLI
// with `--acp`, OpenCode with `acp`, or any other — set up on this device in Settings → Extensions
// → AI agents (AppSettings::acpAgents: a name and a command line; never from the notebook, which
// comes from anywhere). A conversation whose page says `partner: acp:<id>` is answered by the
// agent `id`.
//
// Each agent runs as one process of its own, started for its first turn in the notebook's folder
// and kept while the notebook is open (several conversations answer at once, a session each); it
// signs in by itself — a subscription its own program knows how to use, a key of its own — so the
// app holds none of its credentials. What the app serves it:
//
// * Brulion's tools: `brulion-cli mcp --vault <notebook>` (the program next to the app, else on
//   PATH) as an MCP server of each session, when the agent's "Brulion's tools" is on. With "Let it
//   change the app" (AgentConfig::appAccess) also `--app-control`: the tools that drive and change
//   the running app live (docs/CLI.md "Live changes"), reaching it with a grant the app makes for
//   the agent's process (setAppGrants(): RemoteControl in the desktop app) — its token only in the
//   tools' environment (BRULION_APP_TOKEN, with BRULION_APP_CLIENT the agent's name), revoked when
//   the process stops. Every session with Brulion's tools also gets `--app-chat` and a grant of
//   the app's chat alone for its conversation (setChatGrants(): BRULION_APP_CHAT_TOKEN): the agent
//   may give another partner of the chat a task — a sub-conversation of its own, whose answer it
//   waits for (ChatControl, docs/UI.md "Agents delegate"). A sub-conversation's agent gets no tools
//   that change the app, and one MaxDepth levels down no chat tools (setLineage()).
// * The notebook's files (`fs/read_text_file`, `fs/write_text_file`): through the Workspace, so a
//   page the agent changes is seen at once by the editor and kept in its history — only files of
//   the notebook's folder, never an encrypted page.
// * Terminals (`terminal/*`): a command the agent runs as a program of the app, its output kept
//   (as much as the agent asks for) and shown in the chat with its tool call.
//
// A new session (the first turn of a conversation for this agent) gets what the agent needs to know
// — the notebook's folder, the conversation's page it must leave alone, the page the conversation
// is about (its text where the agent takes it), the conversation so far — and every later turn
// only the user's newest message; pictures go as pictures where the agent takes them, else as
// links to their files. The session's id is kept in the notebook's device state (LocalStore
// "acp/sessions", with how many messages it has seen): after a restart the conversation's next
// turn resumes it where the agent can — unless the conversation changed meanwhile (messages
// another partner wrote, or another device), when a new session gets the conversation so far.
//
// The options of a session (its model, mode, effort …) are the chat's chips: one chosen is set on
// the session and remembered for the agent (AgentConfig::options, with the options its last session
// offered: AgentConfig::known), so every new session — and a conversation before its first message
// — starts with it. What the agent asks (may it write a file, run a command; or sign in) is the
// conversation's question (questionText(), answer()); Stop cancels the turn (session/cancel) and
// answers a question waiting "cancelled".
//
// A program runs only in the desktop app (available()): Android and the browser start no programs.
// An agent on another computer does run there (remoteAvailable(): Android and the desktop): the
// user's computer runs it with `brulion-cli acp serve` (docs/CLI.md "AI agents for other
// devices"), the device paired once with a code of that computer (pair()); its "process" is then
// an acp::RemoteStream to that computer (core acp/remote.h) — the protocol and everything above it
// as for a program of this device, only the agent's files, terminals and Brulion's tools are that
// computer's, so this side offers none and names that computer's notebook folder (AgentConfig::root)
// to the agent.
//
//   agents->setAgents({{u"claude"_s, u"Claude Code"_s, u"npx -y @agentclientprotocol/claude-agent-acp"_s}});
//   agents->run(path, u"acp:claude"_s, history, u"Dom"_s);  // → answered / failed / status / partial
//
// Single-threaded (the GUI thread).

#include "acp/acp.h"
#include "acp/remote.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <functional>
#include <memory>
#include <optional>

class QProcess;
class QTimer;

namespace brulion::shell {

class Workspace;

/// An agent the user set up on this device (AppSettings::acpAgents).
/// An agent's session option as ChatAgentOptions shows it: id, name, description, category,
/// boolean, current, currentName and its choices (value, name, description, group); a select of
/// just "Off" and "On" (in any order, also true/false, enabled/disabled, yes/no) adds onOff, its
/// onValue and offValue, and isOn.
QVariantMap agentOptionRow(const acp::ConfigOption &option);

struct AgentConfig
{
    QString id;      ///< stable, in pages' `partner: acp:<id>`: letters, digits, "-"
    QString name;    ///< as the chat shows it
    QString command; ///< the program and its arguments, as a shell would split them
    bool tools = true; ///< the session gets Brulion's MCP server
    /// The options chosen for its sessions ({option id → value}).
    QJsonObject options{};
    /// The options its last session offered (acp::ConfigOption::toJson()), for the chat before a
    /// session is there.
    QJsonArray known{};
    /// An agent on another computer (`brulion-cli acp serve` there): its address ("" a program of
    /// this device), the agent's id there, the computer's name and notebook folder, and this
    /// device's token for it (from the app's secrets, never in the settings or the notebook).
    QString url{};
    QString remote{};
    QString host{};
    QString root{};
    QString token{};
    /// "Let it change the app while it runs" (settings row key `app`; a program of this device
    /// with Brulion's tools only): its tools get `--app-control` and a grant of the app.
    bool appAccess = false;
    friend bool operator==(const AgentConfig &, const AgentConfig &) = default;
    bool isRemote() const { return !url.isEmpty(); }
    /// The same program: what it runs is not changed (its name and remembered options may be).
    bool sameProgram(const AgentConfig &o) const
    {
        return id == o.id && command == o.command && tools == o.tools && url == o.url && remote == o.remote
               && token == o.token && appAccess == o.appAccess;
    }
};

class WorkspaceAgents : public QObject
{
    Q_OBJECT
public:
    explicit WorkspaceAgents(Workspace *workspace, QObject *parent = nullptr);
    ~WorkspaceAgents() override;

    /// The prefix of an agent's partner id ("acp:claude").
    static constexpr QLatin1StringView Prefix{"acp:"};
    static bool isAgent(const QString &partner);
    /// Programs can run here (the desktop app).
    static bool available();
    /// Agents on another computer can be reached from here: everywhere (the browser where that
    /// computer allows the app's site, `acp serve --allowed-origin`).
    static bool remoteAvailable();
    /// `agent` can answer here.
    static bool runs(const AgentConfig &agent) { return agent.isRemote() ? remoteAvailable() : available(); }
    /// This device's name as a computer lists it among its paired devices.
    static QString deviceName();
    /// Why a stream to `host` ended, as it follows "The AI agent “…” stopped: ".
    static QString remoteEndText(const QString &host, acp::RemoteStream::End why, const QString &detail);
    /// The error of a pairing or of the computer's info in words.
    static QString remoteErrorText(const QString &address, const acp::RemoteError &error);
    /// The agents offered when one is added: {id, name, command} of Claude Code, Codex, Gemini CLI,
    /// OpenCode.
    static QList<AgentConfig> presets();
    /// The program that serves Brulion's tools (brulion-cli next to the app, else on PATH; "" none).
    static QString toolsProgram();
    /// The program and its arguments of `command` (split as a shell would). On Windows a program
    /// found as a batch file (`npx` is `npx.cmd`) runs through `cmd.exe /c`, which CreateProcess
    /// needs for it; `find` looks a program up on PATH (QStandardPaths::findExecutable).
    static QStringList commandLine(const QString &command, bool windows,
                                   const std::function<QString(const QString &)> &find);
    /// The text a new session starts with (English: the agent reads it): the notebook's folder,
    /// the conversation's page, the page it is about, whether Brulion's tools are there and (`app`)
    /// whether they may change the running app.
    static QString context(const QString &root, const QString &conversation, const QString &page, bool tools,
                           bool app = false, bool chat = false);
    /// The conversation so far as one text for a new session ("" when there is none before the
    /// newest message): `history` as WorkspaceChats builds it ({role, content}).
    static QString transcript(const QVariantList &history);
    /// `text`'s lines from `line` (1-based; 0: the first) on, at most `limit` (0: all) — as
    /// fs/read_text_file asks for them.
    static QString lines(const QString &text, int line, int limit);
    /// `output` kept to its last `limit` bytes, cut at a character's start (terminal/output).
    static QByteArray keepLast(const QByteArray &output, qint64 limit, bool *cut = nullptr);
    /// What a session's usage says in the chat: "4 % of the context · 0.21 USD" ("" nothing known).
    static QString usageText(const acp::Usage &usage);

    /// Grants of the running app for agents with appAccess (RemoteControl::grant / revokeGrant in
    /// the desktop app): `grant` makes one for an agent's name and gives its token ("" none),
    /// `revoke` ends it. Unset (Android, tests): such an agent gets no live tools.
    using GrantFunction = std::function<QString(const QString &client)>;
    using RevokeFunction = std::function<void(const QString &token)>;
    void setAppGrants(GrantFunction grant, RevokeFunction revoke);
    /// Grants of the running app's chat alone (RemoteControl::chatGrant: hello and the chat's
    /// methods), one per conversation of an agent with Brulion's tools — so its tools may hand
    /// another partner a task as a sub-conversation of that conversation. Unset: no chat tools.
    void setChatGrants(GrantFunction grant, RevokeFunction revoke);
    /// The conversation whose chat grant `token` is ("" none: no such grant, or a full one).
    QString grantConversation(const QString &token) const;
    /// How deep a conversation is among sub-conversations (ChatLineage::depth) and whether it is
    /// one: a sub-conversation's agent gets no live tools, and one at ChatLineage::MaxDepth no chat
    /// grant. Unset: every conversation is the user's own.
    void setLineage(std::function<int(const QString &path)> depth, std::function<bool(const QString &path)> isChild);

    void setAgents(const QList<AgentConfig> &agents);
    QList<AgentConfig> agents() const { return m_agents; }
    /// Who may answer: {id "acp:<id>", label, extension ("AI agent")} of each agent set up.
    QVariantList partners() const;
    /// An agent's name ("" for no agent set up here).
    QString label(const QString &partner) const;
    /// What the program of agent `id` said it is in its initialize (its name; "" not started).
    QString programName(const QString &id) const;

    /// Runs the turn of the conversation `path` with the agent `partner`: `history` its messages
    /// ({role, content, images}, the user's last; images named as in notes), `page` the name of the
    /// page it is about ("" none). Its end comes as answered() or failed() — at once when the
    /// agent is not set up here.
    void run(const QString &path, const QString &partner, const QVariantList &history, const QString &page);
    /// Stops the turn of `path`: nothing more comes of it.
    void stop(const QString &path);
    bool running(const QString &path) const;

    /// The conversations whose agent on another computer was working on an answer when this device
    /// lost the stream it came in — Android ended the app meanwhile — and that still wait for that
    /// answer (their newest message is the one it answers).
    QStringList recoverable() const;
    /// The answer of that turn from the computer, which kept it (GET sessions/<id>/turn): running()
    /// meanwhile, status() "… is still working on …", then answered() or failed() as a turn's
    /// (also after the stream of a turn that runs ended: the computer was not reached for long,
    /// or it no longer knew the stream).
    void recover(const QString &path);
    /// A turn of an agent on another computer runs: on Android a foreground service keeps the app
    /// working meanwhile with the screen off or behind another app (AppController →
    /// platform::setAgentWork(): its notification says remoteWork()).
    bool remoteWorking() const { return m_remoteWorking; }
    /// What those turns do, for that notification: its title ("Claude Code is answering…", "2 AI
    /// agents are answering…", or the first question waiting: "Claude Code asks: Write Dom.md"), its
    /// line (the computer and the conversation), and whether every one of them waits on the user's
    /// answer (nothing comes from that computer meanwhile). Empty while none runs.
    struct RemoteWork
    {
        QString title;
        QString text;
        bool waiting = false;
        friend bool operator==(const RemoteWork &, const RemoteWork &) = default;
    };
    RemoteWork remoteWork() const;
    /// The conversations whose turn an agent on another computer answers (recovered ones too).
    QStringList remoteTurns() const;

    /// The question the turn of `path` waits on: its text (a line of title, then what it would
    /// do) and options; "" / {} none.
    QString questionText(const QString &path) const;
    QStringList questionOptions(const QString &path) const;
    /// The change of a file the question is about: {path, oldText, newText}; {} none.
    QVariantMap questionDiff(const QString &path) const;
    /// Answers it with `option` (one of questionOptions()).
    void answer(const QString &path, const QString &option);
    /// The plan the question asks the user to approve (Claude Code leaving its plan mode),
    /// Markdown; "" none.
    QString questionPlan(const QString &path) const;

    /// How the agent's questions in the conversation `path` are answered ("Approvals", as T3
    /// Code's permission modes): "ask" — each one is shown —, "edits" — changes of files (edit,
    /// move) and reads are allowed by themselves —, "all" — everything is, but a plan to approve
    /// and a sign-in. Kept per conversation in the notebook's device state ("chat/approvals"),
    /// never in its page (a page that syncs from elsewhere must not allow anything); a
    /// conversation never set has the mode chosen last ("" path: that one).
    QString approvals(const QString &path) const;
    /// Sets it for `path` (and as the mode of the next new conversations); "" `path`: only that.
    void setApprovals(const QString &path, const QString &mode);
    /// The option an agent's question is answered with by itself under `mode` ("" none: it is
    /// asked): the first allow_once option, else allow_always.
    static QString autoAnswer(const QString &mode, const acp::Permission &permission);

    /// The files of the notebook the last turn of `path` changed (as T3 Code's turn diffs), each
    /// {path, oldText, newText, existed, exists, known}: `known` false when the text before is not
    /// (the change came before the app saw the call): no undo for it. Followed through the
    /// agent's tool calls that name files (edit, move, delete) and its fs/write_text_file; kept
    /// until its next turn starts (in memory). [] none — or the agent is on another computer.
    QVariantList changes(const QString &path) const;
    /// Puts each of them back as it was before the turn (a file it made goes to the trash), unless
    /// it changed since: {undone (how many), skipped (their paths)}.
    QVariantMap undoChanges(const QString &path);
    /// Forgets them (the card is closed).
    void forgetChanges(const QString &path);
    /// A file's text before a tool call from a snapshot of it: a snapshot taken after the agent
    /// changed the file already (it holds the diff's new text once, not its old one) is turned
    /// back with the diff; `known` false when that cannot be told. Pure.
    static QString textBefore(const QString &snapshot, const std::optional<acp::Diff> &diff, bool done,
                              bool *known = nullptr);

    /// What the running turn of `path` does: its tool calls {title, status, kind, output, diffPath,
    /// diffOld, diffNew} (a terminal's output as it comes; the change of a file it made) and its
    /// plan {content, status}; [] none.
    QVariantList tools(const QString &path) const;
    QVariantList plan(const QString &path) const;
    /// The options of the session of `path` — of its agent before there is one (`partner`, for a
    /// conversation not written yet: path "") — as the chat shows them: {id, name, description,
    /// boolean, current (text: a choice's value, "true"/"false"), currentName, choices: [{value,
    /// name, description, group}]}.
    QVariantList options(const QString &path, const QString &partner) const;
    /// Sets one (on the session when there is one, for the agent's next sessions anyway).
    void setOption(const QString &path, const QString &partner, const QString &id, const QString &value);
    /// The slash commands of the session of `path` (or the agent's last ones): {name, description, hint}.
    QVariantList commands(const QString &path, const QString &partner) const;
    /// How much of the session's context is used and what it cost ("" nothing known).
    QString usage(const QString &path) const;

    /// Starts the agent `id` and makes a session in the notebook's folder without a question:
    /// checked(id, ok, what it says — its name and version, or why it failed).
    void check(const QString &id);

    /// Pairs this device with a computer that runs `brulion-cli acp serve` at `address`, with a
    /// code of its `acp pair`: paired(true, what it says, its agents — each an AgentConfig with
    /// url, remote, host, root, token and a name; no id yet) or paired(false, why, {}). Without a
    /// code the computer may know this device by itself (its user's in their tailnet: `acp serve
    /// --tailscale`) — or a token for it is here already (another of its agents) —, else it asks
    /// for the code: codeNeeded(address), then paired(false, "", {}). `only`: of its agents just
    /// that one (its id there).
    void pair(const QString &address, const QString &code, const QString &only = QString());
    /// Tests: the transport to other computers (default one over the network of its own).
    void setTransport(sync::HttpTransport *transport) { m_transport = transport; }
    /// Tests: every agent left as an app the system ended leaves them — no stream ended, no
    /// process stopped, nothing said of the turns.
    void abandon();

    /// The notebook closed: every agent stops, without a word of its turns.
    void reset();

Q_SIGNALS:
    void answered(const QString &path, const QString &text);
    void failed(const QString &path, const QString &error);
    void status(const QString &path, const QString &text);
    void partial(const QString &path, const QString &text, const QString &thoughts);
    /// A question of `path` came or went.
    void questionChanged(const QString &path);
    /// tools() or plan() of `path` changed.
    void activityChanged(const QString &path);
    /// options(), commands() or usage() changed (of `path`, or of an agent's: "").
    void sessionChanged(const QString &path);
    /// changes() of `path` changed (its turn ended, they were undone or forgotten).
    void changesChanged(const QString &path);
    /// approvals() changed.
    void approvalsChanged();
    /// An agent's question was answered by itself under the conversation's approvals: what, for
    /// the user to see.
    void autoAnswered(const QString &path, const QString &text);
    /// The options of agent `id` were chosen or learnt: AppSettings keeps them.
    void remembered(const QString &id, const QJsonObject &options, const QJsonArray &known);
    void checked(const QString &id, bool ok, const QString &text);
    void remoteWorkingChanged();
    /// remoteWork() may have changed.
    void remoteWorkChanged();
    /// A turn of `path` is recovered (recover(), or its stream ended while it ran): the chat shows it
    /// as running.
    void recovering(const QString &path);
    void paired(bool ok, const QString &text, const QList<brulion::shell::AgentConfig> &agents);
    /// A pairing without a code: the computer at `address` wants its code (pair()).
    void codeNeeded(const QString &address);

private:
    struct Process;
    struct Terminal;
    struct Question
    {
        QString text;
        QStringList options;
        QStringList ids; ///< the agent's option ids (a permission), the methods' (a sign-in)
        bool signIn = false;
        acp::Diff diff;  ///< the change of a file it would make (path "": none)
        QString plan{};  ///< a plan to approve (Markdown), "" none
        /// A question of a turn recovered from another computer: the computer's id of it (it is
        /// answered there: acp::answerRemoteQuestion()).
        QString remote{};
    };
    /// A question as the chat shows it: "<name> asks: <title>", what it would do, its options.
    static Question permissionQuestion(const QString &name, const acp::Permission &permission);

    Process *processFor(const AgentConfig &config);
    void connectClient(Process *p);
    void startProgram(Process *p);
    void startRemote(Process *p);
    /// The agent's program or stream is gone (`why` for the user): its turns fail.
    void ended(const QString &id, Process *p, const QString &why);
    void stopProcess(const QString &id);
    sync::HttpTransport *transport();
    QString failureText(const QString &name, const acp::Failure &failure) const;
    void setQuestion(const QString &path, const Question &question);
    void clearQuestion(const QString &path);
    void finish(const QString &path);
    /// The rename of `path` its turn waited for (m_renamedDuringTurn): the session and its record go
    /// to the new name.
    void followRename(const QString &path);
    const AgentConfig *config(const QString &id) const;
    /// What every turn of the agent's process `p` sends: the folder, Brulion's tools (with its
    /// grant's live tools unless `child`, and with `chatToken` the chat's), the options.
    acp::TurnInput baseInput(const AgentConfig &config, const Process *p, const QString &chatToken = QString(),
                             bool child = false) const;
    /// The grants of `p`'s process end (it stopped): its own and its conversations' chat grants.
    void releaseGrant(Process *p);
    /// The chat grant of the conversation `path` answered by `config`'s process (made when first
    /// asked); "" none (no grant provider, no tools, too deep).
    QString chatGrantFor(const AgentConfig &config, const QString &path);
    void handle(Process *p, const QString &key, const QJsonValue &id, const QString &method, const QJsonObject &params);
    void readFile(acp::Client *client, const QJsonValue &id, const QJsonObject &params);
    void writeFile(acp::Client *client, const QString &key, const QJsonValue &id, const QJsonObject &params);

    // The files a running turn changes (m_tracking, by conversation) and those its last one
    // changed (m_changes).
    struct FileChange {
        QString path;   ///< the notebook's
        QString before; ///< its text before the turn
        bool existed = false;
        bool known = true;
        QString after{};
        bool exists = false;
    };
    QHash<QString, QList<FileChange>> m_tracking;
    QHash<QString, QList<FileChange>> m_changes;
    /// Keeps the text of `file` (the notebook's) before the turn of `path` changes it, unless kept
    /// already; `diff` and `done`: the call that names it (textBefore()).
    void snapshot(const QString &path, const QString &file, const std::optional<acp::Diff> &diff = std::nullopt,
                  bool done = false);
    /// The turn's tool calls so far: their files kept before they change.
    void followTools(acp::Client *client, const QString &path);
    /// The turn of `path` ended: what it changed, as it is now.
    void settleChanges(const QString &path);
    void terminalRequest(Process *p, const QString &key, const QJsonValue &id, const QString &method,
                         const QJsonObject &params);
    void endTerminals(const QString &agent, const QString &key = QString());
    /// The notebook's path of an absolute `file` the agent names ("" outside the notebook).
    QString notebookPath(const QString &file) const;
    void learn(const QString &agent, const QString &key);
    void activityDue(const QString &path);
    /// remoteWorking() again after the turns changed.
    void noteWorking();
    /// The turns recovered from another computer (recover()).
    struct Recovery {
        QString agent;
        QString session;
        int seen = 0; ///< what the session had seen before the turn (Stored::seen)
        QString turn; ///< the turn's id (acp::TurnInput::turnId)
        int failures = 0;
        qint64 failingSince = -1;
        /// The computer's id of the question answered here last (a poll that crossed the answer
        /// does not ask it again).
        QString answered{};
    };
    void pollRecovery(const QString &path);
    void recovered(const QString &path, const acp::RemoteTurn &turn, const acp::RemoteError &error);
    /// The user answered the question `q` of the recovered turn of `path` with `optionId` ("": no).
    void answerRecovered(const QString &path, const Question &q, const QString &optionId);
    // The device's record of conversations' sessions (LocalStore "acp/sessions").
    struct Stored
    {
        QString agent;
        QString session;
        int seen = 0;
        /// A turn of an agent on another computer runs: the conversation's messages when it began
        /// (its answer is the next one), 0 none; and the turn's id (acp::TurnInput::turnId).
        int pending = 0;
        QString turn{};
    };
    std::optional<Stored> stored(const QString &path) const;
    void store(const QString &path, const std::optional<Stored> &entry);

    Workspace *m_workspace = nullptr;
    QList<AgentConfig> m_agents;
    QHash<QString, Process *> m_processes; // by agent id
    QHash<QString, QString> m_turns;       // conversation → agent id, of the turns running
    QHash<QString, int> m_seen;            // conversation → the messages its running turn will have seen
    /// Conversations renamed while their turn ran (from → to): their session and its record go along
    /// when the turn ends (followRename()).
    QHash<QString, QString> m_renamedDuringTurn;
    QHash<QString, Question> m_questions;  // by conversation
    QHash<QString, Terminal *> m_terminals; // by id
    /// The output of terminals released in a turn that runs (shown with their tool calls until it
    /// ends): terminal id → {conversation, output}.
    QHash<QString, std::pair<QString, QByteArray>> m_released;
    QHash<QString, QJsonArray> m_commands;  // agent id → the slash commands of its last session
    QHash<QString, QString> m_checks;       // agent id → the key of its check
    QSet<QString> m_activityPending;        // conversations whose activityChanged() is due
    QTimer *m_activityTimer = nullptr;      // (a terminal's output: ten times a second at most)
    int m_nextTerminal = 1;
    bool m_remoteWorking = false;
    QHash<QString, Recovery> m_recovering; // conversation → its turn recovered from another computer
    /// The running turns of agents on another computer: conversation → its messages then, turn id.
    QHash<QString, std::pair<int, QString>> m_remoteTurns;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true); // (requests on their way after reset())
    sync::HttpTransport *m_transport = nullptr;
    std::unique_ptr<sync::HttpTransport> m_ownTransport;
    GrantFunction m_grant;
    RevokeFunction m_revoke;
    GrantFunction m_chatGrant;
    RevokeFunction m_chatRevoke;
    std::function<int(const QString &)> m_depth;
    std::function<bool(const QString &)> m_isChild;
    /// The chat grants: token → the agent whose process has it and its conversation (a rename
    /// follows it).
    struct ChatGrant {
        QString agent;
        QString conversation;
    };
    QHash<QString, ChatGrant> m_chatGrants;
};

} // namespace brulion::shell
