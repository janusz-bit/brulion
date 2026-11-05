// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.app.job.JobParameters
import android.app.job.JobService
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.Message
import android.os.Messenger
import android.os.RemoteException
import android.util.Log

/**
 * The periodic job of the background check ([BackgroundCheck]), in the app's main process: binds
 * [BackgroundCheckService] (its own process, where Brulion's C++ reads the notebook), waits for its
 * answer and arms the reminders it found. Skipped while Brulion is on the screen — the app keeps its
 * reminders up to date itself then.
 */
class BackgroundCheckJob : JobService() {
  private val main = Handler(Looper.getMainLooper())
  private var run: Run? = null

  override fun onStartJob(params: JobParameters): Boolean {
    if (BackgroundCheck.target(this) == null) {
      Log.i(BackgroundCheck.TAG, "background check: nothing to check")
      return false
    }
    if (BrulionAndroid.isUiShown()) {
      Log.i(BackgroundCheck.TAG, "background check skipped: Brulion is on the screen")
      return false
    }
    val next = Run(params, BrulionAndroid.attachCount())
    val bound =
      try {
        bindService(Intent(this, BackgroundCheckService::class.java), next, Context.BIND_AUTO_CREATE)
      } catch (e: SecurityException) {
        Log.w(BackgroundCheck.TAG, "cannot start the background check", e)
        false
      }
    if (!bound) {
      unbindQuietly(next)
      return false
    }
    run = next
    main.postDelayed(next.timeout, TIMEOUT_MS)
    return true
  }

  override fun onStopJob(params: JobParameters): Boolean {
    // Android ends the job (its time is up, constraints changed): try again later.
    Log.i(BackgroundCheck.TAG, "background check stopped by the system")
    run?.end(finishJob = false)
    return true
  }

  private fun unbindQuietly(connection: ServiceConnection) {
    try {
      unbindService(connection)
    } catch (e: IllegalArgumentException) {
      // never bound
    }
  }

  /** One check: the connection to the service, its answer, the time limit. */
  private inner class Run(private val params: JobParameters, private val attachedBefore: Int) : ServiceConnection {
    private var done = false

    private val replies =
      Messenger(
        Handler(Looper.getMainLooper()) { message ->
          if (message.what == BackgroundCheckService.MSG_RESULT) {
            onResult(message.data?.getString(BackgroundCheckService.KEY_JSON))
          }
          true
        }
      )

    val timeout = Runnable {
      Log.w(BackgroundCheck.TAG, "background check: no answer in time")
      onResult(null)
    }

    override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
      val hello = Message.obtain(null, BackgroundCheckService.MSG_HELLO).apply { replyTo = replies }
      try {
        Messenger(service).send(hello)
      } catch (e: RemoteException) {
        Log.w(BackgroundCheck.TAG, "background check: the service went away", e)
        onResult(null)
      }
    }

    override fun onServiceDisconnected(name: ComponentName?) {
      // Its process died (a crash, or killed for memory) before it answered.
      if (!done) {
        Log.w(BackgroundCheck.TAG, "background check: its process ended without an answer")
        onResult(null)
      }
    }

    fun onResult(json: String?) {
      if (done) return
      // The app's own set wins when it came to the front or started meanwhile.
      val tookOver = BrulionAndroid.isUiShown() || BrulionAndroid.attachCount() != attachedBefore
      BackgroundCheck.finish(this@BackgroundCheckJob, json, tookOver)
      end(finishJob = true)
    }

    /** Unbinding ends the service and with it the check's process. */
    fun end(finishJob: Boolean) {
      if (done) return
      done = true
      main.removeCallbacks(timeout)
      unbindQuietly(this)
      if (run === this) run = null
      if (finishJob) jobFinished(params, false)
    }
  }

  private companion object {
    /** The C++ side gives up after 4 minutes; Android allows a job about 10. */
    const val TIMEOUT_MS = 5L * 60 * 1000
  }
}
