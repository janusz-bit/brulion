// The background check of the notes (Android, docs/ANDROID.md "Checking notes in the background"): while
// Brulion is closed, the system runs a periodic job (JobScheduler, BackgroundCheckJob.kt) that
// starts Brulion's library in a process of its own without any UI (BackgroundCheckService.kt, a
// QtService; main() → runBackgroundCheck()). There one Check:
//
//   1. opens the notebook whose reminders are armed (Target: handed over by the app whenever its
//      reminders start, kept by the Kotlin side) as brulion-cli does (notebook::Notebook);
//   2. with Nextcloud sync set up for it, syncs it once (the same SyncEngine and sync bookkeeping as
//      the app; the engine's lock keeps the app and this process from syncing at the same time);
//   3. reads the notes (the index cache makes that quick) and computes the reminders exactly as the
//      app's ReminderScheduler would hand them to Android (upcomingFor());
//   4. answers with the JSON of AndroidBridge::scheduleReminders() — the Kotlin side arms it in
//      the app's main process, which owns the armed set.
//
// Nothing here writes notes except the sync (as the app's sync would); a check that cannot read the
// notebook (missing folder, no files: an unmounted card, the storage permission gone) answers
// ok = false, and the armed reminders stay as they are.
//
// Built on every platform (the desktop tests run it); only Android starts it.
#pragma once

#include "reminders/notifications.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>

namespace brulion {
class VaultIndex;
namespace notebook {
class Notebook;
}
namespace sync {
class HttpTransport;
class SyncEngine;
class WebDavClient;
struct SyncReport;
} // namespace sync
} // namespace brulion

namespace brulion::background {

/// How often the check runs (minutes): off, every 15 minutes (the shortest period Android allows),
/// every 30 minutes (the default), every hour.
inline constexpr int IntervalChoices[] = {0, 15, 30, 60};
inline constexpr int DefaultIntervalMinutes = 30;
/// The setting's key in AppSettings (device-local: AppSettings::value()).
inline constexpr char IntervalSettingKey[] = "reminders/backgroundCheckMinutes";
/// The nearest choice of IntervalChoices (negative: off).
int normalizedInterval(int minutes);

/// What the check works on: the notebook whose reminders the app armed, how they look, how often to
/// look. JSON (Kotlin keeps it, BackgroundCheck.kt):
///
///   { root, stateDir, vault, ringMinutes, language: "pl" | "en", intervalMinutes,
///     nextcloud: { davUrl, user, device } (absent: no Nextcloud sync) }
///
/// The Nextcloud app password is never part of it: the check reads it from the app's own file
/// (AppSettings::nextcloudPassword()).
struct Target {
    QString root;     ///< absolute notebook folder
    QString stateDir; ///< its device state folder (LocalStore, index cache, History)
    QString vault;    ///< Vault::id() of the notebook, stamped on the reminders
    int ringMinutes = ScheduledNotification::DefaultRingMinutes;
    QString language = QStringLiteral("pl");
    int intervalMinutes = DefaultIntervalMinutes;

    struct Nextcloud {
        QString davUrl; ///< WebDAV address of the notebook's server folder
        QString user;
        QString device; ///< this device's name in conflict copies
        friend bool operator==(const Nextcloud &, const Nextcloud &) = default;
    };
    std::optional<Nextcloud> nextcloud;

    /// A notebook to check, and checks are on.
    bool isValid() const;
    QJsonObject toJson() const;
    /// nullopt for nonsense (no root, state folder or notebook id); values are normalised.
    static std::optional<Target> fromJson(const QJsonObject &json);
    friend bool operator==(const Target &, const Target &) = default;
};

/// The reminders the app's ReminderScheduler would hand Android for `index` at `now`: its horizon
/// and limit, the target's ring time and language, the notebook's id on each.
QList<ScheduledNotification> upcomingFor(const VaultIndex &index, const QDateTime &now, const Target &target);

/// One check (see the file comment). start() once; finished() is emitted once, from the event loop.
///
/// Result: { ok, vault, notes (files read), reminders: [AndroidBridge::scheduleReminders() items],
///           sync: "off" | "ok" | "failed" | "busy" | "skipped", syncError (code, if any),
///           uploaded, downloaded, error (ok false: "missing" | "not-a-folder" | "failed" |
///           "empty" | "invalid"), message (technical, redacted), elapsedMs }
class Check : public QObject {
    Q_OBJECT
public:
    /// `password`: the Nextcloud app password (used only with target.nextcloud); `transport`: the
    /// HTTP transport of the sync (not owned; nullptr: no sync); `clock`: local wall-clock time.
    Check(Target target, QString password, sync::HttpTransport *transport, std::function<QDateTime()> clock = {},
          QObject *parent = nullptr);
    ~Check() override;

    /// The sync gives up after this long (the reminders are computed all the same).
    void setSyncTimeout(int msec) { m_syncTimeoutMs = msec; }
    void start();
    bool isFinished() const { return m_finished; }
    QJsonObject result() const { return m_result; }

Q_SIGNALS:
    void finished(const QJsonObject &result);

private:
    void syncThenRead();
    void syncDone(const sync::SyncReport &report);
    void readReminders();
    void fail(const QString &error, const QString &message);
    void finish();
    QDateTime now() const;

    Target m_target;
    QString m_password;
    sync::HttpTransport *m_transport = nullptr;
    std::function<QDateTime()> m_clock;
    int m_syncTimeoutMs = 3 * 60 * 1000;
    std::unique_ptr<notebook::Notebook> m_notebook;
    std::unique_ptr<sync::WebDavClient> m_client;
    std::unique_ptr<sync::SyncEngine> m_engine;
    QTimer m_syncTimer;
    QElapsedTimer m_elapsed;
    QJsonObject m_result;
    bool m_started = false;
    bool m_finished = false;
};

} // namespace brulion::background
