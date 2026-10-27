// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.app.PendingIntent
import android.appwidget.AppWidgetManager
import android.content.Context
import android.content.Intent
import android.graphics.Typeface
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.text.style.RelativeSizeSpan
import android.text.style.StyleSpan
import android.widget.RemoteViews
import androidx.annotation.ColorRes
import io.github.januszbit.brulion.R
import java.time.LocalDate
import java.time.YearMonth

/** The three widgets: their provider and the key their list's intent carries. */
internal enum class WidgetKind(val provider: Class<*>, val key: String) {
  TASKS(TasksWidget::class.java, "tasks"),
  CALENDAR(CalendarWidget::class.java, "calendar"),
  NOTES(NotesWidget::class.java, "notes");

  companion object {
    fun fromKey(key: String?): WidgetKind? = values().firstOrNull { it.key == key }
  }
}

/**
 * The widgets' RemoteViews, drawn from the snapshot on the phone's current day (the lists' rows:
 * [WidgetListService]). Taps open Brulion at a note's line the way a notification does
 * ([Extras.PATH] / [Extras.LINE] on the launch intent: `BrulionAndroid.activationFrom`), "+" opens
 * [QuickAddActivity], the calendar's buttons go to [WidgetReceiver].
 */
internal object WidgetViews {
  /** A tasks widget at least this tall (dp, three rows of cells) also lists tomorrow's tasks. */
  const val TALL_TASKS_DP = 300

  /** A calendar widget at least this tall (dp) shows the month grid above its agenda. */
  const val TALL_CALENDAR_DP = 270

  private const val REQUEST_OPEN_APP = 0x5710
  private const val REQUEST_QUICK_TASK = 0x5711
  private const val REQUEST_QUICK_NOTE = 0x5712

  fun render(context: Context, kind: WidgetKind, widgetId: Int, snapshot: WidgetSnapshot, options: Bundle?): RemoteViews =
    when (kind) {
      WidgetKind.TASKS -> tasks(context, widgetId, snapshot)
      WidgetKind.CALENDAR ->
        if (heightDp(options) >= TALL_CALENDAR_DP) calendar(context, widgetId, snapshot) else agenda(context, widgetId, snapshot)
      WidgetKind.NOTES -> notes(context, widgetId, snapshot)
    }

  /** The widget's height (dp) in portrait (the largest the launcher reports). */
  fun heightDp(options: Bundle?): Int {
    if (options == null) return 0
    return maxOf(options.getInt(AppWidgetManager.OPTION_APPWIDGET_MIN_HEIGHT), options.getInt(AppWidgetManager.OPTION_APPWIDGET_MAX_HEIGHT))
  }

  // --- the widgets ------------------------------------------------------------------------------

  private fun tasks(context: Context, widgetId: Int, snapshot: WidgetSnapshot): RemoteViews {
    val text = WidgetDates.localized(context, snapshot.locale)
    val today = LocalDate.now()
    val views = RemoteViews(context.packageName, R.layout.widget_tasks)
    views.setTextViewText(R.id.widget_title, text.getString(R.string.brulion_widget_my_day))
    val overdue = snapshot.tasksOn(today, listOf(TaskSections.OVERDUE)).size
    val subtitle = StringBuilder(WidgetDates.long(today, snapshot.locale))
    if (overdue > 0) subtitle.append(" · ").append(text.getString(R.string.brulion_widget_overdue_count, overdue))
    views.setTextViewText(R.id.widget_subtitle, subtitle)
    views.setTextViewText(
      R.id.widget_empty,
      text.getString(if (snapshot.hasVault) R.string.brulion_widget_empty_today else R.string.brulion_widget_open_app)
    )
    views.setOnClickPendingIntent(R.id.widget_header, openApp(context))
    views.setOnClickPendingIntent(R.id.widget_add, quickAdd(context, WidgetAction.KIND_TASK))
    views.setContentDescription(R.id.widget_add, text.getString(R.string.brulion_widget_add_task))
    list(context, views, WidgetKind.TASKS, widgetId)
    return views
  }

