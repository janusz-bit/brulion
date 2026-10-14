import { afterEach, describe, expect, it, vi } from 'vitest';
import type { ScheduledNotification } from '@/core/types';
import { ActivationHub, emitInAppNotification, inAppListenerCount, onInAppNotification, parseActivation } from './notifications-common';

const item: ScheduledNotification = {
  id: 'dzienniki/2026-10-14.md#3@2026-10-14T09:00',
  nativeId: 12345,
  title: 'Zadzwonić do mamy',
  body: 'Kupić żółty ser',
  at: '2026-10-14T09:00',
  alarm: false,
  path: 'dzienniki/2026-10-14.md',
  line: 3,
};

describe('in-app notification listeners', () => {
  const unsubs: (() => void)[] = [];
  afterEach(() => {
    for (const u of unsubs.splice(0)) u();
  });

  it('calls every listener with the item and info', () => {
    const a = vi.fn();
    const b = vi.fn();
    unsubs.push(onInAppNotification(a), onInAppNotification(b));
    emitInAppNotification(item, { systemShown: false });
    expect(a).toHaveBeenCalledWith(item, { systemShown: false });
    expect(b).toHaveBeenCalledTimes(1);
  });

  it('isolates failing listeners', () => {
    const spy = vi.spyOn(console, 'error').mockImplementation(() => {});
    const ok = vi.fn();
    unsubs.push(
      onInAppNotification(() => {
        throw new Error('boom');
      }),
      onInAppNotification(ok),
    );
    emitInAppNotification(item, { systemShown: true });
    expect(ok).toHaveBeenCalled();
    expect(spy).toHaveBeenCalled();
    spy.mockRestore();
  });

  it('unsubscribes', () => {
    const before = inAppListenerCount();
    const cb = vi.fn();
    const off = onInAppNotification(cb);
    expect(inAppListenerCount()).toBe(before + 1);
    off();
    off();
    expect(inAppListenerCount()).toBe(before);
    emitInAppNotification(item, { systemShown: true });
    expect(cb).not.toHaveBeenCalled();
  });
});

describe('parseActivation', () => {
  it('accepts path + line (numbers and numeric strings)', () => {
    expect(parseActivation({ path: 'Książki/Pan Tadeusz.md', line: 12 })).toEqual({ path: 'Książki/Pan Tadeusz.md', line: 12 });
    expect(parseActivation({ path: 'a.md', line: '7' })).toEqual({ path: 'a.md', line: 7 });
    expect(parseActivation({ path: 'a.md', line: 3.9, extra: 1 })).toEqual({ path: 'a.md', line: 3 });
  });

  it('defaults invalid lines to 0', () => {
    expect(parseActivation({ path: 'a.md' })).toEqual({ path: 'a.md', line: 0 });
    expect(parseActivation({ path: 'a.md', line: -1 })).toEqual({ path: 'a.md', line: 0 });
    expect(parseActivation({ path: 'a.md', line: 'x' })).toEqual({ path: 'a.md', line: 0 });
    expect(parseActivation({ path: 'a.md', line: Number.NaN })).toEqual({ path: 'a.md', line: 0 });
  });

  it('rejects payloads without a path', () => {
    expect(parseActivation(null)).toBeNull();
    expect(parseActivation({})).toBeNull();
    expect(parseActivation({ path: '' })).toBeNull();
    expect(parseActivation({ path: 5 })).toBeNull();
    expect(parseActivation('a.md')).toBeNull();
  });
});

describe('ActivationHub', () => {
  it('fans out to subscribers', () => {
    const hub = new ActivationHub();
    const a = vi.fn();
    const b = vi.fn();
    hub.subscribe(a);
    const offB = hub.subscribe(b);
    hub.emit({ path: 'Notatki/Żółw.md', line: 2 });
    expect(a).toHaveBeenCalledWith({ path: 'Notatki/Żółw.md', line: 2 });
    expect(b).toHaveBeenCalledTimes(1);
    offB();
    hub.emit({ path: 'x.md', line: 0 });
    expect(a).toHaveBeenCalledTimes(2);
    expect(b).toHaveBeenCalledTimes(1);
    expect(hub.size).toBe(1);
  });

  it('ignores malformed activations', () => {
    const hub = new ActivationHub();
    const cb = vi.fn();
    hub.subscribe(cb);
    hub.emit({});
    hub.emit(null);
    expect(cb).not.toHaveBeenCalled();
  });

  it('keeps an activation that arrived before anyone subscribed (cold start)', async () => {
    const hub = new ActivationHub();
    hub.emit({ path: 'stary.md', line: 1 });
    hub.emit({ path: 'nowy.md', line: 4 });
    const cb = vi.fn();
    hub.subscribe(cb);
    expect(cb).not.toHaveBeenCalled();
    await Promise.resolve();
    expect(cb).toHaveBeenCalledExactlyOnceWith({ path: 'nowy.md', line: 4 });
    const later = vi.fn();
    hub.subscribe(later);
    await Promise.resolve();
    expect(later).not.toHaveBeenCalled();
  });

  it('keeps the pending activation when the subscriber leaves before delivery', async () => {
    const hub = new ActivationHub();
    hub.emit({ path: 'a.md', line: 1 });
    const off = hub.subscribe(vi.fn());
    off();
    await Promise.resolve();
    const next = vi.fn();
    hub.subscribe(next);
    await Promise.resolve();
    expect(next).toHaveBeenCalledWith({ path: 'a.md', line: 1 });
  });

  it('isolates failing subscribers', () => {
    const spy = vi.spyOn(console, 'error').mockImplementation(() => {});
    const hub = new ActivationHub();
    const ok = vi.fn();
    hub.subscribe(() => {
      throw new Error('x');
    });
    hub.subscribe(ok);
    hub.emit({ path: 'a.md', line: 0 });
    expect(ok).toHaveBeenCalled();
    spy.mockRestore();
  });
});
