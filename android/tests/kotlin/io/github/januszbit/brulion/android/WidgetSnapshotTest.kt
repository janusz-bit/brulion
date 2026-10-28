// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.time.LocalDate
import java.time.LocalDateTime
import java.time.ZoneId

/**
 * The widgets' snapshot (WidgetSnapshot.kt) as the C++ builds it: the golden file of
 * tests/app/widgets/tst_widgets.cpp (tests/data/widgets/snapshot-v1.json), the sections of a later
 * day (refile-cases.json, the C++ rule), the rows the widgets list, quick add's optimistic row.
 */
class WidgetSnapshotTest {
  private fun data(name: String): String {
    val dir = System.getProperty("brulion.testData") ?: error("brulion.testData is not set (unit-tests.gradle)")
    return File(dir, "widgets/$name").readText(Charsets.UTF_8)
  }

  private val golden by lazy { WidgetSnapshot.parse(data("snapshot-v1.json")) }
  private val day = LocalDate.of(2026, 10, 27)

  @Test
  fun parsesTheGoldenSnapshot() {
    val s = golden
    assertEquals(day, s.today)
    assertEquals(1, s.weekStart)
    assertEquals("pl", s.locale)
    assertEquals(VaultInfo("brulion-test", "Brulion", "/storage/emulated/0/Brulion"), s.vault)
    assertEquals(10, s.tasks.size)
    val apples = s.tasks.first { it.text == "Kupić żółte jabłka" }
    assertEquals("journals/2026-10-27.md", apples.path)
    assertEquals(3, apples.line)
    assertEquals("2026-10-27 09:00", apples.reminder)
    assertEquals("09:00", apples.timeOn(day))
    assertTrue(s.tasks.first { it.text == "Pobudka na pociąg" }.alarm)
    assertEquals(listOf(PageRef("Projekty/Dom.md", "Dom"), PageRef("Zakupy ąę.md", "Zakupy ąę")), s.favorites)
    assertEquals(CaptureDay(LocalDate.of(2026, 10, 28), "journals/2026-10-28.md", false, "# środa, 28 października\n\n- "), s.captureOn(day.plusDays(1)))
    assertNull(s.captureOn(day.plusDays(2)))
    // An existing note carries its template text too: deleted before quick add writes, it starts as the app's would.
    assertEquals(CaptureDay(day, "journals/2026-10-27.md", true, "# wtorek, 27 października\n\n- "), s.captureOn(day))
    assertTrue(s.remindersOn)
    assertEquals(LocalDate.of(2026, 10, 19), s.calendarFrom)
    assertTrue(s.covers(day) && s.covers(LocalDate.of(2026, 12, 22)) && !s.covers(LocalDate.of(2026, 12, 23)))
    val today = s.day(day)!!
    assertEquals("journals/2026-10-27.md", today.journal)
    assertEquals("09:00", today.items.first().time)
    assertTrue("<script> stays text", s.tasks.any { it.text.startsWith("<script>") })
  }

  /** On the snapshot's own day the mirror files every task where the C++ did. */
  @Test
  fun sectionsOfTheSnapshotsDayAreTheApps() {
    for (task in golden.tasks) assertEquals(task.text, task.section, task.sectionOn(day, golden.weekStart))
  }

  @Test
  fun refileCasesAreTheApps() {
    val cases = JSONObject(data("refile-cases.json")).getJSONArray("cases")
    assertTrue(cases.length() >= 20)
    for (i in 0 until cases.length()) {
      val c = cases.getJSONObject(i)
      val today = LocalDate.parse(c.getString("today"))
      val reminder =
        c.optJSONArray("reminders")?.let { array ->
          val next = TaskSections.nextReminder((0 until array.length()).map { TaskReminder(array.getString(it), false) }, today)
          if (c.has("next")) assertEquals(c.getString("name"), c.getString("next"), next?.at ?: "")
          next?.at ?: ""
        } ?: c.getString("reminder")
      val section =
        TaskSections.sectionOf(
          "todo",
          WidgetTask.dateOf(c.getString("due")),
          WidgetTask.dateOf(c.getString("scheduled")),
          WidgetTask.dateOf(c.getString("start")),
          WidgetTask.dateOf(reminder),
          today,
          c.getInt("weekStart")
        )
      assertEquals(c.getString("name"), c.getString("section"), section)
    }
  }

