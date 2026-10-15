#include "workspaceintegration.h"

#include "appcontroller.h"
#include "appsettings.h"
#include "workspace.h"
#include "workspacereminders.h"
#include "workspacesync.h"

#include "desktopnotifier.h"
#include "notifier.h"

#include <QCoreApplication>
#include <QPointer>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

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
    QObject::connect(notifier, &notify::Notifier::errorOccurred, app,
                     [app](const QString &message) { app->toast(message, u"error"_s); });
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
        n.intervalMinutes = settings->syncIntervalMinutes();
        n.password = settings->nextcloudPassword();
        sync->setDeviceName(settings->value(u"sync/deviceName"_s, QString()).toString());
        sync->setNextcloud(n);
    };
    const auto applySyncthing = [settings, sync] {
        SyncthingSettings s;
        s.apiKey = settings->value(u"sync/syncthingApiKey"_s, QString()).toString().trimmed();
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
}

} // namespace brulion::shell
