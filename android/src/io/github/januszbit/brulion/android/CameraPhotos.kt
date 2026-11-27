// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

/**
 * A photo asked of the camera app: its [file] (in the app's cache, written by the camera app),
 * what asked for it — the notebook [notebook] (C++'s Vault::id) and the caller's [purpose] (the AI
 * chat: the draft the photo goes into) — and when it was asked ([started], ms since the epoch).
 */
internal data class PhotoRecord(val file: String, val notebook: String, val purpose: String, val started: Long) {
  fun toJson(): JSONObject =
    JSONObject().put("file", file).put("notebook", notebook).put("purpose", purpose).put("started", started)

  companion object {
    /** Null without a file. */
    fun fromJson(json: JSONObject?): PhotoRecord? {
      val file = json?.optString("file", "") ?: return null
      if (file.isEmpty()) return null
      return PhotoRecord(file, json.optString("notebook", ""), json.optString("purpose", ""), json.optLong("started", 0L))
    }
  }
}

/**
 * The camera's photos over the end of the app's process (docs/ANDROID.md "Camera and dictation"):
 * Android may end it while the camera app is in front, and the camera's answer then comes to a new
 * process, in which nothing of the first one is left but files. So [MediaCapture] writes this
 * (cache/camera/photos.json) before the camera app starts — the photo being taken, [pending], at
 * most one — and turns it into a [taken] photo when the answer comes, whichever process gets it;
 * C++ takes the taken photos of its notebook once ([handOut]). Plain JVM code: each change is the
 * new state and the photo files it lets go (to delete).
 */
internal data class CameraPhotos(val pending: PhotoRecord? = null, val taken: List<PhotoRecord> = emptyList()) {
  /** The state after a change and the photo files no longer wanted. */
  data class Change(val photos: CameraPhotos, val gone: List<String> = emptyList())

  /** The answer of the camera app applied: the photo kept for C++ ([kept]) or none. */
  data class Answer(val photos: CameraPhotos, val kept: PhotoRecord?, val purpose: String, val gone: List<String>)

  /** [record] is being taken now; an earlier photo the camera never answered is let go. */
  fun begin(record: PhotoRecord): Change = Change(copy(pending = record), listOfNotNull(pending?.file))

  /**
   * The camera app answered the pending photo: kept for C++ when it said [ok] and the camera wrote
   * the file ([bytes] of it > 0), else let go. No pending photo (its answer came already, or it
   * expired): nothing changes.
   */
  fun answer(ok: Boolean, bytes: (String) -> Long): Answer {
    val photo = pending ?: return Answer(this, null, "", emptyList())
    if (!ok || bytes(photo.file) <= 0) return Answer(copy(pending = null), null, photo.purpose, listOf(photo.file))
    val all = taken.filter { it.file != photo.file } + photo
    val kept = all.takeLast(MAX_TAKEN)
    return Answer(CameraPhotos(null, kept), photo, photo.purpose, (all - kept.toSet()).map { it.file })
  }

  /** The taken photos of [notebook] — and those of no notebook — handed out (oldest first); the others stay. */
  fun handOut(notebook: String): Pair<List<PhotoRecord>, CameraPhotos> {
    val (out, kept) = taken.partition { it.notebook.isEmpty() || it.notebook == notebook }
    return out to copy(taken = kept)
  }

  /**
   * Records older than [now] allows let go: the pending one after [PENDING_MS] (the camera never
   * answered: its process went with the photo's answer), a taken one after [TAKEN_MS] (its
   * notebook never opened again).
   */
  fun expire(now: Long): Change {
    val stalePending = pending?.takeIf { now - it.started > PENDING_MS }
    val (old, fresh) = taken.partition { now - it.started > TAKEN_MS }
    return Change(CameraPhotos(if (stalePending != null) null else pending, fresh),
      listOfNotNull(stalePending?.file) + old.map { it.file })
  }

  /** Every photo file a record names. */
  fun files(): Set<String> = (listOfNotNull(pending?.file) + taken.map { it.file }).toSet()

  fun toJson(): String {
    val json = JSONObject().put("taken", JSONArray().apply { taken.forEach { put(it.toJson()) } })
    pending?.let { json.put("pending", it.toJson()) }
    return json.toString()
  }

  companion object {
    /** A photo the camera did not answer for a day is gone. */
    const val PENDING_MS = 86_400_000L
    /** A taken photo waits a month for its notebook. */
    const val TAKEN_MS = 30L * 86_400_000L
    /** At most so many taken photos wait (the oldest go). */
    const val MAX_TAKEN = 20

    /** The state of [raw] (photos.json); nothing when missing or broken, broken entries left out. */
    fun parse(raw: String?): CameraPhotos {
      if (raw.isNullOrEmpty()) return CameraPhotos()
      return try {
        val json = JSONObject(raw)
        val taken = json.optJSONArray("taken")
        CameraPhotos(PhotoRecord.fromJson(json.optJSONObject("pending")),
          (0 until (taken?.length() ?: 0)).mapNotNull { PhotoRecord.fromJson(taken?.optJSONObject(it)) })
      } catch (e: JSONException) {
        CameraPhotos()
      }
    }

    /**
     * The photo files of the folder ([files]: path → last change) that no record of [photos] names
     * and that are older than a day: left by a process that ended between writing them and the
     * record, or by an older version of the app.
     */
    fun strays(files: Map<String, Long>, photos: CameraPhotos, now: Long): List<String> {
      val named = photos.files()
      return files.filter { (path, changed) -> path !in named && now - changed > PENDING_MS }.keys.sorted()
    }
  }
}