  /** A block with several reminders goes by its next one on a later day (TasksView's choice). */
  @Test
  fun aLaterDayTakesTheNextReminder() {
    val task =
      WidgetTask.fromJson(
        JSONObject(
          "{\"path\": \"Ogród.md\", \"line\": 0, \"text\": \"Podlać storczyki\", \"status\": \"todo\"," +
            " \"reminder\": \"2026-10-27 09:00\", \"alarm\": false, \"section\": \"today\"," +
            " \"reminders\": [{\"at\": \"2026-10-27 09:00\", \"alarm\": false}, {\"at\": \"2026-10-30 07:15\", \"alarm\": true}, {\"at\": \"zła\"}]}"
        )
      )!!
    assertEquals(2, task.reminders.size)
    assertEquals(TaskSections.TODAY, task.sectionOn(day, 1))
    assertEquals("09:00", task.timeOn(day))
    val friday = LocalDate.of(2026, 10, 30)
    assertEquals(TaskSections.THIS_WEEK, task.sectionOn(day.plusDays(1), 1))
    assertEquals(TaskSections.TOMORROW, task.sectionOn(friday.minusDays(1), 1))
    assertEquals(TaskSections.TODAY, task.sectionOn(friday, 1))
    assertEquals("⏰! 07:15", taskDetail(task, TaskSections.TODAY, friday, "pl"))
    assertEquals("", task.timeOn(day.plusDays(1)))
    // The JSON keeps them (quick add rewrites the stored snapshot).
    assertEquals(task, WidgetTask.fromJson(task.toJson()))
    // A task of an older snapshot without "reminders" goes by its one reminder.
    val single = task.copy(reminders = emptyList())
    assertEquals(TaskSections.TODAY, single.sectionOn(day.plusDays(1), 1))
  }

  @Test
  fun aDayLaterTasksMoveOn() {
    val sections = listOf(TaskSections.OVERDUE, TaskSections.TODAY, TaskSections.TOMORROW)
    val today = golden.tasksOn(day, sections).map { it.first to it.second.text }
    assertEquals(TaskSections.OVERDUE to "Zadzwonić do hydraulika #dom", today.first())
    assertEquals(1, today.count { it.first == TaskSections.OVERDUE })
    val next = golden.tasksOn(day.plusDays(1), sections).associate { it.second.text to it.first }
    // Due yesterday (the 27th) is overdue now, tomorrow's is today's, a reminder of the 27th stays today.
    assertEquals(TaskSections.OVERDUE, next["Spotkanie z Łucją pilne #praca"])
    assertEquals(TaskSections.TODAY, next["Zapłacić rachunki"])
    assertEquals(TaskSections.TODAY, next["Pobudka na pociąg"])
    assertEquals(TaskSections.TOMORROW, next["W trakcie"])
    assertEquals(TaskSections.TODAY, next["Odebrać paczkę"])
    assertEquals(TaskSections.TODAY, next["Kupić żółte jabłka"])
    // Sections in order.
    val order = golden.tasksOn(day.plusDays(1), sections).map { TaskSections.ORDER.indexOf(it.first) }
    assertEquals(order.sorted(), order)
  }

  @Test
  fun brokenOrOtherSnapshotsAreEmpty() {
    for (raw in listOf(null, "", "nie json", "[]", "{\"version\": 2, \"tasks\": []}", "{\"tasks\": 5}")) {
      assertSame(raw, WidgetSnapshot.EMPTY, WidgetSnapshot.parse(raw))
    }
    val odd = WidgetSnapshot.parse("{\"version\": 1, \"vault\": {\"root\": \"\"}, \"tasks\": [{\"text\": \"bez ścieżki\"}, 7], \"weekStart\": 9}")
    assertFalse(odd.hasVault)
    assertTrue(odd.tasks.isEmpty())
    assertEquals(1, odd.weekStart)
    assertTrue(odd.tasksOn(day, TaskSections.ORDER).isEmpty())
    assertFalse(WidgetSnapshot.EMPTY.hasVault)
  }

