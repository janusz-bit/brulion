// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject
import java.time.LocalDate

/** An open task of the snapshot (src/app/widgets/widgetsnapshot.h). Dates "YYYY-MM-DD[ HH:mm]" or "". */
internal data class WidgetTask(
  val key: String,
  val text: String,
  val status: String,
  val priority: Int,
  val path: String,
  val line: Int,
  val page: String,
  val due: String,
  val scheduled: String,
  val start: String,
  val reminder: String,
  val alarm: Boolean,
  val recurring: Boolean,
  /** The section on the snapshot's day. */
  val section: String
) {
  /** The section on [day] (TaskListModel::sectionOf, mirrored by [TaskSections]). */
  fun sectionOn(day: LocalDate, weekStart: Int): String =
    TaskSections.sectionOf(
      status,
      dateOf(due),
      dateOf(scheduled),
      dateOf(start),
      dateOf(reminder),
      day,
      weekStart
    )

  /**
   * The time shown in the row on [day]: the reminder's when it is that day, else the due time
   * that day; "" without one.
   */
  fun timeOn(day: LocalDate): String =
    when {
      dateOf(reminder) == day && reminder.length >= 16 -> reminder.substring(11, 16)
      dateOf(due) == day && due.length >= 16 -> due.substring(11, 16)
      else -> ""
    }

  fun toJson(): JSONObject =
    JSONObject()
      .put("key", key)
      .put("text", text)
      .put("status", status)
      .put("priority", priority)
      .put("path", path)
      .put("line", line)
      .put("page", page)
      .put("due", due)
      .put("scheduled", scheduled)
      .put("start", start)
      .put("reminder", reminder)
      .put("alarm", alarm)
      .put("recurring", recurring)
      .put("section", section)

  companion object {
    fun fromJson(json: JSONObject): WidgetTask? {
      val path = json.optString("path", "")
      if (path.isEmpty()) return null
      return WidgetTask(
        key = json.optString("key", ""),
        text = json.optString("text", ""),
        status = json.optString("status", "todo"),
        priority = json.optInt("priority", 0),
        path = path,
        line = json.optInt("line", 0),
        page = json.optString("page", ""),
        due = json.optString("due", ""),
        scheduled = json.optString("scheduled", ""),
        start = json.optString("start", ""),
        reminder = json.optString("reminder", ""),
        alarm = json.optBoolean("alarm", false),
        recurring = json.optBoolean("recurring", false),
        section = json.optString("section", "")
      )
    }

    /** The date of a "YYYY-MM-DD[ HH:mm]" spec, null when there is none or it is broken. */
    fun dateOf(spec: String): LocalDate? = if (spec.length < 10) null else JournalAppend.parseDate(spec.substring(0, 10))
  }
}

/** The sections of TasksView (TaskListModel in src/app/planning/tasklistmodel.h). */
internal object TaskSections {
  const val OVERDUE = "overdue"
  const val TODAY = "today"
  const val TOMORROW = "tomorrow"
  const val THIS_WEEK = "thisWeek"
  const val LATER = "later"
  const val NO_DATE = "noDate"
  const val DONE = "done"
  val ORDER = listOf(OVERDUE, TODAY, TOMORROW, THIS_WEEK, LATER, NO_DATE, DONE)

  /**
   * TaskListModel::sectionOf(): closed tasks are done; a due date before [today] is overdue;
   * else the earlier of due and scheduled — else the reminder's date, else a start date after
   * [today] — files it: on or before today, tomorrow, until the end of the week, later. Checked
   * against tests/data/widgets/refile-cases.json, as the C++ is.
   */
  fun sectionOf(
    status: String,
    due: LocalDate?,
    scheduled: LocalDate?,
    start: LocalDate?,
    reminder: LocalDate?,
    today: LocalDate,
    weekStart: Int
  ): String {
    if (status == "done" || status == "cancelled") return DONE
    if (due != null && due < today) return OVERDUE
    var date: LocalDate? = listOfNotNull(due, scheduled).minOrNull()
    if (date == null) date = reminder
    if (date == null && start != null && start > today) date = start
    return when {
      date == null -> NO_DATE
      date <= today -> TODAY
      date == today.plusDays(1) -> TOMORROW
      date <= endOfWeek(today, weekStart) -> THIS_WEEK
      else -> LATER
    }
  }

  /** The first day of [date]'s week, weeks starting on [weekStart] (1 = Monday … 7 = Sunday). */
  fun startOfWeek(date: LocalDate, weekStart: Int): LocalDate {
    val start = if (weekStart in 1..7) weekStart else 1
    return date.minusDays(((date.dayOfWeek.value - start + 7) % 7).toLong())
  }

  fun endOfWeek(date: LocalDate, weekStart: Int): LocalDate = startOfWeek(date, weekStart).plusDays(6)
}

