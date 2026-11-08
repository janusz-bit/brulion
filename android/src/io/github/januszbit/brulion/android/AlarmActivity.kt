// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.app.Activity
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Rect
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.text.TextUtils
import android.text.format.DateFormat
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowManager
import android.view.accessibility.AccessibilityManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import io.github.januszbit.brulion.R
import java.time.Instant
import java.time.ZoneId
import java.util.Date

/**
 * Full-screen alarm shown over the lock screen when an alarm rings while the screen is off
 * (the notification's full-screen intent). The ringing itself belongs to the notification;
 * every answer ends it. A plain Android activity: it does not start Qt.
 *
 * Black (an OLED screen's pixels stay off), the time large and thin, the date, the reminder and
 * its page; "Done" (a task) and "Open note" as chips under them, and at the bottom the
 * [AlarmSlider]: slide left to snooze, right for OK — a tap alone answers nothing. While
 * TalkBack's touch exploration is on, two plain buttons take the slider's place.
 *
 * The screen also closes when the alarm stops some other way: its ring time is over
 * ([RingTimeout]: at the deadline while it is shown, or when the AlarmManager deadline fires
 * first), a reaction in the notification ("OK", "Snooze 10 min", "Done", a tap — all through
 * [ReminderNotifier.cancel]), and when it comes back to the front with its alarm no longer
 * ringing.
 */
class AlarmActivity : Activity() {
  private val handler = Handler(Looper.getMainLooper())

  /** The alarm shown. */
  private var reminder: Reminder? = null

  /** An answer was given: this screen finishes by itself (opening the note may need unlocking). */
  private var handled = false

  private val stopRinging = Runnable {
    if (!handled) {
      reminder?.let { RingTimeout.expire(this, it) }
      finish()
    }
  }

  // Not after an answer: "Open note" stays open until the keyguard is dismissed.
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

  private fun snooze(reminder: Reminder) {
    handled = true
    ReminderActions.snooze(this, reminder)
    finish()
  }

  private fun dismiss(reminder: Reminder) {
    handled = true
    ReminderActions.dismiss(this, reminder.id)
    finish()
  }

