// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.LocalDate

/** The queue of "Zrobione" taps C++ takes (DoneQueue.kt; parsed by androidDoneRequests in C++). */
class DoneQueueTest {
  private val reminder =
    Reminder(
      id = 77,
      title = "Kupić żółty ser — ąćęłńóśźż",
      body = "Zakupy",
      at = 1_792_000_000_000L,
      alarm = false,
      path = "dziennik/2026-10-27.md",
      line = 4,
      key = "0123456789abcdef",
      local = "2026-10-27T09:00",
      task = true,
      vault = "notatnik-dom"
    )

  private fun request(key: String, day: String = "2026-10-27", vault: String = "") =
    DoneRequest(key, "2026-10-27T09:00", "Łódź.md", 2, "Zadanie $key", day, vault)

  @Test
  fun requestOfAReminderKeepsTheOccurrenceAndTheDayOfTheTap() {
    // A snoozed copy has another `at`: the occurrence is named by key and local time.
    val snoozed = reminder.copy(at = reminder.at + 600_000L, snoozed = true)
    val done = DoneRequest.of(snoozed, LocalDate.of(2026, 10, 28))
    assertEquals(
      DoneRequest(
        "0123456789abcdef",
        "2026-10-27T09:00",
        "dziennik/2026-10-27.md",
        4,
        "Kupić żółty ser — ąćęłńóśźż",
        "2026-10-28",
        "notatnik-dom"
      ),
      done
    )
    val json = done.toJson()
    assertEquals("2026-10-28", json.getString("day"))
    assertEquals("notatnik-dom", json.getString("vault"))
    assertEquals(done, DoneRequest.fromJson(JSONObject(json.toString())))
  }

  @Test
  fun appendKeepsOrderAndReplacesTheSameOccurrence() {
    var raw: String? = null
    raw = DoneQueue.appended(raw, request("a"))
    raw = DoneQueue.appended(raw, request("b"))
    // The notification's button and then the alarm screen's: one request, the later one.
    raw = DoneQueue.appended(raw, request("a", day = "2026-10-28"))
    val items = DoneQueue.parse(raw)
    assertEquals(listOf("b", "a"), items.map { it.key })
    assertEquals("2026-10-28", items.last().day)
  }

  @Test
  fun takeHandsOverOnlyTheReadyNotebooksTaps() {
    var raw: String? = null
    raw = DoneQueue.appended(raw, request("dom", vault = "notatnik-dom"))
    raw = DoneQueue.appended(raw, request("praca", vault = "notatnik-praca"))
    // Queued from a reminder armed by an older version: no notebook, any one takes it.
    raw = DoneQueue.appended(raw, request("stary"))
    // The same occurrence in two notebooks (a shared template): two requests.
    raw = DoneQueue.appended(raw, request("dom", vault = "notatnik-praca"))

    val (taken, kept) = DoneQueue.split(raw, "notatnik-dom")
    assertEquals(listOf("dom", "stary"), taken.map { it.key })
    assertEquals(listOf("notatnik-dom", ""), taken.map { it.vault })
    // The other notebook's taps wait for it.
    assertEquals(listOf("praca", "dom"), kept.map { it.key })
    assertTrue(kept.all { it.vault == "notatnik-praca" })
    // No notebook named: everything (the self-test).
    assertEquals(4, DoneQueue.split(raw, "").first.size)
    assertTrue(DoneQueue.split(null, "notatnik-dom").first.isEmpty())
  }

  @Test
  fun queueIsBounded() {
    var raw: String? = null
    for (i in 1..7) raw = DoneQueue.appended(raw, request("k$i"), max = 5)
    assertEquals(listOf("k3", "k4", "k5", "k6", "k7"), DoneQueue.parse(raw).map { it.key })
    assertEquals(1, DoneQueue.parse(DoneQueue.appended(raw, request("x"), max = 0)).size)
  }

  @Test
  fun brokenEntriesAreSkipped() {
    assertTrue(DoneQueue.parse(null).isEmpty())
    assertTrue(DoneQueue.parse("").isEmpty())
    assertTrue(DoneQueue.parse("nie json").isEmpty())
    assertNull(DoneRequest.fromJson(JSONObject("""{"key":"","local":"2026-10-27T09:00","path":"a.md"}""")))
    assertNull(DoneRequest.fromJson(JSONObject("""{"key":"k","path":"a.md"}""")))
    assertNull(DoneRequest.fromJson(JSONObject("""{"key":"k","local":"2026-10-27T09:00"}""")))
    val raw =
      """[7, null, {"key":"k"}, {"key":"ok","local":"2026-10-27T09:00","path":"Źródła.md","line":"x","day":"2026-10-27"}]"""
    val items = DoneQueue.parse(raw)
    assertEquals(listOf("ok"), items.map { it.key })
    assertEquals(0, items.single().line)
    assertEquals("", items.single().title)
    // Appending to a broken queue starts it again.
    assertEquals(1, JSONArray(DoneQueue.appended("{zepsute", request("n"))).length())
  }
}
