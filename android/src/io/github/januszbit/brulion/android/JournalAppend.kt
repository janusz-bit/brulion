// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.charset.CharacterCodingException
import java.nio.charset.CodingErrorAction
import java.time.LocalDate
import java.time.LocalTime
import java.time.format.DateTimeFormatter
import java.time.format.DateTimeParseException
import java.util.UUID

/**
 * What quick add of a widget writes into today's daily note while Brulion is closed — the only
 * place Kotlin writes into a notebook (docs/ARCHITECTURE.md). A port of the C++ that writes the
 * same entry when the app runs: [actionBlock] is `widgets::widgetActionBlock()` (the task line of
 * `planning::taskLine()`, a note of `notebook::blockFromText()`), [appendBlock] is
 * `notebook::appendBlock()`; both are checked against tests/data/widgets/journal-cases.json, as
 * the C++ is. No Markdown is parsed here: the snapshot told where to write.
 *
 * [appendToFile] writes atomically (a temporary file in the same folder, renamed over the note)
 * and only inside the notebook's folder; a note another program changed meanwhile is left alone.
 */
internal object JournalAppend {
  private const val TEMP_PREFIX = ".brulion-tmp-"

  // --- the entry -------------------------------------------------------------------------------

  /** The block [action] writes, "" for blank text (see widgets::widgetActionBlock()). */
  fun actionBlock(action: WidgetAction): String {
    if (qtTrim(action.text).isEmpty()) return ""
    if (action.kind == WidgetAction.KIND_NOTE) return blockFromText(action.text)
    val date = parseDate(action.date) ?: return taskLine(action.text)
    val time = parseTime(action.time)
    return if (time != null) {
      taskLine(action.text, null, "${date} ${hhmm(time)}", action.alarm)
    } else {
      taskLine(action.text, date.toString(), null, false)
    }
  }

  /** "- [ ] <text>" plus " 📅 <due>" and " ⏰ <reminder>" (" ⏰! …" with [alarm]). */
  fun taskLine(text: String, due: String? = null, reminder: String? = null, alarm: Boolean = false): String {
    val line = StringBuilder("- [ ] ").append(cleanItemText(text))
    if (!due.isNullOrEmpty()) line.append(" 📅 ").append(due)
    if (!reminder.isNullOrEmpty()) line.append(if (alarm) " ⏰! " else " ⏰ ").append(reminder)
    return line.toString()
  }

  private val ITEM_MARKER = Regex("^\\s*(?:[-*+]|\\d+[.)])\\s+")
  private val ITEM_CHECKBOX = Regex("^\\[[ xX/>\\-]\\]\\s*")
  private val LINE_BREAKS = Regex("\\s*[\\r\\n]+\\s*")
  private val LIST_MARKER = Regex("^([-*+]|\\d+[.)])\\s")
  private val EMPTY_BULLET = Regex("^\\s*[-*+]\\s*$")

  /** The user's text as one line, without a list marker or checkbox of its own. */
  fun cleanItemText(text: String): String {
    var t = qtTrim(LINE_BREAKS.replace(text, " "))
    t = ITEM_MARKER.replaceFirst(t, "")
    t = ITEM_CHECKBOX.replaceFirst(t, "")
    return qtTrim(t)
  }

  /** Free text as one block: "- " unless it starts with a list marker, further lines indented. */
  fun blockFromText(text: String): String {
    val lines = text.replace("\r", "").split('\n').toMutableList()
    while (lines.isNotEmpty() && qtTrim(lines.first()).isEmpty()) lines.removeAt(0)
    while (lines.isNotEmpty() && qtTrim(lines.last()).isEmpty()) lines.removeAt(lines.size - 1)
    if (lines.isEmpty()) return ""
    var first = qtTrim(lines.first())
    if (!LIST_MARKER.containsMatchIn(first)) first = "- $first"
    val block = StringBuilder(first)
    for (i in 1 until lines.size) block.append("\n  ").append(qtTrim(lines[i]))
    return block.toString()
  }

  // --- appending -------------------------------------------------------------------------------

  /** One line of a note and its terminator ("\n", "\r\n", "\r" or "" for the last). */
  data class Line(val text: String, val eol: String)

  /** The lines of [content] with their terminators exactly (text::splitLines()). */
  fun splitLines(content: String): List<Line> {
    val lines = ArrayList<Line>()
    var start = 0
    var i = 0
    val n = content.length
    while (i < n) {
      val c = content[i]
      if (c != '\n' && c != '\r') {
        i++
        continue
      }
      val eol = if (c == '\r' && i + 1 < n && content[i + 1] == '\n') "\r\n" else c.toString()
      lines.add(Line(content.substring(start, i), eol))
      i += eol.length
      start = i
    }
    lines.add(Line(content.substring(start), ""))
    return lines
  }

  /** The note's line ending: its first one, "\n" when it has none (text::detectEol()). */
  fun detectEol(content: String): String {
    val i = content.indexOfFirst { it == '\n' || it == '\r' }
    if (i < 0 || content[i] == '\n') return "\n"
    return if (i + 1 < content.length && content[i + 1] == '\n') "\r\n" else "\r"
  }

