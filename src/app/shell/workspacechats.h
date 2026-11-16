#pragma once
// WorkspaceChats — the conversations with the AI assistant of the open notebook (docs/UI.md "AI
// chat"; QML: Workspace.chats). A conversation is a page in the folder AI/ (core chat/chat.h:
// `- 🧑 …` and `- 🤖 …` messages). Sending a message writes it into its page — a new conversation
// first gets its page, named after the question, and a line in today's journal that links to it —
// and then runs the assistant's turn (WorkspaceExtensions::runChat: the built-in asystent's command
// "chat" with the conversation's messages and the page it is about); the answer is written into the
// page as the AI's message. One turn at a time, whatever is on screen: the chat view may close, the
// answer still lands in its page (which follows a rename; a page deleted meanwhile ends the turn).
//
//   Workspace.chats.send("", "Co mam jutro do zrobienia?", "Dom")   // a new one: its path
//   Workspace.chats.send(path, "A pojutrze?")                         // the next message
//   Workspace.chats.running / status / stop()                         // the turn on its way
//   Workspace.chats.partial / thoughts                                // its answer as it is written
//   Workspace.chats.failedPath / failure / retry(path)                // a turn that failed
//
// Single-threaded (the GUI thread).

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

#include <utility>

namespace brulion::shell {

class Workspace;
class WorkspaceExtensions;

class WorkspaceChats : public QObject {
    Q_OBJECT
    /// The conversation (its path) whose turn runs, "" for none.
    Q_PROPERTY(QString running READ running NOTIFY stateChanged FINAL)
    /// What the running turn does now (b.status: "Czytam „Dom”…"), "" while the model thinks.
    Q_PROPERTY(QString status READ status NOTIFY stateChanged FINAL)
    /// The answer of the running turn as the model writes it ("" none yet: it thinks, or calls a
    /// tool); each of the turn's requests to the model starts anew. Markdown, the model's words.
    Q_PROPERTY(QString partial READ partial NOTIFY partialChanged FINAL)
    /// The end of what the model thinks meanwhile (a model that shows it: the one on the device),
    /// a few lines: "" none.
    Q_PROPERTY(QString thoughts READ thoughts NOTIFY partialChanged FINAL)
    /// How fast the model writes now — over its last two seconds —, as a text ("16,7 tok/s"); ""
    /// when not known (a provider on the network does not say while it writes).
    Q_PROPERTY(QString speed READ speed NOTIFY partialChanged FINAL)
    /// The conversation whose last turn failed and why ("" for none): shown in it, with Try again.
    Q_PROPERTY(QString failedPath READ failedPath NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString failure READ failure NOTIFY stateChanged FINAL)

public:
    WorkspaceChats(Workspace *workspace, WorkspaceExtensions *extensions, QObject *parent = nullptr);

    QString running() const { return m_running; }
    QString status() const { return m_status; }
    QString failedPath() const { return m_failedPath; }
    QString failure() const { return m_failure; }
    QString partial() const { return m_partial; }
    QString thoughts() const { return m_thoughts; }
    QString speed() const { return m_speed; }

    /// Tokens a second over the last `windowMs` of `samples` ({writing ms, tokens} in order; a
    /// window shorter than half of it: over all of them); 0 when they do not tell.
    static double tokensPerSecond(const QList<std::pair<qint64, int>> &samples, qint64 windowMs = 2000);

    /// The end of `thoughts` the chat shows: at most about `chars` characters, from a word on.
    static QString thoughtsTail(const QString &thoughts, int chars = 240);

    /// Writes `text` into the conversation `path` and runs the AI's turn; "" `path`: a new
    /// conversation about the page `page` (its name as a link names it; "" for none). Returns the
    /// conversation's path, "" when nothing was sent: no text, a turn running, the assistant off
    /// (said in `failure`), no notebook, a page that could not be written.
    Q_INVOKABLE QString send(const QString &path, const QString &text, const QString &page = QString());
    /// The AI's turn again on the conversation as it is (its last message the user's): after a
    /// failure or a stop. False when it cannot run now.
    Q_INVOKABLE bool retry(const QString &path);
    /// Stops the running turn: nothing is written for it (the user's message stays).
    Q_INVOKABLE void stop();
    /// Forgets the failure shown.
    Q_INVOKABLE void dismissFailure();
    /// The page `path` is a conversation: in the folder AI/ with a message.
    Q_INVOKABLE bool isConversation(const QString &path) const;
    /// The folder of the conversations ("AI").
    Q_INVOKABLE QString folder() const;
    /// A chat view shows the conversation `path` (shown true) or no longer does (false): a question
    /// of its turn is then answered there, not in the dialog (ShellExtensionQuestion). Counted: a
    /// tab and the side panel may both show it.
    Q_INVOKABLE void showing(const QString &path, bool shown);
    /// A chat view shows the conversation `path`.
    Q_INVOKABLE bool isShown(const QString &path) const;

    /// The notebook closed: no turn runs.
    void reset();

Q_SIGNALS:
    void stateChanged();
    /// A message was written into the conversation `path` (the view follows it to the end).
    void written(const QString &path);
    /// showing() changed what is shown.
    void shownChanged();
    /// partial or thoughts changed (at most ten times a second).
    void partialChanged();

private:
    bool runTurn(const QString &path);
    void answered(const QString &turn, const QString &text);
    void failed(const QString &turn, const QString &error);
    void setFailure(const QString &path, const QString &failure);
    void endTurn();
    void setPartial(const QString &text, const QString &thoughts, const QString &speed);

    Workspace *m_workspace = nullptr;
    WorkspaceExtensions *m_extensions = nullptr;
    QString m_turn;    // the path the running turn was started with (its answer comes with it)
    QString m_running; // where its page is now (a rename moves it)
    QString m_status;
    QString m_failedPath;
    QString m_failure;
    QHash<QString, int> m_shown; // by path, lower case: the chat views showing it
    QString m_partial;
    QString m_thoughts;
    QString m_speed;
    QString m_nextPartial; // what comes, said by m_partialTimer
    QString m_nextThoughts;
    QList<std::pair<qint64, int>> m_samples; // the request's {writing ms, tokens}, for its speed
    QTimer m_partialTimer;
};

} // namespace brulion::shell