/** An entry of a calendar day (CalendarModel's entry maps, plain text). */
internal data class CalendarEntry(
  val time: String,
  val minutes: Int,
  val title: String,
  val kind: String,
  val variant: String,
  val path: String,
  val line: Int,
  val isTask: Boolean,
  val closed: Boolean,
  val alarm: Boolean,
  val recurring: Boolean
) {
  fun toJson(): JSONObject =
    JSONObject()
      .put("time", time)
      .put("minutes", minutes)
      .put("title", title)
      .put("kind", kind)
      .put("variant", variant)
      .put("path", path)
      .put("line", line)
      .put("isTask", isTask)
      .put("closed", closed)
      .put("alarm", alarm)
      .put("recurring", recurring)

  companion object {
    fun fromJson(json: JSONObject): CalendarEntry =
      CalendarEntry(
        time = json.optString("time", ""),
        minutes = json.optInt("minutes", -1),
        title = json.optString("title", ""),
        kind = json.optString("kind", ""),
        variant = json.optString("variant", ""),
        path = json.optString("path", ""),
        line = json.optInt("line", 0),
        isTask = json.optBoolean("isTask", false),
        closed = json.optBoolean("closed", false),
        alarm = json.optBoolean("alarm", false),
        recurring = json.optBoolean("recurring", false)
      )
  }
}

internal data class CalendarDay(val journal: String, val kinds: List<String>, val items: List<CalendarEntry>)

internal data class PageRef(val path: String, val title: String)

/** Where quick add writes on [date] (vault-relative [path]; [newText] for a missing note). */
internal data class CaptureDay(val date: LocalDate, val path: String, val exists: Boolean, val newText: String)

internal data class VaultInfo(val id: String, val name: String, val root: String)

/**
 * The snapshot the app publishes for the widgets (src/app/widgets/widgetsnapshot.h, JSON
 * version 1): plain data, read while Brulion is closed. Parsing never throws — a broken or
 * unknown snapshot is [EMPTY] (the widgets ask to open Brulion). The widgets show it on the
 * phone's current day: tasks are filed again ([tasksOn]) when that is not the snapshot's day.
 */
internal class WidgetSnapshot private constructor(val json: JSONObject) {
  val built: String = json.optString("built", "")
  val today: LocalDate? = JournalAppend.parseDate(json.optString("today", ""))
  val weekStart: Int = json.optInt("weekStart", 1).let { if (it in 1..7) it else 1 }
  val locale: String = if (json.optString("locale", "pl") == "en") "en" else "pl"
  val vault: VaultInfo? =
    json.optJSONObject("vault")?.let {
      val root = it.optString("root", "")
      if (root.isEmpty()) null else VaultInfo(it.optString("id", ""), it.optString("name", ""), root)
    }
  val tasks: List<WidgetTask> = objects(json.optJSONArray("tasks")).mapNotNull { WidgetTask.fromJson(it) }
  val calendarFrom: LocalDate? = json.optJSONObject("calendar")?.let { JournalAppend.parseDate(it.optString("from", "")) }
  val calendarTo: LocalDate? = json.optJSONObject("calendar")?.let { JournalAppend.parseDate(it.optString("to", "")) }
  val days: Map<LocalDate, CalendarDay> = parseDays(json.optJSONObject("calendar")?.optJSONObject("days"))
  val favorites: List<PageRef> = pages(json.optJSONObject("notes")?.optJSONArray("favorites"))
  val recent: List<PageRef> = pages(json.optJSONObject("notes")?.optJSONArray("recent"))
  val capture: List<CaptureDay> =
    objects(json.optJSONObject("capture")?.optJSONArray("days")).mapNotNull {
      val date = JournalAppend.parseDate(it.optString("date", "")) ?: return@mapNotNull null
      val path = it.optString("path", "")
      if (path.isEmpty()) null else CaptureDay(date, path, it.optBoolean("exists", false), it.optString("newText", ""))
    }

  val hasVault: Boolean get() = vault != null

  /**
   * The tasks of [sections] on [day], in TasksView's order (the snapshot's order within a section):
   * filed again by [TaskSections] for a day other than the snapshot's.
   */
  fun tasksOn(day: LocalDate, sections: Collection<String>): List<Pair<String, WidgetTask>> =
    tasks
      .map { (if (day == today) it.section else it.sectionOn(day, weekStart)) to it }
      .filter { it.first in sections }
      .sortedBy { TaskSections.ORDER.indexOf(it.first) }

  /** Where quick add writes on [day], null when the snapshot does not know (too old). */
  fun captureOn(day: LocalDate): CaptureDay? = capture.firstOrNull { it.date == day }

