// The reminders handed to Android (AndroidBridge::scheduleReminders(), docs/ANDROID.md) as
// JSON — built on every platform so it is unit-tested on the desktop.
//
//   { id: nativeId (positive 31-bit, stable per occurrence), title, body,
//     at: epoch ms of the local wall-clock time, alarm: bool, ringMinutes: 1–60 (alarms),
//     path: vault-relative note, line: 0-based line of the reminder,
//     key: the occurrence's id (ScheduledNotification::id), local: its wall-clock time
//     "yyyy-MM-ddTHH:mm" (never derived through UTC: a natively snoozed copy has another `at`),
//     task: the reminder is on an open task (the notification offers "Zrobione"),
//     vault: the notebook's id (ScheduledNotification::vault) }
//
// "Zrobione" tapped in a notification or on the alarm screen is queued by the Kotlin side
// (DoneQueue.kt) and taken by AndroidBridge::takeDoneRequests() as a JSON array of
//
//   { key, local, path, line, title, day: "yyyy-MM-dd" — the local date of the tap, vault }
//
// androidDoneRequests() turns them into the notifications to complete (Notifier::doneRequested).
//
// The sounds (AndroidBridge::setReminderSounds(), previewSound(), pickSound()):
//
//   { reminder: {mode, uri, title}, alarm: {mode, uri, title} }   mode: default | custom |
//   vibrate | silent; uri: the ringtone (content://…) of a custom sound. The Kotlin side
//   (ReminderSounds.kt) gives a kind whose sound changed a new notification channel.
#pragma once

#include "reminders/notifications.h"
#include "soundchoice.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>

namespace brulion::notify {

/// One reminder (a missing nativeId is derived from the id with nativeIdFor()).
QJsonObject androidReminderItem(const ScheduledNotification &notification);

/// All reminders, in order; entries without a valid time are left out.
QJsonArray androidReminderPayload(const QList<ScheduledNotification> &notifications);

/// A "Done" tapped on Android: the notification (id, at, path, line, title, vault) and the day of
/// the tap.
struct AndroidDoneRequest {
    ScheduledNotification notification;
    QDate day; ///< invalid: not given (completed with today's date)
};

/// The "Done" requests of `array` (AndroidBridge::takeDoneRequests()), in order; entries without
/// a key, a note or a valid `local` time are left out.
QList<AndroidDoneRequest> androidDoneRequests(const QJsonArray &array);

/// One sound: {mode, uri, title} (normalised: a custom sound without a uri is the default).
QJsonObject androidSoundItem(const SoundChoice &choice);
/// Both sounds: {reminder: {…}, alarm: {…}}.
QJsonObject androidSoundSettings(const SoundChoice &reminder, const SoundChoice &alarm);
/// A sound to preview: {kind: "reminder" | "alarm", mode, uri, title}.
QJsonObject androidSoundPreview(SoundKind kind, const SoundChoice &choice);
/// The sound the system picker answered with ({mode, uri, title} from Kotlin); `ok` false for
/// an error answer ({error: …}) or nonsense.
SoundChoice androidPickedSound(const QJsonObject &answer, bool *ok = nullptr);

} // namespace brulion::notify
