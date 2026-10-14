// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.junit.Assert.assertEquals
import org.junit.Test

/** Ring-time arithmetic of alarms (plain JVM: no Android API is called). */
class RingTimeoutTest {
  @Test
  fun absentRingTimeIsTheDefault() {
    assertEquals(10, RingTimeout.DEFAULT_MINUTES)
    assertEquals(RingTimeout.DEFAULT_MINUTES, RingTimeout.minutes(null))
  }

  @Test
  fun ringTimeIsClampedToOneHour() {
    assertEquals(1, RingTimeout.minutes(1))
    assertEquals(25, RingTimeout.minutes(25))
    assertEquals(60, RingTimeout.minutes(60))
    assertEquals(1, RingTimeout.minutes(0))
    assertEquals(1, RingTimeout.minutes(-3))
    assertEquals(60, RingTimeout.minutes(61))
    // A Long that would wrap around as an Int still clamps to the maximum.
    assertEquals(60, RingTimeout.minutes(1_000_000_000_000L))
    assertEquals(1, RingTimeout.minutes(Long.MIN_VALUE))
  }

  @Test
  fun deadlineCountsFromWhenTheAlarmStartsRinging() {
    assertEquals(600_000L, RingTimeout.durationMs(10))
    assertEquals(5_000L + 600_000L, RingTimeout.deadline(5_000L, 10))
    assertEquals(5_000L + 60_000L, RingTimeout.deadline(5_000L, 1))
    // Out-of-range values stored by hand are clamped here too.
    assertEquals(3_600_000L, RingTimeout.durationMs(500))
    assertEquals(60_000L, RingTimeout.durationMs(0))
  }
}
