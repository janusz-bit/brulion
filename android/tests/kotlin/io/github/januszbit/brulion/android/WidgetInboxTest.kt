// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Quick-add entries waiting for the app (WidgetInbox.kt; read by widgets::widgetActions in C++). */
class WidgetInboxTest {
  private fun action(id: String, vault: String = "", text: String = "Zadanie $id") =
    WidgetAction(id, WidgetAction.KIND_TASK, text, "2026-10-28", "07:15", true, vault, "2026-10-27T22:10")

  @Test
  fun jsonRoundTripIsWhatCppReads() {
    val a = action("ż-1", "notatnik", "Kupić żółty ser — ąćęłńóśźż")
    val json = a.toJson()
    assertEquals("task", json.getString("kind"))
    assertEquals("2026-10-28", json.getString("date"))
    assertEquals("07:15", json.getString("time"))
    assertTrue(json.getBoolean("alarm"))
    assertEquals(a, WidgetAction.fromJson(json))
    assertNull(WidgetAction.fromJson(JSONObject().put("kind", "task").put("text", "bez id")))
    assertNull(WidgetAction.fromJson(JSONObject().put("id", "x").put("kind", "zadanie")))
    // No kind: a task (as the C++ reads it).
    assertEquals(WidgetAction.KIND_TASK, WidgetAction.fromJson(JSONObject().put("id", "y"))!!.kind)
  }

  @Test
  fun appendedReplacesTheSameIdAndKeepsTheNewest() {
    var raw: String? = null
    raw = WidgetInbox.appended(raw, action("a"))
    raw = WidgetInbox.appended(raw, action("b"))
    raw = WidgetInbox.appended(raw, action("a", text = "poprawione"))
    val items = WidgetInbox.parse(raw)
    assertEquals(listOf("b", "a"), items.map { it.id })
    assertEquals("poprawione", items.last().text)
    var many: String? = null
    for (i in 1..5) many = WidgetInbox.appended(many, action("$i"), max = 3)
    assertEquals(listOf("3", "4", "5"), WidgetInbox.parse(many).map { it.id })
  }

  /** Entries the app took but could not write go back (as widgets::widgetActionToJson writes them). */
  @Test
  fun requeuedEntriesWaitForTheNextTry() {
    val queue = JSONArray().put(action("1", "dom").toJson()).put(action("2", "praca").toJson()).toString()
    val (taken, kept) = WidgetInbox.split(queue, "dom")
    assertEquals(listOf("1"), taken.map { it.id })
    // The C++'s JSON of the entry it could not write.
    val fromApp =
      "[{\"id\":\"1\",\"kind\":\"task\",\"text\":\"Zadanie 1\",\"date\":\"2026-10-28\",\"time\":\"07:15\"," +
        "\"alarm\":true,\"vault\":\"dom\",\"added\":\"2026-10-27T22:10\"}]"
    val requeued = WidgetInbox.parse(fromApp)
    assertEquals(taken, requeued)
    val raw = WidgetInbox.appendedAll(JSONArray().put(kept.single().toJson()).toString(), requeued)
    assertEquals(listOf("2", "1"), WidgetInbox.parse(raw).map { it.id })
    assertEquals(listOf("1"), WidgetInbox.split(raw, "dom").first.map { it.id })
    // Twice: still once.
    assertEquals(2, WidgetInbox.parse(WidgetInbox.appendedAll(raw, requeued)).size)
    assertEquals(WidgetInbox.parse(raw), WidgetInbox.parse(WidgetInbox.appendedAll(null, WidgetInbox.parse(raw))))
  }

  @Test
  fun eachNotebookTakesItsOwn() {
    val raw =
      JSONArray()
        .put(action("1", "dom").toJson())
        .put(action("2", "praca").toJson())
        .put(action("3").toJson())
        .put("nie obiekt")
        .put(JSONObject().put("id", ""))
        .toString()
    val (taken, kept) = WidgetInbox.split(raw, "dom")
    assertEquals(listOf("1", "3"), taken.map { it.id })
    assertEquals(listOf("2"), kept.map { it.id })
    assertEquals(3, WidgetInbox.split(raw, "").first.size)
    assertTrue(WidgetInbox.parse("zepsute").isEmpty())
    assertTrue(WidgetInbox.parse(null).isEmpty())
  }
}
