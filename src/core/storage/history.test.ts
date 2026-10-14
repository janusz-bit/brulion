import { describe, expect, it, vi } from 'vitest';
import { History } from './history';
import { LocalStore } from './localStore';
import { makeClock } from './adapterContract.testutil';

const MIN = 60_000;
const DAY = 24 * 60 * MIN;

function setup(opts: ConstructorParameters<typeof History>[1] = {}) {
  const clock = makeClock(Date.UTC(2026, 9, 14, 9, 0));
  const store = LocalStore.memory('history');
  const history = new History(store, { now: clock.now, ...opts });
  return { clock, store, history };
}

describe('History.snapshot / list / get', () => {
  it('stores snapshots and lists them newest first without content', async () => {
    const { clock, history } = setup();
    await history.snapshot('Notatka.md', 'pierwsza wersja', 'user');
    clock.advance(6 * MIN);
    await history.snapshot('Notatka.md', 'druga wersja — zażółć', 'sync');
    const list = await history.list('Notatka.md');
    expect(list).toHaveLength(2);
    expect(list[0]).toMatchObject({ reason: 'sync', size: new TextEncoder().encode('druga wersja — zażółć').byteLength });
    expect(list[1]).toMatchObject({ reason: 'user', size: 15 });
    expect(list[0]!.time).toBeGreaterThan(list[1]!.time);
    expect(Object.keys(list[0]!).sort()).toEqual(['id', 'reason', 'size', 'time']);
    expect(await history.get(list[0]!.id)).toBe('druga wersja — zażółć');
    expect(await history.get(list[1]!.id)).toBe('pierwsza wersja');
  });

  it('returns an empty list / undefined for unknown paths and ids', async () => {
    const { history } = setup();
    expect(await history.list('nic.md')).toEqual([]);
    expect(await history.get('nope')).toBeUndefined();
  });

  it('normalises paths', async () => {
    const { history } = setup();
    await history.snapshot('./a//b.md', 'x', 'user');
    expect(await history.list('a/b.md')).toHaveLength(1);
  });

  it('dedupes identical consecutive content', async () => {
    const { clock, history } = setup();
    await history.snapshot('a.md', 'same', 'user');
    clock.advance(10 * MIN);
    await history.snapshot('a.md', 'same', 'user');
    await history.snapshot('a.md', 'same', 'sync');
    expect(await history.list('a.md')).toHaveLength(1);
  });

  it('throttles ordinary snapshots to one per 5 minutes per path', async () => {
    const { clock, history } = setup();
    await history.snapshot('a.md', 'v1', 'user');
    clock.advance(1 * MIN);
    await history.snapshot('a.md', 'v2', 'user');
    clock.advance(3 * MIN);
    await history.snapshot('a.md', 'v3', 'system');
    await history.snapshot('b.md', 'other path is independent', 'user');
    clock.advance(1 * MIN + 1);
    await history.snapshot('a.md', 'v4', 'user');
    const list = await history.list('a.md');
    expect(list.map((e) => e.reason)).toEqual(['user', 'user']);
    expect(await history.get(list[0]!.id)).toBe('v4');
    expect(await history.list('b.md')).toHaveLength(1);
  });

  it('always keeps sync, import and external snapshots, and the next user snapshot after them', async () => {
    const { clock, history } = setup();
    await history.snapshot('a.md', 'v1', 'user');
    clock.advance(1000);
    await history.snapshot('a.md', 'v2', 'sync');
    clock.advance(1000);
    await history.snapshot('a.md', 'v3', 'user');
    clock.advance(1000);
    await history.snapshot('a.md', 'v4', 'external');
    clock.advance(1000);
    await history.snapshot('a.md', 'v5', 'import');
    clock.advance(1000);
    await history.snapshot('a.md', 'v6', 'user');
    clock.advance(1000);
    await history.snapshot('a.md', 'v7', 'user');
    expect((await history.list('a.md')).map((e) => e.reason)).toEqual(['user', 'import', 'external', 'user', 'sync', 'user']);
  });

  it('serialises concurrent snapshots of the same path', async () => {
    const { clock, history } = setup({ throttleMs: 0 });
    await Promise.all(
      Array.from({ length: 10 }, (_, i) => {
        clock.advance(1);
        return history.snapshot('a.md', `wersja ${i}`, 'user');
      }),
    );
    expect(await history.list('a.md')).toHaveLength(10);
  });

  it('caps the number of snapshots per path on write', async () => {
    const { clock, history, store } = setup({ throttleMs: 0, maxPerPath: 7, minKeep: 2 });
    for (let i = 0; i < 12; i++) {
      clock.advance(MIN);
      await history.snapshot('a.md', `v${i}`, 'user');
    }
    const list = await history.list('a.md');
    expect(list).toHaveLength(7);
    expect(await history.get(list[0]!.id)).toBe('v11');
    expect(await history.get(list[6]!.id)).toBe('v5');
    expect(await store.keys('hist/c/')).toHaveLength(7);
  });

  it('recovers from a corrupted index', async () => {
    const { store, history } = setup();
    await store.set('hist/idx/a.md', 'garbage');
    await store.set('hist/idx/b.md', [{ id: 1 }, null, { id: 'x', time: 1, size: 1, reason: 'user', hash: 'h' }]);
    expect(await history.list('a.md')).toEqual([]);
    expect(await history.list('b.md')).toEqual([{ id: 'x', time: 1, size: 1, reason: 'user' }]);
    await history.snapshot('a.md', 'fresh', 'user');
    expect(await history.list('a.md')).toHaveLength(1);
  });
});

