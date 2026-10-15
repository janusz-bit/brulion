// The app-level wiring of the Workspace's reminders and sync (called once by AppController's
// constructor):
//
//   reminders   the platform's notifier (src/app/notify: D-Bus with the tray as fallback,
//               Android alarms, browser notifications) becomes the Workspace's sink; the
//               reminder settings (on/off, alarm ring time, snooze length, missed reminders)
//               follow AppSettings; "Snooze" in a notification writes the new time into the
//               note; a click opens it (desktop — Android and the browser deliver taps through
//               main.cpp / AppController themselves); the settings page's test notification goes
//               through the same notifier; closing a notebook cancels what it armed
//   sync        Nextcloud settings and the app password (AppSettings), the device name
//               ("sync/deviceName") and the Syncthing API key / address ("sync/syncthingApiKey",
//               "sync/syncthingUrl") configure Workspace.sync; its status feeds the
//               AppController's sync seam (syncAvailable, syncStatus, requestSync()); new
//               conflicts are announced with a toast
#pragma once

namespace brulion::shell {

class AppController;

void integrateWorkspace(AppController *app);

} // namespace brulion::shell
