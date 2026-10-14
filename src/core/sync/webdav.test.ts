import { describe, expect, it } from 'vitest';
import type { HttpResponse, HttpTransport } from '../types';
import { isWebDavError, WebDavError } from './errors';
import { FakeDavServer } from './fakeDav.testutil';
import { WebDavClient } from './webdav';

const enc = new TextEncoder();
const dec = new TextDecoder();

function setup(opts: ConstructorParameters<typeof FakeDavServer>[0] = {}, folder = 'Notatki') {
  const server = new FakeDavServer(opts);
  server.nodes.set(server.abs(folder), { kind: 'dir', mtime: 0, etag: 'root' });
  const client = new WebDavClient({ baseUrl: server.url(folder), username: server.username, password: server.password, transport: server.transport });
  return { server, client };
}

async function rejection(p: Promise<unknown>): Promise<unknown> {
  try {
    await p;
  } catch (e) {
    return e;
  }
  throw new Error('expected a rejection');
}

describe('WebDavClient.test', () => {
  it('succeeds with a PROPFIND Depth 0 on the base folder', async () => {
    const { server, client } = setup();
    expect(await client.test()).toEqual({ ok: true });
    const [req] = server.requests('PROPFIND');
    expect(req?.headers.Depth).toBe('0');
    expect(req?.path).toBe('/remote.php/dav/files/jan/Notatki');
  });

  it('reports wrong credentials as auth (Basic auth is UTF-8)', async () => {
    const { server } = setup();
    const bad = new WebDavClient({ baseUrl: server.url('Notatki'), username: 'jan', password: 'zle-haslo', transport: server.transport });
    expect(await bad.test()).toMatchObject({ ok: false, error: 'auth', status: 401 });
  });

  it('reports 403 and 404', async () => {
    const { server, client } = setup();
    server.hooks.push(() => ({ status: 403, headers: {}, body: new Uint8Array() }));
    expect(await client.test()).toMatchObject({ ok: false, error: 'forbidden', status: 403 });
    const missing = new WebDavClient({ baseUrl: server.url('Brak'), username: 'jan', password: server.password, transport: new FakeDavServer().transport });
    expect(await missing.test()).toMatchObject({ ok: false, error: 'not-found', status: 404 });
  });

  it('maps a browser fetch TypeError to cors and other failures to network', async () => {
    const cors = new WebDavClient({ baseUrl: 'https://h/dav', username: 'a', password: 'b', transport: async () => Promise.reject(new TypeError('Failed to fetch')) });
    expect(await cors.test()).toMatchObject({ ok: false, error: 'cors', status: 0 });
    const nodeErr = Object.assign(new TypeError('fetch failed'), { cause: { code: 'ECONNREFUSED' } });
    const refused = new WebDavClient({ baseUrl: 'https://h/dav', username: 'a', password: 'b', transport: async () => Promise.reject(nodeErr) });
    expect(await refused.test()).toMatchObject({ ok: false, error: 'network' });
    const tauri = new WebDavClient({ baseUrl: 'https://h/dav', username: 'a', password: 'b', transport: async () => Promise.reject(new Error('error sending request')) });
    expect(await tauri.test()).toMatchObject({ ok: false, error: 'network' });
  });

  it('reports HTML login pages as parse errors', async () => {
    const transport: HttpTransport = async () => ({ status: 207, headers: {}, body: enc.encode('<html>login</html>') });
    const c = new WebDavClient({ baseUrl: 'https://h/dav', username: 'a', password: 'b', transport });
    expect(await c.test()).toMatchObject({ ok: false, error: 'parse' });
  });

  it('sends Authorization and X-Requested-With headers', async () => {
    const { server, client } = setup();
    await client.test();
    const h = server.log[0]!.headers;
    expect(h.Authorization).toBe(`Basic ${Buffer.from(`jan:${server.password}`).toString('base64')}`);
    expect(h['X-Requested-With']).toBe('XMLHttpRequest');
  });
});

