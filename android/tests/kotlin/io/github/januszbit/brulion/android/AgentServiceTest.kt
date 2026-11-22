// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import io.github.januszbit.brulion.android.AgentService.Companion.next
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * What the foreground service of an AI agent on another computer does when C++ says what runs
 * (AgentService.update, docs/ANDROID.md "An AI agent on another computer").
 */
class AgentServiceTest {
  @Test
  fun startsForTheFirstTurn() {
    assertEquals(AgentService.Step.START, next(working = true, wasWorking = false, running = false, starting = false, timedOut = false))
  }

  @Test
  fun theRunningServiceShowsWhatChanged() {
    // (Another turn, a question waiting: the notification says so.)
    assertEquals(AgentService.Step.APPLY, next(working = true, wasWorking = true, running = true, starting = false, timedOut = false))
    assertEquals(AgentService.Step.APPLY, next(working = true, wasWorking = false, running = true, starting = false, timedOut = false))
  }

  @Test
  fun aServiceOnItsWayIsNotStartedTwice() {
    assertEquals(AgentService.Step.NOTHING, next(working = true, wasWorking = true, running = false, starting = true, timedOut = false))
  }

  @Test
  fun stopsALittleLater() {
    // (The turn that waited for this one starts right after it: the service stays.)
    assertEquals(AgentService.Step.STOP_LATER, next(working = false, wasWorking = true, running = true, starting = false, timedOut = false))
    assertEquals(AgentService.Step.STOP_LATER, next(working = false, wasWorking = true, running = false, starting = true, timedOut = false))
  }

  @Test
  fun notAgainForTheSameTurnsAfterItsTimeWasUp() {
    // Android 15+ ended it (dataSync's 6 hours): the same turns go on without it; the next ones start it.
    assertEquals(AgentService.Step.NOTHING, next(working = true, wasWorking = true, running = false, starting = false, timedOut = true))
    assertEquals(AgentService.Step.START, next(working = true, wasWorking = false, running = false, starting = false, timedOut = true))
  }
}
