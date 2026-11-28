// WorkspaceCalendars — calendars of other programs for the open notebook (QML:
// `Workspace.calendars`; docs/UI.md "Calendars"): ICS subscriptions shown read-only in the
// calendar view, CalDAV accounts whose calendars show their events (created, edited and deleted
// here too) and sync their tasks with task lines of the notes (docs/SYNTAX.md §5 "Tasks synced
// with CalDAV"). The core's caldav::CalendarSync does the work; a sync::SyncScheduler runs it —
// at once when the notebook opens, every account's interval (subscriptions when they are due),
// ~20 s after an edit of a synced task, when the network comes back and in the foreground — and
// the sync center's "Calendars" tab shows its log.
//
// brulion-cli and its MCP tools ask for changes of events through `<device state>/calendars/inbox`
// (caldav/eventchange.h): watched while the notebook has calendars, taken at once (shown, sent
// with a run that starts then); this object's CalendarSync is the one writer of the calendars'
// state. Kept on the device (never in the notebook): `<device state>/calendars/` (caldav/store.h); the
// passwords in owner-only files `<secrets>/caldav-<account id>` like the Nextcloud app password,
// masked in every log line while in memory. In the browser the requests are subject to CORS
// (docs/WEB.md "Calendars in the browser").
//
// QML values (plain data):
//   calendars        what the calendar view can show: [{ id, kind ("subscription" | "caldav"), name,
//                     color, visible, readOnly, events (shows events), tasks (syncs tasks), taskPage,
//                     account (its name), url, error (code, "" none), errorText (translated),
//                     statusText ("Updated today 10:32"), cors }]
//   subscriptions    the subscriptions: the same rows plus refreshMinutes and alarms (their
//                     events' reminders ring: off until asked for)
//   accounts         [{ id, name, server, user, conflicts ("newer" | "both"), intervalMinutes,
//                     hasPassword, calendars: [rows as above] }]
//                     (a CalDAV row also has `alarms`: the events' reminders ring)
//   writableCalendars the calendars an event can be created in: [{ id, name, color }]
//   log              the newest first: [{ when, whenText, kind, tone, name, text }]
//   state, statusText, running, needsConfirmation, pendingDeletions (titles)
//   discovering      discover() runs; discovered(ok, message, calendars) answers
#pragma once

#include "eventsource.h"
#include "types.h"

#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTimeZone>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

class QFileSystemWatcher;

namespace brulion {
struct ScheduledNotification;
class Vault;
class VaultIndex;
} // namespace brulion

namespace brulion::sync {
class HttpTransport;
class SyncScheduler;
} // namespace brulion::sync

namespace brulion::caldav {
class CalendarStore;
struct CalendarEntry;
struct Subscription;
class CalendarSync;
struct CalendarData;
struct CalendarSettings;
struct EventEdit;
struct EventIntent;
struct CalendarRunReport;
struct Discovery;
} // namespace brulion::caldav

namespace brulion::shell {

class WorkspaceCalendars : public planning::EventSource {
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY configurationChanged FINAL)
    Q_PROPERTY(bool hasCalendars READ hasCalendars NOTIFY configurationChanged FINAL)
    Q_PROPERTY(QVariantList calendars READ calendars NOTIFY configurationChanged FINAL)
    Q_PROPERTY(QVariantList subscriptions READ subscriptions NOTIFY configurationChanged FINAL)
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY configurationChanged FINAL)
    Q_PROPERTY(QVariantList writableCalendars READ writableCalendars NOTIFY configurationChanged FINAL)
    Q_PROPERTY(QVariantList log READ log NOTIFY logChanged FINAL)
    Q_PROPERTY(QString state READ state NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool running READ running NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool needsConfirmation READ needsConfirmation NOTIFY statusChanged FINAL)
    Q_PROPERTY(QStringList pendingDeletions READ pendingDeletions NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool discovering READ discovering NOTIFY discoveringChanged FINAL)
    Q_PROPERTY(bool browser READ browser NOTIFY configurationChanged FINAL)
    /// The colours offered (palette()).
    Q_PROPERTY(QStringList colors READ colors CONSTANT FINAL)
    /// Grows whenever the events shown change (bind views to it).
    Q_PROPERTY(int revision READ revision NOTIFY eventsChanged FINAL)

