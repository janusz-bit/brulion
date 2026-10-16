// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.content.Context
import android.database.Cursor
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject

/**
 * Picked documents (Storage Access Framework) for imports, behind [BrulionAndroid.documentInfo]
 * and [BrulionAndroid.listDocuments]: the system's picker hands out a document URI (a picked
 * file) or a tree URI (a picked folder, `ACTION_OPEN_DOCUMENT_TREE`). C++
 * (`src/app/io/fileexchange.cpp`) walks a picked tree folder by folder with [list] — one
 * ContentResolver query per folder, where Qt's content file engine queries every property of
 * every entry on its own — and reads the files itself (QFile opens content URIs).
 *
 * Called from C++ worker threads: nothing here needs the main thread or keeps state.
 */
internal object Documents {
  private const val TAG = "BrulionDocuments"

  private val CHILD_COLUMNS =
    arrayOf(
      DocumentsContract.Document.COLUMN_DOCUMENT_ID,
      DocumentsContract.Document.COLUMN_DISPLAY_NAME,
      DocumentsContract.Document.COLUMN_MIME_TYPE,
      DocumentsContract.Document.COLUMN_SIZE,
      DocumentsContract.Document.COLUMN_FLAGS
    )

  /** The entry of [uriString] (a tree URI: its root document), or `{"error": ...}`. */
  fun info(context: Context, uriString: String): String {
    return try {
      val uri = documentUri(context, Uri.parse(uriString))
      val resolver = context.contentResolver
      // All columns: document providers answer the document columns, other content providers
      // (a share, MediaStore) at least the openable ones (display name, size).
      resolver.query(uri, null, null, null, null).use { cursor ->
        if (cursor == null || !cursor.moveToFirst()) {
          return failure("not found: $uriString", null)
        }
        val mime = string(cursor, DocumentsContract.Document.COLUMN_MIME_TYPE) ?: resolver.getType(uri)
        entry(uri, cursor, mime).toString()
      }
    } catch (e: Exception) {
      // SecurityException (the permission is gone), IllegalArgumentException (not a document),
      // FileNotFoundException, UnsupportedOperationException (the provider cannot query).
      failure("documentInfo failed: $e", e)
    }
  }

  /** The entries of the folder document [uriString] of a picked tree, or `{"error": ...}`. */
  fun list(context: Context, uriString: String): String {
    return try {
      val folder = documentUri(context, Uri.parse(uriString))
      val childrenUri =
        DocumentsContract.buildChildDocumentsUriUsingTree(folder, DocumentsContract.getDocumentId(folder))
      val documents = JSONArray()
      context.contentResolver.query(childrenUri, CHILD_COLUMNS, null, null, null).use { cursor ->
        if (cursor == null) {
          return failure("cannot list: $uriString", null)
        }
        if (column(cursor, DocumentsContract.Document.COLUMN_DOCUMENT_ID) == null) {
          return failure("no document ids: $uriString", null)
        }
        while (cursor.moveToNext()) {
          val id = string(cursor, DocumentsContract.Document.COLUMN_DOCUMENT_ID)
          if (id.isNullOrEmpty()) continue
          val child = DocumentsContract.buildDocumentUriUsingTree(folder, id)
          documents.put(entry(child, cursor, string(cursor, DocumentsContract.Document.COLUMN_MIME_TYPE)))
        }
      }
      JSONObject().put("documents", documents).toString()
    } catch (e: Exception) {
      failure("listDocuments failed: $e", e)
    }
  }

  /** A tree URI as picked (`…/tree/<id>`) names its root document (`…/tree/<id>/document/<id>`). */
  private fun documentUri(context: Context, uri: Uri): Uri =
    if (DocumentsContract.isTreeUri(uri) && !DocumentsContract.isDocumentUri(context, uri)) {
      DocumentsContract.buildDocumentUriUsingTree(uri, DocumentsContract.getTreeDocumentId(uri))
    } else {
      uri
    }

  private fun entry(uri: Uri, cursor: Cursor, mime: String?): JSONObject {
    val name = string(cursor, OpenableColumns.DISPLAY_NAME) ?: uri.lastPathSegment ?: ""
    val size = column(cursor, OpenableColumns.SIZE)?.let { if (cursor.isNull(it)) null else cursor.getLong(it) } ?: -1L
    val flags =
      column(cursor, DocumentsContract.Document.COLUMN_FLAGS)?.let { if (cursor.isNull(it)) null else cursor.getInt(it) }
        ?: 0
    return JSONObject()
      .put("uri", uri.toString())
      .put("name", name)
      .put("dir", mime == DocumentsContract.Document.MIME_TYPE_DIR)
      .put("size", size)
      .put("virtual", (flags and DocumentsContract.Document.FLAG_VIRTUAL_DOCUMENT) != 0)
  }

  /** The index of [name] in [cursor], or null when the provider has no such column. */
  private fun column(cursor: Cursor, name: String): Int? = cursor.getColumnIndex(name).takeIf { it >= 0 }

  private fun string(cursor: Cursor, name: String): String? =
    column(cursor, name)?.let { if (cursor.isNull(it)) null else cursor.getString(it) }

  private fun failure(message: String, e: Exception?): String {
    if (e != null) Log.w(TAG, message, e) else Log.w(TAG, message)
    return JSONObject().put("error", message).toString()
  }
}