describe('History.prune', () => {
  it('drops snapshots older than 90 days but keeps the 5 newest', async () => {
    const { clock, history, store } = setup({ throttleMs: 0 });
    for (let i = 0; i < 8; i++) {
      await history.snapshot('old.md', `stara ${i}`, 'user');
      clock.advance(DAY);
    }
    await history.snapshot('fresh.md', 'nowa', 'user');
    clock.advance(200 * DAY);
    await history.prune();
    expect(await history.list('old.md')).toHaveLength(5);
    expect(await history.get((await history.list('old.md'))[0]!.id)).toBe('stara 7');
    expect(await history.list('fresh.md')).toHaveLength(1);
    expect(await store.keys('hist/c/')).toHaveLength(6);
  });

  it('keeps at most 50 per path', async () => {
    const { clock, history, store } = setup({ throttleMs: 0, maxPerPath: 1000 });
    for (let i = 0; i < 60; i++) {
      clock.advance(MIN);
      await history.snapshot('a.md', `v${i}`, 'user');
    }
    expect(await history.list('a.md')).toHaveLength(60);
    const pruned = new History(store, { now: clock.now });
    await pruned.prune();
    const list = await pruned.list('a.md');
    expect(list).toHaveLength(50);
    expect(await pruned.get(list[49]!.id)).toBe('v10');
  });

  it('removes orphaned content but leaves recent orphans (in-flight snapshots)', async () => {
    const { clock, store, history } = setup();
    const oldId = `${(clock.now() - 60 * MIN).toString(36)}-0000abcd`;
    const newId = `${clock.now().toString(36)}-0000efgh`;
    await store.set(`hist/c/${oldId}`, 'orphan');
    await store.set(`hist/c/${newId}`, 'in flight');
    await store.set('hist/c/not-a-time-id', 'junk');
    await history.snapshot('a.md', 'kept', 'user');
    await history.prune();
    const keys = await store.keys('hist/c/');
    expect(keys).toContain(`hist/c/${newId}`);
    expect(keys).not.toContain(`hist/c/${oldId}`);
    expect(keys).not.toContain('hist/c/not-a-time-id');
    expect(keys).toHaveLength(2);
  });

  it('is a no-op on an empty store', async () => {
    const { history } = setup();
    await history.prune();
    expect(await history.paths()).toEqual([]);
  });
});

