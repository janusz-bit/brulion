// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The camera's photos kept over the end of the app's process (CameraPhotos.kt, written by
 * MediaCapture.kt; read by platform::capturedPhotos in C++): begun before the camera starts,
 * answered in whichever process gets the answer, handed out once per notebook, old ones let go.
 */
class CameraPhotosTest {
  private val day = 86_400_000L
  private val start = 1_795_000_000_000L

  private fun record(n: Int, notebook: String = "notatnik-dom", purpose: String = "AI/2026-11-27 Roślina.md") =
    PhotoRecord("/data/cache/camera/photo-$n.jpg", notebook, purpose, start + n)

  /** The state after a round trip through photos.json: what the next process reads. */
  private fun reread(photos: CameraPhotos) = CameraPhotos.parse(photos.toJson())

  @Test
  fun aPhotoBegunInOneProcessIsTakenInTheNext() {
    val begun = CameraPhotos().begin(record(1))
    assertTrue(begun.gone.isEmpty())
    // The process ends while the camera app is in front: the next one reads the file.
    val answer = reread(begun.photos).answer(true) { 2_048L }
    assertEquals(record(1), answer.kept)
    assertEquals("AI/2026-11-27 Roślina.md", answer.purpose)
    assertNull(answer.photos.pending)
    assertTrue(answer.gone.isEmpty())
    val (handed, rest) = reread(answer.photos).handOut("notatnik-dom")
    assertEquals(listOf(record(1)), handed)
    assertEquals(CameraPhotos(), rest)
    // Handed out once: a second take finds nothing.
    assertTrue(rest.handOut("notatnik-dom").first.isEmpty())
  }

  @Test
  fun aPhotoNotTakenIsLetGo() {
    val begun = CameraPhotos().begin(record(1)).photos
    for ((ok, bytes) in listOf(false to 2_048L, true to 0L, false to 0L)) {
      val answer = begun.answer(ok) { bytes }
      assertNull(answer.kept)
      assertEquals(CameraPhotos(), answer.photos)
      assertEquals(listOf(record(1).file), answer.gone)
      assertEquals("AI/2026-11-27 Roślina.md", answer.purpose)
    }
    // An answer with nothing pending (it came already, or the record expired): no change.
    val none = CameraPhotos().answer(true) { 2_048L }
    assertNull(none.kept)
    assertEquals(CameraPhotos(), none.photos)
    assertTrue(none.gone.isEmpty())
    assertEquals("", none.purpose)
  }

  @Test
  fun aNewPhotoReplacesOneNeverAnswered() {
    val first = CameraPhotos().begin(record(1)).photos
    val second = first.begin(record(2))
    assertEquals(record(2), second.photos.pending)
    assertEquals(listOf(record(1).file), second.gone)
    // Taken photos waiting for C++ stay.
    val taken = CameraPhotos(record(2), listOf(record(1))).begin(record(3))
    assertEquals(listOf(record(1)), taken.photos.taken)
    assertEquals(listOf(record(2).file), taken.gone)
  }

  @Test
  fun eachNotebookTakesItsOwnPhotos() {
    var photos = CameraPhotos()
    for ((n, notebook) in listOf(1 to "notatnik-dom", 2 to "praca", 3 to "", 4 to "notatnik-dom")) {
      photos = photos.begin(record(n, notebook)).photos.answer(true) { 1L }.photos
    }
    val (home, rest) = photos.handOut("notatnik-dom")
    assertEquals(listOf(1, 3, 4).map { record(it, if (it == 3) "" else "notatnik-dom") }, home)
    assertEquals(listOf(record(2, "praca")), rest.taken)
  }

  @Test
  fun theSamePhotoAnsweredTwiceIsKeptOnce() {
    val begun = CameraPhotos(record(1), listOf(record(1)))
    assertEquals(listOf(record(1)), begun.answer(true) { 1L }.photos.taken)
  }

  @Test
  fun atMostSoManyTakenPhotosWait() {
    var photos = CameraPhotos()
    var gone = emptyList<String>()
    for (n in 1..CameraPhotos.MAX_TAKEN + 2) {
      val answer = photos.begin(record(n)).photos.answer(true) { 1L }
      photos = answer.photos
      gone = gone + answer.gone
    }
    assertEquals(CameraPhotos.MAX_TAKEN, photos.taken.size)
    assertEquals(record(3), photos.taken.first())
    assertEquals(listOf(record(1).file, record(2).file), gone)
  }

  @Test
  fun oldRecordsAndStrayPhotosGo() {
    val photos = CameraPhotos(record(5), listOf(record(1), record(4)))
    // Nothing is old yet.
    assertEquals(CameraPhotos.Change(photos), photos.expire(start + 10))
    // A day on: the photo the camera never answered; a month on: those whose notebook never came.
    val later = photos.expire(start + 5 + day + 1)
    assertEquals(CameraPhotos(null, listOf(record(1), record(4))), later.photos)
    assertEquals(listOf(record(5).file), later.gone)
    val month = photos.expire(start + 4 + CameraPhotos.TAKEN_MS + 1)
    assertEquals(CameraPhotos(), month.photos)
    assertEquals(listOf(record(5).file, record(1).file, record(4).file), month.gone)
    // Files of the folder no record names, older than a day: an older version's or a lost record's.
    val files = mapOf(
      record(1).file to start - 2 * day,
      "/data/cache/camera/photo-stary.jpg" to start - 2 * day,
      "/data/cache/camera/photo-świeży.jpg" to start,
    )
    assertEquals(listOf("/data/cache/camera/photo-stary.jpg"), CameraPhotos.strays(files, photos, start + 10))
  }

  @Test
  fun brokenRecordsAreLeftOut() {
    for (raw in listOf(null, "", "nie json", "[]", "{\"taken\": 5}")) assertEquals(CameraPhotos(), CameraPhotos.parse(raw))
    val raw = JSONObject()
      .put("pending", JSONObject().put("notebook", "bez pliku"))
      .put("taken", org.json.JSONArray().put(record(1).toJson()).put("tekst").put(JSONObject().put("file", "")))
      .toString()
    assertEquals(CameraPhotos(null, listOf(record(1))), CameraPhotos.parse(raw))
    // What C++ reads: file, notebook, purpose (Polish text kept).
    val json = record(7, "notatnik-ąę", "AI/Żółw.md").toJson()
    assertEquals("AI/Żółw.md", json.getString("purpose"))
    assertEquals(record(7, "notatnik-ąę", "AI/Żółw.md"), PhotoRecord.fromJson(JSONObject(json.toString())))
  }
}
