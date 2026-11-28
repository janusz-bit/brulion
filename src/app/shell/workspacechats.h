#pragma once
// WorkspaceChats — the conversations with the AI of the open notebook (docs/UI.md "AI chat"; QML:
// Workspace.chats). A conversation is a page in the folder AI/ (core chat/chat.h: `- 🧑 …` and
// `- 🤖 …` messages, each with its time). Sending a message writes it into its page — a new
// conversation first gets its page, named after the question, and a line in today's journal that
// links to it — and then runs the turn of its partner (WorkspaceExtensions::runChat: the built-in
// assistant's command "chat", or an extension's chat command the page's `partner:` names; an AI
// agent of the user's own, `partner: acp:<id>`, through WorkspaceAgents) with the
// conversation's messages — whole turns from the oldest left out when they do not fit the model
// (chat::firstSent, historyBudget) — and the page it is about; the answer is written into the page
// as the AI's message. Pictures go with a message as attachments of the notebook embedded in it
// (addPicture…: stored when picked, dropped again when taken off before sending).
//
// Every conversation has a turn of its own (ChatTurn, turn(path)): several answer at once, one
// turn per conversation. The model on the device answers one turn at a time: the others wait in
// line (ChatTurn.queued, its place in line), and start in the order they were sent. A turn goes
// on whatever is on screen: the chat view may close, the answer still lands in its page (which
// follows a rename; a page deleted meanwhile ends the turn).
//
//   Workspace.chats.send("", "Co mam jutro do zrobienia?", "Dom")   // a new one: its path
//   Workspace.chats.send(path, "A pojutrze?")                         // the next message
//   Workspace.chats.turn(path).running / queued / status / partial / thoughts / failure
//   Workspace.chats.stop(path) / retry(path) / dismissFailure(path)
//   Workspace.chats.rename(path, "Nowa nazwa") / remove(path)        // the list's menu
//   Workspace.chats.setPinned(path, true) / setArchived(path, true)   // its frontmatter's marks
//   Workspace.chats.partners() / asking(path, questionSerial) / answer(path, option)
//   Workspace.chats.askAside(path, "Ile kosztuje bilet?")               // a side chat (/btw): its path
//
// A side chat (`/btw`) is a conversation of its own beside another one (chat::sideChat): a copy of
// it with the side question, answered by its partner in a turn of its own; never in the list, never
// unread nor notified — the conversation's view shows it (lineage()->sideChats()). A sub-conversation
// an AI agent started with "wake" (ChatControl) wakes its parent when its turn ends (setWake()): the
// parent gets a message (chat::wakeMessage) through its queue — after its running turn, kept over a
// restart — and its partner a turn; MaxWakesPerHour at most, an archived parent's waits for the user.
// Conversations untouched for a while are archived by themselves when the device's setting says so
// (setAutoArchiveDays(): when the notebook is ready, then every hour). New conversations are answered
// by the device's default partner (setDefaultPartner(), defaultPartner) — the assistant when it is
// not set up here. The notifications of answers, failures and questions are kept in the workspace's
// NotificationCenter too; an AI agent's own go through it (ChatControl's chatNotify).
//
// Single-threaded (the GUI thread).

#include "agentlimits.h"
#include "agentshare.h"
#include "ai/ai.h"
#include "agentusage.h"
#include "chatlineage.h"
#include "chatschedules.h"
#include "chatsecrets.h"
#include "hookserver.h"

#include "acp/appchats.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include <functional>
#include <utility>

namespace brulion::gitcli {
class Client;
}

namespace brulion::platform {
struct CapturedPhoto;
struct PickedFile;
}

namespace brulion::shell {

class Workspace;
class WorkspaceAgents;
class WorkspaceExtensions;
struct AgentConfig;
class WorkspaceChats;

/// The turn of one conversation (WorkspaceChats::turn): whether it runs or waits for the model
/// on the device, what it does, the answer as the model writes it, why it failed.
class ChatTurn : public QObject {
    Q_OBJECT
    /// The conversation (its page now: a rename moves it).
    Q_PROPERTY(QString path READ path NOTIFY stateChanged FINAL)
    /// A turn runs — or waits in line for the model on the device (queued).
    Q_PROPERTY(bool running READ running NOTIFY stateChanged FINAL)
    /// It waits for the model on the device, which answers another conversation; `place` in line
    /// (1: next).
    Q_PROPERTY(bool queued READ queued NOTIFY stateChanged FINAL)
    Q_PROPERTY(int place READ place NOTIFY stateChanged FINAL)
    /// What it does now (b.status: "Czytam „Dom”…"), "" while the model thinks.
    Q_PROPERTY(QString status READ status NOTIFY stateChanged FINAL)
    /// Why its last turn failed ("" none): shown in the conversation, with Try again.
    Q_PROPERTY(QString failure READ failure NOTIFY stateChanged FINAL)
    /// Of the running turn, the earlier messages the model did not get (they do not fit it).
    Q_PROPERTY(int omitted READ omitted NOTIFY stateChanged FINAL)
    /// The answer as the model writes it ("" none yet: it thinks, or calls a tool); each request
    /// of the turn starts anew. Markdown, the model's words.
    Q_PROPERTY(QString partial READ partial NOTIFY partialChanged FINAL)
    /// `partial` cut where its block being written starts (WorkspaceChats::splitWritten()): its
    /// finished blocks, which change only when a block is finished, and that block.
    Q_PROPERTY(QString partialSettled READ partialSettled NOTIFY settledChanged FINAL)
    Q_PROPERTY(QString partialTail READ partialTail NOTIFY partialChanged FINAL)
    /// The end of what the model thinks meanwhile, a few lines ("" none), and all of it (shown
    /// when the thoughts are opened).
    Q_PROPERTY(QString thoughts READ thoughts NOTIFY partialChanged FINAL)
    Q_PROPERTY(QString allThoughts READ allThoughts NOTIFY partialChanged FINAL)
    /// How fast the model writes now ("16,7 tok/s"); "" when not known.
    Q_PROPERTY(QString speed READ speed NOTIFY partialChanged FINAL)
    /// An AI agent's turn: its tool calls ({title, status, kind, output}) and its plan ({content,
    /// status}) as they go; [] otherwise.
    Q_PROPERTY(QVariantList tools READ tools NOTIFY activityChanged FINAL)
    Q_PROPERTY(QVariantList plan READ plan NOTIFY activityChanged FINAL)
    /// The computer whose app runs this turn ("" this device): a conversation the desktop app on
    /// another computer of the user's runs, followed here as it works (ComputerChats) — its question
    /// answered, Stop and the next message go there.
    Q_PROPERTY(QString remoteHost READ remoteHost NOTIFY stateChanged FINAL)

public:
    explicit ChatTurn(const QString &path, QObject *parent = nullptr);

    QString path() const { return m_path; }
    bool running() const { return m_running; }
    bool queued() const { return m_queued; }
    int place() const { return m_place; }
    QString status() const { return m_status; }
    QString failure() const { return m_failure; }
    int omitted() const { return m_omitted; }
    QString partial() const { return m_partial; }
    QString partialSettled() const { return m_settled; }
    QString partialTail() const { return m_tail; }
    QString thoughts() const { return m_thoughts; }
    QString allThoughts() const { return m_allThoughts; }
    QString speed() const { return m_speed; }
    QVariantList tools() const { return m_tools; }
    QVariantList plan() const { return m_plan; }
    QString remoteHost() const { return m_remoteHost; }

Q_SIGNALS:
    void stateChanged();
    void activityChanged();
    /// partial or thoughts changed (at most ten times a second).
    void partialChanged();
    /// partialSettled changed: a block of the answer was finished.
    void settledChanged();

private:
    friend class WorkspaceChats;
    void setPartial(const QString &text, const QString &thoughts, const QString &speed);
    void clearPartial();
    void setActivity(const QVariantList &tools, const QVariantList &plan);

