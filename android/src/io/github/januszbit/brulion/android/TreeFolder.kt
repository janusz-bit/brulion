// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.content.Context
import android.content.Intent
import android.database.Cursor
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject
import java.io.FileNotFoundException

/**
 * A folder picked in the system's dialog (ACTION_OPEN_DOCUMENT_TREE) as the other side of a
 * notebook's sync (docs/ANDROID.md "A folder picked in the system's dialog"): the notebook lives in
 * the app's own storage, and C++ (src/platform/android/safstorage.cpp, a sync::FolderStorage behind
 * the sync engine) reaches the picked folder through these calls, by paths relative to it
 * ("Dziennik/2026-11-22.md", "" for the folder itself). No storage permission: the persisted grant
 * of the picked tree ([take]) is all it needs, so the app for Google Play does without "All files
 * access".
 *
 * Answers are JSON: an entry `{"exists": true, "dir", "size", "mtime"}` or `{"exists": false}`,
 * `{"entries": [{"name", "dir", "size", "mtime"}]}`, `{}` for done, or `{"error": code, "message"}`
 * with the codes of sync::FolderErrorCode: "not-found", "exists", "not-allowed" (the grant is gone),
 * "unavailable" (the folder cannot be reached), "failed". [read] answers the bytes, or null and
 * [lastError]. A missing file is told from a folder that cannot be read: "missing" is said only
 * when its folder could be listed without it — the sync would otherwise take files for deleted.
 *
 * Called from one C++ worker thread at a time (sync::FolderDavTransport); nothing here needs the
 * main thread. Paths to document ids: the phone's storage and memory cards
 * (com.android.externalstorage.documents) name documents by their path, other providers are walked
 * folder by folder (ids cached).
 */
internal object TreeFolder {
  private const val TAG = "BrulionTreeFolder"
  /** Files are created with this type: a provider then keeps the name as given (no extension added). */
  private const val FILE_MIME = "application/octet-stream"

  private val COLUMNS =
    arrayOf(
      Document.COLUMN_DOCUMENT_ID,
      Document.COLUMN_DISPLAY_NAME,
      Document.COLUMN_MIME_TYPE,
      Document.COLUMN_SIZE,
      Document.COLUMN_LAST_MODIFIED
    )

  /** A failure with one of the codes above. */
  class FolderException(val code: String, message: String, cause: Throwable? = null) : Exception(message, cause)

  data class Entry(val id: String, val name: String, val dir: Boolean, val size: Long, val mtime: Long)

  private val lastErrorText = ThreadLocal<String>()

