/**
 * When to sync: periodically, shortly after local edits (debounced), when the device comes
 * back online, when the app becomes visible again and (with pending edits) when it goes to the
 * background. Never while offline. Exposes an observable status for the UI.
 */
import type { VaultChange } from '@/core/storage';
import { errorMessage, isAbortError, isSyncError, isWebDavError } from './errors';
import type { SyncReport, SyncStatus } from './types';
import type { SyncRunOptions } from './engine';

/** Timer functions (injectable for tests). Handles are opaque. */
export interface SchedulerTimers {
  setTimeout(fn: () => void, ms: number): unknown;
  clearTimeout(handle: unknown): void;
  now(): number;
}

/** Minimal event target for 'online' / 'offline' / 'visibilitychange' (window in browsers). */
export interface SchedulerEventTarget {
  addEventListener(type: string, listener: () => void): void;
  removeEventListener(type: string, listener: () => void): void;
}

/** What the scheduler needs from the engine. */
export interface SchedulableEngine {
  sync(opts?: SyncRunOptions): Promise<SyncReport>;
}

/** Constructor options of `SyncScheduler`. */
export interface SyncSchedulerOptions {
  engine: SchedulableEngine;
  /** Periodic sync interval (default 5 min). */
  intervalMs?: number;
  /** Delay after the last local change (default 20 s). */
  debounceMs?: number;
  /** Network state (default: `navigator.onLine`, true when unknown). */
  isOnline?: () => boolean;
  /** Whether the app is in the foreground (default: `document.visibilityState`). */
  isVisible?: () => boolean;
  /** First retry delay after a failed run; doubles up to `intervalMs` (default 30 s). */
  retryBaseMs?: number;
  /** Becoming visible triggers a sync only if the last one is older than this (default 60 s). */
  visibleSyncGapMs?: number;
  timers?: SchedulerTimers;
  /** Where to listen for online/offline/visibilitychange (default `globalThis` if it can). */
  events?: SchedulerEventTarget | null;
}

const defaultTimers: SchedulerTimers = {
  setTimeout: (fn, ms) => globalThis.setTimeout(fn, ms),
  clearTimeout: (h) => globalThis.clearTimeout(h as ReturnType<typeof globalThis.setTimeout>),
  now: () => Date.now(),
};

function defaultIsOnline(): boolean {
  return typeof globalThis.navigator === 'undefined' || globalThis.navigator.onLine !== false;
}

function defaultIsVisible(): boolean {
  const doc = (globalThis as { document?: { visibilityState?: string } }).document;
  return doc?.visibilityState === undefined || doc.visibilityState === 'visible';
}

function defaultEvents(): SchedulerEventTarget | null {
  const g = globalThis as Partial<SchedulerEventTarget>;
  return typeof g.addEventListener === 'function' && typeof g.removeEventListener === 'function' ? (g as SchedulerEventTarget) : null;
}

/** Error code of a failure, when known (WebDAV / sync error codes). */
function errorCode(e: unknown): string | undefined {
  if (isWebDavError(e) || isSyncError(e)) return e.code;
  return undefined;
}

/** One-line summary of per-file errors of a run. */
function summarizeErrors(report: SyncReport): string | undefined {
  const first = report.errors[0];
  if (!first) return undefined;
  const more = report.errors.length > 1 ? ` (+${report.errors.length - 1} more)` : '';
  return `${first.path}: ${first.message}${more}`;
}

/** Drives a SyncEngine: periodic, debounced on change, on reconnect / app focus. */
export class SyncScheduler {
  private readonly engine: SchedulableEngine;
  private readonly intervalMs: number;
  private readonly debounceMs: number;
  private readonly retryBaseMs: number;
  private readonly visibleSyncGapMs: number;
  private readonly isOnline: () => boolean;
  private readonly isVisible: () => boolean;
  private readonly timers: SchedulerTimers;
  private readonly events: SchedulerEventTarget | null;
  private readonly listeners = new Set<(s: SyncStatus) => void>();
  private current: SyncStatus = { state: 'disabled' };
  private started = false;
  private periodicTimer: unknown = null;
  private debounceTimer: unknown = null;
  private running: Promise<SyncReport | null> | null = null;
  private controller: AbortController | null = null;
  private rerun = false;
  private failures = 0;
  private lastAttempt = 0;