public:
    /// Colours offered to new calendars (the first one not taken).
    static QStringList palette();
    QStringList colors() const { return palette(); }

    explicit WorkspaceCalendars(QObject *parent = nullptr);
    ~WorkspaceCalendars() override;

    // ---- wiring -------------------------------------------------------------------------
    /// HTTP of CalDAV and of subscriptions (tests: fakes); default: the app's own. Not owned.
    void setTransports(sync::HttpTransport *caldav, sync::HttpTransport *feeds);
    /// Where the passwords live (`<app data>/secrets`).
    void setSecretsFolder(const QString &folder);
    void setClock(std::function<QDateTime()> clock);
    /// The device's zone (default the system's).
    void setZone(const QTimeZone &zone);
    void setLanguage(const QString &language);
    void setBrowser(bool browser);
    /// Automatic runs (default on). Off: only refresh() / syncNow() (tests).
    void setAutomatic(bool on);
    void setOnlineCheck(std::function<bool()> isOnline);
    /// The default folder of new calendars' task pages ("Kalendarze" in Polish).
    void setTaskFolder(const QString &folder);

    void attach(Vault *vault, VaultIndex *index);
    void detach();
    void setAppActive(bool active);
    /// Vault changes: an edit of a page with synced tasks syncs after the debounce.
    void notifyChanges(const QList<brulion::VaultChange> &changes);

    caldav::CalendarSync *calendarSync() const { return m_sync.get(); }
    caldav::CalendarStore *store() const { return m_store.get(); }
    sync::SyncScheduler *scheduler() const { return m_scheduler.get(); }

    // ---- EventSource --------------------------------------------------------------------
    QList<planning::ExternalEvent> eventsBetween(QDate from, QDate to) const override;
    /// The reminders of the events' VALARMs ringing within [from, to] (local wall-clock time,
    /// inclusive) of the CalDAV calendars that show events and have their alarms on, and of the
    /// subscriptions with their alarms on — what the
    /// reminder scheduler takes as its extra source (ReminderScheduler::setExtraSource()). The
    /// window the scheduler normally asks about is kept until the events change.
    QList<ScheduledNotification> eventAlarmsBetween(const QDateTime &from, const QDateTime &to) const;

    // ---- state --------------------------------------------------------------------------
    bool available() const { return m_store != nullptr; }
    bool hasCalendars() const;
    QVariantList calendars() const;
    QVariantList subscriptions() const;
    QVariantList accounts() const;
    QVariantList writableCalendars() const;
    QVariantList log() const;
    QString state() const;
    QString statusText() const;
    bool running() const;
    bool needsConfirmation() const;
    QStringList pendingDeletions() const;
    bool discovering() const { return m_discovering > 0; }
    bool browser() const { return m_browser; }
    int revision() const { return m_revision; }

    // ---- subscriptions --------------------------------------------------------------------
    /// Subscribe to `url` (http(s), webcal://): its id, "" when the address is no good
    /// (errorOccurred says why). Fetched at once.
    Q_INVOKABLE QString addSubscription(const QString &url, const QString &name, const QString &color);
    /// { name, color, refreshMinutes, url } — the keys given change.
    Q_INVOKABLE bool updateSubscription(const QString &id, const QVariantMap &values);

    // ---- CalDAV ----------------------------------------------------------------------------
    /// Ask a server for an account's calendars; discovered() answers with [{ url, name, color,
    /// events, tasks, readOnly, taskPage (suggested) }].
    Q_INVOKABLE void discover(const QString &server, const QString &user, const QString &password);
    /// The account found by the last discover(): its calendars of `eventCalendars` (urls) show
    /// their events, those of `taskCalendars` sync their tasks (to the page discover() suggested);
    /// its id, "" on failure (errorOccurred).
    Q_INVOKABLE QString addAccount(const QString &server, const QString &user, const QString &password,
                                   const QStringList &eventCalendars, const QStringList &taskCalendars);
    /// { conflicts: "newer" | "both", intervalMinutes, password }.
    Q_INVOKABLE bool updateAccount(const QString &id, const QVariantMap &values);
    /// The account, its calendars, their states and its password go (the notes stay: synced task
    /// lines are left as they are).
    Q_INVOKABLE bool removeAccount(const QString &id);
    /// { name, color, showEvents, alarms, syncTasks, taskPage, visible }.
    Q_INVOKABLE bool updateCalendar(const QString &id, const QVariantMap &values);
    /// What the account dialog ticks at first among discovered() calendars: the urls of those with
    /// events (`tasks` false), or of those holding tasks only (`tasks` true).
    Q_INVOKABLE QStringList defaultChoice(const QVariantList &found, bool tasks) const;
    /// The page a calendar's tasks land on ("Kalendarze/Praca.md").
    Q_INVOKABLE QString defaultTaskPage(const QString &calendarName) const;

    // ---- both ------------------------------------------------------------------------------
    /// Remove a subscription or a CalDAV calendar (its cache and state; not the notes).
    Q_INVOKABLE bool removeCalendar(const QString &id);
    Q_INVOKABLE void setCalendarVisible(const QString &id, bool visible);
    /// Refresh one subscription or calendar now ("" = all of them).
    Q_INVOKABLE void refresh(const QString &id);
    Q_INVOKABLE void syncNow();
    /// The run held deletions back (the guard): apply them.
    Q_INVOKABLE void confirmDeletions();
    Q_INVOKABLE void clearLog();

    // ---- events ------------------------------------------------------------------------------
    /// One occurrence (an entry's eventKey): { key, calendarId, calendarName, color, title,
    /// location, description, url, startDate, startTime, endDate, endTime ("" all-day), allDay,
    /// recurring, rule (Brulion's form, "" none or not representable), ruleText (as written),
    /// readOnly, canEdit, whenText ("Fri, Nov 20, 10:00–11:00"), reminderMinutes (-1 none) };
    /// {} when unknown.
    Q_INVOKABLE QVariantMap eventDetails(const QString &key) const;
    /// Create (`key` "") or change an event: { key, calendarId, title, location, description,
    /// startDate, startTime, endDate, endTime, allDay, rule, reminderMinutes, scope: "all" |
    /// "this" (one occurrence of a series) }. Shown at once, sent with a run that starts now.
    Q_INVOKABLE bool saveEvent(const QVariantMap &values);
    /// Delete an event, or only this occurrence of its series.
    Q_INVOKABLE bool deleteEvent(const QString &key, bool thisOccurrenceOnly);
    /// Answer an invitation of the account: "accepted", "declined" or "tentative" (the whole
    /// event or series; the server sends the organizer the reply). False when the event invites
    /// no address of the account (eventDetails' canAnswer) or the answer is none of those.
    Q_INVOKABLE bool answerEvent(const QString &key, const QString &answer);
    /// The journal's line about an event ("- 10:00–11:00 Zebranie (Praca)").
    Q_INVOKABLE QString journalLine(const QString &key) const;

