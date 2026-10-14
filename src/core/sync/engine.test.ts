import { describe, expect, it, vi } from 'vitest';
import { CONFIG_PATH, LocalStore, MemoryFs, Vault, type VaultChange } from '@/core/storage';
import type { HttpResponse } from '../types';
import { conflictStamp } from './conflicts';
import { SyncEngine, decodeSyncText, groupByCase, isSyncablePath, isSyncTextPath, planSync, remoteVersion, SYNC_BASE_PREFIX, SYNC_STATE_PREFIX, type SyncFacts, type SyncRunOptions } from './engine';
import { isSyncError, isWebDavError } from './errors';
import { CaseInsensitiveFs } from './caseFs.testutil';
import { FakeDavServer, type FakeDavOptions } from './fakeDav.testutil';
import type { SyncProgress, SyncReport } from './types';
import { WebDavClient } from './webdav';

const START = new Date(2026, 9, 14, 10, 0, 0).getTime();
const STAMP = conflictStamp(START);
const enc = new TextEncoder();
const dec = new TextDecoder();
const FOLDER = 'Notatki';

function makeClock(start = START) {
  let t = start;
  return {
    now: () => t,
    advance(ms: number) {
      t += ms;
    },
  };
}

type Clock = ReturnType<typeof makeClock>;

interface Device {
  name: string;
  fs: MemoryFs;
  vault: Vault;
  store: LocalStore;
  engine: SyncEngine;
  changes: VaultChange[];
  sync(o?: SyncRunOptions): Promise<SyncReport>;
  read(path: string): Promise<string>;
  write(path: string, content: string): Promise<void>;
  /** Text of all files outside the trash. */
  files(): Record<string, string>;
  trash(): string[];
}

function newServer(opts: FakeDavOptions = {}, clock?: Clock): FakeDavServer {
  return new FakeDavServer({ now: clock?.now, ...opts });
}

async function device(
  server: FakeDavServer,
  name: string,
  files: Record<string, string | Uint8Array> = {},
  opts: { clock?: Clock; folder?: string; concurrency?: number; maxFileSize?: number; open?: boolean } = {},
): Promise<Device> {
  const clock = opts.clock ?? makeClock();
  const fs = new MemoryFs({ now: clock.now, files });
  const vault = new Vault(fs, { id: name, name, store: LocalStore.memory(`vault-${name}`), now: clock.now });
  if (opts.open !== false) await vault.open();
  const client = new WebDavClient({ baseUrl: server.url(opts.folder ?? FOLDER), username: server.username, password: server.password, transport: server.transport });
  const store = LocalStore.memory(`sync-${name}`);
  const engine = new SyncEngine({
    vault,
    client,
    store,
    device: name,
    now: clock.now,
    ...(opts.concurrency ? { concurrency: opts.concurrency } : {}),
    ...(opts.maxFileSize ? { maxFileSize: opts.maxFileSize } : {}),
  });
  const changes: VaultChange[] = [];
  vault.onChange((c) => changes.push(...c));
  return {
    name,
    fs,
    vault,
    store,
    engine,
    changes,
    sync: (o) => engine.sync(o),
    read: (p) => vault.read(p),
    write: (p, c) => vault.write(p, c),
    files() {
      const out: Record<string, string> = {};
      for (const [p, v] of Object.entries(fs.dump())) if (!p.startsWith('.trash/')) out[p] = typeof v === 'string' ? v : dec.decode(v);
      return out;
    },
    trash() {
      return Object.keys(fs.dump()).filter((p) => p.startsWith('.trash/'));
    },
  };
}

function counts(r: SyncReport) {
  return { up: r.uploaded, down: r.downloaded, delL: r.deletedLocal, delR: r.deletedRemote, merged: r.merged, conflicts: r.conflicts.length, errors: r.errors.length };
}

const NONE = { up: 0, down: 0, delL: 0, delR: 0, merged: 0, conflicts: 0, errors: 0 };

async function rejection(p: Promise<unknown>): Promise<unknown> {
  try {
    await p;
  } catch (e) {
    return e;
  }
  throw new Error('expected a rejection');
}

// ---------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------

describe('planSync', () => {
  const f = (local: boolean, remote: boolean, known: boolean, localChanged = false, remoteChanged = false): SyncFacts => ({ local, remote, known, localChanged, remoteChanged });

  it('without history: upload, download or reconcile', () => {
    expect(planSync(f(true, false, false, true))).toBe('upload');
    expect(planSync(f(false, true, false, false, true))).toBe('download');
    expect(planSync(f(true, true, false, true, true))).toBe('reconcile');
    expect(planSync(f(false, false, false))).toBe('forget');
  });

  it('both present with history', () => {
    expect(planSync(f(true, true, true))).toBe('noop');
    expect(planSync(f(true, true, true, true))).toBe('upload');
    expect(planSync(f(true, true, true, false, true))).toBe('download');
    expect(planSync(f(true, true, true, true, true))).toBe('reconcile');
  });

  it('deletions propagate unless the other side was modified', () => {
    expect(planSync(f(true, false, true))).toBe('deleteLocal');
    expect(planSync(f(true, false, true, true))).toBe('upload');
    expect(planSync(f(false, true, true))).toBe('deleteRemote');
    expect(planSync(f(false, true, true, false, true))).toBe('download');
    expect(planSync(f(false, false, true))).toBe('forget');
  });
});

