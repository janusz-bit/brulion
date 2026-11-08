// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.animation.Animator
import android.animation.AnimatorListenerAdapter
import android.animation.ObjectAnimator
import android.animation.ValueAnimator
import android.annotation.SuppressLint
import android.content.Context
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.LayerDrawable
import android.os.Build
import android.os.Bundle
import android.util.TypedValue
import android.view.Gravity
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import android.view.animation.AccelerateDecelerateInterpolator
import android.view.animation.OvershootInterpolator
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.TextView
import io.github.januszbit.brulion.R
import kotlin.math.abs

/**
 * Where a drag of the alarm slider's knob ends ([AlarmSlider]) — plain arithmetic, tested on the
 * JVM. The knob starts in the middle; [reach] is how far it can go to either side (px), an offset
 * is negative to the left (snooze) and positive to the right (OK).
 */
internal object SlideGesture {
  enum class Choice { NONE, SNOOZE, OK }

  /** The share of the reach the knob has to travel for its side to count. */
  const val THRESHOLD = 0.7f

  fun clamp(offset: Float, reach: Float): Float = if (reach <= 0f) 0f else offset.coerceIn(-reach, reach)

  /** The side the knob is past the threshold of, if any. */
  fun choice(offset: Float, reach: Float): Choice {
    if (reach <= 0f) return Choice.NONE
    val share = clamp(offset, reach) / reach
    return when {
      share <= -THRESHOLD -> Choice.SNOOZE
      share >= THRESHOLD -> Choice.OK
      else -> Choice.NONE
    }
  }

  /** 0 (in the middle) … 1 (at an end): how far the knob is from the middle. */
  fun progress(offset: Float, reach: Float): Float = if (reach <= 0f) 0f else abs(clamp(offset, reach)) / reach
}

/**
 * The alarm screen's slider: an amber knob with a bell in the middle of a track, "‹ Snooze" on
 * its left and "OK ›" on its right. Dragged past [SlideGesture.THRESHOLD] of the way to a side and
 * let go, it answers the alarm with that side ([onChoice], once); let go before, it springs back.
 * A tap does nothing, so the alarm is not answered in a pocket or by a sleepy brush. Snooze stays
 * on the left also in a right-to-left language, matching the arrows.
 *
 * Without a drag: the left and right arrow keys choose a side, and accessibility services get
 * the two answers as actions of the slider. (The alarm screen shows plain buttons instead of the
 * slider while TalkBack's touch exploration is on — [AlarmActivity].) While the phone's
 * animations are on, the knob nudges to both sides now and then to show that it moves.
 */
