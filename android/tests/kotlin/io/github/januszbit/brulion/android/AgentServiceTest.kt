// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.content.pm.ServiceInfo
import io.github.januszbit.brulion.android.AgentService.Companion.next
import io.github.januszbit.brulion.android.AgentService.Companion.typeFor
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
  fun theModelOnTheDeviceComputesAsSpecialUse() {
    // Android 14+: its own type while the model answers; a download and an agent's turn fetch data.
    assertEquals(ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE, typeFor("model", 34))
    assertEquals(ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE, typeFor("model", 36))
    assertEquals(ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC, typeFor("data", 36))
    assertEquals(ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC, typeFor("agent", 36))
    // Before Android 14 there is no specialUse: dataSync for all of it.
    assertEquals(ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC, typeFor("model", 33))
  }

  @Test
  fun notAgainForTheSameTurnsAfterItsTimeWasUp() {
    // Android 15+ ended it (dataSync's 6 hours): the same turns go on without it; the next ones start it.
    assertEquals(AgentService.Step.NOTHING, next(working = true, wasWorking = true, running = false, starting = false, timedOut = true))
    assertEquals(AgentService.Step.START, next(working = true, wasWorking = false, running = false, starting = false, timedOut = true))
  }
}