  /** Before the app published a snapshot the widgets speak the language the app starts in. */
  @Test
  fun withoutASnapshotThePhonesLanguage() {
    assertEquals("en", WidgetSnapshot.localeOf("", "en"))
    assertEquals("pl", WidgetSnapshot.localeOf("", "pl"))
    assertEquals("pl", WidgetSnapshot.localeOf("", "de"))
    assertEquals("pl", WidgetSnapshot.localeOf("pl", "en"))
    assertEquals("en", WidgetSnapshot.localeOf("en", "pl"))
    assertEquals("pl", WidgetSnapshot.localeOf("fr", "pl"))
    assertEquals(WidgetSnapshot.localeOf(""), WidgetSnapshot.EMPTY.locale)
    assertEquals("pl", golden.locale)
  }

  @Test
  fun monthGrids() {
    // October 2026 starts on a Thursday.
    val monday = WidgetSnapshot.monthGrid(2026, 10, 1)
    assertEquals(42, monday.size)
    assertEquals(LocalDate.of(2026, 9, 28), monday.first())
    assertEquals(LocalDate.of(2026, 11, 8), monday.last())
    assertEquals(LocalDate.of(2026, 9, 27), WidgetSnapshot.monthGrid(2026, 10, 7).first())
    // February of a leap year starting on a Monday (2027 is not leap; 2028 is, starting on Tuesday).
    val february = WidgetSnapshot.monthGrid(2028, 2, 1)
    assertEquals(LocalDate.of(2028, 1, 31), february.first())
    assertTrue(february.contains(LocalDate.of(2028, 2, 29)))
    // The grid does not care about the DST change (25 October 2026 in Poland).
    assertEquals(LocalDate.of(2026, 10, 26), monday[28])
  }

  @Test
  fun rowsOfTheTasksWidget() {
    val rows = taskRows(golden, day, tall = false) { it }
    assertEquals(WidgetRow.Header("overdue · 1", true), rows[0])
    assertEquals("Zadzwonić do hydraulika #dom", (rows[1] as WidgetRow.Task).task.text)
    assertEquals(WidgetRow.Header("today · 5", false), rows[2])
    assertEquals(8, rows.size)
    val tall = taskRows(golden, day, tall = true) { it }
    assertEquals(WidgetRow.Header("tomorrow · 2", false), tall[8])
    assertEquals(11, tall.size)
    val apples = golden.tasks.first { it.text == "Kupić żółte jabłka" }
    assertEquals("⏰ 09:00", taskDetail(apples, TaskSections.TODAY, day, "pl"))
    val plumber = golden.tasks.first { it.text.startsWith("Zadzwonić") }
    assertEquals("wczoraj", taskDetail(plumber, TaskSections.OVERDUE, day, "pl"))
    assertEquals("pn 26 paź", taskDetail(plumber, TaskSections.OVERDUE, day.plusDays(2), "pl"))
    assertEquals("Mon, Oct 26", taskDetail(plumber, TaskSections.OVERDUE, day.plusDays(2), "en"))
    val alarm = golden.tasks.first { it.text == "Pobudka na pociąg" }
    assertEquals("⏰! 06:30 · Dom", taskDetail(alarm, TaskSections.TOMORROW, day, "pl"))
    assertEquals("🔁 · Dom", taskDetail(golden.tasks.first { it.text == "Podlać kwiaty" }, TaskSections.TODAY, day, "pl"))
  }

  @Test
  fun rowsOfTheCalendarAndNotesWidgets() {
    val agenda = agendaRows(golden, day, day)
    assertEquals(WidgetRow.Header("Dziś · wt 27 paź", false), agenda[0])
    assertEquals("Kupić żółte jabłka", (agenda[1] as WidgetRow.Entry).entry.title)
    assertEquals("⏰", entryMark((agenda[1] as WidgetRow.Entry).entry))
    assertTrue(agenda.contains(WidgetRow.Header("Jutro · śr 28 paź", false)))
    val closed = agenda.filterIsInstance<WidgetRow.Entry>().first { it.entry.title == "Anulowane" }
    assertEquals("✓", entryMark(closed.entry))

    val pages = pageRows(golden, "Ulubione", "Ostatnie")
    assertEquals(
      listOf(
        WidgetRow.Header("Ulubione"),
        WidgetRow.Page(PageRef("Projekty/Dom.md", "Dom")),
        WidgetRow.Page(PageRef("Zakupy ąę.md", "Zakupy ąę")),
        WidgetRow.Header("Ostatnie"),
        WidgetRow.Page(PageRef("journals/2026-10-27.md", "2026-10-27"))
      ),
      pages
    )
    assertEquals("wtorek, 27 października", pageLabel(PageRef("journals/2026-10-27.md", "2026-10-27"), "pl"))
    assertEquals("Zakupy ąę", pageLabel(PageRef("Zakupy ąę.md", "Zakupy ąę"), "pl"))
  }