  private readonly onOnline = (): void => {
    if (!this.started) return;
    void this.trigger();
  };

  private readonly onOffline = (): void => {
    if (!this.started || this.running) return;
    this.setStatus({ state: 'offline' });
  };

  private readonly onVisibility = (): void => {
    if (!this.started) return;
    if (!this.isVisible()) {
      // Going to the background (Android may suspend the app): pending edits must not wait
      // for the debounce timer, which would only fire at the next app start.
      if (this.debounceTimer !== null) void this.trigger();
      return;
    }
    const last = Math.max(this.current.lastSync ?? 0, this.lastAttempt);
    if (this.timers.now() - last >= this.visibleSyncGapMs) void this.trigger();
  };

  constructor(o: SyncSchedulerOptions) {
    this.engine = o.engine;
    this.intervalMs = Math.max(1000, o.intervalMs ?? 5 * 60_000);
    this.debounceMs = Math.max(0, o.debounceMs ?? 20_000);
    this.retryBaseMs = Math.max(1000, o.retryBaseMs ?? 30_000);
    this.visibleSyncGapMs = o.visibleSyncGapMs ?? 60_000;
    this.isOnline = o.isOnline ?? defaultIsOnline;
    this.isVisible = o.isVisible ?? defaultIsVisible;
    this.timers = o.timers ?? defaultTimers;
    this.events = o.events === undefined ? defaultEvents() : o.events;
  }

  /** Current status (a copy). */
  get status(): SyncStatus {
    return { ...this.current };
  }

  /** Whether `start()` was called (and not stopped). */
  get active(): boolean {
    return this.started;
  }

  /** Start automatic syncing: a first sync right away, then periodically. Idempotent. */
  start(): void {
    if (this.started) return;
    this.started = true;
    this.events?.addEventListener('online', this.onOnline);
    this.events?.addEventListener('offline', this.onOffline);
    this.events?.addEventListener('visibilitychange', this.onVisibility);
    this.setStatus({ state: this.isOnline() ? 'idle' : 'offline' });
    this.schedulePeriodic(0);
  }

  /** Stop automatic syncing and abort a running sync (files already synced stay synced). */
  stop(): void {
    if (!this.started) return;
    this.started = false;
    this.events?.removeEventListener('online', this.onOnline);
    this.events?.removeEventListener('offline', this.onOffline);
    this.events?.removeEventListener('visibilitychange', this.onVisibility);
    this.clearTimer('periodic');
    this.clearTimer('debounce');
    this.rerun = false;
    this.controller?.abort();
    this.setStatus({ state: 'disabled', nextSync: undefined, progress: undefined });
  }

  /** A note was edited locally: sync after `debounceMs` without further edits. */
  notifyLocalChange(): void {
    if (!this.started) return;
    if (this.running) {
      this.rerun = true;
      return;
    }
    this.clearTimer('debounce');
    this.debounceTimer = this.timers.setTimeout(() => {
      this.debounceTimer = null;
      void this.trigger();
    }, this.debounceMs);
  }

  /** Feed vault changes; the engine's own writes (source 'sync') are ignored. */
  notifyChanges(changes: readonly VaultChange[]): void {
    if (changes.some((c) => c.source !== 'sync')) this.notifyLocalChange();
  }

  /**
   * Sync now (also when not started, e.g. a "sync now" button). Resolves to the report, or
   * null when offline / failed / aborted (see `status.lastError`). Joins a running sync.
   */
  syncNow(): Promise<SyncReport | null> {
    return this.trigger();
  }

