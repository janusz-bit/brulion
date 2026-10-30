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
import androidx.core.view.WindowCompat
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
 * Shares from other apps ([ShareActivity]) take the same two ways: [takeShare] and [nativeShared].
 *
 * "Zrobione" taps go the same two ways, through a persisted queue ([DoneQueue]): [nativeDoneQueued]
 * while C++ listens, [takeDoneRequests] after its start. So do the entries of the widgets' quick add
 * that Kotlin could not write itself ([WidgetInbox]: [nativeWidgetQueued], [takeWidgetActions]);
 * the widgets' snapshot comes from C++ through [updateWidgets].
 */
object BrulionAndroid {
  private const val TAG = "BrulionAndroid"

  /** Request codes of the runtime permission dialogs (results: [onPermissionsResult]). */
  private const val REQUEST_NOTIFICATIONS = 0x4201
  private const val REQUEST_STORAGE = 0x4202

  /** Request codes of the ringtone pickers (results: [onActivityResult]). */
  private const val REQUEST_SOUND_REMINDER = 0x4203
  private const val REQUEST_SOUND_ALARM = 0x4204

  /** Which runtime permissions the app asked for already (to detect "don't ask again"). */
  private const val PREFS_PERMISSIONS = "io.github.januszbit.brulion.android.permissions"

  private val lock = Any()

  /** Notification tap not delivered to C++ yet (guarded by [lock]). */
  private var pendingActivation: JSONObject? = null

  /** A share ([ShareActivity]) not delivered to C++ yet, as JSON (guarded by [lock]). */
  private var pendingShare: String? = null

  /** C++ registered the native methods and listens (guarded by [lock]). */
  private var nativeAttached = false

  @Volatile private var activityRef: WeakReference<Activity>? = null
  /** [setSystemBarsLight]'s value, null before C++ asked. */
  @Volatile private var systemBarsLight: Boolean? = null

  // -----------------------------------------------------------------------------------------
  // Native methods, registered from C++ (QJniEnvironment::registerNativeMethods)
  // -----------------------------------------------------------------------------------------

  /** A notification of the note [path] (reminder on [line]) was tapped while Qt runs. */
  @JvmStatic external fun nativeActivated(path: String, line: Int)

  /**
   * Another app shared text or files ([ShareActivity]) while Qt runs: [json] as [takeShare] gives
   * it (`src/core/notebook/share.h`).
   */
  @JvmStatic external fun nativeShared(json: String)

  /** A permission dialog was answered: the app should re-check its permissions. */
  @JvmStatic external fun nativePermissionsChanged()

  /** "Zrobione" was tapped ([DoneQueue] holds it): C++ takes the queue with [takeDoneRequests]. */
  @JvmStatic external fun nativeDoneQueued()

  /** Quick add of a widget queued an entry ([WidgetInbox]): C++ takes it with [takeWidgetActions]. */
  @JvmStatic external fun nativeWidgetQueued()

  /** Quick add of a widget wrote the note [path] (absolute) itself: C++ hands its reminders over again. */
  @JvmStatic external fun nativeWidgetWrote(path: String)

  /**
   * The ringtone picker of [pickSound] answered for [kind] (0 reminders, 1 alarms): [json] is the
   * choice `{mode, uri, title}`, or `{"error": "..."}` when there is no picker.
   */
  @JvmStatic external fun nativeSoundPicked(kind: Int, json: String)

  /**
   * The on-screen keyboard covers [overlap] physical pixels of Qt's view ([ImeInsets]): [shown]
   * while it is up or coming up, [animating] for each frame of its slide (Android 11+).
   */
  @JvmStatic external fun nativeImeChanged(overlap: Int, shown: Boolean, animating: Boolean)

  /**
   * The system keeps [left] and [right] physical pixels of the window's edges for its own swipes
   * (gesture navigation's "back"; [ImeInsets]): the drawer's edge swipe starts beyond them.
   */
  @JvmStatic external fun nativeGestureInsetsChanged(left: Int, right: Int)

  // -----------------------------------------------------------------------------------------
  // Lifecycle (C++)
  // -----------------------------------------------------------------------------------------

  /** C++ registered the native methods: deliver activations to it from now on. */
  @JvmStatic
  fun attach(context: Context) {
    ReminderNotifier.ensureChannels(context)
    synchronized(lock) { nativeAttached = true }
    // What the keyboard observer saw before C++ listened.
    onMain { ImeInsets.reportAgain() }
  }

  /**
   * C++ asks for the on-screen keyboard's state and the system gesture insets (they arrive through
   * [nativeImeChanged] and [nativeGestureInsetsChanged]).
   */
  @JvmStatic
  fun requestImeState() {
    onMain { ImeInsets.reportAgain() }
  }