describe('path classification', () => {
  it('isSyncTextPath by extension', () => {
    for (const p of ['a.md', 'b/C.MD', 'x.txt', 'd.json', 'e.csv', 'f.canvas', 'g.css', 'h.ics', 'i.opml', 'j.html', 'k.svg', 'l.markdown']) expect(isSyncTextPath(p)).toBe(true);
    for (const p of ['a.png', 'b.pdf', 'README', '.md', 'x.md.png']) expect(isSyncTextPath(p)).toBe(false);
  });

  it('isSyncablePath skips trash, ignored folders and temp files but keeps the config', () => {
    expect(isSyncablePath('Strona.md')).toBe(true);
    expect(isSyncablePath('Strona.sync-conflict-20261014-101010-ABCDEFG.md')).toBe(true);
    expect(isSyncablePath(CONFIG_PATH)).toBe(true);
    for (const p of ['.trash/a.md', '.obsidian/app.json', '.git/HEAD', 'logseq/config.edn', '.brulion/cache.json', '~syncthing~a.md.tmp', 'a/.brulion-tmp-1', '', 'x.md.part']) {
      expect(isSyncablePath(p)).toBe(false);
    }
  });

  it('decodeSyncText accepts UTF-8 (BOM dropped) and rejects other encodings', () => {
    expect(decodeSyncText(enc.encode('Zażółć gęślą jaźń 😀'))).toBe('Zażółć gęślą jaźń 😀');
    expect(decodeSyncText(new Uint8Array([0xef, 0xbb, 0xbf, 0x61]))).toBe('a');
    expect(decodeSyncText(new Uint8Array())).toBe('');
    expect(decodeSyncText(new Uint8Array([0x5a, 0x61, 0xbf, 0xf3]))).toBeUndefined();
    // A real U+FFFD in valid UTF-8 is text.
    expect(decodeSyncText(enc.encode('\uFFFD'))).toBe('\uFFFD');
  });

  it('groupByCase groups names differing only in case, local-only names first', () => {
    const local = new Map([['Notatka.md', 1], ['a.md', 1]]);
    const remote = new Map([['notatka.md', 1], ['a.md', 1], ['B.md', 1]]);
    expect(groupByCase(['B.md', 'Notatka.md', 'a.md', 'notatka.md'], local, remote)).toEqual([['B.md'], ['Notatka.md', 'notatka.md'], ['a.md']]);
    expect(groupByCase(['Notatka.md', 'notatka.md'], new Map([['notatka.md', 1]]), new Map([['Notatka.md', 1]]))).toEqual([['notatka.md', 'Notatka.md']]);
    expect(groupByCase(['Łódź.md', 'łódź.md'], new Map(), new Map())).toEqual([['Łódź.md', 'łódź.md']]);
    expect(groupByCase([], new Map(), new Map())).toEqual([]);
  });

  it('remoteVersion prefers the etag', () => {
    expect(remoteVersion({ etag: 'abc', mtime: 1, size: 2 })).toBe('abc');
    expect(remoteVersion({ etag: '', mtime: 1, size: 2 })).toBe('m1-s2');
  });
});

// ---------------------------------------------------------------------------
// One device
// ---------------------------------------------------------------------------

