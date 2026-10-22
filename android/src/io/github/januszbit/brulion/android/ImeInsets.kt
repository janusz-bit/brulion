// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.app.Activity
import android.os.Build
import android.util.Log
import android.view.View
import android.view.ViewGroup
import android.view.ViewTreeObserver
import android.view.WindowInsets
import android.view.WindowInsetsAnimation
import androidx.annotation.RequiresApi
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import java.lang.ref.WeakReference

/**
 * How much of Qt's view the on-screen keyboard (IME) covers, for C++'s `KeyboardInsets`
 * (src/app/ui/keyboardinsets.h): the shell keeps the editing toolbar, bottom sheets and toasts on
 * the keyboard's top edge with it (docs/ANDROID.md, "Klawiatura ekranowa").
 *
 * [BrulionActivity] runs with `adjustResize` (AndroidManifest.xml: Qt never pans the window).
 * Whether the system then resizes the window (Android 9–14 without edge-to-edge) or only hands the
 * keyboard's insets to an edge-to-edge window (always on Android 15+), the overlap is measured on
 * screen — the bottom of Qt's view minus the keyboard's top —, so it is 0 in the first case and the
 * keyboard's height in the second.
 *
 * An invisible 0×0 view of its own, a child of the window's decor view, observes; Qt's own inset
 * listeners (SafeArea) stay untouched and nothing is consumed:
 * * its OnApplyWindowInsetsListener: the settled value;
 * * its WindowInsetsAnimation.Callback (Android 11+): every frame of the keyboard's slide;
 * * a global layout listener on the decor view: a system that handed the insets to Qt's view
 *   only (Android 9–10 stop at the first view that consumes them).
 * Reports go to C++ through [BrulionAndroid.imeChanged] (physical pixels), only when they change.
 * Main thread only.
 */
internal class ImeInsets private constructor(private val activity: Activity) {
  private val observer = View(activity)
  private var animating = false
  private var last: Report? = null
  private val layoutListener = ViewTreeObserver.OnGlobalLayoutListener { if (!animating) reportNow(false) }

  private data class Report(val overlap: Int, val shown: Boolean, val animating: Boolean)

