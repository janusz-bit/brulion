import { describe, expect, it, vi } from 'vitest';
import type { VaultChange } from '@/core/storage';
import type { SyncRunOptions } from './engine';
import { SyncError, WebDavError } from './errors';
import { SyncScheduler, type SchedulerEventTarget, type SchedulerTimers } from './scheduler';
import type { SyncReport, SyncStatus } from './types';

/** Manual clock + timers. */
class FakeTimers implements SchedulerTimers {
  t = 1_000_000;
  private seq = 0;
  private readonly pending = new Map<number, { at: number; fn: () => void }>();

  setTimeout(fn: () => void, ms: number): unknown {
    const id = ++this.seq;
    this.pending.set(id, { at: this.t + ms, fn });
    return id;
  }

  clearTimeout(handle: unknown): void {
    this.pending.delete(handle as number);
  }

  now(): number {
    return this.t;
  }

  get count(): number {
    return this.pending.size;
  }

  /** Advance time, firing due timers in order (and letting promises settle). */
  async advance(ms: number): Promise<void> {
    const end = this.t + ms;
    for (;;) {
      const next = [...this.pending.entries()].filter(([, v]) => v.at <= end).sort((a, b) => a[1].at - b[1].at)[0];
      if (!next) break;
      this.pending.delete(next[0]);
      this.t = next[1].at;
      next[1].fn();
      await flush();
    }
    this.t = end;
    await flush();
  }
}

async function flush(): Promise<void> {
  for (let i = 0; i < 10; i++) await Promise.resolve();
}

class FakeEvents implements SchedulerEventTarget {
  readonly listeners = new Map<string, Set<() => void>>();
  addEventListener(type: string, l: () => void): void {
    if (!this.listeners.has(type)) this.listeners.set(type, new Set());
    this.listeners.get(type)!.add(l);
  }
  removeEventListener(type: string, l: () => void): void {
    this.listeners.get(type)?.delete(l);
  }
  dispatch(type: string): void {
    for (const l of this.listeners.get(type) ?? []) l();
  }
  size(): number {
    return [...this.listeners.values()].reduce((n, s) => n + s.size, 0);
  }
}

function report(over: Partial<SyncReport> = {}): SyncReport {
  return { uploaded: 0, downloaded: 0, deletedLocal: 0, deletedRemote: 0, merged: 0, conflicts: [], conflictCopies: [], errors: [], skipped: [], changes: [], startedAt: 0, finishedAt: 0, ...over };
}

/** Engine double: each sync() waits for `release()` unless `auto` is set. */
class FakeEngine {
  calls: SyncRunOptions[] = [];
  auto = true;
  result: () => Promise<SyncReport> = async () => report({ finishedAt: this.timers.now() });
  private releases: (() => void)[] = [];
  constructor(private readonly timers: FakeTimers) {}

  sync(opts?: SyncRunOptions): Promise<SyncReport> {
    this.calls.push(opts ?? {});
    if (this.auto) return this.result();
    return new Promise<SyncReport>((resolve, reject) => {
      const onAbort = () => reject(new DOMException('Aborted', 'AbortError'));
      opts?.signal?.addEventListener('abort', onAbort);
      this.releases.push(() => {
        opts?.signal?.removeEventListener('abort', onAbort);
        this.result().then(resolve, reject);
      });
    });
  }

  release(): void {
    this.releases.shift()?.();
  }
}

function setup(opts: { online?: boolean; intervalMs?: number; debounceMs?: number; visible?: boolean } = {}) {
  const timers = new FakeTimers();
  const events = new FakeEvents();
  const engine = new FakeEngine(timers);
  const net = { online: opts.online ?? true, visible: opts.visible ?? true };
  const scheduler = new SyncScheduler({
    engine,
    timers,
    events,
    intervalMs: opts.intervalMs ?? 300_000,
    debounceMs: opts.debounceMs ?? 20_000,
    isOnline: () => net.online,
    isVisible: () => net.visible,
  });
  const statuses: SyncStatus[] = [];
  scheduler.subscribe((s) => statuses.push(s));
  return { timers, events, engine, net, scheduler, statuses };
}

