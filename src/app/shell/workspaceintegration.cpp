#include "workspaceintegration.h"

#include "appcontroller.h"
#include "appsettings.h"
#include "workspace.h"
#include "workspacebackups.h"
#include "workspacegithub.h"
#include "workspacereminders.h"
#include "workspacesync.h"

#include "backgroundcheck.h" // Android's background check of the notes (src/app/background)
#include "desktopnotifier.h"
#include "notifier.h"
#include "remindersounds.h"

#include "log/categories.h"
#include "vault/vault.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

const QString kSyncthingKeyLegacy = u"sync/syncthingApiKey"_s;
const QString kSyncthingKeyRevision = u"sync/syncthingApiKeyRevision"_s;

QString syncthingKeyFile(const AppSettings *settings)
{
    return QFileInfo(settings->passwordFile()).absolutePath() + u"/syncthing-api-key"_s;
}

void wireBackgroundCheck(AppController *app, notify::Notifier *notifier);

void wireReminders(AppController *app)
{
    AppSettings *settings = app->settings();
    Workspace *ws = app->workspace();
    WorkspaceReminders *reminders = ws->reminders();

    notify::Notifier *notifier = notify::createPlatformNotifier(app);
    const QPointer<notify::Notifier> guard(notifier);
    reminders->setBackendInfo([guard] { return guard ? guard->backend() : QString(); },
                              [guard] { return guard && guard->canShow(); });
    reminders->setSink(notifier, notifier);

    const auto apply = [settings, reminders, guard] {
        reminders->setEnabled(settings->remindersEnabled());
        reminders->setAlarmRingMinutes(settings->alarmRingMinutes());
        reminders->setSnoozeMinutes(settings->snoozeMinutes());
        reminders->setNotifyMissed(settings->notifyMissed());
        if (guard)
            guard->setSnoozeMinutes(settings->snoozeMinutes());
    };
    apply();
    QObject::connect(settings, &AppSettings::remindersEnabledChanged, reminders, apply);
    QObject::connect(settings, &AppSettings::alarmRingMinutesChanged, reminders, apply);
    QObject::connect(settings, &AppSettings::snoozeMinutesChanged, reminders, apply);
    QObject::connect(settings, &AppSettings::notifyMissedChanged, reminders, apply);

    QObject::connect(notifier, &notify::Notifier::snoozeRequested, reminders,
                     [reminders](const ScheduledNotification &n, int minutes) {
                         reminders->snoozeNotification(n, minutes);
                     });
    // "Done" on a task's reminder (D-Bus, Windows toasts, Android — there possibly a tap queued
    // while Brulion was closed, applied once the notebook is ready).
    QObject::connect(
            notifier, &notify::Notifier::doneRequested, reminders,
            [reminders](const ScheduledNotification &n, QDate day) { reminders->completeNotification(n, day); });
    // Android keeps its queued taps until their notebook is ready to write them.
    const auto doneTarget = [reminders, guard] {
        if (guard)
            guard->setDoneTarget(reminders->doneTarget());
    };
    QObject::connect(reminders, &WorkspaceReminders::doneTargetChanged, notifier, doneTarget);
    doneTarget();
    // The confirmation (also of the missed banner's "Done"): after a cold start on Android the
    // only sign that the queued tap was written.
    QObject::connect(reminders, &WorkspaceReminders::taskCompleted, app, [app](const QString &title) {
        app->toast(QCoreApplication::translate("brulion::shell::WorkspaceIntegration", "“%1” marked done").arg(title),
                   u"success"_s);
    });
    QObject::connect(notifier, &notify::Notifier::errorOccurred, app,
                     [app](const QString &message) { app->toast(message, u"error"_s); });
    // The sounds of reminders and alarms (Settings → Reminders, QML ReminderSounds): device-local,
    // kept in AppSettings ("reminders/sound/…"), played by the notifier.
    auto *sounds = new notify::ReminderSounds(app);
    sounds->setStorage([settings](const QString &key) { return settings->value(key, QString()); },
                       [settings](const QString &key, const QVariant &value) { settings->setValue(key, value); });
    sounds->setNotifier(notifier);
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_WASM)
    // Android taps reach AppController through main.cpp (AndroidBridge::activated), browser
    // clicks through its own WasmNotifier: only the desktop notifier opens notes from here.
    QObject::connect(notifier, &notify::Notifier::activated, app, &AppController::openNotification);
