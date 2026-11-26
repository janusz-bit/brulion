#pragma once
// ChatControl — the AI chat for AI agents and scripts (docs/CLI.md "Conversations"): the remote
// control's chat methods (ipc/controlprotocol.h: chats, chatRead, chatStart, chatSend, chatWait,
// chatStop, chatPartners, chatSchedules, chatScheduleAdd / Set / Remove / Run, chatRequestSecret,
// chatNotify, chatLive, chatAnswer)
// answered over WorkspaceChats — `brulion-cli app chat …` and the MCP tools brulion_app_chat_* are
// its clients, and the agent sharing's `acp serve` (its grant: the user's phone, as the user).
//
// chatLive is the conversations the app runs, as they go (core acp/appchats.h: RemoteChats' JSON,
// for the phone): those whose turn runs, waits in line or asks, and those that ended within
// acp::RecentChatMinutes (Workspace::now()), newest activity first, one of them in full (`path`:
// its answer being written, its tools, plan, question, the messages waiting). Its `revision` changes
// with anything of them — WorkspaceChats' state, questions, queue, agents, a message written, a
// running turn's state, answer, thoughts, tools — at most ten times a second, and never goes back,
// not even over a restart of the app (it is at least the time in ms since the epoch); a long poll
// waits for a newer one (`since`), `timeout` ms at most. chatAnswer answers the question a turn waits
// on with one of its options — the user's alone: never an AI agent's (a grant's caller is refused).
// chatSend's `mode` "steer" sends the message now, ahead of the waiting ones, the running turn
// stopped first (WorkspaceChats::steer); chatStart with `project` takes the thread's `worktree` (a
// git worktree of the project's — under its worktrees root or one `git worktree list` lists; another
// computer's project checks its own) and `branch`.
//
// Who asks decides what may be done (Caller): a client of the open remote control (the user's
// own script) any of it; an AI agent the app started reaches it with its conversation's chat
// grant (WorkspaceAgents) — what it starts is a sub-conversation of that conversation (its page
// says `parent:`), it sends to and stops only its own sub-conversations (any depth), within the
// limits of delegation (ChatLineage: MaxDepth levels, MaxRunningChildren at once), and changes
// only the schedules it set up from there, and asks the user for a secret (chatRequestSecret:
// ChatSecrets' private card) only in its own conversation while its turn runs, and notifies the
// user (chatNotify: WorkspaceChats::notify, a click opening its conversation or a page) only from
// its own conversation. chatStart without a partner goes to the device's default
// (WorkspaceChats::defaultPartner). Reading (chats,
// chatRead, chatWait, chatPartners, chatSchedules) is open to all: the agent's notebook tools read
// every page anyway. No schedule's webhook secret is ever given or taken here.
//
// chatStart and chatSend take `wake`: instead of waiting (chatWait), the asking agent ends its turn
// and the end of the sub-conversation's turn wakes its conversation — the agent's own (a grant's),
// a script's the sub-conversation's parent (WorkspaceChats::setWake); chatStop takes it away. Side
// chats (`/btw`, the user's asides) are no conversation of `chats` nor of a name: read by path.
//
// GUI-free (the desktop's RemoteControl passes requests on; tests call it directly). Waits are
// answered when the turn's state changes or their time is up, and dropped when their client is
// gone. Single-threaded (the GUI thread).

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <functional>
#include <memory>

namespace brulion::shell {

class ChatTurn;
class Workspace;
class WorkspaceChats;

class ChatControl : public QObject {
    Q_OBJECT
public:
    explicit ChatControl(Workspace *workspace, QObject *parent = nullptr);
    ~ChatControl() override;

    /// Who asks.
    struct Caller {
        /// An AI agent the app started (a grant's connection) …
        bool granted = false;
        /// … and the conversation its grant belongs to ("" none known: it starts nothing).
        QString conversation{};
        /// Its name (the schedules it sets up say it).
        QString client{};
    };
    /// The answer: ok(result) or fail(code, message, details) once; `connected` false when the
    /// client is gone (a wait is dropped).
    struct Reply {
        std::function<void(const QJsonObject &)> ok;
        std::function<void(const QString &code, const QString &message, const QJsonObject &details)> fail;
        std::function<bool()> connected{};
    };

    /// The longest text of messages chatRead gives at once (a message cut: its offset goes on).
    static constexpr int MaxReadChars = 30000;
    /// chatWait's time when none is asked, and the longest.
    static constexpr int DefaultWaitMs = 60000;
    static constexpr int MaxWaitMs = 600000;
    /// chatLive's wait when none is asked (with `since`), and the longest.
    static constexpr int DefaultLiveWaitMs = 30000;
    static constexpr int MaxLiveWaitMs = 60000;
    /// The revision changes at most this often (ms): a turn's answer comes ten times a second.
    static constexpr int LiveIntervalMs = 100;
    /// chatLive's revision now.
    qint64 revision() const { return m_revision; }

