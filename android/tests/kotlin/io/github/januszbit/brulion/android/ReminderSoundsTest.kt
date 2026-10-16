// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Sounds of reminders and alarms (plain JVM: no Android API is called): the choice JSON from C++
 * (`notify::androidSoundSettings`), the ringtone picker's answer and the versioned channel ids.
 */
class ReminderSoundsTest {
  private val ringtone = SoundChoice(SoundChoice.CUSTOM, "content://media/internal/audio/media/27", "Żółta łódź")

  @Test
  fun choicesFromCppAreParsed() {
    val sent =
      JSONObject(
        """{"reminder":{"mode":"custom","uri":" content://media/internal/audio/media/27 ","title":"Żółta łódź "},
           "alarm":{"mode":"vibrate","uri":"content://zapamiętany","title":"Stary"}}"""
      )
    assertEquals(ringtone, SoundChoice.fromJson(sent.optJSONObject("reminder")))
    // Another mode: the remembered ringtone does not matter here.
    assertEquals(SoundChoice(SoundChoice.VIBRATE), SoundChoice.fromJson(sent.optJSONObject("alarm")))
    assertEquals(SoundChoice(SoundChoice.SILENT), SoundChoice.parse("""{"mode":"silent"}"""))
  }

  @Test
  fun brokenChoicesAreTheDefault() {
    assertEquals(SoundChoice(), SoundChoice.fromJson(null))
    assertEquals(SoundChoice(), SoundChoice.parse(null))
    assertEquals(SoundChoice(), SoundChoice.parse(""))
    assertEquals(SoundChoice(), SoundChoice.parse("nie json"))
    assertEquals(SoundChoice(), SoundChoice.parse("""{"mode":"głośno"}"""))
    // A custom sound without a ringtone has nothing to play.
    assertEquals(SoundChoice(), SoundChoice.parse("""{"mode":"custom","uri":"  "}"""))
  }

  @Test
  fun roundTripThroughTheStore() {
    for (choice in listOf(SoundChoice(), ringtone, SoundChoice(SoundChoice.VIBRATE), SoundChoice(SoundChoice.SILENT))) {
      assertEquals(choice, SoundChoice.parse(choice.toJson().toString()))
    }
  }

  @Test
  fun whatAChoicePlays() {
    assertTrue(SoundChoice().hasSound)
    assertTrue(ringtone.hasSound)
    assertFalse(SoundChoice(SoundChoice.VIBRATE).hasSound)
    assertTrue(SoundChoice(SoundChoice.VIBRATE).vibrates)
    assertFalse(SoundChoice(SoundChoice.SILENT).hasSound)
    assertFalse(SoundChoice(SoundChoice.SILENT).vibrates)
  }

  @Test
  fun pickerAnswers() {
    val defaultUri = "content://settings/system/notification_sound"
    assertEquals(SoundChoice(), SoundChoice.fromPicker(defaultUri, defaultUri, "Domyślny"))
    assertEquals(SoundChoice(), SoundChoice.fromPicker("", defaultUri, ""))
    assertEquals(SoundChoice(SoundChoice.SILENT), SoundChoice.fromPicker(null, defaultUri, ""))
    assertEquals(ringtone, SoundChoice.fromPicker(ringtone.uri, defaultUri, " Żółta łódź"))
  }

  @Test
  fun kindsOfTheJniCalls() {
    assertEquals(SoundKind.REMINDER, SoundKind.fromCode(0))
    assertEquals(SoundKind.ALARM, SoundKind.fromCode(1))
    assertNull(SoundKind.fromCode(2))
    assertEquals(SoundKind.ALARM, SoundKind.fromKey("alarm"))
    assertNull(SoundKind.fromKey("budzik"))
    assertNull(SoundKind.fromKey(null))
  }

  @Test
  fun versionZeroKeepsTheOriginalChannels() {
    // Brulion's channels before sounds could be chosen (and the emulator test) keep their ids.
    assertEquals("reminders", SoundChannel(SoundKind.REMINDER).id)
    assertEquals("alarms", SoundChannel(SoundKind.ALARM).id)
    assertEquals("alarms-v3", SoundChannel(SoundKind.ALARM, version = 3).id)
    assertEquals("reminders", SoundChannel.channelId("reminders", -1))
  }

  @Test
  fun anotherSoundIsTheNextChannelVersion() {
    val start = SoundChannel(SoundKind.REMINDER)
    // The same sound (an older install sending the default): the same channel.
    assertEquals(start, start.withChoice(SoundChoice()))

    val custom = start.withChoice(ringtone)
    assertEquals(1, custom.version)
    assertEquals("reminders-v1", custom.id)
    assertEquals(ringtone, custom.choice)

    // Only the shown title changed: the channel sounds the same.
    val renamed = custom.withChoice(ringtone.copy(title = "Inna nazwa"))
    assertEquals(1, renamed.version)
    assertEquals("Inna nazwa", renamed.choice.title)

    // Another ringtone, then silent, then the default again: a new channel each time (Android
    // would give a deleted channel its old sound back).
    val other = renamed.withChoice(ringtone.copy(uri = "content://media/external/audio/media/5"))
    assertEquals(2, other.version)
    val silent = other.withChoice(SoundChoice(SoundChoice.SILENT))
    assertEquals("reminders-v3", silent.id)
    val back = silent.withChoice(SoundChoice())
    assertEquals("reminders-v4", back.id)
  }

  @Test
  fun earlierVersionsAreStale() {
    val existing =
      listOf("reminders", "reminders-v2", "alarms", "alarms-v1", "missed-alarms", "miscellaneous", "remindersX")
    assertEquals(
      listOf("reminders", "alarms-v1"),
      SoundChannel.stale(existing, listOf("reminders-v2", "alarms"))
    )
    assertTrue(SoundChannel.stale(listOf("missed-alarms"), listOf("reminders", "alarms")).isEmpty())
  }
}
