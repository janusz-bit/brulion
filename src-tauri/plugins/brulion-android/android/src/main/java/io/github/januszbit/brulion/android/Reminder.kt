// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject
import java.util.TimeZone

/** Intent extras shared by the plugin, the receivers and the alarm screen. */
internal object Extras {
  /** A [Reminder] as JSON (fire, snooze, dismiss and alarm-screen intents). */
  const val REMINDER = "io.github.januszbit.brulion.android.extra.REMINDER"

  /** Note to open (VaultPath) — on the launch intent of a tapped notification. */
  const val PATH = "io.github.januszbit.brulion.android.extra.PATH"

  /** Line of the reminder in that note. */
  const val LINE = "io.github.januszbit.brulion.android.extra.LINE"

  /** Notification to dismiss once the app is opened from it. */
  const val NOTIFICATION_ID = "io.github.januszbit.brulion.android.extra.NOTIFICATION_ID"

  /** When a ringing alarm stops by itself (`SystemClock.elapsedRealtime` ms) — alarm screen. */
  const val STOP_AT = "io.github.januszbit.brulion.android.extra.STOP_AT"
}

/** Intent actions of the plugin's own broadcasts. */
internal object Actions {
  const val FIRE = "io.github.januszbit.brulion.android.action.FIRE"
  const val SNOOZE = "io.github.januszbit.brulion.android.action.SNOOZE"
  const val DISMISS = "io.github.januszbit.brulion.android.action.DISMISS"

  /** The ring time of an alarm is over (see [RingTimeout]). */
  const val RING_TIMEOUT = "io.github.januszbit.brulion.android.action.RING_TIMEOUT"
}

/**
 * One native reminder — `AndroidReminder` of the IPC contract (docs/ARCHITECTURE.md).
 *
 * [at] is epoch ms. [ringMinutes] (alarms only) is how long the alarm rings before it turns into
 * a silent "Przegapiony alarm", already resolved by [RingTimeout.minutes]. [snoozed] marks a
 * reminder re-armed natively by "Drzemka 10 min": the frontend does not know about it, so it
 * survives `scheduleReminders` replacing the set.
 */
internal data class Reminder(
  val id: Int,
  val title: String,
  val body: String,
  val at: Long,
  val alarm: Boolean,
  val path: String,
  val line: Int,
  val snoozed: Boolean = false,
  val ringMinutes: Int = RingTimeout.DEFAULT_MINUTES
) {
  fun toJson(): JSONObject {
    val json = JSONObject()
    json.put("id", id)
    json.put("title", title)
    json.put("body", body)
    json.put("at", at)
    json.put("alarm", alarm)
    json.put("path", path)
    json.put("line", line)
    json.put("snoozed", snoozed)
    json.put("ringMinutes", ringMinutes)
    return json
  }

  /** Puts this reminder into [intent] (extras travel with PendingIntents). */
  fun putInto(intent: Intent): Intent = intent.putExtra(Extras.REMINDER, toJson().toString())

  companion object {
    fun fromJson(json: JSONObject): Reminder? =
      try {
        Reminder(
          id = json.getInt("id"),
          title = json.optString("title", ""),
          body = json.optString("body", ""),
          at = json.getLong("at"),
          alarm = json.optBoolean("alarm", false),
          path = json.optString("path", ""),
          line = json.optInt("line", 0),
          snoozed = json.optBoolean("snoozed", false),
          // Absent in reminders stored by older versions: the default.
          ringMinutes = RingTimeout.minutes((json.opt("ringMinutes") as? Number)?.toLong())
        )
      } catch (e: JSONException) {
        null
      }

    fun fromIntent(intent: Intent?): Reminder? {
      val raw = intent?.getStringExtra(Extras.REMINDER) ?: return null
      return try {
        fromJson(JSONObject(raw))
      } catch (e: JSONException) {
        null
      }
    }

    fun listToJson(items: List<Reminder>): String {
      val array = JSONArray()
      for (item in items) {
        array.put(item.toJson())
      }
      return array.toString()
    }

    fun listFromJson(raw: String?): List<Reminder> {
      if (raw.isNullOrEmpty()) return emptyList()
      val array =
        try {
          JSONArray(raw)
        } catch (e: JSONException) {
          return emptyList()
        }
      val items = ArrayList<Reminder>(array.length())
      for (i in 0 until array.length()) {
        val json = array.optJSONObject(i) ?: continue
        fromJson(json)?.let { items.add(it) }
      }
      return items
    }
  }
}

/**
 * The armed reminders, persisted in SharedPreferences so they can be re-armed after a reboot,
 * an app update or a clock change, when the web app is not running.
 *
 * Also remembers the time zone the times were computed in: reminders are local wall-clock
 * time, so after a time-zone change they are moved to the same wall-clock time (see
 * [WallClock]). All callers run on the main thread.
 */
internal class ReminderStore(context: Context) {
  private val prefs: SharedPreferences =
    context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

  fun load(): List<Reminder> = Reminder.listFromJson(prefs.getString(KEY_ITEMS, null))

  /** Time-zone id the stored times belong to (null before the first save). */
  fun zone(): String? = prefs.getString(KEY_ZONE, null)

  fun save(items: List<Reminder>, zone: String) {
    prefs.edit().putString(KEY_ITEMS, Reminder.listToJson(items)).putString(KEY_ZONE, zone).apply()
  }

  /** Adds or replaces the reminder with the same id (keeps the stored zone). */
  fun upsert(reminder: Reminder) {
    val items = load().filter { it.id != reminder.id } + reminder
    save(items, zone() ?: TimeZone.getDefault().id)
  }

  /** Forgets reminder [id] once it fired (a later entry with the same id is kept). */
  fun removeFired(id: Int, now: Long) {
    val items = load()
    val kept = items.filterNot { it.id == id && it.at <= now + FIRE_TOLERANCE_MS }
    if (kept.size != items.size) {
      save(kept, zone() ?: TimeZone.getDefault().id)
    }
  }

  private companion object {
    const val PREFS_NAME = "io.github.januszbit.brulion.android.reminders"
    const val KEY_ITEMS = "items"
    const val KEY_ZONE = "zone"
    const val FIRE_TOLERANCE_MS = 60_000L
  }
}
