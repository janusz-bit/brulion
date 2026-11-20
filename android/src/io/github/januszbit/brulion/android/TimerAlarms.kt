// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.content.Context
import android.content.SharedPreferences

/**
 * The ends of extensions' timers (`b.timer`: the gym's rest between sets, a focus timer), armed
 * with AlarmManager apart from the reminders — so that the notification comes when Brulion is
 * closed or its process is gone: [AlarmScheduler.replaceAll] never touches them (the app's
 * reminder set and the background check's replace only reminders). One alarm per timer id (the
 * app's id of the extension's timer in its notebook): arming the id again moves it, [cancel] (the
 * timer stopped) takes it away. Persisted in their own preferences ([TimerStore]) and re-armed
 * after a reboot or an update ([rearm]); a timer that ended while the phone was off is dropped
 * — the moment it was for is gone. When one comes due, [ReminderReceiver] forgets it ([fired]) and
 * [ReminderNotifier] shows it as a reminder without "Snooze" and "Done".
 */
internal object TimerAlarms {
  /** At most this many timers wait at once (one per extension of a notebook, a few notebooks). */
  const val MAX_TIMERS = 32

  /**
   * Arms [timer] (marked as a timer; replacing the one of its id). Returns whether the alarm is
   * exact — false: the inexact fallback without the exact-alarm permission (or a time already
   * past: nothing armed).
   */
  fun arm(context: Context, timer: Reminder, now: Long = System.currentTimeMillis()): Boolean =
    synchronized(AlarmScheduler.lock) {
      val store = TimerStore(context)
      val armed = withTimer(store.load(), timer.copy(timer = true, snoozed = false), now)
      store.save(armed)
      if (armed.none { it.id == timer.id }) {
        AlarmScheduler.cancel(context, timer.id)
        return false
      }
      val exact = AlarmScheduler.canScheduleExact(context)
      AlarmScheduler.arm(context, timer.copy(timer = true), exact)
      exact
    }

  /** Cancels the timer [id]: its alarm, and its notification when it shows already. */
  fun cancel(context: Context, id: Int) {
    synchronized(AlarmScheduler.lock) {
      val store = TimerStore(context)
      store.save(store.load().filterNot { it.id == id })
      AlarmScheduler.cancel(context, id)
    }
  }

  /** The timer [id] came due: forgotten (its notification shows). */
  fun fired(context: Context, id: Int, now: Long) {
    synchronized(AlarmScheduler.lock) {
      val store = TimerStore(context)
      val items = store.load()
      val kept = items.filterNot { it.id == id && it.at <= now + FIRE_TOLERANCE_MS }
      if (kept.size != items.size) store.save(kept)
    }
  }

  /** Arms the stored timers again (a reboot, an update, a clock change); past ones are dropped. */
  fun rearm(context: Context, now: Long = System.currentTimeMillis()) {
    synchronized(AlarmScheduler.lock) {
      val store = TimerStore(context)
      val items = store.load()
      val future = pending(items, now)
      for (timer in items) {
        if (timer !in future) AlarmScheduler.cancel(context, timer.id)
      }
      val exact = AlarmScheduler.canScheduleExact(context)
      for (timer in future) AlarmScheduler.arm(context, timer, exact)
      if (future.size != items.size) store.save(future)
    }
  }

  /** The timers still to come at [now], soonest first. */
  fun pending(items: List<Reminder>, now: Long): List<Reminder> =
    items.filter { it.at > now }.sortedWith(compareBy<Reminder> { it.at }.thenBy { it.id })

  /** [items] with [timer] in place of the one of its id, past ones dropped, at most [MAX_TIMERS]. */
  fun withTimer(items: List<Reminder>, timer: Reminder, now: Long): List<Reminder> =
    pending(items.filterNot { it.id == timer.id } + timer, now).take(MAX_TIMERS)

  private const val FIRE_TOLERANCE_MS = 60_000L
}

/** The armed timers, persisted for [TimerAlarms.rearm] (their times are moments: no time zone). */
internal class TimerStore(context: Context) {
  private val prefs: SharedPreferences =
    context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

  fun load(): List<Reminder> = Reminder.listFromJson(prefs.getString(KEY_ITEMS, null))

  fun save(items: List<Reminder>) {
    prefs.edit().putString(KEY_ITEMS, Reminder.listToJson(items)).apply()
  }

  private companion object {
    const val PREFS_NAME = "io.github.januszbit.brulion.android.timers"
    const val KEY_ITEMS = "items"
  }
}
