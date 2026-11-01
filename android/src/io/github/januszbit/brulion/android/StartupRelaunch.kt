// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

/**
 * Android recreated [BrulionActivity] before Qt had started (docs/ANDROID.md, "Aktywność
 * utworzona od nowa w trakcie startu"): a configuration change no app can handle itself
 * (`CONFIG_ASSETS_PATHS`, e.g. the overlays an emulator applies right after booting, or the
 * system's theme colours changing at that moment) relaunches the activity, and Qt 6.11's QtActivity starts the native app only from the first
 * activity's first layout. When that activity is gone before its first layout, the new one finds
 * Qt's libraries loaded (`QtLoader.LoadingResult.AlreadyLoaded`) and starts nothing — no layout,
 * a blank window for good. (A relaunch after Qt started is Qt's own case: it restarts the app.)
 * BrulionActivity then restarts the app itself, at most [MAX_RESTARTS] times in a row (counted in
 * the restarted intent, [EXTRA_RESTARTS]), so a cause that keeps coming back cannot loop.
 * Pure: the decision only (Kotlin unit test).
 */
internal object StartupRelaunch {
  /** Intent extra (Int): how many times in a row the app was restarted so. */
  const val EXTRA_RESTARTS = "io.github.januszbit.brulion.STARTUP_RESTARTS"
  const val MAX_RESTARTS = 2

  /**
   * Whether the activity just through QtActivity.onCreate has to restart the app: an earlier
   * BrulionActivity of this process got that far ([createdBefore]), this one has no content
   * ([contentViews], the children of `android.R.id.content`: Qt's layout when it started
   * something), it is not finishing (Qt finishes it when its libraries failed to load) and the
   * restarts in a row are below the limit.
   */
  fun needsRestart(createdBefore: Boolean, contentViews: Int, finishing: Boolean, restarts: Int): Boolean =
    createdBefore && contentViews == 0 && !finishing && restarts in 0 until MAX_RESTARTS
}
