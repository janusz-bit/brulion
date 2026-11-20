// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.Message
import android.os.Messenger
import android.os.RemoteException
import android.util.Log
import org.json.JSONException
import org.json.JSONObject
import java.time.LocalDateTime
import java.time.ZoneId
import java.util.UUID

/**
 * Automation (docs/ANDROID.md "Automation (Tasker …)"): brulion:// links and the intents of
 * Tasker, Automate, MacroDroid or `adb` — what they ask for, where it goes, the answer.
 *
 * * [requestUri] turns an intent into one brulion:// address: its data (a `brulion://` or
 *   `web+brulion:` address) or the action extra, plus every known parameter given as a string
 *   extra (Tasker's "Send Intent" takes three extras: `text`, `key` and one more — the rest goes
 *   into the data). The C++ core reads it (automation::parseUri): one parser for every way in.
 * * [needsKeyless]: opening something, a request without the key and an extension's command
 *   need the app's window ([BrulionAndroid.deliverUri]: the app checks it, a write asks the user);
 *   a write with a key runs in the background ([runInBackground]: [AutomationService], its own
 *   process, while Brulion may stay closed). Which key is valid only C++ knows.
 * * The app's target ([saveTarget]: the notebook, the policy, the language) goes with each job.
 * * The answer: [broadcastResult] (`io.github.januszbit.brulion.action.RESULT`) — what was written
 *   only for a request whose key was valid; a provisional reminder for a task with a time
 *   ([armReminder]), replaced by the app's own set at its next start (as quick add's).
 */
internal object Automation {
  const val TAG = "BrulionAutomation"

  /** The explicit intent of automation apps (an activity or a broadcast receiver). */
  const val ACTION_RUN = "io.github.januszbit.brulion.action.RUN"
  /** The answer broadcast. */
  const val ACTION_RESULT = "io.github.januszbit.brulion.action.RESULT"

  /** A whole address in an extra (instead of the intent's data). */
  const val EXTRA_URI = "uri"
  /** The action when neither the data nor [EXTRA_URI] gives one ("append", "task", …). */
  const val EXTRA_ACTION = "action"
  /** Send the answer only to this package (else to any app that listens for [ACTION_RESULT]). */
  const val EXTRA_RESULT_PACKAGE = "result_package"
  /** No toast for a failed background run (the answer broadcast says it anyway). */
  const val EXTRA_QUIET = "quiet"

  /** The parameters an extra may give (automation/plan.h knownParameters() and the common ones). */
  private val PARAMETERS = setOf(
    "page", "file", "path", "heading", "block", "line", "date", "daily", "title", "name", "folder",
    "template", "text", "content", "mode", "open", "create", "due", "scheduled", "start", "remind",
    "alarm", "every", "priority", "parse", "query", "q", "view", "section", "tab", "tag",
    "extension", "command", "args", "vault", "notebook", "key", "id", "x-source", "x-success",
    "x-error", "x-cancel"
  )

  /** Longer addresses are refused here already (the core's own limit: MaxUriLength). */
  const val MAX_URI_CHARS = 100_000

  private const val PREFS = "io.github.januszbit.brulion.android.automation"
  private const val KEY_TARGET = "target"

  // --- the address ------------------------------------------------------------------------------

  /** A brulion:// or web+brulion: address (any letter case of the scheme). */
  fun isBrulionUri(text: String?): Boolean {
    val t = text?.trim()?.lowercase() ?: return false
    return t.startsWith("brulion:") || t.startsWith("web+brulion:")
  }

