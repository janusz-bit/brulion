// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.media.AudioAttributes
import android.media.Ringtone
import android.media.RingtoneManager
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.util.Log
import io.github.januszbit.brulion.R
import org.json.JSONException
import org.json.JSONObject

/**
 * Which sound setting: plain reminders or alarms. [code] is the `kind` of the JNI calls
 * (`AndroidBridge::pickSound` …), [key] the one of the JSON, [baseChannel] the notification
 * channel id of version 0 (see [SoundChannel]).
 */
internal enum class SoundKind(val code: Int, val key: String, val baseChannel: String) {
  REMINDER(0, "reminder", ReminderNotifier.CHANNEL_REMINDERS),
  ALARM(1, "alarm", ReminderNotifier.CHANNEL_ALARMS);

  companion object {
    fun fromCode(code: Int): SoundKind? = values().firstOrNull { it.code == code }

    fun fromKey(key: String?): SoundKind? = values().firstOrNull { it.key == key }
  }
}

/**
 * A sound chosen in Brulion's settings (C++ `notify::SoundChoice`, sent by
 * `AndroidBridge::setReminderSounds` as `{mode, uri, title}`): [mode] is [DEFAULT] (the system's
 * notification / alarm sound), [CUSTOM] (the ringtone [uri] from the system picker, shown as
 * [title]), [VIBRATE] (vibration only) or [SILENT] (neither sound nor vibration).
 */
internal data class SoundChoice(val mode: String = DEFAULT, val uri: String = "", val title: String = "") {
  /** What the channel's sound depends on: the mode and the ringtone, not its shown title. */
  val channelKey: String
    get() = if (mode == CUSTOM) "$CUSTOM:$uri" else mode

  /** A sound plays (the system's or a ringtone). */
  val hasSound: Boolean
    get() = mode == DEFAULT || mode == CUSTOM

  /** The phone vibrates. */
  val vibrates: Boolean
    get() = mode != SILENT

  fun toJson(): JSONObject = JSONObject().put("mode", mode).put("uri", uri).put("title", title)

  companion object {
    const val DEFAULT = "default"
    const val CUSTOM = "custom"
    const val VIBRATE = "vibrate"
    const val SILENT = "silent"

    /** Parses a choice; an unknown mode (or a custom sound without a URI) is the default. */
    fun fromJson(json: JSONObject?): SoundChoice {
      if (json == null) return SoundChoice()
      val uri = json.optString("uri", "").trim()
      return when (val mode = json.optString("mode", DEFAULT)) {
        CUSTOM -> if (uri.isEmpty()) SoundChoice() else SoundChoice(CUSTOM, uri, json.optString("title", "").trim())
        VIBRATE, SILENT -> SoundChoice(mode)
        else -> SoundChoice()
      }
    }

    /** Parses the stored / sent JSON text; anything broken is the default. */
    fun parse(raw: String?): SoundChoice {
      if (raw.isNullOrEmpty()) return SoundChoice()
      return try {
        fromJson(JSONObject(raw))
      } catch (e: JSONException) {
        SoundChoice()
      }
    }

    /**
     * The answer of the system ringtone picker: [picked] null is its "Silent" entry, [defaultUri]
     * (or nothing) its "Default" entry, anything else a ringtone (shown as [title]).
     */
    fun fromPicker(picked: String?, defaultUri: String, title: String): SoundChoice =
      when {
        picked == null -> SoundChoice(SILENT)
        picked.isEmpty() || picked == defaultUri -> SoundChoice()
        else -> SoundChoice(CUSTOM, picked, title.trim())
      }
  }
}

/**
 * The notification channel of one [kind]: its [choice] and the [version] in its id.
 *
 * A channel's sound cannot change once it exists (Android keeps the user's channel settings, also
 * of a deleted channel recreated with the same id), so another sound means a new channel:
 * version 0 is the channel Brulion always had ("reminders", "alarms" — kept while the sound is
 * the default, with whatever the user set for it in Android), later ones are "reminders-v3".
 */
internal data class SoundChannel(val kind: SoundKind, val choice: SoundChoice = SoundChoice(), val version: Int = 0) {
  val id: String
    get() = channelId(kind.baseChannel, version)