  /** Path -> document id of the walked providers, per tree ("<tree>\n<path>"). */
  private val ids =
    object : LinkedHashMap<String, String>(256, 0.75f, true) {
      override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, String>?) = size > 8192
    }

  // ---------------------------------------------------------------------------------------------
  // Picking: the grant, the name
  // ---------------------------------------------------------------------------------------------

  /**
   * Keeps the grant of a folder just picked (it survives restarts) and describes it:
   * `{"uri", "name", "where"}` (`where`: "primary:Notatki" style id of the phone's storage, "" for
   * other providers), or an error.
   */
  @JvmStatic
  fun take(context: Context, uriString: String): String =
    answer {
      val tree = Uri.parse(uriString)
      if (!DocumentsContract.isTreeUri(tree)) throw FolderException("failed", "not a folder: $uriString")
      val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
      try {
        context.contentResolver.takePersistableUriPermission(tree, flags)
      } catch (e: SecurityException) {
        // Taken already (Qt's dialog may have), or a grant that cannot persist: what counts is
        // whether the app holds it now.
        if (!hasAccess(context, uriString)) throw FolderException("not-allowed", "no lasting access to $uriString", e)
      }
      val root = rootEntry(context, tree)
      if (!root.dir) throw FolderException("failed", "not a folder: $uriString")
      val rootId = DocumentsContract.getTreeDocumentId(tree)
      JSONObject()
        .put("uri", uriString)
        .put("name", root.name)
        .put("where", if (tree.authority == TreeFolderPaths.EXTERNAL_STORAGE) rootId else "")
    }

  /** Gives the grant of a folder back (the notebook no longer syncs with it). */
  @JvmStatic
  fun release(context: Context, uriString: String) {
    val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
    for (grant in grantsOf(context, uriString)) {
      try {
        context.contentResolver.releasePersistableUriPermission(grant, flags)
      } catch (e: Exception) {
        Log.i(TAG, "release: $e")
      }
    }
    synchronized(ids) { ids.keys.removeAll { it.startsWith(uriString + "\n") } }
  }

  /** The app still holds the grant to read and write the folder (it can be taken back in the settings). */
  @JvmStatic
  fun hasAccess(context: Context, uriString: String): Boolean =
    context.contentResolver.persistedUriPermissions.any {
      it.isReadPermission && it.isWritePermission && sameTree(it.uri, uriString)
    }

  /** The kept grants of the folder [uriString] (as the system wrote its address). */
  private fun grantsOf(context: Context, uriString: String): List<Uri> =
    context.contentResolver.persistedUriPermissions.map { it.uri }.filter { sameTree(it, uriString) }

  /** [uri] names the tree [uriString] names: the same text, or the same provider and root document. */
  private fun sameTree(uri: Uri, uriString: String): Boolean {
    if (uri.toString() == uriString) return true
    val other = Uri.parse(uriString)
    if (uri.authority != other.authority || !DocumentsContract.isTreeUri(uri) || !DocumentsContract.isTreeUri(other)) {
      return false
    }
    return try {
      DocumentsContract.getTreeDocumentId(uri) == DocumentsContract.getTreeDocumentId(other)
    } catch (e: Exception) {
      false
    }
  }

  // ---------------------------------------------------------------------------------------------
  // The sync's operations (sync::FolderStorage)
  // ---------------------------------------------------------------------------------------------

  @JvmStatic
  fun stat(context: Context, uriString: String, path: String): String =
    answer {
      val entry = statEntry(context, Uri.parse(uriString), path)
      if (entry == null) JSONObject().put("exists", false) else entryJson(entry).put("exists", true)
    }

  @JvmStatic
  fun list(context: Context, uriString: String, path: String): String =
    answer {
      val children =
        listChildren(context, Uri.parse(uriString), path) ?: throw FolderException("not-found", "no folder $path")
      val entries = JSONArray()
      for (child in children) entries.put(entryJson(child).put("name", child.name))
      JSONObject().put("entries", entries)
    }

  /** The whole file, or null ([lastError] says why). */
  @JvmStatic
  fun read(context: Context, uriString: String, path: String, maxBytes: Long): ByteArray? =
    try {
      val tree = Uri.parse(uriString)
      val entry = statEntry(context, tree, path) ?: throw FolderException("not-found", "no file $path")
      if (entry.dir) throw FolderException("failed", "$path is a folder")
      if (entry.size > maxBytes) throw FolderException("failed", "$path: ${entry.size} bytes, more than $maxBytes")
      val uri = DocumentsContract.buildDocumentUriUsingTree(tree, entry.id)
      val stream = context.contentResolver.openInputStream(uri) ?: throw FolderException("failed", "cannot open $path")
      stream.use {
        val bytes = TreeFolderPaths.readAtMost(it, maxBytes) ?: throw FolderException("failed", "$path grew past $maxBytes bytes")
        lastErrorText.remove()
        bytes
      }
    } catch (e: Exception) {
      lastErrorText.set(failure(e).toString())
      null
    }

  /**
   * The file opened for reading, as a file descriptor the caller owns and closes (a detached
   * ParcelFileDescriptor: the native side reads it in pieces, never the whole file through the
   * JVM), or -1 ([lastError] says why). Checked as [read] checks.
   */
  @JvmStatic
  fun openRead(context: Context, uriString: String, path: String, maxBytes: Long): Int =
    try {
      val tree = Uri.parse(uriString)
      val entry = statEntry(context, tree, path) ?: throw FolderException("not-found", "no file $path")
      if (entry.dir) throw FolderException("failed", "$path is a folder")
      if (entry.size > maxBytes) throw FolderException("failed", "$path: ${entry.size} bytes, more than $maxBytes")
      val uri = DocumentsContract.buildDocumentUriUsingTree(tree, entry.id)
      val descriptor = context.contentResolver.openFileDescriptor(uri, "r") ?: throw FolderException("failed", "cannot open $path")
      lastErrorText.remove()
      descriptor.detachFd()
    } catch (e: Exception) {
      lastErrorText.set(failure(e).toString())
      -1
    }

  /** Why the last [read] or [openRead] on this thread failed (an error answer). */
  @JvmStatic fun lastError(): String = lastErrorText.get() ?: failure(FolderException("failed", "unknown")).toString()

  /** Writes the whole file (its folder must exist); answers the entry it has afterwards. */
  @JvmStatic
  fun write(context: Context, uriString: String, path: String, data: ByteArray): String =
    answer {
      val tree = Uri.parse(uriString)
      val (parentPath, name) = TreeFolderPaths.split(path)
      if (name.isEmpty()) throw FolderException("failed", "no name: $path")
      val parent = statEntry(context, tree, parentPath)
      if (parent == null || !parent.dir) throw FolderException("not-found", "no folder for $path")
      val existing = statEntry(context, tree, path)
      if (existing != null && existing.dir) throw FolderException("failed", "$path is a folder")
      val uri =
        if (existing != null) {
          DocumentsContract.buildDocumentUriUsingTree(tree, existing.id)
        } else {
          create(context, tree, parent, path, name, FILE_MIME)
        }
      // "wt": the file replaced from its start (the bytes are all here: written in one go).
      val stream =
        context.contentResolver.openOutputStream(uri, if (existing != null) "wt" else "w")
          ?: throw FolderException("failed", "cannot write $path")
      stream.use {
        it.write(data)
        it.flush()
      }
      val after = statEntry(context, tree, path) ?: throw FolderException("failed", "$path gone right after it was written")
      // A provider that does not truncate ("wt" taken as "w") would leave the old tail behind.
      if (after.size != data.size.toLong()) {
        throw FolderException("failed", "$path: ${after.size} bytes after writing ${data.size} (the folder's app does not replace files)")
      }
      entryJson(after).put("exists", true)
    }

  @JvmStatic
  fun makeFolder(context: Context, uriString: String, path: String): String =
    answer {
      val tree = Uri.parse(uriString)
      if (path.isEmpty()) {
        // The picked folder itself: only the system's dialog makes it.
        if (statEntry(context, tree, "") != null) throw FolderException("exists", "the folder exists")
        throw FolderException("not-allowed", "the picked folder is gone: pick it again")
      }
      val (parentPath, name) = TreeFolderPaths.split(path)
      val parent = statEntry(context, tree, parentPath)
      if (parent == null || !parent.dir) throw FolderException("not-found", "no folder for $path")
      if (statEntry(context, tree, path) != null) throw FolderException("exists", "$path exists")
      create(context, tree, parent, path, name, Document.MIME_TYPE_DIR)
      JSONObject()
    }

  /** Deletes a file, or a folder with everything in it. */
  @JvmStatic
  fun remove(context: Context, uriString: String, path: String): String =
    answer {
      val tree = Uri.parse(uriString)
      if (path.isEmpty()) throw FolderException("not-allowed", "never the picked folder itself")
      val entry = statEntry(context, tree, path) ?: throw FolderException("not-found", "no $path")
      val uri = DocumentsContract.buildDocumentUriUsingTree(tree, entry.id)
      if (!DocumentsContract.deleteDocument(context.contentResolver, uri)) {
        throw FolderException("failed", "the folder's app did not delete $path")
      }
      forget(uriString, path)
      JSONObject()
    }

  // ---------------------------------------------------------------------------------------------
  // Internals
  // ---------------------------------------------------------------------------------------------

  private inline fun answer(block: () -> JSONObject): String =
    try {
      block().toString()
    } catch (e: Exception) {
      failure(e).toString()
    }

  private fun failure(e: Exception): JSONObject {
    val code =
      when (e) {
        is FolderException -> e.code
        is SecurityException -> "not-allowed"
        else -> "failed"
      }
    if (code != "not-found" && code != "exists") Log.w(TAG, "$code: ${e.message}", e)
    return JSONObject().put("error", code).put("message", e.message ?: e.toString())
  }

  private fun entryJson(entry: Entry): JSONObject =
    JSONObject().put("dir", entry.dir).put("size", entry.size).put("mtime", entry.mtime)

  /** The picked folder's own entry; an error (never "missing") when it cannot be read. */
  private fun rootEntry(context: Context, tree: Uri): Entry {
    val rootId = DocumentsContract.getTreeDocumentId(tree)
    return try {
      queryOne(context, DocumentsContract.buildDocumentUriUsingTree(tree, rootId))
        ?: throw FolderException("unavailable", "the folder's app does not answer for $tree")
    } catch (e: FolderException) {
      throw e
    } catch (e: SecurityException) {
      throw FolderException("not-allowed", "no access to the folder any more: $e", e)
    } catch (e: Exception) {
      throw FolderException("unavailable", "the folder cannot be reached: $e", e)
    }
  }

  /**
   * The entry of [path], null when it is missing — said only when its folder lists without it (or
   * is missing itself). Any other failure is an exception.
   */
  private fun statEntry(context: Context, tree: Uri, path: String): Entry? {
    if (path.isEmpty()) return rootEntry(context, tree)
    val key = tree.toString()
    val id = idFor(context, tree, path) ?: return null
    try {
      val entry = queryOne(context, DocumentsContract.buildDocumentUriUsingTree(tree, id))
      if (entry != null) return entry
    } catch (e: SecurityException) {
      throw FolderException("not-allowed", "no access to $path: $e", e)
    } catch (e: Exception) {
      // Missing (a path id of a file that is not there, a stale id), or broken: its folder decides.
      Log.d(TAG, "stat $path: $e")
    }
    forget(key, path)
    val (parentPath, name) = TreeFolderPaths.split(path)
    val siblings = listChildren(context, tree, parentPath) ?: return null
    val found = TreeFolderPaths.find(siblings.map { it.name }, name) ?: return null
    val entry = siblings.first { it.name == found }
    remember(key, path, entry.id)
    return entry
  }

  /** The children of the folder [path], null when it is missing. */
  private fun listChildren(context: Context, tree: Uri, path: String): List<Entry>? {
    val key = tree.toString()
    val id = idFor(context, tree, path) ?: return null
    val children =
      try {
        queryChildren(context, tree, id)
      } catch (e: SecurityException) {
        throw FolderException("not-allowed", "no access to the folder any more: $e", e)
      } catch (e: Exception) {
        if (path.isEmpty()) throw FolderException("unavailable", "the folder cannot be read: $e", e)
        null
      }
    if (children != null) {
      for (child in children) remember(key, TreeFolderPaths.join(path, child.name), child.id)
      return children
    }
    // Not listed: a folder that is gone (its own folder lists without it), or a failure.
    forget(key, path)
    val entry = statEntry(context, tree, path) ?: return null
    if (!entry.dir) return null
    return try {
      queryChildren(context, tree, entry.id)
    } catch (e: Exception) {
      throw FolderException("failed", "cannot list $path: $e", e)
    }
  }

  /** The document id of [path]: by its path on the phone's storage, else cached or walked. */
  private fun idFor(context: Context, tree: Uri, path: String): String? {
    val rootId = DocumentsContract.getTreeDocumentId(tree)
    if (path.isEmpty()) return rootId
    if (tree.authority == TreeFolderPaths.EXTERNAL_STORAGE) return TreeFolderPaths.externalId(rootId, path)
    val key = tree.toString()
    synchronized(ids) { ids["$key\n$path"] }?.let { return it }
    var folderId = rootId
    var folderPath = ""
    for (segment in path.split('/')) {
      val children =
        try {
          queryChildren(context, tree, folderId)
        } catch (e: SecurityException) {
          throw FolderException("not-allowed", "no access to the folder any more: $e", e)
        } catch (e: Exception) {
          throw FolderException("failed", "cannot list ${folderPath.ifEmpty { "the folder" }}: $e", e)
        }
      for (child in children) remember(key, TreeFolderPaths.join(folderPath, child.name), child.id)
      val found = TreeFolderPaths.find(children.map { it.name }, segment) ?: return null
      val child = children.first { it.name == found }
      folderPath = TreeFolderPaths.join(folderPath, segment)
      folderId = child.id
      if (folderPath != path && !child.dir) return null
    }
    return folderId
  }

  /** Makes a file or folder [name] in [parent]; the name the provider gave must be [name]. */
  private fun create(context: Context, tree: Uri, parent: Entry, path: String, name: String, mime: String): Uri {
    val parentUri = DocumentsContract.buildDocumentUriUsingTree(tree, parent.id)
    val created =
      DocumentsContract.createDocument(context.contentResolver, parentUri, mime, name)
        ?: throw FolderException("failed", "the folder's app did not create $path")
    val entry = queryOne(context, created)
    if (entry == null || entry.name != name) {
      // Renamed by the provider ("Plan (1).md" beside a name taken in another letter case, a
      // character a memory card cannot hold): not the file asked for.
      try {
        DocumentsContract.deleteDocument(context.contentResolver, created)
      } catch (e: Exception) {
        Log.w(TAG, "could not delete the misnamed $created", e)
      }
      throw FolderException("failed", "the folder's app named $path \"${entry?.name}\"")
    }
    remember(tree.toString(), path, entry.id)
    return created
  }

  private fun queryOne(context: Context, uri: Uri): Entry? =
    context.contentResolver.query(uri, COLUMNS, null, null, null)?.use { cursor ->
      if (cursor.moveToFirst()) entryOf(cursor) else null
    }

  private fun queryChildren(context: Context, tree: Uri, folderId: String): List<Entry> {
    val uri = DocumentsContract.buildChildDocumentsUriUsingTree(tree, folderId)
    val cursor =
      context.contentResolver.query(uri, COLUMNS, null, null, null)
        ?: throw FileNotFoundException("no answer listing $folderId")
    return cursor.use {
      val out = ArrayList<Entry>(it.count.coerceAtLeast(0))
      while (it.moveToNext()) entryOf(it)?.let { entry -> out.add(entry) }
      out
    }
  }

  private fun entryOf(cursor: Cursor): Entry? {
    val id = text(cursor, Document.COLUMN_DOCUMENT_ID) ?: return null
    val name = text(cursor, Document.COLUMN_DISPLAY_NAME) ?: return null
    if (!TreeFolderPaths.validName(name)) return null
    val dir = text(cursor, Document.COLUMN_MIME_TYPE) == Document.MIME_TYPE_DIR
    return Entry(id, name, dir, if (dir) 0 else number(cursor, Document.COLUMN_SIZE), number(cursor, Document.COLUMN_LAST_MODIFIED))
  }

  private fun text(cursor: Cursor, column: String): String? {
    val index = cursor.getColumnIndex(column)
    return if (index < 0 || cursor.isNull(index)) null else cursor.getString(index)
  }

  private fun number(cursor: Cursor, column: String): Long {
    val index = cursor.getColumnIndex(column)
    return if (index < 0 || cursor.isNull(index)) 0L else cursor.getLong(index)
  }

  private fun remember(key: String, path: String, id: String) {
    synchronized(ids) { ids["$key\n$path"] = id }
  }

  /** Drops [path] and what is below it from the walked ids. */
  private fun forget(key: String, path: String) {
    val exact = "$key\n$path"
    val below = "$exact/"
    synchronized(ids) { ids.keys.removeAll { it == exact || it.startsWith(below) } }
  }
}

