// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.junit.Assert.assertEquals
import org.junit.Test

/** The keyboard's overlap of Qt's view, in physical pixels (plain JVM: no Android API is called). */
class ImeInsetsTest {
  // A 1080x2400 phone; Qt's view fills the screen (edge-to-edge).
  private val screen = 2400

  @Test
  fun hiddenKeyboardCoversNothing() {
    assertEquals(0, ImeInsets.overlap(viewTop = 0, viewBottom = screen, windowBottom = screen, imeBottom = 0))
    assertEquals(0, ImeInsets.overlap(viewTop = 0, viewBottom = screen, windowBottom = screen, imeBottom = -5))
  }

  @Test
  fun edgeToEdgeWindowIsCoveredByTheWholeKeyboard() {
    // Android 15+: the window keeps its size, the keyboard (900 px, navigation bar included)
    // lies over its bottom.
    assertEquals(900, ImeInsets.overlap(viewTop = 0, viewBottom = screen, windowBottom = screen, imeBottom = 900))
  }

  @Test
  fun resizedWindowIsNotCovered() {
    // Android 9–14 without edge-to-edge: the system made Qt's view end at the keyboard's top.
    assertEquals(0, ImeInsets.overlap(viewTop = 0, viewBottom = screen - 900, windowBottom = screen, imeBottom = 900))
    // ... or above it (a status bar's worth of padding).
    assertEquals(0, ImeInsets.overlap(viewTop = 63, viewBottom = screen - 1000, windowBottom = screen, imeBottom = 900))
  }

  @Test
  fun partlyResizedViewIsCoveredByTheRest() {
    // The view ends above the navigation bar (126 px), the keyboard is taller.
    assertEquals(
      774,
      ImeInsets.overlap(viewTop = 0, viewBottom = screen - 126, windowBottom = screen, imeBottom = 900),
    )
  }

  @Test
  fun splitScreenWindowInTheBottomHalf() {
    // The app's window starts at row 1220 of the screen and ends at its bottom.
    assertEquals(900, ImeInsets.overlap(viewTop = 1220, viewBottom = screen, windowBottom = screen, imeBottom = 900))
    // A keyboard taller than the window covers all of it, no more.
    assertEquals(
      1180,
      ImeInsets.overlap(viewTop = 1220, viewBottom = screen, windowBottom = screen, imeBottom = 2000),
    )
  }

  @Test
  fun slidingKeyboardIsFollowed() {
    // Frames of the slide: the overlap grows with the keyboard's inset.
    val frames = listOf(0, 120, 480, 820, 900)
    assertEquals(
      frames,
      frames.map { ImeInsets.overlap(viewTop = 0, viewBottom = screen, windowBottom = screen, imeBottom = it) },
    )
  }

  @Test
  fun emptyViewIsNeverCovered() {
    assertEquals(0, ImeInsets.overlap(viewTop = 500, viewBottom = 500, windowBottom = screen, imeBottom = 900))
    assertEquals(0, ImeInsets.overlap(viewTop = 600, viewBottom = 500, windowBottom = screen, imeBottom = 900))
  }
}
