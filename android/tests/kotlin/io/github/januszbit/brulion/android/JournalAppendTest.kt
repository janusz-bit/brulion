// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.nio.file.Files

/**
 * Quick add's writing with Brulion closed (JournalAppend.kt): the same lines and appends as the C++
 * (tests/data/widgets/journal-cases.json, checked by tests/app/widgets/tst_widgets.cpp too), the
 * note written atomically and only inside the notebook.
 */
class JournalAppendTest {
  @get:Rule val tmp = TemporaryFolder()

  private fun cases(): JSONObject {
    val dir = System.getProperty("brulion.testData") ?: error("brulion.testData is not set (unit-tests.gradle)")
    return JSONObject(File(dir, "widgets/journal-cases.json").readText(Charsets.UTF_8))
  }

  @Test
  fun linesAreTheAppsLines() {
    val lines = cases().getJSONArray("lines")
    assertTrue(lines.length() >= 20)
    for (i in 0 until lines.length()) {
      val case = lines.getJSONObject(i)
      val action = WidgetAction.fromJson(case.getJSONObject("action").put("id", "case"))
      assertNotNull(case.getString("name"), action)
      assertEquals(case.getString("name"), case.getString("line"), JournalAppend.actionBlock(action!!))
    }
  }

  @Test
  fun appendsAreTheAppsAppends() {
    val appends = cases().getJSONArray("append")
    assertTrue(appends.length() >= 15)
    for (i in 0 until appends.length()) {
      val case = appends.getJSONObject(i)
      assertEquals(
        case.getString("name"),
        case.getString("expected"),
        JournalAppend.appendBlock(case.getString("content"), case.getString("block"))
      )
    }
  }

  @Test
  fun qtTrimTrimsWhatQtTrims() {
    assertEquals("ż", JournalAppend.qtTrim("\u00A0\t ż\u0085  \r\n"))
    // A BOM is not white space (it stays at the start of a note).
    assertEquals("\uFEFFa", JournalAppend.qtTrim("\uFEFFa "))
  }

  @Test
  fun writesTheNoteAtomicallyWithItsBytes() {
    val root = tmp.newFolder("Brulion")
    val note = File(root, "journals/2026-10-27.md")
    note.parentFile!!.mkdirs()
    note.writeBytes("\uFEFF# Wtorek\r\n- ".toByteArray(Charsets.UTF_8))
    val result = JournalAppend.appendToFile(root, "journals/2026-10-27.md", "", "- [ ] Kupić żółty ser 📅 2026-10-28")
    assertTrue(result.error, result.ok)
    assertEquals(1, result.line)
    assertEquals("\uFEFF# Wtorek\r\n- [ ] Kupić żółty ser 📅 2026-10-28\r\n", note.readText(Charsets.UTF_8))
    assertEquals(listOf("2026-10-27.md"), note.parentFile!!.list()!!.toList()) // no temporary file left
  }

  @Test
  fun createsAMissingNoteFromTheTemplateText() {
    val root = tmp.newFolder("Brulion")
    val result = JournalAppend.appendToFile(root, "Dziennik/2026-10-28.md", "# środa, 28 października\n\n- ", "- Notatka")
    assertTrue(result.error, result.ok)
    assertEquals(2, result.line)
    assertEquals("# środa, 28 października\n\n- Notatka\n", File(root, "Dziennik/2026-10-28.md").readText())
  }

  @Test
  fun refusesPathsOutsideTheNotebook() {
    val root = tmp.newFolder("Brulion")
    val outside = File(tmp.root, "outside.md")
    for (path in listOf("../outside.md", "journals/../../outside.md", "/etc/passwd.md", "", "journals/x.txt", "a\u0000.md")) {
      val result = JournalAppend.appendToFile(root, path, "", "- x")
      assertFalse(path, result.ok)
    }
    assertFalse(outside.exists())
    // Nor through a symbolic link leaving it.
    val elsewhere = tmp.newFolder("elsewhere")
    Files.createSymbolicLink(File(root, "link").toPath(), elsewhere.toPath())
    assertNull(JournalAppend.resolveInside(root, "link/note.md"))
    assertFalse(JournalAppend.appendToFile(root, "link/note.md", "", "- x").ok)
    assertTrue(elsewhere.list()!!.isEmpty())
    assertNotNull(JournalAppend.resolveInside(root, "journals/./2026-10-27.md"))
  }

  @Test
  fun leavesANoteThatIsNotUtf8Alone() {
    val root = tmp.newFolder("Brulion")
    val note = File(root, "stara.md")
    val bytes = byteArrayOf(0x2d, 0x20, 0xb3.toByte(), 0xf3.toByte(), 0x64, 0x9f.toByte(), 0x0a) // CP1250 "- łódź"
    note.writeBytes(bytes)
    assertFalse(JournalAppend.appendToFile(root, "stara.md", "", "- x").ok)
    assertTrue(bytes.contentEquals(note.readBytes()))
  }

  /** A notebook folder that is gone (moved, deleted, not mounted) is never re-created. */
  @Test
  fun neverRecreatesAMissingNotebookFolder() {
    val root = File(tmp.root, "Notatki")
    val result = JournalAppend.appendToFile(root, "journals/2026-10-28.md", "# środa\n\n- ", "- [ ] Zadzwonić do Łucji")
    assertFalse(result.ok)
    assertEquals("no notebook folder", result.error)
    assertFalse(root.exists())
    // A file in its place is no notebook either.
    root.writeText("nie folder")
    assertFalse(JournalAppend.appendToFile(root, "journals/2026-10-28.md", "", "- x").ok)
    assertEquals("nie folder", root.readText())
  }

  @Test
  fun refusesAFolderAndABlankBlock() {
    val root = tmp.newFolder("Brulion")
    File(root, "folder.md").mkdirs()
    assertFalse(JournalAppend.appendToFile(root, "folder.md", "", "- x").ok)
    assertFalse(JournalAppend.appendToFile(root, "nowa.md", "", "  ").ok)
    assertFalse(File(root, "nowa.md").exists())
  }

  @Test
  fun splitLinesKeepsEveryTerminator() {
    val lines = JournalAppend.splitLines("a\r\nb\rc\n")
    assertEquals(
      listOf(JournalAppend.Line("a", "\r\n"), JournalAppend.Line("b", "\r"), JournalAppend.Line("c", "\n"), JournalAppend.Line("", "")),
      lines
    )
    assertEquals("\r", JournalAppend.detectEol("x\ry\n"))
    assertEquals("\n", JournalAppend.detectEol("bez końca"))
  }
}
