// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.Manifest
import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.util.Log
import androidx.core.content.ContextCompat
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject
import java.lang.ref.WeakReference

/**
 * Brulion's Android side as seen from C++ (`src/platform/android/androidbridge.cpp`).
 *
 * Every `@JvmStatic` function is a static Java method called through JNI (class
 * `io/github/januszbit/brulion/android/BrulionAndroid`); keep their names and signatures in
 * sync with androidbridge.cpp. They may be called from Qt's thread: work that needs the main
 * thread (permission dialogs, settings screens) is posted to it. `Context` parameters accept the
 * activity or any other context (QNativeInterface::QAndroidApplication::context()); dialogs use
 * the running [BrulionActivity] when there is one.
 *
 * Notification taps reach C++ in two ways (docs/ANDROID.md, "Most JNI (C++ ↔ Kotlin)"):
 * * cold start — the tap launched the app: [BrulionActivity] keeps the activation until C++
 *   attaches (Qt's `main()` runs in its own thread), C++ fetches it with [takeActivation]
 *   right after [attach];
 * * app running — `onNewIntent` calls the native method [nativeActivated], which C++ registered
 *   before calling [attach]; it emits `AndroidBridge::activated` on Qt's thread.
 */
object BrulionAndroid {
  private const val TAG = "BrulionAndroid"

  /** Request codes of the runtime permission dialogs (results: [onPermissionsResult]). */
  private const val REQUEST_NOTIFICATIONS = 0x4201
  private const val REQUEST_STORAGE = 0x4202

  /** Which runtime permissions the app asked for already (to detect "don't ask again"). */
  private const val PREFS_PERMISSIONS = "io.github.januszbit.brulion.android.permissions"

  private val lock = Any()

  /** Notification tap not delivered to C++ yet (guarded by [lock]). */
  private var pendingActivation: JSONObject? = null

  /** C++ registered the native methods and listens (guarded by [lock]). */
  private var nativeAttached = false

  @Volatile private var activityRef: WeakReference<Activity>? = null

  // -----------------------------------------------------------------------------------------
  // Native methods, registered from C++ (QJniEnvironment::registerNativeMethods)
  // -----------------------------------------------------------------------------------------

  /** A notification of the note [path] (reminder on [line]) was tapped while Qt runs. */
  @JvmStatic external fun nativeActivated(path: String, line: Int)

  /** A permission dialog was answered: the app should re-check its permissions. */
  @JvmStatic external fun nativePermissionsChanged()

  // -----------------------------------------------------------------------------------------
  // Lifecycle (C++)
  // -----------------------------------------------------------------------------------------

  /** C++ registered the native methods: deliver activations to it from now on. */
  @JvmStatic
  fun attach(context: Context) {
    ReminderNotifier.ensureChannels(context)
    synchronized(lock) { nativeAttached = true }
  }

  /** C++ is going away (application shutdown): keep activations for [takeActivation]. */
  @JvmStatic
  fun detach() {
    synchronized(lock) { nativeAttached = false }
  }

  // -----------------------------------------------------------------------------------------
  // All-files access (a Syncthing folder on shared storage as the vault)
  // -----------------------------------------------------------------------------------------

