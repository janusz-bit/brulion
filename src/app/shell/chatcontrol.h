#pragma once
// ChatControl — the AI chat for AI agents and scripts (docs/CLI.md "Conversations"): the remote
// control's chat methods (ipc/controlprotocol.h: chats, chatRead, chatStart, chatSend, chatWait,
// chatStop, chatPartners, chatSchedules, chatScheduleAdd / Set / Remove / Run, chatRequestSecret)
// answered over WorkspaceChats — `brulion-cli app chat …` and the MCP tools brulion_app_chat_* are
// its clients.
//
// Who asks decides what may be done (Caller): a client of the open remote control (the user's
// own script) any of it; an AI agent the app started reaches it with its conversation's chat
// grant (WorkspaceAgents) — what it starts is a sub-conversation of that conversation (its page
// says `parent:`), it sends to and stops only its own sub-conversations (any depth), within the
// limits of delegation (ChatLineage: MaxDepth levels, MaxRunningChildren at once), and changes
// only the schedules it set up from there, and asks the user for a secret (chatRequestSecret:
// ChatSecrets' private card) only in its own conversation while its turn runs. Reading (chats,
// chatRead, chatWait, chatPartners, chatSchedules) is open to all: the agent's notebook tools read
// every page anyway. No schedule's webhook secret is ever given or taken here.
//
// GUI-free (the desktop's RemoteControl passes requests on; tests call it directly). Waits are
// answered when the turn's state changes or their time is up, and dropped when their client is
// gone. Single-threaded (the GUI thread).

#include <QJsonObject>
#include <QList>
#include <QObject>
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

    /// `method` is one of the chat's.
    static bool handles(const QString &method);
    void handle(const QString &method, const QJsonObject &params, const Caller &caller, const Reply &reply);

    /// The conversation `ref` names: a vault path (".md" and "AI/" optional, any letter case) or
    /// a conversation's name without its date when one alone has it; "" none (`*ambiguous`: several).
    QString resolve(const QString &ref, QStringList *ambiguous = nullptr) const;

private:
    struct Wait;
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

    /// A conversation's row: {path, name, partner, partnerName, state, failure?, question?,
    /// parent?, messages, changed}.
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
};

} // namespace brulion::shell
