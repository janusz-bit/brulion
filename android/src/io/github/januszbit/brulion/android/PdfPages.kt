// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.Matrix
import android.graphics.RectF
import android.graphics.pdf.PdfRenderer
import android.os.Build
import android.os.ParcelFileDescriptor
import android.util.Log
import androidx.annotation.RequiresApi
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.nio.ByteBuffer

/**
 * PDFs through the system's [PdfRenderer] (API 21+), behind the `pdf*` functions of
 * [BrulionAndroid] (`src/platform/pdfpages_android.cpp`, `PdfReader`): a drawing's backdrop, the
 * thumbnails of embedded PDFs and the PDF viewer — without Qt PDF's 4.5 MB in the APK.
 *
 * A session is an open PDF (its descriptor and renderer, opened once); its pages are opened per
 * call (PdfRenderer allows one open page at a time: every call is synchronized on its session). At
 * most [MAX_SESSIONS] are open — the oldest is closed — so a leak on the C++ side cannot exhaust the
 * process's file descriptors. Called from C++ worker threads: nothing here needs the main thread.
 *
 * Text (the viewer's search) needs Android 15 (API 35: [PdfRenderer.Page.getTextContents],
 * [PdfRenderer.Page.searchText]); [textAvailable] probes once whether this system has them (an OEM
 * build might not). PdfRenderer's own search is case-insensitive only: C++ finds the spellings of a
 * match in the page's text with its own folding (diacritics too) and asks [search] for each.
 */
internal object PdfPages {
  private const val TAG = "BrulionPdfPages"

  /** The most sessions open at once (each holds a file descriptor and pdfium's document). */
  const val MAX_SESSIONS = 6

  /** The largest picture one [renderInto] draws (src/platform/pdfpages.h MaxPdfClipPixels). */
  private const val MAX_PIXELS = 16_000_000L

  /** The largest page drawn (MaxPdfFullSide). */
  private const val MAX_FULL_SIDE = 32768

  private class Session(val descriptor: ParcelFileDescriptor, val renderer: PdfRenderer) {
    fun close() {
      try {
        renderer.close()
      } catch (e: Exception) {
        Log.w(TAG, "closing a renderer: $e")
      }
      try {
        descriptor.close()
      } catch (e: Exception) {
        Log.w(TAG, "closing a descriptor: $e")
      }
    }
  }

  /** Open sessions, the oldest first. */
  private val sessions = LinkedHashMap<Int, Session>()
  private var nextSession = 1

  private fun session(id: Int): Session? = synchronized(sessions) { sessions[id] }

  /**
   * `{"session", "pages", "size": [w, h] (the first page, points), "locked", "error"}` of the PDF at
   * [path]; session 0 when it could not be opened.
   */
  fun open(path: String): String {
    var descriptor: ParcelFileDescriptor? = null
    return try {
      descriptor = ParcelFileDescriptor.open(File(path), ParcelFileDescriptor.MODE_READ_ONLY)
      val renderer = PdfRenderer(descriptor)
      val pages = renderer.pageCount
      val size = if (pages > 0) renderer.openPage(0).use { JSONArray().put(it.width).put(it.height) } else JSONArray()
      val session = Session(descriptor, renderer)
      val evicted = ArrayList<Session>()
      val id: Int
      synchronized(sessions) {
        id = nextSession++
        sessions[id] = session
        while (sessions.size > MAX_SESSIONS) {
          val oldest = sessions.keys.first()
          sessions.remove(oldest)?.let { evicted.add(it) }
        }
      }
      for (old in evicted) synchronized(old) { old.close() }
      openJson(id, pages, size, false, null)
    } catch (e: SecurityException) {
      // A password-protected PDF (PdfRenderer opens none).
      descriptor?.close()
      openJson(0, -1, null, true, e.message ?: "The PDF is protected with a password")
    } catch (e: Exception) {
      Log.w(TAG, "cannot open $path: $e")
      descriptor?.close()
      openJson(0, -1, null, false, e.toString())
    }
  }

  /** The JSON of [open] (pure; PdfJsonTest). */
  fun openJson(session: Int, pages: Int, size: JSONArray?, locked: Boolean, error: String?): String {
    val out = JSONObject().put("session", session).put("pages", pages).put("locked", locked)
    if (size != null) out.put("size", size)
    if (error != null) out.put("error", error)
    return out.toString()
  }

  /** `[[w, h], …]` of pages [from] … [from] + [count] − 1 (1-based, points); `[]` for no session. */
  fun pageSizes(id: Int, from: Int, count: Int): String {
    val s = session(id) ?: return "[]"
    val out = JSONArray()
    synchronized(s) {
      val last = minOf(s.renderer.pageCount, from + count - 1)
      for (page in maxOf(1, from)..last) {
        try {
          s.renderer.openPage(page - 1).use { out.put(JSONArray().put(it.width).put(it.height)) }
        } catch (e: Exception) {
          out.put(JSONArray().put(0).put(0))
        }
      }
    }
    return out.toString()
  }