  @JvmStatic
  fun hasAllFilesAccess(context: Context): Boolean =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
      Environment.isExternalStorageManager()
    } else {
      ContextCompat.checkSelfPermission(context, Manifest.permission.WRITE_EXTERNAL_STORAGE) ==
        PackageManager.PERMISSION_GRANTED
    }

  /**
   * Android 11+: opens the "All files access" screen (the app re-checks when it resumes).
   * Android 9–10: asks for the storage permission, or opens the app settings after "don't ask
   * again".
   */
  @JvmStatic
  fun requestAllFilesAccess(context: Context) {
    onMain {
      val activity = currentActivity()
      when {
        hasAllFilesAccess(context) -> Unit
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.R ->
          startFirst(
            context,
            Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, packageUri(context)),
            Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION),
            appDetailsIntent(context)
          )
        activity != null && mayAsk(activity, Manifest.permission.WRITE_EXTERNAL_STORAGE) ->
          ask(
            activity,
            arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE),
            REQUEST_STORAGE
          )
        else -> startFirst(context, appDetailsIntent(context))
      }
    }
  }

  // -----------------------------------------------------------------------------------------
  // Notifications
  // -----------------------------------------------------------------------------------------

  /** May Brulion post notifications (permission granted and notifications not turned off)? */
  @JvmStatic
  fun notificationPermission(context: Context): Boolean = ReminderNotifier.canPost(context)

  /**
   * Asks for the notification permission (Android 13+ system dialog); when the system would
   * not ask any more (denied for good, or notifications turned off) opens Brulion's
   * notification settings. Returns the current state: the answer arrives later through
   * [nativePermissionsChanged] (dialog) or when the app resumes (settings).
   */
  @JvmStatic
  fun requestNotificationPermission(context: Context): Boolean {
    if (ReminderNotifier.canPost(context)) return true
    onMain {
      val activity = currentActivity()
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
        activity != null &&
        ContextCompat.checkSelfPermission(activity, Manifest.permission.POST_NOTIFICATIONS) !=
        PackageManager.PERMISSION_GRANTED &&
        mayAsk(activity, Manifest.permission.POST_NOTIFICATIONS)
      ) {
        ask(activity, arrayOf(Manifest.permission.POST_NOTIFICATIONS), REQUEST_NOTIFICATIONS)
      } else {
        startFirst(context, notificationSettingsIntent(context), appDetailsIntent(context))
      }
    }
    return false
  }

  // -----------------------------------------------------------------------------------------
  // Exact alarms and full-screen intents
  // -----------------------------------------------------------------------------------------

  @JvmStatic
  fun canScheduleExact(context: Context): Boolean = AlarmScheduler.canScheduleExact(context)

  /** Android 12: opens "Alarms & reminders"; granting it re-arms the alarms (BootReceiver). */
  @JvmStatic
  fun requestExactAlarms(context: Context) {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && !AlarmScheduler.canScheduleExact(context)) {
      onMain {
        startFirst(
          context,
          Intent(Settings.ACTION_REQUEST_SCHEDULE_EXACT_ALARM, packageUri(context)),
          appDetailsIntent(context)
        )
      }
    }
  }

  @JvmStatic
  fun canUseFullScreenIntent(context: Context): Boolean = ReminderNotifier.canUseFullScreenIntent(context)

  /** Android 14+: opens "Full screen notifications" (the alarm screen over the lock screen). */
  @JvmStatic
  fun requestFullScreenIntent(context: Context) {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE &&
      !ReminderNotifier.canUseFullScreenIntent(context)
    ) {
      onMain {
        startFirst(
          context,
          Intent(Settings.ACTION_MANAGE_APP_USE_FULL_SCREEN_INTENT, packageUri(context)),
          notificationSettingsIntent(context),
          appDetailsIntent(context)
        )
      }
    }
  }

  // -----------------------------------------------------------------------------------------
  // Reminders
  // -----------------------------------------------------------------------------------------

  /**
   * Replaces the armed reminders with [json], a JSON array of
   * `{id, title, body, at (epoch ms), alarm, ringMinutes, path, line}` (entries without a
   * numeric `id`/`at` are skipped). Returns `{"scheduled": n, "exact": bool}`, or
   * `{"error": "..."}` — then the armed set is left as it was.
   */
  @JvmStatic
  fun scheduleReminders(context: Context, json: String): String =
    try {
      // Parsed strictly first: a malformed argument must not cancel every armed reminder.
      val array = JSONArray(json)
      val result = AlarmScheduler.replaceAll(context.applicationContext, Reminder.listFromJson(array))
      JSONObject().put("scheduled", result.scheduled).put("exact", result.exact).toString()
    } catch (e: JSONException) {
      errorJson("scheduleReminders: not a JSON array", e)
    } catch (e: RuntimeException) {
      errorJson("scheduleReminders failed: $e", e)
    }

  /**
   * The notification tap not delivered yet (cold start), as `{"path": "...", "line": n}`, or
   * `{}`. Forgets it: each tap is handed out once.
   */
  @JvmStatic
  @Suppress("UNUSED_PARAMETER") // kept for a stable JNI signature
  fun takeActivation(context: Context): String =
    synchronized(lock) {
      val activation = pendingActivation
      pendingActivation = null
      (activation ?: JSONObject()).toString()
    }

  // -----------------------------------------------------------------------------------------
  // BrulionActivity hooks (main thread)
  // -----------------------------------------------------------------------------------------

  internal fun onActivityCreated(activity: Activity) {
    activityRef = WeakReference(activity)
    ReminderNotifier.ensureChannels(activity)
    onActivityIntent(activity, activity.intent)
  }

  internal fun onActivityDestroyed(activity: Activity) {
    if (activityRef?.get() === activity) activityRef = null
  }

  internal fun onActivityIntent(activity: Activity, intent: Intent?) {
    activationFrom(activity, intent)?.let { deliver(it) }
  }

  internal fun onPermissionsResult(requestCode: Int) {
    if (requestCode != REQUEST_NOTIFICATIONS && requestCode != REQUEST_STORAGE) return
    synchronized(lock) {
      if (!nativeAttached) return
      try {
        nativePermissionsChanged()
      } catch (e: UnsatisfiedLinkError) {
        Log.w(TAG, "native permission callback not registered", e)
      }
    }
  }

  // -----------------------------------------------------------------------------------------
  // Helpers
  // -----------------------------------------------------------------------------------------

  /**
   * Reads (and removes) the note a tapped notification points at from [intent]. Ignores
   * intents replayed from Recents after the process died: that tap was handled already.
   */
  private fun activationFrom(context: Context, intent: Intent?): JSONObject? {
    if (intent == null || !intent.hasExtra(Extras.PATH)) return null
    val path = intent.getStringExtra(Extras.PATH)
    val line = intent.getIntExtra(Extras.LINE, 0)
    val notificationId =
      if (intent.hasExtra(Extras.NOTIFICATION_ID)) intent.getIntExtra(Extras.NOTIFICATION_ID, 0) else null
    intent.removeExtra(Extras.PATH)
    intent.removeExtra(Extras.LINE)
    intent.removeExtra(Extras.NOTIFICATION_ID)
    if ((intent.flags and Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY) != 0) return null
    // Tapping also ends a ringing alarm.
    if (notificationId != null) ReminderNotifier.cancel(context, notificationId)
    if (path.isNullOrEmpty()) return null
    return JSONObject().put("path", path).put("line", line)
  }

  /** Hands [activation] to C++ when it listens, else keeps it for [takeActivation]. */
  private fun deliver(activation: JSONObject) {
    synchronized(lock) {
      if (nativeAttached) {
        try {
          nativeActivated(activation.getString("path"), activation.optInt("line", 0))
          pendingActivation = null
          return
        } catch (e: UnsatisfiedLinkError) {
          Log.w(TAG, "native activation callback not registered", e)
        }
      }
      pendingActivation = activation
    }
  }

  private fun errorJson(message: String, e: Exception): String {
    Log.e(TAG, message, e)
    return JSONObject().put("error", message).toString()
  }

  private fun currentActivity(): Activity? = activityRef?.get()?.takeUnless { it.isFinishing || it.isDestroyed }

  private fun onMain(block: () -> Unit) {
    if (Looper.myLooper() == Looper.getMainLooper()) {
      block()
    } else {
      Handler(Looper.getMainLooper()).post(block)
    }
  }

  /**
   * May the system dialog still be shown for [permission]? Not after "don't ask again" (or a
   * second refusal on Android 11+): asked before and no rationale wanted.
   */
  private fun mayAsk(activity: Activity, permission: String): Boolean {
    val asked = activity.getSharedPreferences(PREFS_PERMISSIONS, Context.MODE_PRIVATE).getBoolean(permission, false)
    return !asked || activity.shouldShowRequestPermissionRationale(permission)
  }

  private fun ask(activity: Activity, permissions: Array<String>, requestCode: Int) {
    val prefs = activity.getSharedPreferences(PREFS_PERMISSIONS, Context.MODE_PRIVATE).edit()
    for (permission in permissions) prefs.putBoolean(permission, true)
    prefs.apply()
    activity.requestPermissions(permissions, requestCode)
  }

  private fun packageUri(context: Context): Uri = Uri.parse("package:" + context.packageName)

  private fun appDetailsIntent(context: Context): Intent =
    Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, packageUri(context))

  /** The app's notification settings (Android 8+). */
  private fun notificationSettingsIntent(context: Context): Intent =
    Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS).putExtra(Settings.EXTRA_APP_PACKAGE, context.packageName)

  /** Starts the first of [intents] the system can handle (settings screens vary by vendor). */
  private fun startFirst(context: Context, vararg intents: Intent): Boolean {
    val starter: Context = currentActivity() ?: context
    for (intent in intents) {
      if (starter !is Activity) intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
      try {
        starter.startActivity(intent)
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
