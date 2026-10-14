// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** The reminder JSON: persisted form (SharedPreferences) and what C++ sends (scheduleReminders). */
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
    assertEquals(15, ring("15.0"))
    assertEquals(RingTimeout.DEFAULT_MINUTES, ring("\"dużo\""))
    assertEquals(RingTimeout.DEFAULT_MINUTES, ring("null"))
  }

  @Test
  fun brokenEntriesAreSkipped() {
    assertNull(Reminder.fromJson(JSONObject("""{"id":1}""")))
    assertNull(Reminder.fromJson(JSONObject("""{"id":"x","at":5}""")))
    assertTrue(Reminder.listFromJson(null as String?).isEmpty())
    assertTrue(Reminder.listFromJson("nie json").isEmpty())
    assertEquals(listOf(2), Reminder.listFromJson("""[{"id":1},{"id":2,"at":5},7,null]""").map { it.id })
  }

  /** What `AndroidBridge::scheduleReminders` sends: QJsonDocument may write numbers as doubles. */
  @Test
  fun itemsFromCppAreParsed() {
    val items =
      JSONArray(
        """[
          {"id":42,"title":"Spotkanie","body":"Sala 3","at":1.792e12,"alarm":false,"path":"dziennik/2026-10-14.md","line":3},
          {"id":43.0,"title":"Pobudka","body":"","at":1792003600000,"alarm":true,"ringMinutes":5,"path":"Ąę.md","line":0}
        ]"""
      )
    val parsed = Reminder.listFromJson(items)
    assertEquals(
      listOf(
        Reminder(42, "Spotkanie", "Sala 3", 1_792_000_000_000L, false, "dziennik/2026-10-14.md", 3),
        Reminder(43, "Pobudka", "", 1_792_003_600_000L, true, "Ąę.md", 0, ringMinutes = 5)
      ),
      parsed
    )
    // A ring time out of range is clamped, not rejected.
    val loud = Reminder.listFromJson(JSONArray("""[{"id":1,"at":5,"alarm":true,"ringMinutes":600}]"""))
    assertEquals(RingTimeout.MAX_MINUTES, loud.single().ringMinutes)
  }
}
