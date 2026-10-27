#include "workspaceintegration.h"

#include "appcontroller.h"
#include "appsettings.h"
#include "workspace.h"
#include "workspacereminders.h"
#include "workspacesync.h"

#include "desktopnotifier.h"
#include "notifier.h"
#include "remindersounds.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

const QString kSyncthingKeyLegacy = u"sync/syncthingApiKey"_s;
const QString kSyncthingKeyRevision = u"sync/syncthingApiKeyRevision"_s;

QString syncthingKeyFile(const AppSettings *settings)
{
    return QFileInfo(settings->passwordFile()).absolutePath() + u"/syncthing-api-key"_s;
}

/// Owner-only secret file (as AppSettings stores the Nextcloud app password); "" removes it.
bool writeSecretFile(const QString &file, const QString &value)
{
    if (value.isEmpty())
        return !QFileInfo::exists(file) || QFile::remove(file);
    const QString dir = QFileInfo(file).absolutePath();
    if (!QDir().mkpath(dir))
        return false;
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    // Restricted before the secret is written; the rename keeps the permissions.
    out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    out.write(value.toUtf8());
    if (!out.commit())
        return false;
    QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

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
    QObject::connect(app, &AppController::statusChanged, reminders, [app, ws, reminders] {
        if (app->status() == u"onboarding" && ws->isOpen())
            reminders->cancelAll();
    });
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
    QObject::connect(settings, &AppSettings::nextcloudChanged, sync, applyNextcloud);
    QObject::connect(settings, &AppSettings::nextcloudPasswordChanged, sync, applyNextcloud);
    QObject::connect(settings, &AppSettings::valueChanged, sync, [applyNextcloud, applySyncthing](const QString &key) {
        if (key == u"sync/deviceName")
            applyNextcloud();
        else if (key.startsWith(u"sync/syncthing"_s))
            applySyncthing();
    });

    // The AppController's sync seam (status footer, "Sync now").
    const auto publish = [app, sync] {
        app->setSyncAvailable(sync->active());
        app->setSyncStatus(sync->appStatus());
    };
    publish();
    QObject::connect(sync, &WorkspaceSync::statusChanged, app, publish);
    QObject::connect(sync, &WorkspaceSync::configurationChanged, app, publish);
    QObject::connect(app, &AppController::syncRequested, sync, &WorkspaceSync::syncNow);
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

} // namespace

void integrateWorkspace(AppController *app)
{
    wireReminders(app);
    wireSync(app);
    // Pictures from the internet: shown without asking only when the device's setting says so.
    AppSettings *settings = app->settings();
    Workspace *ws = app->workspace();
    ws->setWebPicturesShown(settings->webPicturesShown());
    QObject::connect(settings, &AppSettings::webPicturesShownChanged, ws,
                     [settings, ws] { ws->setWebPicturesShown(settings->webPicturesShown()); });
}

QString syncthingApiKey(AppSettings *settings)
{
    if (!settings)
        return {};
    const QString file = syncthingKeyFile(settings);
    const QString legacy = settings->value(kSyncthingKeyLegacy, QString()).toString().trimmed();
    if (!legacy.isEmpty()) {
        // Stored in plain QSettings by an older version: move it into the owner-only file.
        if (!writeSecretFile(file, legacy))
            return legacy;
        settings->setValue(kSyncthingKeyLegacy, QString());
        return legacy;
    }
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(in.readAll()).trimmed();
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
    settings->setValue(kSyncthingKeyRevision, settings->value(kSyncthingKeyRevision, 0).toInt() + 1);
    return true;
}

} // namespace brulion::shell
