// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.annotation.SuppressLint
import android.app.AlarmManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log

/**
 * Stops an alarm nobody reacts to: after [Reminder.ringMinutes] the insistent alarm notification
 * is replaced by a silent "Przegapiony alarm" one (a tap still opens the note) and the alarm
 * screen closes.
 *
 * The deadline is a one-shot AlarmManager alarm delivered to [RingTimeoutReceiver], so it fires
 * while the app and the alarm screen are closed. It counts elapsed real time: a clock or
 * time-zone change does not move it, and a reboot (which also clears the notification) drops
 * it. Every reaction (tap, "Drzemka 10 min", "OK", the alarm screen's buttons, swiping the
 * notification away) goes through [ReminderNotifier.cancel], which cancels it and tells an open
 * alarm screen to close ([stopped]).
 */
internal object RingTimeout {
  /** Ring time of an alarm whose item has no `ringMinutes` (Rust: `DEFAULT_RING_MINUTES`). */
  const val DEFAULT_MINUTES = 10

  /** Longest ring time an item may ask for (Rust: `MAX_RING_MINUTES`). */
  const val MAX_MINUTES = 60

  private const val TAG = "BrulionAndroid"
  private const val MINUTE_MS = 60_000L

  /** Called on the main thread with the id of an alarm that stopped ringing (see [stopped]). */
  private val listeners = ArrayList<(Int) -> Unit>()

  /** Ring time of an item: [raw] minutes clamped to 1..[MAX_MINUTES], the default when absent. */
  fun minutes(raw: Long?): Int = raw?.coerceIn(1L, MAX_MINUTES.toLong())?.toInt() ?: DEFAULT_MINUTES

  /** How long an alarm rings for an item asking for [minutes] (resolved by [minutes]). */
  fun durationMs(minutes: Int): Long = minutes(minutes.toLong()) * MINUTE_MS

  /** When an alarm that starts ringing at [now] stops by itself, ringing [minutes] long. */
  fun deadline(now: Long, minutes: Int): Long = now + durationMs(minutes)

  /**
   * Arms the auto-stop of [reminder]'s alarm, which starts ringing now. Returns the deadline
   * (`SystemClock.elapsedRealtime` ms), also handed to the alarm screen.
   */
  @SuppressLint("ScheduleExactAlarm", "MissingPermission")
  fun arm(context: Context, reminder: Reminder, now: Long = SystemClock.elapsedRealtime()): Long {
    val stopAt = deadline(now, reminder.ringMinutes)
    val alarms = context.getSystemService(Context.ALARM_SERVICE) as? AlarmManager ?: return stopAt
    val operation = pendingIntent(context, reminder)
    try {
      if (AlarmScheduler.canScheduleExact(context)) {
        alarms.setExactAndAllowWhileIdle(AlarmManager.ELAPSED_REALTIME_WAKEUP, stopAt, operation)
      } else {
        alarms.setAndAllowWhileIdle(AlarmManager.ELAPSED_REALTIME_WAKEUP, stopAt, operation)
      }
    } catch (e: SecurityException) {
      // The exact-alarm permission was revoked in the meantime.
      Log.w(TAG, "exact ring timeout refused, falling back to an inexact one", e)
      alarms.setAndAllowWhileIdle(AlarmManager.ELAPSED_REALTIME_WAKEUP, stopAt, operation)
    }
    return stopAt
  }

  /** Cancels the pending auto-stop of alarm [id] (no-op when there is none). */
  fun cancel(context: Context, id: Int) {
    val alarms = context.getSystemService(Context.ALARM_SERVICE) as? AlarmManager ?: return
    val existing: PendingIntent? =
      PendingIntent.getBroadcast(
        context,
        id,
        timeoutIntent(context),
        PendingIntent.FLAG_NO_CREATE or PendingIntent.FLAG_IMMUTABLE
      )
    if (existing != null) {
      alarms.cancel(existing)
      existing.cancel()
    }
  }

  /**
   * The ring time of [reminder] is over: when its alarm still rings, replace it with the silent
   * missed-alarm notification; close the alarm screen either way. Idempotent (the alarm screen
   * and the AlarmManager deadline may both get here).
   */
  fun expire(context: Context, reminder: Reminder) {
    if (ReminderNotifier.isRinging(context, reminder.id)) {
      // Goes through ReminderNotifier.cancel, which also closes the alarm screen.
      ReminderNotifier.showSilencedAlarm(context, reminder)
    } else {
      cancel(context, reminder.id)
      stopped(reminder.id)
    }
  }

  /**
   * Alarm [id] stopped ringing (a reaction, or its ring time is over): an alarm screen showing
   * it closes, unless one of its own buttons did this. Runs the listeners on the main thread.
   */
  fun stopped(id: Int) {
    if (Looper.myLooper() != Looper.getMainLooper()) {
      Handler(Looper.getMainLooper()).post { stopped(id) }
      return
    }
    for (listener in listeners.toList()) {
      listener(id)
    }
  }

  fun addListener(listener: (Int) -> Unit) {
    listeners.add(listener)
  }

  fun removeListener(listener: (Int) -> Unit) {
    listeners.remove(listener)
  }

  private fun timeoutIntent(context: Context): Intent =
    Intent(context, RingTimeoutReceiver::class.java).setAction(Actions.RING_TIMEOUT)

  // Request code = id like the fire and button intents; the action keeps them apart.
  private fun pendingIntent(context: Context, reminder: Reminder): PendingIntent =
    PendingIntent.getBroadcast(
      context,
      reminder.id,
      reminder.putInto(timeoutIntent(context)),
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )
}

/** The ring time of an alarm is over (AlarmManager, armed by [RingTimeout.arm]). */
class RingTimeoutReceiver : BroadcastReceiver() {
  override fun onReceive(context: Context, intent: Intent) {
    if (intent.action != Actions.RING_TIMEOUT) return
    val reminder = Reminder.fromIntent(intent) ?: return
    RingTimeout.expire(context, reminder)
  }
}
