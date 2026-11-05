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
import org.qtproject.qt.android.bindings.QtService

/**
 * The background check's process (`:background`, android/AndroidManifest.xml): a Qt service, so
 * Qt starts Brulion's library here with the argument `--background-check` (the service's
 * `android.app.arguments`), and `main()` runs the check without any UI
 * (`src/app/background/backgroundentry.h`). Qt cannot run in the process of the app's activity a
 * second time — hence a process of its own, which ends when the job unbinds (QtService's
 * onDestroy ends the process).
 *
 * The job ([BackgroundCheckJob], main process) binds it and says hello with its [Messenger]; C++
 * hands the result to [finished] (through `BrulionAndroid.backgroundCheckFinished`), which sends
 * it as [MSG_RESULT] — or keeps it until the hello came.
 */
class BackgroundCheckService : QtService() {
  private val incoming =
    Messenger(
      Handler(Looper.getMainLooper()) { message ->
        if (message.what == MSG_HELLO) hello(message.replyTo)
        true
      }
    )

  override fun onBind(intent: Intent?): IBinder = incoming.binder

  companion object {
    private const val TAG = BackgroundCheck.TAG
    internal const val MSG_HELLO = 1
    internal const val MSG_RESULT = 2
    internal const val KEY_JSON = "json"

    private val lock = Any()
    private var client: Messenger? = null
    private var pending: String? = null

    private fun hello(replyTo: Messenger?) {
      synchronized(lock) {
        client = replyTo
        val result = pending ?: return
        pending = null
        send(replyTo, result)
      }
    }

    /** C++ finished the check (any thread). */
    internal fun finished(json: String) {
      synchronized(lock) {
        val to = client
        if (to == null) pending = json else send(to, json)
      }
    }

    private fun send(to: Messenger?, json: String) {
      if (to == null) return
      val message = Message.obtain(null, MSG_RESULT).apply { data = Bundle().apply { putString(KEY_JSON, json) } }
      try {
        to.send(message)
      } catch (e: RemoteException) {
        Log.w(TAG, "the background check's job went away", e)
      }
    }
  }
}
