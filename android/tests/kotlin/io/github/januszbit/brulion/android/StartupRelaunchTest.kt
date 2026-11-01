// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** The activity recreated before Qt started (plain JVM: the decision only). */
class StartupRelaunchTest {
  @Test
  fun recreatedWithoutQtRestarts() {
    assertTrue(StartupRelaunch.needsRestart(createdBefore = true, contentViews = 0, finishing = false, restarts = 0))
    assertTrue(StartupRelaunch.needsRestart(createdBefore = true, contentViews = 0, finishing = false, restarts = 1))
  }

  /** The first activity of a process is where Qt starts: never restarted. */
  @Test
  fun firstActivityNeverRestarts() {
    assertFalse(StartupRelaunch.needsRestart(createdBefore = false, contentViews = 0, finishing = false, restarts = 0))
    assertFalse(StartupRelaunch.needsRestart(createdBefore = false, contentViews = 1, finishing = false, restarts = 0))
  }

  /** Qt set up its layout in the new activity (a Qt that handles this itself): nothing to do. */
  @Test
  fun activityWithQtLayoutStays() {
    assertFalse(StartupRelaunch.needsRestart(createdBefore = true, contentViews = 1, finishing = false, restarts = 0))
    assertFalse(StartupRelaunch.needsRestart(createdBefore = true, contentViews = 3, finishing = false, restarts = 0))
  }

  /** Qt finishes the activity when its libraries failed to load: its error, not a restart. */
  @Test
  fun finishingActivityStays() {
    assertFalse(StartupRelaunch.needsRestart(createdBefore = true, contentViews = 0, finishing = true, restarts = 0))
  }

  /** At most two restarts in a row; a nonsense count (a foreign intent's extra) never restarts. */
  @Test
  fun restartsAreLimited() {
    assertEquals(2, StartupRelaunch.MAX_RESTARTS)
    assertFalse(StartupRelaunch.needsRestart(createdBefore = true, contentViews = 0, finishing = false, restarts = 2))
    assertFalse(StartupRelaunch.needsRestart(createdBefore = true, contentViews = 0, finishing = false, restarts = 99))
    assertFalse(StartupRelaunch.needsRestart(createdBefore = true, contentViews = 0, finishing = false, restarts = -1))
    assertFalse(
      StartupRelaunch.needsRestart(createdBefore = true, contentViews = 0, finishing = false, restarts = Int.MIN_VALUE),
    )
  }

  @Test
  fun extraNameIsInTheAppsNamespace() {
    assertTrue(StartupRelaunch.EXTRA_RESTARTS.startsWith("io.github.januszbit.brulion."))
  }
}
