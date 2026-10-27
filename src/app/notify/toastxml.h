// Windows toast notifications of reminders and alarms (docs/WINDOWS.md "Powiadomienia") — the
// pure parts, built on every platform so the desktop tests cover them; WindowsToastBus
// (windowstoastbus.h, Windows only) shows the XML through WinRT's ToastNotificationManager.
//
// buildToastXml() maps the freedesktop message DesktopNotifier builds (desktopnotifier.h) onto
// the toast schema (https://learn.microsoft.com/windows/apps/design/shell/tiles-and-notifications/
// toast-schema), written with QXmlStreamWriter — every text and attribute is escaped, note text
// never becomes markup:
//
//   <toast launch="brulion-toast:id=7&amp;action=default" scenario="alarm|reminder"
//          duration="long">                     alarms (urgency 2 / resident) stay on screen with
//                                               their looping sound; reminders stay ~25 s, then
//                                               wait in the Action Center
//     <visual><binding template="ToastGeneric"><text>summary</text><text>body</text>…
//     <actions><action content="Drzemka 10 min"
//                      arguments="brulion-toast:id=7&amp;action=snooze"
//                      activationType="foreground"/>…   one per action of the message except
//                                               "default" (the click on the toast: `launch`)
//     <audio src="ms-winsoundevent:Notification.Looping.Alarm" loop="true"/>  or silent="true"
//
// Every action the message carries becomes a button, so a new action key (e.g. "done" for "mark
// the task done", TODO notifydone) needs only buildDesktopNotification() and DesktopNotifier;
// parseToastArguments() already accepts it.
#pragma once

#include "desktopnotifier.h"

#include <QString>
#include <QStringList>

namespace brulion::notify {

/// The toast XML of `notification` shown as `id` (the toast's tag; the activation arguments name
/// it). Sounds: "suppress-sound" → silent; "sound-name" one of windowsToastSounds() (or a
/// freedesktop name with a Windows counterpart: alarm-clock-elapsed → Looping.Alarm,
/// phone-incoming-call → Looping.Call, message-new-instant → IM) → that sound event, anything
/// else (a sound file — unpackaged apps cannot play one —, an unknown name) → the default sound;
/// alarms with a looping sound loop it. Characters XML cannot carry (control characters, lone
/// surrogates) are left out.
QString buildToastXml(const DesktopNotification &notification, uint id);

/// The activation arguments of a toast's click ("default") or one of its buttons.
QString toastArguments(uint id, const QString &action);

/// A toast's activation (ToastActivatedEventArgs::Arguments()), parsed.
struct ToastActivation {
    bool valid = false;
    uint id = 0;
    QString action; ///< "default", "snooze", "ok" or "done"
};

/// Strict: exactly "brulion-toast:id=<1–4294967295>&action=<default|snooze|ok|done>" (decimal, no
/// sign, no spaces, nothing more); anything else is not valid.
ToastActivation parseToastArguments(const QString &arguments);

/// The Windows notification sounds Settings → Reminders offers ("custom" sounds, the names of
/// ms-winsoundevent:Notification.<name>): Default, IM, Mail, Reminder, SMS, Looping.Alarm,
/// Looping.Alarm2…10, Looping.Call, Looping.Call2…10.
QStringList windowsToastSounds();

/// The Windows sound event for a "sound-name" hint ("" when it has none): one of
/// windowsToastSounds() as it is, or a freedesktop name with a Windows counterpart.
QString windowsToastSoundFor(const QString &soundName);

} // namespace brulion::notify
