// The app-level wiring of the Workspace's reminders and sync (called once by AppController's
// constructor):
//
//   reminders   the platform's notifier (src/app/notify: D-Bus with the tray as fallback,
//               Android alarms, browser notifications) becomes the Workspace's sink; the
//               reminder settings (on/off, alarm ring time, snooze length, missed reminders)
//               follow AppSettings; "Snooze" in a notification writes the new time into the
//               note; a click opens it (desktop — Android and the browser deliver taps through
//               main.cpp / AppController themselves); the settings page's test notification goes
//               through the same notifier; the sounds of reminders and alarms
//               (notify::ReminderSounds, stored in AppSettings) reach it too; closing a notebook
//               cancels what it armed
//   sync        Nextcloud settings and the app password (AppSettings), the device name
//               ("sync/deviceName"), the Syncthing address ("sync/syncthingUrl") and API key (an
//               owner-only file, syncthingApiKey() below) configure Workspace.sync; its status feeds the
//               AppController's sync seam (syncAvailable, syncStatus, requestSync()); new
//               conflicts are announced with a toast
#pragma once

#include <QString>

namespace brulion::shell {

class AppController;
class AppSettings;

void integrateWorkspace(AppController *app);

/// The Syncthing REST API key of this device (it controls the whole Syncthing instance): kept
/// like the Nextcloud app password in an owner-only file next to it
/// (<app data>/secrets/syncthing-api-key, 0600), never in the QSettings file. A key an older
/// version stored in QSettings ("sync/syncthingApiKey") moves into that file on the first read.
QString syncthingApiKey(AppSettings *settings);
/// Store the key ("" forgets it); announced as AppSettings::valueChanged(
/// "sync/syncthingApiKeyRevision") so the sync wiring picks it up. False on I/O errors.
bool setSyncthingApiKey(AppSettings *settings, const QString &key);

} // namespace brulion::shell