  private fun agenda(context: Context, widgetId: Int, snapshot: WidgetSnapshot): RemoteViews {
    val text = WidgetDates.localized(context, snapshot.locale)
    val today = LocalDate.now()
    val views = RemoteViews(context.packageName, R.layout.widget_calendar_small)
    views.setTextViewText(R.id.widget_title, text.getString(R.string.brulion_widget_calendar_label))
    views.setTextViewText(R.id.widget_subtitle, WidgetDates.long(today, snapshot.locale))
    views.setTextViewText(R.id.widget_empty, emptyCalendarText(text, snapshot, today))
    views.setOnClickPendingIntent(R.id.widget_header, openApp(context))
    views.setOnClickPendingIntent(R.id.widget_add, quickAdd(context, WidgetAction.KIND_TASK))
    views.setContentDescription(R.id.widget_add, text.getString(R.string.brulion_widget_add_task))
    list(context, views, WidgetKind.CALENDAR, widgetId)
    return views
  }

  private val DAY_IDS = IntArray(42) { dayId(it) }
  private val WEEKDAY_IDS =
    intArrayOf(R.id.widget_wd_0, R.id.widget_wd_1, R.id.widget_wd_2, R.id.widget_wd_3, R.id.widget_wd_4, R.id.widget_wd_5, R.id.widget_wd_6)