  /** The snapshot covers [day] with calendar entries (else: "Otwórz Brulion, aby zobaczyć wpisy"). */
  fun covers(day: LocalDate): Boolean {
    val from = calendarFrom ?: return false
    val to = calendarTo ?: return false
    return !day.isBefore(from) && !day.isAfter(to)
  }

  fun day(date: LocalDate): CalendarDay? = days[date]

  /** The days with entries (or a daily note) from [from] on, at most [count] of them, within [days] days. */
  fun agenda(from: LocalDate, count: Int, days: Int = 62): List<Pair<LocalDate, CalendarDay>> {
    val out = ArrayList<Pair<LocalDate, CalendarDay>>()
    var date = from
    for (i in 0 until days) {
      if (out.size >= count) break
      this.days[date]?.let { if (it.items.isNotEmpty()) out.add(date to it) }
      date = date.plusDays(1)
    }
    return out
  }

  /**
   * The snapshot with [task] added (quick add wrote it with the app closed): it shows at once,
   * until the app publishes again. Also an entry on its day in the calendar.
   */
  fun withAddedTask(task: WidgetTask, day: LocalDate?, entry: CalendarEntry?): WidgetSnapshot {
    val copy = JSONObject(json.toString())
    val tasks = copy.optJSONArray("tasks") ?: JSONArray().also { copy.put("tasks", it) }
    tasks.put(task.toJson())
    if (day != null && entry != null) {
      val calendar = copy.optJSONObject("calendar") ?: JSONObject().also { copy.put("calendar", it) }
      val days = calendar.optJSONObject("days") ?: JSONObject().also { calendar.put("days", it) }
      val dayJson = days.optJSONObject(day.toString()) ?: JSONObject().put("journal", "").put("kinds", JSONArray()).put("items", JSONArray())
      val items = objects(dayJson.optJSONArray("items")).map { CalendarEntry.fromJson(it) } + entry
      // Timed entries first, by time; then all-day ones (CalendarModel's order).
      val sorted = items.sortedWith(compareBy<CalendarEntry> { it.minutes < 0 }.thenBy { if (it.minutes < 0) 0 else it.minutes })
      val array = JSONArray()
      for (item in sorted) array.put(item.toJson())
      dayJson.put("items", array)
      val kinds = dayJson.optJSONArray("kinds") ?: JSONArray()
      val mark = if (entry.variant == "neutral") "scheduled" else entry.variant
      if ((0 until kinds.length()).none { kinds.optString(it) == mark }) kinds.put(mark)
      dayJson.put("kinds", kinds)
      days.put(day.toString(), dayJson)
    }
    return WidgetSnapshot(copy)
  }

  override fun toString(): String = json.toString()

  companion object {
    const val VERSION = 1

    val EMPTY = WidgetSnapshot(JSONObject().put("version", VERSION))

    /** The snapshot in [raw]; [EMPTY] when missing, broken or of another version. */
    fun parse(raw: String?): WidgetSnapshot {
      if (raw.isNullOrEmpty()) return EMPTY
      return try {
        val json = JSONObject(raw)
        if (json.optInt("version", 0) != VERSION) EMPTY else WidgetSnapshot(json)
      } catch (e: JSONException) {
        EMPTY
      } catch (e: RuntimeException) {
        EMPTY
      }
    }

    /** The 42 days (six weeks) of [year]-[month]'s grid, weeks starting on [weekStart]. */
    fun monthGrid(year: Int, month: Int, weekStart: Int): List<LocalDate> {
      val first = TaskSections.startOfWeek(LocalDate.of(year, month, 1), weekStart)
      return (0 until 42).map { first.plusDays(it.toLong()) }
    }

    private fun objects(array: JSONArray?): List<JSONObject> {
      if (array == null) return emptyList()
      val out = ArrayList<JSONObject>(array.length())
      for (i in 0 until array.length()) array.optJSONObject(i)?.let { out.add(it) }
      return out
    }

    private fun pages(array: JSONArray?): List<PageRef> =
      objects(array).mapNotNull {
        val path = it.optString("path", "")
        if (path.isEmpty()) null else PageRef(path, it.optString("title", path))
      }

    private fun parseDays(json: JSONObject?): Map<LocalDate, CalendarDay> {
      if (json == null) return emptyMap()
      val out = HashMap<LocalDate, CalendarDay>()
      for (key in json.keys()) {
        val date = JournalAppend.parseDate(key) ?: continue
        val day = json.optJSONObject(key) ?: continue
        val kinds = day.optJSONArray("kinds")
        out[date] =
          CalendarDay(
            day.optString("journal", ""),
            if (kinds == null) emptyList() else (0 until kinds.length()).map { kinds.optString(it) },
            objects(day.optJSONArray("items")).map { CalendarEntry.fromJson(it) }
          )
      }
      return out
    }
  }
}