  /**
   * The address [intent] asks for, or null: its data or [EXTRA_URI] (a brulion:// address), else
   * `brulion://<EXTRA_ACTION>`; string extras named like parameters are added — replacing the same
   * parameter of the address — and `Intent.EXTRA_TEXT` is `text` unless `text` is given.
   */
  fun requestUri(intent: Intent?): String? {
    if (intent == null) return null
    val extras = safely { intent.extras }
    val data = safely { intent.dataString }
    val base = when {
      isBrulionUri(data) -> data!!.trim()
      isBrulionUri(extras?.let { safely { it.getString(EXTRA_URI) } }) -> extras!!.getString(EXTRA_URI)!!.trim()
      else -> {
        val action = extras?.let { safely { it.getString(EXTRA_ACTION) } }?.trim()
        if (action.isNullOrEmpty() || !action.all { it.isLetter() || it == '-' }) return null
        "brulion://$action"
      }
    }
    if (base.length > MAX_URI_CHARS) return null
    val given = LinkedHashMap<String, String>()
    if (extras != null) {
      for (key in safely { extras.keySet() } ?: emptySet()) {
        val name = key.lowercase()
        if (name !in PARAMETERS) continue
        val value = extraString(extras, key) ?: continue
        given[name] = value
      }
      if ("text" !in given && "content" !in given) {
        safely { extras.getCharSequence(Intent.EXTRA_TEXT)?.toString() }?.let { given["text"] = it }
      }
    }
    return withParams(base, given)?.takeIf { it.length <= MAX_URI_CHARS }
  }

  /** [base] with [params] in its query, replacing parameters of the same name. */
  fun withParams(base: String, params: Map<String, String>): String? {
    if (params.isEmpty()) return base
    val question = base.indexOf('?')
    val head = if (question < 0) base else base.substring(0, question)
    val kept = ArrayList<String>()
    if (question >= 0) {
      for (pair in base.substring(question + 1).split('&')) {
        if (pair.isEmpty()) continue
        val name = Uri.decode(pair.substringBefore('=')).trim().lowercase()
        if (name in params || (name == "notebook" && "vault" in params) || (name == "vault" && "notebook" in params)) continue
        kept.add(pair)
      }
    }
    for ((name, value) in params) kept.add(Uri.encode(name) + "=" + Uri.encode(value))
    return if (kept.isEmpty()) head else head + "?" + kept.joinToString("&")
  }

  private fun extraString(extras: Bundle, key: String): String? =
    safely {
      @Suppress("DEPRECATION") // the typed getters need API 33; a malformed parcel throws
      when (val value = extras.get(key)) {
        is String -> value
        is CharSequence -> value.toString()
        is Boolean, is Int, is Long -> value.toString()
        else -> null
      }
    }

  /** The action of an address ("append", …), "" when it has none. */
  fun actionOf(uri: String): String {
    val rest = uri.substringAfter(':').trimStart('/')
    val path = rest.substringBefore('?')
    val parts = path.split('/').filter { it.isNotEmpty() }
    val first = parts.firstOrNull()?.lowercase() ?: return ""
    return if (first == "x-callback-url") parts.getOrNull(1)?.lowercase() ?: "" else first
  }

  /** The value of the parameter [name] of [uri] (decoded; "+" a space), null when absent. */
  fun paramOf(uri: String, name: String): String? {
    val question = uri.indexOf('?')
    if (question < 0) return null
    for (pair in uri.substring(question + 1).split('&')) {
      val key = Uri.decode(pair.substringBefore('=').replace('+', ' ')).trim().lowercase()
      if (key == name) return Uri.decode(pair.substringAfter('=', "").replace('+', ' '))
    }
    return null
  }

  /**
   * The request needs the app's window: it opens something (open, daily, search, view, capture
   * without text), runs an extension's command, or has no key (only the user may say "Write").
   */
  fun needsWindow(uri: String): Boolean {
    val action = actionOf(uri)
    val text = paramOf(uri, "text") ?: paramOf(uri, "content")
    val opens = action in setOf("open", "daily", "journal", "today", "search", "view") ||
      (action == "capture" && text.isNullOrBlank())
    val runs = action == "run" || action == "command"
    return opens || runs || paramOf(uri, "key").isNullOrBlank()
  }

