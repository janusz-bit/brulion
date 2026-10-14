/**
 * Desktop shell controls of the Tauri build (docs/ARCHITECTURE.md, "Tauri IPC contract"):
 *
 * - Closing the main window hides it to the tray (`set_close_to_tray`, default on).
 * - Quitting (tray "Zakończ", or closing the window when it is not hidden to the tray) hides
 *   the window and emits `brulion://quit-requested`. The frontend flushes pending writes and
 *   answers with `quit_app`; without an answer Rust exits by itself after 3 s.
 *
 * The UI registers its flush (e.g. "save the open editor") with
 * {@link TauriDesktopShell.onQuitRequested}; the listener is installed on first use.
 */
import { loadTauriEvent, tauriInvoke } from './tauri-ipc';

/** Event emitted by Rust when the app is about to quit (no payload). */
export const QUIT_REQUESTED_EVENT = 'brulion://quit-requested';

/** Work to finish before the app exits (e.g. pending note writes). */
export type QuitFlush = () => void | Promise<void>;

/** Tray / window / quit controls of the desktop app (`TauriPlatform.desktop`). */
export class TauriDesktopShell {
  private readonly flushers = new Set<QuitFlush>();
  private listening: Promise<void> | null = null;
  private quitting: Promise<void> | null = null;

  /** Whether closing the main window hides it to the tray (true) or quits the app (false). */
  async setCloseToTray(enabled: boolean): Promise<void> {
    await tauriInvoke<null>('set_close_to_tray', { enabled });
  }

  /** Show, un-minimise and focus the main window (e.g. when a reminder fires while it is in the tray). */
  async showMainWindow(): Promise<void> {
    await tauriInvoke<null>('show_main_window');
  }

  /** Exit at once, without flushing (see {@link flushAndQuit}). */
  async quit(): Promise<void> {
    await tauriInvoke<null>('quit_app');
  }

  /**
   * Run `flush` before the app quits. All registered flushes run in parallel; failures are
   * logged and do not stop the exit. Returns an unsubscribe function.
   */
  onQuitRequested(flush: QuitFlush): () => void {
    this.flushers.add(flush);
    this.listening ??= this.listen();
    return () => {
      this.flushers.delete(flush);
    };
  }

  /**
   * Flush everything registered with {@link onQuitRequested}, then `quit_app`. Concurrent
   * calls (repeated quit events) share one run; a failed `quit_app` can be retried.
   */
  flushAndQuit(): Promise<void> {
    this.quitting ??= this.runQuit().finally(() => {
      this.quitting = null;
    });
    return this.quitting;
  }

  private async runQuit(): Promise<void> {
    const results = await Promise.allSettled(Array.from(this.flushers, (flush) => Promise.resolve().then(flush)));
    for (const r of results) {
      if (r.status === 'rejected') console.error('[platform] flushing before quit failed', r.reason);
    }
    await this.quit();
  }

  private async listen(): Promise<void> {
    try {
      const { listen } = await loadTauriEvent();
      await listen(QUIT_REQUESTED_EVENT, () => {
        this.flushAndQuit().catch((e: unknown) => console.warn('[platform] quit_app failed', e));
      });
    } catch (e) {
      // Rust still exits after its grace period.
      console.warn('[platform] cannot listen for quit requests', e);
      this.listening = null;
    }
  }
}
