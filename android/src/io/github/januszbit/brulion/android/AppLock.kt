// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.app.Activity
import android.app.KeyguardManager
import android.content.ClipData
import android.content.ClipDescription
import android.content.ClipboardManager
import android.content.Context
import android.hardware.biometrics.BiometricManager
import android.hardware.biometrics.BiometricPrompt
import android.os.Build
import android.os.CancellationSignal
import android.os.PersistableBundle
import android.util.Log
import android.view.WindowManager

/**
 * The app lock's Android side (docs/UI.md "App lock", docs/ANDROID.md "App lock"), called through
 * [BrulionAndroid]:
 * * the window's flags — FLAG_SECURE (nothing of the window on screenshots, recordings, a cast
 *   screen or the recent apps' preview) and, on Android 13+, the recent apps' preview alone left
 *   out (Activity.setRecentsScreenshotEnabled(false)) — kept for an activity made anew;
 * * the phone's own check: the framework's BiometricPrompt (API 28+, no androidx), biometrics or the
 *   screen lock's PIN, pattern or password (API 30+: BIOMETRIC_WEAK or DEVICE_CREDENTIAL; 28–29:
 *   setDeviceCredentialAllowed);
 * * a copy marked sensitive (Android 13+: ClipDescription.EXTRA_IS_SENSITIVE — the clipboard's
 *   preview and the keyboard's suggestions leave it out).
 */
internal object AppLock {
  private const val TAG = "BrulionAppLock"

  /** What C++ asked for ([BrulionAndroid.setSecureWindow]). */
  @Volatile var secure = false
  @Volatile var hideRecents = false

  /** The prompt being shown (one at a time). */
  private var cancel: CancellationSignal? = null

  /** The flags on [activity]'s window as asked (main thread). */
  fun apply(activity: Activity?) {
    val window = activity?.window
    if (window == null) {
      Log.w(TAG, "no activity for the window's flags (secure: $secure)")
      return
    }
    val was = window.attributes.flags and WindowManager.LayoutParams.FLAG_SECURE != 0
    if (secure) {
      window.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
    } else {
      window.clearFlags(WindowManager.LayoutParams.FLAG_SECURE)
    }
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      activity.setRecentsScreenshotEnabled(!(hideRecents || secure))
    }
    if (was != secure) Log.i(TAG, "FLAG_SECURE ${if (secure) "on" else "off"} (lock on: $hideRecents)")
  }

  /**
   * Brulion's activity leaves the front (onPause) while the lock is on: its window turns secure at
   * once — before the system takes the recent apps' picture and before the lock screen is drawn
   * again on the way back, both earlier than C++ hears of it. C++ clears it when the app is in
   * front again and unlocked ([BrulionAndroid.setSecureWindow]).
   */
  fun paused(activity: Activity?) {
    if (!hideRecents || secure) return
    secure = true
    apply(activity)
  }

  /**
   * The phone has a screen lock (and so possibly biometrics) the prompt can ask for. Android 10+
   * only: Android 9's prompt has no screen lock to fall back on, and a phone without an enrolled
   * finger would lock the app for good.
   */
  fun deviceSecure(context: Context): Boolean {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return false
    val keyguard = context.getSystemService(KeyguardManager::class.java) ?: return false
    return keyguard.isDeviceSecure
  }

  /**
   * Shows the prompt over [activity]; [done] gets (ok, error) on the main thread — error "" when
   * the user cancelled. False when it could not be shown.
   */
  fun authenticate(activity: Activity, title: String, subtitle: String, cancelText: String, done: (Boolean, String) -> Unit): Boolean {
    if (!deviceSecure(activity)) return false
    val builder = BiometricPrompt.Builder(activity).setTitle(title)
    if (subtitle.isNotEmpty()) builder.setSubtitle(subtitle)
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
      builder.setAllowedAuthenticators(
        BiometricManager.Authenticators.BIOMETRIC_WEAK or BiometricManager.Authenticators.DEVICE_CREDENTIAL
      )
    } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
      // Android 10: the screen lock as the alternative (no cancel button may come with it).
      @Suppress("DEPRECATION")
      builder.setDeviceCredentialAllowed(true)
    } else {
      // Android 9 (never offered: deviceSecure()): biometrics with a cancel button.
      builder.setNegativeButton(cancelText, activity.mainExecutor) { _, _ -> done(false, "") }
    }
    val prompt =
      try {
        builder.build()
      } catch (e: IllegalArgumentException) {
        Log.w(TAG, "cannot build the prompt", e)
        return false
      }
    cancel?.cancel()
    val signal = CancellationSignal()
    cancel = signal
    val callback =
      object : BiometricPrompt.AuthenticationCallback() {
        override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult?) {
          cancel = null
          done(true, "")
        }

        override fun onAuthenticationError(errorCode: Int, errString: CharSequence?) {
          cancel = null
          val cancelled =
            errorCode == BiometricPrompt.BIOMETRIC_ERROR_USER_CANCELED || errorCode == BiometricPrompt.BIOMETRIC_ERROR_CANCELED
          done(false, if (cancelled) "" else (errString?.toString() ?: ""))
        }
        // (onAuthenticationFailed: one wrong finger — the prompt itself says so and goes on.)
      }
    return try {
      prompt.authenticate(signal, activity.mainExecutor, callback)
      true
    } catch (e: RuntimeException) {
      Log.w(TAG, "cannot show the prompt", e)
      cancel = null
      false
    }
  }

  /** Copies [text] as sensitive (no preview, no suggestion of it on Android 13+). */
  fun copySensitive(context: Context, text: String): Boolean {
    val clipboard = context.getSystemService(ClipboardManager::class.java) ?: return false
    val clip = ClipData.newPlainText("", text)
    val extras = PersistableBundle()
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      extras.putBoolean(ClipDescription.EXTRA_IS_SENSITIVE, true)
    } else {
      // The extra's name before it was public (Android 12 launchers and keyboards read it too).
      extras.putBoolean("android.content.extra.IS_SENSITIVE", true)
    }
    clip.description.extras = extras
    return try {
      clipboard.setPrimaryClip(clip)
      true
    } catch (e: RuntimeException) {
      Log.w(TAG, "cannot copy", e)
      false
    }
  }
}