describe('WebDavClient.list', () => {
  it('lists recursively with Depth 1 per folder, decoding names', async () => {
    const { server, client } = setup();
    server.putFile('Notatki/Strona.md', 'abc');
    server.putFile('Notatki/journals/2026-10-14.md', 'dziennik');
    server.putFile('Notatki/Zażółć gęślą jaźń/notatka #1?.md', 'ą');
    server.putFile('Notatki/a/b/c/głęboko.md', 'x');
    const entries = await client.list();
    expect(entries.map((e) => `${e.kind}:${e.path}`)).toEqual([
      'file:Strona.md',
      'dir:Zażółć gęślą jaźń',
      'file:Zażółć gęślą jaźń/notatka #1?.md',
      'dir:a',
      'dir:a/b',
      'dir:a/b/c',
      'file:a/b/c/głęboko.md',
      'dir:journals',
      'file:journals/2026-10-14.md',
    ]);
    expect(server.requests('PROPFIND').every((r) => r.headers.Depth === '1')).toBe(true);
    const file = entries.find((e) => e.path === 'Strona.md')!;
    expect(file).toEqual({ path: 'Strona.md', kind: 'file', etag: server.etag('Notatki/Strona.md'), mtime: Date.UTC(2026, 9, 14, 8), size: 3 });
  });

  it('works with full-URL hrefs and D:/default namespace prefixes', async () => {
    for (const opts of [{ hrefStyle: 'url' as const }, { prefix: 'D' }, { prefix: '' }]) {
      const { server, client } = setup(opts);
      server.putFile('Notatki/x/ż.md', '1');
      expect((await client.list()).map((e) => e.path)).toEqual(['x', 'x/ż.md']);
    }
  });

  it('does not descend into skipped folders', async () => {
    const { server, client } = setup();
    server.putFile('Notatki/.git/objects/aa', 'x');
    server.putFile('Notatki/a.md', 'x');
    const entries = await client.list({ skipDir: (p) => p === '.git' });
    expect(entries.map((e) => e.path)).toEqual(['.git', 'a.md']);
    expect(server.requests('PROPFIND').some((r) => r.path.endsWith('.git'))).toBe(false);
  });

  it('throws not-found when the base folder is missing', async () => {
    const server = new FakeDavServer();
    const client = new WebDavClient({ baseUrl: server.url('Brak'), username: 'jan', password: server.password, transport: server.transport });
    const err = await rejection(client.list());
    expect(isWebDavError(err, 'not-found')).toBe(true);
  });

  it('skips a sub-folder deleted while listing', async () => {
    const { server, client } = setup();
    server.putFile('Notatki/a/x.md', '1');
    server.putFile('Notatki/b.md', '2');
    server.hooks.push((req, path) => (req.method === 'PROPFIND' && path.endsWith('/a') ? { status: 404, headers: {}, body: new Uint8Array() } : undefined));
    expect((await client.list()).map((e) => e.path)).toEqual(['a', 'b.md']);
  });

  it('rejects when aborted', async () => {
    const { server, client } = setup();
    server.putFile('Notatki/a/x.md', '1');
    const ctrl = new AbortController();
    ctrl.abort();
    const err = await rejection(client.list({ signal: ctrl.signal }));
    expect((err as Error).name).toBe('AbortError');
  });

  it('ignores hrefs outside the base folder', async () => {
    const xml = `<d:multistatus xmlns:d="DAV:">
<d:response><d:href>/dav/N/</d:href><d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop></d:propstat></d:response>
<d:response><d:href>/elsewhere/x.md</d:href><d:propstat><d:prop><d:resourcetype/></d:prop></d:propstat></d:response>
<d:response><d:href>y.md</d:href><d:propstat><d:prop><d:resourcetype/><d:getetag>"1"</d:getetag></d:prop></d:propstat></d:response>
</d:multistatus>`;
    const transport: HttpTransport = async () => ({ status: 207, headers: {}, body: enc.encode(xml) });
    const c = new WebDavClient({ baseUrl: 'https://h/dav/N', username: 'a', password: 'b', transport });
    expect(await c.list()).toEqual([{ path: 'y.md', kind: 'file', etag: '1', mtime: 0, size: 0 }]);
  });

  it('keeps using the server spelling of NFD names', async () => {
    const { server, client } = setup();
    const nfdName = 'Zażółć.md'.normalize('NFD');
    server.putFile(`Notatki/${nfdName}`, 'treść');
    const entries = await client.list();
    expect(entries.map((e) => e.path)).toEqual(['Zażółć.md'.normalize('NFC')]);
    const { data } = await client.get('Zażółć.md');
    expect(dec.decode(data)).toBe('treść');
  });
});

