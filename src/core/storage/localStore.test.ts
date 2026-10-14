import { describe, expect, it, vi } from 'vitest';
import { openCachedDb } from './idb';
import { LOCAL_DB, LocalStore } from './localStore';

let n = 0;
const ns = (): string => `test-${Date.now()}-${n++}`;

type Factory = (namespace: string) => Promise<LocalStore>;

const backends: [string, Factory][] = [
  ['memory', async (namespace) => LocalStore.memory(namespace)],
  ['IndexedDB', (namespace) => LocalStore.open(namespace)],
];

for (const [name, open] of backends) {
  describe(`LocalStore (${name})`, () => {
    it('gets, sets and deletes values', async () => {
      const s = await open(ns());
      expect(await s.get('missing')).toBeUndefined();
      await s.set('a', { x: 1, tags: ['zażółć'] });
      expect(await s.get<{ x: number; tags: string[] }>('a')).toEqual({ x: 1, tags: ['zażółć'] });
      await s.set('a', 2);
      expect(await s.get('a')).toBe(2);
      await s.delete('a');
      expect(await s.get('a')).toBeUndefined();
      await s.delete('never-existed');
    });

    it('stores structured-cloneable values (Uint8Array, Map, Date, nested)', async () => {
      const s = await open(ns());
      const value = { bytes: new Uint8Array([1, 2]), map: new Map([['k', 'v']]), when: new Date(5), list: [null, true, 1.5] };
      await s.set('v', value);
      expect(await s.get('v')).toEqual(value);
    });

    it('returns clones: mutating a read value does not change the store', async () => {
      const s = await open(ns());
      const original = { list: [1] };
      await s.set('k', original);
      original.list.push(2);
      const read = await s.get<{ list: number[] }>('k');
      expect(read?.list).toEqual([1]);
      read!.list.push(3);
      expect((await s.get<{ list: number[] }>('k'))?.list).toEqual([1]);
    });

    it('setting undefined deletes the key', async () => {
      const s = await open(ns());
      await s.set('k', 1);
      await s.set('k', undefined);
      expect(await s.keys()).toEqual([]);
    });

    it('lists keys and entries by prefix, sorted', async () => {
      const s = await open(ns());
      await s.setMany([
        ['sync/b', 2],
        ['sync/a', 1],
        ['history/x', 'h'],
        ['synchro', 'not sync/'],
        ['sync/ż', 3],
      ]);
      expect(await s.keys('sync/')).toEqual(['sync/a', 'sync/b', 'sync/ż']);
      expect(await s.entries<number>('sync/')).toEqual([
        ['sync/a', 1],
        ['sync/b', 2],
        ['sync/ż', 3],
      ]);
      expect(await s.keys()).toEqual(['history/x', 'sync/a', 'sync/b', 'sync/ż', 'synchro']);
      expect(await s.entries('nothing/')).toEqual([]);
    });

    it('handles prefixes ending in the highest code unit', async () => {
      const s = await open(ns());
      await s.set('a\uFFFF1', 1);
      await s.set('a\uFFFF', 0);
      await s.set('b', 2);
      expect(await s.keys('a\uFFFF')).toEqual(['a\uFFFF', 'a\uFFFF1']);
    });

    it('getMany / deleteMany', async () => {
      const s = await open(ns());
      await s.setMany([
        ['a', 1],
        ['b', 2],
      ]);
      expect(await s.getMany(['a', 'zz', 'b'])).toEqual([1, undefined, 2]);
      await s.deleteMany(['a', 'b', 'zz']);
      expect(await s.keys()).toEqual([]);
      await s.deleteMany([]);
      await s.setMany([]);
    });

    it('update is an atomic read-modify-write', async () => {
      const s = await open(ns());
      const results = await Promise.all(Array.from({ length: 20 }, () => s.update<number>('counter', (old) => (old ?? 0) + 1)));
      expect(await s.get('counter')).toBe(20);
      expect(results.sort((a, b) => a! - b!)[19]).toBe(20);
      expect(await s.update('counter', () => undefined)).toBeUndefined();
      expect(await s.get('counter')).toBeUndefined();
    });

    it('clear() only removes this namespace', async () => {
      const name = ns();
      const a = await open(name);
      await a.set('x', 1);
      if (open === backends[1]![1]) {
        const b = await open(`${name}:inner`);
        const c = await open(name.slice(0, -1));
        await b.set('x', 2);
        await c.set('x', 3);
        await a.clear();
        expect(await a.keys()).toEqual([]);
        expect(await b.get('x')).toBe(2);
        expect(await c.get('x')).toBe(3);
      } else {
        await a.clear();
        expect(await a.keys()).toEqual([]);
      }
    });

    it('exposes its namespace', async () => {
      const name = ns();
      expect((await open(name)).namespace).toBe(name);
    });
  });
}

