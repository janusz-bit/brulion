// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** "Udostępnij → Brulion": the pure part of ShareActivity (filter, copy names, inbox, JSON). */
class ShareIntentTest {
  @Test
  fun textFilesByExtensionOrType() {
    assertTrue(ShareIntent.isText("Lista zakupów.md", null))
    assertTrue(ShareIntent.isText("NOTATKA.MD", "application/octet-stream"))
    assertTrue(ShareIntent.isText("plan.markdown", null))
    assertTrue(ShareIntent.isText("żółw.txt", "text/plain"))
    assertTrue(ShareIntent.isText(null, "text/plain"))
    assertTrue(ShareIntent.isText("bez rozszerzenia", " Text/Markdown "))
    assertFalse(ShareIntent.isText("zdjęcie.png", "image/png"))
    assertFalse(ShareIntent.isText(null, null))
    assertFalse(ShareIntent.isText("md", null)) // no extension: the name is "md"
    assertFalse(ShareIntent.isText("umowa.pdf", "application/pdf"))
  }

  /** Brulion's own providers are refused, also behind a "<userId>@" prefix the resolver drops. */
  @Test
  fun ownProvidersAreRecognised() {
    val pkg = "io.github.januszbit.brulion"
    assertTrue(ShareIntent.isOwnAuthority("io.github.januszbit.brulion.qtprovider", pkg))
    assertTrue(ShareIntent.isOwnAuthority("0@io.github.januszbit.brulion.qtprovider", pkg))
    assertTrue(ShareIntent.isOwnAuthority("10@0@io.github.januszbit.brulion", pkg))
    assertTrue(ShareIntent.isOwnAuthority("", pkg))
    assertTrue(ShareIntent.isOwnAuthority(null, pkg))
    assertFalse(ShareIntent.isOwnAuthority("com.android.providers.downloads.documents", pkg))
    assertFalse(ShareIntent.isOwnAuthority("0@com.google.android.apps.docs.storage", pkg))
    assertFalse(ShareIntent.isOwnAuthority("io.github.januszbit.brulionżółw", pkg))
  }

  @Test
  fun copyNamesAreSafe() {
    assertEquals("Łódź — notatka.md", ShareIntent.copyName("Łódź — notatka.md", 0))
    assertEquals("_x.md", ShareIntent.copyName("../x.md", 1).trimStart('.'))
    assertFalse(ShareIntent.copyName("../../etc/passwd", 2).contains('/'))
    assertFalse(ShareIntent.copyName("a\\b\u0000c.txt", 3).contains('\\'))
    assertFalse(ShareIntent.copyName("a\\b\u0000c.txt", 3).contains('\u0000'))
    assertEquals("ukryty.md", ShareIntent.copyName(".ukryty.md", 4))
    assertEquals("shared-5.txt", ShareIntent.copyName("", 5))
    assertEquals("shared-6.txt", ShareIntent.copyName(null, 6))
    assertEquals("shared-7.txt", ShareIntent.copyName("..", 7))
    assertEquals("shared-8.txt", ShareIntent.copyName("///", 8))
    val long = ShareIntent.copyName("ż".repeat(300) + ".md", 9)
    assertEquals(100, long.length)
    assertTrue(long.endsWith(".md"))
    // A cut never splits a surrogate pair (emoji).
    val emoji = ShareIntent.copyName("😀".repeat(80), 10)
    assertFalse(Character.isHighSurrogate(emoji.last()))
  }

  @Test
  fun inboxFoldersExpireAfterADay() {
    val now = 1_792_000_000_000L
    val name = ShareIntent.folderName(now - 1000, 3)
    assertEquals("${now - 1000}-3", name)
    assertFalse(ShareIntent.isExpired(name, now))
    assertTrue(ShareIntent.isExpired(ShareIntent.folderName(now - ShareIntent.KEEP_MS - 1, 1), now))
    assertTrue(ShareIntent.isExpired(ShareIntent.folderName(now + 3_600_000, 1), now)) // the clock went back
    assertTrue(ShareIntent.isExpired("obcy", now))
    assertTrue(ShareIntent.isExpired("123-abc", now))
  }

  @Test
  fun textIsLimited() {
    assertNull(ShareIntent.limitText(null))
    assertEquals("Zażółć", ShareIntent.limitText("Zażółć"))
    val cut = ShareIntent.limitText("a".repeat(ShareIntent.MAX_TEXT_CHARS - 1) + "😀😀")!!
    assertEquals(ShareIntent.MAX_TEXT_CHARS - 1, cut.length)
  }

  @Test
  fun jsonForCpp() {
    val files =
      listOf(
        ShareIntent.SharedFile("/data/files/share-in/1-1/0/Łódź.md", "Łódź.md", "text/markdown", 12),
        ShareIntent.SharedFile(null, "duży.txt", "text/plain", 9_999_999, "tooLarge")
      )
    val json = JSONObject(ShareIntent.toJson("Artykuł", "Zażółć https://a.pl", "/data/files/share-in/1-1", files).toString())
    assertEquals("Artykuł", json.getString("subject"))
    assertEquals("Zażółć https://a.pl", json.getString("text"))
    assertEquals("/data/files/share-in/1-1", json.getString("dir"))
    val array = json.getJSONArray("files")
    assertEquals(2, array.length())
    assertEquals("/data/files/share-in/1-1/0/Łódź.md", array.getJSONObject(0).getString("path"))
    assertEquals(12L, array.getJSONObject(0).getLong("size"))
    assertFalse(array.getJSONObject(0).has("error"))
    assertFalse(array.getJSONObject(1).has("path"))
    assertEquals("tooLarge", array.getJSONObject(1).getString("error"))
    // Empty parts are left out.
    assertEquals("{}", ShareIntent.toJson(null, "", null, emptyList()).toString())
  }

  @Test
  fun emptyShares() {
    assertTrue(ShareIntent.isEmpty(null, null, emptyList()))
    assertTrue(ShareIntent.isEmpty(" ", "\n", emptyList()))
    assertFalse(ShareIntent.isEmpty("Tytuł", null, emptyList()))
    assertFalse(ShareIntent.isEmpty(null, null, listOf(ShareIntent.SharedFile(null, "a.png", null, 1, "notText"))))
  }

  @Test
  fun previewImagesOfTextShares() {
    val image = ShareIntent.SharedFile(null, "podgląd.png", "image/png", 10, "notText")
    val foreign = ShareIntent.SharedFile(null, "miniatura.jpg", null, -1, "unreadable")
    val note = ShareIntent.SharedFile("/data/files/share-in/1-1/0/Łódź.md", "Łódź.md", "text/markdown", 12)
    val unreadableNote = ShareIntent.SharedFile(null, "notatka.txt", "text/plain", -1, "unreadable")
    // A link with a thumbnail in ClipData only: the thumbnail is no shared file.
    assertTrue(ShareIntent.isPreview(image, true, "Artykuł", "https://a.pl/żurek"))
    assertTrue(ShareIntent.isPreview(foreign, true, null, "https://a.pl"))
    // A text file, or a stream named in EXTRA_STREAM, or a share without text, stays listed.
    assertFalse(ShareIntent.isPreview(note, true, "Artykuł", "https://a.pl"))
    assertFalse(ShareIntent.isPreview(unreadableNote, true, "Artykuł", "https://a.pl"))
    assertFalse(ShareIntent.isPreview(image, false, "Artykuł", "https://a.pl"))
    assertFalse(ShareIntent.isPreview(image, true, " ", null))
  }
}
