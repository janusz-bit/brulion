// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The background check's decisions (BackgroundCheck.kt): what counts as a target, which results are
 * armed, the status kept for the settings page. The JSON is what C++ sends
 * (src/app/background/backgroundcheck.h).
 */
class BackgroundCheckTest {
  private val targetJson =
    JSONObject()
      .put("root", "/storage/emulated/0/Notatki — Łódź")
      .put("stateDir", "/data/user/0/io.github.januszbit.brulion/files/vaults/abc")
      .put("vault", "notatnik-dom")
      .put("ringMinutes", 10)
      .put("language", "pl")
      .put("intervalMinutes", 30)
      .toString()

  private fun result(ok: Boolean = true, vault: String = "notatnik-dom", reminders: Int = 2): JSONObject {
    val items = JSONArray()
    for (i in 1..reminders) {
      items.put(
        JSONObject()
          .put("id", i)
          .put("title", "Spotkanie z Łucją $i")
          .put("at", 1_792_000_000_000L + i)
          .put("alarm", false)
      )
    }
    return JSONObject()
      .put("ok", ok)
      .put("vault", vault)
      .put("reminders", items)
      .put("sync", "ok")
      .put("notes", 42)
      .put("elapsedMs", 812)
  }

  @Test
  fun targetNeedsANotebookAndAnInterval() {
    val target = BackgroundCheck.Target.parse(targetJson)
    assertNotNull(target)
    assertEquals("notatnik-dom", target!!.vault)
    assertEquals(30, target.intervalMinutes)
    assertEquals(targetJson, target.json)

    assertNull(BackgroundCheck.Target.parse(null))
    assertNull(BackgroundCheck.Target.parse(""))
    assertNull(BackgroundCheck.Target.parse("{}"))
    assertNull(BackgroundCheck.Target.parse("nie json"))
    assertNull(BackgroundCheck.Target.parse(JSONObject(targetJson).put("intervalMinutes", 0).toString()))
    assertNull(BackgroundCheck.Target.parse(JSONObject(targetJson).put("vault", "").toString()))
    assertNull(BackgroundCheck.Target.parse(JSONObject(targetJson).apply { remove("root") }.toString()))
  }

  @Test
  fun targetIntervalIsAtLeastWhatAndroidAllows() {
    val target = BackgroundCheck.Target.parse(JSONObject(targetJson).put("intervalMinutes", 5).toString())
    assertEquals(BackgroundCheck.MIN_INTERVAL_MINUTES, target!!.intervalMinutes)
  }

  @Test
  fun aGoodResultOfTheCurrentNotebookIsArmed() {
    val target = BackgroundCheck.Target.parse(targetJson)
    assertEquals(BackgroundCheck.Verdict.APPLY, BackgroundCheck.verdict(result(), target, appTookOver = false))
    // No reminders at all is a result too: they were deleted on the computer.
    assertEquals(BackgroundCheck.Verdict.APPLY, BackgroundCheck.verdict(result(reminders = 0), target, false))
  }

  @Test
  fun aFailedCheckArmsNothing() {
    val target = BackgroundCheck.Target.parse(targetJson)
    assertEquals(BackgroundCheck.Verdict.FAILED, BackgroundCheck.verdict(null, target, false))
    assertEquals(BackgroundCheck.Verdict.FAILED, BackgroundCheck.verdict(result(ok = false), target, false))
    val noList = result().apply { remove("reminders") }
    assertEquals(BackgroundCheck.Verdict.FAILED, BackgroundCheck.verdict(noList, target, false))
    // An empty folder answers ok = false in C++ ("empty"): never "no reminders".
    val empty = JSONObject().put("ok", false).put("error", "empty").put("vault", "notatnik-dom")
    assertEquals(BackgroundCheck.Verdict.FAILED, BackgroundCheck.verdict(empty, target, false))
  }

  @Test
  fun aResultOfAnotherNotebookOrOfACheckTurnedOffIsStale() {
    val target = BackgroundCheck.Target.parse(targetJson)
    assertEquals(BackgroundCheck.Verdict.STALE, BackgroundCheck.verdict(result(vault = "inny"), target, false))
    assertEquals(BackgroundCheck.Verdict.STALE, BackgroundCheck.verdict(result(), null, false))
  }

  @Test
  fun theAppWinsWhenItCameToTheFront() {
    val target = BackgroundCheck.Target.parse(targetJson)
    assertEquals(BackgroundCheck.Verdict.APP_TOOK_OVER, BackgroundCheck.verdict(result(), target, appTookOver = true))
  }

  @Test
  fun statusSaysWhatHappened() {
    val status = BackgroundCheck.statusOf(result(reminders = 3), BackgroundCheck.Verdict.APPLY, 1_792_000_000_000L)
    assertEquals(1_792_000_000_000L, status.getLong("at"))
    assertTrue(status.getBoolean("ok"))
    assertTrue(status.getBoolean("applied"))
    assertEquals("apply", status.getString("verdict"))
    assertEquals(3, status.getInt("reminders"))
    assertEquals("ok", status.getString("sync"))
    assertEquals(42, status.getInt("notes"))
    assertEquals(812L, status.getLong("elapsedMs"))
    assertFalse(status.has("error"))

    val failed =
      BackgroundCheck.statusOf(
        JSONObject().put("ok", false).put("error", "missing").put("sync", "off"),
        BackgroundCheck.Verdict.FAILED,
        5L
      )
    assertFalse(failed.getBoolean("ok"))
    assertFalse(failed.getBoolean("applied"))
    assertEquals("missing", failed.getString("error"))
    assertEquals(0, failed.getInt("reminders"))

    val none = BackgroundCheck.statusOf(null, BackgroundCheck.Verdict.FAILED, 5L)
    assertFalse(none.getBoolean("ok"))
    assertEquals("off", none.getString("sync"))
  }
}
