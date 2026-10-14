import { describe, expect, it, vi } from 'vitest';
import type { HttpRequest } from '@/core/types';
import { classifyTransportError } from '@/core/sync';
import { createBrowserHttpTransport, createFetchTransport, createNetworkError, isNetworkError, toRequestInit, type FetchLike } from './http';

const enc = new TextEncoder();
const dec = new TextDecoder();

function fakeFetch(respond: (url: string, init: RequestInit) => Response | Promise<Response>) {
  return vi.fn<FetchLike>(async (url, init) => respond(url, init));
}

describe('toRequestInit', () => {
  it('upper-cases methods and copies headers', () => {
    const headers = { Depth: '1' };
    const init = toRequestInit({ method: 'propfind', url: 'https://x/', headers });
    expect(init.method).toBe('PROPFIND');
    expect(init.headers).toEqual({ Depth: '1' });
    expect(init.headers).not.toBe(headers);
  });

  it('passes string and byte bodies', () => {
    expect(toRequestInit({ method: 'PUT', url: 'https://x/a.md', body: 'Zażółć' }).body).toBe('Zażółć');
    const bytes = Uint8Array.from([1, 2, 3]);
    expect(toRequestInit({ method: 'PUT', url: 'https://x/a.bin', body: bytes }).body).toBe(bytes);
  });

  it('rejects bodies on GET / HEAD', () => {
    expect(() => toRequestInit({ method: 'get', url: 'https://x/', body: 'x' })).toThrow(TypeError);
    expect(() => toRequestInit({ method: 'HEAD', url: 'https://x/', body: new Uint8Array() })).toThrow(TypeError);
  });

  it('merges extra init fields', () => {
    const init = toRequestInit({ method: 'GET', url: 'https://x/' }, { credentials: 'omit', cache: 'no-store' });
    expect(init).toMatchObject({ method: 'GET', credentials: 'omit', cache: 'no-store' });
  });
});

describe('createFetchTransport', () => {
  it('performs WebDAV requests and returns status, lower-cased headers and raw bytes', async () => {
    const xml = '<?xml version="1.0"?><d:multistatus xmlns:d="DAV:"><d:href>/Notatki/Żółw.md</d:href></d:multistatus>';
    const fetch = fakeFetch(
      () =>
        new Response(xml, {
          status: 207,
          headers: { 'Content-Type': 'application/xml; charset=utf-8', ETag: '"abc"', 'OC-FileId': '42' },
        }),
    );
    const http = createFetchTransport(fetch);
    const req: HttpRequest = {
      method: 'PROPFIND',
      url: 'https://cloud.example.com/remote.php/dav/files/jan/Notatki/',
      headers: { Depth: 'infinity', Authorization: 'Basic eDp5' },
      body: '<propfind/>',
    };
    const res = await http(req);
    expect(fetch).toHaveBeenCalledWith(req.url, expect.objectContaining({ method: 'PROPFIND', body: '<propfind/>' }));
    expect(res.status).toBe(207);
    expect(res.headers).toMatchObject({ 'content-type': 'application/xml; charset=utf-8', etag: '"abc"', 'oc-fileid': '42' });
    expect(Object.keys(res.headers).every((k) => k === k.toLowerCase())).toBe(true);
    expect(dec.decode(res.body)).toBe(xml);
  });

  it('sends binary bodies (MKCOL / PUT) unchanged', async () => {
    let seen: unknown;
    const fetch = fakeFetch(async (_url, init) => {
      seen = init.body;
      return new Response(null, { status: 201 });
    });
    const http = createFetchTransport(fetch);
    const data = enc.encode('ąćęłńóśźż');
    const res = await http({ method: 'put', url: 'https://x/Łódź.md', body: data });
    expect(res.status).toBe(201);
    expect(res.body.byteLength).toBe(0);
    expect(seen).toBeInstanceOf(Uint8Array);
    expect(dec.decode(seen as Uint8Array)).toBe('ąćęłńóśźż');
    expect(fetch.mock.calls[0]![1].method).toBe('PUT');
  });

  it('returns HTTP errors instead of throwing', async () => {
    const http = createFetchTransport(fakeFetch(() => new Response('Unauthorized', { status: 401, headers: { 'WWW-Authenticate': 'Basic' } })));
    const res = await http({ method: 'GET', url: 'https://x/' });
    expect(res.status).toBe(401);
    expect(res.headers['www-authenticate']).toBe('Basic');
  });

  it('turns fetch failures into a NetworkError that mentions CORS', async () => {
    const http = createFetchTransport(
      fakeFetch(() => {
        throw new TypeError('Failed to fetch');
      }),
    );
    const err = await http({ method: 'PROPFIND', url: 'https://jan:hasło@cloud.example.com/dav' }).catch((e: unknown) => e);
    expect(isNetworkError(err)).toBe(true);
    expect((err as Error).name).toBe('NetworkError');
    expect((err as Error).message).toMatch(/CORS/);
    expect((err as Error).message).toContain('PROPFIND https://cloud.example.com/dav');
    expect((err as Error).message).not.toContain('hasło');
    expect((err as Error).message).toContain('Failed to fetch');
    expect((err as Error).cause).toBeInstanceOf(TypeError);
  });

  it('can omit the CORS hint (native transports)', async () => {
    const http = createFetchTransport(
      fakeFetch(() => {
        throw 'error sending request for url';
      }),
      { corsHint: false },
    );
    const err = (await http({ method: 'GET', url: 'https://x/' }).catch((e: unknown) => e)) as Error;
    expect(err.name).toBe('NetworkError');
    expect(err.message).not.toMatch(/CORS/);
    expect(err.message).toContain('error sending request for url');
  });

  it('reports a body read failure as a NetworkError', async () => {
    const broken = { status: 200, headers: new Headers(), arrayBuffer: () => Promise.reject(new TypeError('network error')) } as unknown as Response;
    const http = createFetchTransport(fakeFetch(() => broken));
    await expect(http({ method: 'GET', url: 'https://x/' })).rejects.toMatchObject({ name: 'NetworkError' });
  });

  it('lets AbortError through', async () => {
    const abort = new DOMException('aborted', 'AbortError');
    const http = createFetchTransport(
      fakeFetch(() => {
        throw abort;
      }),
    );
    await expect(http({ method: 'GET', url: 'https://x/' })).rejects.toBe(abort);
  });

  it('does not wrap GET-with-body programming errors', async () => {
    const fetch = fakeFetch(() => new Response(''));
    const http = createFetchTransport(fetch);
    await expect(http({ method: 'GET', url: 'https://x/', body: 'x' })).rejects.toBeInstanceOf(TypeError);
    expect(fetch).not.toHaveBeenCalled();
  });
});

