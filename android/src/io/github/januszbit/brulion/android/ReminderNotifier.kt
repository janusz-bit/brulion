// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.Manifest
import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.text.format.DateFormat
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import io.github.januszbit.brulion.R
import java.util.Date

/**
 * Builds and posts reminder notifications.
 *
 * The sound of reminders and alarms is chosen in Brulion's settings ([ReminderSounds]); a channel's
 * sound is fixed once it exists, so each sound has its own channel version ([SoundChannel]) and
 * every notification goes to the current one ([SoundStore]).
 */
internal object ReminderNotifier {
  /** Regular reminders: heads-up, the chosen sound (default: the notification sound) — version 0's id. */
  const val CHANNEL_REMINDERS = "reminders"

  /**
   * Alarms: the chosen sound (default: the alarm sound) on the alarm stream, rings until a reaction
   * or the ring time, full screen — version 0's id.
   */
  const val CHANNEL_ALARMS = "alarms"

  /** Alarms that rang out with no reaction ("Przegapiony alarm"): silent. */
  const val CHANNEL_MISSED_ALARMS = "missed-alarms"

  /**
   * Last resort: the system itself removes a ringing alarm this long after its ring time,
   * should the [RingTimeout] alarm never come (an inexact alarm deferred by the system).
   */
  private const val RING_CAP_SLACK_MS = 15L * 60 * 1000

  /** Brulion's amber (reminders and alarms, docs/UI.md). */
  private const val ACCENT = 0xFFF2B33D.toInt()
  val ALARM_VIBRATION = longArrayOf(0, 800, 400, 800, 400, 800)
  val REMINDER_VIBRATION = longArrayOf(0, 250, 150, 250)

  /**
   * Creates the channels (updates names and descriptions when they exist): the current versions
   * of the reminder and alarm channels with their sounds, and the missed-alarm one; deletes
   * earlier versions. The channels decide sound, vibration and importance (Android 8+; Brulion
   * needs Android 9). Returns the ids of the reminder and alarm channels (empty without a
   * notification manager). Under [ReminderSounds.lock], like changing a sound.
   */
  fun ensureChannels(context: Context): Map<SoundKind, String> =
    synchronized(ReminderSounds.lock) { createChannels(context) }

  private fun createChannels(context: Context): Map<SoundKind, String> {
    val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as? NotificationManager ?: return emptyMap()
    val sounds = SoundStore(context)

    val reminders = soundChannel(context, sounds.channel(SoundKind.REMINDER))
    reminders.description = context.getString(R.string.brulion_channel_reminders_description)

    // IMPORTANCE_MAX is documented as unused for channels; HIGH is the loudest level, the
    // alarm behaviour comes from the sound, CATEGORY_ALARM and FLAG_INSISTENT.
    val alarms = soundChannel(context, sounds.channel(SoundKind.ALARM))
    alarms.description = context.getString(R.string.brulion_channel_alarms_description)
    alarms.lockscreenVisibility = Notification.VISIBILITY_PUBLIC

    // Default importance keeps it among the alerting notifications, without sound or vibration.
    val missedAlarms =
      NotificationChannel(
        CHANNEL_MISSED_ALARMS,
        context.getString(R.string.brulion_channel_missed_alarms),
        NotificationManager.IMPORTANCE_DEFAULT
      )
    missedAlarms.description = context.getString(R.string.brulion_channel_missed_alarms_description)
    missedAlarms.setSound(null, null)
    missedAlarms.enableVibration(false)

    manager.createNotificationChannels(listOf(reminders, alarms, missedAlarms))

    // Another sound was chosen: the channels of the earlier ones go (Android lists them as
    // deleted categories; their settings would come back with the same id).
    try {
      for (id in SoundChannel.stale(manager.notificationChannels.map { it.id }, listOf(reminders.id, alarms.id))) {
        manager.deleteNotificationChannel(id)
      }
    } catch (e: RuntimeException) {
      Log.w(TAG, "cannot delete earlier notification channels", e)
    }
    return mapOf(SoundKind.REMINDER to reminders.id, SoundKind.ALARM to alarms.id)
  }

