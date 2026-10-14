// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.Manifest
import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.Settings
import android.util.Log
import android.webkit.WebView
import androidx.annotation.RequiresApi
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import app.tauri.PermissionState
import app.tauri.annotation.Command
import app.tauri.annotation.InvokeArg
import app.tauri.annotation.Permission
import app.tauri.annotation.PermissionCallback
import app.tauri.annotation.TauriPlugin
import app.tauri.plugin.Invoke
import app.tauri.plugin.JSObject
import app.tauri.plugin.Plugin

/** Permission alias of POST_NOTIFICATIONS (Android 13+). */
private const val ALIAS_NOTIFICATIONS = "notifications"

/** Permission alias of the legacy storage permissions (Android < 11). */
private const val ALIAS_STORAGE = "storage"

/** Event emitted when a reminder notification is tapped while the web app listens. */
private const val EVENT_ACTIVATED = "notification-activated"

private const val TAG = "BrulionAndroid"

/** `AndroidReminder` of the IPC contract, as Rust forwards it. */
@InvokeArg
class ReminderArg {
  var id: Int = 0
  var title: String = ""
  var body: String = ""
  var at: Long = 0
  var alarm: Boolean = false
  var path: String = ""
  var line: Int = 0

  /** Alarms only: minutes of ringing before it turns into a silent "Przegapiony alarm". */
  var ringMinutes: Int? = null

  internal fun toReminder(): Reminder =
    Reminder(
      id = id,
      title = title,
      body = body,
      at = at,
      alarm = alarm,
      path = path,
      line = line,
      ringMinutes = RingTimeout.minutes(ringMinutes?.toLong())
    )
}

@InvokeArg
class ScheduleRemindersArgs {
  // An array rather than a List: Jackson then needs no generic signature (kept by R8 or not).
  var items: Array<ReminderArg> = emptyArray()
}

/**
 * Brulion's Android plugin (`brulion-android`). Commands are called from Rust
 * (`src-tauri/plugins/brulion-android/src/mobile.rs`) and run on the main thread.
 */
@TauriPlugin(
  permissions = [
    Permission(strings = [Manifest.permission.POST_NOTIFICATIONS], alias = ALIAS_NOTIFICATIONS),
    Permission(
      strings = [Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE],
      alias = ALIAS_STORAGE
    )
  ]
)
class BrulionAndroidPlugin(private val activity: Activity) : Plugin(activity) {
  /** Notification tap not delivered to a listener yet (cold start), for `takeActivation`. */
  private var pendingActivation: JSObject? = null

  override fun load(webView: WebView) {
    super.load(webView)
    ReminderNotifier.ensureChannels(activity)
    activationFrom(activity.intent)?.let { deliver(it) }
  }

  override fun onNewIntent(intent: Intent) {
    super.onNewIntent(intent)
    activationFrom(intent)?.let { deliver(it) }
  }

  // ---------------------------------------------------------------------------------------------
  // All-files access (a Syncthing folder on shared storage as the vault)
  // ---------------------------------------------------------------------------------------------

  @Command
  fun hasAllFilesAccess(invoke: Invoke) {
    invoke.resolve(granted(allFilesGranted()))
  }

