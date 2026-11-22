// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.annotation.SuppressLint
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import io.github.januszbit.brulion.R

/**
 * An AI agent on another computer at work for this device (docs/ANDROID.md "An AI agent on another
 * computer"; C++: `platform::setAgentWork()` from AppController, docs/UI.md "AI agents (ACP)"): a
 * foreground service of the app's own process — not a Qt service, Qt runs here already — that keeps
 * the app going while the screen is off or another app is in front. The process keeps a foreground
 * service's priority (Android does not end it, and its network works in Doze), Qt's event loop runs
 * on in the background (the activity's `android.app.background_running`, the manifest), and a
 * partial wake lock keeps the CPU awake while the agent works — released while every turn waits on
 * the user's answer to a question, when nothing comes from that computer.
 *
 * Its notification is quiet and ongoing: what runs ("Claude Code is answering…", or the question
 * waiting), the computer and the conversation, and **Stop**, which ends those turns
 * ([AgentStopReceiver] → `BrulionAndroid.nativeAgentStop`). A tap brings Brulion to the front.
 *
 * Type dataSync (Android 14+: the app fetches what the agent writes from that computer). Android
 * 15+ allows that type 6 hours a day: [onTimeout] ends it then, and the turns go on there (the
 * computer keeps them for the app).
 *
 * C++ calls [update] (through `BrulionAndroid.setAgentWork`, any thread) with what the notification
 * says, or with nothing when the last turn ended; the service stops a moment later, so a turn that
 * follows at once (a message that waited) keeps it.
 */
class AgentService : Service() {
  private var wakeLock: PowerManager.WakeLock? = null
  private val renew = Runnable { renewWakeLock() }

  override fun onBind(intent: Intent?): IBinder? = null

