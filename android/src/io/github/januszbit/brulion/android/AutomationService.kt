// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.content.Intent
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.Message
import android.os.Messenger
import android.os.RemoteException
import android.util.Log
import org.json.JSONException
import org.json.JSONObject
import org.qtproject.qt.android.bindings.QtService
import java.util.ArrayDeque

/**
 * The process of automation requests that write while Brulion is closed (`:automation`,
 * android/AndroidManifest.xml; docs/ANDROID.md "Automation"): a Qt service, so Qt starts
 * Brulion's library here with the argument `--automation` and `main()` runs
 * `runAutomationService()` (src/app/background/backgroundentry.h) — no window, no QML.
 *
 * Each request ([Automation.runInBackground], main process) binds it and sends [MSG_RUN] with the
 * job `{uri, id, target}`; C++ polls [takeJob], carries the job out (automationrun.h) and hands the
 * result to [finished], which sends it back as [MSG_RESULT] to the one that asked. Jobs run one at
 * a time in their order. The process ends when the last request unbinds (QtService's onDestroy).
 */
class AutomationService : QtService() {
  private val incoming =
    Messenger(
      Handler(Looper.getMainLooper()) { message ->
        if (message.what == MSG_RUN) queue(message.replyTo, message.data?.getString(KEY_JSON))
        true
      }
    )

  override fun onBind(intent: Intent?): IBinder = incoming.binder

  companion object {
    private const val TAG = Automation.TAG
    internal const val MSG_RUN = 1
    internal const val MSG_RESULT = 2
    internal const val KEY_JSON = "json"
    /** More waiting jobs are refused at once (a burst of intents is not a queue). */
    private const val MAX_WAITING = 16

    private class Job(val replyTo: Messenger?, val json: JSONObject)

    private val lock = Any()
    private val waiting = ArrayDeque<Job>()
    /** The job C++ took and works on. */
    private var running: Job? = null

    private fun queue(replyTo: Messenger?, json: String?) {
      val job =
        try {
          JSONObject(json ?: "")
        } catch (e: JSONException) {
          send(replyTo, JSONObject().put("ok", false).put("error", "bad_job").toString())
          return
        }
      synchronized(lock) {
        if (waiting.size >= MAX_WAITING) {
          send(replyTo, JSONObject().put("ok", false).put("error", "busy").put("id", job.optString("id")).toString())
          return
        }
        waiting.add(Job(replyTo, job))
      }
    }

    /** C++ (any thread): the next job, "" when none waits. */
    internal fun takeJob(): String =
      synchronized(lock) {
        if (running != null) return ""
        val job = waiting.pollFirst() ?: return ""
        running = job
        job.json.toString()
      }

    /** C++ finished the job it took. */
    internal fun finished(json: String) {
      val job = synchronized(lock) { running.also { running = null } } ?: return
      send(job.replyTo, json)
    }

    private fun send(to: Messenger?, json: String) {
      if (to == null) return
      val message = Message.obtain(null, MSG_RESULT).apply { data = Bundle().apply { putString(KEY_JSON, json) } }
      try {
        to.send(message)
      } catch (e: RemoteException) {
        Log.w(TAG, "the automation request went away", e)
      }
    }
  }
}
