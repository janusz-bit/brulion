// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/** A reminder came due (AlarmManager): forget it and show the notification. */
class ReminderReceiver : BroadcastReceiver() {
  override fun onReceive(context: Context, intent: Intent) {
    if (intent.action != Actions.FIRE) return
    val reminder = Reminder.fromIntent(intent) ?: return
    ReminderStore(context).removeFired(reminder.id, System.currentTimeMillis())
    ReminderNotifier.show(context, reminder)
  }
}

/** Notification buttons: "Drzemka 10 min" and "OK". */
class ActionReceiver : BroadcastReceiver() {
  override fun onReceive(context: Context, intent: Intent) {
    val reminder = Reminder.fromIntent(intent) ?: return
    when (intent.action) {
      Actions.SNOOZE -> ReminderActions.snooze(context, reminder)
      Actions.DISMISS -> ReminderActions.dismiss(context, reminder.id)
    }
  }
}

/**
 * Alarms do not survive a reboot (and are re-evaluated on clock changes): re-arm the stored
 * reminders. Also runs after an app update and when the exact-alarm permission is granted,
 * which turns inexact alarms into exact ones.
 *
 * Not exported: system broadcasts still reach it (like WorkManager's RescheduleReceiver),
 * other apps cannot trigger it.
 */
class BootReceiver : BroadcastReceiver() {
  override fun onReceive(context: Context, intent: Intent) {
    when (intent.action) {
      Intent.ACTION_BOOT_COMPLETED,
      ACTION_QUICKBOOT_POWERON,
      Intent.ACTION_MY_PACKAGE_REPLACED,
      Intent.ACTION_TIME_CHANGED,
      Intent.ACTION_TIMEZONE_CHANGED,
      ACTION_EXACT_ALARM_PERMISSION_CHANGED -> AlarmScheduler.rearmFromStore(context)
    }
  }

  private companion object {
    /** Fast boot of some vendors (HTC, older Xiaomi) instead of BOOT_COMPLETED. */
    const val ACTION_QUICKBOOT_POWERON = "android.intent.action.QUICKBOOT_POWERON"

    /** `AlarmManager.ACTION_SCHEDULE_EXACT_ALARM_PERMISSION_STATE_CHANGED` (Android 12+). */
    const val ACTION_EXACT_ALARM_PERMISSION_CHANGED =
      "android.app.action.SCHEDULE_EXACT_ALARM_PERMISSION_STATE_CHANGED"
  }
}

/** What the notification buttons and the alarm screen do. */
internal object ReminderActions {
  const val SNOOZE_MS = 10L * 60 * 1000

  /** Silences the notification and shows it again in 10 minutes (re-armed natively). */
  fun snooze(context: Context, reminder: Reminder, now: Long = System.currentTimeMillis()) {
    ReminderNotifier.cancel(context, reminder.id)
    val next = reminder.copy(at = now + SNOOZE_MS, snoozed = true)
    synchronized(AlarmScheduler.lock) {
      ReminderStore(context).upsert(next)
      AlarmScheduler.arm(context, next, AlarmScheduler.canScheduleExact(context))
    }
  }

  fun dismiss(context: Context, id: Int) {
    ReminderNotifier.cancel(context, id)
  }
}
