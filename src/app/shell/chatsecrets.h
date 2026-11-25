#pragma once
// ChatSecrets — a secret an AI agent asks the user for in a private card of its conversation
// (docs/UI.md "AI chat" → "A secret the agent asks for"; T3 Code's request_secret; QML:
// Workspace.chats.secrets, ChatSecretCard.qml): a token, a password, a PIN, an API key only the user
// can give — never asked in the chat, where it would land in the conversation's page (synced, kept
// in its history) and go to the model.
//
// The agent's tool (`brulion_app_chat_request_secret`, `brulion-cli app chat request-secret`) calls
// the remote control's chatRequestSecret (ChatControl), which asks here for its own conversation and
// waits: the card shows who asks, the label and the reason (plain text), an optional sign-in or
// setup address (http/https only, opened through Workspace.openUrl) and a masked field with Show,
// Paste, Send and Cancel. Sent, the value goes to the agent as the path of an owner-only file
// (secretdrop::Drop: 0600 in a 0700 folder of this run, under the app's runtime folder) that is
// deleted when the conversation's turn ends, or after 15 minutes — or, when the agent asked for
// `delivery: value`, as the value itself (the card says it then goes into the agent's context and
// its provider's logs). Either way the value is registered with the event log's redaction while it
// lives, and never written into the page, the event log, a notification, a toast or the notebook's
// device state (LocalStore). Cancel, Stop of the turn, the turn's end, the conversation deleted, the
// agent's call gone or its time up end the card with a refusal the agent reads.
//
// One card per conversation at a time (a second request meanwhile: busy). While a card waits, the
// conversation "asks" (WorkspaceChats::stateOf: the list's row, the agent that delegated to it — a
// sub-conversation's secret makes its parent's wait say waiting_for_user —, the notification and
// the toast when no chat view shows it). GUI-free; single-threaded (the GUI thread).

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace brulion::secretdrop {
class Drop;
}

namespace brulion::shell {

class Workspace;
class WorkspaceChats;

class ChatSecrets : public QObject {
    Q_OBJECT
    /// Changes whenever a card comes or goes (QML asks pending() / card() again).
    Q_PROPERTY(int serial READ serial NOTIFY changed FINAL)

public:
    ChatSecrets(Workspace *workspace, WorkspaceChats *chats, QObject *parent = nullptr);
    ~ChatSecrets() override;

    /// How long a request waits for the user when it names no time, and the longest.
    static constexpr int DefaultWaitMs = 5 * 60 * 1000;
    static constexpr int MaxWaitMs = 15 * 60 * 1000;
    /// The longest label, reason, placeholder and address kept (longer ones are cut).
    static constexpr int MaxLabelChars = 120;
    static constexpr int MaxReasonChars = 1000;
    static constexpr int MaxPlaceholderChars = 120;
    static constexpr int MaxUrlChars = 2000;

    /// What an agent asks for.
    struct Request {
        QString path;  ///< its conversation
        QString agent; ///< who asks (the grant's client name: the agent's)
        QString label;
        QString reason{};
        QString placeholder{};
        QString url{};            ///< a sign-in or setup page ("" none): http or https
        bool inlineValue = false; ///< delivery "value": the value itself, not a file
        int waitMs = DefaultWaitMs;
    };
    /// The answer to the agent: ok(result) or fail(code, message, details) once; `connected`
    /// false when its call is gone (the card goes).
    struct Reply {
        std::function<void(const QJsonObject &)> ok;
        std::function<void(const QString &code, const QString &message, const QJsonObject &details)> fail;
        std::function<bool()> connected{};
    };
    /// Shows the card of `request` and answers `reply` when the user sends or cancels, or the card
    /// ends otherwise (above). Refused at once — bad_params (no label, an address that is no http or
    /// https), busy (another card waits in that conversation), not_found (no such conversation).
    void ask(const Request &request, const Reply &reply);

    int serial() const { return m_serial; }
    /// A card waits in `path` (`serial`: for QML bindings).
    Q_INVOKABLE bool pending(const QString &path, int serial = 0) const;
    /// The card of `path`: {agent, label, reason, placeholder, url, host, inline}; {} none.
    Q_INVOKABLE QVariantMap card(const QString &path, int serial = 0) const;
    /// The card's question in one line ("Claude Code asks for a secret: GitHub token"), "" none.
    QString questionText(const QString &path) const;
    /// The user sends `value` (not empty): the agent gets it (a file, or the value); false when no
    /// card waits there or it could not be handed over (a toast says why).
    Q_INVOKABLE bool send(const QString &path, const QString &value);
    /// The user cancels the card.
    Q_INVOKABLE void cancel(const QString &path);

    /// The turn of `path` ended (answered, failed, stopped): its card goes ("stopped" when the
    /// user stopped it) and the files it handed over are deleted.
    void turnEnded(const QString &path, bool stopped = false);
    /// The notebook closed: every card goes, every file is deleted.
    void reset();

    /// The drop of the files (tests).
    secretdrop::Drop *drop() const { return m_drop.get(); }
    /// Where the files go: a folder of the user's runtime folder (`$XDG_RUNTIME_DIR`, never on a
    /// disk) when there is a private one, else the app's cache folder.
    static QString defaultBase();
    /// Tests: another folder for the files (the next one written).
    void setBase(const QString &base);

Q_SIGNALS:
    void changed();
    /// A card came in `path` (WorkspaceChats: the conversation asks; a toast when none shows it).
    void asked(const QString &path, const QString &text);
    /// Words for a toast ("error"): a value not handed over.
    void message(const QString &text, const QString &kind);

private:
    struct Pending;
    void finish(const QString &key, const QString &code, const QString &message, const QJsonObject &details = {});
    void renamed(const QString &from, const QString &to);
    void dropGoneClients();
    void watchDrop();

    Workspace *m_workspace = nullptr;
    WorkspaceChats *m_chats = nullptr;
    std::unique_ptr<secretdrop::Drop> m_drop;
    QHash<QString, std::shared_ptr<Pending>> m_pending; // by conversation, lower case
    /// The conversations whose turn ran when their value was handed over (its end deletes it).
    QHash<QString, QString> m_handed; // lower case → the drop's owner key
    QTimer m_clientCheck;
    int m_serial = 0;
};

} // namespace brulion::shell