describe('createBrowserHttpTransport', () => {
  it('omits cookies and the HTTP cache', async () => {
    const fetch = fakeFetch(() => new Response('ok'));
    await createBrowserHttpTransport(fetch)({ method: 'GET', url: 'https://x/' });
    expect(fetch.mock.calls[0]![1]).toMatchObject({ credentials: 'omit', cache: 'no-store', redirect: 'follow' });
  });

  it('uses globalThis.fetch looked up per call', async () => {
    const original = globalThis.fetch;
    const fetch = vi.fn(async () => new Response('hej'));
    try {
      const http = createBrowserHttpTransport();
      globalThis.fetch = fetch as unknown as typeof globalThis.fetch;
      const res = await http({ method: 'GET', url: 'https://x/' });
      expect(dec.decode(res.body)).toBe('hej');
      expect(fetch).toHaveBeenCalledTimes(1);
    } finally {
      globalThis.fetch = original;
    }
  });
});

describe('createNetworkError', () => {
  it('builds a named error with the request in the message', () => {
    const e = createNetworkError({ method: 'mkcol', url: 'https://x/Nowy folder' }, new Error('offline'));
    expect(e.name).toBe('NetworkError');
    expect(e.message).toContain('MKCOL');
    expect(e.message).toContain('offline');
  });
});

describe('network errors as seen by the sync layer (review fixes)', () => {
  const failingWith = (e: unknown) =>
    fakeFetch(() => {
      throw e;
    });

  it('browser fetch TypeErrors stay TypeErrors, so the WebDAV client can report "cors"', async () => {
    const err = await createBrowserHttpTransport(failingWith(new TypeError('Failed to fetch')))({
      method: 'PROPFIND',
      url: 'https://chmura.example.pl/remote.php/dav/files/jan/Notatki/',
    }).catch((e: unknown) => e);
    expect(isNetworkError(err)).toBe(true);
    expect(err).toBeInstanceOf(TypeError);
    expect(classifyTransportError(err).code).toBe('cors');
  });

  it('native transports (no CORS) are classified as plain network failures', async () => {
    const err = await createFetchTransport(failingWith(new TypeError('error sending request')), { corsHint: false })({
      method: 'GET',
      url: 'https://x/',
    }).catch((e: unknown) => e);
    expect(isNetworkError(err)).toBe(true);
    expect(err).not.toBeInstanceOf(TypeError);
    expect(classifyTransportError(err).code).toBe('network');
  });

  it('a TypeError with a system error code (connection refused) is a network failure, not CORS', async () => {
    const refused = new TypeError('fetch failed', { cause: Object.assign(new Error('connect ECONNREFUSED'), { code: 'ECONNREFUSED' }) });
    const err = await createBrowserHttpTransport(failingWith(refused))({ method: 'GET', url: 'https://x/' }).catch((e: unknown) => e);
    expect(isNetworkError(err)).toBe(true);
    expect((err as Error).message).not.toMatch(/CORS/);
    expect(classifyTransportError(err).code).toBe('network');
  });

  it('a connection dropped while reading the body does not blame CORS', async () => {
    const broken = { status: 200, headers: new Headers(), arrayBuffer: () => Promise.reject(new TypeError('network error')) } as unknown as Response;
    const err = (await createBrowserHttpTransport(fakeFetch(() => broken))({ method: 'GET', url: 'https://x/' }).catch((e: unknown) => e)) as Error;
    expect(err.name).toBe('NetworkError');
    expect(err.message).not.toMatch(/CORS/);
    expect(classifyTransportError(err).code).toBe('network');
  });
});
