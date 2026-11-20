// One run over a notebook's calendars (docs/UI.md "Calendars"): the subscriptions that are due
// (all of them when asked by hand) fetched with their validators, then every CalDAV calendar —
// the events changed here sent (If-Match / If-None-Match: a change on the server meanwhile is a
// conflict, decided by the account's policy), the server's changes fetched (sync-collection, else
// the ETags compared), the tasks reconciled with the task lines of the notes (tasksync_p.h) — and
// everything logged (CalendarStore::log). A sync::SyncRunner: the app's sync::SyncScheduler runs
// it periodically, after edits of synced tasks, when the network comes back and in the
// foreground; Android's background check runs it once. Part of the caldav module — see
// caldav/caldav.h.
#pragma once

#include "caldav/client.h"
#include "caldav/events.h"
#include "caldav/feed.h"
#include "caldav/store.h"
#include "sync/synctypes.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimeZone>

#include <functional>
#include <memory>

namespace brulion {
class Vault;
class VaultIndex;
} // namespace brulion

namespace brulion::caldav {

/// What one run did (also one calendar's part of it).
struct CalendarRunReport {
    int refreshed = 0;     ///< subscriptions fetched (or found unchanged)
    int downloaded = 0;    ///< task lines written from the server, events received
    int uploaded = 0;      ///< tasks and events written to the server
    int deletedLocal = 0;  ///< task lines removed (deleted on the server)
    int deletedRemote = 0; ///< tasks and events deleted on the server
    int merged = 0;        ///< tasks changed on both sides in different fields
    QStringList conflicts; ///< titles of tasks and events changed on both sides in the same field
    QStringList changedNotes;
    QStringList changedCalendars; ///< ids whose events (as shown) changed
    QStringList failed;           ///< ids of subscriptions and calendars that failed
    sync::WebDavError error;      ///< the first failure (auth, network …)
    bool needsConfirmation = false;
    QStringList pendingDeletions; ///< titles of tasks whose deletion waits for a confirmation
    bool cancelled = false;
    QDateTime started;
    QDateTime finished;

    /// As the scheduler and the sync history count it.
    sync::SyncReport toSyncReport() const;
};

class CalendarSync : public QObject, public sync::SyncRunner {
    Q_OBJECT
public:
    explicit CalendarSync(CalendarStore *store, QObject *parent = nullptr);
    ~CalendarSync() override;

    /// The notebook whose task lines are synced (none: only events and subscriptions).
    void setVault(Vault *vault, VaultIndex *index);
    /// HTTP of CalDAV (it carries the password: the sync module's transport, same server only)
    /// and of subscriptions (redirects to other servers followed: FeedHttpTransport). Not owned.
    void setTransports(sync::HttpTransport *caldav, sync::HttpTransport *feeds);
    /// The password of an account ("" none: its calendars are not synced, error "auth").
    void setPasswords(std::function<QString(const QString &accountId)> passwords);
    /// The device's zone (wall-clock times of the notes; default the system's).
    void setZone(const QTimeZone &zone) { m_zone = zone; }
    void setClock(std::function<QDateTime()> clock) { m_clock = std::move(clock); }
    void setBrowser(bool browser) { m_browser = browser; }

    CalendarStore *store() const { return m_store; }
    bool isRunning() const { return m_running; }
    CalendarRunReport lastReport() const { return m_last; }

    // sync::SyncRunner: every due subscription (all: SyncTrigger::Manual) and every calendar.
    void startSync(const sync::SyncRunOptions &options, sync::SyncDoneCallback done) override;
    void cancelSync() override;

    /// One run like startSync(), with the report of the calendars.
    void run(bool manual, bool confirmDeletions, std::function<void(const CalendarRunReport &)> done);
    /// Just one subscription or calendar now (the "Refresh" of its row).
    void runOne(const QString &id, std::function<void(const CalendarRunReport &)> done);

    /// An event created, changed or deleted in Brulion: shown at once (the calendar's cache), sent
    /// with the next run. `change.path` "" for a new event: one is made from its UID.
    bool queueEvent(const QString &calendarId, EventChange change);

    /// Find an account's calendars (the setup dialog): discovery with these credentials.
    void discover(const QString &server, const QString &user, const QString &password,
                  std::function<void(const Discovery &)> done);

    QDateTime now() const;

Q_SIGNALS:
    /// The events shown of a subscription or calendar changed (its cache, its outbox).
    void calendarChanged(const QString &id);
    void logChanged();
    void runningChanged();
    /// An event was queued: a run should follow soon.
    void localChange();

private:
    struct Run;
    /// A new run: the device's lock, the store read again; false when another process holds it
    /// (the run ends at once, cancelled).
    bool begin();
    void next();
    void finish();
    void refresh(const Subscription &subscription);
    void syncCalendar(const CalendarEntry &entry);
    CalDavClient *client(const Account &account, QString *problem);
    void log(LogEntry entry);

    CalendarStore *m_store = nullptr;
    QPointer<Vault> m_vault;
    QPointer<VaultIndex> m_index;
    sync::HttpTransport *m_caldav = nullptr;
    sync::HttpTransport *m_feeds = nullptr;
    std::function<QString(const QString &)> m_passwords;
    QTimeZone m_zone;
    std::function<QDateTime()> m_clock;
    bool m_browser = false;
    bool m_running = false;
    bool m_cancel = false;
    std::unique_ptr<Run> m_run;
    QList<std::function<void(const CalendarRunReport &)>> m_waiters;
    CalendarRunReport m_last;
    struct ClientEntry {
        QString key; ///< server | user | password hash
        std::shared_ptr<CalDavClient> client;
    };
    QHash<QString, ClientEntry> m_clients;
};

} // namespace brulion::caldav
