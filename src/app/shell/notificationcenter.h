#pragma once
// NotificationCenter — what the app told the user (docs/UI.md "Notifications"; QML:
// Workspace.notifications): every notification of the open notebook — a reminder that rang, the AI
// chat's answers, failures and questions, an AI agent's, an extension's or a script's own, an
// extension's timer — kept on this device for the notebook (LocalStore "notify/log", never in the
// notebook: it syncs nothing), newest first, at most MaxEntries and none older than MaxAgeDays.
// One that came while the app was in front counts as seen; the others are unread until the user
// opens them, opens what they point to, or marks them all read.
//
// It is also the one way in for notifications the app does not make itself (post()): an AI agent's
// (ChatControl's chatNotify through WorkspaceChats::notify), an extension's (b.notify) and a
// script's — checked against the user's settings (setAllowed()) and MaxPerHour of one source, titled
// with who sends them (a note an agent read must not make it pass for another app), cleaned and
// cut, then shown (shown(): a system notification, or a toast while the app is in front —
// workspaceintegration.cpp) and kept here.
//
//   Workspace.notifications.unread / revision / entries(revision, unreadOnly)
//   Workspace.notifications.markRead(id) / markAllRead() / remove(id) / clear()
//
// Single-threaded (the GUI thread); the clock and whether the app is active are the workspace's.

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <functional>

namespace brulion {
class LocalStore;
}

namespace brulion::shell {

class NotificationCenter : public QObject {
    Q_OBJECT
    /// How many entries the user has not seen.
    Q_PROPERTY(int unread READ unread NOTIFY changed FINAL)
    /// Changes with every entry added, read or removed (entries() asks again).
    Q_PROPERTY(int revision READ revision NOTIFY changed FINAL)
    /// How many entries there are.
    Q_PROPERTY(int count READ count NOTIFY changed FINAL)

public:
    explicit NotificationCenter(std::function<QDateTime()> clock, QObject *parent = nullptr);

    /// What sent it: a reminder (or a calendar event's alarm), the AI chat (a turn answered, failed,
    /// asking), an AI agent, an extension, a script of the user's, an extension's timer.
    enum class Kind { Reminder, Chat, Agent, Extension, Script, Timer };
    static QString kindName(Kind kind);

    /// How many entries are kept at most, and for how long.
    static constexpr int MaxEntries = 200;
    static constexpr int MaxAgeDays = 30;
    /// How many notifications one source (a conversation, an extension, the scripts) may post an hour.
    static constexpr int MaxPerHour = 10;
    /// The longest title and text of a posted one (longer ones cut, on a character).
    static constexpr int MaxTitle = 80;
    static constexpr int MaxText = 1000;

    /// The notebook's device state the log is kept in (nullptr: none open — nothing kept).
    void attach(LocalStore *store);
    void detach();
    /// Whether the app is active now (default: QGuiApplication's state).
    void setAppActive(std::function<bool()> active) { m_appActive = std::move(active); }
    bool appActive() const;

    /// Whether posts of `kind` (Agent, Extension, Script) may show (the user's settings: agents and
    /// extensions on by default, scripts always).
    void setAllowed(Kind kind, bool allowed);
    bool allowed(Kind kind) const;

    /// A notification the app showed by itself (a reminder, the chat's): kept. `open` is what a
    /// click opens (a page's or a conversation's path, an event's "event:<date>"; "" none). Its id.
    int record(Kind kind, const QString &source, const QString &title, const QString &text, const QString &open);

    /// A notification someone else sends the user.
    struct Post {
        Kind kind = Kind::Agent;
        /// Whose hourly count it takes (a conversation's path, an extension's id; "" the scripts').
        QString key;
        /// Who sends it, said in its title ("Claude Code", an extension's name, a script's).
        QString who;
        QString title;
        QString text;
        /// What a click opens ("" the app).
        QString open;
    };
    /// Shows `post` (shown()) and keeps it: "" when shown, else why not — "off" (the user's
    /// setting), "limit" (MaxPerHour in the last hour; `*retryMs` until the next may go), "empty"
    /// (no text once cleaned).
    QString post(const Post &post, qint64 *retryMs = nullptr);

    int unread() const { return m_unread; }
    int revision() const { return m_revision; }
    int count() const { return int(m_entries.size()); }
    /// The entries, newest first: {entryId, kind, source, title, text, open, read, at ("yyyy-MM-ddTHH:mm"),
    /// time ("HH:mm"), day ("yyyy-MM-dd"), section ("today", "yesterday", else its day)} — only the
    /// unread with `unreadOnly`. `revision` only
    /// makes a QML binding ask again.
    Q_INVOKABLE QVariantList entries(int revision = 0, bool unreadOnly = false) const;
    Q_INVOKABLE void markRead(int id);
    Q_INVOKABLE void markAllRead();
    /// Marks read every entry that opens `open` (it was opened another way: a system
    /// notification's click, the conversation shown).
    Q_INVOKABLE void markOpened(const QString &open);
    Q_INVOKABLE void remove(int id);
    Q_INVOKABLE void clear();

    /// `text` without control characters (but line ends and tabs), more than one empty line or
    /// spaces at its ends, at most `max` characters (never half a character; "…" when cut).
    static QString clean(const QString &text, int max);

Q_SIGNALS:
    void changed();
    /// A posted notification to show: `title` (with who sends it) and `text`; a click opens `open`
    /// ("" the app). `inApp`: the app is in front — a toast there instead. `kind` its kindName().
    void shown(const QString &kind, const QString &open, const QString &title, const QString &text, bool inApp);

private:
    struct Entry {
        int id = 0;
        Kind kind = Kind::Reminder;
        QString source;
        QString title;
        QString text;
        QString open;
        QDateTime at;
        bool read = false;
    };
    int add(Entry entry);
    void prune();
    void load();
    void save();
    void recount();

    std::function<QDateTime()> m_clock;
    std::function<bool()> m_appActive;
    LocalStore *m_store = nullptr;
    QList<Entry> m_entries; // newest first
    int m_nextId = 1;
    int m_unread = 0;
    int m_revision = 0;
    bool m_agents = true;
    bool m_extensions = true;
    QHash<QString, QList<qint64>> m_posted; // "<kind>:<key in lower case>" → the last hour's (ms)
};

} // namespace brulion::shell