    /// `method` is one of the chat's.
    static bool handles(const QString &method);
    void handle(const QString &method, const QJsonObject &params, const Caller &caller, const Reply &reply);

    /// The conversation `ref` names: a vault path (".md" and "AI/" optional, any letter case) or
    /// a conversation's name without its date when one alone has it; "" none (`*ambiguous`: several).
    QString resolve(const QString &ref, QStringList *ambiguous = nullptr) const;

private:
    struct Wait;
    struct LiveWait;
    void list(const QJsonObject &params, const Reply &reply);
    void read(const QJsonObject &params, const Reply &reply);
    void start(const QJsonObject &params, const Caller &caller, const Reply &reply);
    void sendTo(const QJsonObject &params, const Caller &caller, const Reply &reply);
    void wait(const QJsonObject &params, const Reply &reply);
    void stop(const QJsonObject &params, const Caller &caller, const Reply &reply);
    void partners(const Reply &reply);
    void schedules(const Reply &reply);
    void scheduleAdd(const QJsonObject &params, const Caller &caller, const Reply &reply);
    void scheduleSet(const QString &method, const QJsonObject &params, const Caller &caller, const Reply &reply);
    void requestSecret(const QJsonObject &params, const Caller &caller, const Reply &reply);
    void notify(const QJsonObject &params, const Caller &caller, const Reply &reply);
    void live(const QJsonObject &params, const Reply &reply);
    void answerQuestion(const QJsonObject &params, const Caller &caller, const Reply &reply);
    /// chatStart's thread once its worktree is known to be the project's.
    void startThreadNow(const QString &project, const QString &text, const QString &partner,
                        const QString &worktree, const QString &branch, const Reply &reply);

    // ---- chatLive
    /// acp::RemoteChats' JSON now: the conversations, `path`'s in full ("" none).
    QJsonObject liveAnswer(const QString &path);
    /// Something of a turn changed: the revision goes up within LiveIntervalMs.
    void liveChanged();
    void bumpRevision();
    /// The running turns watched, those that ended noted (Workspace::now()).
    void noteTurns();
    void finishLive(const std::shared_ptr<LiveWait> &wait);
    /// What chatLive tells of a conversation's page (read again when its size or time changed).
    struct Facts {
        qint64 mtime = -1;
        qint64 size = -1;
        bool conversation = false;
        int messages = 0;
        QString partner;
        QString project;
        QString worktree;
        QString branch;
    };
    const Facts &factsOf(const QString &path);
    /// Drops the conversations that ended longer than acp::RecentChatMinutes ago (true: some went).
    bool pruneEnded();
    /// A conversation's row: {path, name, partner, partnerName, state, failure?, question?,
    /// parent?, side?, wakes?, page?, messages, changed}.
    QJsonObject row(const QString &path, qint64 modified = -1) const;
    /// What chatWait answers for `path` now: {path, state, answer? | question? | failure?}.
    QJsonObject outcome(const QString &path) const;
    /// The conversation of `params[key]`, or the reply failed (not_found / ambiguous_ref).
    QString conversationOf(const QJsonObject &params, const Reply &reply, const QString &key = QStringLiteral("path"));
    /// May `caller` send to or stop `path`? (the reply failed when not)
    bool mayChange(const Caller &caller, const QString &path, const Reply &reply) const;
    bool ready(const Reply &reply) const;
    void checkWaits();
    void finishWait(const std::shared_ptr<Wait> &wait, bool timedOut);

    Workspace *m_workspace = nullptr;
    WorkspaceChats *m_chats = nullptr;
    QList<std::shared_ptr<Wait>> m_waits;
    QTimer m_clientCheck; // (waits of clients that left are dropped)

    qint64 m_revision = 0;
    QTimer m_liveTimer;          // the revision's next change (at most every LiveIntervalMs)
    QElapsedTimer m_sinceRevision; // since it last changed
    QTimer m_pruneTimer;         // conversations that ended lately leave the list
    QList<std::shared_ptr<LiveWait>> m_liveWaits;
    QSet<ChatTurn *> m_watched; // the turns whose changes are followed
    QSet<ChatTurn *> m_active;  // those that run, wait or ask
    struct Ended {
        QString path;
        QDateTime at;
    };
    QHash<QString, Ended> m_ended;     // lower-case path: turns that ended lately
    QHash<QString, qint64> m_activity; // lower-case path → the order of its last change
    qint64 m_activitySerial = 0;
    QHash<QString, Facts> m_facts;     // lower-case path
};

} // namespace brulion::shell
