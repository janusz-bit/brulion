import { describe, expect, it } from 'vitest';
import type { HttpRequest, HttpResponse, HttpTransport } from '../types';
import { completionPercent, findFolderForPath, guessHome, normalizeFsPath, STIGNORE_TEMPLATE, SyncthingClient, SyncthingError } from './syncthing';

const enc = new TextEncoder();
const json = (v: unknown, status = 200): HttpResponse => ({ status, headers: { 'Content-Type': 'application/json' }, body: enc.encode(JSON.stringify(v)) });

const FOLDERS = [
  { id: 'notatki-1', label: 'Notatki', path: '/home/jan/Sync/Notatki', type: 'sendreceive', paused: false },
  { id: 'sync', label: 'Sync', path: '~/Sync/', type: 'sendreceive' },
  { id: 'android', label: '', path: '/storage/emulated/0/Brulion' },
  { label: 'bez id', path: '/x' },
];

function fakeSyncthing(opts: { apiKey?: string; down?: boolean } = {}) {
  const requests: HttpRequest[] = [];
  const transport: HttpTransport = async (req) => {
    requests.push(req);
    if (opts.down) throw new TypeError('fetch failed');
    if (req.headers?.['X-API-Key'] !== (opts.apiKey ?? 'klucz')) return json({ error: 'Forbidden' }, 403);
    const url = new URL(req.url);
    switch (url.pathname) {
      case '/rest/system/ping':
        return json({ ping: 'pong' });
      case '/rest/config/folders':
        return json(FOLDERS);
      case '/rest/system/connections':
        return json({ total: {}, connections: { AAA: { connected: true }, BBB: { connected: false }, CCC: { connected: true } } });
      case '/rest/db/status': {
        const folder = url.searchParams.get('folder');
        if (folder === 'notatki-1') return json({ state: 'syncing', needBytes: 250, globalBytes: 1000, errors: 1, pullErrors: 2 });
        if (folder === 'idle') return json({ state: 'idle', needBytes: 0, globalBytes: 0, errors: 0 });
        return json({ error: 'no such folder' }, 404);
      }
      default:
        return json({}, 404);
    }
  };
  return { transport, requests };
}

describe('SyncthingClient', () => {
  it('uses the default URL and sends the API key header', async () => {
    const { transport, requests } = fakeSyncthing();
    const c = new SyncthingClient({ apiKey: 'klucz', transport });
    expect(await c.ping()).toBe(true);
    expect(requests[0]?.url).toBe('http://127.0.0.1:8384/rest/system/ping');
    expect(requests[0]?.headers?.['X-API-Key']).toBe('klucz');
  });

  it('lists folders (label falls back to id, entries without id skipped)', async () => {
    const c = new SyncthingClient({ url: 'http://127.0.0.1:8384/', apiKey: 'klucz', transport: fakeSyncthing().transport });
    expect(await c.folders()).toEqual([
      { id: 'notatki-1', label: 'Notatki', path: '/home/jan/Sync/Notatki', paused: false },
      { id: 'sync', label: 'Sync', path: '~/Sync/' },
      { id: 'android', label: 'android', path: '/storage/emulated/0/Brulion' },
    ]);
  });

  it('counts connected devices', async () => {
    const c = new SyncthingClient({ apiKey: 'klucz', transport: fakeSyncthing().transport });
    expect(await c.connections()).toBe(2);
  });

  it('reports folder status with completion and summed errors', async () => {
    const c = new SyncthingClient({ apiKey: 'klucz', transport: fakeSyncthing().transport });
    expect(await c.status('notatki-1')).toEqual({ state: 'syncing', needBytes: 250, globalBytes: 1000, completion: 75, errors: 3, folderId: 'notatki-1', connected: 2 });
    expect((await c.status('idle')).completion).toBe(100);
  });

  it('uses the configured folder when status() gets no id', async () => {
    const c = new SyncthingClient({ apiKey: 'klucz', transport: fakeSyncthing().transport, folderId: 'notatki-1' });
    expect((await c.status()).state).toBe('syncing');
  });

  it('without any folder reports daemon reachability', async () => {
    const c = new SyncthingClient({ apiKey: 'klucz', transport: fakeSyncthing().transport });
    expect(await c.status()).toEqual({ state: 'idle', needBytes: 0, globalBytes: 0, completion: 100, errors: 0, connected: 2 });
  });

  it('maps every failure to state offline', async () => {
    const down = new SyncthingClient({ apiKey: 'klucz', transport: fakeSyncthing({ down: true }).transport });
    expect((await down.status('notatki-1')).state).toBe('offline');
    expect((await down.status()).state).toBe('offline');
    expect(await down.ping()).toBe(false);
    const wrongKey = new SyncthingClient({ apiKey: 'zły', transport: fakeSyncthing().transport });
    const st = await wrongKey.status('notatki-1');
    expect(st.state).toBe('offline');
    expect(st.error).toMatch(/403/);
    const unknown = new SyncthingClient({ apiKey: 'klucz', transport: fakeSyncthing().transport });
    expect((await unknown.status('nie-ma')).state).toBe('offline');
  });

  it('folders() throws SyncthingError when unreachable or unauthorised', async () => {
    await expect(new SyncthingClient({ apiKey: 'klucz', transport: fakeSyncthing({ down: true }).transport }).folders()).rejects.toBeInstanceOf(SyncthingError);
    await expect(new SyncthingClient({ apiKey: 'x', transport: fakeSyncthing().transport }).folders()).rejects.toMatchObject({ status: 403 });
  });

  it('rejects invalid JSON', async () => {
    const transport: HttpTransport = async () => ({ status: 200, headers: {}, body: enc.encode('<html>') });
    await expect(new SyncthingClient({ apiKey: 'k', transport }).folders()).rejects.toThrow(/invalid JSON/);
  });

  it('finds the folder containing a path', async () => {
    const c = new SyncthingClient({ apiKey: 'klucz', transport: fakeSyncthing().transport });
    expect((await c.findFolderForPath('/home/jan/Sync/Notatki/journals'))?.id).toBe('notatki-1');
    expect((await c.findFolderForPath('/home/jan/Sync/Inne'))?.id).toBe('sync');
    expect((await c.findFolderForPath('/storage/emulated/0/Brulion/'))?.id).toBe('android');
    expect(await c.findFolderForPath('/home/jan/Dokumenty')).toBeNull();
  });
});

