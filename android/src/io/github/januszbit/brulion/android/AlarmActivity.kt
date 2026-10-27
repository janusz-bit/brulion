// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.app.Activity
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.text.format.DateFormat
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import io.github.januszbit.brulion.R
import java.util.Date

/**
 * Full-screen alarm shown over the lock screen when an alarm rings while the screen is off
 * (the notification's full-screen intent). The ringing itself belongs to the notification;
 * every button ends it. A plain Android activity: it does not start Qt.
 *
 * The screen also closes when the alarm stops some other way: its ring time is over
 * ([RingTimeout]: at the deadline while it is shown, or when the AlarmManager deadline fires
 * first), a reaction in the notification ("OK", "Drzemka 10 min", "Zrobione", a tap — all through
 * [ReminderNotifier.cancel]), and when it comes back to the front with its alarm no longer
 * ringing.
 */
class AlarmActivity : Activity() {
  private val handler = Handler(Looper.getMainLooper())

  /** The alarm shown. */
  private var reminder: Reminder? = null

  /** A button was pressed: this screen finishes by itself (opening the note may need unlocking). */
  private var handled = false

  private val stopRinging = Runnable {
    if (!handled) {
      reminder?.let { RingTimeout.expire(this, it) }
      finish()
    }
  }

  // Not after a button press: "Otwórz notatkę" stays open until the keyguard is dismissed.
  private val onAlarmStopped: (Int) -> Unit = { id ->
    if (!handled && id == reminder?.id) finish()
  }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    showOverLockScreen()
    RingTimeout.addListener(onAlarmStopped)
    if (!bind(intent)) finish()
  }

  override fun onNewIntent(intent: Intent) {
    super.onNewIntent(intent)
    setIntent(intent)
    bind(intent)
  }

  override fun onResume() {
    super.onResume()
    val shown = reminder ?: return
    if (!handled && !ReminderNotifier.isRinging(this, shown.id)) finish()
  }

  override fun onDestroy() {
    handler.removeCallbacks(stopRinging)
    RingTimeout.removeListener(onAlarmStopped)
    super.onDestroy()
  }

  private fun showOverLockScreen() {
    setShowWhenLocked(true)
    setTurnScreenOn(true)
    window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
  }

  private fun bind(intent: Intent?): Boolean {
    val reminder = Reminder.fromIntent(intent) ?: return false
    this.reminder = reminder
    handled = false
    setContentView(content(reminder))
    handler.removeCallbacks(stopRinging)
    val stopAt = intent?.getLongExtra(Extras.STOP_AT, 0L) ?: 0L
    if (stopAt > 0L) {
      handler.postDelayed(stopRinging, (stopAt - SystemClock.elapsedRealtime()).coerceAtLeast(0L))
    }
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
    time.setTextColor(AMBER)
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
        handled = true
        ReminderActions.snooze(this, reminder)
        finish()
      },
      wide(dp(40))
    )
    if (reminder.canComplete) {
      root.addView(
        button(R.string.brulion_action_done) {
          handled = true
          ReminderActions.done(this, reminder)
          finish()
        },
        wide(dp(12))
      )
    }
    root.addView(
      button(R.string.brulion_action_ok) {
        handled = true
        ReminderActions.dismiss(this, reminder.id)
        finish()
      },
      wide(dp(12))
    )
    root.addView(
      button(R.string.brulion_action_open) {
        handled = true
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
    if (keyguard != null && keyguard.isKeyguardLocked) {
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
    // Brulion's ink, amber and a muted paper tone (docs/UI.md).
    const val BACKGROUND = 0xFF1F2430.toInt()
    const val AMBER = 0xFFF2B33D.toInt()
    const val MUTED = 0xFFB8C0D0.toInt()
  }
}
