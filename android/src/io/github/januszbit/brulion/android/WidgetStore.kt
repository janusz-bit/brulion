// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.annotation.SuppressLint
import android.app.AlarmManager
import android.app.PendingIntent
import android.appwidget.AppWidgetManager
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.util.Log
import io.github.januszbit.brulion.R
import java.time.LocalDate
import java.time.LocalDateTime
import java.time.LocalTime
import java.time.YearMonth
import java.time.ZoneId

/**
 * What the widgets keep (SharedPreferences): the app's latest snapshot ([WidgetSnapshot]) and,
 * per calendar widget, the month it shows and the day picked in it.
 */
internal object WidgetStore {
  private const val PREFS_NAME = "io.github.januszbit.brulion.android.widgets"
  private const val KEY_SNAPSHOT = "snapshot"

  private fun prefs(context: Context) = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

  fun snapshot(context: Context): WidgetSnapshot = WidgetSnapshot.parse(prefs(context).getString(KEY_SNAPSHOT, null))

  /** Keeps [snapshot] (written at once: the process may end right after). */
  @SuppressLint("ApplySharedPref")
  fun saveSnapshot(context: Context, snapshot: String) {
    synchronized(AlarmScheduler.lock) { prefs(context).edit().putString(KEY_SNAPSHOT, snapshot).commit() }
  }

  fun month(context: Context, widgetId: Int): YearMonth? =
    prefs(context).getString("month.$widgetId", null)?.let {
      try {
        YearMonth.parse(it)
      } catch (e: RuntimeException) {
        null
      }
    }

  fun selectedDay(context: Context, widgetId: Int): LocalDate? =
    JournalAppend.parseDate(prefs(context).getString("day.$widgetId", null))

  /** The calendar widget [widgetId] shows [month] with [day] picked (null: today's). */
  fun setCalendar(context: Context, widgetId: Int, month: YearMonth?, day: LocalDate?) {
    val edit = prefs(context).edit()
    if (month == null) edit.remove("month.$widgetId") else edit.putString("month.$widgetId", month.toString())
    if (day == null) edit.remove("day.$widgetId") else edit.putString("day.$widgetId", day.toString())
    edit.apply()
  }

  fun forget(context: Context, widgetIds: IntArray) {
    val edit = prefs(context).edit()
    for (id in widgetIds) edit.remove("month.$id").remove("day.$id")
    edit.apply()
  }
}

/**
 * Redraws every placed widget from the stored snapshot, and keeps them on the right day: an
 * inexact alarm a few seconds after midnight redraws them again (today's tasks move on while
 * Brulion is closed), re-armed by every redraw. No periodic updates (`updatePeriodMillis` 0).
 */
internal object WidgetUpdater {
  private const val TAG = "BrulionWidgets"
  private const val REQUEST_MIDNIGHT = 0x5701

  /** Redraws all widgets; returns how many are placed. Any thread. */
  @Suppress("DEPRECATION") // the lists are RemoteViewsService collections (WidgetViews.list)
  fun refreshAll(context: Context): Int {
    val app = context.applicationContext
    val manager = AppWidgetManager.getInstance(app) ?: return 0
    val snapshot = WidgetStore.snapshot(app)
    var placed = 0
    for (kind in WidgetKind.entries) {
      val ids = manager.getAppWidgetIds(ComponentName(app, kind.provider))
      if (ids.isEmpty()) continue
      placed += ids.size
      for (id in ids) {
        try {
          manager.updateAppWidget(id, WidgetViews.render(app, kind, id, snapshot, manager.getAppWidgetOptions(id)))
        } catch (e: RuntimeException) {
          Log.e(TAG, "cannot draw the ${kind.name} widget $id", e)
        }
      }
      manager.notifyAppWidgetViewDataChanged(ids, R.id.widget_list)
    }
    armMidnight(app)
    return placed
  }

  /** Redraws one widget (resized, its month changed). */
  @Suppress("DEPRECATION") // as refreshAll
  fun refresh(context: Context, kind: WidgetKind, widgetId: Int) {
    val app = context.applicationContext
    val manager = AppWidgetManager.getInstance(app) ?: return
    val snapshot = WidgetStore.snapshot(app)
    manager.updateAppWidget(widgetId, WidgetViews.render(app, kind, widgetId, snapshot, manager.getAppWidgetOptions(widgetId)))
    manager.notifyAppWidgetViewDataChanged(intArrayOf(widgetId), R.id.widget_list)
  }

  private fun armMidnight(context: Context) {
    val alarms = context.getSystemService(Context.ALARM_SERVICE) as? AlarmManager ?: return
    val next = LocalDateTime.of(LocalDate.now().plusDays(1), LocalTime.of(0, 0, 5))
    val at = next.atZone(ZoneId.systemDefault()).toInstant().toEpochMilli()
    val intent = Intent(context, WidgetReceiver::class.java).setAction(WidgetReceiver.ACTION_MIDNIGHT)
    val operation =
      PendingIntent.getBroadcast(context, REQUEST_MIDNIGHT, intent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
    alarms.set(AlarmManager.RTC, at, operation)
  }
}

/**
 * The widgets' own broadcasts (not exported: only Brulion's PendingIntents reach it): midnight,
 * the calendar's month buttons and day cells.
 */
class WidgetReceiver : BroadcastReceiver() {
  override fun onReceive(context: Context, intent: Intent) {
    val widgetId = intent.getIntExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, AppWidgetManager.INVALID_APPWIDGET_ID)
    when (intent.action) {
      ACTION_MIDNIGHT -> WidgetUpdater.refreshAll(context)
      ACTION_MONTH -> {
        if (widgetId == AppWidgetManager.INVALID_APPWIDGET_ID) return
        val today = LocalDate.now()
        val shown = WidgetStore.month(context, widgetId) ?: YearMonth.from(today)
        val delta = intent.getIntExtra(EXTRA_DELTA, 0)
        val month = if (delta == 0) null else shown.plusMonths(delta.toLong()).takeIf { it != YearMonth.from(today) }
        // A month of its own: its first day picked; back to today's month: today.
        WidgetStore.setCalendar(context, widgetId, month, month?.atDay(1))
        WidgetUpdater.refresh(context, WidgetKind.CALENDAR, widgetId)
      }
      ACTION_DAY -> {
        if (widgetId == AppWidgetManager.INVALID_APPWIDGET_ID) return
        val day = JournalAppend.parseDate(intent.getStringExtra(EXTRA_DAY)) ?: return
        val month = YearMonth.from(day).takeIf { it != YearMonth.from(LocalDate.now()) }
        WidgetStore.setCalendar(context, widgetId, month, day.takeIf { it != LocalDate.now() })
        WidgetUpdater.refresh(context, WidgetKind.CALENDAR, widgetId)
      }
    }
  }

  internal companion object {
    const val ACTION_MIDNIGHT = "io.github.januszbit.brulion.android.action.WIDGETS_MIDNIGHT"
    const val ACTION_MONTH = "io.github.januszbit.brulion.android.action.WIDGET_MONTH"
    const val ACTION_DAY = "io.github.januszbit.brulion.android.action.WIDGET_DAY"
    const val EXTRA_DELTA = "io.github.januszbit.brulion.android.extra.DELTA"
    const val EXTRA_DAY = "io.github.januszbit.brulion.android.extra.DAY"
  }
}
