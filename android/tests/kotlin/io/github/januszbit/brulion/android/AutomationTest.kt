// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.LocalDateTime
import java.time.ZoneId

/**
 * Automation's pure parts (Automation.kt, docs/ANDROID.md "Automation"): extras added to an address
 * (replacing its own parameter, Polish text form-encoded as the C++ core reads it), the action and
 * a parameter read back, which requests need the app's window, and the provisional reminder of a
 * background write's result (the JSON of src/app/background/automationrun.h).
 */
class AutomationTest {
  @Test
  fun paramsAreAddedAndReplaced() {
    val uri = Automation.withParams("brulion://append?page=Zakupy&text=stary", linkedMapOf("text" to "Mleko 2% i żółw", "key" to "abc"))
    assertEquals("brulion://append?page=Zakupy&text=Mleko+2%25+i+%C5%BC%C3%B3%C5%82w&key=abc", uri)
    assertEquals("Mleko 2% i żółw", Automation.paramOf(uri!!, "text"))
    assertEquals("Zakupy", Automation.paramOf(uri, "page"))
    assertNull(Automation.paramOf(uri, "heading"))
    // vault and notebook are one parameter: an extra replaces the other spelling.
    assertEquals(
      "brulion://open?page=A&vault=Praca",
      Automation.withParams("brulion://open?notebook=Dom&page=A", mapOf("vault" to "Praca"))
    )
    assertEquals("brulion://daily", Automation.withParams("brulion://daily", emptyMap()))
    assertEquals("brulion://capture?text=C%2B%2B", Automation.withParams("brulion://capture", mapOf("text" to "C++")))
  }

  @Test
  fun actionAndParameters() {
    assertEquals("append", Automation.actionOf("brulion://append?text=a"))
    assertEquals("open", Automation.actionOf("brulion:open/Projekty/Dom"))
    assertEquals("task", Automation.actionOf("brulion://x-callback-url/task?text=a"))
    assertEquals("daily", Automation.actionOf("web+brulion://daily"))
    assertEquals("", Automation.actionOf("brulion://"))
    assertEquals("Zadzwoń jutro", Automation.paramOf("brulion://task?text=Zadzwo%C5%84+jutro", "text"))
    assertEquals("50%", Automation.paramOf("brulion://task?text=50%", "text")) // malformed: as it is
    assertTrue(Automation.isBrulionUri(" BRULION://daily"))
    assertTrue(Automation.isBrulionUri("web+brulion:daily"))
    assertFalse(Automation.isBrulionUri("https://example.org"))
    assertFalse(Automation.isBrulionUri(null))
  }

  @Test
  fun whatNeedsTheWindow() {
    assertTrue(Automation.needsWindow("brulion://open?page=Zakupy&key=k"))
    assertTrue(Automation.needsWindow("brulion://daily?key=k"))
    assertTrue(Automation.needsWindow("brulion://view?name=calendar&key=k"))
    assertTrue(Automation.needsWindow("brulion://capture?key=k"))
    assertTrue(Automation.needsWindow("brulion://run?extension=a&command=b&key=k"))
    assertTrue(Automation.needsWindow("brulion://append?text=a"))
    assertTrue(Automation.needsWindow("brulion://append?text=a&key=+"))
    assertFalse(Automation.needsWindow("brulion://append?text=a&key=k"))
    assertFalse(Automation.needsWindow("brulion://capture?text=Mleko&key=k"))
    assertFalse(Automation.needsWindow("brulion://task?text=a&key=k"))
  }

  @Test
  fun taskTitles() {
    assertEquals("Zadzwonić do mamy", Automation.taskTitle("- [ ] Zadzwonić do mamy ⏰ 2026-11-21 18:00"))
    assertEquals("Pociąg", Automation.taskTitle("- [ ] Pociąg ⏰! 2026-11-21 05:40"))
    assertEquals("Raport", Automation.taskTitle("- [ ] Raport 📅 2026-11-27 ⏰ 2026-11-26 09:00"))
    assertEquals("Notatka", Automation.taskTitle("- Notatka\n  druga linia"))
  }

  @Test
  fun provisionalReminder() {
    val now = LocalDateTime.of(2026, 11, 20, 10, 0)
    val result = JSONObject()
      .put("ok", true)
      .put("keyed", true)
      .put("path", "journals/2026-11-20.md")
      .put("line", 2)
      .put("block", "- [ ] Zadzwonić do mamy ⏰ 2026-11-21 18:00")
      .put("reminder", "2026-11-21 18:00")
      .put("vault", "abc123")
      .put("name", "Notatki")
      .put("id", "r1")
    val reminder = Automation.provisionalReminder(result, now, ZoneId.of("Europe/Warsaw"))
    assertNotNull(reminder)
    reminder!!
    assertEquals("Zadzwonić do mamy", reminder.title)
    assertEquals("Notatki", reminder.body)
    assertEquals("journals/2026-11-20.md", reminder.path)
    assertEquals(1, reminder.line) // the result's lines count from 1, a reminder's from 0
    assertEquals("abc123", reminder.vault)
    assertTrue(reminder.provisional)
    assertTrue(reminder.task)
    assertFalse(reminder.alarm)
    assertEquals("2026-11-21T18:00", reminder.local)
    assertTrue(reminder.key.startsWith(QuickAdd.PROVISIONAL_KEY))
    assertTrue(reminder.id < 0)
    // Past, without a time or without a place: none.
    assertNull(Automation.provisionalReminder(JSONObject(result.toString()).put("reminder", "2026-11-19 18:00"), now))
    assertNull(Automation.provisionalReminder(JSONObject(result.toString()).put("reminder", ""), now))
    assertNull(Automation.provisionalReminder(JSONObject(result.toString()).put("path", ""), now))
  }
}