describe('LocalStore namespaces (IndexedDB)', () => {
  it('two stores with the same namespace share data; different namespaces do not', async () => {
    const name = ns();
    const a = await LocalStore.open(name);
    const b = await LocalStore.open(name);
    const other = await LocalStore.open(`${name}-other`);
    await a.set('k', 'v');
    expect(await b.get('k')).toBe('v');
    expect(await other.get('k')).toBeUndefined();
    expect(await other.keys()).toEqual([]);
  });

  it('a namespace that contains a colon does not leak into its prefix namespace', async () => {
    const name = ns();
    const parent = await LocalStore.open(name);
    const child = await LocalStore.open(`${name}:vault1`);
    await child.set('a', 1);
    expect(await parent.keys()).toEqual([]);
  });

  it('memory stores are independent', async () => {
    const a = LocalStore.memory('x');
    const b = LocalStore.memory('x');
    await a.set('k', 1);
    expect(await b.get('k')).toBeUndefined();
  });

  it('openOrMemory returns a working store', async () => {
    const s = await LocalStore.openOrMemory(ns());
    await s.set('k', 1);
    expect(await s.get('k')).toBe(1);
  });
});

describe('LocalStore.setMany atomicity', () => {
  it('IndexedDB: values and deletions are written in one transaction', async () => {
    const s = await LocalStore.open(ns());
    await s.set('stary klucz', 'x');
    const spy = vi.spyOn(IDBDatabase.prototype, 'transaction');
    try {
      await s.setMany([
        ['nowy klucz', 'żółć'],
        ['stary klucz', undefined],
      ]);
      expect(spy.mock.calls.filter((c) => c[1] === 'readwrite')).toHaveLength(1);
    } finally {
      spy.mockRestore();
    }
    expect(await s.get('stary klucz')).toBeUndefined();
    expect(await s.get('nowy klucz')).toBe('żółć');
  });

  it('IndexedDB: a failing value aborts the deletions of the same call', async () => {
    const s = await LocalStore.open(ns());
    await s.set('zostaje', 1);
    await expect(
      s.setMany([
        ['zostaje', undefined],
        ['zły', () => 'functions cannot be cloned'],
      ]),
    ).rejects.toBeTruthy();
    expect(await s.get('zostaje')).toBe(1);
  });

  it('memory: mixed setMany writes and deletes', async () => {
    const s = LocalStore.memory(ns());
    await s.setMany([
      ['a', 1],
      ['b', 2],
    ]);
    await s.setMany([
      ['a', undefined],
      ['c', 3],
    ]);
    expect(await s.entries()).toEqual([
      ['b', 2],
      ['c', 3],
    ]);
  });
});

describe('LocalStore after the database connection is lost', () => {
  it('reconnects when the browser closes the connection (e.g. Safari after backgrounding)', async () => {
    const store = await LocalStore.open(ns());
    await store.set('zażółć', { n: 1 });
    await LocalStore.closeDatabase();
    expect(await store.get('zażółć')).toEqual({ n: 1 });
    await store.set('gęś', 2);
    expect(await store.keys()).toEqual(['gęś', 'zażółć']);
    await LocalStore.closeDatabase();
    expect(await store.update<number>('gęś', (v) => (v ?? 0) + 1)).toBe(3);
    expect(await store.entries()).toEqual([
      ['gęś', 3],
      ['zażółć', { n: 1 }],
    ]);
  });
});

describe('LocalStore when the browser drops the connection without notice', () => {
  it('reopens the database and retries the operation once', async () => {
    const store = await LocalStore.open(ns());
    await store.set('ćma', 'nocna');
    // The cached connection is closed behind the store's back (e.g. Safari "connection lost").
    (await openCachedDb(LOCAL_DB, 1, () => {})).close();
    expect(await store.get('ćma')).toBe('nocna');
    (await openCachedDb(LOCAL_DB, 1, () => {})).close();
    await store.setMany([
      ['ćma', undefined],
      ['ważka', 'dzienna'],
    ]);
    expect(await store.entries()).toEqual([['ważka', 'dzienna']]);
  });
});
