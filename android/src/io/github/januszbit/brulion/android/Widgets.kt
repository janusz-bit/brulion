// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.Context
import android.os.Bundle

/**
 * Brulion's home-screen widgets (docs/ANDROID.md "Widgety"). They draw the snapshot the app
 * published last ([WidgetStore]); the system asks for a redraw when one is placed or resized,
 * the app after each snapshot ([BrulionAndroid.updateWidgets]), [WidgetUpdater] after midnight.
 */
abstract class BrulionWidget internal constructor(private val kind: WidgetKind) : AppWidgetProvider() {
  override fun onUpdate(context: Context, manager: AppWidgetManager, appWidgetIds: IntArray) {
    WidgetUpdater.refreshAll(context)
  }

  override fun onAppWidgetOptionsChanged(context: Context, manager: AppWidgetManager, appWidgetId: Int, newOptions: Bundle) {
    // Resized: another layout (the calendar's grid) or more rows (tomorrow's tasks).
    WidgetUpdater.refresh(context, kind, appWidgetId)
  }

  override fun onDeleted(context: Context, appWidgetIds: IntArray) {
    WidgetStore.forget(context, appWidgetIds)
  }
}

/** "Mój dzień": overdue and today's tasks, "+" adds one (3×2, resizable). */
class TasksWidget : BrulionWidget(WidgetKind.TASKS)

/** The month with dots and the agenda of the picked day; low: the agenda only (4×4, down to 3×2). */
class CalendarWidget : BrulionWidget(WidgetKind.CALENDAR)

/** Today's journal, a quick note, favourite and recent pages (3×2). */
class NotesWidget : BrulionWidget(WidgetKind.NOTES)