    QString m_path;
    QString m_key; // the path the running turn was started with (its answer comes with it)
    bool m_running = false;
    bool m_queued = false;
    int m_place = 0;
    QString m_status;
    QString m_failure;
    int m_omitted = 0;
    QString m_partial;
    QString m_settled;
    QString m_tail;
    QString m_thoughts;
    QString m_allThoughts;
    QString m_speed;
    QVariantList m_tools;
    QVariantList m_plan;
    QString m_remoteHost;       // another computer's turn: that computer's name (WorkspaceChats::mirror)
    QString m_remoteQuestion;   // … what it asks, as one string (a change: questionsMoved)
    QStringList m_remoteWaiting; // … the messages waiting there
    bool m_asking = false; // its question was seen (questionsMoved)
    qint64 m_cameBack = 0; // when its failure or question came (ms since the epoch), 0 none
    QString m_nextPartial; // what comes, said by m_partialTimer
    QString m_nextThoughts;
    QList<std::pair<qint64, int>> m_samples; // the request's {writing ms, tokens}, for its speed
    QElapsedTimer m_clock;                   // since the turn started (its work log's time)
    QTimer m_partialTimer;
};

class WorkspaceChats : public QObject {
    Q_OBJECT
    /// How many conversations' turns run or wait now.
    Q_PROPERTY(int runningCount READ runningCount NOTIFY stateChanged FINAL)
    /// Every running turn is on screen in a chat view (isShown): what the AI does is shown
    /// there, so the top bar's ✨ need not spin (no frame drawn for it).
    Q_PROPERTY(bool runningShown READ runningShown NOTIFY runningShownChanged FINAL)
    /// The characters of a conversation the model gets at most (chat::firstSent's budget): the
    /// model on the device holds little, a provider on the network a lot.
    Q_PROPERTY(int historyBudget READ historyBudget NOTIFY settingsChanged FINAL)
    /// The model takes pictures (not the model on the device that reads text only).
    Q_PROPERTY(bool seesPictures READ seesPictures NOTIFY settingsChanged FINAL)
    /// The model is the one on the device (its turns wait in line for it).
    Q_PROPERTY(bool onDevice READ onDevice NOTIFY settingsChanged FINAL)
    /// A photo can be taken for a message (Android's camera app; platform/capture.h).
    Q_PROPERTY(bool cameraAvailable READ cameraAvailable CONSTANT FINAL)
    /// Pictures for a message come from the browser's own file picker (the WebAssembly build:
    /// pickPicturesInBrowser(), not the FilePicker of the other builds).
    Q_PROPERTY(bool browserPicker READ browserPicker CONSTANT FINAL)
    /// A message can be dictated (the system's speech recognizer, or one of the app's own).
    Q_PROPERTY(bool dictationAvailable READ dictationAvailable NOTIFY dictationChanged FINAL)
    /// Dictation listens now.
    Q_PROPERTY(bool listening READ listening NOTIFY dictationChanged FINAL)
    /// Dictation can listen now (false: the app's own recognizer has no speech model yet).
    Q_PROPERTY(bool dictationReady READ dictationReady NOTIFY dictationChanged FINAL)
    /// What dictation heard is being made into text (the app's own recognizer).
    Q_PROPERTY(bool recognizing READ recognizing NOTIFY dictationChanged FINAL)
    /// How long dictation has listened and the longest it listens, in ms (-1 / 0: not told).
    Q_PROPERTY(int dictationElapsed READ dictationElapsed NOTIFY dictationElapsedChanged FINAL)
    Q_PROPERTY(int dictationLimit READ dictationLimit NOTIFY dictationChanged FINAL)
    /// Changes whenever a question of a turn comes or goes (asking() and the rest ask again).
    Q_PROPERTY(int questionSerial READ questionSerial NOTIFY questionsChanged FINAL)
    /// Changes whenever an AI agent's session options, commands or usage change (agentOptions()
    /// and the rest ask again).
    Q_PROPERTY(int agentSerial READ agentSerial NOTIFY agentsChanged FINAL)
    /// Changes whenever a conversation's messages waiting to be sent change (queued() and the rest
    /// ask again).
    Q_PROPERTY(int queueSerial READ queueSerial NOTIFY queueChanged FINAL)
    /// Changes whenever the files an AI agent's last turn changed change (changes() asks again).
    Q_PROPERTY(int changesSerial READ changesSerial NOTIFY changesChanged FINAL)
    /// AI agents answer here: programs of this device (the desktop app), or agents on another
    /// computer (also Android).
    Q_PROPERTY(bool agentsAvailable READ agentsAvailable CONSTANT FINAL)
    /// A pairing with another computer is on its way (pairAgentHost()).
    Q_PROPERTY(bool agentPairing READ agentPairing NOTIFY agentPairingChanged FINAL)
    /// "Share agents with the phone": this computer's agents for the user's other devices, and the
    /// agents other computers offer in the notebook (agentshare.h).
    Q_PROPERTY(brulion::shell::AgentShare *share READ share CONSTANT FINAL)
    /// The subscription limits of the agents that are Claude Code or Codex (agentlimits.h).
    Q_PROPERTY(brulion::shell::AgentLimits *limits READ limits CONSTANT FINAL)
    /// What the agents that are Claude Code or Codex used, from their own session logs (agentusage.h).
    Q_PROPERTY(brulion::shell::AgentUsage *usage READ usage CONSTANT FINAL)
    /// Which conversations AI agents started from which (sub-conversations: chatlineage.h).
    Q_PROPERTY(brulion::shell::ChatLineage *lineage READ lineage CONSTANT FINAL)
    /// Scheduled conversations (chatschedules.h).
    Q_PROPERTY(brulion::shell::ChatSchedules *schedules READ schedules CONSTANT FINAL)
    /// The secrets AI agents ask the user for in a private card (chatsecrets.h).
    Q_PROPERTY(brulion::shell::ChatSecrets *secrets READ secrets CONSTANT FINAL)
    /// The webhooks of scheduled conversations: their server (hookserver.h).
    Q_PROPERTY(brulion::shell::HookServer *hooks READ hooks CONSTANT FINAL)
    /// The AI of Settings → Extensions → AI is set up: regenerateTitle() can ask it.
    Q_PROPERTY(bool canRegenerateTitles READ canRegenerateTitles NOTIFY titlesChanged FINAL)
    /// Changes whenever a title's regeneration starts or ends (regeneratingTitle() asks again).
    Q_PROPERTY(int titleSerial READ titleSerial NOTIFY titlesChanged FINAL)
    /// Changes whenever a thread's checkpoints change (checkpoints() asks again).
    Q_PROPERTY(int checkpointSerial READ checkpointSerial NOTIFY checkpointsChanged FINAL)
    /// Changes whenever a turn's work log is kept (workLog() asks again).
    Q_PROPERTY(int workLogSerial READ workLogSerial NOTIFY workLogChanged FINAL)
    /// Changes whenever a draft is kept or forgotten (draft() asks again: Code mode's sidebar shows a
    /// project's draft of a new thread).
    Q_PROPERTY(int draftSerial READ draftSerial NOTIFY draftsChanged FINAL)
    /// Who answers a new conversation (setDefaultPartner()): the device's choice while it answers
    /// here (one of partners()), else "" — the built-in assistant.
    Q_PROPERTY(QString defaultPartner READ defaultPartner NOTIFY defaultPartnerChanged FINAL)
    /// Changes whenever the conversations the app on another computer runs change (computerChats()
    /// asks again).
    Q_PROPERTY(int computerSerial READ computerSerial NOTIFY computerChatsChanged FINAL)

public:
    WorkspaceChats(Workspace *workspace, WorkspaceExtensions *extensions, QObject *parent = nullptr);

    /// The characters of a conversation a model on the network gets at most (the request's own
    /// limit is ai::MaxRequestChars, with the tools and their results) …
    static constexpr int NetworkBudget = 120000;
    /// … and the model on the device (LocalAi::ContextTokens, some three characters a token,
    /// with room for the assistant's instructions, tools, their results and the answer).
    static constexpr int DeviceBudget = 16000;

    int runningCount() const;
    bool runningShown() const { return m_runningShown; }
    int historyBudget() const;
    bool seesPictures() const;
    bool onDevice() const;

