// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** The pure part of PdfPages.kt: the JSON C++ reads (pdfpages_android.cpp) and the sizes it draws. */
class PdfJsonTest {
  @Test
  fun openedSession() {
    val json = JSONObject(PdfPages.openJson(3, 12, JSONArray().put(595).put(842), false, null))
    assertEquals(3, json.getInt("session"))
    assertEquals(12, json.getInt("pages"))
    assertEquals(595, json.getJSONArray("size").getInt(0))
    assertEquals(842, json.getJSONArray("size").getInt(1))
    assertFalse(json.getBoolean("locked"))
    assertFalse(json.has("error"))
  }

  @Test
  fun lockedOrBroken() {
    val locked = JSONObject(PdfPages.openJson(0, -1, null, true, "Hasło — zażółć"))
    assertEquals(0, locked.getInt("session"))
    assertEquals(-1, locked.getInt("pages"))
    assertTrue(locked.getBoolean("locked"))
    assertEquals("Hasło — zażółć", locked.getString("error"))
    assertFalse(locked.has("size"))
  }

  @Test
  fun matchesByStart() {
    val json = JSONArray(
      PdfPages.matchesJson(
        listOf(
          7 to listOf(floatArrayOf(10f, 20f, 60.5f, 32f)),
          40 to listOf(floatArrayOf(500f, 700f, 540f, 712f), floatArrayOf(50f, 714f, 90f, 726f))
        )
      )
    )
    assertEquals(2, json.length())
    assertEquals(7, json.getJSONObject(0).getInt("start"))
    assertEquals(60.5, json.getJSONObject(0).getJSONArray("rects").getJSONArray(0).getDouble(2), 1e-6)
    assertEquals(2, json.getJSONObject(1).getJSONArray("rects").length())
    assertEquals("[]", PdfPages.matchesJson(emptyList()))
  }

  @Test
  fun renderableSizes() {
    assertTrue(PdfPages.renderable(1240, 1754, 0, 0, 1240, 1754))
    assertTrue(PdfPages.renderable(32768, 23170, 1000, 1000, 1200, 800))
    assertFalse(PdfPages.renderable(32769, 100, 0, 0, 10, 10)) // the page too large
    assertFalse(PdfPages.renderable(32768, 23170, 0, 0, 5000, 4000)) // more than 16 MP
    assertFalse(PdfPages.renderable(100, 100, 90, 0, 20, 10)) // outside the page
    assertFalse(PdfPages.renderable(100, 100, -1, 0, 10, 10))
    assertFalse(PdfPages.renderable(100, 100, 0, 0, 0, 10))
    assertFalse(PdfPages.renderable(0, 100, 0, 0, 1, 1))
    assertFalse(PdfPages.renderable(100, 100, Int.MAX_VALUE, 0, 10, 10)) // no overflow
  }
}