  private fun attach() {
    val decor = activity.window.decorView as ViewGroup
    observer.visibility = View.INVISIBLE
    decor.addView(observer, ViewGroup.LayoutParams(0, 0))
    observer.setOnApplyWindowInsetsListener { view, insets ->
      // While the keyboard slides, the end state arrives here first: the frames come from the
      // animation callback.
      if (!animating) report(WindowInsetsCompat.toWindowInsetsCompat(insets, view), false, false)
      insets
    }
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) followAnimation()
    decor.viewTreeObserver.addOnGlobalLayoutListener(layoutListener)
  }

  private fun detach() {
    val decor = activity.window.decorView as ViewGroup
    decor.viewTreeObserver.removeOnGlobalLayoutListener(layoutListener)
    observer.setOnApplyWindowInsetsListener(null)
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) observer.setWindowInsetsAnimationCallback(null)
    decor.removeView(observer)
  }

  @RequiresApi(Build.VERSION_CODES.R)
  private fun followAnimation() {
    observer.setWindowInsetsAnimationCallback(
      object : WindowInsetsAnimation.Callback(DISPATCH_MODE_CONTINUE_ON_SUBTREE) {
        private fun isIme(animation: WindowInsetsAnimation) =
          (animation.typeMask and WindowInsets.Type.ime()) != 0

        override fun onPrepare(animation: WindowInsetsAnimation) {
          if (isIme(animation)) animating = true
        }

        override fun onProgress(
          insets: WindowInsets,
          runningAnimations: MutableList<WindowInsetsAnimation>,
        ): WindowInsets {
          if (animating && runningAnimations.any { isIme(it) }) {
            report(WindowInsetsCompat.toWindowInsetsCompat(insets, observer), true, false)
          }
          return insets
        }

        override fun onEnd(animation: WindowInsetsAnimation) {
          if (!isIme(animation)) return
          animating = false
          reportNow(false)
        }
      },
    )
  }

  /** The current state from the window's insets. */
  private fun reportNow(force: Boolean) {
    val insets = ViewCompat.getRootWindowInsets(observer) ?: return
    report(insets, false, force)
  }

  private fun report(insets: WindowInsetsCompat, moving: Boolean, force: Boolean) {
    val ime = insets.getInsets(WindowInsetsCompat.Type.ime()).bottom
    val shown = insets.isVisible(WindowInsetsCompat.Type.ime())
    val decor = activity.window.decorView
    val qtView = qtView()
    val decorOnScreen = IntArray(2)
    val viewOnScreen = IntArray(2)
    decor.getLocationOnScreen(decorOnScreen)
    qtView.getLocationOnScreen(viewOnScreen)
    val value =
      overlap(
        viewTop = viewOnScreen[1],
        viewBottom = viewOnScreen[1] + qtView.height,
        windowBottom = decorOnScreen[1] + decor.height,
        imeBottom = ime,
      )
    val next = Report(value, shown, moving)
    if (!force && next == last) return
    // Every report, frames of a slide included: `adb shell setprop log.tag.BrulionIme DEBUG`.
    if (Log.isLoggable(TAG, Log.DEBUG)) {
      Log.d(TAG, "ime ${ime}px, view ${viewOnScreen[1]}..${viewOnScreen[1] + qtView.height}: overlap $value shown=$shown animating=$moving")
    }
    if (BrulionAndroid.imeChanged(next.overlap, next.shown, next.animating)) last = next
  }

  /** Qt's view: the activity's content (QtActivity sets one view), else the content frame. */
  private fun qtView(): View {
    val content = activity.findViewById<ViewGroup>(android.R.id.content) ?: return activity.window.decorView
    for (i in 0 until content.childCount) {
      val child = content.getChildAt(i)
      if (child !== observer && child.visibility == View.VISIBLE && child.height > 0) return child
    }
    return content
  }

  companion object {
    private const val TAG = "BrulionIme"

    /** The observer of the running activity (its listeners keep it alive with the activity). */
    private var current: WeakReference<ImeInsets>? = null

    /** Starts observing [activity]'s window (its onCreate). */
    fun install(activity: Activity) {
      try {
        current?.get()?.detach()
        current = WeakReference(ImeInsets(activity).also { it.attach() })
      } catch (e: RuntimeException) {
        // C++ falls back to Qt's keyboard rectangle.
        Log.w(TAG, "the keyboard observer could not be installed", e)
        current = null
      }
    }

    /** [activity] is going away (its onDestroy). */
    fun uninstall(activity: Activity) {
      val insets = current?.get() ?: return
      if (insets.activity !== activity) return
      try {
        insets.detach()
      } catch (e: RuntimeException) {
        Log.w(TAG, "the keyboard observer could not be removed", e)
      }
      current = null
    }

    /** Reports the current state again (C++ attached or asked for it). */
    fun reportAgain() {
      current?.get()?.let { if (!it.animating) it.reportNow(true) }
    }

    /**
     * Physical pixels of a view (screen rows [viewTop]…[viewBottom]) below the top of a keyboard
     * covering [imeBottom] pixels of a window that ends at [windowBottom]: 0 when the keyboard is
     * hidden or the view ends above it (the system resized the window), at most the view's height.
     */
    @JvmStatic
    fun overlap(viewTop: Int, viewBottom: Int, windowBottom: Int, imeBottom: Int): Int {
      if (imeBottom <= 0 || viewBottom <= viewTop) return 0
      val imeTop = windowBottom - imeBottom
      return (viewBottom - maxOf(imeTop, viewTop)).coerceIn(0, viewBottom - viewTop)
    }
  }
}