#endif
    if (auto *desktop = qobject_cast<notify::DesktopNotifier *>(notifier)) {
        desktop->setTrayAvailable(app->trayAvailable());
        QObject::connect(app, &AppController::trayAvailableChanged, desktop,
                         [app, desktop] { desktop->setTrayAvailable(app->trayAvailable()); });
        // No notification service: the tray shows the reminder (DesktopTray).
        QObject::connect(desktop, &notify::DesktopNotifier::trayMessageRequested, app,
                         &AppController::notificationRequested);
    }

    app->setTestNotificationHandler([app, reminders, guard](const QString &title, const QString &body) {
        if (!guard || !guard->canShow()) {
            QString message;
            if (app->isWasm())
                message = QCoreApplication::translate("brulion::shell::WorkspaceIntegration",
                                                      "The browser does not allow notifications for Brulion.");
            else if (app->isAndroid())
                message = QCoreApplication::translate("brulion::shell::WorkspaceIntegration",
                                                      "Notifications are turned off for Brulion in Android's settings.");
            else
                message = QCoreApplication::translate(
                        "brulion::shell::WorkspaceIntegration",
                        "No notification service or system tray on this desktop: reminders appear only inside Brulion.");
            app->toast(message, u"warning"_s);
            return;
        }
        reminders->sendTestNotification(title, body);
        if (app->isAndroid())
            app->toast(QCoreApplication::translate("brulion::shell::WorkspaceIntegration",
                                                   "A test notification will appear in a few seconds."),
                       u"info"_s);
    });

    // A reminder fired with nowhere to show it (no notification service and no tray, or the
    // browser refused): inside the window at least.
    QObject::connect(reminders, &WorkspaceReminders::fired, app, [app, guard](const QVariantMap &n) {
        if (guard && guard->canShow())
            return;
        const QString body = n.value(u"body"_s).toString();
        QString text = u"⏰ "_s + n.value(u"title"_s).toString();
        if (!body.isEmpty())
            text += u" — "_s + body;
        app->toast(text, n.value(u"alarm"_s).toBool() ? u"warning"_s : u"info"_s);
    });

    // Closing a notebook on purpose (back to onboarding): nothing of it may ring any more.
    // Quitting keeps Android's alarms armed — they are meant to ring while Brulion is closed.
    QObject::connect(app, &AppController::statusChanged, reminders, [app, ws, reminders, guard] {
        if (app->status() == u"onboarding" && ws->isOpen()) {
            reminders->cancelAll();
            if (guard)
                guard->setBackgroundCheck({}); // nor may a background check arm them again
        }
    });

    wireBackgroundCheck(app, notifier);
}

void wireBackgroundCheck(AppController *app, notify::Notifier *notifier)
{
    // Android's background check (docs/ANDROID.md "Checking notes in the background"): the notebook whose
    // reminders run is the one it reads while Brulion is closed. Handed over whenever they start
    // and when a setting it uses changes; kept when they stop because the app quits, another
    // notebook is being opened or the documentation is open (Android keeps that notebook's alarms
    // armed then); off with the reminders, or with the check's own setting.
    AppSettings *settings = app->settings();
    Workspace *ws = app->workspace();
    WorkspaceReminders *reminders = ws->reminders();
    WorkspaceSync *sync = ws->sync();
    const QPointer<notify::Notifier> guard(notifier);
    reminders->setBackgroundCheckInfo([guard] { return guard ? guard->backgroundCheckStatus() : QJsonObject(); });

    const auto push = [app, settings, ws, reminders, sync, guard] {
        if (!guard)
            return;
        const int interval = background::normalizedInterval(
                settings->value(QString::fromLatin1(background::IntervalSettingKey), background::DefaultIntervalMinutes)
                        .toInt());
        if (!settings->remindersEnabled() || interval <= 0) {
            guard->setBackgroundCheck({});
            return;
        }
        Vault *vault = ws->vault();
        if (!reminders->isRunning() || !vault)
            return; // keep what was handed over for the notebook whose alarms stay armed
        background::Target target;
        target.root = vault->rootPath();
        target.stateDir = app->deviceStatePath(vault->rootPath());
        target.vault = vault->id();
        target.ringMinutes = settings->alarmRingMinutes();
        target.language = settings->language();
        target.intervalMinutes = interval;
        if (sync->active()) {
            target.nextcloud = background::Target::Nextcloud{sync->currentDavUrl(), settings->nextcloudUser().trimmed(),
                                                             sync->deviceName()};
        }
        guard->setBackgroundCheck(target.toJson());
    };
    QObject::connect(reminders, &WorkspaceReminders::runningChanged, notifier, push);
    QObject::connect(settings, &AppSettings::remindersEnabledChanged, notifier, push);
    QObject::connect(settings, &AppSettings::alarmRingMinutesChanged, notifier, push);
    QObject::connect(settings, &AppSettings::languageChanged, notifier, push);
    QObject::connect(settings, &AppSettings::valueChanged, notifier, [push](const QString &key) {
        if (key == QLatin1String(background::IntervalSettingKey))
            push();
    });
    QObject::connect(sync, &WorkspaceSync::configurationChanged, notifier, push);
    push();

    // What the last check did while Brulion was closed, for the event log.
    const QJsonObject last = notifier->backgroundCheckStatus();
    if (const qint64 at = last.value(u"at"_s).toInteger(); at > 0) {
        qCInfo(logging::lcReminders).noquote() << u"Last background check %1: %2"_s.arg(
                QDateTime::fromMSecsSinceEpoch(at).toString(u"yyyy-MM-dd HH:mm"_s),
                QString::fromUtf8(QJsonDocument(last).toJson(QJsonDocument::Compact)));
    }
}