Q_SIGNALS:
    void configurationChanged();
    void logChanged();
    void statusChanged();
    void discoveringChanged();
    void discovered(bool ok, const QString &message, const QVariantList &calendars);
    void errorOccurred(const QString &message);
    /// A run met tasks changed on both sides (titles).
    void conflictsFound(const QStringList &titles);
    /// A run held deletions back.
    void deletionsHeld(int count);

private:
    struct Shown;
    /// The change made (its problem said in words when `sayWhy`), sent with a run that starts now.
    bool queueIntent(caldav::EventIntent intent, bool sayWhy);
    /// The inbox of brulion-cli's changes watched while the notebook has calendars.
    void watchInbox();
    void refreshTaskPages(const caldav::CalendarSettings &settings);
    /// "Fri, Nov 20, 14:00–15:30" in the app's language.
    QString whenText(const caldav::EventEdit &fields) const;
    void takeInbox();
    void rebuild();
    void teardown();
    void configure();
    void onRunFinished();
    void invalidate(const QString &id);
    void scheduleEventsChanged();
    /// eventAlarmsBetween() computed for [from, to], without its cache.
    QList<ScheduledNotification> alarmsWithin(const QDateTime &from, const QDateTime &to) const;
    std::shared_ptr<const caldav::CalendarData> dataOf(const QString &id) const;
    QString passwordFile(const QString &accountId) const;
    QString password(const QString &accountId) const;
    QVariantMap row(const QString &id) const;
    QVariantMap subscriptionRow(const caldav::Subscription &sub) const;
    QVariantMap calendarRow(const caldav::CalendarSettings &settings, const caldav::CalendarEntry &c) const;
    QString errorText(const QString &code) const;
    QString human(const QDateTime &time) const;
    QString nextColor() const;
    QDateTime now() const;
    void refreshTracked();

    QPointer<Vault> m_vault;
    QPointer<VaultIndex> m_index;
    std::unique_ptr<caldav::CalendarStore> m_store;
    std::unique_ptr<caldav::CalendarSync> m_sync;
    std::unique_ptr<sync::SyncScheduler> m_scheduler;
    sync::HttpTransport *m_caldavTransport = nullptr;
    sync::HttpTransport *m_feedTransport = nullptr;
    std::unique_ptr<sync::HttpTransport> m_ownCaldav;
    std::unique_ptr<sync::HttpTransport> m_ownFeeds;
    QString m_secrets;
    QString m_language = QStringLiteral("en");
    QString m_taskFolder;
    std::function<QDateTime()> m_clock;
    std::function<bool()> m_onlineCheck;
    QTimeZone m_zone;
    bool m_browser = false;
    bool m_automatic = true;
    bool m_appActive = true;
    int m_discovering = 0;
    int m_revision = 0;
    QTimer m_eventsTimer;
    QTimer m_startTimer;
    QTimer m_inboxTimer; ///< a burst of files in the inbox taken once
    std::unique_ptr<QFileSystemWatcher> m_inboxWatcher;
    mutable QHash<QString, std::shared_ptr<const caldav::CalendarData>> m_data;
    /// The last range asked for and its entries (the calendar asks for the same range again and
    /// again: its days, its day panel); dropped with every change of the events.
    mutable QDate m_cacheFrom;
    mutable QDate m_cacheTo;
    mutable QList<planning::ExternalEvent> m_cache;
    mutable QDate m_alarmCacheDay; ///< the day m_alarmCache was computed for (invalid: none)
    mutable QList<ScheduledNotification> m_alarmCache;
    QSet<QString> m_trackedBlocks; ///< block ids of synced tasks (an edit of their notes syncs)
    QSet<QString> m_taskPages;
    QVariantMap m_lastDiscovery; ///< { server, user, principal, home }
    QVariantList m_lastFound;    ///< its calendars, as discovered() gave them
    QDateTime m_lastRun;
    QString m_lastError;
    bool m_needsConfirmation = false;
    QStringList m_pending;
};

} // namespace brulion::shell
