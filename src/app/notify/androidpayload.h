// The reminders handed to Android (AndroidBridge::scheduleReminders(), docs/ANDROID.md) as
// JSON — built on every platform so it is unit-tested on the desktop.
//
//   { id: nativeId (positive 31-bit, stable per occurrence), title, body,
//     at: epoch ms of the local wall-clock time, alarm: bool, ringMinutes: 1–60 (alarms),
//     path: vault-relative note, line: 0-based line of the reminder }
#pragma once

#include "reminders/notifications.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>

namespace brulion::notify {

/// One reminder (a missing nativeId is derived from the id with nativeIdFor()).
QJsonObject androidReminderItem(const ScheduledNotification &notification);

/// All reminders, in order; entries without a valid time are left out.
QJsonArray androidReminderPayload(const QList<ScheduledNotification> &notifications);

} // namespace brulion::notify
