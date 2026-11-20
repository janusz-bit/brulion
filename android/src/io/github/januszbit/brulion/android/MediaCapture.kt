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
import org.json.JSONObject
import java.io.File

/**
 * A photo from the camera and dictation for the AI chat (docs/ANDROID.md "Camera and dictation";
 * src/platform/capture.h): the system's own apps through intents, so Brulion needs neither the
 * CAMERA nor the RECORD_AUDIO permission — the camera app takes the photo into a file of the app's
 * cache it is granted for this one photo (the FileProvider "<package>.qtprovider"), the speech
 * recognizer (Google's, or another one the device has) listens and gives back the text.
 * [BrulionAndroid.capture] starts one; the result comes back through
 * [BrulionAndroid.onActivityResult] as {file} / {text}, {cancelled: true} or {error}.
 */
internal object MediaCapture {
  private const val TAG = "BrulionCapture"

  /** What is asked for: a photo, or speech as text (the codes of the C++ side). */
  const val PHOTO = 0
  const val SPEECH = 1

  /** The photo being taken (its file, written by the camera app). */
  private var photoFile: File? = null

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

  /** Starts the camera app or the recognizer for `requestCode`; null when it started, else why not. */
  fun start(activity: Activity, kind: Int, language: String, requestCode: Int): String? =
    try {
      if (kind == PHOTO) {
        val dir = File(activity.cacheDir, "camera").apply { mkdirs() }
        // Photos of earlier days were taken into the notebook already (a copy): gone.
        val dayAgo = System.currentTimeMillis() - 86_400_000L
        dir.listFiles()?.forEach { if (it.lastModified() < dayAgo) it.delete() }
        val file = File(dir, "photo-${System.currentTimeMillis()}.jpg")
        val uri = FileProvider.getUriForFile(activity, activity.packageName + ".qtprovider", file)
        val intent = intentOf(PHOTO)
          .putExtra(MediaStore.EXTRA_OUTPUT, uri)
          .addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION or Intent.FLAG_GRANT_READ_URI_PERMISSION)
        intent.clipData = ClipData.newRawUri("", uri)
        photoFile = file
        activity.startActivityForResult(intent, requestCode)
      } else {
        val intent = intentOf(SPEECH)
          .putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
          .putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1)
        if (language.isNotEmpty()) intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE, language)
        activity.startActivityForResult(intent, requestCode)
      }
      null
    } catch (e: ActivityNotFoundException) {
      Log.w(TAG, "no app for $kind", e)
      if (kind == PHOTO) "no camera app" else "no speech recognizer"
    } catch (e: Exception) {
      Log.w(TAG, "cannot start $kind", e)
      e.message ?: e.javaClass.simpleName
    }

  /** The photo's result: {file} (its path), or {cancelled: true} (nothing taken: the file goes). */
  fun photoResult(resultCode: Int): JSONObject {
    val file = photoFile
    photoFile = null
    if (resultCode == Activity.RESULT_OK && file != null && file.length() > 0)
      return JSONObject().put("file", file.absolutePath)
    file?.delete()
    return JSONObject().put("cancelled", true)
  }

  /** What the recognizer heard: {text} ("" for nothing), or {cancelled: true}. */
  fun speechResult(resultCode: Int, data: Intent?): JSONObject {
    if (resultCode != Activity.RESULT_OK) return JSONObject().put("cancelled", true)
    val text = data?.getStringArrayListExtra(RecognizerIntent.EXTRA_RESULTS)?.firstOrNull() ?: ""
    return JSONObject().put("text", text)
  }
}
