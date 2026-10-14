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
import android.media.AudioAttributes
import android.media.AudioManager
import android.media.RingtoneManager
import android.net.Uri
import android.os.Build
import android.text.format.DateFormat
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import java.util.Date

/** Builds and posts reminder notifications. */
internal object ReminderNotifier {
  /** Regular reminders: heads-up, default sound. */
  const val CHANNEL_REMINDERS = "reminders"

  /** Alarms: alarm sound on the alarm stream, rings until dismissed, full screen. */
  const val CHANNEL_ALARMS = "alarms"

  private const val ACCENT = 0xFF5B8DEF.toInt()
  private val ALARM_VIBRATION = longArrayOf(0, 800, 400, 800, 400, 800)
  private val REMINDER_VIBRATION = longArrayOf(0, 250, 150, 250)

  /** Creates both channels (no-op before Android 8 and when they exist). */
  fun ensureChannels(context: Context) {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
    val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as? NotificationManager ?: return

    val reminders =
      NotificationChannel(
        CHANNEL_REMINDERS,
        context.getString(R.string.brulion_channel_reminders),
        NotificationManager.IMPORTANCE_HIGH
      )
    reminders.description = context.getString(R.string.brulion_channel_reminders_description)
    reminders.enableVibration(true)
    reminders.vibrationPattern = REMINDER_VIBRATION

    // IMPORTANCE_MAX is documented as unused for channels; HIGH is the loudest level, the
    // alarm behaviour comes from the sound, CATEGORY_ALARM and FLAG_INSISTENT.
    val alarms =
      NotificationChannel(
        CHANNEL_ALARMS,
        context.getString(R.string.brulion_channel_alarms),
        NotificationManager.IMPORTANCE_HIGH
      )
    alarms.description = context.getString(R.string.brulion_channel_alarms_description)
    alarms.setSound(
      alarmSound(),
      AudioAttributes.Builder()
        .setUsage(AudioAttributes.USAGE_ALARM)
        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
        .build()
    )
    alarms.enableVibration(true)
    alarms.vibrationPattern = ALARM_VIBRATION
    alarms.lockscreenVisibility = Notification.VISIBILITY_PUBLIC

    manager.createNotificationChannels(listOf(reminders, alarms))
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
   * Shows [reminder]. Alarms ring (insistent, alarm stream, full-screen alarm screen) unless
   * [missed] — a reminder that came due while the phone was off is shown quietly.
   */
  @SuppressLint("MissingPermission")
  fun show(context: Context, reminder: Reminder, missed: Boolean = false) {
    if (!canPost(context)) return
    ensureChannels(context)

    val ringing = reminder.alarm && !missed
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
      NotificationCompat.Builder(context, if (ringing) CHANNEL_ALARMS else CHANNEL_REMINDERS)
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
    actionIntent(context, reminder, Actions.DISMISS)?.let {
      builder.addAction(0, context.getString(R.string.brulion_action_ok), it)
    }

    if (ringing) {
      // Before Android 8 the sound and vibration come from the notification itself.
      builder
        .setOngoing(true)
        .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
        .setSound(alarmSound(), AudioManager.STREAM_ALARM)
        .setVibrate(ALARM_VIBRATION)
      fullScreenIntent(context, reminder)?.let { builder.setFullScreenIntent(it, true) }
    } else {
      builder
        .setVisibility(NotificationCompat.VISIBILITY_PRIVATE)
        .setDefaults(NotificationCompat.DEFAULT_SOUND or NotificationCompat.DEFAULT_LIGHTS)
        .setVibrate(REMINDER_VIBRATION)
    }

    val notification = builder.build()
    if (ringing) {
      // Repeat the sound until the user reacts.
      notification.flags = notification.flags or Notification.FLAG_INSISTENT
    }
    NotificationManagerCompat.from(context).notify(reminder.id, notification)
  }

  /** Removes the notification of reminder [id] (stops an alarm ringing). */
  fun cancel(context: Context, id: Int) {
    NotificationManagerCompat.from(context).cancel(id)
  }

  /**
   * Launch intent of the app carrying the note to open; read by the plugin in `load` (cold
   * start) or `onNewIntent` (app running).
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

  private fun fullScreenIntent(context: Context, reminder: Reminder): PendingIntent? {
    val intent = reminder.putInto(Intent(context, AlarmActivity::class.java))
    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_NO_USER_ACTION)
    return PendingIntent.getActivity(
      context,
      reminder.id,
      intent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )
  }

  private fun alarmSound(): Uri? =
    RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM)
      ?: RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE)
}