  @Test
  fun quickAddShowsTheTaskAtOnce() {
    val action = WidgetAction("a1", WidgetAction.KIND_TASK, "- [ ] Kupić żółty ser", "2026-10-28", "07:15", true, "brulion-test")
    val task = QuickAdd.optimisticTask(action, "journals/2026-10-27.md", 9, day, 1)
    assertEquals("Kupić żółty ser", task.text)
    assertEquals("2026-10-28 07:15", task.reminder)
    assertEquals("", task.due)
    assertEquals(TaskSections.TOMORROW, task.section)
    assertEquals("journals/2026-10-27.md:9", task.key)
    val entry = QuickAdd.entryOf(task, action)!!
    assertEquals(435, entry.minutes)
    assertEquals("alarm", entry.variant)
    val updated = golden.withAddedTask(task, LocalDate.of(2026, 10, 28), entry)
    val reparsed = WidgetSnapshot.parse(updated.toString())
    assertEquals(golden.tasks.size + 1, reparsed.tasks.size)
    assertEquals(TaskSections.TODAY, reparsed.tasksOn(day.plusDays(1), TaskSections.ORDER).first { it.second.key == task.key }.first)
    val items = reparsed.day(LocalDate.of(2026, 10, 28))!!.items
    assertEquals(listOf("06:30", "07:15"), items.filter { it.minutes >= 0 }.map { it.time })
    assertEquals(golden.tasks.size, golden.tasks.size) // the stored one is unchanged

    val noDate = QuickAdd.optimisticTask(WidgetAction("a2", WidgetAction.KIND_TASK, "Bez terminu"), "j.md", 1, day, 1)
    assertEquals(TaskSections.NO_DATE, noDate.section)
    assertNull(QuickAdd.entryOf(noDate, WidgetAction("a2", WidgetAction.KIND_TASK, "Bez terminu")))
    val dueToday = QuickAdd.optimisticTask(WidgetAction("a3", WidgetAction.KIND_TASK, "Dziś", "2026-10-27"), "j.md", 1, day, 1)
    assertEquals("2026-10-27", dueToday.due)
    assertEquals(TaskSections.TODAY, dueToday.section)
  }

  /** The day chips resolve when the entry is added: a dialog open past midnight writes the new day. */
  @Test
  fun quickAddDayChipsResolveWhenAdded() {
    val opened = LocalDate.of(2026, 10, 27)
    val added = LocalDate.of(2026, 10, 28)
    assertEquals(added, QuickAdd.resolveDay(QuickAdd.DayChoice.TODAY, null, added))
    assertEquals(LocalDate.of(2026, 10, 29), QuickAdd.resolveDay(QuickAdd.DayChoice.TOMORROW, null, added))
    assertNull(QuickAdd.resolveDay(QuickAdd.DayChoice.NONE, null, added))
    assertEquals(LocalDate.of(2026, 11, 5), QuickAdd.resolveDay(QuickAdd.DayChoice.DATE, LocalDate.of(2026, 11, 5), added))
    assertEquals(added, QuickAdd.resolveDay(QuickAdd.DayChoice.DATE, null, added))
    // New Year's Eve to 1 January.
    assertEquals(LocalDate.of(2027, 1, 2), QuickAdd.resolveDay(QuickAdd.DayChoice.TOMORROW, null, LocalDate.of(2027, 1, 1)))
    assertEquals(opened, QuickAdd.resolveDay(QuickAdd.DayChoice.TODAY, null, opened))
  }