  /**
   * [content] with [block] appended as its last block in the note's line ending: trailing blank
   * lines go, a template's empty last bullet is filled; every other character stays.
   */
  fun appendBlock(content: String, block: String): String {
    if (qtTrim(block).isEmpty()) return content
    val eol = detectEol(content)
    val existing = splitLines(content).toMutableList()
    while (existing.isNotEmpty() && qtTrim(existing.last().text).isEmpty()) existing.removeAt(existing.size - 1)
    if (existing.isNotEmpty() && EMPTY_BULLET.containsMatchIn(existing.last().text)) existing.removeAt(existing.size - 1)
    val out = StringBuilder()
    for (line in existing) out.append(line.text).append(line.eol)
    if (out.isNotEmpty() && !out.endsWith("\n") && !out.endsWith("\r")) out.append(eol)
    out.append(block.replace("\r", "").replace("\n", eol)).append(eol)
    return out.toString()
  }

  /** The outcome of [appendToFile]. */
  data class Result(
    val ok: Boolean,
    /** The note (absolute) when written. */
    val file: File? = null,
    /** 0-based line of the block's first line in the note. */
    val line: Int = -1,
    val error: String = ""
  )

  /**
   * Appends [block] to the note [path] (vault-relative) of the notebook folder [root]; a missing
   * note is created with [newText] (the journal template, as the app would). Refused: a
   * notebook folder that is not there (moved, deleted, storage not mounted: never re-created —
   * the app does not make an empty notebook in its place either), a path outside the folder
   * (also through `..` or a symbolic link), not a `.md` note, a note that is not UTF-8 text, a
   * note changed by another program while this one wrote.
   */
  fun appendToFile(root: File, path: String, newText: String, block: String): Result {
    if (qtTrim(block).isEmpty()) return Result(false, error = "empty")
    if (!path.endsWith(".md", ignoreCase = true)) return Result(false, error = "not a note")
    try {
      if (!root.isDirectory) return Result(false, error = "no notebook folder")
    } catch (e: SecurityException) {
      return Result(false, error = e.toString())
    }
    val target = resolveInside(root, path) ?: return Result(false, error = "outside the notebook")
    try {
      val exists = target.exists()
      if (exists && !target.isFile) return Result(false, error = "not a file")
      val size = if (exists) target.length() else -1L
      val mtime = if (exists) target.lastModified() else 0L
      val content = if (exists) decodeUtf8(target.readBytes()) ?: return Result(false, error = "not UTF-8") else newText
      val out = appendBlock(content, block)
      val parent = target.parentFile ?: return Result(false, error = "no folder")
      if (!parent.isDirectory && !parent.mkdirs()) return Result(false, error = "cannot create the folder")
      val temp = File(parent, TEMP_PREFIX + UUID.randomUUID().toString())
      try {
        FileOutputStream(temp).use { stream ->
          stream.write(out.toByteArray(Charsets.UTF_8))
          stream.fd.sync()
        }
        val unchanged = if (exists) target.isFile && target.length() == size && target.lastModified() == mtime else !target.exists()
        if (!unchanged) return Result(false, error = "changed meanwhile")
        if (!temp.renameTo(target)) return Result(false, error = "cannot replace the note")
      } finally {
        if (temp.exists()) temp.delete()
      }
      val blockLines = block.replace("\r", "").split('\n').size
      // `out` ends with a line ending: its last "line" is the empty one after it.
      val line = splitLines(out).size - 1 - blockLines
      return Result(true, target, line)
    } catch (e: IOException) {
      return Result(false, error = e.toString())
    } catch (e: SecurityException) {
      return Result(false, error = e.toString())
    }
  }

  /** [path] below [root], or null when it would leave it (`..`, an absolute path, a symlink). */
  fun resolveInside(root: File, path: String): File? {
    if (path.isEmpty() || path.startsWith("/") || path.contains('\u0000')) return null
    return try {
      val base = root.canonicalFile
      val file = File(base, path).canonicalFile
      if (file.path.startsWith(base.path + File.separator)) file else null
    } catch (e: IOException) {
      null
    }
  }

  private fun decodeUtf8(bytes: ByteArray): String? =
    try {
      Charsets.UTF_8.newDecoder()
        .onMalformedInput(CodingErrorAction.REPORT)
        .onUnmappableCharacter(CodingErrorAction.REPORT)
        .decode(ByteBuffer.wrap(bytes))
        .toString()
    } catch (e: CharacterCodingException) {
      null
    }

  // --- helpers ----------------------------------------------------------------------------------

  /** QChar::isSpace(): Unicode separators and the ASCII/Latin-1 white space Qt trims. */
  fun isQtSpace(c: Char): Boolean =
    c == ' ' || c in '\t'..'\r' || c == '\u0085' || c == '\u00A0' ||
      Character.getType(c).let {
        it == Character.SPACE_SEPARATOR.toInt() || it == Character.LINE_SEPARATOR.toInt() ||
          it == Character.PARAGRAPH_SEPARATOR.toInt()
      }

  /** QString::trimmed(). */
  fun qtTrim(s: String): String = s.trim { isQtSpace(it) }

  private val ISO_TIME = DateTimeFormatter.ofPattern("HH:mm")

  /** A strict "YYYY-MM-DD", else null. */
  fun parseDate(text: String?): LocalDate? {
    if (text == null || text.length != 10) return null
    return try {
      LocalDate.parse(text)
    } catch (e: DateTimeParseException) {
      null
    }
  }

  /** A strict "HH:mm", else null. */
  fun parseTime(text: String?): LocalTime? {
    if (text == null || text.length != 5) return null
    return try {
      LocalTime.parse(text, ISO_TIME)
    } catch (e: DateTimeParseException) {
      null
    }
  }

  fun hhmm(time: LocalTime): String = ISO_TIME.format(time)
}
