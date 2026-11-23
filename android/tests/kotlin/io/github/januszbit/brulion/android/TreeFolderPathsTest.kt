// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayInputStream

/** The pure part of TreeFolder.kt (a folder picked in the system's dialog): paths, ids, names. */
class TreeFolderPathsTest {
  @Test
  fun splitAndJoin() {
    assertEquals(Pair("Dziennik/2026", "2026-11-22.md"), TreeFolderPaths.split("Dziennik/2026/2026-11-22.md"))
    assertEquals(Pair("", "Zażółć gęślą jaźń.md"), TreeFolderPaths.split("Zażółć gęślą jaźń.md"))
    assertEquals(Pair("", ""), TreeFolderPaths.split(""))
    assertEquals("Dziennik/łąka.md", TreeFolderPaths.join("Dziennik", "łąka.md"))
    assertEquals("łąka.md", TreeFolderPaths.join("", "łąka.md"))
  }

  @Test
  fun externalStorageIds() {
    // The phone's storage names a document by its path below the root of the picked tree.
    assertEquals("primary:Syncthing/Notatki/Dziennik/a.md", TreeFolderPaths.externalId("primary:Syncthing/Notatki", "Dziennik/a.md"))
    assertEquals("primary:Syncthing/Notatki", TreeFolderPaths.externalId("primary:Syncthing/Notatki", ""))
    // A memory card's root, picked whole.
    assertEquals("1A2B-3C4D:Notatki/a.md", TreeFolderPaths.externalId("1A2B-3C4D:", "Notatki/a.md"))
  }

  @Test
  fun validNames() {
    assertTrue(TreeFolderPaths.validName("Zażółć gęślą jaźń.md"))
    assertTrue(TreeFolderPaths.validName(".stignore"))
    assertFalse(TreeFolderPaths.validName(""))
    assertFalse(TreeFolderPaths.validName("."))
    assertFalse(TreeFolderPaths.validName(".."))
    assertFalse(TreeFolderPaths.validName("a/b"))
    assertFalse(TreeFolderPaths.validName("a\u0000b"))
  }

  @Test
  fun findingANameAmongTheChildren() {
    val decomposed = "Zaz\u0307o\u0301\u0142c\u0301.md" // "Zażółć.md" as macOS writes it (NFD)
    val names = listOf("Notatka.md", decomposed, "dziennik")
    // The same name first; then the composed form (a name macOS wrote decomposed); then letter case.
    assertEquals("Notatka.md", TreeFolderPaths.find(names, "Notatka.md"))
    assertEquals(decomposed, TreeFolderPaths.find(names, "Za\u017c\u00f3\u0142\u0107.md"))
    assertEquals("dziennik", TreeFolderPaths.find(names, "Dziennik"))
    assertNull(TreeFolderPaths.find(names, "Brak.md"))
    assertNull(TreeFolderPaths.find(emptyList(), "Notatka.md"))
    // The exact one wins over another letter case.
    assertEquals("notatka.md", TreeFolderPaths.find(listOf("Notatka.md", "notatka.md"), "notatka.md"))
  }

  @Test
  fun readingAtMost() {
    val bytes = "żółw".toByteArray()
    assertArrayEquals(bytes, TreeFolderPaths.readAtMost(ByteArrayInputStream(bytes), bytes.size.toLong()))
    assertNull(TreeFolderPaths.readAtMost(ByteArrayInputStream(bytes), bytes.size.toLong() - 1))
    assertArrayEquals(ByteArray(0), TreeFolderPaths.readAtMost(ByteArrayInputStream(ByteArray(0)), 0))
    val big = ByteArray(200_000) { (it % 251).toByte() }
    assertArrayEquals(big, TreeFolderPaths.readAtMost(ByteArrayInputStream(big), 200_000))
  }
}
