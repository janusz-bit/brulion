// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.OpenableColumns
import android.util.Log
import android.widget.Toast
import io.github.januszbit.brulion.R
import java.io.File
import java.io.IOException
import java.util.concurrent.atomic.AtomicInteger

/**
 * "Udostępnij → Brulion" (docs/ANDROID.md): the target of the share sheet for text and text files
 * (`ACTION_SEND` / `ACTION_SEND_MULTIPLE`, text MIME types). A separate exported activity
 * without a UI of its own — the system starts it in the sending app's task, where a second Qt
 * activity cannot live. It copies the shared files to `files/share-in/<stamp>/<n>/<name>` on a background thread
 * (the sender's read permission ends with this activity), hands the share to C++ through
 * [BrulionAndroid.deliverShare] (in this process: no intent extra any other app could forge) and
 * brings [BrulionActivity] to the front, where the share dialog opens. Everything another app
 * sends is untrusted: only `content://` streams of other apps are read, at most
 * [ShareIntent.MAX_FILES] files of [ShareIntent.MAX_BYTES] each.
 */
class ShareActivity : Activity() {
  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    // Recreated (the process was killed while copying): that share is gone with its permission.
    val intent = intent
    if (savedInstanceState != null || intent == null ||
      (intent.action != Intent.ACTION_SEND && intent.action != Intent.ACTION_SEND_MULTIPLE)
    ) {
      finish()
      return
    }
    val subject = safely { intent.getCharSequenceExtra(Intent.EXTRA_SUBJECT)?.toString() }
    val text = ShareIntent.limitText(safely { intent.getCharSequenceExtra(Intent.EXTRA_TEXT)?.toString() })
    val streams = streamsOf(intent)
    val type = intent.type
    val app = applicationContext
    Thread({
      val json =
        try {
          receive(app, subject, text, streams, type)
        } catch (e: RuntimeException) {
          Log.w(TAG, "cannot receive the share", e)
          null
        }
      runOnUiThread { finishWith(json) }
    }, "BrulionShare").start()
  }

  private fun finishWith(json: String?) {
    if (json == null) {
      Toast.makeText(applicationContext, R.string.brulion_share_nothing, Toast.LENGTH_SHORT).show()
    } else {
      BrulionAndroid.deliverShare(json)
      packageManager.getLaunchIntentForPackage(packageName)?.let { launch ->
        launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP)
        try {
          startActivity(launch)
        } catch (e: RuntimeException) {
          Log.w(TAG, "cannot bring Brulion to the front", e)
        }
      }
    }
    finish()
  }

  private companion object {
    const val TAG = "BrulionShare"
    val counter = AtomicInteger()

    /** An extra read defensively: a malformed parcel from another app must not crash Brulion. */
    inline fun <T> safely(read: () -> T?): T? =
      try {
        read()
      } catch (e: RuntimeException) {
        Log.w(TAG, "malformed share extra", e)
        null
      }

    /** The shared streams: EXTRA_STREAM (one or a list) and ClipData (some apps use only that). */
    @Suppress("DEPRECATION") // the typed getters need API 33
    fun streamsOf(intent: Intent): List<Uri> {
      val out = LinkedHashSet<Uri>()
      safely {
        if (intent.action == Intent.ACTION_SEND_MULTIPLE) {
          val list =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
              intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM, Uri::class.java)
            } else {
              intent.getParcelableArrayListExtra<Uri>(Intent.EXTRA_STREAM)
            }
          list?.filterNotNull()?.let { out.addAll(it) }
        } else {
          val uri =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
              intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java)
            } else {
              intent.getParcelableExtra<Uri>(Intent.EXTRA_STREAM)
            }
          uri?.let { out.add(it) }
        }
      }
      safely {
        val clip = intent.clipData
        if (clip != null) for (i in 0 until clip.itemCount) clip.getItemAt(i)?.uri?.let { out.add(it) }
      }
      return out.toList()
    }

    /** The share as JSON for C++, or null when there is nothing to save. Background thread. */
    fun receive(context: Context, subject: String?, text: String?, streams: List<Uri>, type: String?): String? {
      val inbox = File(context.filesDir, ShareIntent.INBOX)
      val now = System.currentTimeMillis()
      removeLeftovers(inbox, now)
      val dir = File(inbox, ShareIntent.folderName(now, counter.incrementAndGet()))
      val files = ArrayList<ShareIntent.SharedFile>()
      var copied = 0
      for ((index, uri) in streams.withIndex()) {
        if (files.size >= 100) break
        val file = copy(context, uri, dir, index, if (streams.size == 1) type else null, copied)
        if (file.error == null) copied++
        files.add(file)
      }
      if (ShareIntent.isEmpty(subject, text, files)) {
        dir.deleteRecursively()
        return null
      }
      val dirPath = if (dir.exists()) dir.absolutePath else null
      return ShareIntent.toJson(subject, text, dirPath, files).toString()
    }

    /** One stream: copied below [dir], or with the reason it is not. */
    fun copy(context: Context, uri: Uri, dir: File, index: Int, type: String?, copied: Int): ShareIntent.SharedFile {
      val resolver = context.contentResolver
      // Only another app's content: a file:// path, or a content:// URI of Brulion's own provider,
      // would let a sender make Brulion read its own private files (a confused deputy).
      val authority = uri.authority ?: ""
      if (uri.scheme != "content" || authority == context.packageName ||
        authority.startsWith(context.packageName + ".")
      ) {
        return ShareIntent.SharedFile(null, uri.lastPathSegment, null, -1, "unreadable")
      }
      var name: String? = null
      var size = -1L
      try {
        resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)?.use { c ->
          if (c.moveToFirst()) {
            val nameColumn = c.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            val sizeColumn = c.getColumnIndex(OpenableColumns.SIZE)
            if (nameColumn >= 0 && !c.isNull(nameColumn)) name = c.getString(nameColumn)
            if (sizeColumn >= 0 && !c.isNull(sizeColumn)) size = c.getLong(sizeColumn)
          }
        }
      } catch (e: RuntimeException) {
        Log.w(TAG, "cannot query a shared stream", e)
      }
      if (name == null) name = uri.lastPathSegment
      val mime = safely { resolver.getType(uri) } ?: type
      if (copied >= ShareIntent.MAX_FILES) return ShareIntent.SharedFile(null, name, mime, size, "tooMany")
      if (!ShareIntent.isText(name, mime)) return ShareIntent.SharedFile(null, name, mime, size, "notText")
      if (size > ShareIntent.MAX_BYTES) return ShareIntent.SharedFile(null, name, mime, size, "tooLarge")
      val target = File(File(dir, index.toString()), ShareIntent.copyName(name, index))
      return try {
        target.parentFile?.mkdirs()
        var total = 0L
        val input = resolver.openInputStream(uri) ?: throw IOException("no stream")
        input.use { stream ->
          target.outputStream().use { out ->
            val buffer = ByteArray(64 * 1024)
            while (true) {
              val n = stream.read(buffer)
              if (n < 0) break
              total += n
              if (total > ShareIntent.MAX_BYTES) break
              out.write(buffer, 0, n)
            }
          }
        }
        if (total > ShareIntent.MAX_BYTES) {
          target.delete()
          ShareIntent.SharedFile(null, name, mime, total, "tooLarge")
        } else {
          ShareIntent.SharedFile(target.absolutePath, name, mime, total)
        }
      } catch (e: Exception) {
        // SecurityException (no permission), FileNotFoundException, IOException.
        Log.w(TAG, "cannot copy a shared stream", e)
        target.delete()
        ShareIntent.SharedFile(null, name, mime, size, "unreadable")
      }
    }

    /** Copies of earlier shares nobody saved or dismissed (the app was killed, say). */
    fun removeLeftovers(inbox: File, now: Long) {
      val leftovers = inbox.listFiles() ?: return
      for (folder in leftovers) if (ShareIntent.isExpired(folder.name, now)) folder.deleteRecursively()
    }
  }
}
