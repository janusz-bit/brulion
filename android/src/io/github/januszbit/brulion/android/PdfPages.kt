// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.pdf.PdfRenderer
import android.os.ParcelFileDescriptor
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * PDF pages as pictures through the system's [PdfRenderer] (API 21+), behind
 * [BrulionAndroid.pdfInfo] and [BrulionAndroid.renderPdfPage]: a PDF page as a drawing's backdrop
 * (`src/platform/pdfpages.h`) without Qt PDF's 4.5 MB in the APK. Every call opens the file and
 * closes the page, the renderer and the descriptor again; the platform serialises PdfRenderer's
 * pdfium itself.
 *
 * Called from C++ worker threads (the page picker's thumbnails, the backdrops being prepared):
 * nothing here needs the main thread or keeps state.
 */
internal object PdfPages {
  private const val TAG = "BrulionPdfPages"

  /** The largest long side drawn (src/platform/pdfpages.h MaxPdfPageSide). */
  private const val MAX_SIDE = 8192

  fun info(path: String): String {
    val out = JSONObject()
    try {
      ParcelFileDescriptor.open(File(path), ParcelFileDescriptor.MODE_READ_ONLY).use { descriptor ->
        PdfRenderer(descriptor).use { renderer ->
          val sizes = JSONArray()
          for (i in 0 until renderer.pageCount) {
            renderer.openPage(i).use { page -> sizes.put(JSONArray().put(page.width).put(page.height)) }
          }
          out.put("pages", renderer.pageCount)
          out.put("sizes", sizes)
        }
      }
    } catch (e: SecurityException) {
      // A password-protected PDF (PdfRenderer opens none).
      out.put("pages", -1)
      out.put("locked", true)
      out.put("error", e.message ?: "The PDF is protected with a password")
    } catch (e: Exception) {
      Log.w(TAG, "cannot read $path: $e")
      out.put("pages", -1)
      out.put("error", e.toString())
    }
    return out.toString()
  }

  fun render(path: String, page: Int, maxSide: Int): ByteArray? {
    return try {
      ParcelFileDescriptor.open(File(path), ParcelFileDescriptor.MODE_READ_ONLY).use { descriptor ->
        PdfRenderer(descriptor).use { renderer ->
          if (page < 1 || page > renderer.pageCount) return null
          renderer.openPage(page - 1).use { pdfPage -> pixels(pdfPage, maxSide.coerceIn(1, MAX_SIDE)) }
        }
      }
    } catch (e: Exception) {
      Log.w(TAG, "cannot render page $page of $path: $e")
      null
    }
  }

  private fun pixels(page: PdfRenderer.Page, side: Int): ByteArray? {
    val longSide = max(page.width, page.height)
    if (page.width <= 0 || page.height <= 0) return null
    val scale = side.toDouble() / longSide
    val width = (page.width * scale).roundToInt().coerceIn(1, side)
    val height = (page.height * scale).roundToInt().coerceIn(1, side)
    val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
    try {
      bitmap.eraseColor(Color.WHITE)
      page.render(bitmap, null, null, PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY)
      val buffer = ByteBuffer.allocate(width * height * 4)
      bitmap.copyPixelsToBuffer(buffer)
      val out = ByteArray(8 + width * height * 4)
      ByteBuffer.wrap(out, 0, 8).order(ByteOrder.BIG_ENDIAN).putInt(width).putInt(height)
      System.arraycopy(buffer.array(), 0, out, 8, width * height * 4)
      return out
    } finally {
      bitmap.recycle()
    }
  }
}