    /// `markdown` being written as {its finished blocks, the block being written}: cut before the
    /// last line that starts a block at the margin after a blank line, outside a code block
    /// ("" and all of it when there is none). Each part renders as it would within the whole.
    static std::pair<QString, QString> splitWritten(const QString &markdown);
    /// Tokens a second over the last `windowMs` of `samples` ({writing ms, tokens} in order; a
    /// window shorter than half of it: over all of them); 0 when they do not tell.
    static double tokensPerSecond(const QList<std::pair<qint64, int>> &samples, qint64 windowMs = 2000);
    /// The end of `thoughts` the chat shows: at most about `chars` characters, from a word on.
    static QString thoughtsTail(const QString &thoughts, int chars = 240);

    /// The turn of the conversation `path` (made when first asked; the same object after a
    /// rename); "": a new conversation's, which only fails (the assistant off, a page not saved).
    Q_INVOKABLE brulion::shell::ChatTurn *turn(const QString &path);
    /// The turn of `path` runs (or waits in line) / waits in line — without making a ChatTurn.
    bool isRunning(const QString &path) const;
    bool isQueued(const QString &path) const;
    /// What the conversation does, in a word: "asking" (its turn waits for the user's answer),
    /// "queued", "running", "failed" (`*failure`: why), else "idle" — without making a ChatTurn.
    QString stateOf(const QString &path, QString *failure = nullptr) const;

    /// Writes `text` and `pictures` (vault paths, addPicture…) into the conversation `path` and
    /// runs the AI's turn; "" `path`: a new conversation about the page `page` (its name as a link
    /// names it; "" for none) answered by `partner` ("" the built-in assistant). Returns the
    /// conversation's path, "" when nothing was sent: no text nor picture, a turn of it running,
    /// the assistant off (said in the turn's failure), no notebook, a page that could not be
    /// written. `parent`: a new conversation is a sub-conversation of that one (its page names it:
    /// an AI agent's task for another partner, ChatControl). `wakes`: the end of this turn wakes
    /// that conversation (setWake(), armed before the turn starts).
    Q_INVOKABLE QString send(const QString &path, const QString &text, const QString &page = QString(),
                             const QStringList &pictures = QStringList(), const QString &partner = QString(),
                             const QString &parent = QString(), const QString &wakes = QString());

    // ---- threads of Code mode (docs/CODE.md) ------------------------------------------------------
    /// A new thread of the project `project` (Workspace.projects): a conversation whose page says
    /// `project:` — and `worktree:` / `branch:` of the git worktree it works in ("" the project's
    /// folder) —, answered by the AI agent `partner` ("" the default partner when it is an agent,
    /// else the first agent), its first message `text` and `pictures`; no line in the journal.
    /// Its path ("" when it did not start: why in turn("")'s failure).
    /// `pullRequest` ("" none): the pull request it works on ("#123 Title"), its page's `pr:` — the
    /// agent's first context names it.
    Q_INVOKABLE QString startThread(const QString &project, const QString &text,
                                    const QStringList &pictures = QStringList(), const QString &partner = QString(),
                                    const QString &worktree = QString(), const QString &branch = QString(),
                                    const QString &pullRequest = QString());
    /// startThread() as a sub-conversation of `parent` whose end wakes `wakes` ("" none): an AI
    /// agent's or a script's task in a project (ChatControl's chatStart with `project`).
    QString startThreadFrom(const QString &project, const QString &text, const QString &partner, const QString &parent,
                            const QString &wakes, const QStringList &pictures = {});

    /// The conversations whose turn of this device ended after `since`, newest first, with when —
    /// kept in the notebook's device state ("chat/ended", a day at most), so a turn that ended before
    /// the app shared its conversations, or before it started again, is still known (ChatControl's
    /// chatLive lists those of the last minutes). Turns of another computer followed here are not.
    QList<std::pair<QString, QDateTime>> endedSince(const QDateTime &since) const;
    /// Where the thread `path` works: {project, projectName, worktree, branch, folder (the
    /// worktree's, else the project's), known (the project is on this device), exists (the folder
    /// is there)}; {} for a conversation of the notebook. A project of another computer (it runs
    /// there): also remote true, host (the computer's name), remoteFolder (the folder there) —
    /// folder "" and exists false (nothing of it is on this device).
    Q_INVOKABLE QVariantMap threadPlace(const QString &path) const;
    /// The folder the thread `path` works in ("" none: not a thread, or its project or worktree
    /// is not on this device).
    Q_INVOKABLE QString threadFolder(const QString &path) const;
    /// The AI agents among partners() (a thread is answered by one) …
    Q_INVOKABLE QVariantList agentPartners() const;
    /// … and the one a new thread gets: the default partner when it is an agent, else the first
    /// agent ("" none set up).
    Q_INVOKABLE QString threadPartner() const;
    /// The checkpoints of the thread `path`, oldest first: [{id, at ("yyyy-MM-dd HH:mm"), message
    /// (the user's message the turn answered, counted from 1)}] — git snapshots of its folder taken
    /// before each of its agent's turns (gitcli checkpoints: the Changes panel shows what a turn
    /// changed and reverts it). Kept in the notebook's device state ("code/checkpoints"), at most
    /// MaxCheckpoints a thread (older ones dropped from git too). `serial` only makes a binding ask
    /// again. The newest turn's checkpoint also goes into the thread's page (`checkpoint:` in its
    /// frontmatter), so every device of the notebook knows it — the one the turn ran on, or the
    /// computer of a project of another computer (WorkspaceAgents::checkpointMade()): a checkpoint
    /// of the page not in this device's list is listed last (`at` "").
    Q_INVOKABLE QVariantList checkpoints(const QString &path, int serial = 0) const;
    /// The newest one's id ("" none).
    Q_INVOKABLE QString lastCheckpoint(const QString &path, int serial = 0) const;
    /// The checkpoint taken before the turn of the thread's message `number` (the user's, counted
    /// from 1 as in the page; "" none): "Edit from here" can put the files back to it.
    Q_INVOKABLE QString checkpointOf(const QString &path, int number, int serial = 0) const;
    /// Puts the thread's folder back as checkpoint `id` had it (gitcli::Client::restore: its files
    /// back, the ones added since deleted; HEAD, the index and the branch stay) without waiting:
    /// checkpointRestored(path, ok, message). False when it cannot start (no folder of this device,
    /// an unknown checkpoint).
    Q_INVOKABLE bool restoreCheckpoint(const QString &path, const QString &id);
    static constexpr int MaxCheckpoints = 30;
    int checkpointSerial() const { return m_checkpointSerial; }
    /// What an AI agent's turn did, kept once it answered (as T3 Code's "Worked for …"): the answer
    /// `number` (the page's messages counted from 1) → {seconds, tools: [{title, kind, status,
    /// output, paths, diffPath, diffOld, diffNew}], plan: [{content, status}]} — {} when not known (no tool
    /// call and no plan, another device's turn, older than what is kept). Kept in the notebook's
    /// device state ("chat/worklog"): MaxWorkLogTurns turns a conversation, MaxWorkLogConversations
    /// conversations, an output's last MaxWorkLogOutput characters, a change's texts up to
    /// MaxWorkLogDiff characters. `serial` only makes a binding ask again.
    Q_INVOKABLE QVariantMap workLog(const QString &path, int number, int serial = 0) const;
    static constexpr int MaxWorkLogTurns = 50;
    static constexpr int MaxWorkLogConversations = 200;
    static constexpr int MaxWorkLogCalls = 200;
    static constexpr int MaxWorkLogOutput = 4000;
    static constexpr int MaxWorkLogDiff = 40000;
    int workLogSerial() const { return m_workLogSerial; }
    /// A turn's tools and plan bounded as workLog() keeps them (pure; tested).
    static QJsonObject workLogEntry(int number, qint64 seconds, const QVariantList &tools, const QVariantList &plan);
    int draftSerial() const { return m_draftSerial; }
    /// Sets the thread's worktree and branch ("" both: back to the project's folder).
    Q_INVOKABLE bool setThreadWorktree(const QString &path, const QString &worktree, const QString &branch);
    /// The threads that work in `folder` (a worktree's, or a project's own: the threads without a
    /// worktree), their paths — not archived ones only, every thread of the notebook.
    Q_INVOKABLE QStringList threadsIn(const QString &folder) const;
    /// A turn of a thread that works in `folder` runs (or waits in line, or saves its checkpoint):
    /// its branch must not be switched under it.
    Q_INVOKABLE bool runningIn(const QString &folder) const;
    /// "Mark unread" (Code mode's thread menu): the conversation is unread again, as if its answer
    /// had just come while no view showed it.
    Q_INVOKABLE void markUnread(const QString &path);
    /// The AI's turn again on the conversation as it is (its last message the user's): after a
    /// failure or a stop. False when it cannot run now.
    Q_INVOKABLE bool retry(const QString &path);
    /// Stops the turn of `path` (running or in line): nothing is written for it (the user's
    /// message stays).
    Q_INVOKABLE void stop(const QString &path);
    /// Stops every turn.
    Q_INVOKABLE void stopAll();
    /// Stops every turn an AI agent on another computer answers (Android's notification of them:
    /// its Stop).
    void stopRemoteAgents();
    /// Stops the turn the model on the device answers and those in line for it (Android's
    /// notification of the model at work: its Stop).
    void stopOnDevice();
    /// Forgets the failure shown in `path`.
    Q_INVOKABLE void dismissFailure(const QString &path);
    /// The page `path` is a conversation: in the folder AI/ with a message.
    Q_INVOKABLE bool isConversation(const QString &path) const;
    /// The folder of the conversations ("AI").
    Q_INVOKABLE QString folder() const;
    /// A chat view shows the conversation `path` (shown true) or no longer does (false): a question
    /// of its turn is then answered there, not in the dialog (ShellExtensionQuestion). Counted: a
    /// tab and the side panel may both show it.
    Q_INVOKABLE void showing(const QString &path, bool shown);