describe('SyncEngine — one device', () => {
  it('first sync creates the base folder and uploads syncable files only', async () => {
    const server = newServer();
    const png = new Uint8Array([137, 80, 78, 71, 0, 1, 2, 255]);
    const a = await device(
      server,
      'laptop',
      {
        'Strona.md': '# Strona\n',
        'journals/2026-10-14.md': '- kawa\n',
        'Zażółć gęślą jaźń.md': 'ąćęłńóśźż\n',
        'assets/zdjęcie.png': png,
        [CONFIG_PATH]: '{\n  "journalsFolder": "journals"\n}\n',
        '.brulion/cache.json': '{}',
        '.trash/stare.md': 'usunięte',
        '.obsidian/app.json': '{}',
        '~syncthing~Strona.md.tmp': 'tmp',
      },
      { folder: 'Dokumenty/Notatki' },
    );
    const r = await a.sync();
    expect(counts(r)).toEqual({ ...NONE, up: 5 });
    expect(server.files('Dokumenty/Notatki')).toEqual({
      '.brulion/config.json': '{\n  "journalsFolder": "journals"\n}\n',
      'Strona.md': '# Strona\n',
      'Zażółć gęślą jaźń.md': 'ąćęłńóśźż\n',
      'assets/zdjęcie.png': dec.decode(png),
      'journals/2026-10-14.md': '- kawa\n',
    });
    expect(server.bytes('Dokumenty/Notatki/assets/zdjęcie.png')).toEqual(png);
    expect(r.startedAt).toBe(START);
    expect(r.changes.every((c) => c.action === 'upload')).toBe(true);
    // New files are uploaded with If-None-Match: *.
    expect(server.requests('PUT').every((q) => q.headers['If-None-Match'] === '*')).toBe(true);
  });

  it('a second sync without changes transfers nothing', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'x\n', 'b.png': new Uint8Array([1, 2, 3]) });
    await a.sync();
    const before = server.log.length;
    const r = await a.sync();
    expect(counts(r)).toEqual(NONE);
    expect(server.log.slice(before).every((q) => q.method === 'PROPFIND')).toBe(true);
  });

  it('uploads local edits with If-Match', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'v1\n' });
    await a.sync();
    const etag = server.etag(`${FOLDER}/a.md`);
    await a.write('a.md', 'v2\n');
    expect(counts(await a.sync())).toEqual({ ...NONE, up: 1 });
    expect(server.text(`${FOLDER}/a.md`)).toBe('v2\n');
    expect(server.requests('PUT').at(-1)?.headers['If-Match']).toBe(`"${etag}"`);
  });

  it('downloads remote edits and new remote files (source "sync")', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'v1\n' });
    await a.sync();
    server.putFile(`${FOLDER}/a.md`, 'v2 z serwera\n');
    server.putFile(`${FOLDER}/nowy/plik ż.md`, 'nowy\n');
    a.changes.length = 0;
    expect(counts(await a.sync())).toEqual({ ...NONE, down: 2 });
    expect(await a.read('a.md')).toBe('v2 z serwera\n');
    expect(await a.read('nowy/plik ż.md')).toBe('nowy\n');
    await a.vault.idle();
    expect(a.changes.map((c) => `${c.type}:${c.path}:${c.source}`).sort()).toEqual(['create:nowy/plik ż.md:sync', 'modify:a.md:sync']);
  });

  it('an ETag change without a content change does not rewrite the file', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'v1\n' });
    await a.sync();
    server.touchEtag(`${FOLDER}/a.md`);
    a.changes.length = 0;
    expect(counts(await a.sync())).toEqual(NONE);
    await a.vault.idle();
    expect(a.changes).toEqual([]);
    // ...and the new ETag is remembered.
    const before = server.requests('GET').length;
    await a.sync();
    expect(server.requests('GET').length).toBe(before);
  });

  it('propagates deletions both ways (local deletions go to the trash)', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'a\n', 'b.md': 'b\n', 'c.md': 'c\n' });
    await a.sync();
    await a.vault.delete('a.md');
    server.remove(`${FOLDER}/b.md`);
    const r = await a.sync();
    expect(counts(r)).toEqual({ ...NONE, delL: 1, delR: 1 });
    expect(server.files(FOLDER)).toEqual({ 'c.md': 'c\n' });
    expect(a.files()).toEqual({ 'c.md': 'c\n' });
    expect(a.trash().sort()).toEqual(['.trash/a.md', '.trash/b.md']);
    expect(await a.engine.stateOf('a.md')).toBeUndefined();
    expect(await a.engine.stateOf('b.md')).toBeUndefined();
    expect(await a.store.get(`${SYNC_BASE_PREFIX}b.md`)).toBeUndefined();
  });

  it('drops the state of files deleted on both sides', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'a\n', 'b.md': 'b\n' });
    await a.sync();
    await a.vault.delete('a.md');
    server.remove(`${FOLDER}/a.md`);
    expect(counts(await a.sync())).toEqual(NONE);
    expect(await a.store.keys(SYNC_STATE_PREFIX)).toEqual([`${SYNC_STATE_PREFIX}b.md`]);
  });

  it('stores merge bases for text files only', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'tekst\n', 'b.png': new Uint8Array([1]) });
    await a.sync();
    expect(await a.store.get(`${SYNC_BASE_PREFIX}a.md`)).toBe('tekst\n');
    expect(await a.store.get(`${SYNC_BASE_PREFIX}b.png`)).toBeUndefined();
    expect(await a.engine.stateOf('a.md')).toMatchObject({ remoteEtag: server.etag(`${FOLDER}/a.md`), size: 6, syncedAt: START });
  });

  it('runs only one sync at a time', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'x' });
    const p1 = a.sync();
    const p2 = a.sync();
    expect(p2).toBe(p1);
    expect(a.engine.syncing).toBe(true);
    await p1;
    expect(a.engine.syncing).toBe(false);
    expect(a.sync()).not.toBe(p1);
  });

  it('a failing file does not abort the others and is retried next time', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'a', 'zły.md': 'z', 'c.md': 'c' });
    let fail = true;
    server.hooks.push((req, path): HttpResponse | undefined =>
      fail && req.method === 'PUT' && path.endsWith('/zły.md') ? { status: 500, headers: {}, body: new Uint8Array() } : undefined,
    );
    const r = await a.sync();
    expect(counts(r)).toEqual({ ...NONE, up: 2, errors: 1 });
    expect(r.errors[0]?.path).toBe('zły.md');
    expect(r.errors[0]?.message).toMatch(/500/);
    fail = false;
    expect(counts(await a.sync())).toEqual({ ...NONE, up: 1 });
    expect(server.text(`${FOLDER}/zły.md`)).toBe('z');
  });

  it('aborts on authentication failures instead of failing every file', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'a', 'b.md': 'b' });
    server.hooks.push((req) => (req.method === 'PUT' ? { status: 401, headers: {}, body: new Uint8Array() } : undefined));
    const err = await rejection(a.sync());
    expect(isWebDavError(err, 'auth')).toBe(true);
  });

  it('respects an AbortSignal between files and resumes later', async () => {
    const server = newServer();
    const files: Record<string, string> = {};
    for (let i = 0; i < 6; i++) files[`n${i}.md`] = `notatka ${i}\n`;
    const a = await device(server, 'laptop', files, { concurrency: 1 });
    const ctrl = new AbortController();
    const err = await rejection(
      a.sync({
        signal: ctrl.signal,
        onProgress: (p) => {
          if (p.phase === 'syncing' && p.done === 2) ctrl.abort();
        },
      }),
    );
    expect((err as Error).name).toBe('AbortError');
    expect(Object.keys(server.files(FOLDER))).toEqual(['n0.md', 'n1.md']);
    expect(a.engine.syncing).toBe(false);
    const r = await a.sync();
    expect(counts(r)).toEqual({ ...NONE, up: 4 });
  });

  it('rejects immediately with an already aborted signal', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'a' });
    const ctrl = new AbortController();
    ctrl.abort();
    expect(((await rejection(a.sync({ signal: ctrl.signal }))) as Error).name).toBe('AbortError');
    expect(server.log).toEqual([]);
  });

  it('reports progress', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'a', 'b.md': 'b' });
    const events: SyncProgress[] = [];
    await a.sync({ onProgress: (p) => events.push(p) });
    expect(events.map((e) => e.phase)).toEqual(['connecting', 'listing', 'syncing', 'syncing', 'syncing', 'done']);
    expect(events.at(-1)).toEqual({ phase: 'done', done: 2, total: 2 });
    // A throwing progress callback does not break the sync.
    const r = await a.sync({
      onProgress: () => {
        throw new Error('ups');
      },
    });
    expect(r.errors).toEqual([]);
  });

  it('skips files that are too large', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'mały.md': 'x', 'duży.bin': new Uint8Array(100) }, { maxFileSize: 50 });
    const r = await a.sync();
    expect(r.skipped).toEqual(['duży.bin']);
    expect(Object.keys(server.files(FOLDER))).toEqual(['mały.md']);
  });

  it('merges when the remote file changed between listing and upload (412)', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'jeden\ndwa\ntrzy\n' });
    await a.sync();
    await a.write('a.md', 'JEDEN\ndwa\ntrzy\n');
    let raced = false;
    server.hooks.push((req, path) => {
      if (!raced && req.method === 'PUT' && path.endsWith('/a.md')) {
        raced = true;
        server.putFile(`${FOLDER}/a.md`, 'jeden\ndwa\ntrzy\ncztery\n');
      }
      return undefined;
    });
    const r = await a.sync();
    expect(counts(r)).toEqual({ ...NONE, merged: 1 });
    expect(server.text(`${FOLDER}/a.md`)).toBe('JEDEN\ndwa\ntrzy\ncztery\n');
    expect(await a.read('a.md')).toBe('JEDEN\ndwa\ntrzy\ncztery\n');
    expect(counts(await a.sync())).toEqual(NONE);
  });

  it('restores a remote file changed while we were deleting it (412 on DELETE)', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'v1\n' });
    await a.sync();
    await a.vault.delete('a.md');
    let raced = false;
    server.hooks.push((req, path) => {
      if (!raced && req.method === 'DELETE' && path.endsWith('/a.md')) {
        raced = true;
        server.putFile(`${FOLDER}/a.md`, 'v2 z innego urządzenia\n');
      }
      return undefined;
    });
    const r = await a.sync();
    expect(counts(r)).toEqual({ ...NONE, down: 1 });
    expect(await a.read('a.md')).toBe('v2 z innego urządzenia\n');
  });

  it('skips a file edited locally while it was being downloaded', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'baza\n' });
    await a.sync();
    server.putFile(`${FOLDER}/a.md`, 'baza\nzdalnie\n');
    let edited = false;
    server.hooks.push((req, path) => {
      if (!edited && req.method === 'GET' && path.endsWith('/a.md')) {
        edited = true;
        void a.write('a.md', 'lokalnie\nbaza\n');
      }
      return undefined;
    });
    const r = await a.sync();
    expect(counts(r)).toEqual(NONE);
    expect(await a.read('a.md')).toBe('lokalnie\nbaza\n');
    // Next sync: both changed -> clean merge.
    const r2 = await a.sync();
    expect(counts(r2)).toEqual({ ...NONE, merged: 1 });
    expect(await a.read('a.md')).toBe('lokalnie\nbaza\nzdalnie\n');
    expect(server.text(`${FOLDER}/a.md`)).toBe('lokalnie\nbaza\nzdalnie\n');
  });

  it('quietly skips a local file that vanished after listing', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'a', 'b.md': 'b' });
    // Removed by another program; the vault has not noticed yet.
    a.fs.externalRemove('a.md', { silent: true });
    const r = await a.sync();
    expect(counts(r)).toEqual({ ...NONE, up: 1 });
    expect(Object.keys(server.files(FOLDER))).toEqual(['b.md']);
  });

  it('starts over (without deleting anything) when the server folder changes', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'a\n' });
    await a.sync();
    server.putFile('Inne/b.md', 'b\n');
    const other = new WebDavClient({ baseUrl: server.url('Inne'), username: server.username, password: server.password, transport: server.transport });
    const engine2 = new SyncEngine({ vault: a.vault, client: other, store: a.store, device: 'laptop', now: () => START });
    const r = await engine2.sync();
    expect(counts(r)).toEqual({ ...NONE, up: 1, down: 1 });
    expect(server.files('Inne')).toEqual({ 'a.md': 'a\n', 'b.md': 'b\n' });
    expect(await a.read('b.md')).toBe('b\n');
  });

  it('refuses to trash everything when the server folder is suddenly empty', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'a\n', 'b.md': 'b\n' });
    await a.sync();
    server.remove(FOLDER);
    const err = await rejection(a.sync());
    expect(isSyncError(err, 'remote-empty')).toBe(true);
    expect(a.files()).toEqual({ 'a.md': 'a\n', 'b.md': 'b\n' });
    // After a reset the vault is uploaded again.
    await a.engine.resetState();
    expect(counts(await a.sync())).toEqual({ ...NONE, up: 2 });
    expect(server.files(FOLDER)).toEqual({ 'a.md': 'a\n', 'b.md': 'b\n' });
  });

  it('allowEmptySide lets an intended mass deletion through', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'a\n', 'b.md': 'b\n' });
    await a.sync();
    server.remove(`${FOLDER}/a.md`);
    server.remove(`${FOLDER}/b.md`);
    expect(counts(await a.sync({ allowEmptySide: true }))).toEqual({ ...NONE, delL: 2 });
    expect(a.files()).toEqual({});
  });

  it('refuses to delete the server copy of everything when the local vault looks empty', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'a\n', 'b.md': 'b\n' });
    await a.sync();
    // Same store, but the vault was never opened (or its storage was wiped).
    const vault2 = new Vault(new MemoryFs(), { id: 'x', name: 'x', store: LocalStore.memory('x') });
    const client = new WebDavClient({ baseUrl: server.url(FOLDER), username: server.username, password: server.password, transport: server.transport });
    const engine2 = new SyncEngine({ vault: vault2, client, store: a.store, device: 'laptop' });
    expect(isSyncError(await rejection(engine2.sync()), 'local-empty')).toBe(true);
    expect(server.files(FOLDER)).toEqual({ 'a.md': 'a\n', 'b.md': 'b\n' });
  });

  it('works with servers that have no ETags (mtime + size versions)', async () => {
    const clock = makeClock();
    const server = newServer({ noEtags: true }, clock);
    const a = await device(server, 'laptop', { 'a.md': 'v1\n', 'b.md': 'b\n' }, { clock });
    expect(counts(await a.sync())).toEqual({ ...NONE, up: 2 });
    expect(counts(await a.sync())).toEqual(NONE);
    clock.advance(5000);
    server.putFile(`${FOLDER}/a.md`, 'v2 dłuższe\n');
    expect(counts(await a.sync())).toEqual({ ...NONE, down: 1 });
    expect(await a.read('a.md')).toBe('v2 dłuższe\n');
    await a.write('b.md', 'b2\n');
    expect(counts(await a.sync())).toEqual({ ...NONE, up: 1 });
    expect(counts(await a.sync())).toEqual(NONE);
  });

  it('detects changed binary files via size/mtime and hash', async () => {
    const clock = makeClock();
    const server = newServer({}, clock);
    const a = await device(server, 'laptop', { 'img.png': new Uint8Array([1, 2, 3]) }, { clock });
    await a.sync();
    const gets = server.requests('GET').length;
    // Untouched binary: not read, not transferred.
    expect(counts(await a.sync())).toEqual(NONE);
    clock.advance(2000);
    await a.vault.writeBinary('img.png', new Uint8Array([9, 9, 9]));
    expect(counts(await a.sync())).toEqual({ ...NONE, up: 1 });
    expect(server.bytes(`${FOLDER}/img.png`)).toEqual(new Uint8Array([9, 9, 9]));
    expect(server.requests('GET').length).toBe(gets);
  });

  it('syncs .brulion/config.json and applies it to the vault', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'a' });
    await a.sync();
    server.putFile(`${FOLDER}/${CONFIG_PATH}`, '{\n  "journalsFolder": "Dziennik",\n  "weekStartsOn": 0\n}\n');
    expect(counts(await a.sync())).toEqual({ ...NONE, down: 1 });
    expect(a.vault.config.journalsFolder).toBe('Dziennik');
    expect(a.vault.config.weekStartsOn).toBe(0);
  });

  it('does not touch the trash or ignored folders on the server', async () => {
    const server = newServer();
    server.putFile(`${FOLDER}/.trash/x.md`, 'x');
    server.putFile(`${FOLDER}/.git/HEAD`, 'ref');
    server.putFile(`${FOLDER}/a.md`, 'a');
    const a = await device(server, 'laptop');
    expect(counts(await a.sync())).toEqual({ ...NONE, down: 1 });
    expect(a.files()).toEqual({ 'a.md': 'a' });
    expect(server.requests('PROPFIND').some((q) => q.path.includes('.git') || q.path.includes('.trash'))).toBe(false);
  });
});