describe('History.rename / remove / paths', () => {
  it('moves history with a renamed note and merges with existing history', async () => {
    const { clock, history } = setup();
    await history.snapshot('stara.md', 'a', 'user');
    clock.advance(MIN);
    await history.snapshot('nowa.md', 'b', 'user');
    clock.advance(MIN);
    await history.rename('stara.md', 'nowa.md');
    expect(await history.list('stara.md')).toEqual([]);
    const list = await history.list('nowa.md');
    expect(list).toHaveLength(2);
    expect(await history.get(list[0]!.id)).toBe('b');
    expect(await history.paths()).toEqual(['nowa.md']);
    await history.rename('nic.md', 'x.md');
    await history.rename('nowa.md', 'nowa.md');
    expect(await history.paths()).toEqual(['nowa.md']);
  });

  it('remove deletes index and contents', async () => {
    const { history, store } = setup();
    await history.snapshot('ź.md', 'treść', 'user');
    await history.remove('ź.md');
    expect(await history.list('ź.md')).toEqual([]);
    expect(await store.keys()).toEqual([]);
  });

  it('works on the IndexedDB-backed store', async () => {
    const store = await LocalStore.open(`history-idb-${Date.now()}`);
    const clock = makeClock();
    const history = new History(store, { now: clock.now });
    await history.snapshot('Dziennik/2026-10-14.md', '- [ ] zadanie', 'external');
    const fresh = new History(store, { now: clock.now });
    const list = await fresh.list('Dziennik/2026-10-14.md');
    expect(list).toHaveLength(1);
    expect(await fresh.get(list[0]!.id)).toBe('- [ ] zadanie');
  });

  it('propagates store failures from snapshot', async () => {
    const { store, history } = setup();
    vi.spyOn(store, 'set').mockRejectedValueOnce(new Error('quota'));
    await expect(history.snapshot('a.md', 'x', 'user')).rejects.toThrow('quota');
    await history.snapshot('a.md', 'x', 'user');
    expect(await history.list('a.md')).toHaveLength(1);
  });
});

describe('History shared between instances (two tabs) and concurrent maintenance', () => {
  it('prune keeps the contents of history moved by a concurrent rename', async () => {
    const { clock, history } = setup();
    await history.snapshot('stara.md', 'treść sprzed godziny', 'user');
    clock.advance(60 * MIN);
    await Promise.all([history.prune(), history.rename('stara.md', 'nowa.md')]);
    const list = await history.list('nowa.md');
    expect(list).toHaveLength(1);
    expect(await history.get(list[0]!.id)).toBe('treść sprzed godziny');
  });

  it('list() sees snapshots taken by another instance on the same store', async () => {
    const { clock, store, history } = setup();
    const otherTab = new History(store, { now: clock.now });
    expect(await history.list('a.md')).toEqual([]);
    await otherTab.snapshot('a.md', 'z drugiej karty', 'user');
    expect(await history.list('a.md')).toHaveLength(1);
  });

  it('rename() keeps snapshots another instance added after this one last looked', async () => {
    const { clock, store, history } = setup();
    const otherTab = new History(store, { now: clock.now });
    await history.snapshot('a.md', 'v1', 'user');
    clock.advance(10 * MIN);
    await otherTab.snapshot('a.md', 'v2', 'sync');
    await history.rename('a.md', 'b.md');
    const list = await otherTab.list('b.md');
    expect(await Promise.all(list.map((e) => history.get(e.id)))).toEqual(['v2', 'v1']);
  });

  it('dedupe compares with the newest snapshot in the store, not a stale view', async () => {
    const { clock, store, history } = setup();
    const otherTab = new History(store, { now: clock.now });
    await history.snapshot('a.md', 'A', 'user');
    clock.advance(10 * MIN);
    await otherTab.snapshot('a.md', 'B', 'user');
    clock.advance(10 * MIN);
    await history.snapshot('a.md', 'A', 'user');
    expect((await history.list('a.md')).length).toBe(3);
  });
});

describe('History when the device clock is set back', () => {
  it('keeps taking snapshots and lists them in the order they were taken', async () => {
    const { clock, history } = setup();
    await history.snapshot('Dziennik/środa.md', 'wersja 1', 'user');
    clock.advance(-DAY); // clock corrected (NTP, manual change)
    await history.snapshot('Dziennik/środa.md', 'wersja 2', 'user');
    clock.advance(10 * MIN);
    await history.snapshot('Dziennik/środa.md', 'wersja 3', 'user');
    const list = await history.list('Dziennik/środa.md');
    expect(await Promise.all(list.map((e) => history.get(e.id)))).toEqual(['wersja 3', 'wersja 2', 'wersja 1']);
  });

  it('prune does not drop the newest snapshots because of their (earlier) timestamps', async () => {
    const { clock, history } = setup({ minKeep: 2 });
    for (let i = 1; i <= 3; i++) {
      await history.snapshot('a.md', `stara ${i}`, 'sync');
      clock.advance(MIN);
    }
    clock.advance(-200 * DAY); // e.g. a device that booted without a real-time clock
    await history.snapshot('a.md', 'nowa', 'sync');
    clock.advance(200 * DAY);
    await history.prune();
    const list = await history.list('a.md');
    expect(await history.get(list[0]!.id)).toBe('nowa');
    expect(list).toHaveLength(4);
  });
});
