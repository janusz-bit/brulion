// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.annotation.SuppressLint
import android.content.Context
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject
import java.time.LocalDate

/**
 * "Zrobione" tapped for the reminder occurrence [key] at [local] (`yyyy-MM-ddTHH:mm`) of the note
 * [path] (reminder on [line], titled [title]) on [day] (`yyyy-MM-dd`, the phone's local date of
 * the tap: the ✅ date written).
 */
internal data class DoneRequest(
  val key: String,
  val local: String,
  val path: String,
  val line: Int,
  val title: String,
  val day: String
) {
  fun toJson(): JSONObject =
    JSONObject()
      .put("key", key)
      .put("local", local)
      .put("path", path)
      .put("line", line)
      .put("title", title)
      .put("day", day)

  companion object {
    fun of(reminder: Reminder, day: LocalDate): DoneRequest =
      DoneRequest(reminder.key, reminder.local, reminder.path, reminder.line, reminder.title, day.toString())

    /** Null when the occurrence or the note is missing. */
    fun fromJson(json: JSONObject): DoneRequest? {
      val request =
        DoneRequest(
          key = json.optString("key", ""),
          local = json.optString("local", ""),
          path = json.optString("path", ""),
          line = json.optInt("line", 0),
          title = json.optString("title", ""),
          day = json.optString("day", "")
        )
      return request.takeIf { it.key.isNotEmpty() && it.local.isNotEmpty() && it.path.isNotEmpty() }
    }
  }
}

/**
 * The "Zrobione" taps C++ has not taken yet. A notification button runs in a BroadcastReceiver,
 * usually while Qt is not running, and the Markdown is edited by C++ only (one parser): the tap is
 * persisted here and handed over through `BrulionAndroid.takeDoneRequests` — at once when the app
 * runs ([BrulionAndroid.doneQueued]), else at its next start. Under [AlarmScheduler.lock] (receivers
 * run on the main thread, C++ takes the queue on Qt's).
 */
internal object DoneQueue {
  /** More taps than this before Brulion runs again: the oldest go. */
  const val MAX_ENTRIES = 100

  private const val PREFS_NAME = "io.github.januszbit.brulion.android.done"
  private const val KEY_QUEUE = "queue"

  /** Adds [request] (written at once: the process may end right after the receiver). */
  @SuppressLint("ApplySharedPref")
  fun add(context: Context, request: DoneRequest) {
    synchronized(AlarmScheduler.lock) {
      val prefs = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
      prefs.edit().putString(KEY_QUEUE, appended(prefs.getString(KEY_QUEUE, null), request)).commit()
    }
  }

  /** The queued requests as a JSON array (`[]` when none), emptying the queue. */
  @SuppressLint("ApplySharedPref")
  fun take(context: Context): String =
    synchronized(AlarmScheduler.lock) {
      val prefs = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
      val raw = prefs.getString(KEY_QUEUE, null)
      if (raw != null) prefs.edit().remove(KEY_QUEUE).commit()
      toJson(parse(raw))
    }

  /**
   * [raw] with [request] appended: broken entries dropped, an earlier tap of the same occurrence
   * replaced (the notification and its alarm screen), at most [max] entries (the newest).
   */
  fun appended(raw: String?, request: DoneRequest, max: Int = MAX_ENTRIES): String {
    val items = parse(raw).filterNot { it.key == request.key && it.local == request.local } + request
    return toJson(items.takeLast(max.coerceAtLeast(1)))
  }

  /** The requests of the JSON array [raw], skipping broken entries (never throws). */
  fun parse(raw: String?): List<DoneRequest> {
    if (raw.isNullOrEmpty()) return emptyList()
    val array =
      try {
        JSONArray(raw)
      } catch (e: JSONException) {
        return emptyList()
      }
    val items = ArrayList<DoneRequest>(array.length())
    for (i in 0 until array.length()) {
      val json = array.optJSONObject(i) ?: continue
      DoneRequest.fromJson(json)?.let { items.add(it) }
    }
    return items
  }

  private fun toJson(items: List<DoneRequest>): String {
    val array = JSONArray()
    for (item in items) array.put(item.toJson())
    return array.toString()
  }
}
