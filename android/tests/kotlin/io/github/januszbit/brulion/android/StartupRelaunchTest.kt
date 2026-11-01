// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

/** The activity recreated before Qt started (plain JVM: the decision and what the restart carries). */
class StartupRelaunchTest {
  @get:Rule val tmp = TemporaryFolder()

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

  /** The tapped notification's note goes back into the restarted intent. */
  @Test
  fun noteIsCarried() {
    val note = JSONObject().put("path", "Dziennik/2026-11-01 źdźbło ąćęłńóśźż.md").put("line", 7)
    assertEquals("Dziennik/2026-11-01 źdźbło ąćęłńóśźż.md" to 7, StartupRelaunch.carriedNote(note))
    assertEquals("Witaj w Brulionie.md" to 0, StartupRelaunch.carriedNote(JSONObject().put("path", "Witaj w Brulionie.md")))
  }

  @Test
  fun nothingToCarryWithoutAPath() {
    assertNull(StartupRelaunch.carriedNote(null))
    assertNull(StartupRelaunch.carriedNote(JSONObject()))
    assertNull(StartupRelaunch.carriedNote(JSONObject().put("path", "").put("line", 3)))
    assertNull(StartupRelaunch.carriedNote(JSONObject().put("line", 3)))
  }

  /** A line that is no number (malformed JSON) opens the note at its top. */
  @Test
  fun malformedLineIsZero() {
    assertEquals("a.md" to 0, StartupRelaunch.carriedNote(JSONObject().put("path", "a.md").put("line", "żółw")))
  }

  /** A share kept for the restart comes back once, in the restarted process only. */
  @Test
  fun shareSurvivesTheRestartOnce() {
    val dir = File(tmp.root, "no_backup")
    val share = """{"subject":"Zażółć gęślą jaźń","text":"ąćęłńóśźż ĄĆĘŁŃÓŚŹŻ","dir":"/data/x/files/share-in/1-1"}"""
    assertTrue(StartupRelaunch.saveShare(dir, share))
    assertEquals(share, StartupRelaunch.takeSavedShare(dir, restarts = 1))
    assertNull(StartupRelaunch.takeSavedShare(dir, restarts = 1))
    assertFalse(File(dir, StartupRelaunch.SHARE_FILE).exists())
  }

  /** A leftover (a restart that never came) is dropped by the next ordinary start, not handed on. */
  @Test
  fun leftoverShareIsDroppedOnAnOrdinaryStart() {
    val dir = tmp.newFolder("no_backup")
    assertTrue(StartupRelaunch.saveShare(dir, """{"text":"stare"}"""))
    assertNull(StartupRelaunch.takeSavedShare(dir, restarts = 0))
    assertFalse(File(dir, StartupRelaunch.SHARE_FILE).exists())
    assertTrue(StartupRelaunch.saveShare(dir, """{"text":"x"}"""))
    assertNull(StartupRelaunch.takeSavedShare(dir, restarts = -1))
    assertFalse(File(dir, StartupRelaunch.SHARE_FILE).exists())
  }

  @Test
  fun noShareNoFile() {
    val dir = tmp.newFolder("no_backup")
    assertTrue(StartupRelaunch.saveShare(dir, null))
    assertFalse(File(dir, StartupRelaunch.SHARE_FILE).exists())
    assertNull(StartupRelaunch.takeSavedShare(dir, restarts = 1))
    assertNull(StartupRelaunch.takeSavedShare(File(tmp.root, "missing"), restarts = 1))
  }

  @Test
  fun emptyOrUnwritableFile() {
    val dir = tmp.newFolder("no_backup")
    File(dir, StartupRelaunch.SHARE_FILE).writeText("")
    assertNull(StartupRelaunch.takeSavedShare(dir, restarts = 1))
    assertFalse(File(dir, StartupRelaunch.SHARE_FILE).exists())
    // The directory is a file: nothing can be kept (the restart goes on without the share).
    val notADir = tmp.newFile("plik")
    assertFalse(StartupRelaunch.saveShare(notADir, """{"text":"ą"}"""))
  }
}
