// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONObject
import java.io.File
import java.io.IOException

/**
 * Android recreated [BrulionActivity] before Qt had started (docs/ANDROID.md, "Activity
 * recreated during startup"): a configuration change no app can handle itself
 * (`CONFIG_ASSETS_PATHS`, e.g. the overlays an emulator applies right after booting, or the
 * system's theme colours changing at that moment) relaunches the activity, and Qt 6.11's QtActivity starts the native app only from the first
 * activity's first layout. When that activity is gone before its first layout, the new one finds
 * Qt's libraries loaded (`QtLoader.LoadingResult.AlreadyLoaded`) and starts nothing — no layout,
 * a blank window for good. (A relaunch after Qt started is Qt's own case: it restarts the app.)
 * BrulionActivity then restarts the app itself, at most [MAX_RESTARTS] times in a row (counted in
 * the restarted intent, [EXTRA_RESTARTS]), so a cause that keeps coming back cannot loop. What
 * the start carried and only the dying process holds goes along: the note of a tapped notification
 * (the first activity took it out of the intent, which Android hands unchanged to the recreated one)
 * back into the restarted intent ([carriedNote]), a share of another app ([ShareActivity], in memory
 * only: never an intent extra another app could forge) into a private file ([saveShare],
 * [takeSavedShare]). The decision and these pieces are plain JVM code (Kotlin unit test).
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

  /** The private file ([saveShare]) under the app's no-backup files directory. */
  const val SHARE_FILE = "startup-restart-share.json"

  /**
   * The note of a tapped notification kept for C++ (`{"path": "...", "line": n}`) as the
   * restarted intent's extras (`Extras.PATH`, `Extras.LINE`), or null: none, or no path.
   */
  fun carriedNote(activation: JSONObject?): Pair<String, Int>? {
    if (activation == null) return null
    val path = activation.optString("path", "")
    if (path.isEmpty()) return null
    return path to activation.optInt("line", 0)
  }

  /** Keeps the share not delivered yet ([share], JSON; null: nothing) for the restarted process. */
  fun saveShare(dir: File, share: String?): Boolean {
    if (share == null) return true
    return try {
      dir.mkdirs()
      File(dir, SHARE_FILE).writeText(share, Charsets.UTF_8)
      true
    } catch (e: IOException) {
      false
    }
  }

  /**
   * The share [saveShare] kept, when this process is such a restart ([restarts] > 0, the intent's
   * [EXTRA_RESTARTS]); null otherwise. The file goes either way: a leftover of a restart that
   * never came is not handed to a later, unrelated start.
   */
  fun takeSavedShare(dir: File, restarts: Int): String? {
    val file = File(dir, SHARE_FILE)
    if (!file.isFile) return null
    val share =
      try {
        if (restarts > 0) file.readText(Charsets.UTF_8).takeIf { it.isNotEmpty() } else null
      } catch (e: IOException) {
        null
      }
    file.delete()
    return share
  }
}
