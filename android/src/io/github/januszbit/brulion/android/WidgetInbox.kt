// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.annotation.SuppressLint
import android.content.Context
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

/**
 * An entry of a widget's quick add ([QuickAddActivity]): a task ([KIND_TASK], on [date] — "" none
 * — at [time] "HH:mm" — "" none — an alarm with [alarm]) or a note ([KIND_NOTE]) with [text], for
 * the notebook [vault] (its id in Brulion, from the snapshot; "" unknown), [added] at the phone's
 * local time "yyyy-MM-ddTHH:mm". The JSON is what C++ reads (src/app/widgets/widgetactions.h).
 */
internal data class WidgetAction(
  val id: String,
  val kind: String,
  val text: String,
  val date: String = "",
  val time: String = "",
  val alarm: Boolean = false,
  val vault: String = "",
  val added: String = ""
) {
  fun toJson(): JSONObject =
    JSONObject()
      .put("id", id)
      .put("kind", kind)
      .put("text", text)
      .put("date", date)
      .put("time", time)
      .put("alarm", alarm)
      .put("vault", vault)
      .put("added", added)

  companion object {
    const val KIND_TASK = "task"
    const val KIND_NOTE = "note"

    /** Null without an id or with an unknown kind. */
    fun fromJson(json: JSONObject): WidgetAction? {
      val action =
        WidgetAction(
          id = json.optString("id", ""),
          kind = json.optString("kind", KIND_TASK),
          text = json.optString("text", ""),
          date = json.optString("date", ""),
          time = json.optString("time", ""),
          alarm = json.optBoolean("alarm", false),
          vault = json.optString("vault", ""),
          added = json.optString("added", "")
        )
      return action.takeIf { it.id.isNotEmpty() && (it.kind == KIND_TASK || it.kind == KIND_NOTE) }
    }
  }
}

/**
 * Quick-add entries Kotlin could not write itself ([JournalAppend]: no snapshot of a notebook
 * yet, the note changed meanwhile, an I/O error): C++ takes them through
 * `BrulionAndroid.takeWidgetActions` once their notebook is ready and writes them into today's
 * journal (src/app/widgets/widgetpublisher.h) — the same way as [DoneQueue]: persisted at once
 * (`commit()`), taken only by the notebook they belong to, each handed out once. Under
 * [AlarmScheduler.lock] (activities run on the main thread, C++ takes the queue on Qt's).
 */
internal object WidgetInbox {
  /** More entries than this before Brulion runs again: the oldest go. */
  const val MAX_ENTRIES = 200

  private const val PREFS_NAME = "io.github.januszbit.brulion.android.widget-inbox"
  private const val KEY_QUEUE = "queue"

  @SuppressLint("ApplySharedPref")
  fun add(context: Context, action: WidgetAction) {
    synchronized(AlarmScheduler.lock) {
      val prefs = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
      prefs.edit().putString(KEY_QUEUE, appended(prefs.getString(KEY_QUEUE, null), action)).commit()
    }
  }

  /** Queues [actions] in order (the app could not write them: back for its next try). */
  @SuppressLint("ApplySharedPref")
  fun addAll(context: Context, actions: List<WidgetAction>) {
    if (actions.isEmpty()) return
    synchronized(AlarmScheduler.lock) {
      val prefs = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
      prefs.edit().putString(KEY_QUEUE, appendedAll(prefs.getString(KEY_QUEUE, null), actions)).commit()
    }
  }

  /** The queued entries of notebook [vault] (see [split]) as a JSON array, removed from the queue. */
  @SuppressLint("ApplySharedPref")
  fun take(context: Context, vault: String = ""): String =
    synchronized(AlarmScheduler.lock) {
      val prefs = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
      val raw = prefs.getString(KEY_QUEUE, null) ?: return "[]"
      val (taken, kept) = split(raw, vault)
      val edit = prefs.edit()
      if (kept.isEmpty()) edit.remove(KEY_QUEUE) else edit.putString(KEY_QUEUE, toJson(kept))
      edit.commit()
      toJson(taken)
    }

  /** How many entries wait (any notebook). */
  fun size(context: Context): Int =
    parse(
      context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE).getString(KEY_QUEUE, null)
    ).size

  /**
   * The entries of [raw] for notebook [vault] — and those without one — and the rest. Empty
   * [vault]: every entry. Broken entries are dropped.
   */
  fun split(raw: String?, vault: String): Pair<List<WidgetAction>, List<WidgetAction>> =
    parse(raw).partition { vault.isEmpty() || it.vault.isEmpty() || it.vault == vault }

  /** [raw] with [action] appended (an entry with the same id replaced), at most [max] (the newest). */
  fun appended(raw: String?, action: WidgetAction, max: Int = MAX_ENTRIES): String {
    val items = parse(raw).filterNot { it.id == action.id } + action
    return toJson(items.takeLast(max.coerceAtLeast(1)))
  }

  /** [raw] with each of [actions] appended in order (see [appended]). */
  fun appendedAll(raw: String?, actions: List<WidgetAction>, max: Int = MAX_ENTRIES): String =
    actions.fold(raw ?: "[]") { queue, action -> appended(queue, action, max) }

  /** The entries of the JSON array [raw], skipping broken ones (never throws). */
  fun parse(raw: String?): List<WidgetAction> {
    if (raw.isNullOrEmpty()) return emptyList()
    val array =
      try {
        JSONArray(raw)
      } catch (e: JSONException) {
        return emptyList()
      }
    val items = ArrayList<WidgetAction>(array.length())
    for (i in 0 until array.length()) {
      val json = array.optJSONObject(i) ?: continue
      WidgetAction.fromJson(json)?.let { items.add(it) }
    }
    return items
  }

  private fun toJson(items: List<WidgetAction>): String {
    val array = JSONArray()
    for (item in items) array.put(item.toJson())
    return array.toString()
  }
}
