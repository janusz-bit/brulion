// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** The pure part of OpenWith.kt ("Otwórz w innej aplikacji"): the provider's roots, the copy's name. */
class OpenWithTest {
  private val roots = listOf("/data/user/0/io.github.januszbit.brulion/files", "/data/user/0/io.github.januszbit.brulion/cache", "/storage/emulated/0")

  @Test
  fun insideTheProvidersRoots() {
    assertTrue(OpenWith.insideRoots("/storage/emulated/0/Notatki/assets/wykład ąę.pdf", roots))
    assertTrue(OpenWith.insideRoots("/data/user/0/io.github.januszbit.brulion/files/Przykładowy brulion/a.pdf", roots))
    assertTrue(OpenWith.insideRoots("/data/user/0/io.github.januszbit.brulion/cache/open/a.pdf", roots))
    // A memory card, another app's files, a folder whose name starts like a root.
    assertFalse(OpenWith.insideRoots("/storage/1A2B-3C4D/Notatki/a.pdf", roots))
    assertFalse(OpenWith.insideRoots("/data/user/0/com.example/files/a.pdf", roots))
    assertFalse(OpenWith.insideRoots("/storage/emulated/0x/a.pdf", roots))
    assertFalse(OpenWith.insideRoots("/storage/emulated/0/a.pdf", emptyList()))
    assertFalse(OpenWith.insideRoots("/storage/emulated/0/a.pdf", listOf("")))
  }

  @Test
  fun copyNames() {
    assertEquals("Wykład 3 — ćwiczenia.pdf", OpenWith.copyName("Wykład 3 — ćwiczenia.pdf"))
    assertEquals("ab.pdf", OpenWith.copyName("a/b.pdf"))
    assertEquals("ab.pdf", OpenWith.copyName("a\\b.pdf"))
    assertEquals("ab.pdf", OpenWith.copyName("a\u0000b\u0007.pdf"))
    assertEquals("ukryty.pdf", OpenWith.copyName("..ukryty.pdf"))
    assertEquals("plik.pdf", OpenWith.copyName(""))
    assertEquals("plik.pdf", OpenWith.copyName("   "))
    assertEquals("plik.pdf", OpenWith.copyName("/"))
    val long = "ż".repeat(300) + ".pdf"
    val copy = OpenWith.copyName(long)
    assertEquals(120, copy.length)
    assertTrue(copy.endsWith(".pdf"))
  }
}