/** The pure parts of [TreeFolder] (tested on the JVM: android/tests). */
internal object TreeFolderPaths {
  /** The provider of the phone's storage and memory cards: document ids are "<root id>:<path>". */
  const val EXTERNAL_STORAGE = "com.android.externalstorage.documents"

  /** "a/b/c.md" -> ("a/b", "c.md"); "c.md" -> ("", "c.md"). */
  fun split(path: String): Pair<String, String> {
    val slash = path.lastIndexOf('/')
    return if (slash < 0) Pair("", path) else Pair(path.substring(0, slash), path.substring(slash + 1))
  }

  fun join(folder: String, name: String): String = if (folder.isEmpty()) name else "$folder/$name"

  /** The document id of [path] below the root document [rootId] of the phone's storage. */
  fun externalId(rootId: String, path: String): String =
    if (path.isEmpty()) rootId else if (rootId.endsWith(":") || rootId.endsWith("/")) rootId + path else "$rootId/$path"

  /** A name a folder can hold: not empty, not "." or "..", no '/' or NUL. */
  fun validName(name: String): Boolean = name.isNotEmpty() && name != "." && name != ".." && '/' !in name && '\u0000' !in name

  /**
   * The name among [names] that [wanted] means: the same, else the same in Unicode's composed form
   * (NFC; a name written by macOS), else the same in another letter case (the phone's storage
   * ignores it). Null when none is.
   */
  fun find(names: List<String>, wanted: String): String? {
    names.firstOrNull { it == wanted }?.let { return it }
    val composed = java.text.Normalizer.normalize(wanted, java.text.Normalizer.Form.NFC)
    names.firstOrNull { java.text.Normalizer.normalize(it, java.text.Normalizer.Form.NFC) == composed }?.let { return it }
    return names.firstOrNull { it.equals(wanted, ignoreCase = true) }
  }

  /** At most [maxBytes] of [stream], or null when there is more. */
  fun readAtMost(stream: java.io.InputStream, maxBytes: Long): ByteArray? {
    val out = java.io.ByteArrayOutputStream()
    val buffer = ByteArray(64 * 1024)
    var total = 0L
    while (true) {
      val read = stream.read(buffer)
      if (read < 0) break
      total += read
      if (total > maxBytes) return null
      out.write(buffer, 0, read)
    }
    return out.toByteArray()
  }
}