    // ---- the conversations the app on the user's other computers runs (computerchats.h) ---------
    int computerSerial() const { return m_computerSerial; }
    /// The conversations whose turn runs, waits or asks in the desktop app on another computer of the
    /// user's (its agent sharing on: docs/CODE.md "The computer's own turns on the phone"), and those
    /// that ended there lately, the running ones first: [{path, name, state ("running" | "queued" |
    /// "asking" | "failed" | "idle"), host, question, failure, status, project, partnerName, here (this
    /// device has its page)}]. Each one's turn here follows it (ChatTurn.remoteHost).
    Q_INVOKABLE QVariantList computerChats(int serial = 0) const;
    /// The app on the computer of the project `project` (another computer's, whose app shares its
    /// conversations now) starts this device's new threads of it: startThreadThere().
    Q_INVOKABLE bool startsThere(const QString &project) const;
    /// A new thread of `project` started by the app on its computer — answered by `partner` (this
    /// device's agent of that computer; "" its default), in `worktree` there ("" the project's folder)
    /// — whose turn this device follows: the request's number (0: not started — pictures do not go
    /// there yet, no such computer), threadStartedThere() says how it went.
    Q_INVOKABLE int startThreadThere(const QString &project, const QString &text, const QStringList &pictures,
                                     const QString &partner = QString(), const QString &worktree = QString(),
                                     const QString &branch = QString());
    /// The page of `path` as the computer running it has it, while its messages outnumber this
    /// device's copy (the notebook's sync is slower) — ChatMessageModel shows it —, else "".
    Q_INVOKABLE QString computerPage(const QString &path) const;
    /// A chat view shows the conversation `path`.
    Q_INVOKABLE bool isShown(const QString &path) const;

    // ---- the list's menu ------------------------------------------------------------------------
    /// The conversation's name without its date ("Co mam jutro" of "AI/2026-11-15 Co mam jutro.md").
    Q_INVOKABLE QString nameOf(const QString &path) const;
    /// Renames the conversation to `name`, its date kept in front (links to it follow:
    /// Workspace::renamePage); its new path, "" when it could not be (no name, one in use).
    Q_INVOKABLE QString rename(const QString &path, const QString &name);
    /// Moves the conversation to the trash (its turn stops); the trash's path ("" on failure), for
    /// Undo (Workspace::restore).
    Q_INVOKABLE QString remove(const QString &path);
    /// Pins the conversation on top of the list (`on`) or unpins it / archives it (out of the
    /// list, into its folded "Archived") or brings it back: its page's frontmatter (chat::setMark,
    /// the notebook's language), so the user's other devices sort it so too. False when it could
    /// not be written.
    Q_INVOKABLE bool setPinned(const QString &path, bool on);
    Q_INVOKABLE bool setArchived(const QString &path, bool on);
    /// T3 Code's "Regenerate title" (chattitles.cpp): the AI of Settings → Extensions → AI names the
    /// conversation `path` anew from its messages (codeai::titleContext: its first and newest ones)
    /// and its name now — kept when it fits, the page renamed (rename(): its date kept) when the AI
    /// gives another: titleRegenerated(). False when nothing was asked: no AI set up, no
    /// conversation with words at `path`, a regeneration of it on its way.
    Q_INVOKABLE bool regenerateTitle(const QString &path);
    /// A regeneration of `path`'s title is on its way (`serial`: titleSerial, for bindings).
    Q_INVOKABLE bool regeneratingTitle(const QString &path, int serial = 0) const;
    bool canRegenerateTitles() const;
    int titleSerial() const { return m_titleSerial; }

    // ---- side chats (/btw) ------------------------------------------------------------------------
    /// "Ask a side question" (`/btw <question>`, the conversation's menu): a side chat of the
    /// conversation `path` — a page of its own in AI/, a copy of the conversation so far with
    /// `question` and `pictures` after it (chat::sideChat; no journal line) — whose turn runs now
    /// (the conversation's partner; an AI agent's session of its own). Its path, "" when nothing
    /// was asked: no question, `path` no conversation or a side chat itself, the assistant off (said
    /// in the turn's failure of ""), a page not written.
    Q_INVOKABLE QString askAside(const QString &path, const QString &question,
                                 const QStringList &pictures = QStringList());
    /// `text` (the field's) is the app's command `/btw` — "/btw" alone or before a space or a line
    /// break, any letter case — and its question (what follows it, trimmed; "" none).
    Q_INVOKABLE bool isAside(const QString &text) const;
    Q_INVOKABLE QString asideQuestion(const QString &text) const;
    /// The conversation the side chat `path` is beside ("" none: ChatLineage::sideOf).
    Q_INVOKABLE QString sideOf(const QString &path) const;
    /// The AI's last message of `path` (its Markdown; "" none) — a side chat's answer brought into
    /// its conversation's field (quote()).
    Q_INVOKABLE QString lastAnswer(const QString &path) const;
    /// The trash's paths of what the last remove() moved there: the conversation's, then its side
    /// chats' (for one Undo: AppController.toastRestore).
    Q_INVOKABLE QStringList lastRemoved() const { return m_removed; }

    // ---- a sub-conversation that wakes its parent ---------------------------------------------------
    /// The most times one conversation is woken in an hour: the next wake waits for the user (held in
    /// its queue, the message says why) — two agents cannot wake each other for ever.
    static constexpr int MaxWakesPerHour = 10;
    /// When the turn of `child` ends — answered, failed, stopped by the user — or stops to ask the
    /// user, the conversation `parent` gets a message saying so (chat::wakeMessage: its answer
    /// quoted, cut at chat::MaxWakeChars) and its partner a turn: queued behind a running turn of it
    /// (queue(); its waiting messages are kept over a restart). Once — a question wakes it and the
    /// turn's end again. A parent deleted meanwhile is not woken; an archived one, or one woken
    /// MaxWakesPerHour times in the last hour, gets the message held in its queue (Resume sends it).
    /// Kept in the notebook's device state ("chat/wake"), following renames of both. "" `parent`
    /// takes it away.
    void setWake(const QString &child, const QString &parent);
    /// The end of `child`'s turn will wake its parent (setWake()).
    Q_INVOKABLE bool wakesParent(const QString &child) const;

    // ---- conversations untouched for a while, archived by themselves (T3 Code's auto-settle) ------
    /// Settings → Extensions → AI → "Archive conversations untouched for" (AppSettings, 0: never):
    /// when the notebook is ready and every hour after it, archiveUntouched().
    void setAutoArchiveDays(int days);
    int autoArchiveDays() const { return m_autoArchiveDays; }
    /// Archives every conversation untouched for autoArchiveDays() (none when 0): not pinned nor
    /// archived, no side chat, its turn neither running nor asking nor failed, not unread, without
    /// waiting messages, a draft, a sub-conversation that will wake it, not on screen — and its last
    /// message (its time, else its page's last change) older than that before Workspace::now().
    /// The paths archived (autoArchived() follows when there are any).
    QStringList archiveUntouched();
    /// Something holds the conversation `path` where it is (no settling or archiving by itself): its
    /// turn runs, asks or failed, it is unread or on screen, messages wait in its queue, it has a
    /// draft, a sub-conversation will wake it, it is a side chat. True while the notebook is not ready.
    bool heldBack(const QString &path);