  /**
   * The window is drawn behind the system bars (Android 11+, src/app/ui/keyboardsources.cpp): their
   * icons are dark over a [light] window, light over a dark one — the app's theme, not the
   * system's day/night mode, which a theme chosen in the settings may differ from.
   */
  @JvmStatic
  fun setSystemBarsLight(light: Boolean) {
    systemBarsLight = light
    onMain { applySystemBars() }
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

  /**
   * The share of another app not delivered yet (cold start: [ShareActivity] started the process
   * before Qt), as JSON, or `{}`. Forgets it: each share is handed out once.
   */
  @JvmStatic
  @Suppress("UNUSED_PARAMETER") // kept for a stable JNI signature
  fun takeShare(context: Context): String =
    synchronized(lock) {
      val share = pendingShare
      pendingShare = null
      share ?: "{}"
    }

  /**
   * Hands a share to C++ when it listens, else keeps it for [takeShare] (a newer share replaces
   * one not taken yet). Called by [ShareActivity] in this process — never from an intent extra,
   * which any app could send to the exported [BrulionActivity].
   */
  internal fun deliverShare(json: String) {
    synchronized(lock) {
      if (nativeAttached) {
        try {
          nativeShared(json)
          pendingShare = null
          return
        } catch (e: UnsatisfiedLinkError) {
          Log.w(TAG, "native share callback not registered", e)
        }
      }
      pendingShare = json
    }
  }

  /**
   * The "Zrobione" taps of notebook [vault] not handed over yet ([DoneQueue], also those of while
   * Brulion was closed): a JSON array of `{key, local, path, line, title, day, vault}`, `[]` when
   * none. Forgets them (each tap is handed out once); the taps of other notebooks stay queued.
   * Empty [vault]: all of them.
   */
  @JvmStatic
  fun takeDoneRequests(context: Context, vault: String?): String =
    try {
      DoneQueue.take(context, vault ?: "")
    } catch (e: RuntimeException) {
      Log.e(TAG, "takeDoneRequests failed", e)
      "[]"
    }

  // -----------------------------------------------------------------------------------------
  // Home-screen widgets (docs/ANDROID.md "Widgety")
  // -----------------------------------------------------------------------------------------

  /**
   * Keeps the widgets' snapshot [json] (src/app/widgets/widgetsnapshot.h) and redraws every placed
   * widget. Returns `{"widgets": n}` (how many are placed), or `{"error": "..."}` — then the
   * stored snapshot is left as it was.
   */
  @JvmStatic
  fun updateWidgets(context: Context, json: String): String =
    try {
      // Parsed strictly first: a malformed argument must not replace a good snapshot.
      JSONObject(json)
      val app = context.applicationContext
      WidgetStore.saveSnapshot(app, json)
      JSONObject().put("widgets", WidgetUpdater.refreshAll(app)).toString()
    } catch (e: JSONException) {
      errorJson("updateWidgets: not a JSON object", e)
    } catch (e: RuntimeException) {
      errorJson("updateWidgets failed: $e", e)
    }

  /**
   * The quick-add entries of notebook [vault] Kotlin could not write itself ([WidgetInbox]): a
   * JSON array of `{id, kind, text, date, time, alarm, vault, added}`, `[]` when none. Forgets
   * them; the entries of other notebooks stay queued. Empty [vault]: all of them.
   */
  @JvmStatic
  fun takeWidgetActions(context: Context, vault: String?): String =
    try {
      WidgetInbox.take(context, vault ?: "")
    } catch (e: RuntimeException) {
      Log.e(TAG, "takeWidgetActions failed", e)
      "[]"
    }

  /**
   * Puts entries C++ took with [takeWidgetActions] but could not write either back into
   * [WidgetInbox] (the same JSON array), for its next try. Tells nobody: C++ tries again when it
   * returns to the front or starts.
   */
  @JvmStatic
  fun requeueWidgetActions(context: Context, json: String?) {
    try {
      WidgetInbox.addAll(context, WidgetInbox.parse(json))
    } catch (e: RuntimeException) {
      Log.e(TAG, "requeueWidgetActions failed", e)
    }
  }

  /** Quick add queued an entry ([QuickAdd]): tells C++ when it listens (else its start takes it). */
  internal fun widgetQueued() {
    synchronized(lock) {
      if (!nativeAttached) return
      try {
        nativeWidgetQueued()
      } catch (e: UnsatisfiedLinkError) {
        Log.w(TAG, "native widget callback not registered", e)
      }
    }
  }

  /**
   * Quick add wrote [path] (absolute) itself: C++, once its loop runs again, hands its reminder set
   * over again ([scheduleReminders] even when unchanged) — a provisional reminder gives way.
   */
  internal fun widgetWrote(path: String) {
    synchronized(lock) {
      if (!nativeAttached) return
      try {
        nativeWidgetWrote(path)
      } catch (e: UnsatisfiedLinkError) {
        Log.w(TAG, "native widget callback not registered", e)
      }
    }
  }

  // -----------------------------------------------------------------------------------------
  // Picked documents (imports): any thread — C++ lists picked folders on a worker thread
  // -----------------------------------------------------------------------------------------

  /**
   * `{"uri", "name", "dir", "size", "virtual"}` of a picked document, of a picked tree's root
   * document, or of any openable content URI; `{"error": "..."}` when it cannot be read.
   */
  @JvmStatic
  fun documentInfo(context: Context, uri: String): String = Documents.info(context, uri)

  /** `{"documents": [{"uri", "name", "dir", "size", "virtual"}, ...]}` of a folder document. */
  @JvmStatic
  fun listDocuments(context: Context, uri: String): String = Documents.list(context, uri)

  // -----------------------------------------------------------------------------------------
  // PDF pages as a drawing's backdrop (src/platform/pdfpages_android.cpp): any thread
  // -----------------------------------------------------------------------------------------

  /** `{"pages", "sizes": [[w, h], ...] (points), "error", "locked"}` of the PDF at [path]. */
  @JvmStatic
  fun pdfInfo(path: String): String = PdfPages.info(path)

  /**
   * Page [page] (1-based) of the PDF at [path] on white, its long side [maxSide] pixels: 8 bytes of
   * header (width, height, big-endian) and the RGBA_8888 premultiplied pixels; null on failure.
   */
  @JvmStatic
  fun renderPdfPage(path: String, page: Int, maxSide: Int): ByteArray? = PdfPages.render(path, page, maxSide)

  // -----------------------------------------------------------------------------------------
  // Sounds of reminders and alarms (ReminderSounds.kt; Settings → Reminders in the app)
  // -----------------------------------------------------------------------------------------

  /**
   * Applies the sounds `{"reminder": {mode, uri, title}, "alarm": {...}}`: a kind whose sound
   * changed gets a new notification channel (the old one is deleted). Returns the current channel
   * ids `{"reminderChannel": "...", "alarmChannel": "..."}`, or `{"error": "..."}`.
   */
  @JvmStatic
  fun setReminderSounds(context: Context, json: String): String =
    try {
      val sounds = JSONObject(json)
      val channels =
        ReminderSounds.apply(
          context.applicationContext,
          SoundChoice.fromJson(sounds.optJSONObject(SoundKind.REMINDER.key)),
          SoundChoice.fromJson(sounds.optJSONObject(SoundKind.ALARM.key))
        )
      JSONObject()
        .put("reminderChannel", channels[SoundKind.REMINDER])
        .put("alarmChannel", channels[SoundKind.ALARM])
        .toString()
    } catch (e: JSONException) {
      errorJson("setReminderSounds: not a JSON object", e)
    } catch (e: RuntimeException) {
      errorJson("setReminderSounds failed: $e", e)
    }

  /**
   * Opens the system's ringtone picker for [kind] (0 reminders, 1 alarms) showing [current]
   * (`{mode, uri}`); the answer goes to [nativeSoundPicked] (nothing when cancelled). False for
   * an unknown kind.
   */
  @JvmStatic
  fun pickSound(context: Context, kind: Int, current: String): Boolean {
    val soundKind = SoundKind.fromCode(kind) ?: return false
    onMain {
      val activity = currentActivity()
      if (activity == null) {
        soundPicked(soundKind, JSONObject().put("error", "no activity"))
        return@onMain
      }
      try {
        val intent = ReminderSounds.pickerIntent(activity, soundKind, SoundChoice.parse(current))
        val requestCode = if (soundKind == SoundKind.ALARM) REQUEST_SOUND_ALARM else REQUEST_SOUND_REMINDER
        activity.startActivityForResult(intent, requestCode)
      } catch (e: ActivityNotFoundException) {
        Log.w(TAG, "no ringtone picker", e)
        soundPicked(soundKind, JSONObject().put("error", "no ringtone picker"))
      }
    }
    return true
  }

  /**
   * Plays `{kind: "reminder" | "alarm", mode, uri}` for about three seconds (vibrates for
   * "vibrate"). False when there is nothing to play.
   */
  @JvmStatic
  fun previewSound(context: Context, json: String): Boolean =
    try {
      val item = JSONObject(json)
      val kind = SoundKind.fromKey(item.optString("kind"))
      kind != null && ReminderSounds.preview(context, kind, SoundChoice.fromJson(item))
    } catch (e: JSONException) {
      Log.w(TAG, "previewSound: not a JSON object", e)
      false
    }

  /** Opens Android's settings of the notification channel of [kind] (0 reminders, 1 alarms). */
  @JvmStatic
  fun openSoundSettings(context: Context, kind: Int) {
    val soundKind = SoundKind.fromCode(kind) ?: return
    onMain {
      ReminderNotifier.ensureChannels(context)
      startFirst(
        context,
        Intent(Settings.ACTION_CHANNEL_NOTIFICATION_SETTINGS)
          .putExtra(Settings.EXTRA_APP_PACKAGE, context.packageName)
          .putExtra(Settings.EXTRA_CHANNEL_ID, ReminderNotifier.channelId(context, soundKind)),
        notificationSettingsIntent(context),
        appDetailsIntent(context)
      )
    }
  }

  // -----------------------------------------------------------------------------------------
  // BrulionActivity hooks (main thread)
  // -----------------------------------------------------------------------------------------

  internal fun onActivityCreated(activity: Activity) {
    activityRef = WeakReference(activity)
    applySystemBars()
    ReminderNotifier.ensureChannels(activity)
    onActivityIntent(activity, activity.intent)
  }

  /**
   * The system bars' icons as C++ asked ([setSystemBarsLight]) — again after a configuration
   * change, where Qt sets them after the system's day/night mode.
   */
  internal fun applySystemBars() {
    val light = systemBarsLight ?: return
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return
    val window = currentActivity()?.window ?: return
    WindowCompat.getInsetsController(window, window.decorView).run {
      isAppearanceLightStatusBars = light
      isAppearanceLightNavigationBars = light
    }
  }

  internal fun onActivityDestroyed(activity: Activity) {
    if (activityRef?.get() === activity) activityRef = null
  }

  internal fun onActivityIntent(activity: Activity, intent: Intent?) {
    activationFrom(activity, intent)?.let { deliver(it) }
  }

  /** An activity Brulion started for a result answered ([BrulionActivity.onActivityResult]). */
  internal fun onActivityResult(activity: Activity, requestCode: Int, resultCode: Int, data: Intent?) {
    val kind =
      when (requestCode) {
        REQUEST_SOUND_REMINDER -> SoundKind.REMINDER
        REQUEST_SOUND_ALARM -> SoundKind.ALARM
        else -> return
      }
    if (resultCode != Activity.RESULT_OK) return // cancelled: the choice stays
    soundPicked(kind, ReminderSounds.picked(activity, kind, data).toJson())
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

  /**
   * Hands the keyboard's state to C++ ([ImeInsets], main thread). False when C++ does not listen
   * (yet): the observer reports it again on [attach].
   */
  internal fun imeChanged(overlap: Int, shown: Boolean, animating: Boolean): Boolean {
    synchronized(lock) {
      if (!nativeAttached) return false
      try {
        nativeImeChanged(overlap, shown, animating)
        return true
      } catch (e: UnsatisfiedLinkError) {
        Log.w(TAG, "native keyboard callback not registered", e)
        return false
      }
    }
  }

  /**
   * Hands the system gesture insets to C++ ([ImeInsets], main thread). False when C++ does not
   * listen (yet): the observer reports them again on [attach].
   */
  internal fun gestureInsetsChanged(left: Int, right: Int): Boolean {
    synchronized(lock) {
      if (!nativeAttached) return false
      try {
        nativeGestureInsetsChanged(left, right)
        return true
      } catch (e: UnsatisfiedLinkError) {
        Log.w(TAG, "native gesture insets callback not registered", e)
        return false
      }
    }
  }

  /**
   * A "Zrobione" tap was queued ([ReminderActions.done], main thread): tells C++ when it listens;
   * otherwise it takes the queue once a notebook is ready after its start.
   */
  internal fun doneQueued() {
    synchronized(lock) {
      if (!nativeAttached) return
      try {
        nativeDoneQueued()
      } catch (e: UnsatisfiedLinkError) {
        Log.w(TAG, "native done callback not registered", e)
      }
    }
  }

  /** Hands a picked sound to C++ (it listens while attached; else it is dropped). */
  private fun soundPicked(kind: SoundKind, json: JSONObject) {
    synchronized(lock) {
      if (!nativeAttached) return
      try {
        nativeSoundPicked(kind.code, json.toString())
      } catch (e: UnsatisfiedLinkError) {
        Log.w(TAG, "native sound callback not registered", e)
      }
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
