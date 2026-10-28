// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONArray
import org.json.JSONObject

/**
 * The pure part of "Udostępnij → Brulion" ([ShareActivity]; docs/ANDROID.md): which shared
 * streams are text, safe names for the local copies, the inbox folders' names and age, and the
 * JSON handed to C++ (`src/core/notebook/share.h`, `sharedContentFromJson`):
 * `{"subject", "text", "dir", "files": [{"path", "name", "mime", "size", "error"}]}`.
 * No Android APIs here: JVM unit tests (ShareIntentTest) cover it.
 */
internal object ShareIntent {
  /** Largest file copied (bytes); C++ refuses bigger ones too (notebook::SharedFileMaxBytes). */
  const val MAX_BYTES = 2L * 1024 * 1024

  /** Most files of one share that are copied (notebook::SharedFilesMax); the rest: "tooMany". */
  const val MAX_FILES = 20

  /** Most characters of shared text kept (a whole book pasted into a share is cut). */
  const val MAX_TEXT_CHARS = 1_000_000

  /** Folder of the copies below the app's files dir (C++: AppController::shareInboxPath()). */
  const val INBOX = "share-in"

  /** Copies older than this are removed when the next share comes (ms). */
  const val KEEP_MS = 24L * 60 * 60 * 1000

  /** Longest name of a copy (UTF-16 units; file systems allow 255 bytes of UTF-8). */
  private const val MAX_NAME = 100

  private val TEXT_EXTENSIONS = setOf("md", "markdown", "mdown", "mkd", "txt", "text")

  /** A shared file Brulion saves: a text extension, or a text MIME type. */
  /**
   * A content URI's authority is one of Brulion's own providers ([packageName], or below it). A
   * leading "<userId>@" is stripped as ContentResolver does when it resolves the provider:
   * `content://0@<package>.qtprovider/…` names Brulion's own FileProvider too.
   */
  fun isOwnAuthority(authority: String?, packageName: String): Boolean {
    val host = (authority ?: "").substringAfterLast('@')
    return host.isEmpty() || host == packageName || host.startsWith("$packageName.")
  }

  fun isText(name: String?, mime: String?): Boolean {
    val extension = name?.substringAfterLast('.', "")?.lowercase() ?: ""
    if (extension in TEXT_EXTENSIONS) return true
    return mime?.trim()?.lowercase()?.startsWith("text/") == true
  }

  /**
   * A file name for the copy of a shared file called [displayName] (any string another app sent):
   * no folder separators, control characters or leading dots, at most [MAX_NAME] characters with
   * its extension kept; "shared-<index>.txt" when nothing is left.
   */
  fun copyName(displayName: String?, index: Int): String {
    val cleaned =
      (displayName ?: "")
        .map { c -> if (c == '/' || c == '\\' || c == ':' || c.code < 0x20 || c.code == 0x7F) '_' else c }
        .joinToString("")
        .trim()
        .trimStart('.')
        .trim()
    if (cleaned.isEmpty() || cleaned.all { it == '_' }) return "shared-$index.txt"
    if (cleaned.length <= MAX_NAME) return cleaned
    val dot = cleaned.lastIndexOf('.')
    val extension = if (dot > 0 && cleaned.length - dot <= 12) cleaned.substring(dot) else ""
    var stem = cleaned.substring(0, MAX_NAME - extension.length)
    // Never half a surrogate pair at the cut.
    if (stem.isNotEmpty() && Character.isHighSurrogate(stem.last())) stem = stem.dropLast(1)
    return stem + extension
  }

  /** The folder of one share's copies: "<epoch ms>-<n>". */
  fun folderName(nowMs: Long, n: Int): String = "$nowMs-$n"

  /**
   * Whether the inbox folder [name] is a leftover to remove at [nowMs]: older than [KEEP_MS], or
   * not one of ours at all (no "<epoch ms>-<n>" name). A folder from the future (the clock was
   * set back) counts as old too.
   */
  fun isExpired(name: String, nowMs: Long): Boolean {
    val stamp = name.substringBefore('-').toLongOrNull() ?: return true
    if (!Regex("""\d+-\d+""").matches(name)) return true
    return stamp > nowMs + 60_000 || nowMs - stamp > KEEP_MS
  }

  /** Shared text cut to [MAX_TEXT_CHARS] (null stays null). */
  fun limitText(text: String?): String? {
    if (text == null || text.length <= MAX_TEXT_CHARS) return text
    var end = MAX_TEXT_CHARS
    if (Character.isHighSurrogate(text[end - 1])) end -= 1
    return text.substring(0, end)
  }

  /** One shared file as C++ gets it: a copy at [path], or an [error] code and no path. */
  data class SharedFile(
    val path: String?,
    val name: String?,
    val mime: String?,
    val size: Long,
    val error: String? = null
  ) {
    fun toJson(): JSONObject {
      val json = JSONObject()
      if (!path.isNullOrEmpty()) json.put("path", path)
      if (!name.isNullOrEmpty()) json.put("name", name)
      if (!mime.isNullOrEmpty()) json.put("mime", mime)
      if (size >= 0) json.put("size", size)
      if (!error.isNullOrEmpty()) json.put("error", error)
      return json
    }
  }

  /**
   * A text share's preview image, not a shared file: the share sheet's rich preview has the
   * sender put a thumbnail into ClipData only (no EXTRA_STREAM) next to its text. Such a stream
   * that is no text file is left out instead of being listed as one Brulion cannot save.
   */
  fun isPreview(file: SharedFile, onlyInClipData: Boolean, subject: String?, text: String?): Boolean =
    onlyInClipData && file.error != null && !isText(file.name, file.mime) &&
      !(subject.isNullOrBlank() && text.isNullOrBlank())

  /** Nothing to hand over: no text, no subject and no file. */
  fun isEmpty(subject: String?, text: String?, files: List<SharedFile>): Boolean =
    subject.isNullOrBlank() && text.isNullOrBlank() && files.isEmpty()

  /** The JSON for C++ (null fields left out). */
  fun toJson(subject: String?, text: String?, dir: String?, files: List<SharedFile>): JSONObject {
    val json = JSONObject()
    if (!subject.isNullOrEmpty()) json.put("subject", subject)
    if (!text.isNullOrEmpty()) json.put("text", text)
    if (!dir.isNullOrEmpty()) json.put("dir", dir)
    if (files.isNotEmpty()) json.put("files", JSONArray().apply { files.forEach { put(it.toJson()) } })
    return json
  }
}