    // ---- what needs the user: unread answers, failures, questions; notifications ----------------
    /// An answer came while no chat view showed the conversation — until one shows it. Kept in the
    /// notebook's device state (LocalStore "chat/unread"), never in the page: it syncs no change.
    bool isUnread(const QString &path) const;
    /// When the conversation came back to the user (its unread answer, its failure, its question;
    /// ms since the epoch), 0 when nothing of it waits: the list puts such ones first.
    qint64 cameBack(const QString &path) const;
    /// Why its last turn failed ("" none), without making a ChatTurn.
    QString failureOf(const QString &path) const;
    /// When the user hears of an answer, a failure or a question by a system notification
    /// (AppSettings::aiChatNotify): "always" (also while the app is in front, when no chat view
    /// shows the conversation), "background" (only while the app is not active: the default) or
    /// "never". Never for a conversation shown in an active window.
    void setNotifyMode(const QString &mode);
    QString notifyMode() const { return m_notifyMode; }
    /// Whether the app is active now (tests; default: QGuiApplication::applicationState()).
    void setAppActive(std::function<bool()> active)
    {
        m_appActive = std::move(active);
        updateFollowing();
    }

    // ---- who answers a new conversation ---------------------------------------------------------
    /// The device's default partner (AppSettings::aiDefaultPartner: "" the assistant, `acp:<id>`,
    /// an extension's chat command). defaultPartner() gives it while it answers here.
    void setDefaultPartner(const QString &partner);
    QString defaultPartner() const;

    // ---- who answers ----------------------------------------------------------------------------
    /// Who may answer: the built-in assistant, the extensions' chat commands
    /// (WorkspaceExtensions::chatPartners()) and the AI agents set up on this device
    /// (WorkspaceAgents::partners()): {id, label, extension}.
    Q_INVOKABLE QVariantList partners() const;
    /// A partner's name as the chat shows it ("" for one not running or not set up here).
    Q_INVOKABLE QString partnerLabel(const QString &partner) const;
    /// The AI agents of this device (AppSettings::acpAgents).
    void setAgents(const QList<AgentConfig> &agents);
    WorkspaceAgents *agents() const { return m_agents; }
    int agentSerial() const { return m_agentSerial; }
    bool agentsAvailable() const;
    /// The partner is an AI agent ("acp:…").
    Q_INVOKABLE bool isAgent(const QString &partner) const;
    /// An AI agent's tool calls (ChatTurn.tools) in words: "Read 3 files · ran 2 commands"
    /// (chattools.h; ChatActivity's summary line).
    Q_INVOKABLE QString toolSummary(const QVariantList &tools) const;
    /// An AI agent's options for the conversation `path` ("" a new one) answered by `partner`
    /// (WorkspaceAgents::options(): {id, name, description, boolean, current, currentName,
    /// choices}); `serial`: agentSerial.
    Q_INVOKABLE QVariantList agentOptions(const QString &path, const QString &partner, int serial = 0) const;
    /// The choices of one of them ({value, name, description, group}).
    Q_INVOKABLE QVariantList agentChoices(const QString &path, const QString &partner, const QString &option,
                                          int serial = 0) const;
    Q_INVOKABLE void setAgentOption(const QString &path, const QString &partner, const QString &option,
                                    const QString &value);
    /// The slash commands matching what is typed (`typed` starts with "/"; {name, description,
    /// hint, app}, at most 12): the app's own `/btw` (a side question, `app` true: never sent to an
    /// agent) in a conversation that is no side chat, whoever answers it, then an AI agent's; []
    /// when `typed` is no command being written.
    Q_INVOKABLE QVariantList agentCommands(const QString &path, const QString &partner, const QString &typed,
                                           int serial = 0) const;
    /// How much of the agent's context the conversation uses and what it cost ("" nothing known).
    Q_INVOKABLE QString agentUsage(const QString &path, int serial = 0) const;
    /// Settings → AI agents → Check: agentChecked(id, ok, text) follows.
    Q_INVOKABLE void checkAgent(const QString &id);
    /// Settings → AI agents → Add → An agent on another computer: pairs with the computer at
    /// `address` with a code of its `brulion-cli acp pair` — agentsPaired() (its agents, for
    /// AppSettings) and agentHostPaired(ok, what it says) follow.
    Q_INVOKABLE void pairAgentHost(const QString &address, const QString &code);
    /// Settings → AI agents → an agent another computer offers in the notebook (AgentShare::offers):
    /// pairs without a code — that computer knows this device in their tailnet, or this device
    /// has a token for it already — and adds just that agent; agentHostNeedsCode(address) when the
    /// computer wants its code.
    Q_INVOKABLE void addOfferedAgent(const QString &address, const QString &agent);
    AgentShare *share() const { return m_share; }
    AgentLimits *limits() const { return m_limits; }
    AgentUsage *usage() const { return m_usage; }
    ChatLineage *lineage() const { return m_lineage; }
    ChatSchedules *schedules() const { return m_schedules; }
    ChatSecrets *secrets() const { return m_secrets; }
    HookServer *hooks() const { return m_hooks; }
    /// The answers of agents on another computer that went on with a turn while this device was
    /// gone (Android ended the app): fetched from there (WorkspaceAgents::recover()). When the
    /// notebook is ready and when the agents change.
    void recoverAgentTurns();
    bool agentPairing() const { return m_agentPairing; }
    /// The conversation's partner (its page's `partner:`, "" the built-in assistant) — of a thread,
    /// the agent that answers it on this device (WorkspaceAgents::threadPartnerFor()).
    Q_INVOKABLE QString partnerOf(const QString &path) const;
    /// Sets who answers the conversation from now on (its page's frontmatter; "" the assistant).
    Q_INVOKABLE bool setPartner(const QString &path, const QString &partner);

    // ---- reported and hidden answers (Report response) ------------------------------------------
    /// Marks the AI's message `number` (1-based, the page's messages in order) of the conversation
    /// `path` "reported" or "hidden" — "" takes its mark away — in its page's frontmatter
    /// (chat::setFlag; the view shows it folded). False for no such AI's message, an unknown
    /// flag, a page that could not be written.
    Q_INVOKABLE bool setFlag(const QString &path, int number, const QString &flag);
    /// Who answers the conversation `path`, as a report of an answer says it (English): "Brulion's
    /// AI assistant · Anthropic · claude-opus-5-5", "Chat partner pirat/rozmowa (an extension) ·
    /// …", "AI agent Claude Code · model Opus 5.5" — the model of the settings now (aireport::
    /// modelDescription), an agent's from its session's options.
    Q_INVOKABLE QString modelName(const QString &path) const;

    // ---- what a turn asks (a change to allow, an agent's permission or sign-in) ----------------
    int questionSerial() const { return m_questionSerial; }
    /// The turn of `path` waits for the user's choice (`serial`: questionSerial, so a binding asks
    /// again).
    Q_INVOKABLE bool asking(const QString &path, int serial = 0) const;
    /// Its text — a line of title, then the details (plain text: it quotes notes) — and options.
    Q_INVOKABLE QString questionText(const QString &path, int serial = 0) const;
    Q_INVOKABLE QStringList questionOptions(const QString &path, int serial = 0) const;
    /// The change of a file an AI agent's question is about ({path, oldText, newText}; {} none).
    Q_INVOKABLE QVariantMap questionDiff(const QString &path, int serial = 0) const;
    /// Answers it with `option` (one of questionOptions()).
    Q_INVOKABLE void answer(const QString &path, const QString &option);
    /// The plan an AI agent's question asks the user to approve (Markdown as the agent wrote it:
    /// the view renders it as a message, escaped); "" none.
    Q_INVOKABLE QString questionPlan(const QString &path, int serial = 0) const;
    /// Saves that plan as a page of the notebook ("Plan – <the conversation's name>"): its path,
    /// "" when it could not be written.
    Q_INVOKABLE QString savePlan(const QString &path);