describe('path helpers', () => {
  it('normalizeFsPath handles slashes, ~ and Windows paths', () => {
    expect(normalizeFsPath('/home/jan/Sync//Notatki/')).toBe('/home/jan/Sync/Notatki');
    expect(normalizeFsPath('~/Sync', '/home/jan/')).toBe('/home/jan/Sync');
    expect(normalizeFsPath('C:\\Users\\Jan\\Notatki\\')).toBe('c:/Users/Jan/Notatki');
    expect(normalizeFsPath('/')).toBe('/');
  });

  it('guessHome recognises common home folders', () => {
    expect(guessHome('/home/jan/Sync')).toBe('/home/jan');
    expect(guessHome('/Users/ania/x')).toBe('/Users/ania');
    expect(guessHome('/root')).toBe('/root');
    expect(guessHome('/storage/emulated/0/x')).toBeUndefined();
  });

  it('findFolderForPath prefers the longest prefix and respects segment boundaries', () => {
    const folders = [{ path: '/data' }, { path: '/data/notes' }, { path: '/data/notes-old' }];
    expect(findFolderForPath(folders, '/data/notes/a.md')).toBe(folders[1]);
    expect(findFolderForPath(folders, '/data/notes-old')).toBe(folders[2]);
    expect(findFolderForPath(folders, '/data/notesy')).toBe(folders[0]);
    expect(findFolderForPath(folders, '/other')).toBeNull();
  });

  it('findFolderForPath is case-insensitive on Windows and expands ~ with a given home', () => {
    expect(findFolderForPath([{ path: 'C:\\Users\\Jan\\Sync' }], 'c:/users/jan/sync/Notatki')).not.toBeNull();
    expect(findFolderForPath([{ path: '~/Notatki' }], '/srv/x/Notatki/a', '/srv/x')).not.toBeNull();
    expect(findFolderForPath([{ path: '~/Notatki' }], '/storage/emulated/0/Notatki')).toBeNull();
  });

  it('completionPercent', () => {
    expect(completionPercent(0, 0)).toBe(100);
    expect(completionPercent(5, 0)).toBe(0);
    expect(completionPercent(1, 3)).toBe(66.6);
    expect(completionPercent(10, 5)).toBe(0);
  });
});

describe('STIGNORE_TEMPLATE', () => {
  it('contains the recommended patterns and Polish comments', () => {
    for (const line of ['/.trash', '(?d).DS_Store', '(?d)Thumbs.db', '/.obsidian/workspace*.json', '/logseq/bak', '/logseq/.recycle', '*.tmp']) {
      expect(STIGNORE_TEMPLATE.split('\n')).toContain(line);
    }
    expect(STIGNORE_TEMPLATE).toMatch(/^\/\/ .*notatk/m);
    // Every non-empty line is either a comment or a pattern without trailing spaces.
    for (const line of STIGNORE_TEMPLATE.split('\n')) expect(line).toBe(line.trimEnd());
  });
});
