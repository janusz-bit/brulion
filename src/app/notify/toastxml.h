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
// Every action the message carries becomes a button: "Drzemka", "Zrobione" (a task's reminder:
// marks the task done) and "OK".
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

/// The toast of `notification` loops its sound by itself: an alarm (with buttons, so it stays on
/// screen) whose sound is one of the Looping.* events. Any other alarm (a short sound such as
/// "Mail", the default sound) plays its sound once, so DesktopNotifier sends it again every ring
/// interval as on Linux (NotificationBus::ringsItself()).
bool toastLoopsItsSound(const DesktopNotification &notification);

/// What ToastNotifier::Setting() says about this app's toasts. Unknown: it threw — an app without
/// a package gets ERROR_NOT_FOUND until it has shown its first toast.
enum class ToastSetting {
    Unknown,
    Enabled,
    DisabledForApplication,
    DisabledForUser,
    DisabledByGroupPolicy,
    DisabledByManifest,
};

/// Toasts may be shown: the AppUserModelID is registered (an unregistered one's toasts are
/// accepted and never appear) and Windows has not turned them off; Unknown counts as allowed,
/// otherwise the first toast — after which Windows knows the app — would never be sent.
bool toastsAllowed(bool registered, ToastSetting setting);

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
