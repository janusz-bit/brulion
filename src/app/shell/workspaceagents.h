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
//   PATH) as an MCP server of each session, when the agent's "Brulion's tools" is on.
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
    friend bool operator==(const AgentConfig &, const AgentConfig &) = default;
    bool isRemote() const { return !url.isEmpty(); }
    /// The same program: what it runs is not changed (its name and remembered options may be).
    bool sameProgram(const AgentConfig &o) const
    {
        return id == o.id && command == o.command && tools == o.tools && url == o.url && remote == o.remote
               && token == o.token;
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
    /// Agents on another computer can be reached from here (the desktop app and Android; not the
    /// browser, whose requests that computer refuses).
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
    /// the conversation's page, the page it is about, whether Brulion's tools are there.
    static QString context(const QString &root, const QString &conversation, const QString &page, bool tools);
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

    void setAgents(const QList<AgentConfig> &agents);
    QList<AgentConfig> agents() const { return m_agents; }
    /// Who may answer: {id "acp:<id>", label, extension ("AI agent")} of each agent set up.
    QVariantList partners() const;
    /// An agent's name ("" for no agent set up here).
    QString label(const QString &partner) const;

    /// Runs the turn of the conversation `path` with the agent `partner`: `history` its messages
    /// ({role, content, images}, the user's last; images named as in notes), `page` the name of the
    /// page it is about ("" none). Its end comes as answered() or failed() — at once when the
    /// agent is not set up here.
    void run(const QString &path, const QString &partner, const QVariantList &history, const QString &page);
    /// Stops the turn of `path`: nothing more comes of it.
    void stop(const QString &path);
    bool running(const QString &path) const;

    /// The question the turn of `path` waits on: its text (a line of title, then what it would
    /// do) and options; "" / {} none.
    QString questionText(const QString &path) const;
    QStringList questionOptions(const QString &path) const;
    /// The change of a file the question is about: {path, oldText, newText}; {} none.
    QVariantMap questionDiff(const QString &path) const;
    /// Answers it with `option` (one of questionOptions()).
    void answer(const QString &path, const QString &option);

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
    /// url, remote, host, root, token and a name; no id yet) or paired(false, why, {}).
    void pair(const QString &address, const QString &code);
    /// Tests: the transport to other computers (default one over the network of its own).
    void setTransport(sync::HttpTransport *transport) { m_transport = transport; }

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
    /// The options of agent `id` were chosen or learnt: AppSettings keeps them.
    void remembered(const QString &id, const QJsonObject &options, const QJsonArray &known);
    void checked(const QString &id, bool ok, const QString &text);
    void paired(bool ok, const QString &text, const QList<brulion::shell::AgentConfig> &agents);

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
    };

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
    const AgentConfig *config(const QString &id) const;
    acp::TurnInput baseInput(const AgentConfig &config) const;
    void handle(Process *p, const QString &key, const QJsonValue &id, const QString &method, const QJsonObject &params);
    void readFile(acp::Client *client, const QJsonValue &id, const QJsonObject &params);
    void writeFile(acp::Client *client, const QJsonValue &id, const QJsonObject &params);
    void terminalRequest(Process *p, const QString &key, const QJsonValue &id, const QString &method,
                         const QJsonObject &params);
    void endTerminals(const QString &agent, const QString &key = QString());
    /// The notebook's path of an absolute `file` the agent names ("" outside the notebook).
    QString notebookPath(const QString &file) const;
    void learn(const QString &agent, const QString &key);
    void activityDue(const QString &path);
    // The device's record of conversations' sessions (LocalStore "acp/sessions").
    struct Stored
    {
        QString agent;
        QString session;
        int seen = 0;
    };
    std::optional<Stored> stored(const QString &path) const;
    void store(const QString &path, const std::optional<Stored> &entry);

    Workspace *m_workspace = nullptr;
    QList<AgentConfig> m_agents;
    QHash<QString, Process *> m_processes; // by agent id
    QHash<QString, QString> m_turns;       // conversation → agent id, of the turns running
    QHash<QString, int> m_seen;            // conversation → the messages its running turn will have seen
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
    sync::HttpTransport *m_transport = nullptr;
    std::unique_ptr<sync::HttpTransport> m_ownTransport;
};

} // namespace brulion::shell