  // --- the app's target -------------------------------------------------------------------------

  /** Keeps the app's target (C++ publishAutomationTarget(); `{}`: no notebook). */
  fun saveTarget(context: Context, json: String): String {
    val target = JSONObject(json)
    context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(KEY_TARGET, target.toString()).apply()
    return "{}"
  }

  fun target(context: Context): JSONObject =
    try {
      JSONObject(context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY_TARGET, "{}") ?: "{}")
    } catch (e: JSONException) {
      JSONObject()
    }

  // --- the background run -----------------------------------------------------------------------

  /** How long a background run may take before it is given up (the service's start included). */
  private const val TIMEOUT_MS = 40_000L

  /**
   * Runs [uri] in [AutomationService] (its own process, Qt without a window) and hands the result
   * JSON to [done] on the main thread (null: no answer — the service could not start or timed out).
   */
  fun runInBackground(context: Context, uri: String, requestId: String, done: (JSONObject?) -> Unit) {
    val app = context.applicationContext
    val job = JSONObject()
      .put("uri", uri)
      .put("id", requestId)
      .put("target", target(app))
    val main = Handler(Looper.getMainLooper())
    val connection = object : ServiceConnection {
      var finished = false
      val replies = Messenger(
        Handler(Looper.getMainLooper()) { message ->
          if (message.what == AutomationService.MSG_RESULT) {
            val json = message.data?.getString(AutomationService.KEY_JSON)
            finish(json?.let { try { JSONObject(it) } catch (e: JSONException) { null } })
          }
          true
        }
      )
      val timeout = Runnable {
        Log.w(TAG, "automation: no answer in time")
        finish(null)
      }

      override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
        val message = Message.obtain(null, AutomationService.MSG_RUN).apply {
          replyTo = replies
          data = Bundle().apply { putString(AutomationService.KEY_JSON, job.toString()) }
        }
        try {
          Messenger(service).send(message)
        } catch (e: RemoteException) {
          Log.w(TAG, "automation: the service went away", e)
          finish(null)
        }
      }

      override fun onServiceDisconnected(name: ComponentName?) {
        if (!finished) {
          Log.w(TAG, "automation: its process ended without an answer")
          finish(null)
        }
      }

      fun finish(result: JSONObject?) {
        if (finished) return
        finished = true
        main.removeCallbacks(timeout)
        try {
          app.unbindService(this)
        } catch (e: IllegalArgumentException) {
          // never bound
        }
        done(result)
      }
    }
    val bound =
      try {
        app.bindService(Intent(app, AutomationService::class.java), connection, Context.BIND_AUTO_CREATE)
      } catch (e: SecurityException) {
        Log.w(TAG, "cannot start the automation service", e)
        false
      }
    if (!bound) {
      connection.finish(null)
      return
    }
    main.postDelayed(connection.timeout, TIMEOUT_MS)
  }

  /** A request id: the caller's `id` extra or parameter, else a new one. */
  fun requestId(intent: Intent?, uri: String): String =
    safely { intent?.getStringExtra("id") }?.takeIf { it.isNotBlank() }
      ?: paramOf(uri, "id")?.takeIf { it.isNotBlank() }
      ?: UUID.randomUUID().toString()

  // --- the answer -------------------------------------------------------------------------------

  /**
   * The answer broadcast [ACTION_RESULT]: `ok`, `error`, `action`, `id` — and, only when the
   * request's key was valid (`keyed`), what was written: `path`, `line`, `kind`, `reminder`,
   * `message`. To [resultPackage] when given, else to every app listening (Tasker's "Intent
   * Received").
   */
  fun broadcastResult(context: Context, result: JSONObject, requestId: String, resultPackage: String?) {
    val intent = Intent(ACTION_RESULT)
    if (!resultPackage.isNullOrBlank()) intent.setPackage(resultPackage.trim())
    intent.putExtra("ok", result.optBoolean("ok", false))
    intent.putExtra("error", result.optString("error", ""))
    intent.putExtra("action", result.optString("action", ""))
    intent.putExtra("id", result.optString("id", "").ifEmpty { requestId })
    if (result.optBoolean("keyed", false)) {
      intent.putExtra("path", result.optString("path", ""))
      intent.putExtra("line", result.optInt("line", -1))
      intent.putExtra("kind", result.optString("kind", ""))
      intent.putExtra("reminder", result.optString("reminder", ""))
      intent.putExtra("message", result.optString("message", ""))
    }
    try {
      context.sendBroadcast(intent)
    } catch (e: RuntimeException) {
      Log.w(TAG, "cannot send the automation result", e)
    }
  }

  /**
   * After a background write: a task with a time ahead gets a provisional native reminder (the
   * app's next set replaces it, as quick add's: [QuickAdd.PROVISIONAL_KEY]), and a running app
   * reads the note again ([BrulionAndroid.widgetWrote]).
   */
  fun afterWrite(context: Context, result: JSONObject, now: LocalDateTime = LocalDateTime.now()) {
    if (!result.optBoolean("ok", false)) return
    result.optString("file").takeIf { it.isNotEmpty() }?.let { BrulionAndroid.widgetWrote(it) }
    if (result.optBoolean("remindersOn", true)) provisionalReminder(result, now)?.let { armReminder(context, it) }
  }

  /** The provisional reminder of a written task with a time ahead of [now], else null. */
  fun provisionalReminder(result: JSONObject, now: LocalDateTime, zone: ZoneId = ZoneId.systemDefault()): Reminder? {
    val at = result.optString("reminder", "")
    if (at.length < 16) return null
    val date = JournalAppend.parseDate(at.substring(0, 10)) ?: return null
    val time = JournalAppend.parseTime(at.substring(11, 16)) ?: return null
    val local = LocalDateTime.of(date, time)
    if (!local.isAfter(now)) return null
    val path = result.optString("path", "")
    val line = result.optInt("line", 0) - 1 // the result's lines count from 1
    if (path.isEmpty() || line < 0) return null
    val id = result.optString("id", "").ifEmpty { "$path:$line" }
    return Reminder(
      id = -1 - ((("automation:$id").hashCode().toLong() and 0x3fffffffL).toInt()),
      title = taskTitle(result.optString("block", "")),
      body = result.optString("name", ""),
      at = local.atZone(zone).toInstant().toEpochMilli(),
      alarm = result.optBoolean("alarm", false),
      path = path,
      line = line,
      vault = result.optString("vault", ""),
      provisional = true,
      key = QuickAdd.PROVISIONAL_KEY + "automation-" + id,
      local = "${date}T${JournalAppend.hhmm(time)}",
      task = true
    )
  }

  /** The text of a task's first line, without its marker and its tokens. */
  fun taskTitle(block: String): String {
    var line = block.substringBefore('\n').trim()
    line = line.replaceFirst(Regex("^[-*+]\\s+(\\[[ xX/>-]\\]\\s+)?"), "")
    for (token in listOf(" ⏰", " 📅", " ⏳", " 🛫", " 🔁", " ⏫", " 🔼", " 🔽", " ⏬", " 🔺")) {
      val at = line.indexOf(token)
      if (at >= 0) line = line.substring(0, at)
    }
    return line.trim()
  }

  private fun armReminder(context: Context, reminder: Reminder) {
    synchronized(AlarmScheduler.lock) {
      ReminderStore(context).upsert(reminder)
      AlarmScheduler.arm(context, reminder, AlarmScheduler.canScheduleExact(context))
    }
  }

  /** An extra read defensively: a malformed parcel from another app must not crash Brulion. */
  inline fun <T> safely(read: () -> T?): T? =
    try {
      read()
    } catch (e: RuntimeException) {
      Log.w(TAG, "malformed automation extra", e)
      null
    }
}