  /**
   * Page [page] scaled to [fullW] × [fullH] pixels, of it the part at ([clipX], [clipY]) of
   * [clipW] × [clipH], on white, into [out] (a direct buffer over the C++ picture's own pixels:
   * RGBA_8888, premultiplied, rows of clipW × 4 bytes). False when it could not be drawn.
   */
  fun renderInto(
    id: Int,
    page: Int,
    fullW: Int,
    fullH: Int,
    clipX: Int,
    clipY: Int,
    clipW: Int,
    clipH: Int,
    out: ByteBuffer
  ): Boolean {
    if (!renderable(fullW, fullH, clipX, clipY, clipW, clipH) || out.capacity() < clipW.toLong() * clipH * 4)
      return false
    val s = session(id) ?: return false
    var bitmap: Bitmap? = null
    return try {
      bitmap = Bitmap.createBitmap(clipW, clipH, Bitmap.Config.ARGB_8888)
      bitmap.eraseColor(Color.WHITE)
      synchronized(s) {
        if (page < 1 || page > s.renderer.pageCount) return false
        s.renderer.openPage(page - 1).use { p ->
          val matrix = Matrix()
          matrix.setScale(fullW / p.width.toFloat(), fullH / p.height.toFloat())
          matrix.postTranslate(-clipX.toFloat(), -clipY.toFloat())
          p.render(bitmap, null, matrix, PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY)
        }
      }
      out.rewind()
      bitmap.copyPixelsToBuffer(out)
      true
    } catch (e: Exception) {
      Log.w(TAG, "cannot render page $page: $e")
      false
    } finally {
      bitmap?.recycle()
    }
  }

  /** The sizes [renderInto] draws (pure; PdfJsonTest). */
  fun renderable(fullW: Int, fullH: Int, clipX: Int, clipY: Int, clipW: Int, clipH: Int): Boolean =
    fullW in 1..MAX_FULL_SIDE && fullH in 1..MAX_FULL_SIDE && clipW > 0 && clipH > 0 && clipX >= 0 &&
      clipY >= 0 && clipX.toLong() + clipW <= fullW && clipY.toLong() + clipH <= fullH &&
      clipW.toLong() * clipH <= MAX_PIXELS

  @Volatile private var textProbe: Boolean? = null

  /** This system has PdfRenderer's text APIs (Android 15+, and the methods are there). */
  fun textAvailable(): Boolean {
    textProbe?.let { return it }
    val yes = Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM && try {
      PdfText.probe()
    } catch (e: Exception) {
      Log.i(TAG, "no PDF text APIs: $e")
      false
    } catch (e: LinkageError) {
      Log.i(TAG, "no PDF text APIs: $e")
      false
    }
    textProbe = yes
    return yes
  }

  /** The text of page [page] (its text contents joined by line breaks); null without text APIs. */
  fun pageText(id: Int, page: Int): String? {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.VANILLA_ICE_CREAM || !textAvailable()) return null
    val s = session(id) ?: return null
    return try {
      synchronized(s) {
        if (page < 1 || page > s.renderer.pageCount) return null
        s.renderer.openPage(page - 1).use { PdfText.text(it) }
      }
    } catch (e: Exception) {
      Log.w(TAG, "no text of page $page: $e")
      null
    } catch (e: LinkageError) {
      null
    }
  }

  /**
   * PdfRenderer's matches of each of [spellings] on page [page], merged by where they start:
   * `[{"start": i, "rects": [[l, t, r, b], …]}, …]` (points); `[]` without text APIs.
   */
  fun search(id: Int, page: Int, spellings: Array<String>): String {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.VANILLA_ICE_CREAM || !textAvailable()) return "[]"
    val s = session(id) ?: return "[]"
    val found = sortedMapOf<Int, List<RectF>>()
    try {
      synchronized(s) {
        if (page < 1 || page > s.renderer.pageCount) return "[]"
        s.renderer.openPage(page - 1).use { p ->
          for (spelling in spellings.distinct()) {
            if (spelling.isBlank()) continue
            for ((start, rects) in PdfText.search(p, spelling)) found.putIfAbsent(start, rects)
          }
        }
      }
    } catch (e: Exception) {
      Log.w(TAG, "cannot search page $page: $e")
    } catch (e: LinkageError) {
      return "[]"
    }
    return matchesJson(found.map { (start, rects) -> start to rects.map { floatArrayOf(it.left, it.top, it.right, it.bottom) } })
  }

  /** `[{"start", "rects": [[l, t, r, b], …]}, …]` of matches (pure; PdfJsonTest). */
  fun matchesJson(matches: List<Pair<Int, List<FloatArray>>>): String {
    val out = JSONArray()
    for ((start, rects) in matches) {
      val list = JSONArray()
      for (r in rects) list.put(JSONArray().put(r[0].toDouble()).put(r[1].toDouble()).put(r[2].toDouble()).put(r[3].toDouble()))
      out.put(JSONObject().put("start", start).put("rects", list))
    }
    return out.toString()
  }

  fun close(id: Int) {
    val s = synchronized(sessions) { sessions.remove(id) } ?: return
    synchronized(s) { s.close() }
  }

  /** Sessions open (tests, the bridge self-test). */
  fun openSessions(): Int = synchronized(sessions) { sessions.size }

  /** Android 15's text APIs, only touched where they exist. */
  @RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
  private object PdfText {
    fun probe(): Boolean {
      // Resolving the methods throws NoSuchMethodError / NoClassDefFoundError where they are missing.
      PdfRenderer.Page::class.java.getMethod("getTextContents")
      PdfRenderer.Page::class.java.getMethod("searchText", String::class.java)
      return true
    }

    fun text(page: PdfRenderer.Page): String = page.textContents.joinToString("\n") { it.text }

    fun search(page: PdfRenderer.Page, query: String): List<Pair<Int, List<RectF>>> =
      page.searchText(query).map { it.textStartIndex to it.bounds }
  }
}