  /** The channel after [next] was chosen: the same one when it sounds the same, else the next version. */
  fun withChoice(next: SoundChoice): SoundChannel =
    if (next.channelKey == choice.channelKey) copy(choice = next) else SoundChannel(kind, next, version + 1)

  companion object {
    fun channelId(base: String, version: Int): String = if (version <= 0) base else "$base-v$version"

    private val VERSIONED = Regex("""^(reminders|alarms)(-v\d+)?$""")

    /** Earlier versions among [existing] channel ids: everything of Brulion's sound channels but [current]. */
    fun stale(existing: Collection<String>, current: Collection<String>): List<String> =
      existing.filter { VERSIONED.matches(it) && it !in current }
  }
}

/**
 * The sound choices and channel versions, in SharedPreferences: alarms ring while the app is
 * closed (receivers, boot, snooze), so every path reads the current channel ids here.
 */
internal class SoundStore(context: Context) {
  private val prefs: SharedPreferences =
    context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

  fun channel(kind: SoundKind): SoundChannel =
    SoundChannel(kind, SoundChoice.parse(prefs.getString(kind.key, null)), prefs.getInt(kind.key + VERSION, 0))

  /** Written synchronously: the old channel is deleted right after. */
  @SuppressLint("ApplySharedPref")
  fun save(channel: SoundChannel) {
    prefs
      .edit()
      .putString(channel.kind.key, channel.choice.toJson().toString())
      .putInt(channel.kind.key + VERSION, channel.version)
      .commit()
  }

  private companion object {
    const val PREFS_NAME = "io.github.januszbit.brulion.android.sounds"
    const val VERSION = "Version"
  }
}

/** Applying, previewing and picking the sounds of reminders and alarms. */
internal object ReminderSounds {
  private const val TAG = "BrulionAndroid"
  private const val PREVIEW_MS = 3_000L

  /**
   * Guards the read-modify-write of [SoundStore] and the channels made from it (Qt's thread and
   * the main thread): [apply], [ReminderNotifier.ensureChannels] and posting a reminder, so no
   * notification goes to a channel version another thread has just deleted (Android drops it).
   */
  val lock = Any()

  private val handler = Handler(Looper.getMainLooper())

  /** The ringtone of a preview playing now (main thread only). */
  private var playing: Ringtone? = null

  private val stopPreview = Runnable { stop() }

  /** The current channel id of [kind]: every notification of that kind is posted there. */
  fun channelId(context: Context, kind: SoundKind): String = SoundStore(context).channel(kind).id

  /**
   * Applies the choices sent by C++: a kind whose sound changed gets the next channel version
   * (created by [ReminderNotifier.ensureChannels], which also deletes the earlier one). Returns
   * the current channel ids.
   */
  fun apply(context: Context, reminder: SoundChoice, alarm: SoundChoice): Map<SoundKind, String> =
    synchronized(lock) {
      val store = SoundStore(context)
      for ((kind, choice) in listOf(SoundKind.REMINDER to reminder, SoundKind.ALARM to alarm)) {
        val current = store.channel(kind)
        val next = current.withChoice(choice)
        if (next != current) store.save(next)
      }
      ReminderNotifier.ensureChannels(context)
      SoundKind.values().associateWith { store.channel(it).id }
    }

  /** Audio attributes of a kind's sound: alarms on the alarm stream. */
  fun audioAttributes(kind: SoundKind): AudioAttributes =
    AudioAttributes.Builder()
      .setUsage(if (kind == SoundKind.ALARM) AudioAttributes.USAGE_ALARM else AudioAttributes.USAGE_NOTIFICATION_EVENT)
      .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
      .build()

