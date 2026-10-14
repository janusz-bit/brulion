// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import java.util.TimeZone
import org.junit.Assert.assertEquals
import org.junit.Test

/** Plain JVM test (java.util only): scripts/android-build.sh with BRULION_ANDROID_UNIT_TESTS=1. */
class WallClockTest {
  private val warsaw = TimeZone.getTimeZone("Europe/Warsaw")
  private val london = TimeZone.getTimeZone("Europe/London")
  private val newYork = TimeZone.getTimeZone("America/New_York")

  private fun utc(iso: String): Long = java.time.Instant.parse(iso).toEpochMilli()

  @Test
  fun sameZoneKeepsTheInstant() {
    val at = utc("2026-10-14T07:00:00Z")
    assertEquals(at, WallClock.shift(at, warsaw, warsaw))
  }

  @Test
  fun keepsTheLocalTimeInTheNewZone() {
    // 09:00 in Warsaw (CEST, UTC+2) -> 09:00 in London (BST, UTC+1).
    assertEquals(
      utc("2026-07-01T08:00:00Z"),
      WallClock.shift(utc("2026-07-01T07:00:00Z"), warsaw, london)
    )
    // 09:00 in Warsaw -> 09:00 in New York (EDT, UTC-4).
    assertEquals(
      utc("2026-07-01T13:00:00Z"),
      WallClock.shift(utc("2026-07-01T07:00:00Z"), warsaw, newYork)
    )
    // And back.
    assertEquals(
      utc("2026-07-01T07:00:00Z"),
      WallClock.shift(utc("2026-07-01T13:00:00Z"), newYork, warsaw)
    )
  }

  @Test
  fun zonesWithDifferentDaylightSavingDates() {
    // 20 March: New York already on EDT (UTC-4), Warsaw still on CET (UTC+1).
    assertEquals(
      utc("2026-03-20T13:00:00Z"),
      WallClock.shift(utc("2026-03-20T08:00:00Z"), warsaw, newYork)
    )
  }

  @Test
  fun winterTime() {
    // 07:30 in Warsaw (CET, UTC+1) -> 07:30 in London (GMT).
    assertEquals(
      utc("2026-12-24T07:30:00Z"),
      WallClock.shift(utc("2026-12-24T06:30:00Z"), warsaw, london)
    )
  }
}
