// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Persisted form of [Reminder] (SharedPreferences) and the IPC argument [ReminderArg]. */
class ReminderJsonTest {
  private val alarm =
    Reminder(
      id = -7,
      title = "Zadzwoń do mamy — ąćęłńóśźż",
      body = "Zażółć gęślą jaźń",
      at = 1_792_000_000_000L,
      alarm = true,
      path = "strony/Łódź.md",
      line = 12,
      ringMinutes = 25
    )

  @Test
  fun roundTripKeepsEveryField() {
    val snoozed = alarm.copy(id = 8, at = alarm.at + 600_000L, snoozed = true, ringMinutes = 1)
    assertEquals(alarm, Reminder.fromJson(JSONObject(alarm.toJson().toString())))
    assertEquals(
      listOf(alarm, snoozed),
      Reminder.listFromJson(Reminder.listToJson(listOf(alarm, snoozed)))
    )
  }

  @Test
  fun remindersStoredByOlderVersionsGetTheDefaultRingTime() {
    val old =
      JSONObject(
        """{"id":3,"title":"Budzik","body":"","at":1792000000000,"alarm":true,"path":"a.md","line":4,"snoozed":false}"""
      )
    val reminder = Reminder.fromJson(old)!!
    assertEquals(RingTimeout.DEFAULT_MINUTES, reminder.ringMinutes)
    assertEquals(Reminder(3, "Budzik", "", 1_792_000_000_000L, true, "a.md", 4), reminder)
  }

  @Test
  fun storedRingTimeIsValidated() {
    fun ring(raw: String) = Reminder.fromJson(JSONObject("""{"id":1,"at":5,"ringMinutes":$raw}"""))!!.ringMinutes
    assertEquals(1, ring("0"))
    assertEquals(60, ring("9999999999999"))
    assertEquals(RingTimeout.DEFAULT_MINUTES, ring("\"dużo\""))
    assertEquals(RingTimeout.DEFAULT_MINUTES, ring("null"))
  }

  @Test
  fun brokenEntriesAreSkipped() {
    assertNull(Reminder.fromJson(JSONObject("""{"id":1}""")))
    assertTrue(Reminder.listFromJson(null).isEmpty())
    assertTrue(Reminder.listFromJson("nie json").isEmpty())
    assertEquals(listOf(2), Reminder.listFromJson("""[{"id":1},{"id":2,"at":5}]""").map { it.id })
  }

  @Test
  fun argumentResolvesTheRingTime() {
    val arg = ReminderArg()
    arg.id = 3
    arg.at = 10
    arg.title = "Łąka"
    arg.alarm = true
    arg.path = "x/ż.md"
    arg.line = 7
    assertEquals(Reminder(3, "Łąka", "", 10, true, "x/ż.md", 7), arg.toReminder())
    assertEquals(RingTimeout.DEFAULT_MINUTES, arg.toReminder().ringMinutes)
    arg.ringMinutes = 15
    assertEquals(15, arg.toReminder().ringMinutes)
    arg.ringMinutes = 600
    assertEquals(RingTimeout.MAX_MINUTES, arg.toReminder().ringMinutes)
  }
}