  /** The system's default sound of [kind]. */
  fun defaultUri(kind: SoundKind): Uri =
    if (kind == SoundKind.ALARM) {
      RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM)
        ?: RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE)
    } else {
      RingtoneManager.getDefaultUri(RingtoneManager.TYPE_NOTIFICATION)
    }

  /** What a channel of [kind] with [choice] plays (null: no sound). */
  fun soundUri(kind: SoundKind, choice: SoundChoice): Uri? =
    when (choice.mode) {
      SoundChoice.CUSTOM -> Uri.parse(choice.uri)
      SoundChoice.DEFAULT -> defaultUri(kind)
      else -> null
    }

  /**
   * Plays [choice] as [kind] would sound for about three seconds (a vibration for "vibrate").
   * False for "silent". Any thread.
   */
  fun preview(context: Context, kind: SoundKind, choice: SoundChoice): Boolean {
    if (choice.mode == SoundChoice.SILENT) return false
    val app = context.applicationContext
    handler.post {
      stop()
      if (choice.mode == SoundChoice.VIBRATE) {
        val pattern =
          if (kind == SoundKind.ALARM) ReminderNotifier.ALARM_VIBRATION else ReminderNotifier.REMINDER_VIBRATION
        vibrate(app, pattern)
        return@post
      }
      try {
        val ringtone = RingtoneManager.getRingtone(app, soundUri(kind, choice)) ?: return@post
        ringtone.audioAttributes = audioAttributes(kind)
        ringtone.play()
        playing = ringtone
        handler.postDelayed(stopPreview, PREVIEW_MS)
      } catch (e: RuntimeException) {
        Log.w(TAG, "cannot play the sound ${choice.uri}", e)
      }
    }
    return true
  }

  private fun stop() {
    handler.removeCallbacks(stopPreview)
    playing?.stop()
    playing = null
  }

  private fun vibrate(context: Context, pattern: LongArray) {
    val vibrator: Vibrator? =
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        (context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as? VibratorManager)?.defaultVibrator
      } else {
        legacyVibrator(context)
      }
    try {
      vibrator?.vibrate(VibrationEffect.createWaveform(pattern, -1))
    } catch (e: RuntimeException) {
      Log.w(TAG, "cannot vibrate", e)
    }
  }

  @Suppress("DEPRECATION")
  private fun legacyVibrator(context: Context): Vibrator? =
    context.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator

  /** The system's ringtone picker for [kind], showing [current]. */
  fun pickerIntent(context: Context, kind: SoundKind, current: SoundChoice): Intent {
    val type = if (kind == SoundKind.ALARM) RingtoneManager.TYPE_ALARM else RingtoneManager.TYPE_NOTIFICATION
    val title =
      context.getString(
        if (kind == SoundKind.ALARM) R.string.brulion_pick_alarm_sound else R.string.brulion_pick_reminder_sound
      )
    // Brulion has its own "Silent": the picker offers the default and the ringtones.
    return Intent(RingtoneManager.ACTION_RINGTONE_PICKER)
      .putExtra(RingtoneManager.EXTRA_RINGTONE_TYPE, type)
      .putExtra(RingtoneManager.EXTRA_RINGTONE_TITLE, title)
      .putExtra(RingtoneManager.EXTRA_RINGTONE_SHOW_DEFAULT, true)
      .putExtra(RingtoneManager.EXTRA_RINGTONE_SHOW_SILENT, false)
      .putExtra(RingtoneManager.EXTRA_RINGTONE_DEFAULT_URI, defaultUri(kind))
      .putExtra(RingtoneManager.EXTRA_RINGTONE_EXISTING_URI, soundUri(kind, current))
  }

  /** The choice the picker answered with ([data] of a RESULT_OK). */
  fun picked(context: Context, kind: SoundKind, data: Intent?): SoundChoice {
    val uri: Uri? =
      when {
        data == null -> null
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE ->
          data.getParcelableExtra(RingtoneManager.EXTRA_RINGTONE_PICKED_URI, Uri::class.java)
        else -> legacyPickedUri(data)
      }
    val title = uri?.let { ringtoneTitle(context, it) } ?: ""
    return SoundChoice.fromPicker(uri?.toString(), defaultUri(kind).toString(), title)
  }

  // Android 13's typed getParcelableExtra may fail (fixed in 14): the old call before that.
  @Suppress("DEPRECATION")
  private fun legacyPickedUri(data: Intent): Uri? = data.getParcelableExtra(RingtoneManager.EXTRA_RINGTONE_PICKED_URI)

  private fun ringtoneTitle(context: Context, uri: Uri): String =
    try {
      RingtoneManager.getRingtone(context, uri)?.getTitle(context) ?: ""
    } catch (e: RuntimeException) {
      ""
    }
}
