import { describe, expect, it } from 'vitest';
import { classifyTransportError, codeForStatus, errorMessage, isAbortError, isSyncError, isWebDavError, SyncError, WebDavError } from './errors';
import { runPool } from './pool';

const tick = (ms = 1) => new Promise((r) => setTimeout(r, ms));

describe('runPool', () => {
  it('processes every item with bounded concurrency', async () => {
    let active = 0;
    let peak = 0;
    const done: number[] = [];
    await runPool(
      [1, 2, 3, 4, 5, 6, 7],
      async (n) => {
        active++;
        peak = Math.max(peak, active);
        await tick();
        active--;
        done.push(n);
      },
      { concurrency: 3 },
    );
    expect(done.sort()).toEqual([1, 2, 3, 4, 5, 6, 7]);
    expect(peak).toBe(3);
  });

  it('accepts items pushed by workers', async () => {
    const seen: string[] = [];
    await runPool(
      ['a'],
      async (s, push) => {
        seen.push(s);
        if (s.length < 3) {
          push(`${s}x`);
          push(`${s}y`);
        }
      },
      { concurrency: 2 },
    );
    expect(seen.sort()).toEqual(['a', 'ax', 'axx', 'axy', 'ay', 'ayx', 'ayy']);
  });

  it('resolves for an empty input', async () => {
    await expect(runPool([], async () => {}, { concurrency: 4 })).resolves.toBeUndefined();
  });

  it('rejects with the first error after running workers settle', async () => {
    const finished: number[] = [];
    const p = runPool(
      [1, 2, 3, 4],
      async (n) => {
        await tick(n);
        if (n === 1) throw new Error('boom');
        finished.push(n);
      },
      { concurrency: 2 },
    );
    await expect(p).rejects.toThrow('boom');
    expect(finished).toEqual([2]);
  });

  it('turns synchronous worker throws into a rejection', async () => {
    await expect(
      runPool(
        [1],
        () => {
          throw new Error('sync');
        },
        { concurrency: 1 },
      ),
    ).rejects.toThrow('sync');
  });

  it('stops taking items after an abort', async () => {
    const ctrl = new AbortController();
    const seen: number[] = [];
    await runPool(
      [1, 2, 3, 4],
      async (n) => {
        seen.push(n);
        if (n === 2) ctrl.abort();
        await tick();
      },
      { concurrency: 1, signal: ctrl.signal },
    );
    expect(seen).toEqual([1, 2]);
  });
});

describe('errors', () => {
  it('codeForStatus', () => {
    expect([401, 403, 404, 405, 409, 410, 412, 413, 423, 507, 500, 502, 400].map(codeForStatus)).toEqual([
      'auth',
      'forbidden',
      'not-found',
      'not-allowed',
      'conflict',
      'not-found',
      'precondition',
      'too-large',
      'locked',
      'insufficient-storage',
      'server',
      'server',
      'http',
    ]);
  });

  it('classifyTransportError', () => {
    expect(classifyTransportError(new TypeError('Failed to fetch')).code).toBe('cors');
    expect(classifyTransportError(Object.assign(new TypeError('fetch failed'), { cause: { code: 'ENOTFOUND' } })).code).toBe('network');
    expect(classifyTransportError('error sending request').code).toBe('network');
    const own = new WebDavError('auth', 401, 'x');
    expect(classifyTransportError(own)).toBe(own);
  });

  it('classifyTransportError reports network when the browser says offline', () => {
    const nav = Object.getOwnPropertyDescriptor(globalThis, 'navigator');
    Object.defineProperty(globalThis, 'navigator', { value: { onLine: false }, configurable: true });
    try {
      expect(classifyTransportError(new TypeError('Failed to fetch')).code).toBe('network');
    } finally {
      if (nav) Object.defineProperty(globalThis, 'navigator', nav);
      else delete (globalThis as { navigator?: unknown }).navigator;
    }
  });

  it('type guards and messages', () => {
    const w = new WebDavError('precondition', 412, 'PUT failed');
    expect(isWebDavError(w)).toBe(true);
    expect(isWebDavError(w, 'precondition')).toBe(true);
    expect(isWebDavError(w, 'auth')).toBe(false);
    expect(isWebDavError(new Error('x'))).toBe(false);
    const s = new SyncError('remote-empty', 'pusto');
    expect(isSyncError(s, 'remote-empty')).toBe(true);
    expect(isSyncError(w)).toBe(false);
    expect(isAbortError(new DOMException('x', 'AbortError'))).toBe(true);
    expect(isAbortError(new Error('x'))).toBe(false);
    expect(errorMessage(new Error('zła odpowiedź'))).toBe('zła odpowiedź');
    expect(errorMessage('tekst')).toBe('tekst');
  });
});