@SuppressLint("ViewConstructor") // built in code only (AlarmActivity), never inflated from XML
internal class AlarmSlider(
  context: Context,
  private val snoozeLabel: String,
  private val okLabel: String,
  hint: String,
  private val onChoice: (SlideGesture.Choice) -> Unit
) : FrameLayout(context) {
  private val knobSize = dp(KNOB)
  private val knob = FrameLayout(context)
  private val left = label("‹  $snoozeLabel", Gravity.START or Gravity.CENTER_VERTICAL)
  private val right = label("$okLabel  ›", Gravity.END or Gravity.CENTER_VERTICAL)
  private val animated = ValueAnimator.areAnimatorsEnabled() // off: the phone's "Remove animations"
  private var nudge: ObjectAnimator? = null

  /** How far the knob goes to either side (px). */
  private var reach = 0f

  /** The finger's x minus the knob's offset when the drag started. */
  private var grab = 0f
  private var dragging = false

  /** Where the finger went down, and whether it has moved from there since (else: a tap). */
  private var downX = 0f
  private var moved = false
  private val touchSlop = ViewConfiguration.get(context).scaledTouchSlop

  /** The side the knob is past the threshold of during a drag (one haptic tick on entering it). */
  private var armed = SlideGesture.Choice.NONE

  /** A side was chosen: the slider ignores everything after it. */
  private var chosen = false

  init {
    layoutDirection = View.LAYOUT_DIRECTION_LTR
    val track = GradientDrawable()
    track.cornerRadius = dp(HEIGHT).toFloat()
    track.setColor(TRACK)
    track.setStroke(dp(1), TRACK_BORDER)
    background = track
    contentDescription = hint
    isFocusable = true

    addView(left, LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.MATCH_PARENT, Gravity.START).also {
      it.leftMargin = dp(LABEL_MARGIN)
    })
    addView(right, LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.MATCH_PARENT, Gravity.END).also {
      it.rightMargin = dp(LABEL_MARGIN)
    })

    val halo = GradientDrawable()
    halo.shape = GradientDrawable.OVAL
    halo.setColor(HALO)
    val disc = GradientDrawable()
    disc.shape = GradientDrawable.OVAL
    disc.setColor(AMBER)
    val face = LayerDrawable(arrayOf(halo, disc))
    face.setLayerInset(1, dp(6), dp(6), dp(6), dp(6))
    knob.background = face
    knob.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
    val bell = ImageView(context)
    bell.setImageResource(R.drawable.alarm_ic_bell)
    knob.addView(bell, LayoutParams(dp(BELL), dp(BELL), Gravity.CENTER))
    addView(knob, LayoutParams(knobSize, knobSize, Gravity.CENTER))
  }

  private fun dp(value: Int): Int =
    TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value.toFloat(), resources.displayMetrics).toInt()

  private fun label(text: String, gravity: Int): TextView {
    val view = TextView(context)
    view.text = text
    view.gravity = gravity
    view.setTextColor(LABEL)
    view.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
    view.maxLines = 2
    view.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
    return view
  }

  override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
    super.onSizeChanged(w, h, oldw, oldh)
    val inset = (h - knobSize) / 2f
    reach = ((w - knobSize) / 2f - inset).coerceAtLeast(0f)
    // A label keeps to its half, beside the knob at rest.
    val room = ((w - knobSize) / 2 - dp(LABEL_MARGIN) - dp(8)).coerceAtLeast(0)
    left.maxWidth = room
    right.maxWidth = room
  }

  override fun onAttachedToWindow() {
    super.onAttachedToWindow()
    startNudge()
  }

  override fun onDetachedFromWindow() {
    stopNudge()
    knob.animate().cancel()
    super.onDetachedFromWindow()
  }

  override fun onTouchEvent(event: MotionEvent): Boolean {
    if (chosen) return true
    when (event.actionMasked) {
      MotionEvent.ACTION_DOWN -> {
        if (!onKnob(event.x)) return false
        stopNudge()
        knob.animate().cancel()
        grab = event.x - knob.translationX
        downX = event.x
        moved = false
        dragging = true
        parent?.requestDisallowInterceptTouchEvent(true)
      }
      MotionEvent.ACTION_MOVE -> if (dragging) {
        if (abs(event.x - downX) > touchSlop) moved = true
        moveTo(event.x - grab)
      }
      MotionEvent.ACTION_UP -> if (dragging) {
        dragging = false
        val choice = SlideGesture.choice(knob.translationX, reach)
        when {
          choice != SlideGesture.Choice.NONE -> choose(choice)
          moved -> springBack()
          else -> performClick()
        }
      }
      MotionEvent.ACTION_CANCEL -> if (dragging) {
        dragging = false
        springBack()
      }
    }
    return true
  }

  /** A tap on the knob answers nothing: it shakes, to show that it has to be slid. */
  override fun performClick(): Boolean {
    val handled = super.performClick()
    if (chosen) return handled
    left.alpha = 1f
    right.alpha = 1f
    knob.translationX = 0f
    if (!animated) return true
    val step = dp(NUDGE).toFloat()
    val shake = ObjectAnimator.ofFloat(knob, View.TRANSLATION_X, 0f, -step, step, -step / 2f, step / 2f, 0f)
    shake.duration = SHAKE_MS
    shake.addListener(object : AnimatorListenerAdapter() {
      override fun onAnimationEnd(animation: Animator) {
        // Not after stopNudge(): it let go of the shake before cancelling it.
        if (nudge !== animation) return
        nudge = null
        startNudge()
      }
    })
    nudge = shake
    shake.start()
    return true
  }

  /** The finger is on the knob (with some slack around it). */
  private fun onKnob(x: Float): Boolean {
    val centre = width / 2f + knob.translationX
    return abs(x - centre) <= knobSize / 2f + dp(KNOB_SLACK)
  }

  private fun moveTo(offset: Float) {
    val x = SlideGesture.clamp(offset, reach)
    knob.translationX = x
    // The side the knob moves away from fades.
    val share = SlideGesture.progress(x, reach)
    left.alpha = if (x > 0f) 1f - share else 1f
    right.alpha = if (x < 0f) 1f - share else 1f
    val choice = SlideGesture.choice(x, reach)
    if (choice != armed) {
      armed = choice
      if (choice != SlideGesture.Choice.NONE) {
        performHapticFeedback(
          if (Build.VERSION.SDK_INT >= 34) HapticFeedbackConstants.GESTURE_THRESHOLD_ACTIVATE
          else HapticFeedbackConstants.CLOCK_TICK
        )
      }
    }
  }

  private fun springBack() {
    armed = SlideGesture.Choice.NONE
    left.alpha = 1f
    right.alpha = 1f
    if (!animated) {
      knob.translationX = 0f
      return
    }
    knob.animate()
      .translationX(0f)
      .setDuration(SPRING_MS)
      .setInterpolator(OvershootInterpolator(1.4f))
      .withEndAction { startNudge() }
      .start()
  }

  /** Answers the alarm with [choice]: the knob goes to its end first. */
  private fun choose(choice: SlideGesture.Choice) {
    if (chosen || choice == SlideGesture.Choice.NONE) return
    chosen = true
    stopNudge()
    performHapticFeedback(
      if (Build.VERSION.SDK_INT >= 30) HapticFeedbackConstants.CONFIRM else HapticFeedbackConstants.VIRTUAL_KEY
    )
    val end = if (choice == SlideGesture.Choice.SNOOZE) -reach else reach
    left.alpha = if (choice == SlideGesture.Choice.SNOOZE) 1f else 0f
    right.alpha = if (choice == SlideGesture.Choice.OK) 1f else 0f
    if (!animated) {
      knob.translationX = end
      onChoice(choice)
      return
    }
    knob.animate().translationX(end).setDuration(FINISH_MS).withEndAction { onChoice(choice) }.start()
  }

  private fun startNudge() {
    if (!animated || chosen || dragging || nudge != null) return
    val step = dp(NUDGE).toFloat()
    val animator = ObjectAnimator.ofFloat(knob, View.TRANSLATION_X, 0f, -step, 0f, step, 0f, 0f, 0f)
    animator.duration = NUDGE_MS
    animator.repeatCount = ValueAnimator.INFINITE
    animator.interpolator = AccelerateDecelerateInterpolator()
    animator.start()
    nudge = animator
  }

  private fun stopNudge() {
    val running = nudge
    nudge = null
    running?.cancel()
  }

  override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean =
    when (keyCode) {
      KeyEvent.KEYCODE_DPAD_LEFT -> {
        choose(SlideGesture.Choice.SNOOZE)
        true
      }
      KeyEvent.KEYCODE_DPAD_RIGHT -> {
        choose(SlideGesture.Choice.OK)
        true
      }
      else -> super.onKeyDown(keyCode, event)
    }

  override fun onInitializeAccessibilityNodeInfo(info: AccessibilityNodeInfo) {
    super.onInitializeAccessibilityNodeInfo(info)
    info.addAction(AccessibilityNodeInfo.AccessibilityAction(R.id.brulion_alarm_action_snooze, snoozeLabel))
    info.addAction(AccessibilityNodeInfo.AccessibilityAction(R.id.brulion_alarm_action_ok, okLabel))
  }

  override fun performAccessibilityAction(action: Int, arguments: Bundle?): Boolean =
    when (action) {
      R.id.brulion_alarm_action_snooze -> {
        choose(SlideGesture.Choice.SNOOZE)
        true
      }
      R.id.brulion_alarm_action_ok -> {
        choose(SlideGesture.Choice.OK)
        true
      }
      else -> super.performAccessibilityAction(action, arguments)
    }

  companion object {
    /** The track's height (dp): the knob and its halo with a 4-dp gap. */
    const val HEIGHT = 68

    private const val KNOB = 60
    private const val BELL = 26
    private const val KNOB_SLACK = 12
    private const val LABEL_MARGIN = 20
    private const val NUDGE = 14
    private const val NUDGE_MS = 3200L
    private const val SPRING_MS = 320L
    private const val SHAKE_MS = 450L
    private const val FINISH_MS = 140L

    private const val AMBER = 0xFFF2B33D.toInt()
    private const val HALO = 0x2EF2B33D
    private const val TRACK = 0xFF12151C.toInt()
    private const val TRACK_BORDER = 0xFF262A32.toInt()
    private const val LABEL = 0xFFA9B0BF.toInt()
  }
}