    // ---- an AI agent's approvals and the files its turn changed -----------------------------------
    /// How the agent's questions in `path` are answered: "ask", "edits", "all"
    /// (WorkspaceAgents::approvals; "" path: a new conversation's). `serial`: agentSerial.
    Q_INVOKABLE QString approvals(const QString &path, int serial = 0) const;
    Q_INVOKABLE void setApprovals(const QString &path, const QString &mode);
    /// The files the last turn of `path` changed ({path, oldText, newText, existed, exists, known};
    /// WorkspaceAgents::changes). `serial`: changesSerial.
    Q_INVOKABLE QVariantList changes(const QString &path, int serial = 0) const;
    /// Puts them back as they were before that turn: what it did, in words (a toast's).
    Q_INVOKABLE QString undoChanges(const QString &path);
    Q_INVOKABLE void forgetChanges(const QString &path);
    int changesSerial() const { return m_changesSerial; }

    // ---- pictures sent with a message -----------------------------------------------------------
    /// A picture picked from a file (a file dialog, a drop, a photo the camera took) stored as an
    /// attachment of the notebook for the next message: its vault path ("" when it is no picture
    /// or cannot be stored).
    Q_INVOKABLE QString addPicture(const QUrl &file);
    /// addPicture() of each: the vault paths of those that are pictures and could be stored.
    Q_INVOKABLE QStringList addPictures(const QList<QUrl> &files);
    /// `attached` with addPictures() of `files` after it (each path once).
    Q_INVOKABLE QStringList addPicturesTo(const QStringList &attached, const QList<QUrl> &files);
    /// The clipboard's picture stored so ("" none on it).
    Q_INVOKABLE QString addClipboardPicture();
    /// The browser's file picker for pictures, several at once (call it from a click or a tap:
    /// browsers open it only on a user's gesture): picturesPicked(this pick, their vault paths)
    /// follows once they are read and stored, picturesRefused() too when some were no pictures,
    /// too large or over the pick's total; nothing when the pick was cancelled. The pick's number
    /// (a view takes only its own pick's pictures); 0 outside the browser.
    Q_INVOKABLE int pickPicturesInBrowser();
    bool browserPicker() const;
    /// Files of the browser's picker stored as addPicture() stores a file: the vault paths of
    /// those that are pictures (by their name's extension), were read and could be stored.
    QStringList addPickedPictures(const QList<brulion::platform::PickedFile> &files);
    /// A picture of the browser's picker larger than this is refused unread (and the pick stops
    /// at this total: the browser holds the files it read in memory).
    static constexpr qint64 MaxPickedPictureBytes = 50LL * 1024 * 1024;
    static constexpr qint64 MaxPickedPicturesBytes = 200LL * 1024 * 1024;
    /// A picture added but taken off the message before it was sent: deleted again.
    Q_INVOKABLE void dropPicture(const QString &path);
    /// The address of a picture of the notebook to show (Workspace::assetUrl).
    Q_INVOKABLE QUrl pictureUrl(const QString &name, const QString &conversation) const;
    /// The vault path of a picture a message names ("" when the notebook has none).
    Q_INVOKABLE QString picturePath(const QString &name, const QString &conversation) const;

    // ---- the camera and dictation ---------------------------------------------------------------
    bool cameraAvailable() const;
    bool dictationAvailable() const;
    bool listening() const;
    bool dictationReady() const;
    bool recognizing() const;
    int dictationElapsed() const;
    int dictationLimit() const;
    /// A recognizer of the app's own was registered (platform::setSpeechRecognizer()): the chat's
    /// microphone follows it from now on.
    void useSpeechRecognizer();
    /// Asks the camera app for a photo for the field of the conversation `draft` (a draft's path,
    /// as setDraft() takes it). The photo goes into that draft — whoever waits for it, also when
    /// the app's process ended while the camera was in front (Android) and the photo comes to the
    /// next one: placePhoto(). False: no camera here, or it could not be asked.
    Q_INVOKABLE bool takePhoto(const QString &draft);
    /// Starts listening, in the UI's language: dictated(text) when it is heard, dictationFailed(why)
    /// when not ("" cancelled). False when it cannot listen.
    Q_INVOKABLE bool startDictation();
    /// Ends listening: what was heard is given (dictated()).
    Q_INVOKABLE void stopDictation();
    /// Ends listening without a text.
    Q_INVOKABLE void cancelDictation();

    // ---- messages written while a turn runs (as T3 Code's queue) ---------------------------------
    /// Messages written while the conversation's turn runs wait for it, in order, and go one by
    /// one when a turn answers (a new turn each). A turn that fails or is stopped holds them: they
    /// wait for resumeQueue() (or Send now). Kept in the notebook's device state ("chat/queued"),
    /// so they survive a restart — held then.
    int queueSerial() const { return m_queueSerial; }
    /// Adds `text` and `pictures` to the conversation `path`'s waiting messages; when no turn runs
    /// (and none are held) it is sent at once instead. False: nothing to send, no notebook.
    Q_INVOKABLE bool queue(const QString &path, const QString &text, const QStringList &pictures = QStringList());
    /// The waiting messages of `path`: {id, text, pictures, from (a wake message's sub-conversation,
    /// its name; "" none)}, in the order they go.
    Q_INVOKABLE QVariantList queued(const QString &path, int serial = 0) const;
    /// They wait for resumeQueue(): the turn before them failed or was stopped.
    Q_INVOKABLE bool queueHeld(const QString &path, int serial = 0) const;
    /// They are held and every one of them is a sub-conversation's wake message (setWake(): held for
    /// the limit, or an archived conversation).
    Q_INVOKABLE bool heldForWake(const QString &path, int serial = 0) const;
    /// Takes the waiting message `id` out (its pictures deleted again unless `keepPictures`): its
    /// text — for the field, to edit it; its pictures: takenPictures() — "" when there is none.
    Q_INVOKABLE QString unqueue(const QString &path, int id, bool keepPictures = false);
    /// The pictures of the message unqueue() or rewind() gave back last (vault paths).
    Q_INVOKABLE QStringList takenPictures() const { return m_taken; }
    /// Sends the waiting message `id` now (-1: the first): a running turn is stopped first — an
    /// agent cannot take a message in the middle of a turn — and the next go after it.
    Q_INVOKABLE bool sendNow(const QString &path, int id = -1);
    /// The held messages go on: the first is sent now.
    Q_INVOKABLE bool resumeQueue(const QString &path);
    /// `text` sent now (as T3 Code's steer, Ctrl+Enter while a turn runs): ahead of the waiting
    /// messages, the running turn stopped first; sent at once when none runs.
    Q_INVOKABLE bool steer(const QString &path, const QString &text, const QStringList &pictures = QStringList());