  @Test
  fun quickAddArmsAProvisionalReminder() {
    val vault = VaultInfo("v", "Brulion", "/x")
    val action = WidgetAction("id-ż", WidgetAction.KIND_TASK, "Pobudka", "2026-10-28", "06:30", true, "v")
    val task = QuickAdd.optimisticTask(action, "journals/2026-10-27.md", 4, day, 1)
    val zone = ZoneId.of("Europe/Warsaw")
    val reminder = QuickAdd.provisionalReminder(action, task, vault, LocalDateTime.of(2026, 10, 27, 22, 0), zone)!!
    assertTrue(reminder.provisional)
    assertTrue(reminder.id < 0)
    assertTrue(reminder.alarm)
    assertEquals(LocalDateTime.of(2026, 10, 28, 6, 30).atZone(zone).toInstant().toEpochMilli(), reminder.at)
    assertEquals("journals/2026-10-27.md", reminder.path)
    assertEquals(4, reminder.line)
    // "Zrobione" as on the app's reminders: the app finds the task by note, time, line and text.
    assertTrue(reminder.canComplete)
    assertEquals("widget:id-ż", reminder.key)
    assertEquals("2026-10-28T06:30", reminder.local)
    assertEquals(DoneRequest("widget:id-ż", "2026-10-28T06:30", "journals/2026-10-27.md", 4, "Pobudka", "2026-10-28", "v"),
      DoneRequest.of(reminder, LocalDate.of(2026, 10, 28)))
    // The JSON keeps the flag (ReminderStore), an older one has none.
    assertTrue(Reminder.fromJson(reminder.toJson())!!.provisional)
    assertFalse(Reminder.fromJson(JSONObject().put("id", 1).put("at", 5L))!!.provisional)
    // Past times and tasks without a time arm nothing.
    assertNull(QuickAdd.provisionalReminder(action, task, vault, LocalDateTime.of(2026, 10, 28, 7, 0), zone))
    val plain = QuickAdd.optimisticTask(WidgetAction("p", WidgetAction.KIND_TASK, "x", "2026-10-28"), "j.md", 1, day, 1)
    assertNull(QuickAdd.provisionalReminder(action, plain, vault, LocalDateTime.of(2026, 10, 27, 8, 0), zone))
  }

  /** Reminders off in Brulion: the snapshot says so (an older one without the field: on). */
  @Test
  fun snapshotSaysWhetherRemindersAreOn() {
    val off = JSONObject(data("snapshot-v1.json")).put("reminders", false)
    assertFalse(WidgetSnapshot.parse(off.toString()).remindersOn)
    val older = JSONObject(data("snapshot-v1.json")).apply { remove("reminders") }
    assertTrue(WidgetSnapshot.parse(older.toString()).remindersOn)
  }

  /** The app's next set replaces a provisional reminder; a snooze of one stays. */
  @Test
  fun scheduleRemindersDropsProvisionalOnes() {
    val now = 1_000_000L
    val provisional = Reminder(-5, "Pobudka", "", now + 60_000, true, "j.md", 4, provisional = true)
    val snoozed = Reminder(7, "Drzemka", "", now + 60_000, false, "j.md", 2, snoozed = true)
    val snoozedProvisional = provisional.copy(id = -6, snoozed = true)
    val app = Reminder(12, "Z aplikacji", "", now + 120_000, false, "j.md", 4)
    val kept = AlarmScheduler.keptOf(listOf(provisional, snoozed, snoozedProvisional), listOf(app), now)
    assertEquals(listOf(snoozed, snoozedProvisional), kept)
    assertNotNull(kept)
  }

  @Test
  fun datesInTheAppsWords() {
    assertEquals("wtorek, 27 października", WidgetDates.long(day, "pl"))
    assertEquals("Tuesday, October 27", WidgetDates.long(day, "en"))
    assertEquals("wt 27 paź", WidgetDates.short(day, "pl", 2026))
    assertEquals("27 paź 2026", WidgetDates.short(day, "pl", 2027))
    assertEquals("Październik 2026", WidgetDates.monthTitle(2026, 10, "pl"))
    assertEquals("October 2026", WidgetDates.monthTitle(2026, 10, "en"))
    assertEquals("jutro", WidgetDates.relative(day.plusDays(1), day, "pl"))
    assertEquals("nd", WidgetDates.weekdayShort(7, "pl"))
  }
}
