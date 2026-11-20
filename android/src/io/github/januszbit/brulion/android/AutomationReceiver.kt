// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log
import org.json.JSONObject

/**
 * The broadcast intents of automation apps (docs/ANDROID.md "Automation"): `RUN` sent to this
 * receiver writes in the background, never bringing up a window — so only a write with the
 * notebook's key runs ([Automation.runInBackground]); anything else is answered with an error
 * (`needs_window`: opening something or a command, `key_required`: no key — send it to
 * [AutomationActivity] instead, where the user can be asked). The answer is the result broadcast.
 *
 *   adb shell am broadcast -n io.github.januszbit.brulion/io.github.januszbit.brulion.android.AutomationReceiver \
 *       -a io.github.januszbit.brulion.action.RUN -d 'brulion://task?remind=jutro%20o%2018' \
 *       --es text 'Zadzwonić do mamy' --es key <key>
 */
class AutomationReceiver : BroadcastReceiver() {
  override fun onReceive(context: Context, intent: Intent?) {
    if (intent?.action != Automation.ACTION_RUN) return
    val app = context.applicationContext
    val resultPackage = Automation.safely { intent.getStringExtra(Automation.EXTRA_RESULT_PACKAGE) }
    val uri = Automation.requestUri(intent)
    val requestId = Automation.requestId(intent, uri ?: "")
    if (uri == null) {
      Automation.broadcastResult(app, JSONObject().put("ok", false).put("error", "bad_uri"), requestId, resultPackage)
      return
    }
    if (Automation.needsWindow(uri)) {
      val error = if (Automation.paramOf(uri, "key").isNullOrBlank()) "key_required" else "needs_window"
      Log.i(TAG, "automation broadcast refused: $error")
      Automation.broadcastResult(
        app, JSONObject().put("ok", false).put("error", error).put("action", Automation.actionOf(uri)), requestId, resultPackage
      )
      return
    }
    // The answer comes after onReceive returns: keep the broadcast (and the process) alive.
    val pending = goAsync()
    Automation.runInBackground(app, uri, requestId) { result ->
      val answer = result ?: JSONObject().put("ok", false).put("error", "no_answer").put("id", requestId)
      try {
        Automation.afterWrite(app, answer)
        Automation.broadcastResult(app, answer, requestId, resultPackage)
      } finally {
        pending.finish()
      }
    }
  }

  private companion object {
    const val TAG = Automation.TAG
  }
}