  /** Subscribe to status changes; the callback is called immediately with the current status. */
  subscribe(cb: (s: SyncStatus) => void): () => void {
    this.listeners.add(cb);
    try {
      cb(this.status);
    } catch (e) {
      console.warn('[sync] status listener failed', e);
    }
    return () => {
      this.listeners.delete(cb);
    };
  }

  // -------------------------------------------------------------------------
  // Internals
  // -------------------------------------------------------------------------

  private setStatus(patch: Partial<SyncStatus>): void {
    const next: SyncStatus = { ...this.current, ...patch };
    for (const k of Object.keys(next) as (keyof SyncStatus)[]) if (next[k] === undefined) delete next[k];
    this.current = next;
    for (const cb of Array.from(this.listeners)) {
      try {
        cb({ ...next });
      } catch (e) {
        console.warn('[sync] status listener failed', e);
      }
    }
  }

  private clearTimer(which: 'periodic' | 'debounce'): void {
    const handle = which === 'periodic' ? this.periodicTimer : this.debounceTimer;
    if (handle !== null) this.timers.clearTimeout(handle);
    if (which === 'periodic') this.periodicTimer = null;
    else this.debounceTimer = null;
  }

  private schedulePeriodic(delay: number): void {
    this.clearTimer('periodic');
    if (!this.started) return;
    this.periodicTimer = this.timers.setTimeout(() => {
      this.periodicTimer = null;
      void this.trigger();
    }, delay);
    this.setStatus({ nextSync: this.timers.now() + delay });
  }

  /** Run a sync unless offline; joins a running one. */
  private trigger(): Promise<SyncReport | null> {
    if (this.running) {
      if (!this.controller?.signal.aborted) return this.running;
      // The running sync is being cancelled (stop() then start()): sync again once it settled.
      return this.running.then(() => this.trigger());
    }
    if (!this.isOnline()) {
      this.setStatus({ state: 'offline', progress: undefined });
      // Keep checking periodically (cheap, no network) in case no 'online' event comes.
      this.schedulePeriodic(this.intervalMs);
      return Promise.resolve(null);
    }
    this.clearTimer('debounce');
    this.clearTimer('periodic');
    const controller = new AbortController();
    this.controller = controller;
    this.lastAttempt = this.timers.now();
    this.setStatus({ state: 'syncing', nextSync: undefined, progress: undefined });
    let started: Promise<SyncReport>;
    try {
      started = this.engine.sync({ signal: controller.signal, onProgress: (p) => this.setStatus({ progress: p }) });
    } catch (e) {
      started = Promise.reject(e);
    }
    const run = started
      .then(
        (report) => this.succeeded(report),
        (e: unknown) => this.failed(e, controller.signal.aborted),
      )
      .finally(() => {
        if (this.controller === controller) this.controller = null;
        this.running = null;
        if (this.rerun && this.started) {
          this.rerun = false;
          this.notifyLocalChange();
        }
      });
    this.running = run;
    return run;
  }

  private idleState(): SyncStatus['state'] {
    return this.started ? 'idle' : 'disabled';
  }

  private succeeded(report: SyncReport): SyncReport {
    this.failures = 0;
    const lastError = summarizeErrors(report);
    this.setStatus({
      state: lastError ? 'error' : this.idleState(),
      lastSync: report.finishedAt,
      lastReport: report,
      lastError,
      lastErrorCode: lastError ? 'files' : undefined,
      progress: undefined,
    });
    this.schedulePeriodic(this.intervalMs);
    return report;
  }

  private failed(e: unknown, aborted: boolean): null {
    if (aborted || isAbortError(e)) {
      this.setStatus({ state: this.idleState(), progress: undefined });
      if (this.started) this.schedulePeriodic(this.intervalMs);
      return null;
    }
    this.failures++;
    this.setStatus({
      state: this.isOnline() ? 'error' : 'offline',
      lastError: errorMessage(e),
      lastErrorCode: errorCode(e),
      progress: undefined,
    });
    const backoff = Math.min(this.intervalMs, this.retryBaseMs * 2 ** (this.failures - 1));
    this.schedulePeriodic(backoff);
    return null;
  }
}