describe('SyncScheduler', () => {
  it('starts disabled and syncs right after start()', async () => {
    const { scheduler, engine, timers, statuses } = setup();
    expect(scheduler.status.state).toBe('disabled');
    expect(statuses[0]?.state).toBe('disabled');
    scheduler.start();
    expect(scheduler.status.state).toBe('idle');
    await timers.advance(0);
    expect(engine.calls).toHaveLength(1);
    expect(statuses.map((s) => s.state)).toContain('syncing');
    expect(scheduler.status).toMatchObject({ state: 'idle', lastSync: timers.now() });
    expect(scheduler.status.lastReport).toBeDefined();
    scheduler.stop();
  });

  it('syncs periodically', async () => {
    const { scheduler, engine, timers } = setup({ intervalMs: 60_000 });
    scheduler.start();
    await timers.advance(0);
    expect(scheduler.status.nextSync).toBe(timers.now() + 60_000);
    await timers.advance(59_999);
    expect(engine.calls).toHaveLength(1);
    await timers.advance(1);
    expect(engine.calls).toHaveLength(2);
    await timers.advance(120_000);
    expect(engine.calls).toHaveLength(4);
    scheduler.stop();
  });

  it('debounces local changes', async () => {
    const { scheduler, engine, timers } = setup({ debounceMs: 20_000 });
    scheduler.start();
    await timers.advance(0);
    scheduler.notifyLocalChange();
    await timers.advance(10_000);
    scheduler.notifyLocalChange();
    await timers.advance(19_999);
    expect(engine.calls).toHaveLength(1);
    await timers.advance(1);
    expect(engine.calls).toHaveLength(2);
    scheduler.stop();
  });

  it('notifyChanges ignores the engine’s own writes', async () => {
    const { scheduler, engine, timers } = setup({ debounceMs: 1000 });
    scheduler.start();
    await timers.advance(0);
    const own: VaultChange[] = [{ type: 'modify', path: 'a.md', source: 'sync' }];
    scheduler.notifyChanges(own);
    await timers.advance(5000);
    expect(engine.calls).toHaveLength(1);
    scheduler.notifyChanges([...own, { type: 'create', path: 'b.md', source: 'user' }]);
    await timers.advance(1000);
    expect(engine.calls).toHaveLength(2);
    scheduler.stop();
  });

  it('a change during a running sync triggers another one afterwards', async () => {
    const { scheduler, engine, timers } = setup({ debounceMs: 1000 });
    engine.auto = false;
    scheduler.start();
    await timers.advance(0);
    expect(scheduler.status.state).toBe('syncing');
    scheduler.notifyLocalChange();
    engine.release();
    await flush();
    expect(scheduler.status.state).toBe('idle');
    engine.auto = true;
    await timers.advance(1000);
    expect(engine.calls).toHaveLength(2);
    scheduler.stop();
  });

  it('syncNow runs immediately, joins a running sync and works when not started', async () => {
    const { scheduler, engine } = setup();
    const r = await scheduler.syncNow();
    expect(r).not.toBeNull();
    expect(engine.calls).toHaveLength(1);
    expect(scheduler.status.state).toBe('disabled');
    expect(scheduler.status.lastSync).toBeDefined();
    engine.auto = false;
    const p1 = scheduler.syncNow();
    const p2 = scheduler.syncNow();
    expect(p2).toBe(p1);
    engine.release();
    await p1;
    expect(engine.calls).toHaveLength(2);
  });

  it('does not attempt anything while offline and syncs when back online', async () => {
    const { scheduler, engine, timers, events, net } = setup({ online: false });
    scheduler.start();
    expect(scheduler.status.state).toBe('offline');
    await timers.advance(0);
    await timers.advance(3_600_000);
    expect(engine.calls).toHaveLength(0);
    expect(await scheduler.syncNow()).toBeNull();
    scheduler.notifyLocalChange();
    await timers.advance(60_000);
    expect(engine.calls).toHaveLength(0);
    expect(scheduler.status.state).toBe('offline');
    net.online = true;
    events.dispatch('online');
    await flush();
    expect(engine.calls).toHaveLength(1);
    expect(scheduler.status.state).toBe('idle');
    scheduler.stop();
  });

  it('goes offline on the offline event while idle', async () => {
    const { scheduler, timers, events, net } = setup();
    scheduler.start();
    await timers.advance(0);
    net.online = false;
    events.dispatch('offline');
    expect(scheduler.status.state).toBe('offline');
    scheduler.stop();
  });

  it('syncs when the app becomes visible if the last sync is old enough', async () => {
    const { scheduler, engine, timers, events, net } = setup({ intervalMs: 3_600_000 });
    scheduler.start();
    await timers.advance(0);
    net.visible = true;
    events.dispatch('visibilitychange');
    await flush();
    expect(engine.calls).toHaveLength(1);
    await timers.advance(61_000);
    net.visible = false;
    events.dispatch('visibilitychange');
    await flush();
    expect(engine.calls).toHaveLength(1);
    net.visible = true;
    events.dispatch('visibilitychange');
    await flush();
    expect(engine.calls).toHaveLength(2);
    scheduler.stop();
  });

  it('flushes a pending debounced sync when the app goes to the background', async () => {
    const { scheduler, engine, timers, events, net } = setup({ intervalMs: 3_600_000, debounceMs: 20_000 });
    scheduler.start();
    await timers.advance(0);
    expect(engine.calls).toHaveLength(1);
    scheduler.notifyLocalChange();
    await timers.advance(5_000);
    expect(engine.calls).toHaveLength(1);
    // Android suspends a backgrounded app: the edit must not wait for the next app start.
    net.visible = false;
    events.dispatch('visibilitychange');
    await flush();
    expect(engine.calls).toHaveLength(2);
    // Without pending changes going to the background does nothing.
    net.visible = true;
    events.dispatch('visibilitychange');
    await flush();
    net.visible = false;
    events.dispatch('visibilitychange');
    await flush();
    expect(engine.calls).toHaveLength(2);
    scheduler.stop();
  });

  it('reports errors and retries with exponential backoff', async () => {
    const { scheduler, engine, timers } = setup({ intervalMs: 300_000 });
    engine.result = async () => {
      throw new WebDavError('server', 503, 'PROPFIND failed: HTTP 503');
    };
    scheduler.start();
    await timers.advance(0);
    expect(scheduler.status).toMatchObject({ state: 'error', lastError: 'PROPFIND failed: HTTP 503', lastErrorCode: 'server' });
    await timers.advance(30_000);
    expect(engine.calls).toHaveLength(2);
    await timers.advance(59_999);
    expect(engine.calls).toHaveLength(2);
    await timers.advance(1);
    expect(engine.calls).toHaveLength(3);
    engine.result = async () => report({ finishedAt: timers.now() });
    await timers.advance(120_000);
    expect(engine.calls).toHaveLength(4);
    expect(scheduler.status.state).toBe('idle');
    expect(scheduler.status.lastError).toBeUndefined();
    scheduler.stop();
  });

  it('survives an engine that throws synchronously', async () => {
    const engine = {
      sync: (): Promise<SyncReport> => {
        throw new Error('nie skonfigurowano');
      },
    };
    const s = new SyncScheduler({ engine, events: null, isOnline: () => true });
    expect(await s.syncNow()).toBeNull();
    expect(s.status).toMatchObject({ state: 'error', lastError: 'nie skonfigurowano' });
  });

  it('keeps sync error codes (e.g. remote-empty) for the UI', async () => {
    const { scheduler, engine } = setup();
    engine.result = async () => {
      throw new SyncError('remote-empty', 'pusty');
    };
    expect(await scheduler.syncNow()).toBeNull();
    expect(scheduler.status).toMatchObject({ state: 'error', lastErrorCode: 'remote-empty' });
  });

  it('a network failure while the device went offline shows offline', async () => {
    const { scheduler, engine, net } = setup();
    engine.result = async () => {
      net.online = false;
      throw new WebDavError('network', 0, 'offline');
    };
    await scheduler.syncNow();
    expect(scheduler.status.state).toBe('offline');
  });

  it('per-file errors put the status into error with a summary', async () => {
    const { scheduler, engine } = setup();
    engine.result = async () =>
      report({
        errors: [
          { path: 'zły.md', message: 'HTTP 500' },
          { path: 'b.md', message: 'x' },
        ],
      });
    await scheduler.syncNow();
    expect(scheduler.status).toMatchObject({ state: 'error', lastError: 'zły.md: HTTP 500 (+1 more)', lastErrorCode: 'files' });
  });

  it('stop() aborts a running sync, removes listeners and timers', async () => {
    const { scheduler, engine, timers, events } = setup();
    engine.auto = false;
    scheduler.start();
    expect(events.size()).toBe(3);
    await timers.advance(0);
    const signal = engine.calls[0]?.signal;
    expect(signal?.aborted).toBe(false);
    scheduler.stop();
    expect(signal?.aborted).toBe(true);
    await flush();
    expect(scheduler.status.state).toBe('disabled');
    expect(events.size()).toBe(0);
    expect(timers.count).toBe(0);
    scheduler.notifyLocalChange();
    expect(timers.count).toBe(0);
  });

  it('a restart while the stopped run is still aborting syncs right after it settles', async () => {
    const { scheduler, engine, timers } = setup();
    engine.auto = false;
    scheduler.start();
    await timers.advance(0);
    expect(engine.calls).toHaveLength(1);
    scheduler.stop();
    scheduler.start();
    engine.auto = true;
    await timers.advance(0);
    expect(engine.calls).toHaveLength(2);
    expect(engine.calls[1]?.signal?.aborted).toBe(false);
    expect(scheduler.status.state).toBe('idle');
    scheduler.stop();
  });

  it('start() and stop() are idempotent', async () => {
    const { scheduler, events, timers, engine } = setup();
    scheduler.start();
    scheduler.start();
    expect(events.size()).toBe(3);
    await timers.advance(0);
    expect(engine.calls).toHaveLength(1);
    scheduler.stop();
    scheduler.stop();
    expect(scheduler.active).toBe(false);
  });

  it('passes progress into the status', async () => {
    const { scheduler, engine, statuses } = setup();
    engine.result = async () => {
      engine.calls.at(-1)?.onProgress?.({ phase: 'syncing', done: 1, total: 3, path: 'a.md' });
      return report();
    };
    await scheduler.syncNow();
    expect(statuses.some((s) => s.progress?.path === 'a.md')).toBe(true);
    expect(scheduler.status.progress).toBeUndefined();
  });

  it('subscribe returns an unsubscribe function and survives throwing listeners', async () => {
    const { scheduler } = setup();
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
    const seen: string[] = [];
    const off = scheduler.subscribe((s) => seen.push(s.state));
    scheduler.subscribe(() => {
      throw new Error('ups');
    });
    off();
    await scheduler.syncNow();
    expect(seen).toEqual(['disabled']);
    warn.mockRestore();
  });

  it('works without injected timers/events (globals are guarded)', async () => {
    vi.useFakeTimers();
    try {
      const engine = { sync: vi.fn(async () => report()) };
      const s = new SyncScheduler({ engine, intervalMs: 60_000 });
      s.start();
      await vi.advanceTimersByTimeAsync(0);
      expect(engine.sync).toHaveBeenCalledTimes(1);
      await vi.advanceTimersByTimeAsync(60_000);
      expect(engine.sync).toHaveBeenCalledTimes(2);
      s.stop();
    } finally {
      vi.useRealTimers();
    }
  });
});