  @Command
  fun requestAllFilesAccess(invoke: Invoke) {
    when {
      allFilesGranted() -> invoke.resolve()
      Build.VERSION.SDK_INT >= Build.VERSION_CODES.R -> {
        // The app re-checks when it resumes.
        startFirst(
          Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, packageUri()),
          Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION),
          appDetailsIntent()
        )
        invoke.resolve()
      }
      getPermissionState(ALIAS_STORAGE) == PermissionState.DENIED -> {
        // "Don't ask again": only the app settings can grant it now.
        startFirst(appDetailsIntent())
        invoke.resolve()
      }
      else -> requestPermissionForAlias(ALIAS_STORAGE, invoke, "storagePermissionCallback")
    }
  }

  @PermissionCallback
  private fun storagePermissionCallback(invoke: Invoke) {
    invoke.resolve()
  }

  private fun allFilesGranted(): Boolean =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
      Environment.isExternalStorageManager()
    } else {
      ContextCompat.checkSelfPermission(activity, Manifest.permission.WRITE_EXTERNAL_STORAGE) ==
        PackageManager.PERMISSION_GRANTED
    }

  // ---------------------------------------------------------------------------------------------
  // Notifications
  // ---------------------------------------------------------------------------------------------

  @Command
  fun notificationPermission(invoke: Invoke) {
    invoke.resolve(granted(notificationsEnabled()))
  }

  @Command
  fun requestNotificationPermission(invoke: Invoke) {
    when {
      notificationsEnabled() -> invoke.resolve(granted(true))
      Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
        getPermissionState(ALIAS_NOTIFICATIONS) != PermissionState.DENIED &&
        ContextCompat.checkSelfPermission(activity, Manifest.permission.POST_NOTIFICATIONS) !=
        PackageManager.PERMISSION_GRANTED ->
        requestPermissionForAlias(ALIAS_NOTIFICATIONS, invoke, "notificationPermissionCallback")
      else -> {
        // Turned off by the user (or "don't ask again"): the system will not prompt.
        openNotificationSettings()
        invoke.resolve(granted(false))
      }
    }
  }

  @PermissionCallback
  private fun notificationPermissionCallback(invoke: Invoke) {
    invoke.resolve(granted(notificationsEnabled()))
  }

  private fun notificationsEnabled(): Boolean = NotificationManagerCompat.from(activity).areNotificationsEnabled()

  private fun openNotificationSettings() {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
      startFirst(notificationSettingsIntent(), appDetailsIntent())
    } else {
      startFirst(appDetailsIntent())
    }
  }

  // ---------------------------------------------------------------------------------------------
  // Full-screen intents (the alarm screen over the lock screen, Android 14+)
  // ---------------------------------------------------------------------------------------------

  @Command
  fun canUseFullScreenIntent(invoke: Invoke) {
    invoke.resolve(granted(ReminderNotifier.canUseFullScreenIntent(activity)))
  }

  @Command
  fun requestFullScreenIntent(invoke: Invoke) {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE &&
      !ReminderNotifier.canUseFullScreenIntent(activity)
    ) {
      // "Full screen notifications" special access; the app re-checks when it resumes.
      startFirst(
        Intent(Settings.ACTION_MANAGE_APP_USE_FULL_SCREEN_INTENT, packageUri()),
        notificationSettingsIntent(),
        appDetailsIntent()
      )
    }
    invoke.resolve()
  }

  // ---------------------------------------------------------------------------------------------
  // Exact alarms
  // ---------------------------------------------------------------------------------------------

  @Command
  fun canScheduleExact(invoke: Invoke) {
    invoke.resolve(granted(AlarmScheduler.canScheduleExact(activity)))
  }

  @Command
  fun requestExactAlarms(invoke: Invoke) {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && !AlarmScheduler.canScheduleExact(activity)) {
      // "Alarms & reminders"; granting it re-arms the alarms (BootReceiver).
      startFirst(Intent(Settings.ACTION_REQUEST_SCHEDULE_EXACT_ALARM, packageUri()), appDetailsIntent())
    }
    invoke.resolve()
  }

  // ---------------------------------------------------------------------------------------------
  // Reminders
  // ---------------------------------------------------------------------------------------------

  @Command
  fun scheduleReminders(invoke: Invoke) {
    try {
      val args = invoke.parseArgs(ScheduleRemindersArgs::class.java)
      val result = AlarmScheduler.replaceAll(activity, args.items.map { it.toReminder() })
      val answer = JSObject()
      answer.put("scheduled", result.scheduled)
      answer.put("exact", result.exact)
      invoke.resolve(answer)
    } catch (e: Exception) {
      invoke.reject("scheduleReminders failed: ${e.message}", e)
    }
  }

  @Command
  fun takeActivation(invoke: Invoke) {
    val activation = pendingActivation
    pendingActivation = null
    invoke.resolve(activation ?: JSObject())
  }

  /**
   * Reads (and removes) the note a tapped notification points at from [intent]. Ignores
   * intents replayed from Recents after the process died: that tap was handled already.
   */
  private fun activationFrom(intent: Intent?): JSObject? {
    if (intent == null || !intent.hasExtra(Extras.PATH)) return null
    val path = intent.getStringExtra(Extras.PATH)
    val line = intent.getIntExtra(Extras.LINE, 0)
    val notificationId = if (intent.hasExtra(Extras.NOTIFICATION_ID)) intent.getIntExtra(Extras.NOTIFICATION_ID, 0) else null
    intent.removeExtra(Extras.PATH)
    intent.removeExtra(Extras.LINE)
    intent.removeExtra(Extras.NOTIFICATION_ID)
    if ((intent.flags and Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY) != 0) return null
    // Tapping also ends a ringing alarm.
    if (notificationId != null) ReminderNotifier.cancel(activity, notificationId)
    if (path == null) return null
    val activation = JSObject()
    activation.put("path", path)
    activation.put("line", line)
    return activation
  }

  /** Emits the activation when the web app listens, else keeps it for `takeActivation`. */
  private fun deliver(activation: JSObject) {
    if (hasListener(EVENT_ACTIVATED)) {
      pendingActivation = null
      trigger(EVENT_ACTIVATED, activation)
    } else {
      pendingActivation = activation
    }
  }

  // ---------------------------------------------------------------------------------------------
  // Helpers
  // ---------------------------------------------------------------------------------------------

  private fun granted(value: Boolean): JSObject {
    val result = JSObject()
    result.put("granted", value)
    return result
  }

  private fun packageUri(): Uri = Uri.parse("package:" + activity.packageName)

  private fun appDetailsIntent(): Intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, packageUri())

  /** The app's notification settings (Android 8+). */
  @RequiresApi(Build.VERSION_CODES.O)
  private fun notificationSettingsIntent(): Intent =
    Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS).putExtra(Settings.EXTRA_APP_PACKAGE, activity.packageName)

  /** Starts the first of [intents] the system can handle (settings screens vary by vendor). */
  private fun startFirst(vararg intents: Intent): Boolean {
    for (intent in intents) {
      try {
        activity.startActivity(intent)
        return true
      } catch (e: ActivityNotFoundException) {
        Log.w(TAG, "no activity for ${intent.action}")
      } catch (e: SecurityException) {
        Log.w(TAG, "not allowed to open ${intent.action}", e)
      }
    }
    return false
  }
}
