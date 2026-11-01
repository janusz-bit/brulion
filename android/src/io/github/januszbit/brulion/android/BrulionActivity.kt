// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.content.Intent
import android.content.res.Configuration
import android.os.Bundle
import android.util.Log
import android.view.ViewGroup
import org.qtproject.qt.android.bindings.QtActivity

/**
 * Brulion's main activity: Qt's activity plus the hooks [BrulionAndroid] needs — the note a
 * tapped notification points at (launch intent or `onNewIntent`, launch mode singleTop), the
 * answers of runtime permission dialogs and of the ringtone pickers —, the on-screen keyboard's
 * observer ([ImeInsets]) and the system bars' icons after the app's theme. Recreated before Qt
 * started, it restarts the app ([StartupRelaunch]).
 */
class BrulionActivity : QtActivity() {
  private var restarting = false

  override fun onCreate(savedInstanceState: Bundle?) {
    val createdBefore = createdInProcess
    createdInProcess = true
    super.onCreate(savedInstanceState)
    val content = findViewById<ViewGroup>(android.R.id.content)
    val restarts = intent?.getIntExtra(StartupRelaunch.EXTRA_RESTARTS, 0) ?: 0
    if (StartupRelaunch.needsRestart(createdBefore, content?.childCount ?: 0, isFinishing, restarts)) {
      restartApp(restarts + 1)
      return
    }
    if (!createdBefore) BrulionAndroid.restoreAfterRestart(this, restarts)
    BrulionAndroid.onActivityCreated(this)
    ImeInsets.install(this)
  }

  /**
   * The app started afresh with this activity's intent (its extras: a debuggable build's
   * arguments; the notification's note and a share go along, [BrulionAndroid.keepForRestart]), as
   * QtActivity restarts it after a relaunch once Qt runs.
   */
  private fun restartApp(restarts: Int) {
    Log.w(TAG, "recreated before Qt started (a configuration change during the start): restart $restarts")
    restarting = true
    val restart = Intent(intent ?: Intent())
      .setComponent(componentName)
      .putExtra(StartupRelaunch.EXTRA_RESTARTS, restarts)
      .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
    BrulionAndroid.keepForRestart(this, restart)
    startActivity(restart)
    finish()
    Runtime.getRuntime().exit(0)
  }

  // Qt sets the system bars' icons after the system's day/night mode here: the app's theme wins.
  override fun onConfigurationChanged(newConfig: Configuration) {
    super.onConfigurationChanged(newConfig)
    BrulionAndroid.applySystemBars()
  }

  override fun onNewIntent(intent: Intent) {
    super.onNewIntent(intent)
    // getIntent() returns the newest intent from now on (QtActivity does not do this).
    setIntent(intent)
    BrulionAndroid.onActivityIntent(this, intent)
  }

  // Qt's own results (file dialogs) are dispatched by super; the ringtone pickers are ours.
  override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
    super.onActivityResult(requestCode, resultCode, data)
    BrulionAndroid.onActivityResult(this, requestCode, resultCode, data)
  }

  override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
    super.onRequestPermissionsResult(requestCode, permissions, grantResults)
    BrulionAndroid.onPermissionsResult(requestCode)
  }

  override fun onDestroy() {
    if (!restarting) {
      ImeInsets.uninstall(this)
      BrulionAndroid.onActivityDestroyed(this)
    }
    super.onDestroy()
  }

  private companion object {
    const val TAG = "BrulionActivity"

    /** A BrulionActivity of this process got as far as onCreate (Qt's libraries are loaded). */
    var createdInProcess = false
  }
}
