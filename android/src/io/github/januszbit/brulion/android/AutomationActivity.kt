// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.util.Log
import android.widget.Toast
import io.github.januszbit.brulion.R

/**
 * brulion:// links and the activity intents of automation apps (docs/ANDROID.md "Automation"): a
 * separate exported activity without a UI of its own (its own task, not in Recents), as
 * [ShareActivity]. It reads the request ([Automation.requestUri]); what needs the app's window —
 * opening something, a request without the key, an extension's command — goes to C++ inside this
 * process ([BrulionAndroid.deliverUri]; never as an extra of the exported [BrulionActivity], which
 * any app could forge) and Brulion comes to the front, where the app checks it as any link's (a
 * write without the key asks first). A write with a key runs in the background
 * ([Automation.runInBackground]) and Brulion stays where it is; its answer is the result broadcast
 * and, on failure, a toast.
 *
 *   adb shell am start -a android.intent.action.VIEW -d 'brulion://open?page=Zakupy'
 *   adb shell am start -a io.github.januszbit.brulion.action.RUN -d 'brulion://append?page=Zakupy' \
 *       --es text 'Mleko' --es key <key>
 */
class AutomationActivity : Activity() {
  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    if (savedInstanceState != null) {
      finish() // recreated: that request was handled
      return
    }
    handle(intent)
  }

  private fun handle(intent: Intent?) {
    val uri = Automation.requestUri(intent)
    if (uri == null) {
      Log.w(TAG, "an intent without a brulion:// request")
      Toast.makeText(applicationContext, R.string.brulion_automation_unusable, Toast.LENGTH_SHORT).show()
      finish()
      return
    }
    val quiet = Automation.safely { intent?.getBooleanExtra(Automation.EXTRA_QUIET, false) } ?: false
    val resultPackage = Automation.safely { intent?.getStringExtra(Automation.EXTRA_RESULT_PACKAGE) }
    if (Automation.needsWindow(uri)) {
      BrulionAndroid.deliverUri(uri)
      packageManager.getLaunchIntentForPackage(packageName)?.let { launch ->
        launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP)
        try {
          startActivity(launch)
        } catch (e: RuntimeException) {
          Log.w(TAG, "cannot bring Brulion to the front", e)
        }
      }
      finish()
      return
    }
    val app = applicationContext
    val requestId = Automation.requestId(intent, uri)
    Automation.runInBackground(app, uri, requestId) { result ->
      val answer = result ?: org.json.JSONObject().put("ok", false).put("error", "no_answer").put("id", requestId)
      Automation.afterWrite(app, answer)
      Automation.broadcastResult(app, answer, requestId, resultPackage)
      if (!answer.optBoolean("ok", false) && !quiet) {
        Toast.makeText(app, app.getString(R.string.brulion_automation_failed, answer.optString("error")), Toast.LENGTH_LONG).show()
      }
    }
    finish()
  }

  private companion object {
    const val TAG = Automation.TAG
  }
}
