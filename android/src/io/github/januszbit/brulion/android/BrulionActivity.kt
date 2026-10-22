// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.content.Intent
import android.os.Bundle
import org.qtproject.qt.android.bindings.QtActivity

/**
 * Brulion's main activity: Qt's activity plus the hooks [BrulionAndroid] needs — the note a
 * tapped notification points at (launch intent or `onNewIntent`, launch mode singleTop), the
 * answers of runtime permission dialogs and of the ringtone pickers — and the on-screen
 * keyboard's observer ([ImeInsets]).
 */
class BrulionActivity : QtActivity() {
  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    BrulionAndroid.onActivityCreated(this)
    ImeInsets.install(this)
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
    ImeInsets.uninstall(this)
    BrulionAndroid.onActivityDestroyed(this)
    super.onDestroy()
  }
}
