// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.appwidget.AppWidgetManager
import android.content.Context
import android.content.Intent
import android.text.SpannableString
import android.text.Spanned
import android.text.style.StrikethroughSpan
import android.widget.RemoteViews
import android.widget.RemoteViewsService
import io.github.januszbit.brulion.R
import java.time.LocalDate

/**
 * The rows of the widgets' lists (a collection: RemoteViewsService works on every Android
 * Brulion supports): tasks by section, the calendar's agenda, favourite and recent pages. Built
 * from the stored snapshot; Brulion's Qt part never starts for them.
 */
class WidgetListService : RemoteViewsService() {
  override fun onGetViewFactory(intent: Intent): RemoteViewsFactory =
    Factory(
      applicationContext,
      WidgetKind.fromKey(intent.getStringExtra(EXTRA_KIND)) ?: WidgetKind.TASKS,
      intent.getIntExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, AppWidgetManager.INVALID_APPWIDGET_ID)
    )

  internal companion object {
    const val EXTRA_KIND = "io.github.januszbit.brulion.android.extra.WIDGET_KIND"

    /** The intent a row fills into the list's template: open [path] at [line]. */
    fun fillIn(path: String, line: Int): Intent = Intent().putExtra(Extras.PATH, path).putExtra(Extras.LINE, line)
  }

  internal class Factory(private val context: Context, private val kind: WidgetKind, private val widgetId: Int) :
    RemoteViewsFactory {
    private var rows: List<WidgetRow> = emptyList()
    private var snapshot: WidgetSnapshot = WidgetSnapshot.EMPTY
    private var today: LocalDate = LocalDate.now()

    override fun onCreate() = Unit

    override fun onDataSetChanged() {
      snapshot = WidgetStore.snapshot(context)
      today = LocalDate.now()
      val text = WidgetDates.localized(context, snapshot.locale)
      rows =
        when (kind) {
          WidgetKind.TASKS -> {
            val manager = AppWidgetManager.getInstance(context)
            val tall = WidgetViews.heightDp(manager?.getAppWidgetOptions(widgetId)) >= WidgetViews.TALL_TASKS_DP
            taskRows(text, snapshot, today, tall)
          }
          WidgetKind.CALENDAR -> agendaRows(snapshot, WidgetViews.selectedDay(context, widgetId, today), today)
          WidgetKind.NOTES -> pageRows(text, snapshot)
        }
    }

    override fun onDestroy() {
      rows = emptyList()
    }

    override fun getCount(): Int = rows.size

    override fun getViewAt(position: Int): RemoteViews {
      val row = rows.getOrNull(position) ?: return RemoteViews(context.packageName, R.layout.widget_row_header)
      return when (row) {
        is WidgetRow.Header -> header(row)
        is WidgetRow.Task -> task(row)
        is WidgetRow.Entry -> entry(row.entry)
        is WidgetRow.Page -> page(row.page)
      }
    }

    private fun header(row: WidgetRow.Header): RemoteViews {
      val views = RemoteViews(context.packageName, R.layout.widget_row_header)
      views.setTextViewText(R.id.row_text, row.text)
      WidgetViews.textColor(context, views, R.id.row_text, if (row.alert) R.color.widget_overdue else R.color.widget_muted)
      return views
    }

    private fun task(row: WidgetRow.Task): RemoteViews {
      val views = RemoteViews(context.packageName, R.layout.widget_row_task)
      val task = row.task
      val overdue = row.section == TaskSections.OVERDUE
      views.setTextViewText(R.id.row_mark, if (task.status == "doing") "◐" else "○")
      WidgetViews.textColor(context, views, R.id.row_mark, if (overdue) R.color.widget_overdue else R.color.widget_muted)
      views.setTextViewText(R.id.row_text, task.text)
      val detail = taskDetail(task, row.section, today, snapshot.locale)
      views.setTextViewText(R.id.row_detail, detail)
      views.setViewVisibility(R.id.row_detail, if (detail.isEmpty()) android.view.View.GONE else android.view.View.VISIBLE)
      WidgetViews.textColor(context, views, R.id.row_detail, if (overdue) R.color.widget_overdue else R.color.widget_muted)
      views.setOnClickFillInIntent(R.id.row_root, fillIn(task.path, task.line))
      return views
    }

    private fun entry(entry: CalendarEntry): RemoteViews {
      val views = RemoteViews(context.packageName, R.layout.widget_row_entry)
      views.setTextViewText(R.id.row_time, entry.time)
      views.setTextViewText(R.id.row_mark, entryMark(entry))
      WidgetViews.textColor(context, views, R.id.row_mark, entryColor(entry))
      val title = SpannableString(entry.title)
      if (entry.closed) title.setSpan(StrikethroughSpan(), 0, title.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
      views.setTextViewText(R.id.row_text, title)
      WidgetViews.textColor(context, views, R.id.row_text, if (entry.closed) R.color.widget_done else R.color.widget_text)
      views.setOnClickFillInIntent(R.id.row_root, fillIn(entry.path, entry.line))
      return views
    }

    private fun page(page: PageRef): RemoteViews {
      val views = RemoteViews(context.packageName, R.layout.widget_row_page)
      views.setTextViewText(R.id.row_text, page.title)
      views.setOnClickFillInIntent(R.id.row_text, fillIn(page.path, 0))
      return views
    }

    override fun getLoadingView(): RemoteViews? = null

    override fun getViewTypeCount(): Int = 4

    override fun getItemId(position: Int): Long = position.toLong()

    override fun hasStableIds(): Boolean = false
  }
}