describe('WebDavClient file operations', () => {
  it('get returns bytes and the normalised etag', async () => {
    const { server, client } = setup();
    server.putFile('Notatki/a.md', 'zażółć');
    const r = await client.get('a.md');
    expect(dec.decode(r.data)).toBe('zażółć');
    expect(r.etag).toBe(server.etag('Notatki/a.md'));
    expect(isWebDavError(await rejection(client.get('brak.md')), 'not-found')).toBe(true);
  });

  it('put creates missing parent collections and returns the new etag', async () => {
    const { server, client } = setup();
    const r = await client.put('a/b/ć.md', enc.encode('x'));
    expect(server.text('Notatki/a/b/ć.md')).toBe('x');
    expect(r.etag).toBe(server.etag('Notatki/a/b/ć.md'));
    expect(server.requests('MKCOL').map((q) => q.path)).toEqual(['/remote.php/dav/files/jan/Notatki/a', '/remote.php/dav/files/jan/Notatki/a/b']);
    // Known folders are not created again.
    await client.put('a/b/d.md', enc.encode('y'));
    expect(server.requests('MKCOL')).toHaveLength(2);
  });

  it('put ignores 405 for existing folders it did not know about', async () => {
    const { server, client } = setup();
    server.putFile('Notatki/istniejący/x.md', '1');
    await client.put('istniejący/y.md', enc.encode('2'));
    expect(server.text('Notatki/istniejący/y.md')).toBe('2');
  });

  it('put recreates folders deleted behind its back (409 -> MKCOL -> retry)', async () => {
    const { server, client } = setup();
    server.putFile('Notatki/f/x.md', '1');
    await client.list();
    server.remove('Notatki/f');
    await client.put('f/y.md', enc.encode('2'));
    expect(server.text('Notatki/f/y.md')).toBe('2');
  });

  it('put honours If-Match and If-None-Match (412 -> precondition)', async () => {
    const { server, client } = setup();
    server.putFile('Notatki/a.md', 'v1');
    const etag = server.etag('Notatki/a.md')!;
    await client.put('a.md', enc.encode('v2'), { ifMatch: etag });
    expect(server.text('Notatki/a.md')).toBe('v2');
    const stale = await rejection(client.put('a.md', enc.encode('v3'), { ifMatch: etag }));
    expect(stale).toBeInstanceOf(WebDavError);
    expect(stale).toMatchObject({ code: 'precondition', status: 412 });
    expect(server.text('Notatki/a.md')).toBe('v2');
    expect(isWebDavError(await rejection(client.put('a.md', enc.encode('v4'), { ifNoneMatch: true })), 'precondition')).toBe(true);
    await client.put('nowy.md', enc.encode('n'), { ifNoneMatch: true });
    expect(server.text('Notatki/nowy.md')).toBe('n');
    const put = server.requests('PUT').at(-1)!;
    expect(put.headers['If-None-Match']).toBe('*');
  });

  it('put sends quoted If-Match and X-OC-MTime', async () => {
    const { server, client } = setup();
    server.putFile('Notatki/a.md', 'v1');
    await client.put('a.md', enc.encode('v2'), { ifMatch: server.etag('Notatki/a.md')!, mtime: 1_760_000_000_500 });
    const put = server.requests('PUT')[0]!;
    expect(put.headers['If-Match']).toMatch(/^"e[0-9a-f]+"$/);
    expect(put.headers['X-OC-MTime']).toBe('1760000000');
  });

  it('put without an ETag header returns {}', async () => {
    const { server, client } = setup({ putEtag: false });
    expect(await client.put('a.md', enc.encode('x'))).toEqual({});
    expect((await client.stat('a.md'))?.etag).toBe(server.etag('Notatki/a.md'));
  });

  it('delete removes files, tolerates missing ones and honours If-Match', async () => {
    const { server, client } = setup();
    server.putFile('Notatki/a.md', '1');
    server.putFile('Notatki/b.md', '1');
    await client.delete('a.md');
    expect(server.text('Notatki/a.md')).toBeUndefined();
    await client.delete('a.md');
    expect(isWebDavError(await rejection(client.delete('b.md', { ifMatch: 'stare' })), 'precondition')).toBe(true);
    expect(server.text('Notatki/b.md')).toBe('1');
    await expect(client.delete('')).rejects.toThrow(/base folder/);
  });

  it('mkcol creates folders and accepts existing ones', async () => {
    const { server, client } = setup();
    await client.mkcol('nowy folder');
    await client.mkcol('nowy folder');
    expect(server.nodes.get(server.abs('Notatki/nowy folder'))?.kind).toBe('dir');
    expect(isWebDavError(await rejection(client.mkcol('a/b')), 'conflict')).toBe(true);
  });

  it('ensureBase creates a nested base folder', async () => {
    const server = new FakeDavServer();
    const client = new WebDavClient({ baseUrl: server.url('Dokumenty/Moje notatki/Brulion'), username: 'jan', password: server.password, transport: server.transport });
    await client.ensureBase();
    expect(server.nodes.get(server.abs('Dokumenty/Moje notatki/Brulion'))?.kind).toBe('dir');
    expect(await client.list()).toEqual([]);
    await client.ensureBase();
  });

  it('stat returns null for missing paths and folder info for folders', async () => {
    const { server, client } = setup();
    server.putFile('Notatki/f/a.md', '123');
    expect(await client.stat('brak.md')).toBeNull();
    expect(await client.stat('f')).toMatchObject({ path: 'f', kind: 'dir', size: 0 });
    expect(await client.stat('f/a.md')).toMatchObject({ path: 'f/a.md', kind: 'file', size: 3 });
  });

  it('maps server errors to typed codes', async () => {
    const { server, client } = setup();
    const statuses: [number, string][] = [
      [500, 'server'],
      [507, 'insufficient-storage'],
      [423, 'locked'],
      [413, 'too-large'],
      [418, 'http'],
    ];
    for (const [status, code] of statuses) {
      server.hooks.length = 0;
      server.hooks.push((): HttpResponse => ({ status, headers: {}, body: new Uint8Array() }));
      expect(await rejection(client.get('a.md'))).toMatchObject({ code, status });
    }
  });

  it('exposes an identity for the account + folder', () => {
    const { client } = setup();
    expect(client.identity).toBe('jan@https://chmura.example.pl/remote.php/dav/files/jan/Notatki');
  });
});