    // ---- going back, forking, quoting -------------------------------------------------------------
    /// The user's questions of `path`, newest first (Up in an empty field brings them back).
    Q_INVOKABLE QStringList prompts(const QString &path) const;
    /// "Edit from here": the conversation `path` without its message at `line` (the user's) and
    /// every one after it — its turn stopped, its waiting messages dropped, an agent's next turn a
    /// new session with what is left. Returns that message's text for the field (its pictures:
    /// takenPictures(); "" when nothing changed); undoRewind() puts the page back as it was.
    Q_INVOKABLE QString rewind(const QString &path, int line);
    /// Puts back what the last rewind() of `path` took out (while the page was not changed since).
    Q_INVOKABLE bool undoRewind(const QString &path);
    /// "Fork from here": a new conversation, a copy of `path` up to its message at `line`, which
    /// goes on by itself with the same partner (`fork:` names the source: docs/SYNTAX.md §14).
    /// Its path, "" when it could not be written.
    Q_INVOKABLE QString fork(const QString &path, int line);
    /// The conversation `path` was forked from ("" none): its path.
    Q_INVOKABLE QString forkedFrom(const QString &path) const;
    /// What was written in the field of the conversation `path` and not sent ("" a new
    /// conversation's): kept per conversation while another is shown, and over a restart (the
    /// notebook's device state, "chat/drafts"). setDraft() with "" forgets it.
    Q_INVOKABLE QString draft(const QString &path) const;
    /// The pictures in the field with the draft (vault paths, those still in the notebook): taken
    /// with the camera for it, or attached and not sent.
    Q_INVOKABLE QStringList draftPictures(const QString &path) const;
    /// Keeps `text` and `pictures` as the draft of `path` (both empty: none). A picture it keeps
    /// stays in the notebook when the notebook closes (those added and not sent go then).
    Q_INVOKABLE void setDraft(const QString &path, const QString &text, const QStringList &pictures = {});
    /// A message as a quote for the field ("> …" lines and a blank line).
    Q_INVOKABLE QString quote(const QString &markdown) const;
    /// The page being named at `caret` of the field's `text` (as T3 Code's @-mentions): "@" and
    /// the start of a name (not inside a word, an address), or "[[" and anything but "]": the
    /// pages it may be ({title, path, folder, start, end} — `start`..`end` the text replaced by
    /// the link), at most `limit` — not the conversation `path` itself; [] when no name is being
    /// written.
    Q_INVOKABLE QVariantList mentions(const QString &path, const QString &text, int caret, int limit = 8) const;
    /// Where a page's name being written at `caret` starts and what it is so far ({start, query};
    /// start -1: none). Pure.
    static std::pair<int, QString> mentionAt(const QString &text, int caret);
    /// `text` with the page's name being written at `caret` replaced by a link to `title` and a
    /// space ("" when none is being written), and where the caret goes then.
    Q_INVOKABLE QString withMention(const QString &text, int caret, const QString &title) const;
    Q_INVOKABLE int mentionEnd(const QString &text, int caret, const QString &title) const;

    /// The notebook closed: no turn runs.
    void reset();

Q_SIGNALS:
    void stateChanged();
    /// A message was written into the conversation `path` (the view follows it to the end).
    void written(const QString &path);
    /// showing() changed what is shown.
    void shownChanged();
    void runningShownChanged();
    /// The model's settings changed (historyBudget, seesPictures, onDevice).
    void settingsChanged();
    /// Who may answer changed (partners()).
    void partnersChanged();
    /// defaultPartner() may have changed.
    void defaultPartnerChanged();
    void checkpointsChanged();
    /// restoreCheckpoint() ended: `message` for the user.
    void checkpointRestored(const QString &path, bool ok, const QString &message);
    void workLogChanged();
    void draftsChanged();
    void questionsChanged();
    /// computerChats() changed.
    void computerChatsChanged();
    /// computerPage(`path`) changed.
    void computerPageChanged(const QString &path);
    /// startThreadThere()'s request `request` started the thread `path` there ("" and `error`: not).
    void threadStartedThere(int request, const QString &path, const QString &error);
    void agentsChanged();
    void queueChanged();
    /// rewind() took messages out of `path`: AppController offers Undo (undoRewind()).
    void rewound(const QString &path, const QString &text);
    /// wakesParent() of a conversation changed.
    void wakesChanged();
    /// archiveUntouched() archived `paths`, untouched for `days` (AppController's toast, with Undo).
    void autoArchived(const QStringList &paths, int days);
    void changesChanged();
    /// setArchived() wrote `path` archived (`on`) or brought it back — also archiveUntouched()'s
    /// (the terminals of an archived thread end: TerminalRegistry).
    void archivedChanged(const QString &path, bool on);
    /// An AI agent asks something in the conversation `path`, which no chat view shows: `text`
    /// says who asks what (AppController shows it as a toast with Open).
    void questionWaiting(const QString &path, const QString &text);
    /// isUnread() or cameBack() of a conversation changed.
    void attentionChanged();
    void titlesChanged();
    /// regenerateTitle() of `path` ended: `outcome` "renamed" (its page is `newPath` now),
    /// "unchanged" (the AI kept the name: `newPath` = `path`) or "failed" (`message` says why).
    void titleRegenerated(const QString &path, const QString &newPath, const QString &outcome, const QString &message);
    /// A turn of `path` was answered, failed or asks something, and the user hears of it
    /// (setNotifyMode()): a system notification with `title`, the conversation's name, and
    /// `text` — the answer's first line, why it failed, what it asks (workspaceintegration.cpp
    /// shows it; a click opens the conversation). Kept in the NotificationCenter as well.
    void notificationWanted(const QString &path, const QString &title, const QString &text);
    void agentChecked(const QString &id, bool ok, const QString &text);
    /// A pairing ended: `text` says with whom and which agents, or why not ("" nothing to say:
    /// agentHostNeedsCode() came).
    void agentHostPaired(bool ok, const QString &text);
    /// A pairing without a code: the computer at `address` wants its pairing code.
    void agentHostNeedsCode(const QString &address);
    /// The agents of a computer this device was just paired with (AppSettings::addRemoteAcpAgents).
    void agentsPaired(const QList<brulion::shell::AgentConfig> &agents);
    void agentPairingChanged();
    /// The options of agent `id` were chosen or learnt (AppSettings keeps them).
    void agentRemembered(const QString &id, const QJsonObject &options, const QJsonArray &known);
    /// addPictures() got files that are no pictures (or could not be stored).
    void picturesRefused();
    /// The pictures pickPicturesInBrowser()'s pick `pick` stored, in the order picked (some may
    /// have been refused).
    void picturesPicked(int pick, const QStringList &paths);
    /// A photo taken for the draft `draft` (takePhoto()) went into it: the picture `path` of the
    /// notebook. A field showing that draft puts it among its pictures.
    void photoTaken(const QString &draft, const QString &path);
    /// No photo for the draft `draft`: why, for the user.
    void captureFailed(const QString &draft, const QString &why);
    void dictationChanged();
    void dictationElapsedChanged();
    /// What dictation heard so far (a recognizer that tells), and at the end.
    void dictationPartial(const QString &text);
    void dictated(const QString &text);
    void dictationFailed(const QString &why);

private:
    /// Where a new thread works (startThread()).
    struct ThreadPlace {
        QString project;
        QString worktree;
        QString branch;
        QString pullRequest{};
    };
    QString sendMessage(const QString &path, const QString &text, const QString &page, const QStringList &pictures,
                        const QString &partner, const QString &parent, const QString &wakes, const ThreadPlace *thread);
    ChatTurn *find(const QString &path) const;
    bool runTurn(ChatTurn *turn);
    /// Starts the turn now (false: failed at once).
    bool startTurn(ChatTurn *turn);
    void startWaiting();
    void updatePlaces();
    void answered(const QString &key, const QString &text);
    void failed(const QString &key, const QString &error);
    void setStatus(const QString &key, const QString &text);
    void setWriting(const QString &key, const QString &text, const QString &thoughts, int tokens, qint64 writingMs);
    void questionsMoved();
    /// The conversation `path` as the computer running it said last (ComputerChats): its turn here
    /// follows it, or ends with it.
    void mirror(const QString &path);
    /// Another computer's turn runs for `path` (mirror()).
    bool remote(const QString &path) const;
    /// The detail of `path` as the computer whose app lists it said last (its agent's options, its
    /// messages waiting or held there), nullptr none — or this device's own turn of it runs.
    const acp::RemoteChatDetail *computerDetail(const QString &path) const;
    /// mirror()'s part for computerDetail(): a change of its held messages or options signalled.
    void mirrorExtras(const QString &path);
    /// The computers are asked while the app is in front, or while a turn of theirs runs here.
    void updateFollowing();
    /// How many messages this device's copy of `path` has.
    int localMessages(const QString &path) const;
    /// The page of a turn that ended there came (computerPage()): its notification, its view.
    void computerPageCame(const QString &path);
    /// `text` and `pictures` to another computer's turn of `path` (queued there, or steered in); false
    /// when nothing went (a picture that cannot be prepared: said in a toast).
    bool sendToComputer(const QString &path, const QString &text, const QStringList &pictures, acp::SendMode mode);
    /// The address of the computer whose app runs the next turn of `path` — a conversation that app
    /// runs or ended lately, or a thread of a project of that computer, while its app shares its
    /// conversations —, "" none (this device runs it: a thread through that computer's shared agent).
    QString computerFor(const QString &path) const;
    /// The address of the computer that runs `path` now (a message of it on its way there, or it
    /// lists it), "" none.
    QString computerRunning(const QString &path) const;
    /// `pictures` (vault paths) as a computer's app takes them: prepared for the model
    /// (Workspace::aiPicture, as this device's turn would send them) within acp's limits; nullopt
    /// when one cannot be (said in a toast naming `host`).
    std::optional<QList<acp::RemotePicture>> picturesForComputer(const QStringList &pictures,
                                                                 const QString &host) const;
    /// The pictures of a message the computer took: this device's copies deleted (that computer
    /// stored its own, which come with the notebook's sync).
    void sentThere(const QStringList &pictures);
    void setFailure(ChatTurn *turn, const QString &failure);
    void endTurn(ChatTurn *turn);
    /// endedSince()'s record of `path`'s turn ended now.
    void noteEnded(const QString &path);
    void updateRunningShown();
    bool setMark(const QString &path, int mark, bool on);
    void setUnread(const QString &path, bool unread);
    /// notificationWanted() when the mode and what is on screen say so.
    void notifyUser(const QString &path, const QString &text);
    /// Now, for cameBack(): ms since the epoch, later than the one before.
    qint64 stamp();
    qint64 m_lastStamp = 0;

