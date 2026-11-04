// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.app.Activity
import android.content.Context
import android.os.Bundle
import android.os.CancellationSignal
import android.os.ParcelFileDescriptor
import android.print.PageRange
import android.print.PrintAttributes
import android.print.PrintDocumentAdapter
import android.print.PrintDocumentInfo
import android.print.PrintManager
import android.util.Log
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.IOException

/**
 * "Drukuj…" of a page ([BrulionAndroid.printPdf], `ExportController::print` in the app): the PDF
 * Brulion made of it (the PDF export: its pages, A4 or Letter by the device's locale) handed to the
 * system's print service — PrintManager with a [PrintDocumentAdapter] that copies the file, no
 * WebView. The printers, the copies and "Zapisz jako PDF" are the system's; the file (in the app's
 * cache) is deleted when the print job's dialog is done with it.
 */
internal object PdfPrint {
  private const val TAG = "BrulionPrint"

  /** Opens the system's print dialog for [file] as the job [name]; false without a print service. */
  fun print(activity: Activity, file: File, name: String): Boolean {
    val manager = activity.getSystemService(Context.PRINT_SERVICE) as? PrintManager ?: return false
    return try {
      manager.print(name, FileAdapter(file, name), PrintAttributes.Builder().build())
      true
    } catch (e: RuntimeException) {
      Log.w(TAG, "print failed", e)
      false
    }
  }

  private class FileAdapter(private val file: File, private val name: String) : PrintDocumentAdapter() {
    override fun onLayout(
      oldAttributes: PrintAttributes?,
      newAttributes: PrintAttributes,
      cancellationSignal: CancellationSignal?,
      callback: LayoutResultCallback,
      extras: Bundle?
    ) {
      if (cancellationSignal?.isCanceled == true) {
        callback.onLayoutCancelled()
        return
      }
      // The pages are the PDF's own: a new paper size or orientation changes nothing in it.
      val info =
        PrintDocumentInfo.Builder(name)
          .setContentType(PrintDocumentInfo.CONTENT_TYPE_DOCUMENT)
          .setPageCount(PrintDocumentInfo.PAGE_COUNT_UNKNOWN)
          .build()
      callback.onLayoutFinished(info, oldAttributes == null)
    }

    override fun onWrite(
      pages: Array<out PageRange>?,
      destination: ParcelFileDescriptor,
      cancellationSignal: CancellationSignal?,
      callback: WriteResultCallback
    ) {
      try {
        FileInputStream(file).use { input ->
          FileOutputStream(destination.fileDescriptor).use { output -> input.copyTo(output) }
        }
        if (cancellationSignal?.isCanceled == true) {
          callback.onWriteCancelled()
        } else {
          callback.onWriteFinished(arrayOf(PageRange.ALL_PAGES))
        }
      } catch (e: IOException) {
        Log.w(TAG, "could not hand the PDF over", e)
        callback.onWriteFailed(e.message)
      }
    }

    override fun onFinish() {
      if (!file.delete()) Log.i(TAG, "the printed PDF stays in the cache: ${file.name}")
    }
  }
}