void wireSync(AppController *app)
{
    AppSettings *settings = app->settings();
    WorkspaceSync *sync = app->workspace()->sync();

    const auto applyNextcloud = [settings, sync] {
        NextcloudSettings n;
        n.enabled = settings->nextcloudEnabled();
        n.url = settings->nextcloudUrl();
        n.user = settings->nextcloudUser();
        n.folder = settings->nextcloudFolder();
        // Another notebook of this device on the same server folder (set up by 0.3.1–0.3.5; the
        // settings form refuses that now): nothing syncs until one of them gets another folder.
        n.sharedWith = n.enabled ? settings->nextcloudFolderUser(n.folder) : QString();
        n.intervalMinutes = settings->syncIntervalMinutes();
        n.password = settings->nextcloudPassword();
        sync->setDeviceName(settings->value(u"sync/deviceName"_s, QString()).toString());
        sync->setNextcloud(n);
    };
    const auto applySyncthing = [settings, sync] {
        SyncthingSettings s;
        s.apiKey = syncthingApiKey(settings);
        s.url = settings->value(u"sync/syncthingUrl"_s, QString()).toString().trimmed();
        sync->setSyncthing(s);
    };
    applyNextcloud();
    applySyncthing();
    // Changed Syncthing values are applied together, when the event loop runs again: the setup form saves
    // the key, then the address, and each apply asks Syncthing at once — the new key would reach the old address.
    auto *syncthingApply = new QTimer(sync);
    syncthingApply->setSingleShot(true);
    syncthingApply->setInterval(0);
    QObject::connect(syncthingApply, &QTimer::timeout, sync, applySyncthing);
    QObject::connect(settings, &AppSettings::nextcloudChanged, sync, applyNextcloud);
    QObject::connect(settings, &AppSettings::nextcloudPasswordChanged, sync, applyNextcloud);
    QObject::connect(settings, &AppSettings::valueChanged, sync, [applyNextcloud, syncthingApply](const QString &key) {
        if (key == u"sync/deviceName")
            applyNextcloud();
        else if (key.startsWith(u"sync/syncthing"_s))
            syncthingApply->start();
    });

    // The AppController's sync seam (status footer, "Sync now"): Nextcloud's, else — a notebook
    // kept on GitHub without Nextcloud — GitHub's (its "Sync now" gets the newest version).
    WorkspaceGitHub *github = app->workspace()->github();
    const auto publish = [app, sync, github] {
        if (!sync->active() && github->linked()) {
            app->setSyncAvailable(true);
            app->setSyncStatus(github->appStatus());
            return;
        }
        app->setSyncAvailable(sync->active());
        app->setSyncStatus(sync->appStatus());
    };
    publish();
    QObject::connect(sync, &WorkspaceSync::statusChanged, app, publish);
    QObject::connect(sync, &WorkspaceSync::configurationChanged, app, publish);
    QObject::connect(github, &WorkspaceGitHub::statusChanged, app, publish);
    QObject::connect(github, &WorkspaceGitHub::linkChanged, app, publish);
    QObject::connect(app, &AppController::syncRequested, sync, [sync, github] {
        if (!sync->active() && github->linked())
            github->pull();
        else
            sync->syncNow();
    });
    QObject::connect(sync, &WorkspaceSync::folderSharingFound, app,
                     [app](const QString &message) { app->toast(message, u"warning"_s); });
    QObject::connect(sync, &WorkspaceSync::conflictsFound, app, [app](int count) {
        app->toast(QCoreApplication::translate("brulion::shell::WorkspaceIntegration",
                                               "%n note(s) changed on two devices at once. Both versions are kept: "
                                               "merge them in the sync center.",
                                               nullptr, count),
                   u"warning"_s);
    });
}

