// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.app.job.JobInfo
import android.app.job.JobScheduler
import android.content.ComponentName
import android.content.Context
import android.util.Log
import org.json.JSONException
import org.json.JSONObject

/**
 * The background check of the notes (docs/ANDROID.md "Sprawdzanie notatek w tle"): while Brulion is
 * closed a periodic system job ([BackgroundCheckJob], JobScheduler) starts Brulion's C++ in a process
 * of its own ([BackgroundCheckService]), which reads the notebook whose reminders are armed — after a
 * Nextcloud sync, when that is set up — and answers with the reminders the app would arm. The job,
 * in the app's main process (it owns [ReminderStore]: SharedPreferences are not shared between
 * processes), arms them with [AlarmScheduler.replaceAll].
 *
 * The app hands over what to check whenever its reminders start ([configure], from C++:
 * `src/app/background/backgroundcheck.h`, `Target`): `{root, stateDir, vault, ringMinutes, language,
 * intervalMinutes, nextcloud?}`; `{}` turns the check off (reminders off, the notebook closed on
 * purpose, the setting "Wyłączone").
 */
internal object BackgroundCheck {
  const val TAG = "BrulionBackground"

  /** The periodic job's id (one job; scheduling it again replaces it). */
  const val JOB_ID = 0x4210

  /** Android runs periodic jobs at most every 15 minutes. */
  const val MIN_INTERVAL_MINUTES = 15

  private const val PREFS = "io.github.januszbit.brulion.android.background"
  private const val KEY_TARGET = "target"
  private const val KEY_STATUS = "status"

  /** What to check (the C++ Target's JSON as handed over, with the fields the job looks at). */
  data class Target(val vault: String, val intervalMinutes: Int, val json: String) {
    companion object {
      /** Null for nonsense or a check that is off (no notebook, no interval). */
      fun parse(json: String?): Target? {
        if (json.isNullOrBlank()) return null
        val o =
          try {
            JSONObject(json)
          } catch (e: JSONException) {
            return null
          }
        val vault = o.optString("vault")
        val interval = o.optInt("intervalMinutes", 0)
        if (o.optString("root").isEmpty() || o.optString("stateDir").isEmpty() || vault.isEmpty() || interval <= 0) {
          return null
        }
        return Target(vault, maxOf(interval, MIN_INTERVAL_MINUTES), json)
      }
    }
  }

  /** What becomes of a check's result. */
  enum class Verdict {
    /** Arm its reminders. */
    APPLY,
    /** The check could not read the notebook (or gave nonsense): the armed set stays. */
    FAILED,
    /** The check is off now, or for another notebook than the one checked. */
    STALE,
    /** Brulion came to the front (or started) meanwhile: its own set wins. */
    APP_TOOK_OVER,
  }

  /**
   * The verdict on [result] (the C++ Check's JSON) for the [current] target; [appTookOver]: the app's
   * UI was shown, or its C++ attached, while the check ran.
   */
  fun verdict(result: JSONObject?, current: Target?, appTookOver: Boolean): Verdict =
    when {
      result == null || !result.optBoolean("ok", false) || result.optJSONArray("reminders") == null -> Verdict.FAILED
      current == null || result.optString("vault") != current.vault -> Verdict.STALE
      appTookOver -> Verdict.APP_TOOK_OVER
      else -> Verdict.APPLY
    }

  /**
   * The status kept for the settings page and the event log:
   * `{at, ok, applied, reminders, sync, error, notes, elapsedMs}` of [result] ([verdict], at [now]).
   */
  fun statusOf(result: JSONObject?, verdict: Verdict, now: Long): JSONObject {
    val status =
      JSONObject()
        .put("at", now)
        .put("ok", result?.optBoolean("ok", false) ?: false)
        .put("applied", verdict == Verdict.APPLY)
        .put("verdict", verdict.name.lowercase())
        .put("reminders", result?.optJSONArray("reminders")?.length() ?: 0)
        .put("sync", result?.optString("sync", "off") ?: "off")
    result?.optString("error")?.takeIf { it.isNotEmpty() }?.let { status.put("error", it) }
    result?.optString("syncError")?.takeIf { it.isNotEmpty() }?.let { status.put("syncError", it) }
    if (result?.has("notes") == true) status.put("notes", result.optInt("notes"))
    if (result?.has("elapsedMs") == true) status.put("elapsedMs", result.optLong("elapsedMs"))
    return status
  }

