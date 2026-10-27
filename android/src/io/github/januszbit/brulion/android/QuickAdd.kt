// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.content.Context
import android.util.Log
import java.io.File
import java.time.LocalDate
import java.time.LocalDateTime
import java.time.ZoneId

/**
 * Quick add of the widgets ([QuickAddActivity]): the entry goes into today's daily note at once —
 * Kotlin writes it ([JournalAppend]: the note and, for a missing one, its template text from the
 * snapshot; atomic, only inside the notebook's folder), also while Brulion runs (it reads the
 * note again: [BrulionAndroid.widgetWrote]). The widgets show a new task at once (the stored
 * snapshot gets it), and a task with a time gets a provisional native reminder until the app
 * arms its own. Without a snapshot that knows today's note, or when writing fails, the entry
 * waits in [WidgetInbox] for the app (it writes it at its next start).
 */
internal object QuickAdd {
  private const val TAG = "BrulionWidgets"

  enum class Outcome { WRITTEN, QUEUED, EMPTY }

  fun add(context: Context, action: WidgetAction, now: LocalDateTime = LocalDateTime.now()): Outcome {
    val block = JournalAppend.actionBlock(action)
    if (block.isEmpty()) return Outcome.EMPTY
    val app = context.applicationContext
    val snapshot = WidgetStore.snapshot(app)
    val vault = snapshot.vault
    val today = now.toLocalDate()
    val day = snapshot.captureOn(today)
    if (vault != null && day != null) {
      val result = JournalAppend.appendToFile(File(vault.root), day.path, if (day.exists) "" else day.newText, block)
      if (result.ok && result.file != null) {
        Log.i(TAG, "quick add wrote ${day.path}:${result.line}")
        if (action.kind == WidgetAction.KIND_TASK) {
          val task = optimisticTask(action, day.path, result.line, snapshot.today ?: today, snapshot.weekStart)
          WidgetStore.saveSnapshot(app, snapshot.withAddedTask(task, JournalAppend.parseDate(action.date), entryOf(task, action)).toString())
          provisionalReminder(action, task, vault, now)?.let { arm(app, it) }
        }
        // The notebook's own spelling of the path (the canonical one may differ: /data/user/0 is /data/data).
        BrulionAndroid.widgetWrote(File(vault.root, day.path).path)
        WidgetUpdater.refreshAll(app)
        return Outcome.WRITTEN
      }
      Log.w(TAG, "quick add could not write ${day.path}: ${result.error}; queued for the app")
    }
    WidgetInbox.add(app, action.copy(vault = action.vault.ifEmpty { vault?.id ?: "" }))
    BrulionAndroid.widgetQueued()
    return Outcome.QUEUED
  }

  /** The row the widgets show for a task quick add wrote, until the app publishes again. */
  fun optimisticTask(action: WidgetAction, path: String, line: Int, snapshotDay: LocalDate, weekStart: Int): WidgetTask {
    val date = JournalAppend.parseDate(action.date)
    val time = if (date != null) JournalAppend.parseTime(action.time) else null
    val due = if (date != null && time == null) date.toString() else ""
    val reminder = if (date != null && time != null) "$date ${JournalAppend.hhmm(time)}" else ""
    val page = path.substringAfterLast('/').removeSuffix(".md")
    val section = TaskSections.sectionOf("todo", JournalAppend.parseDate(due), null, null, date.takeIf { time != null }, snapshotDay, weekStart)
    return WidgetTask(
      key = "$path:$line",
      text = JournalAppend.cleanItemText(action.text),
      status = "todo",
      priority = 0,
      path = path,
      line = line,
      page = page,
      due = due,
      scheduled = "",
      start = "",
      reminder = reminder,
      alarm = action.alarm && time != null,
      recurring = false,
      section = section
    )
  }

  /** The task's entry on its day in the calendar widget (null without a day). */
  fun entryOf(task: WidgetTask, action: WidgetAction): CalendarEntry? {
    if (JournalAppend.parseDate(action.date) == null) return null
    val time = if (task.reminder.length >= 16) task.reminder.substring(11, 16) else ""
    val minutes = JournalAppend.parseTime(time)?.let { it.hour * 60 + it.minute } ?: -1
    val reminder = time.isNotEmpty()
    return CalendarEntry(
      time = time,
      minutes = minutes,
      title = task.text,
      kind = if (reminder) "reminder" else "due",
      variant = if (!reminder) "due" else if (task.alarm) "alarm" else "reminder",
      path = task.path,
      line = task.line,
      isTask = true,
      closed = false,
      alarm = task.alarm,
      recurring = false
    )
  }

  /**
   * A native reminder for a task with a time ahead of [now] (null otherwise): the app's next
   * `scheduleReminders` replaces it with its own for the line (provisional ones are not kept).
   * A negative id: the app's ids are positive.
   */
  fun provisionalReminder(action: WidgetAction, task: WidgetTask, vault: VaultInfo, now: LocalDateTime, zone: ZoneId = ZoneId.systemDefault()): Reminder? {
    if (task.reminder.isEmpty()) return null
    val date = JournalAppend.parseDate(task.reminder.substring(0, 10)) ?: return null
    val time = JournalAppend.parseTime(task.reminder.substring(11)) ?: return null
    val local = LocalDateTime.of(date, time)
    if (!local.isAfter(now)) return null
    return Reminder(
      id = -1 - ((action.id.hashCode().toLong() and 0x3fffffffL).toInt()),
      title = task.text,
      body = vault.name,
      at = local.atZone(zone).toInstant().toEpochMilli(),
      alarm = task.alarm,
      path = task.path,
      line = task.line,
      vault = vault.id,
      provisional = true
    )
  }

  private fun arm(context: Context, reminder: Reminder) {
    synchronized(AlarmScheduler.lock) {
      ReminderStore(context).upsert(reminder)
      AlarmScheduler.arm(context, reminder, AlarmScheduler.canScheduleExact(context))
    }
  }
}