  override fun onCreate() {
    super.onCreate()
    instance = this
  }

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    // A start through startForegroundService() must go to the foreground at once, even when it is
    // not needed any more (Android ends the app otherwise).
    if (!goForeground()) {
      stopNow()
      return START_NOT_STICKY
    }
    apply()
    return START_NOT_STICKY
  }

  /** Android 15+: dataSync's 6 hours of the day are used up. */
  override fun onTimeout(startId: Int, fgsType: Int) {
    Log.i(TAG, "the foreground service's time is up: the agent's turns go on on that computer")
    timedOut = true
    stopNow()
  }

  override fun onDestroy() {
    handler.removeCallbacks(renew)
    wakeLock?.let { if (it.isHeld) it.release() }
    wakeLock = null
    if (instance === this) instance = null
    started = false
    super.onDestroy()
  }

  private fun goForeground(): Boolean {
    val work = current ?: Work("", "", "", true)
    return try {
      val notification = notification(this, work)
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
      } else {
        startForeground(NOTIFICATION_ID, notification)
      }
      true
    } catch (e: RuntimeException) {
      // (Android 12+ refuses one started from the background; 14+ one without its type.)
      Log.w(TAG, "the agent's foreground service did not start", e)
      false
    }
  }

  /** The notification and the wake lock as [current] says; nothing: the service ends. */
  @SuppressLint("MissingPermission")
  private fun apply() {
    val work = current
    if (work == null) {
      stopNow()
      return
    }
    try {
      NotificationManagerCompat.from(this).notify(NOTIFICATION_ID, notification(this, work))
    } catch (e: RuntimeException) {
      Log.w(TAG, "the agent's notification was not updated", e)
    }
    if (work.waiting) {
      handler.removeCallbacks(renew)
      wakeLock?.let { if (it.isHeld) it.release() }
    } else {
      renewWakeLock()
    }
  }

  /** Held a while at a time, renewed while the agent works (never forever, should a stop be missed). */
  private fun renewWakeLock() {
    val lock =
      wakeLock
        ?: (getSystemService(Context.POWER_SERVICE) as? PowerManager)
          ?.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "Brulion:agent")
          ?.also {
            it.setReferenceCounted(false)
            wakeLock = it
          }
        ?: return
    lock.acquire(WAKE_LOCK_MS)
    handler.removeCallbacks(renew)
    handler.postDelayed(renew, WAKE_RENEW_MS)
  }

  private fun stopNow() {
    handler.removeCallbacks(renew)
    wakeLock?.let { if (it.isHeld) it.release() }
    stopForeground(STOP_FOREGROUND_REMOVE)
    stopSelf()
  }

  /** What the notification says. */
  internal data class Work(val title: String, val text: String, val stop: String, val waiting: Boolean)

  /** What [update] does. */
  internal enum class Step { START, APPLY, STOP_LATER, NOTHING }

  companion object {
    private const val TAG = "BrulionAgent"
    const val CHANNEL_AGENTS = "agents"
    /** Negative: no reminder's or timer's id (those are 1 … 2^31 − 1). */
    const val NOTIFICATION_ID = -0x4220
    internal const val ACTION_STOP = "io.github.januszbit.brulion.action.AGENT_STOP"
    /** A turn that follows the last one this soon keeps the service (and its notification). */
    private const val STOP_DELAY_MS = 3000L
    private const val WAKE_LOCK_MS = 15L * 60 * 1000
    private const val WAKE_RENEW_MS = 10L * 60 * 1000

    // (Lazy: the JVM tests of next() have no main looper.)
    private val handler by lazy { Handler(Looper.getMainLooper()) }

    /** The running service (main thread). */
    private var instance: AgentService? = null
    /** startForegroundService() was called, the service not destroyed since (main thread). */
    private var started = false
    /** Android ended it for its time (Android 15+): not started again until the next turn begins. */
    private var timedOut = false
    /** What C++ asked for last; null: nothing runs (main thread). */
    private var current: Work? = null
    private val stopLater = Runnable { instance?.apply() }

    /**
     * C++ ([BrulionAndroid.setAgentWork], any thread): an agent works — [title], [text] and the
     * button [stop] of its notification, [waiting] while every turn waits on the user's answer —
     * or, with an empty [title], nothing runs any more.
     */
    fun update(context: Context, title: String, text: String, stop: String, waiting: Boolean) {
      val app = context.applicationContext
      handler.post {
        handler.removeCallbacks(stopLater)
        val work = if (title.isEmpty()) null else Work(title, text, stop, waiting)
        val step = next(work != null, current != null, instance != null, started, timedOut)
        current = work
        if (work == null) timedOut = false
        when (step) {
          Step.STOP_LATER -> handler.postDelayed(stopLater, STOP_DELAY_MS)
          Step.APPLY -> instance?.apply()
          Step.START -> start(app)
          Step.NOTHING -> Unit
        }
      }
    }

    /**
     * What [update] does (pure): no work any more — the service stops a moment later (a turn that
     * follows keeps it); work — the running service shows it, one on its way shows it when it
     * starts, else it starts; but not again for the same turns after Android ended it for its time
     * ([timedOut], Android 15+: until those turns end).
     */
    internal fun next(working: Boolean, wasWorking: Boolean, running: Boolean, starting: Boolean, timedOut: Boolean): Step =
      when {
        !working -> Step.STOP_LATER
        running -> Step.APPLY
        starting -> Step.NOTHING // (onStartCommand applies it)
        timedOut && wasWorking -> Step.NOTHING
        else -> Step.START
      }

    /**
     * The notification's Stop ([AgentStopReceiver], main thread): C++ ends the turns (and says so
     * with an update without a title); the notification goes at once.
     */
    internal fun stopRequested() {
      BrulionAndroid.agentStopRequested()
      handler.removeCallbacks(stopLater)
      current = null
      instance?.stopNow()
    }

    private fun start(context: Context) {
      ensureChannel(context)
      try {
        ContextCompat.startForegroundService(context, Intent(context, AgentService::class.java))
        started = true
        timedOut = false
      } catch (e: RuntimeException) {
        // (Android 12+: not from the background — the turn goes on on that computer, which keeps it.)
        Log.w(TAG, "the agent's foreground service could not be started", e)
      }
    }

    /** The quiet channel of the agents' notification (no sound, no vibration, no heads-up). */
    fun ensureChannel(context: Context) {
      val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as? NotificationManager ?: return
      val channel =
        NotificationChannel(
          CHANNEL_AGENTS,
          context.getString(R.string.brulion_channel_agents),
          NotificationManager.IMPORTANCE_LOW
        )
      channel.description = context.getString(R.string.brulion_channel_agents_description)
      channel.setShowBadge(false)
      manager.createNotificationChannel(channel)
    }

    internal fun notification(context: Context, work: Work): android.app.Notification {
      ensureChannel(context)
      val builder =
        NotificationCompat.Builder(context, CHANNEL_AGENTS)
          .setSmallIcon(R.drawable.ic_stat_brulion)
          .setContentTitle(work.title.ifEmpty { context.getString(R.string.brulion_agent_working) })
          .setOngoing(true)
          .setOnlyAlertOnce(true)
          .setSilent(true)
          .setShowWhen(false)
          .setCategory(NotificationCompat.CATEGORY_PROGRESS)
          .setPriority(NotificationCompat.PRIORITY_LOW)
          .setVisibility(NotificationCompat.VISIBILITY_PRIVATE)
          // (Android 12+: shown after some seconds — a short turn shows none —, its Stop button
          // notwithstanding, which would show it at once.)
          .setForegroundServiceBehavior(NotificationCompat.FOREGROUND_SERVICE_DEFERRED)
      if (work.text.isNotEmpty()) builder.setContentText(work.text)
      if (!work.waiting) builder.setProgress(0, 0, true)
      context.packageManager.getLaunchIntentForPackage(context.packageName)?.let { launch ->
        launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP)
        builder.setContentIntent(
          PendingIntent.getActivity(context, NOTIFICATION_ID, launch, PendingIntent.FLAG_IMMUTABLE)
        )
      }
      if (work.stop.isNotEmpty()) {
        val stop = Intent(context, AgentStopReceiver::class.java).setAction(ACTION_STOP)
        builder.addAction(
          0,
          work.stop,
          PendingIntent.getBroadcast(
            context,
            NOTIFICATION_ID,
            stop,
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
          )
        )
      }
      return builder.build()
    }
  }
}

/** Stop in the notification of [AgentService] (a broadcast: allowed whatever the app's state). */
class AgentStopReceiver : BroadcastReceiver() {
  override fun onReceive(context: Context, intent: Intent) {
    if (intent.action == AgentService.ACTION_STOP) AgentService.stopRequested()
  }
}