/** One row of a widget's list. */
internal sealed class WidgetRow {
  data class Header(val text: String, val alert: Boolean = false) : WidgetRow()
  data class Task(val task: WidgetTask, val section: String) : WidgetRow()
  data class Entry(val entry: CalendarEntry) : WidgetRow()
  data class Page(val page: PageRef) : WidgetRow()
}

/** The rows of the widgets' lists, pure (WidgetSnapshotTest). */
internal object WidgetRows {
  /** At most this many tasks a section lists (the app has them all). */
  const val MAX_TASKS = 60

  fun sectionLabel(text: Context, section: String): String =
    text.getString(
      when (section) {
        TaskSections.OVERDUE -> R.string.brulion_widget_overdue
        TaskSections.TOMORROW -> R.string.brulion_widget_tomorrow
        else -> R.string.brulion_widget_today
      }
    )
}

/** "Zaległe · 2" … then the tasks, for [today]; tomorrow's only on a [tall] widget. */
internal fun taskRows(text: Context, snapshot: WidgetSnapshot, today: LocalDate, tall: Boolean): List<WidgetRow> =
  taskRows(snapshot, today, tall) { WidgetRows.sectionLabel(text, it) }

internal fun taskRows(
  snapshot: WidgetSnapshot,
  today: LocalDate,
  tall: Boolean,
  label: (String) -> String
): List<WidgetRow> {
  val sections = if (tall) listOf(TaskSections.OVERDUE, TaskSections.TODAY, TaskSections.TOMORROW) else listOf(TaskSections.OVERDUE, TaskSections.TODAY)
  val tasks = snapshot.tasksOn(today, sections)
  val out = ArrayList<WidgetRow>()
  for (section in sections) {
    val inSection = tasks.filter { it.first == section }
    if (inSection.isEmpty()) continue
    out.add(WidgetRow.Header("${label(section)} · ${inSection.size}", section == TaskSections.OVERDUE))
    for ((_, task) in inSection.take(WidgetRows.MAX_TASKS)) out.add(WidgetRow.Task(task, section))
  }
  return out
}

/** The second line of a task: when (overdue: its date; today: its time, ⏰ for a reminder) · page. */
internal fun taskDetail(task: WidgetTask, section: String, today: LocalDate, locale: String): String {
  val parts = ArrayList<String>()
  if (section == TaskSections.OVERDUE) {
    WidgetTask.dateOf(task.due)?.let { parts.add(WidgetDates.relative(it, today, locale)) }
  } else {
    val day = if (section == TaskSections.TOMORROW) today.plusDays(1) else today
    val time = task.timeOn(day)
    if (time.isNotEmpty()) {
      val reminderTime = WidgetTask.dateOf(task.reminder) == day
      parts.add(if (reminderTime) (if (task.alarm) "⏰! $time" else "⏰ $time") else time)
    }
  }
  if (task.recurring) parts.add("🔁")
  // A daily note's title is its date: the row says when already.
  if (task.page.isNotEmpty() && JournalAppend.parseDate(task.page) == null) parts.add(task.page)
  return parts.joinToString(" · ")
}

/** The calendar's agenda from [from]: each day with entries ("Dziś · wt 27 paź"), its entries. */
internal fun agendaRows(snapshot: WidgetSnapshot, from: LocalDate, today: LocalDate): List<WidgetRow> {
  val out = ArrayList<WidgetRow>()
  for ((date, day) in snapshot.agenda(from, 14)) {
    val label =
      if (date == today || date == today.plusDays(1) || date == today.minusDays(1)) {
        WidgetDates.relative(date, today, snapshot.locale).replaceFirstChar { it.uppercaseChar() } + " · " +
          WidgetDates.short(date, snapshot.locale, today.year)
      } else {
        WidgetDates.short(date, snapshot.locale, today.year)
      }
    out.add(WidgetRow.Header(label, false))
    for (entry in day.items) out.add(WidgetRow.Entry(entry))
    if (out.size >= 80) break
  }
  return out
}

/** "Ulubione" and "Ostatnie" (without the favourites again). */
internal fun pageRows(text: Context, snapshot: WidgetSnapshot): List<WidgetRow> =
  pageRows(snapshot, text.getString(R.string.brulion_widget_favorites), text.getString(R.string.brulion_widget_recent))

internal fun pageRows(snapshot: WidgetSnapshot, favorites: String, recent: String): List<WidgetRow> {
  val out = ArrayList<WidgetRow>()
  if (snapshot.favorites.isNotEmpty()) {
    out.add(WidgetRow.Header(favorites))
    snapshot.favorites.forEach { out.add(WidgetRow.Page(it)) }
  }
  val favoritePaths = snapshot.favorites.map { it.path }.toSet()
  val others = snapshot.recent.filter { it.path !in favoritePaths }
  if (others.isNotEmpty()) {
    out.add(WidgetRow.Header(recent))
    others.forEach { out.add(WidgetRow.Page(it)) }
  }
  return out
}

/** The mark of an agenda entry: a task's status, a reminder's bell. */
internal fun entryMark(entry: CalendarEntry): String =
  when {
    entry.isTask && entry.closed -> "✓"
    entry.kind == "reminder" -> "⏰"
    entry.isTask -> "○"
    else -> "•"
  }

private fun entryColor(entry: CalendarEntry): Int =
  when (entry.variant) {
    "overdue", "alarm" -> R.color.widget_overdue
    "reminder" -> R.color.widget_reminder
    "due" -> R.color.widget_accent
    "scheduled" -> R.color.widget_scheduled
    "done" -> R.color.widget_done
    else -> R.color.widget_muted
  }
