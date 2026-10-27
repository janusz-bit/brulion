// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.annotation.SuppressLint
import android.app.AlarmManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Build
import android.util.Log
import java.util.TimeZone

/**
 * Arms reminders with AlarmManager.
 *
 * Alarms use `setAlarmClock` (exact, wakes the device, shows the alarm icon), reminders
 * `setExactAndAllowWhileIdle`; without the exact-alarm permission both fall back to the
 * inexact `setAndAllowWhileIdle`. Each reminder id has its own PendingIntent (request code =
 * id), so arming an id again replaces its alarm.
 */
internal object AlarmScheduler {
  private const val TAG = "BrulionAndroid"

  /** Android refuses more than 500 concurrent alarms per app; keep room for snoozes. */
  const val MAX_ALARMS = 400

  /** Reminders missed while the phone was off are still shown if at most this old. */
  const val MISSED_WINDOW_MS = 24L * 60 * 60 * 1000

  /**
   * Guards every read-modify-write of [ReminderStore]: `scheduleReminders` runs on Qt's thread,
   * receivers and notification actions on the main thread.
   */
  val lock = Any()

  data class Result(val scheduled: Int, val exact: Boolean)

  fun canScheduleExact(context: Context): Boolean {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) return true
    val alarms = context.getSystemService(Context.ALARM_SERVICE) as? AlarmManager ?: return false
    return alarms.canScheduleExactAlarms()
  }

  /**
   * Replaces the whole set with [incoming]: past reminders are skipped, the last entry of a
   * duplicated id wins, native snoozes still pending are kept unless the app sent the same id
   * again ([keptOf]). Alarms of reminders that are gone are cancelled.
   */
  fun replaceAll(
    context: Context,
    incoming: List<Reminder>,
    now: Long = System.currentTimeMillis()
  ): Result =
    synchronized(lock) {
      val store = ReminderStore(context)
      val previous = store.load()

      val latest = LinkedHashMap<Int, Reminder>()
      for (reminder in incoming) {
        latest[reminder.id] = reminder.copy(snoozed = false)
      }
      val fresh = latest.values.filter { it.at > now }
      val next =
        (fresh + keptOf(previous, fresh, now)).sortedWith(compareBy<Reminder> { it.at }.thenBy { it.id }).take(MAX_ALARMS)

      val nextIds = next.mapTo(HashSet()) { it.id }
      for (reminder in previous) {
        if (reminder.id !in nextIds) cancel(context, reminder.id)
      }
      val exact = canScheduleExact(context)
      for (reminder in next) {
        arm(context, reminder, exact)
      }
      store.save(next, TimeZone.getDefault().id)
      Result(next.size, exact)
    }

  /**
   * What [replaceAll] keeps of the [previous] set next to the app's [fresh] one: the native
   * snoozes still pending whose id the app did not send again (also of a provisional reminder:
   * the app re-arms only future times). A provisional reminder of a widget's quick add itself goes:
   * the app's set has the line quick add wrote by now.
   */
  fun keptOf(previous: List<Reminder>, fresh: List<Reminder>, now: Long): List<Reminder> {
    val freshIds = fresh.mapTo(HashSet()) { it.id }
    return previous.filter { it.snoozed && it.at > now && it.id !in freshIds }
  }

  /**
   * Re-arms the stored reminders (after a reboot, an app update, a clock or time-zone change,
   * or when the exact-alarm permission changed). After a time-zone change reminders keep their
   * local wall-clock time. Reminders that came due meanwhile (phone off) are shown at once as
   * missed when they are at most [MISSED_WINDOW_MS] old, older ones are dropped.
   */
  fun rearmFromStore(context: Context, now: Long = System.currentTimeMillis()) {
    val missed = ArrayList<Reminder>()
    synchronized(lock) {
      val store = ReminderStore(context)
      var items = store.load()
      val zone = TimeZone.getDefault()
      val storedZone = store.zone()
      if (items.isEmpty()) {
        if (storedZone != zone.id) store.save(items, zone.id)
        return
      }
      if (storedZone != null && storedZone != zone.id) {
        val from = TimeZone.getTimeZone(storedZone)
        // Snoozes are "in 10 minutes", not a wall-clock time: they stay where they are.
        items = items.map { if (it.snoozed) it else it.copy(at = WallClock.shift(it.at, from, zone)) }
      }
      val exact = canScheduleExact(context)
      val future = ArrayList<Reminder>()
      for (reminder in items) {
        if (reminder.at > now) {
          arm(context, reminder, exact)
          future.add(reminder)
        } else {
          cancel(context, reminder.id)
          if (now - reminder.at <= MISSED_WINDOW_MS) missed.add(reminder)
        }
      }
      store.save(future, zone.id)
    }
    for (reminder in missed) {
      ReminderNotifier.show(context, reminder, missed = true)
    }
  }

  @SuppressLint("ScheduleExactAlarm", "MissingPermission")
  fun arm(context: Context, reminder: Reminder, exact: Boolean) {
    val alarms = context.getSystemService(Context.ALARM_SERVICE) as? AlarmManager ?: return
    val operation = firePendingIntent(context, reminder) ?: return
    try {
      if (exact && reminder.alarm) {
        val info = AlarmManager.AlarmClockInfo(reminder.at, ReminderNotifier.contentIntent(context, reminder))
        alarms.setAlarmClock(info, operation)
      } else if (exact) {
        alarms.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, reminder.at, operation)
      } else {
        alarms.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, reminder.at, operation)
      }
    } catch (e: SecurityException) {
      // The exact-alarm permission was revoked in the meantime.
      Log.w(TAG, "exact alarm refused, falling back to an inexact one", e)
      alarms.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, reminder.at, operation)
    }
  }

  fun cancel(context: Context, id: Int) {
    val alarms = context.getSystemService(Context.ALARM_SERVICE) as? AlarmManager ?: return
    val existing: PendingIntent? =
      PendingIntent.getBroadcast(
        context,
        id,
        fireIntent(context),
        PendingIntent.FLAG_NO_CREATE or PendingIntent.FLAG_IMMUTABLE
      )
    if (existing != null) {
      alarms.cancel(existing)
      existing.cancel()
    }
  }

  private fun fireIntent(context: Context): Intent =
    Intent(context, ReminderReceiver::class.java).setAction(Actions.FIRE)

  private fun firePendingIntent(context: Context, reminder: Reminder): PendingIntent? =
    PendingIntent.getBroadcast(
      context,
      reminder.id,
      reminder.putInto(fireIntent(context)),
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )
}

/** Local wall-clock arithmetic for reminders ("09:00" stays 09:00 in a new time zone). */
internal object WallClock {
  /** Moves [at] so that it shows the same local time in [to] as it did in [from]. */
  fun shift(at: Long, from: TimeZone, to: TimeZone): Long {
    // Local time of `at` in `from`, as if it were UTC.
    val local = at + from.getOffset(at)
    // Offset of `to` at the result; the second pass settles DST transitions.
    val guess = local - to.getOffset(local)
    return local - to.getOffset(guess)
  }
}
