// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.core.content.FileProvider
import java.io.File
import java.lang.ref.WeakReference
import java.util.concurrent.Executors

/**
 * "Open in another app" of the PDF viewer ([BrulionAndroid.viewFile],
 * `src/platform/openexternal_android.cpp`): the file handed to the app the system has for its type —
 * ACTION_VIEW of a content:// URI of Brulion's FileProvider (`<package>.qtprovider`, non-exported,
 * a read grant for this one URI), no chooser (the system asks itself when there is no default app).
 * The provider's roots (android/res/xml/qtprovider_paths.xml) are the app's files and cache and the
 * shared storage (`/storage/emulated/0`, where notebooks usually live); a file elsewhere — a memory
 * card — is copied into `cache/open/` first, on a background thread (older copies deleted), and the
 * app opens when the copy is there.
 */
internal object OpenWith {
  private const val TAG = "BrulionOpenWith"

  const val STARTED = 0
  const val NO_APP = 1
  const val FAILED = 2
  const val COPYING = 3

  private val copier = Executors.newSingleThreadExecutor()

  fun view(context: Context, path: String, mime: String): Int {
    val file = File(path)
    if (!file.isFile) return FAILED
    val authority = context.packageName + ".qtprovider"
    val roots = listOfNotNull(context.filesDir?.path, context.cacheDir?.path, sharedStorage())
    val direct = if (insideRoots(file.canonicalPath, roots.map { File(it).canonicalPath })) uriOf(context, authority, file) else null
    if (direct != null) return start(context, direct, mime)
    // Outside the provider's roots: a copy in the cache first.
    val app = context.applicationContext
    val caller = WeakReference(context)
    copier.execute {
      val copy = try {
        copyToCache(app, file)
      } catch (e: Exception) {
        Log.w(TAG, "cannot copy $path: $e")
        null
      }
      Handler(Looper.getMainLooper()).post {
        val uri = copy?.let { uriOf(app, authority, it) }
        if (uri == null) {
          Log.w(TAG, "no URI for $path")
          return@post
        }
        val code = start(caller.get() ?: app, uri, mime)
        if (code != STARTED) Log.w(TAG, "could not open the copy of $path: $code")
      }
    }
    return COPYING
  }

  @Suppress("DEPRECATION")
  private fun sharedStorage(): String? = android.os.Environment.getExternalStorageDirectory()?.path

  private fun uriOf(context: Context, authority: String, file: File): Uri? =
    try {
      FileProvider.getUriForFile(context, authority, file)
    } catch (e: IllegalArgumentException) {
      null // outside the provider's roots
    }

  private fun start(context: Context, uri: Uri, mime: String): Int {
    val intent = Intent(Intent.ACTION_VIEW).setDataAndType(uri, mime).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    if (context !is Activity) intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    return try {
      context.startActivity(intent)
      STARTED
    } catch (e: ActivityNotFoundException) {
      NO_APP
    } catch (e: Exception) {
      Log.w(TAG, "cannot start a viewer: $e")
      FAILED
    }
  }

  private fun copyToCache(context: Context, file: File): File {
    val dir = File(context.cacheDir, "open")
    dir.mkdirs()
    // One copy at a time: the previous ones go (a viewer still showing one has it open already).
    dir.listFiles()?.forEach { it.delete() }
    val copy = File(dir, copyName(file.name))
    file.inputStream().use { input -> copy.outputStream().use { input.copyTo(it) } }
    return copy
  }

  /**
   * The name of a copy in `cache/open/`: the file's own (Polish letters kept: the viewing app shows
   * it), without path separators or control characters, at most 120 characters keeping the
   * extension; "plik.pdf" ("file.pdf") for nothing usable. Pure (OpenWithTest).
   */
  fun copyName(name: String): String {
    var clean = name.filter { it != '/' && it != '\\' && it >= ' ' && it != '\u007f' }.trim().trimStart('.')
    if (clean.isEmpty()) return "plik.pdf"
    if (clean.length > 120) {
      val dot = clean.lastIndexOf('.')
      val ext = if (dot > 0 && clean.length - dot <= 10) clean.substring(dot) else ""
      clean = clean.substring(0, 120 - ext.length) + ext
    }
    return clean
  }

  /** `path` (canonical) lies inside one of `roots` (canonical folders). Pure (OpenWithTest). */
  fun insideRoots(path: String, roots: List<String>): Boolean =
    roots.any { root -> root.isNotEmpty() && (path == root || path.startsWith(root.trimEnd('/') + "/")) }
}
