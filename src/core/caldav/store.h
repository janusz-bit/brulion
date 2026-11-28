// What a notebook's calendars keep on the device (never in the notebook: a subscription's address
// may hold a secret token, the caches are the server's): `<device state>/calendars/` —
// calendars.json (accounts, their calendars, subscriptions and how each is shown), one
// `<id>.ics` + `<id>.meta.json` per subscription (the feed as last fetched, its validators), one
// `<id>.state.json` per CalDAV calendar (sync token, ctag, every member with its ETag and data, the
// synced tasks with both sides' last fields, the events changed here and not sent yet) and log.json
// (what the runs did: the sync center's "Calendars"). Written atomically (QSaveFile). Part of the
// caldav module — see caldav/caldav.h.
#pragma once

#include "caldav/taskmap.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

namespace brulion::caldav {

/// An ICS subscription.
struct Subscription {
    QString id;
    QString url;   ///< feedUrl() of what was typed
    QString name;  ///< chosen, else the feed's X-WR-CALNAME, else the host
    QString color; ///< "#rrggbb"
    bool visible = true;
    int refreshMinutes = 360; ///< how often it is fetched again (15 … 10 080; default 6 h)
    /// The VALARMs of its events ring (eventalarms.h) — off until asked for: someone else's
    /// calendar (a school's, a team's) need not ring on this device.
    bool alarms = false;
    friend bool operator==(const Subscription &, const Subscription &) = default;
};

/// What happens when a task changed in the notes and on the server since the last sync, in the
/// same field: the newer version wins, or both stay (the server's as another task line).
enum class ConflictPolicy { Newer, KeepBoth };

/// A CalDAV account. Its password lives in the app's secrets folder, never here.
struct Account {
    QString id;
    QString server;       ///< the address as given ("https://chmura.example.pl")
    QString user;
    QString principalUrl; ///< found by discovery
    QString homeUrl;
    QString name;         ///< shown ("jan @ chmura.example.pl")
    ConflictPolicy conflicts = ConflictPolicy::Newer;
    int intervalMinutes = 15; ///< automatic sync (5 … 1440)
    friend bool operator==(const Account &, const Account &) = default;
};

/// A calendar of an account and what Brulion does with it.
struct CalendarEntry {
    QString id;
    QString account;
    QString url;
    QString name;
    QString color;
    QStringList components; ///< "VEVENT", "VTODO" (empty: both)
    bool readOnly = false;
    bool showEvents = true; ///< its events in the calendar view
    bool alarms = true;     ///< the VALARMs of its events ring (eventalarms.h)
    bool syncTasks = false; ///< its tasks ↔ task lines of `taskPage`
    QString taskPage;       ///< the page new tasks of the server go to ("Kalendarze/Praca.md")
    bool visible = true;    ///< shown in the calendar view (the toggle of its colour dot)
    friend bool operator==(const CalendarEntry &, const CalendarEntry &) = default;
};

struct CalendarSettings {
    QList<Account> accounts;
    QList<CalendarEntry> calendars;
    QList<Subscription> subscriptions;

    const Account *account(const QString &id) const;
    const CalendarEntry *calendar(const QString &id) const;
    const Subscription *subscription(const QString &id) const;
    QJsonObject toJson() const;
    static CalendarSettings fromJson(const QJsonObject &json);
    friend bool operator==(const CalendarSettings &, const CalendarSettings &) = default;
};

/// A subscription's cached copy.
struct FeedMeta {
    QString etag;
    QString lastModified;
    QDateTime fetchedAt;   ///< the last successful fetch (also a 304)
    QDateTime attemptedAt; ///< the last attempt
    QString error;         ///< errorCodeName() of the last failure ("" none)
    QString errorText;     ///< its technical message
    bool cors = false;     ///< the failure looks like the browser's CORS
    qint64 bytes = 0;
    int events = 0;
    QJsonObject toJson() const;
    static FeedMeta fromJson(const QJsonObject &json);
};

/// One member of a CalDAV calendar as last seen.
struct Resource {
    QString etag;
    QString uid;
    QString kind; ///< "VTODO", "VEVENT", "" (anything else)
    QByteArray data;
};

/// A task synced between a line of the notes and a VTODO.
struct TrackedTask {
    QString path;     ///< the resource on the server
    QString blockId;  ///< the line's block id
    QString note;     ///< the note it was last seen in
    TaskFields local; ///< the line's fields after the last sync
    TaskFields remote; ///< the VTODO's fields after the last sync
};

/// An event created, changed or deleted in Brulion, waiting to reach the server.
struct EventChange {
    enum class Kind { Create, Update, Delete };
    Kind kind = Kind::Update;
    QString path;
    QString uid;
    QByteArray data;   ///< the new resource (Create, Update)
    QString ifMatch;   ///< the ETag it was changed from (Update, Delete)
    QDateTime at;      ///< when (the newer one wins a conflict)
};

struct CalendarState {
    QString syncToken;
    QString ctag;
    QMap<QString, Resource> resources; ///< by path
    QMap<QString, TrackedTask> tasks;  ///< by UID
    QList<EventChange> outbox;
    QDateTime syncedAt;
    QString error;     ///< the last run's failure code ("" none)
    QString errorText;

    QJsonObject toJson() const;
    static CalendarState fromJson(const QJsonObject &json);
};

/// One line of the calendars' log (the sync center).
struct LogEntry {
    enum class Kind {
        Refreshed,  ///< a subscription fetched (or unchanged)
        Synced,     ///< a CalDAV calendar synced: counts in `text`
        Conflict,   ///< a task or event changed on both sides
        Deleted,    ///< deletions held back or done
        Error,
    };
    QDateTime at;
    Kind kind = Kind::Synced;
    QString calendar; ///< its id
    QString name;     ///< its name then
    QString code;     ///< error code, conflict policy ("newer", "both")
    QString text;     ///< technical detail (English) or the task's summary
    int downloaded = 0;
    int uploaded = 0;
    int deleted = 0;
    QJsonObject toJson() const;
    static LogEntry fromJson(const QJsonObject &json);
};

class CalendarStore {
public:
    static constexpr int MaxLogEntries = 200;

    /// `folder`: `<device state>/calendars` ("" = nothing kept: memory only).
    explicit CalendarStore(QString folder);

    QString folder() const { return m_folder; }
    /// Read everything from the files again (another process — Android's background check — may
    /// have synced meanwhile).
    void reload();
    CalendarSettings settings() const { return m_settings; }
    bool saveSettings(const CalendarSettings &settings);

    QByteArray feedData(const QString &id) const;
    FeedMeta feedMeta(const QString &id) const;
    bool saveFeed(const QString &id, const QByteArray *data, const FeedMeta &meta);

    CalendarState state(const QString &calendarId) const;
    bool saveState(const QString &calendarId, const CalendarState &state);

    /// Everything of a subscription or calendar removed (its cache, its state).
    void forget(const QString &id);

    QList<LogEntry> log() const { return m_log; }
    void append(const LogEntry &entry);
    void clearLog();

    /// A new id ("s-…" for subscriptions, "a-…" accounts, "c-…" calendars).
    static QString newId(QChar kind);

private:
    QString path(const QString &name) const;
    bool write(const QString &name, const QByteArray &data) const;
    QByteArray read(const QString &name) const;

    QString m_folder;
    CalendarSettings m_settings;
    mutable QMap<QString, CalendarState> m_states; ///< memory only (no folder) and the read cache
    QMap<QString, QByteArray> m_feeds;            ///< memory only
    QMap<QString, FeedMeta> m_metas;              ///< memory only
    QList<LogEntry> m_log;
};

} // namespace brulion::caldav