  // -----------------------------------------------------------------------------------------
  // Main process: the target, the job, the result
  // -----------------------------------------------------------------------------------------

  /**
   * Keeps [json] as what to check and schedules the periodic job for its interval (unchanged: the
   * job keeps its period); `{}` or a target without an interval cancels it. Returns
   * `{"interval": minutes}` (0: off). Throws [JSONException] for a malformed argument (the stored
   * target is left as it was).
   */
  fun configure(context: Context, json: String): String {
    val off = JSONObject(json).length() == 0
    val target = if (off) null else Target.parse(json) ?: throw JSONException("not a background check target")
    val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    // commit(): the check's own process reads the file as soon as it starts.
    if (target == null) prefs.edit().remove(KEY_TARGET).commit() else prefs.edit().putString(KEY_TARGET, json).commit()
    if (target == null) cancelJob(context) else scheduleJob(context, target.intervalMinutes)
    return JSONObject().put("interval", target?.intervalMinutes ?: 0).toString()
  }

  /** The target handed over last (null: none, or off). */
  fun target(context: Context): Target? =
    Target.parse(context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY_TARGET, null))

  /** The target's JSON for the check's process (`{}`: none). */
  fun targetJson(context: Context): String = target(context)?.json ?: "{}"

  /** The last check's status (`{}` before the first one). */
  fun status(context: Context): String =
    context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY_STATUS, null) ?: "{}"

  /**
   * The check answered [json]: arm its reminders unless the [verdict] says otherwise, keep the
   * status. Main thread of the main process.
   */
  fun finish(context: Context, json: String?, appTookOver: Boolean, now: Long = System.currentTimeMillis()) {
    val result =
      try {
        json?.let { JSONObject(it) }
      } catch (e: JSONException) {
        null
      }
    val verdict = verdict(result, target(context), appTookOver)
    var status = statusOf(result, verdict, now)
    if (verdict == Verdict.APPLY) {
      try {
        val armed = AlarmScheduler.replaceAll(context, Reminder.listFromJson(result!!.getJSONArray("reminders")))
        status.put("armed", armed.scheduled)
      } catch (e: JSONException) {
        Log.w(TAG, "the check's reminders are malformed: nothing armed", e)
        status = statusOf(result, Verdict.FAILED, now)
      }
    }
    Log.i(TAG, "background check: $status")
    context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(KEY_STATUS, status.toString()).apply()
  }

  /** The job for [minutes], unless it is scheduled with that period already. */
  private fun scheduleJob(context: Context, minutes: Int) {
    val jobs = context.getSystemService(Context.JOB_SCHEDULER_SERVICE) as? JobScheduler ?: return
    val period = minutes * 60_000L
    val pending = jobs.getPendingJob(JOB_ID)
    if (pending != null && pending.isPeriodic && pending.intervalMillis == period) return
    val job =
      JobInfo.Builder(JOB_ID, ComponentName(context, BackgroundCheckJob::class.java))
        .setPeriodic(period)
        // Survives a reboot (RECEIVE_BOOT_COMPLETED); a sync without network fails fast and the
        // reminders of what the phone has are armed all the same: no network constraint.
        .setPersisted(true)
        .build()
    val result = jobs.schedule(job)
    Log.i(TAG, "background check every $minutes min: " + if (result == JobScheduler.RESULT_SUCCESS) "scheduled" else "refused")
  }

  private fun cancelJob(context: Context) {
    val jobs = context.getSystemService(Context.JOB_SCHEDULER_SERVICE) as? JobScheduler ?: return
    if (jobs.getPendingJob(JOB_ID) != null) {
      jobs.cancel(JOB_ID)
      Log.i(TAG, "background check off")
    }
  }
}