  private fun calendar(context: Context, widgetId: Int, snapshot: WidgetSnapshot): RemoteViews {
    val text = WidgetDates.localized(context, snapshot.locale)
    val today = LocalDate.now()
    val month = WidgetStore.month(context, widgetId) ?: YearMonth.from(today)
    val selected = selectedDay(context, widgetId, today)
    val views = RemoteViews(context.packageName, R.layout.widget_calendar)
    views.setTextViewText(R.id.widget_title, WidgetDates.monthTitle(month.year, month.monthValue, snapshot.locale))
    views.setOnClickPendingIntent(R.id.widget_title, monthIntent(context, widgetId, 0))
    views.setOnClickPendingIntent(R.id.widget_prev, monthIntent(context, widgetId, -1))
    views.setOnClickPendingIntent(R.id.widget_next, monthIntent(context, widgetId, 1))
    views.setContentDescription(R.id.widget_prev, text.getString(R.string.brulion_widget_previous_month))
    views.setContentDescription(R.id.widget_next, text.getString(R.string.brulion_widget_next_month))
    views.setOnClickPendingIntent(R.id.widget_add, quickAdd(context, WidgetAction.KIND_TASK))
    views.setContentDescription(R.id.widget_add, text.getString(R.string.brulion_widget_add_task))
    for (i in 0 until 7) {
      val weekday = (snapshot.weekStart - 1 + i) % 7 + 1
      views.setTextViewText(WEEKDAY_IDS[i], WidgetDates.weekdayShort(weekday, snapshot.locale))
    }
    val grid = WidgetSnapshot.monthGrid(month.year, month.monthValue, snapshot.weekStart)
    for ((i, date) in grid.withIndex()) {
      val id = DAY_IDS[i]
      val label = SpannableStringBuilder(date.dayOfMonth.toString())
      if (date == today) label.setSpan(StyleSpan(Typeface.BOLD), 0, label.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
      val dots = dotsOf(snapshot.day(date)?.kinds.orEmpty())
      label.append('\n')
      if (dots.isEmpty()) {
        label.append(' ')
      } else {
        for (color in dots) {
          val start = label.length
          label.append('●')
          label.setSpan(ForegroundColorSpan(context.getColor(color)), start, label.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
      }
      label.setSpan(RelativeSizeSpan(0.55f), label.length - maxOf(1, dots.size), label.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
      views.setTextViewText(id, label)
      textColor(context, views, id, if (date.month == month.month) R.color.widget_text else R.color.widget_faint)
      views.setInt(
        id,
        "setBackgroundResource",
        when (date) {
          today -> R.drawable.widget_cell_today
          selected -> R.drawable.widget_cell_selected
          else -> R.drawable.widget_cell
        }
      )
      views.setContentDescription(id, WidgetDates.long(date, snapshot.locale))
      views.setOnClickPendingIntent(id, dayIntent(context, widgetId, i, date))
    }
    views.setTextViewText(R.id.widget_empty, emptyCalendarText(text, snapshot, selected))
    list(context, views, WidgetKind.CALENDAR, widgetId)
    return views
  }

  private fun notes(context: Context, widgetId: Int, snapshot: WidgetSnapshot): RemoteViews {
    val text = WidgetDates.localized(context, snapshot.locale)
    val today = LocalDate.now()
    val views = RemoteViews(context.packageName, R.layout.widget_notes)
    views.setTextViewText(R.id.widget_title, snapshot.vault?.name?.takeIf { it.isNotEmpty() } ?: "Brulion")
    views.setTextViewText(R.id.widget_subtitle, WidgetDates.long(today, snapshot.locale))
    views.setTextViewText(R.id.widget_journal, text.getString(R.string.brulion_widget_journal_today))
    views.setTextViewText(R.id.widget_note, text.getString(R.string.brulion_widget_quick_note))
    views.setTextViewText(
      R.id.widget_empty,
      text.getString(if (snapshot.hasVault) R.string.brulion_widget_notes_empty else R.string.brulion_widget_open_app)
    )
    views.setOnClickPendingIntent(R.id.widget_header, openApp(context))
    val journal = todaysJournal(snapshot, today)
    views.setOnClickPendingIntent(R.id.widget_journal, if (journal != null) openNote(context, journal, 0, REQUEST_OPEN_APP + 1) else openApp(context))
    views.setOnClickPendingIntent(R.id.widget_note, quickAdd(context, WidgetAction.KIND_NOTE))
    views.setOnClickPendingIntent(R.id.widget_add, quickAdd(context, WidgetAction.KIND_NOTE))
    views.setContentDescription(R.id.widget_add, text.getString(R.string.brulion_widget_quick_note))
    list(context, views, WidgetKind.NOTES, widgetId)
    return views
  }

  // --- helpers ----------------------------------------------------------------------------------

  /** The day picked in calendar widget [widgetId] (today's when none, or its month's first). */
  fun selectedDay(context: Context, widgetId: Int, today: LocalDate): LocalDate {
    WidgetStore.selectedDay(context, widgetId)?.let { return it }
    val month = WidgetStore.month(context, widgetId) ?: return today
    return if (month == YearMonth.from(today)) today else month.atDay(1)
  }

  /** Today's daily note when it exists (else the app opens as it would). */
  fun todaysJournal(snapshot: WidgetSnapshot, today: LocalDate): String? {
    snapshot.captureOn(today)?.let { if (it.exists) return it.path }
    return snapshot.day(today)?.journal?.takeIf { it.isNotEmpty() }
  }

  private fun emptyCalendarText(text: Context, snapshot: WidgetSnapshot, day: LocalDate): String =
    text.getString(
      when {
        !snapshot.hasVault -> R.string.brulion_widget_open_app
        !snapshot.covers(day) -> R.string.brulion_widget_calendar_outside
        else -> R.string.brulion_widget_calendar_empty
      }
    )

  /** The colours of a day's dots, at most three, the most urgent first. */
  fun dotsOf(kinds: List<String>): List<Int> {
    val order =
      listOf(
        "overdue" to R.color.widget_overdue,
        "alarm" to R.color.widget_overdue,
        "reminder" to R.color.widget_reminder,
        "due" to R.color.widget_accent,
        "scheduled" to R.color.widget_scheduled,
        "done" to R.color.widget_done,
        "journal" to R.color.widget_faint
      )
    return order.filter { it.first in kinds }.map { it.second }.distinct().take(3)
  }

  /** A text colour that follows the launcher's day / night (a resource on Android 12+). */
  fun textColor(context: Context, views: RemoteViews, id: Int, @ColorRes color: Int) {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
      views.setColorStateList(id, "setTextColor", color)
    } else {
      views.setTextColor(id, context.getColor(color))
    }
  }

  @Suppress("DEPRECATION") // RemoteCollectionItems needs API 31: one way for API 28+
  private fun list(context: Context, views: RemoteViews, kind: WidgetKind, widgetId: Int) {
    val service =
      Intent(context, WidgetListService::class.java)
        .putExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, widgetId)
        .putExtra(WidgetListService.EXTRA_KIND, kind.key)
    // A data URI of its own: the system shares one factory per distinct intent.
    service.data = Uri.parse(service.toUri(Intent.URI_INTENT_SCHEME))
    views.setRemoteAdapter(R.id.widget_list, service)
    views.setEmptyView(R.id.widget_list, R.id.widget_empty)
    views.setPendingIntentTemplate(R.id.widget_list, rowTemplate(context, widgetId))
  }

  private fun launchIntent(context: Context): Intent =
    (context.packageManager.getLaunchIntentForPackage(context.packageName) ?: Intent(context, BrulionActivity::class.java))
      .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP)

  fun openApp(context: Context): PendingIntent =
    PendingIntent.getActivity(context, REQUEST_OPEN_APP, launchIntent(context), PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

  private fun openNote(context: Context, path: String, line: Int, requestCode: Int): PendingIntent =
    PendingIntent.getActivity(
      context,
      requestCode,
      launchIntent(context).putExtra(Extras.PATH, path).putExtra(Extras.LINE, line),
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )

  /** The rows fill in the note and line ([WidgetListService.fillIn]): a mutable explicit intent. */
  private fun rowTemplate(context: Context, widgetId: Int): PendingIntent {
    val flags = PendingIntent.FLAG_UPDATE_CURRENT or (if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0)
    return PendingIntent.getActivity(context, 0x6000 + widgetId, launchIntent(context), flags)
  }

  fun quickAdd(context: Context, kind: String): PendingIntent {
    val intent =
      Intent(context, QuickAddActivity::class.java)
        .putExtra(QuickAddActivity.EXTRA_KIND, kind)
        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
    val request = if (kind == WidgetAction.KIND_NOTE) REQUEST_QUICK_NOTE else REQUEST_QUICK_TASK
    return PendingIntent.getActivity(context, request, intent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
  }

  private fun monthIntent(context: Context, widgetId: Int, delta: Int): PendingIntent {
    val intent =
      Intent(context, WidgetReceiver::class.java)
        .setAction(WidgetReceiver.ACTION_MONTH)
        .setData(Uri.parse("brulion-widget://month/$widgetId/$delta"))
        .putExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, widgetId)
        .putExtra(WidgetReceiver.EXTRA_DELTA, delta)
    return PendingIntent.getBroadcast(context, 0, intent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
  }

  private fun dayIntent(context: Context, widgetId: Int, cell: Int, date: LocalDate): PendingIntent {
    val intent =
      Intent(context, WidgetReceiver::class.java)
        .setAction(WidgetReceiver.ACTION_DAY)
        .setData(Uri.parse("brulion-widget://day/$widgetId/$cell"))
        .putExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, widgetId)
        .putExtra(WidgetReceiver.EXTRA_DAY, date.toString())
    return PendingIntent.getBroadcast(context, 0, intent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
  }

  private fun dayId(cell: Int): Int =
    when (cell) {
      0 -> R.id.widget_day_0; 1 -> R.id.widget_day_1; 2 -> R.id.widget_day_2; 3 -> R.id.widget_day_3
      4 -> R.id.widget_day_4; 5 -> R.id.widget_day_5; 6 -> R.id.widget_day_6; 7 -> R.id.widget_day_7
      8 -> R.id.widget_day_8; 9 -> R.id.widget_day_9; 10 -> R.id.widget_day_10; 11 -> R.id.widget_day_11
      12 -> R.id.widget_day_12; 13 -> R.id.widget_day_13; 14 -> R.id.widget_day_14; 15 -> R.id.widget_day_15
      16 -> R.id.widget_day_16; 17 -> R.id.widget_day_17; 18 -> R.id.widget_day_18; 19 -> R.id.widget_day_19
      20 -> R.id.widget_day_20; 21 -> R.id.widget_day_21; 22 -> R.id.widget_day_22; 23 -> R.id.widget_day_23
      24 -> R.id.widget_day_24; 25 -> R.id.widget_day_25; 26 -> R.id.widget_day_26; 27 -> R.id.widget_day_27
      28 -> R.id.widget_day_28; 29 -> R.id.widget_day_29; 30 -> R.id.widget_day_30; 31 -> R.id.widget_day_31
      32 -> R.id.widget_day_32; 33 -> R.id.widget_day_33; 34 -> R.id.widget_day_34; 35 -> R.id.widget_day_35
      36 -> R.id.widget_day_36; 37 -> R.id.widget_day_37; 38 -> R.id.widget_day_38; 39 -> R.id.widget_day_39
      40 -> R.id.widget_day_40; else -> R.id.widget_day_41
    }
}
