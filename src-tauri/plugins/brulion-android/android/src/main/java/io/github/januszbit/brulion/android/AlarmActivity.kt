// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.app.Activity
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.text.format.DateFormat
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import java.util.Date

/**
 * Full-screen alarm shown over the lock screen when an alarm rings while the screen is off
 * (the notification's full-screen intent). The ringing itself belongs to the notification;
 * every button ends it.
 */
class AlarmActivity : Activity() {
  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    showOverLockScreen()
    if (!bind(intent)) finish()
  }

  override fun onNewIntent(intent: Intent) {
    super.onNewIntent(intent)
    setIntent(intent)
    bind(intent)
  }

  private fun showOverLockScreen() {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
      setShowWhenLocked(true)
      setTurnScreenOn(true)
    } else {
      @Suppress("DEPRECATION")
      window.addFlags(
        WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED or WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON
      )
    }
    window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
  }

  private fun bind(intent: Intent?): Boolean {
    val reminder = Reminder.fromIntent(intent) ?: return false
    setContentView(content(reminder))
    return true
  }

  private fun dp(value: Int): Int =
    TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value.toFloat(), resources.displayMetrics).toInt()

  private fun content(reminder: Reminder): View {
    val root = LinearLayout(this)
    root.orientation = LinearLayout.VERTICAL
    root.gravity = Gravity.CENTER
    root.setPadding(dp(32), dp(48), dp(32), dp(48))
    root.setBackgroundColor(BACKGROUND)

    val time = TextView(this)
    time.text = getString(R.string.brulion_alarm_label, DateFormat.getTimeFormat(this).format(Date(reminder.at)))
    time.setTextColor(MUTED)
    time.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f)
    time.gravity = Gravity.CENTER
    root.addView(time, wrap())

    val title = TextView(this)
    title.text = reminder.title.ifBlank { getString(R.string.brulion_reminder_default_title) }
    title.setTextColor(Color.WHITE)
    title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 30f)
    title.gravity = Gravity.CENTER
    title.setPadding(0, dp(16), 0, dp(8))
    root.addView(title, wrap())

    if (reminder.body.isNotBlank() && reminder.body != reminder.title) {
      val body = TextView(this)
      body.text = reminder.body
      body.setTextColor(MUTED)
      body.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f)
      body.gravity = Gravity.CENTER
      body.maxLines = 6
      root.addView(body, wrap())
    }

    root.addView(
      button(R.string.brulion_action_snooze) {
        ReminderActions.snooze(this, reminder)
        finish()
      },
      wide(dp(40))
    )
    root.addView(
      button(R.string.brulion_action_ok) {
        ReminderActions.dismiss(this, reminder.id)
        finish()
      },
      wide(dp(12))
    )
    root.addView(
      button(R.string.brulion_action_open) {
        ReminderActions.dismiss(this, reminder.id)
        openNote(reminder)
      },
      wide(dp(12))
    )
    return root
  }

  private fun button(label: Int, onClick: () -> Unit): Button {
    val button = Button(this)
    button.text = getString(label)
    button.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f)
    button.setOnClickListener { onClick() }
    return button
  }

  private fun wrap(): LinearLayout.LayoutParams =
    LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)

  private fun wide(topMargin: Int): LinearLayout.LayoutParams {
    val params = wrap()
    params.topMargin = topMargin
    return params
  }

  /** Opens the note in the app — after unlocking when the phone is locked. */
  private fun openNote(reminder: Reminder) {
    val keyguard = getSystemService(Context.KEYGUARD_SERVICE) as? KeyguardManager
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && keyguard != null && keyguard.isKeyguardLocked) {
      keyguard.requestDismissKeyguard(
        this,
        object : KeyguardManager.KeyguardDismissCallback() {
          override fun onDismissSucceeded() {
            launchApp(reminder)
          }
        }
      )
    } else {
      launchApp(reminder)
    }
  }

  private fun launchApp(reminder: Reminder) {
    val launch = ReminderNotifier.launchIntent(this, reminder)
    if (launch != null) startActivity(launch)
    finish()
  }

  private companion object {
    const val BACKGROUND = 0xFF1F2430.toInt()
    const val MUTED = 0xFFB8C0D0.toInt()
  }
}
