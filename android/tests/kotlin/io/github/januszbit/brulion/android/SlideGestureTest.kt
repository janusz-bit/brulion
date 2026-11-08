// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import io.github.januszbit.brulion.android.SlideGesture.Choice
import org.junit.Assert.assertEquals
import org.junit.Test

/** The alarm slider's arithmetic (px; plain JVM: no Android API is called). */
class SlideGestureTest {
  // A 1080-px-wide track less the knob: the knob goes 400 px to either side.
  private val reach = 400f

  @Test
  fun aTapOrAShortDragAnswersNothing() {
    assertEquals(Choice.NONE, SlideGesture.choice(0f, reach))
    assertEquals(Choice.NONE, SlideGesture.choice(-120f, reach))
    assertEquals(Choice.NONE, SlideGesture.choice(279f, reach))
    assertEquals(Choice.NONE, SlideGesture.choice(-279f, reach))
  }

  @Test
  fun leftIsSnoozeRightIsOk() {
    assertEquals(Choice.SNOOZE, SlideGesture.choice(-280f, reach)) // exactly the threshold (0.7)
    assertEquals(Choice.SNOOZE, SlideGesture.choice(-400f, reach))
    assertEquals(Choice.OK, SlideGesture.choice(280f, reach))
    assertEquals(Choice.OK, SlideGesture.choice(400f, reach))
  }

  @Test
  fun aFingerPastTheEndStillCounts() {
    assertEquals(Choice.SNOOZE, SlideGesture.choice(-5000f, reach))
    assertEquals(Choice.OK, SlideGesture.choice(5000f, reach))
    assertEquals(-400f, SlideGesture.clamp(-5000f, reach), 0f)
    assertEquals(400f, SlideGesture.clamp(5000f, reach), 0f)
    assertEquals(1f, SlideGesture.progress(-5000f, reach), 0f)
  }

  @Test
  fun progressFromTheMiddle() {
    assertEquals(0f, SlideGesture.progress(0f, reach), 0f)
    assertEquals(0.5f, SlideGesture.progress(-200f, reach), 1e-6f)
    assertEquals(0.5f, SlideGesture.progress(200f, reach), 1e-6f)
  }

  @Test
  fun noRoomToMoveAnswersNothing() {
    // Before layout, or a track no wider than the knob.
    for (r in floatArrayOf(0f, -10f)) {
      assertEquals(Choice.NONE, SlideGesture.choice(300f, r))
      assertEquals(Choice.NONE, SlideGesture.choice(-300f, r))
      assertEquals(0f, SlideGesture.clamp(300f, r), 0f)
      assertEquals(0f, SlideGesture.progress(300f, r), 0f)
    }
  }
}