describe('WebDavClient timeouts', () => {
  const never: HttpTransport = () => new Promise<HttpResponse>(() => {});

  it('a stalled request fails as a network error instead of hanging forever', async () => {
    const c = new WebDavClient({ baseUrl: 'https://h/dav', username: 'a', password: 'b', transport: never, timeoutMs: 20 });
    expect(await c.test()).toMatchObject({ ok: false, error: 'network', status: 0 });
    const err = await rejection(c.list());
    expect(isWebDavError(err, 'network')).toBe(true);
    expect((err as Error).message).toMatch(/timed out/);
    expect(isWebDavError(await rejection(c.delete('a.md')), 'network')).toBe(true);
  });

  it('gives uploads and downloads extra time for their size', async () => {
    let calls = 0;
    const slow: HttpTransport = async (req): Promise<HttpResponse> => {
      calls++;
      if (req.method === 'MKCOL') return { status: 405, headers: {}, body: new Uint8Array() };
      await new Promise((r) => setTimeout(r, 60));
      return { status: req.method === 'GET' ? 200 : 201, headers: { ETag: '"e1"' }, body: new Uint8Array(3) };
    };
    const c = new WebDavClient({ baseUrl: 'https://h/dav', username: 'a', password: 'b', transport: slow, timeoutMs: 30, minBytesPerSecond: 1_000_000 });
    // 60 kB at 1 MB/s adds 60 ms to the 30 ms base: the 60 ms upload succeeds.
    expect(await c.put('d/duży.bin', new Uint8Array(60_000))).toEqual({ etag: 'e1' });
    expect(isWebDavError(await rejection(c.put('mały.bin', new Uint8Array(1))), 'network')).toBe(true);
    expect((await c.get('duży.bin', { size: 60_000 })).etag).toBe('e1');
    // Without a size hint a download gets ten times the base timeout (300 ms here).
    expect((await c.get('mały.bin')).etag).toBe('e1');
    expect(calls).toBeGreaterThan(0);
  });

  it('timeoutMs: 0 disables the timeout', async () => {
    const delayed: HttpTransport = async () => {
      await new Promise((r) => setTimeout(r, 30));
      return { status: 207, headers: {}, body: enc.encode('<d:multistatus xmlns:d="DAV:"/>') };
    };
    const c = new WebDavClient({ baseUrl: 'https://h/dav', username: 'a', password: 'b', transport: delayed, timeoutMs: 0 });
    expect(await c.test()).toEqual({ ok: true });
  });
});