// ---------------------------------------------------------------------------
// Two devices through one server
// ---------------------------------------------------------------------------

describe('SyncEngine — two devices', () => {
  async function pair(aFiles: Record<string, string | Uint8Array> = {}, bFiles: Record<string, string | Uint8Array> = {}) {
    const clock = makeClock();
    const server = newServer({}, clock);
    const a = await device(server, 'laptop', aFiles, { clock });
    const b = await device(server, 'telefon', bFiles, { clock });
    return { server, a, b, clock };
  }

  it('propagates create, edit and delete between devices', async () => {
    const { server, a, b } = await pair({ 'Strona.md': 'v1\n', 'Zażółć.md': 'ź\n' });
    await a.sync();
    expect(counts(await b.sync())).toEqual({ ...NONE, down: 2 });
    expect(b.files()).toEqual({ 'Strona.md': 'v1\n', 'Zażółć.md': 'ź\n' });

    await b.write('Strona.md', 'v2 z telefonu\n');
    await b.write('folder/Nowa.md', 'nowa\n');
    await b.vault.delete('Zażółć.md');
    expect(counts(await b.sync())).toEqual({ ...NONE, up: 2, delR: 1 });
    expect(counts(await a.sync())).toEqual({ ...NONE, down: 2, delL: 1 });
    expect(a.files()).toEqual({ 'Strona.md': 'v2 z telefonu\n', 'folder/Nowa.md': 'nowa\n' });
    expect(a.trash()).toEqual(['.trash/Zażółć.md']);
    expect(server.files(FOLDER)).toEqual(a.files());
    expect(counts(await a.sync())).toEqual(NONE);
    expect(counts(await b.sync())).toEqual(NONE);
  });

  it('merges journal appends made on both devices without a conflict', async () => {
    const journal = 'journals/2026-10-14.md';
    const { server, a, b } = await pair({ [journal]: '- 08:00 kawa\n' });
    await a.sync();
    await b.sync();
    await a.write(journal, '- 08:00 kawa\n- 09:00 spotkanie z Łucją\n');
    await b.write(journal, '- 08:00 kawa\n- 12:30 obiad: żurek\n');
    expect(counts(await a.sync())).toEqual({ ...NONE, up: 1 });
    const rb = await b.sync();
    expect(counts(rb)).toEqual({ ...NONE, merged: 1 });
    // Union of both insertions: this device's lines first, then the ones from the server.
    const merged = '- 08:00 kawa\n- 12:30 obiad: żurek\n- 09:00 spotkanie z Łucją\n';
    expect(await b.read(journal)).toBe(merged);
    expect(server.text(`${FOLDER}/${journal}`)).toBe(merged);
    expect(counts(await a.sync())).toEqual({ ...NONE, down: 1 });
    expect(await a.read(journal)).toBe(merged);
    expect(counts(await a.sync())).toEqual(NONE);
    expect(counts(await b.sync())).toEqual(NONE);
  });

  it('merges edits of different parts of a note', async () => {
    const { a, b } = await pair({ 'n.md': '# Tytuł\n\nakapit 1\n\nakapit 2\n\nakapit 3\n' });
    await a.sync();
    await b.sync();
    await a.write('n.md', '# Tytuł zmieniony\n\nakapit 1\n\nakapit 2\n\nakapit 3\n');
    await b.write('n.md', '# Tytuł\n\nakapit 1\n\nakapit 2\n\nakapit 3 — poprawiony\n');
    await a.sync();
    expect(counts(await b.sync())).toEqual({ ...NONE, merged: 1 });
    await a.sync();
    const expected = '# Tytuł zmieniony\n\nakapit 1\n\nakapit 2\n\nakapit 3 — poprawiony\n';
    expect(await a.read('n.md')).toBe(expected);
    expect(await b.read('n.md')).toBe(expected);
  });

  it('a real conflict keeps both versions: local at the path, remote as a conflict copy', async () => {
    const { server, a, b } = await pair({ 'Zakupy.md': '- [ ] chleb\n' });
    await a.sync();
    await b.sync();
    await a.write('Zakupy.md', '- [ ] chleb razowy\n');
    await b.write('Zakupy.md', '- [ ] chleb żytni\n');
    await a.sync();
    const rb = await b.sync();
    const copy = `Zakupy.conflict-telefon-${STAMP}.md`;
    expect(counts(rb)).toEqual({ ...NONE, conflicts: 1 });
    expect(rb.conflicts).toEqual(['Zakupy.md']);
    expect(rb.conflictCopies).toEqual([copy]);
    expect(b.files()).toEqual({ 'Zakupy.md': '- [ ] chleb żytni\n', [copy]: '- [ ] chleb razowy\n' });
    expect(server.files(FOLDER)).toEqual(b.files());
    // The other device receives both.
    expect(counts(await a.sync())).toEqual({ ...NONE, down: 2 });
    expect(a.files()).toEqual(b.files());
    expect(counts(await a.sync())).toEqual(NONE);
    expect(counts(await b.sync())).toEqual(NONE);
  });

  it('an edit wins over a deletion on the other device (both orders)', async () => {
    const { server, a, b } = await pair({ 'x.md': 'x\n', 'y.md': 'y\n' });
    await a.sync();
    await b.sync();
    // x: deleted on A, edited on B. y: edited on A, deleted on B.
    await a.vault.delete('x.md');
    await b.write('x.md', 'x poprawione\n');
    await a.write('y.md', 'y poprawione\n');
    await b.vault.delete('y.md');
    expect(counts(await a.sync())).toEqual({ ...NONE, up: 1, delR: 1 });
    // B: x edited locally but deleted remotely -> restored on the server; y deleted locally but edited remotely -> restored locally.
    expect(counts(await b.sync())).toEqual({ ...NONE, up: 1, down: 1 });
    expect(counts(await a.sync())).toEqual({ ...NONE, down: 1 });
    const expected = { 'x.md': 'x poprawione\n', 'y.md': 'y poprawione\n' };
    expect(a.files()).toEqual(expected);
    expect(b.files()).toEqual(expected);
    expect(server.files(FOLDER)).toEqual(expected);
  });

  it('syncs binary files and saves a conflicting remote binary as a copy', async () => {
    const { a, b, clock } = await pair({ 'assets/kot.png': new Uint8Array([1, 1, 1]) });
    await a.sync();
    await b.sync();
    expect(await b.vault.readBinary('assets/kot.png')).toEqual(new Uint8Array([1, 1, 1]));
    clock.advance(1000);
    await a.vault.writeBinary('assets/kot.png', new Uint8Array([2, 2, 2, 2]));
    await b.vault.writeBinary('assets/kot.png', new Uint8Array([3, 3]));
    await a.sync();
    const rb = await b.sync();
    expect(rb.conflicts).toEqual(['assets/kot.png']);
    const copy = `assets/kot.conflict-telefon-${conflictStamp(START + 1000)}.png`;
    expect(rb.conflictCopies).toEqual([copy]);
    expect(await b.vault.readBinary('assets/kot.png')).toEqual(new Uint8Array([3, 3]));
    expect(await b.vault.readBinary(copy)).toEqual(new Uint8Array([2, 2, 2, 2]));
    await a.sync();
    expect(await a.vault.readBinary('assets/kot.png')).toEqual(new Uint8Array([3, 3]));
    expect(await a.vault.readBinary(copy)).toEqual(new Uint8Array([2, 2, 2, 2]));
  });

  it('first sync with existing content on both sides keeps everything', async () => {
    const { server, a, b } = await pair(
      { 'wspólny.md': 'taki sam\n', 'tylko-a.md': 'a\n', 'dziennik.md': '# Dziennik\n- rano: bieganie\n', 'obraz.bin': new Uint8Array([1]) },
      { 'wspólny.md': 'taki sam\n', 'tylko-b.md': 'b\n', 'dziennik.md': '# Dziennik\n- wieczorem: książka\n', 'obraz.bin': new Uint8Array([2]) },
    );
    expect(counts(await a.sync())).toEqual({ ...NONE, up: 4 });
    const rb = await b.sync();
    // tylko-a: download; tylko-b: upload; dziennik: union merge; obraz: conflict; wspólny: identical.
    expect(counts(rb)).toEqual({ ...NONE, up: 1, down: 1, merged: 1, conflicts: 1 });
    expect(await b.read('dziennik.md')).toBe('# Dziennik\n- wieczorem: książka\n- rano: bieganie\n');
    expect(await b.vault.readBinary('obraz.bin')).toEqual(new Uint8Array([2]));
    expect(await b.vault.readBinary(`obraz.conflict-telefon-${STAMP}.bin`)).toEqual(new Uint8Array([1]));
    await a.sync();
    expect(a.files()).toEqual(b.files());
    expect(server.files(FOLDER)).toEqual(b.files());
    expect(counts(await a.sync())).toEqual(NONE);
    expect(counts(await b.sync())).toEqual(NONE);
  });

  it('identical files present on both sides at first sync are only recorded', async () => {
    const { server, a, b } = await pair({ 'a.md': 'same\n' }, { 'a.md': 'same\n' });
    await a.sync();
    const puts = server.requests('PUT').length;
    expect(counts(await b.sync())).toEqual(NONE);
    expect(server.requests('PUT').length).toBe(puts);
    expect(await b.engine.stateOf('a.md')).toMatchObject({ remoteEtag: server.etag(`${FOLDER}/a.md`) });
  });

  it('conflict copies are synced like normal files (also Syncthing ones)', async () => {
    const { a, b } = await pair({ 'n.sync-conflict-20261014-101010-ABCDEFG.md': 'kopia\n' });
    await a.sync();
    await b.sync();
    expect(b.files()).toEqual({ 'n.sync-conflict-20261014-101010-ABCDEFG.md': 'kopia\n' });
  });

  it('merges vault config changes key by key', async () => {
    const base = '{\n  "journalsFolder": "journals",\n  "weekStartsOn": 1,\n  "defaultReminderTime": "09:00"\n}\n';
    const { a, b } = await pair({ [CONFIG_PATH]: base });
    await a.sync();
    await b.sync();
    // Adjacent lines changed on both sides: a line conflict, but different keys.
    await a.write(CONFIG_PATH, '{\n  "journalsFolder": "Dziennik",\n  "weekStartsOn": 1,\n  "defaultReminderTime": "09:00"\n}\n');
    await b.write(CONFIG_PATH, '{\n  "journalsFolder": "journals",\n  "weekStartsOn": 0,\n  "defaultReminderTime": "09:00"\n}\n');
    await a.sync();
    const rb = await b.sync();
    expect(rb.conflicts).toEqual([]);
    expect(rb.merged).toBe(1);
    await a.sync();
    for (const d of [a, b]) {
      expect(d.vault.config.journalsFolder).toBe('Dziennik');
      expect(d.vault.config.weekStartsOn).toBe(0);
    }
  });

  it('keeps CRLF line endings of the local file when merging', async () => {
    const { a, b } = await pair({ 'w.md': 'a\r\nb\r\nc\r\n' });
    await a.sync();
    await b.sync();
    await a.write('w.md', 'A\nb\nc\n');
    await b.write('w.md', 'a\r\nb\r\nc\r\nd\r\n');
    await a.sync();
    expect(counts(await b.sync())).toEqual({ ...NONE, merged: 1 });
    expect(await b.read('w.md')).toBe('A\r\nb\r\nc\r\nd\r\n');
  });

  it('runs transfers in parallel (at most 4 at a time)', async () => {
    const files: Record<string, string> = {};
    for (let i = 0; i < 12; i++) files[`p${i}.md`] = `${i}`;
    const server = newServer();
    let active = 0;
    let peak = 0;
    const transport = server.transport;
    const slow = async (req: Parameters<typeof transport>[0]) => {
      if (req.method !== 'PUT') return transport(req);
      active++;
      peak = Math.max(peak, active);
      await new Promise((r) => setTimeout(r, 5));
      active--;
      return transport(req);
    };
    const fs = new MemoryFs({ files });
    const vault = new Vault(fs, { id: 'v', name: 'v', store: LocalStore.memory('v') });
    await vault.open();
    const client = new WebDavClient({ baseUrl: server.url(FOLDER), username: server.username, password: server.password, transport: slow });
    const engine = new SyncEngine({ vault, client, store: LocalStore.memory('s'), device: 'pc' });
    const r = await engine.sync();
    expect(r.uploaded).toBe(12);
    expect(peak).toBeGreaterThan(1);
    expect(peak).toBeLessThanOrEqual(4);
  });

  it('logs a warning but succeeds when the conflict copy upload fails', async () => {
    const { server, a, b } = await pair({ 'k.md': 'baza\n' });
    await a.sync();
    await b.sync();
    await a.write('k.md', 'wersja A\n');
    await b.write('k.md', 'wersja B\n');
    await a.sync();
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
    server.hooks.push((req, path) => (req.method === 'PUT' && path.includes('.conflict-') ? { status: 500, headers: {}, body: new Uint8Array() } : undefined));
    const rb = await b.sync();
    expect(rb.conflicts).toEqual(['k.md']);
    expect(rb.errors).toEqual([]);
    expect(warn).toHaveBeenCalled();
    warn.mockRestore();
    server.hooks.length = 0;
    // The copy is uploaded by the next sync.
    expect(counts(await b.sync())).toEqual({ ...NONE, up: 1 });
    expect(Object.keys(server.files(FOLDER)).some((p) => p.includes('.conflict-telefon-'))).toBe(true);
  });

  it('server-side content stays byte-identical to the synced vault text (UTF-8)', async () => {
    const { server, a } = await pair({ 'pl.md': 'Zażółć gęślą jaźń — ĄĆĘŁŃÓŚŹŻ\n' });
    await a.sync();
    expect(server.bytes(`${FOLDER}/pl.md`)).toEqual(enc.encode('Zażółć gęślą jaźń — ĄĆĘŁŃÓŚŹŻ\n'));
  });
});