  /**
   * The screen: in the middle (scrolls when it does not fit, e.g. in landscape) the heading, the
   * time, the date, the reminder, its page and the chips; at the bottom the slider and its hint.
   */
  private fun content(reminder: Reminder): View {
    val time = DateFormat.getTimeFormat(this).format(Date(reminder.at))
    // Read out first by TalkBack ("Alarm · 07:30").
    title = getString(R.string.brulion_alarm_label, time)

    val root = LinearLayout(this)
    root.orientation = LinearLayout.VERTICAL
    root.setBackgroundColor(Color.BLACK)
    val side = dp(24)
    root.setPadding(side, dp(16), side, dp(20))
    root.setOnApplyWindowInsetsListener { view, insets ->
      val bars = systemInsets(insets)
      view.setPadding(side + bars.left, dp(16) + bars.top, side + bars.right, dp(20) + bars.bottom)
      insets
    }

    val scroll = ScrollView(this)
    scroll.isFillViewport = true
    scroll.isVerticalScrollBarEnabled = false
    val middle = LinearLayout(this)
    middle.orientation = LinearLayout.VERTICAL
    middle.gravity = Gravity.CENTER
    middle.setPadding(0, dp(8), 0, dp(16))
    scroll.addView(middle, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
    root.addView(scroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))

    val heading = text("●  " + getString(R.string.brulion_alarm_heading), 13f, AMBER, Typeface.create("sans-serif-medium", Typeface.NORMAL))
    heading.isAllCaps = true
    heading.letterSpacing = 0.14f
    heading.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO // the window title says it
    middle.addView(heading, wide(0))

    val clock = text(time, 76f, Color.WHITE, Typeface.create("sans-serif-thin", Typeface.NORMAL))
    clock.maxLines = 1
    clock.includeFontPadding = false
    middle.addView(clock, wide(dp(10)))

    val date = Instant.ofEpochMilli(reminder.at).atZone(ZoneId.systemDefault()).toLocalDate()
    middle.addView(text(WidgetDates.long(date, appLocale()), 15f, FAINT), wide(dp(10)))

    val name = text(reminder.title.ifBlank { getString(R.string.brulion_reminder_default_title) }, 22f, Color.WHITE)
    name.maxLines = 4
    name.ellipsize = TextUtils.TruncateAt.END
    name.setLineSpacing(0f, 1.1f)
    middle.addView(name, wide(dp(36)))

    if (reminder.body.isNotBlank() && reminder.body != reminder.title) {
      // The page the reminder is on (and a task's due date): notifications.cpp puts them there.
      val page = text(reminder.body, 15f, MUTED)
      page.maxLines = 3
      page.ellipsize = TextUtils.TruncateAt.END
      page.setCompoundDrawablesRelativeWithIntrinsicBounds(R.drawable.alarm_ic_page, 0, 0, 0)
      page.compoundDrawablePadding = dp(6)
      middle.addView(page, centred(dp(10)))
    }

    val chips = LinearLayout(this)
    chips.orientation = LinearLayout.HORIZONTAL
    chips.gravity = Gravity.CENTER
    if (reminder.canComplete) {
      chips.addView(
        chip(getString(R.string.brulion_action_done)) {
          handled = true
          ReminderActions.done(this, reminder)
          finish()
        },
        LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).also { it.marginEnd = dp(8) }
      )
    }
    chips.addView(
      chip(getString(R.string.brulion_action_open)) {
        handled = true
        ReminderActions.dismiss(this, reminder.id)
        openNote(reminder)
      },
      LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT)
    )
    middle.addView(chips, centred(dp(24)))

    if (touchExploration()) {
      root.addView(fallbackButtons(reminder), wide(dp(8)))
    } else {
      val slider = AlarmSlider(
        this,
        getString(R.string.brulion_action_snooze),
        getString(R.string.brulion_action_ok),
        getString(R.string.brulion_alarm_slide_hint)
      ) { choice ->
        when (choice) {
          SlideGesture.Choice.SNOOZE -> snooze(reminder)
          SlideGesture.Choice.OK -> dismiss(reminder)
          SlideGesture.Choice.NONE -> Unit
        }
      }
      root.addView(slider, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(AlarmSlider.HEIGHT)).also { it.topMargin = dp(8) })
      val hint = text(getString(R.string.brulion_alarm_slide_hint), 13f, FAINT)
      hint.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO // the slider's description
      root.addView(hint, wide(dp(10)))
    }
    return root
  }

  /** TalkBack's touch exploration: a drag would explore, not move the knob. */
  private fun touchExploration(): Boolean =
    (getSystemService(Context.ACCESSIBILITY_SERVICE) as? AccessibilityManager)?.isTouchExplorationEnabled == true

  /** "Snooze 10 min" and "OK" as two large buttons, in the slider's place. */
  private fun fallbackButtons(reminder: Reminder): View {
    val row = LinearLayout(this)
    row.orientation = LinearLayout.HORIZONTAL
    row.addView(
      pill(getString(R.string.brulion_action_snooze), filled = false) { snooze(reminder) },
      LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f).also { it.marginEnd = dp(6) }
    )
    row.addView(
      pill(getString(R.string.brulion_action_ok), filled = true) { dismiss(reminder) },
      LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f).also { it.marginStart = dp(6) }
    )
    return row
  }

  private fun text(value: String, sp: Float, color: Int, face: Typeface? = null): TextView {
    val view = TextView(this)
    view.text = value
    view.setTextColor(color)
    view.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp)
    view.gravity = Gravity.CENTER
    if (face != null) view.typeface = face
    return view
  }

  private fun rounded(fill: Int?, stroke: Int?): GradientDrawable {
    val shape = GradientDrawable()
    shape.cornerRadius = dp(32).toFloat()
    if (fill != null) shape.setColor(fill)
    if (stroke != null) shape.setStroke(dp(1), stroke)
    return shape
  }

  private fun button(label: String, onClick: () -> Unit): Button {
    val button = Button(this)
    button.text = label
    button.setOnClickListener { onClick() }
    button.isAllCaps = false
    button.stateListAnimator = null // no Material elevation shadow
    button.maxLines = 2
    return button
  }

  /** "Done" / "Open note": an outlined chip, a 48-dp touch target. */
  private fun chip(label: String, onClick: () -> Unit): Button {
    val button = button(label, onClick)
    button.background = RippleDrawable(ColorStateList.valueOf(PRESSED), rounded(null, CHIP_BORDER), rounded(Color.WHITE, null))
    button.setTextColor(TEXT)
    button.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
    button.minHeight = dp(48)
    button.minimumHeight = dp(48)
    button.minWidth = dp(48)
    button.minimumWidth = dp(48)
    button.setPadding(dp(18), 0, dp(18), 0)
    return button
  }

  /** A fallback answer: amber ("OK") or outlined ("Snooze"), 56 dp tall. */
  private fun pill(label: String, filled: Boolean, onClick: () -> Unit): Button {
    val button = button(label, onClick)
    val shape = if (filled) rounded(AMBER, null) else rounded(null, CHIP_BORDER)
    button.background = RippleDrawable(ColorStateList.valueOf(if (filled) PRESSED_ON_AMBER else PRESSED), shape, rounded(Color.WHITE, null))
    button.setTextColor(if (filled) INK else TEXT)
    button.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17f)
    button.typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
    button.minHeight = dp(56)
    return button
  }

  /** "en" or "pl": the language of the screen's strings (res/values or res/values-pl). */
  private fun appLocale(): String =
    if (resources.configuration.locales[0].language == "pl") "pl" else "en"

  /** The system bars and the camera cutout (edge to edge from Android 15). */
  private fun systemInsets(insets: WindowInsets): Rect =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
      val bars = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
      Rect(bars.left, bars.top, bars.right, bars.bottom)
    } else {
      @Suppress("DEPRECATION")
      Rect(insets.systemWindowInsetLeft, insets.systemWindowInsetTop, insets.systemWindowInsetRight, insets.systemWindowInsetBottom)
    }

  private fun wide(topMargin: Int): LinearLayout.LayoutParams {
    val params = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
    params.topMargin = topMargin
    return params
  }

  private fun centred(topMargin: Int): LinearLayout.LayoutParams {
    val params = LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT)
    params.topMargin = topMargin
    params.gravity = Gravity.CENTER_HORIZONTAL
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
    // Brulion's amber and ink, the dark theme's text tones (docs/UI.md, Theme.qml).
    const val AMBER = 0xFFF2B33D.toInt()
    const val INK = 0xFF1F2430.toInt()
    const val TEXT = 0xFFD3D8E2.toInt()
    const val MUTED = 0xFFA9B0BF.toInt()
    const val FAINT = 0xFF8F96A7.toInt()
    const val CHIP_BORDER = 0xFF3A404D.toInt()
    const val PRESSED = 0x33D3D8E2
    const val PRESSED_ON_AMBER = 0x331F2430
  }
}
