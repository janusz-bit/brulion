// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later
package io.github.januszbit.brulion.android

import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.provider.MediaStore
import android.speech.RecognizerIntent
import android.util.Log
import androidx.core.content.FileProvider
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException

/**
 * A photo from the camera and dictation for the AI chat (docs/ANDROID.md "Camera and dictation";
 * src/platform/capture.h): the system's own apps through intents, so Brulion needs neither the
 * CAMERA nor the RECORD_AUDIO permission — the camera app takes the photo into a file of the app's
 * cache it is granted for this one photo (the FileProvider "<package>.qtprovider"), the speech
 * recognizer (Google's, or another one the device has) listens and gives back the text.
 * [BrulionAndroid.takePhoto] / [BrulionAndroid.capture] start one; the answer comes back through
 * [BrulionAndroid.onActivityResult]. A photo and what asked for it are kept in the cache
 * ([CameraPhotos], cache/camera/photos.json) from before the camera app starts until C++ takes the
 * photo ([takePhotos]): the camera's answer may come to a new process of the app.
 */
internal object MediaCapture {
  private const val TAG = "BrulionCapture"

  /** What is asked for: a photo, or speech as text (the codes of the C++ side). */
  const val PHOTO = 0
  const val SPEECH = 1

  /** [CameraPhotos] is read and written under it (the main thread answers, C++ takes on Qt's). */
  private val lock = Any()

  private fun intentOf(kind: Int): Intent =
    if (kind == PHOTO) Intent(MediaStore.ACTION_IMAGE_CAPTURE) else Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH)

  /** Does the device have an app that answers `kind` (AndroidManifest.xml's <queries> lets us see it)? */
  fun available(context: Context, kind: Int): Boolean =
    try {
      intentOf(kind).resolveActivity(context.packageManager) != null
    } catch (e: Exception) {
      Log.w(TAG, "available($kind)", e)
      false
    }

  /**
   * Starts the camera app for a photo for [purpose] in [notebook] (`requestCode`): the record is
   * written first. Null when it started, else why not.
   */
  fun startPhoto(activity: Activity, notebook: String, purpose: String, requestCode: Int): String? =
    try {
      val now = System.currentTimeMillis()
      val file = File(folder(activity), "photo-$now.jpg")
      begin(activity, PhotoRecord(file.absolutePath, notebook, purpose, now))
      val uri = FileProvider.getUriForFile(activity, activity.packageName + ".qtprovider", file)
      val intent = intentOf(PHOTO)
        .putExtra(MediaStore.EXTRA_OUTPUT, uri)
        .addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION or Intent.FLAG_GRANT_READ_URI_PERMISSION)
      intent.clipData = ClipData.newRawUri("", uri)
      startOrUndo(activity, intent, requestCode)
    } catch (e: Exception) {
      Log.w(TAG, "cannot take a photo", e)
      e.message ?: e.javaClass.simpleName
    }

  /** Starts the speech recognizer in `language` (BCP 47; "" the device's) for `requestCode`. */
  fun startSpeech(activity: Activity, language: String, requestCode: Int): String? =
    try {
      val intent = intentOf(SPEECH)
        .putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
        .putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1)
      if (language.isNotEmpty()) intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE, language)
      activity.startActivityForResult(intent, requestCode)
      null
    } catch (e: ActivityNotFoundException) {
      Log.w(TAG, "no speech recognizer", e)
      "no speech recognizer"
    } catch (e: Exception) {
      Log.w(TAG, "cannot listen", e)
      e.message ?: e.javaClass.simpleName
    }

  /** The camera app for the photo just begun; when it cannot start, the photo is let go again. */
  private fun startOrUndo(activity: Activity, intent: Intent, requestCode: Int): String? =
    try {
      activity.startActivityForResult(intent, requestCode)
      null
    } catch (e: ActivityNotFoundException) {
      Log.w(TAG, "no camera app", e)
      photoResult(activity, Activity.RESULT_CANCELED)
      "no camera app"
    }

  /**
   * The camera app answered (in this process or a new one): {taken: true} — the photo waits for
   * [takePhotos] — or {cancelled: true, purpose} (nothing taken: the file goes).
   */
  fun photoResult(context: Context, resultCode: Int): JSONObject {
    val answer = change(context) { photos ->
      val answer = photos.answer(resultCode == Activity.RESULT_OK) { file -> File(file).length() }
      CameraPhotos.Change(answer.photos, answer.gone) to answer
    }
    if (answer.kept != null) return JSONObject().put("taken", true)
    return JSONObject().put("cancelled", true).put("purpose", answer.purpose)
  }

  /** The taken photos of [notebook] (and of none) as a JSON array of {file, notebook, purpose}, handed out once. */
  fun takePhotos(context: Context, notebook: String): String {
    val out = change(context) { photos ->
      val (handed, kept) = photos.handOut(notebook)
      CameraPhotos.Change(kept) to handed
    }
    return JSONArray().apply { out.forEach { put(it.toJson()) } }.toString()
  }

  /** What the recognizer heard: {text} ("" for nothing), or {cancelled: true}. */
  fun speechResult(resultCode: Int, data: Intent?): JSONObject {
    if (resultCode != Activity.RESULT_OK) return JSONObject().put("cancelled", true)
    val text = data?.getStringArrayListExtra(RecognizerIntent.EXTRA_RESULTS)?.firstOrNull() ?: ""
    return JSONObject().put("text", text)
  }

  // --- the record (cache/camera/photos.json) -----------------------------------------------

  private fun folder(context: Context): File = File(context.cacheDir, "camera").apply { mkdirs() }

  /** [record] begun; old records and stray photos of earlier days go first. */
  private fun begin(context: Context, record: PhotoRecord) {
    change(context) { photos ->
      val expired = photos.expire(record.started)
      val begun = expired.photos.begin(record)
      val dir = folder(context)
      val files = dir.listFiles { f -> f.isFile && f.name.endsWith(".jpg") }.orEmpty()
      val strays = CameraPhotos.strays(files.associate { it.absolutePath to it.lastModified() }, begun.photos,
        record.started)
      CameraPhotos.Change(begun.photos, expired.gone + begun.gone + strays) to Unit
    }
  }

  /**
   * Reads the record, applies [step] (the new state and the files it lets go, and a result),
   * writes it back (an IOException when it cannot: nothing changed then) and deletes those files.
   */
  private fun <T> change(context: Context, step: (CameraPhotos) -> Pair<CameraPhotos.Change, T>): T =
    synchronized(lock) {
      val file = File(folder(context), "photos.json")
      val raw = try { if (file.isFile) file.readText(Charsets.UTF_8) else null } catch (e: IOException) { null }
      val before = CameraPhotos.parse(raw)
      val (change, result) = step(before)
      if (change.photos != before) write(file, change.photos.toJson())
      change.gone.forEach { File(it).delete() }
      result
    }

  /** [text] into [file] at once and whole (a temporary file renamed over it): the process may end next. */
  private fun write(file: File, text: String) {
    val temp = File(file.parentFile, file.name + ".tmp")
    temp.writeText(text, Charsets.UTF_8)
    if (!temp.renameTo(file)) throw IOException("cannot write ${file.name}")
  }
}