// ---------------------------------------------------------------------------
// Regressions found in review
// ---------------------------------------------------------------------------

describe('SyncEngine — review regressions', () => {
  // "Zażółć gęślą\n" in Windows-1250 (common for CSV files exported by Excel in Poland).
  const CP1250 = new Uint8Array([0x5a, 0x61, 0xbf, 0xf3, 0xb3, 0xe6, 0x20, 0x67, 0xea, 0x9c, 0x6c, 0xb9, 0x0a]);

  it('transfers text files that are not valid UTF-8 byte for byte (both directions)', async () => {
    const clock = makeClock();
    const server = newServer({}, clock);
    const a = await device(server, 'laptop', { 'dane.csv': CP1250, 'notatka.md': 'ok\n' }, { clock });
    expect(counts(await a.sync())).toEqual({ ...NONE, up: 2 });
    expect(server.bytes(`${FOLDER}/dane.csv`)).toEqual(CP1250);
    const b = await device(server, 'telefon', {}, { clock });
    expect(counts(await b.sync())).toEqual({ ...NONE, down: 2 });
    expect(await b.vault.readBinary('dane.csv')).toEqual(CP1250);
    // Nothing left to do on either side.
    expect(counts(await a.sync())).toEqual(NONE);
    expect(counts(await b.sync())).toEqual(NONE);
  });

  it('a non-UTF-8 text file changed on both sides becomes a conflict copy with the exact remote bytes', async () => {
    const clock = makeClock();
    const server = newServer({}, clock);
    const a = await device(server, 'laptop', { 'dane.txt': CP1250 }, { clock });
    const b = await device(server, 'telefon', {}, { clock });
    await a.sync();
    await b.sync();
    clock.advance(1000);
    const fromA = new Uint8Array([...CP1250, 0xb3, 0x0a]);
    await a.vault.writeBinary('dane.txt', fromA);
    await b.vault.writeBinary('dane.txt', new Uint8Array([...CP1250, 0xea, 0x0a]));
    await a.sync();
    const rb = await b.sync();
    expect(rb.conflicts).toEqual(['dane.txt']);
    expect(await b.vault.readBinary(rb.conflictCopies[0]!)).toEqual(fromA);
  });

  it('does not pile up conflict copies while the upload keeps failing', async () => {
    const { server, a, b, clock } = await (async () => {
      const clock = makeClock();
      const server = newServer({}, clock);
      return { server, clock, a: await device(server, 'laptop', { 'k.md': 'baza\n' }, { clock }), b: await device(server, 'telefon', {}, { clock }) };
    })();
    await a.sync();
    await b.sync();
    await a.write('k.md', 'wersja A\n');
    await b.write('k.md', 'wersja B\n');
    await a.sync();
    let full = true;
    server.hooks.push((req, path) => (full && req.method === 'PUT' && path.endsWith('/k.md') ? { status: 507, headers: {}, body: new Uint8Array() } : undefined));
    for (let i = 0; i < 3; i++) {
      clock.advance(60_000);
      const r = await b.sync();
      expect(r.errors.map((e) => e.path)).toEqual(['k.md']);
    }
    const copies = () => Object.keys(b.files()).filter((p) => p.includes('.conflict-'));
    expect(copies()).toHaveLength(1);
    expect(b.files()[copies()[0]!]).toBe('wersja A\n');
    full = false;
    clock.advance(60_000);
    const r = await b.sync();
    expect(r.conflicts).toEqual(['k.md']);
    expect(r.conflictCopies).toEqual([]);
    expect(copies()).toHaveLength(1);
    expect(server.text(`${FOLDER}/k.md`)).toBe('wersja B\n');
    await a.sync();
    expect(a.files()).toEqual(b.files());
  });

  it('refuses to delete everything on the server when only freshly created files are left locally', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'a\n', 'b.md': 'b\n', 'c.md': 'c\n' });
    await a.sync();
    // The vault storage was lost and the app has created today's journal and a default config.
    const vault2 = new Vault(new MemoryFs({ files: { 'journals/2026-10-14.md': '- \n', [CONFIG_PATH]: '{}\n' } }), { id: 'x', name: 'x', store: LocalStore.memory('x') });
    await vault2.open();
    const client = new WebDavClient({ baseUrl: server.url(FOLDER), username: server.username, password: server.password, transport: server.transport });
    const engine2 = new SyncEngine({ vault: vault2, client, store: a.store, device: 'laptop' });
    expect(isSyncError(await rejection(engine2.sync()), 'local-empty')).toBe(true);
    expect(Object.keys(server.files(FOLDER)).sort()).toEqual(['a.md', 'b.md', 'c.md']);
  });

  it('refuses to trash every note when the server only has new files', async () => {
    const server = newServer();
    const a = await device(server, 'laptop', { 'a.md': 'a\n', 'b.md': 'b\n' });
    await a.sync();
    server.remove(`${FOLDER}/a.md`);
    server.remove(`${FOLDER}/b.md`);
    server.putFile(`${FOLDER}/inny.md`, 'z innego urządzenia\n');
    expect(isSyncError(await rejection(a.sync()), 'remote-empty')).toBe(true);
    expect(a.files()).toEqual({ 'a.md': 'a\n', 'b.md': 'b\n' });
  });

  it('first sync of two different vault configs yields valid JSON (key-wise merge)', async () => {
    const cfg = (o: Record<string, unknown>) => `${JSON.stringify({ version: 1, journalsFolder: 'journals', weekStartsOn: 1, ...o }, null, 2)}\n`;
    const clock = makeClock();
    const server = newServer({}, clock);
    const a = await device(server, 'laptop', { [CONFIG_PATH]: cfg({ weekStartsOn: 1 }) }, { clock });
    const b = await device(server, 'telefon', { [CONFIG_PATH]: cfg({ journalsFolder: 'Dziennik', weekStartsOn: 0, extra: 'ż' }) }, { clock });
    await a.sync();
    const rb = await b.sync();
    expect(rb.conflicts).toEqual([]);
    const text = server.text(`${FOLDER}/${CONFIG_PATH}`)!;
    expect(JSON.parse(text)).toEqual({ version: 1, journalsFolder: 'Dziennik', weekStartsOn: 0, extra: 'ż' });
    expect(await b.read(CONFIG_PATH)).toBe(text);
    await a.sync();
    expect(a.vault.config.journalsFolder).toBe('Dziennik');
  });

  it('never syncs ignored folders spelled with different letter case (case-insensitive file systems)', async () => {
    expect(isSyncablePath('.GIT/hooks/pre-commit')).toBe(false);
    expect(isSyncablePath('.Obsidian/plugins/x/main.js')).toBe(false);
    expect(isSyncablePath('.Brulion/config.json')).toBe(false);
    expect(isSyncablePath('.Stignore')).toBe(false);
    expect(isSyncablePath('Notatki/Łódź.md')).toBe(true);
    // Visible folders keep their case: imported Logseq / Node notes stay synced.
    expect(isSyncablePath('Import/Logseq/pages/Strona.md')).toBe(true);
    expect(isSyncablePath('Projekty/Node_Modules.md')).toBe(true);
    expect(isSyncablePath('logseq/config.edn')).toBe(false);
    const server = newServer();
    server.putFile(`${FOLDER}/.GIT/hooks/pre-commit`, '#!/bin/sh\nrm -rf ~\n');
    server.putFile(`${FOLDER}/a.md`, 'a');
    const a = await device(server, 'laptop');
    expect(counts(await a.sync())).toEqual({ ...NONE, down: 1 });
    expect(a.files()).toEqual({ 'a.md': 'a' });
    expect(server.requests('PROPFIND').some((q) => q.path.includes('.GIT'))).toBe(false);
  });

  describe('case-insensitive local file system', () => {
    async function ciDevice(server: FakeDavServer, files: Record<string, string>) {
      const mem = new MemoryFs({ files });
      const vault = new Vault(new CaseInsensitiveFs(mem), { id: 'ci', name: 'ci', store: LocalStore.memory('ci-vault') });
      await vault.open();
      const client = new WebDavClient({ baseUrl: server.url(FOLDER), username: server.username, password: server.password, transport: server.transport });
      const engine = new SyncEngine({ vault, client, store: LocalStore.memory('ci-sync'), device: 'telefon' });
      const visible = () => Object.fromEntries(Object.entries(mem.dump()).filter(([p]) => !p.startsWith('.trash/')));
      return { mem, vault, engine, visible };
    }

    it('a case-only rename on another device keeps the note (no download-then-trash of the same file)', async () => {
      const server = newServer();
      const d = await ciDevice(server, { 'Notatka.md': 'treść ważna\n', 'inna.md': 'x\n' });
      await d.engine.sync();
      // Another device renamed "Notatka.md" to "notatka.md".
      server.remove(`${FOLDER}/Notatka.md`);
      server.putFile(`${FOLDER}/notatka.md`, 'treść ważna\n');
      for (let i = 0; i < 3; i++) {
        await d.engine.sync();
        await d.vault.rescan();
      }
      expect(d.visible()).toEqual({ 'inna.md': 'x\n', 'notatka.md': 'treść ważna\n' });
      expect(server.files(FOLDER)).toEqual({ 'inna.md': 'x\n', 'notatka.md': 'treść ważna\n' });
    });

    it('never overwrites a local file whose name differs only in letter case', async () => {
      const server = newServer();
      const d = await ciDevice(server, { 'Zadania.md': 'lokalne zadania\n' });
      await d.engine.sync();
      await d.vault.write('Zadania.md', 'lokalne zadania — edycja\n');
      // Another device renamed the file case-only meanwhile.
      server.remove(`${FOLDER}/Zadania.md`);
      server.putFile(`${FOLDER}/zadania.md`, 'lokalne zadania\n');
      const r = await d.engine.sync();
      expect(r.errors.map((e) => e.path)).toEqual(['zadania.md']);
      expect(await d.mem.readText('Zadania.md')).toBe('lokalne zadania — edycja\n');
      expect(server.text(`${FOLDER}/Zadania.md`)).toBe('lokalne zadania — edycja\n');
    });
  });
});