void wireGitHub(AppController *app)
{
    // A notebook kept in a GitHub repository (workspacegithub.h): the per-notebook links in
    // AppSettings, the token in an owner-only file next to the Nextcloud app password, new
    // notebooks from GitHub in the app's storage (in the browser too: the app data folder is kept
    // in IndexedDB), its messages as toasts.
    AppSettings *settings = app->settings();
    WorkspaceGitHub *github = app->workspace()->github();
    github->setBrowser(app->isWasm());
    github->setNotebooksRoot(QDir(app->dataDir()).filePath(u"GitHub"_s));
    github->setSettingsAccess(
            [settings](const QString &key, const QVariant &fallback) { return settings->value(key, fallback); },
            [settings](const QString &key, const QVariant &value) { settings->setValue(key, value); });
    github->setTokenFile(QFileInfo(settings->passwordFile()).absolutePath() + u"/github-token"_s);
    QObject::connect(github, &WorkspaceGitHub::operationFinished, app,
                     [app](const QString &, bool, const QString &message, const QString &tone) {
                         if (!message.isEmpty())
                             app->toast(message, tone);
                     });
}

} // namespace

void integrateWorkspace(AppController *app)
{
    wireReminders(app);
    wireGitHub(app);
    wireSync(app);
    // Pictures from the internet: shown without asking only when the device's setting says so.
    AppSettings *settings = app->settings();
    Workspace *ws = app->workspace();
    ws->setTaskStatusHistory(settings->taskStatusHistory());
    QObject::connect(settings, &AppSettings::taskStatusHistoryChanged, ws,
                     [settings, ws] { ws->setTaskStatusHistory(settings->taskStatusHistory()); });
    ws->setWebPicturesShown(settings->webPicturesShown());
    QObject::connect(settings, &AppSettings::webPicturesShownChanged, ws,
                     [settings, ws] { ws->setWebPicturesShown(settings->webPicturesShown()); });
    // Automatic backups: their messages (also of a failed scheduled run) as toasts.
    QObject::connect(ws->backups(), &WorkspaceBackups::messageRequested, app,
                     [app](const QString &text, const QString &kind) { app->toast(text, kind); });
}

QString syncthingApiKey(AppSettings *settings)
{
    if (!settings)
        return {};
    const QString file = syncthingKeyFile(settings);
    const QString legacy = settings->value(kSyncthingKeyLegacy, QString()).toString().trimmed();
    if (!legacy.isEmpty()) {
        // Stored in plain QSettings by an older version: move it into the owner-only file.
        if (writeSecretFile(file, legacy))
            settings->setValue(kSyncthingKeyLegacy, QString());
        return legacy;
    }
    return readSecretFile(file);
}

bool setSyncthingApiKey(AppSettings *settings, const QString &key)
{
    if (!settings)
        return false;
    const QString clean = key.trimmed();
    if (clean == syncthingApiKey(settings))
        return true;
    if (!writeSecretFile(syncthingKeyFile(settings), clean))
        return false;
    // A key left in QSettings (its move failed) would still win over the file: replaced — or forgotten — too.
    if (!settings->value(kSyncthingKeyLegacy, QString()).toString().isEmpty())
        settings->setValue(kSyncthingKeyLegacy, QString());
    settings->setValue(kSyncthingKeyRevision, settings->value(kSyncthingKeyRevision, 0).toInt() + 1);
    return true;
}

} // namespace brulion::shell