    Workspace *m_workspace = nullptr;
    WorkspaceExtensions *m_extensions = nullptr;
    WorkspaceAgents *m_agents = nullptr;
    int m_questionSerial = 0;
    int m_computerSerial = 0;
    QString m_lastShown; ///< the conversation a chat view showed last (showing())
    /// Threads whose message is on its way to the app of their project's computer: their address.
    QHash<QString, QString> m_routing;
    bool m_sendHere = false; ///< sendMessage() runs the turn here (that computer's app did not take it)
    int m_nextStart = 1;     ///< startThreadThere()'s requests
    /// Turns that ended on another computer whose answer is not here yet (its notification waits for
    /// the page), by path in lower case.
    QSet<QString> m_remoteEnded;
    /// mirrorExtras(): what was seen of each conversation's computerDetail() (lower-case path).
    QHash<QString, QString> m_computerExtras;
    int m_draftSerial = 0;
    int m_agentSerial = 0;
    bool m_agentPairing = false;
    AgentShare *m_share = nullptr;
    AgentLimits *m_limits = nullptr;
    AgentUsage *m_usage = nullptr;
    ChatLineage *m_lineage = nullptr;
    ChatSchedules *m_schedules = nullptr;
    ChatSecrets *m_secrets = nullptr;
    HookServer *m_hooks = nullptr;
    QHash<QString, ChatTurn *> m_turns;         // by path, lower case
    QHash<QString, QPointer<ChatTurn>> m_byKey; // the running turns by the path they started with
    QList<QPointer<ChatTurn>> m_waiting;        // in line for the model on the device, in order
    QPointer<ChatTurn> m_onDevice;              // the turn the model on the device answers
    ChatTurn *m_stopping = nullptr;             // the turn stop() ends (its secret card: "stopped")
    QHash<QString, int> m_shown; // by path, lower case: the chat views showing it
    QStringList m_added;         // pictures added and not sent yet (dropPicture deletes only those)
    [[maybe_unused]] int m_lastPick = 0; // pickPicturesInBrowser()'s last pick (the browser build)
    bool m_runningShown = false;
    QString m_notifyMode = QStringLiteral("background");
    std::function<bool()> m_appActive;
    QString m_defaultPartner; // the device's choice (defaultPartner(): if it answers)
    // Checkpoints of threads (checkpoints()): git, the turn waiting for its snapshot (key → token).
    gitcli::Client *m_git = nullptr;
    QHash<QString, int> m_checkpointing;
    int m_checkpointToken = 0;
    int m_checkpointSerial = 0;
    int m_workLogSerial = 0;
    /// Keeps what turn `t` did (its tools and plan, how long) for the answer `number` of `path`.
    void keepWorkLog(const ChatTurn *t, const QString &path, int number);
    /// The agent's turn of `t` after a snapshot of the thread's `folder` (checkpoints()).
    void checkpointThenRun(ChatTurn *t, const QString &folder, int message, const QString &partner,
                           const QVariantList &history, const QString &page);
    void keepCheckpoint(const QString &path, const QString &folder, const QString &id, int message);
    /// The newest turn's checkpoint the thread's page names (`checkpoint:`; "" none) / sets it.
    QString pageCheckpoint(const QString &path) const;
    void notePageCheckpoint(const QString &path, const QString &id);
    /// The app is active now (m_appActive, else the application's state).
    bool appActive() const;
    QPointer<QObject> m_speech; // the recognizer connected to (platform::SpeechRecognizer)

    // The messages waiting for a turn (by path, lower case) and those held after a stop or a
    // failure; kept in the notebook's device state.
    struct Waiting {
        int id = 0;
        QString text;
        QStringList pictures;
    };
    QHash<QString, QList<Waiting>> m_queued;
    QSet<QString> m_held;
    int m_queueSerial = 0;
    int m_changesSerial = 0;
    int m_nextQueued = 1;
    bool m_queueLoaded = false;
    /// What the last rewind() of a conversation took out: {its page's content before, after}.
    QHash<QString, std::pair<QString, QString>> m_rewound;
    QStringList m_taken; // the pictures unqueue() / rewind() gave back last
    void loadQueue();
    void saveQueue();
    void queueMoved();
    /// The turn of `path` answered: its next waiting message goes (unless held).
    void sendNextQueued(const QString &path);
    /// The waiting messages of `from` follow a rename to `to` / go with a deleted page.
    void moveQueue(const QString &from, const QString &to);
    /// Connects the recognizer of now (it may come later: one of the app's own).
    void connectSpeech();
    /// Connects the camera of now (platform::photoCamera()) and takes the photos waiting for this
    /// notebook (placePhoto() each): when the notebook is ready, and when one is taken.
    void connectCamera();
    void takeCapturedPhotos();
    /// The camera's failed(): captureFailed() unless no photo was taken (`why` "").
    void cameraFailed(const QString &draft, const QString &why);
    /// The one place a photo of the camera goes: copied into the notebook as addPicture() stores a
    /// file, into the draft it was taken for (setDraft's), its cache file removed; photoTaken().
    void placePhoto(const platform::CapturedPhoto &photo);
    /// Every picture some draft keeps (lower case).
    QSet<QString> draftedPictures() const;
    /// The draft of `from` (its text and pictures) becomes `to`'s: a rename (none: nothing changes).
    void moveDraft(const QString &from, const QString &to);

    QStringList m_removed; // the trash's paths of the last remove()
    /// The side chat `path` (beside a conversation of the notebook): no unread, no notification.
    bool isSideChat(const QString &path) const;
    /// The parents a sub-conversation will wake (lower case) / heldBack() with them.
    QSet<QString> awaitedParents() const;
    bool heldBack(const QString &path, const QSet<QString> &awaited) const;

    // The sub-conversations that wake their parent (child path in lower case → the parent's path)
    // and when each parent was woken (lower case → ms since the epoch, the last hour's).
    QHash<QString, QString> m_wakes;
    bool m_wakesLoaded = false;
    QHash<QString, QList<qint64>> m_woken;
    void loadWakes();
    void saveWakes();
    /// The turn of `child` came to `wake` (`text`: its answer, failure, question): its parent woken.
    void wake(const QString &child, int wake, const QString &text);
    /// `text` in line in `path`'s queue — held (`hold`), or sent at once when nothing runs nor waits.
    void enqueue(const QString &path, const QString &text, bool hold);

    int m_autoArchiveDays = 0;
    QTimer m_settleTimer; // every hour while autoArchiveDays() > 0

    // The titles being regenerated (by path, lower case → the question on its way, its number).
    struct Titling {
        quint64 id = 0;
        ai::Pending pending;
    };
    QHash<QString, Titling> m_titling;
    quint64 m_titleId = 0;
    int m_titleSerial = 0;
    /// regenerateTitle()'s answer `id` for `path`: the page renamed, or why not (titleRegenerated());
    /// nothing for a question stopped or asked again since.
    void titleAnswered(const QString &path, quint64 id, const ai::Reply &reply);
    /// The questions of regenerateTitle() stopped (the notebook closed): no answer comes.
    void stopTitles();
};

} // namespace brulion::shell