  /**
   * The channel of [channel]'s kind with its sound: the system's default (alarms: the alarm sound
   * on the alarm stream), a ringtone, vibration only or nothing at all.
   */
  private fun soundChannel(context: Context, channel: SoundChannel): NotificationChannel {
    val alarm = channel.kind == SoundKind.ALARM
    val result =
      NotificationChannel(
        channel.id,
        context.getString(if (alarm) R.string.brulion_channel_alarms else R.string.brulion_channel_reminders),
        NotificationManager.IMPORTANCE_HIGH
      )
    val choice = channel.choice
    when {
      // Reminders with the default sound: the channel's default (the notification sound).
      choice.mode == SoundChoice.DEFAULT && !alarm -> Unit
      choice.hasSound ->
        result.setSound(ReminderSounds.soundUri(channel.kind, choice), ReminderSounds.audioAttributes(channel.kind))
      else -> result.setSound(null, null)
    }
    result.enableVibration(choice.vibrates)
    if (choice.vibrates) result.vibrationPattern = if (alarm) ALARM_VIBRATION else REMINDER_VIBRATION
    return result
  }

  /** The channel notifications of [kind] go to now. */
  fun channelId(context: Context, kind: SoundKind): String = ReminderSounds.channelId(context, kind)

  /**
   * Can alarms open the full-screen alarm screen? Android 14+ lets the user revoke it ("Full
   * screen notifications"); the alarm then shows as a heads-up notification (still rings).
   */
  fun canUseFullScreenIntent(context: Context): Boolean {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.UPSIDE_DOWN_CAKE) return true
    val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as? NotificationManager ?: return false
    return manager.canUseFullScreenIntent()
  }

  /** May the app post notifications at all? */
  fun canPost(context: Context): Boolean {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
      ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) !=
      PackageManager.PERMISSION_GRANTED
    ) {
      return false
    }
    return NotificationManagerCompat.from(context).areNotificationsEnabled()
  }

  /**
   * Shows [reminder]. Alarms ring (insistent, alarm stream, full-screen alarm screen) for
   * [Reminder.ringMinutes] at most (see [RingTimeout]) unless [missed] — a reminder that came
   * due while the phone was off is shown without ringing.
   */
  @SuppressLint("MissingPermission")
  fun show(context: Context, reminder: Reminder, missed: Boolean = false) {
    if (!canPost(context)) return

    val ringing = reminder.alarm && !missed
    val kind = if (ringing) SoundKind.ALARM else SoundKind.REMINDER
    val time = DateFormat.getTimeFormat(context).format(Date(reminder.at))
    val title = reminder.title.ifBlank { context.getString(R.string.brulion_reminder_default_title) }
    val details = reminder.body.ifBlank { time }
    val text =
      if (missed) {
        context.getString(R.string.brulion_missed, time) +
          (if (reminder.body.isBlank()) "" else " · " + reminder.body)
      } else {
        details
      }

    val builder =
      // The channel is set when posting (below).
      NotificationCompat.Builder(context, kind.baseChannel)
        .setSmallIcon(R.drawable.ic_stat_brulion)
        .setColor(ACCENT)
        .setContentTitle(title)
        .setContentText(text)
        .setStyle(NotificationCompat.BigTextStyle().bigText(text))
        .setWhen(reminder.at)
        .setShowWhen(true)
        .setCategory(if (ringing) NotificationCompat.CATEGORY_ALARM else NotificationCompat.CATEGORY_REMINDER)
        .setPriority(if (ringing) NotificationCompat.PRIORITY_MAX else NotificationCompat.PRIORITY_HIGH)
        .setAutoCancel(true)

    contentIntent(context, reminder)?.let { builder.setContentIntent(it) }
    actionIntent(context, reminder, Actions.SNOOZE)?.let {
      builder.addAction(0, context.getString(R.string.brulion_action_snooze), it)
    }
    // A task's reminder: "Zrobione" marks the task done without opening the note (3 buttons, the
    // most a notification shows).
    if (reminder.canComplete) {
      actionIntent(context, reminder, Actions.DONE)?.let {
        builder.addAction(0, context.getString(R.string.brulion_action_done), it)
      }
    }
    val dismiss = actionIntent(context, reminder, Actions.DISMISS)
    dismiss?.let { builder.addAction(0, context.getString(R.string.brulion_action_ok), it) }

    if (ringing) {
      val stopAt = RingTimeout.arm(context, reminder)
      builder
        .setOngoing(true)
        .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
        .setTimeoutAfter(RingTimeout.durationMs(reminder.ringMinutes) + RING_CAP_SLACK_MS)
      // Swiped away (allowed on Android 14+ while unlocked): like "OK", drops the ring timeout.
      dismiss?.let { builder.setDeleteIntent(it) }
      fullScreenIntent(context, reminder, stopAt)?.let { builder.setFullScreenIntent(it, true) }
    } else {
      builder.setVisibility(NotificationCompat.VISIBILITY_PRIVATE)
    }

    // The current channel version, created and posted to under the sounds' lock: a sound changed
    // meanwhile on Qt's thread (ReminderSounds.apply) deletes the earlier version, and Android
    // drops a notification posted to a deleted channel.
    synchronized(ReminderSounds.lock) {
      builder.setChannelId(ensureChannels(context)[kind] ?: channelId(context, kind))
      val notification = builder.build()
      if (ringing) {
        // Repeat the sound until the user reacts.
        notification.flags = notification.flags or Notification.FLAG_INSISTENT
      }
      NotificationManagerCompat.from(context).notify(reminder.id, notification)
    }
  }

  /**
   * Replaces the ringing alarm of [reminder] with a silent "Przegapiony alarm" notification
   * (its ring time is over and nobody reacted). Tapping it opens the note.
   */
  @SuppressLint("MissingPermission")
  fun showSilencedAlarm(context: Context, reminder: Reminder) {
    // Cancel first: an update would not stop the insistent sound reliably.
    cancel(context, reminder.id)
    if (!canPost(context)) return
    ensureChannels(context)

    val time = DateFormat.getTimeFormat(context).format(Date(reminder.at))
    val title = reminder.title.ifBlank { context.getString(R.string.brulion_reminder_default_title) }
    val text =
      context.getString(R.string.brulion_missed_alarm, time) +
        (if (reminder.body.isBlank()) "" else " · " + reminder.body)

    val builder =
      NotificationCompat.Builder(context, CHANNEL_MISSED_ALARMS)
        .setSmallIcon(R.drawable.ic_stat_brulion)
        .setColor(ACCENT)
        .setContentTitle(title)
        .setContentText(text)
        .setStyle(NotificationCompat.BigTextStyle().bigText(text))
        .setWhen(reminder.at)
        .setShowWhen(true)
        .setCategory(NotificationCompat.CATEGORY_REMINDER)
        .setPriority(NotificationCompat.PRIORITY_DEFAULT)
        .setVisibility(NotificationCompat.VISIBILITY_PRIVATE)
        .setOnlyAlertOnce(true)
        .setAutoCancel(true)
    contentIntent(context, reminder)?.let { builder.setContentIntent(it) }
    NotificationManagerCompat.from(context).notify(reminder.id, builder.build())
  }

  /** Does the alarm notification of [id] still ring? (Unknown counts as ringing.) */
  fun isRinging(context: Context, id: Int): Boolean {
    val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as? NotificationManager ?: return true
    return try {
      manager.activeNotifications.any {
        it.id == id && it.tag == null && (it.notification.flags and Notification.FLAG_INSISTENT) != 0
      }
    } catch (e: RuntimeException) {
      true
    }
  }

  /**
   * Removes the notification of reminder [id]: stops an alarm ringing, drops its pending
   * auto-stop and closes its alarm screen (e.g. "OK" pulled down from the notification shade
   * while the alarm screen is shown). Every reaction to a notification or the alarm screen ends
   * here.
   */
  fun cancel(context: Context, id: Int) {
    NotificationManagerCompat.from(context).cancel(id)
    RingTimeout.cancel(context, id)
    RingTimeout.stopped(id)
  }

  /**
   * Launch intent of the app carrying the note to open; read by [BrulionActivity] in `onCreate`
   * (cold start) or `onNewIntent` (app running) and handed to C++ by [BrulionAndroid].
   */
  fun launchIntent(context: Context, reminder: Reminder): Intent? {
    val intent = context.packageManager.getLaunchIntentForPackage(context.packageName) ?: return null
    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP)
    intent.putExtra(Extras.PATH, reminder.path)
    intent.putExtra(Extras.LINE, reminder.line)
    intent.putExtra(Extras.NOTIFICATION_ID, reminder.id)
    return intent
  }

  /** Opens the app's launch activity at the reminder's note. */
  fun contentIntent(context: Context, reminder: Reminder): PendingIntent? {
    val intent = launchIntent(context, reminder) ?: return null
    return PendingIntent.getActivity(
      context,
      reminder.id,
      intent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )
  }

  private fun actionIntent(context: Context, reminder: Reminder, action: String): PendingIntent? {
    val intent = reminder.putInto(Intent(context, ActionReceiver::class.java).setAction(action))
    return PendingIntent.getBroadcast(
      context,
      reminder.id,
      intent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )
  }

  private fun fullScreenIntent(context: Context, reminder: Reminder, stopAt: Long): PendingIntent? {
    val intent = reminder.putInto(Intent(context, AlarmActivity::class.java))
    intent.putExtra(Extras.STOP_AT, stopAt)
    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_NO_USER_ACTION)
    return PendingIntent.getActivity(
      context,
      reminder.id,
      intent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )
  }

  private const val TAG = "BrulionAndroid"
}
