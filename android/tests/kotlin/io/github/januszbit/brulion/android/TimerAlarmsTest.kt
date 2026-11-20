// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** The ends of extensions' timers (b.timer) armed apart from the reminders: what stays armed. */
class TimerAlarmsTest {
  private val now = 1_792_000_000_000L

  private fun timer(id: Int, inMs: Long, title: String = "Przerwa — czas na serię") =
    Reminder(id = id, title = title, body = "Zażółć gęślą jaźń", at = now + inMs, alarm = false, path = "", line = 0, timer = true)

  @Test
  fun theTimerFlagRoundTripsAndDefaultsToFalse() {
    val rest = timer(2_000_000_002, 120_000L)
    assertEquals(rest, Reminder.fromJson(JSONObject(rest.toJson().toString())))
    assertTrue(Reminder.fromJson(JSONObject(rest.toJson().toString()))!!.timer)
    // what C++ sent before timers existed, and a reminder: no timer
    val old = JSONObject().put("id", 5).put("at", now)
    assertFalse(Reminder.fromJson(old)!!.timer)
    assertFalse(rest.canComplete) // no "Done" on a timer's end
  }

  @Test
  fun armingAnIdAgainMovesItsTimer() {
    val first = timer(7, 60_000L)
    val moved = timer(7, 180_000L)
    val other = timer(8, 90_000L, "Skupienie")
    val armed = TimerAlarms.withTimer(TimerAlarms.withTimer(listOf(first), other, now), moved, now)
    assertEquals(listOf(other, moved), armed) // soonest first, one per id
  }

  @Test
  fun pastTimersAreDroppedAndTheCountIsCapped() {
    val past = timer(1, -1_000L)
    assertEquals(emptyList<Reminder>(), TimerAlarms.withTimer(emptyList(), past, now))
    assertEquals(emptyList<Reminder>(), TimerAlarms.pending(listOf(timer(2, 0L)), now)) // its moment is now: due
    val many = (1..(TimerAlarms.MAX_TIMERS + 5)).map { timer(it, it * 1_000L) }
    var armed = emptyList<Reminder>()
    for (t in many) armed = TimerAlarms.withTimer(armed, t, now)
    assertEquals(TimerAlarms.MAX_TIMERS, armed.size)
    assertEquals(many.take(TimerAlarms.MAX_TIMERS), armed) // the soonest stay
  }

  @Test
  fun aRebootKeepsOnlyTimersStillToCome() {
    val items = listOf(timer(3, 60_000L), timer(4, -60_000L), timer(5, 30_000L))
    assertEquals(listOf(items[2], items[0]), TimerAlarms.pending(items, now))
  }
}
