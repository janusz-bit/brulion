import { describe, expect, it } from 'vitest';
import { isWebDavError } from './errors';
import { parseMultistatus, parseStatusLine, PROPFIND_BODY } from './xml';

const NEXTCLOUD = `<?xml version="1.0"?>
<d:multistatus xmlns:d="DAV:" xmlns:s="http://sabredav.org/ns" xmlns:oc="http://owncloud.org/ns" xmlns:nc="http://nextcloud.org/ns">
 <d:response>
  <d:href>/remote.php/dav/files/jan/Notatki/</d:href>
  <d:propstat>
   <d:prop>
    <d:getlastmodified>Tue, 14 Oct 2026 08:00:00 GMT</d:getlastmodified>
    <d:resourcetype><d:collection/></d:resourcetype>
    <d:getetag>&quot;5f1a&quot;</d:getetag>
   </d:prop>
   <d:status>HTTP/1.1 200 OK</d:status>
  </d:propstat>
  <d:propstat>
   <d:prop><d:getcontentlength/></d:prop>
   <d:status>HTTP/1.1 404 Not Found</d:status>
  </d:propstat>
 </d:response>
 <d:response>
  <d:href>/remote.php/dav/files/jan/Notatki/Za%C5%BC%C3%B3%C5%82%C4%87%20g%C4%99%C5%9Bl%C4%85.md</d:href>
  <d:propstat>
   <d:prop>
    <d:getlastmodified>Tue, 14 Oct 2026 09:30:00 GMT</d:getlastmodified>
    <d:getcontentlength>1234</d:getcontentlength>
    <d:resourcetype/>
    <d:getetag>"0012ab"</d:getetag>
   </d:prop>
   <d:status>HTTP/1.1 200 OK</d:status>
  </d:propstat>
 </d:response>
</d:multistatus>`;

describe('parseMultistatus', () => {
  it('parses Nextcloud-style responses with the d: prefix', () => {
    const r = parseMultistatus(NEXTCLOUD);
    expect(r).toEqual([
      { href: '/remote.php/dav/files/jan/Notatki/', isCollection: true, etag: '5f1a', mtime: Date.UTC(2026, 9, 14, 8, 0, 0), size: 0 },
      {
        href: '/remote.php/dav/files/jan/Notatki/Za%C5%BC%C3%B3%C5%82%C4%87%20g%C4%99%C5%9Bl%C4%85.md',
        isCollection: false,
        etag: '0012ab',
        mtime: Date.UTC(2026, 9, 14, 9, 30, 0),
        size: 1234,
      },
    ]);
  });

  it('accepts the D: prefix (Apache mod_dav) and mixed prefixes', () => {
    const xml = `<?xml version="1.0" encoding="utf-8"?>
<D:multistatus xmlns:D="DAV:" xmlns:lp1="DAV:">
<D:response><D:href>http://h/dav/a.md</D:href><D:propstat><D:prop>
<lp1:getlastmodified>Wed, 15 Oct 2026 10:00:00 GMT</lp1:getlastmodified><lp1:getcontentlength>5</lp1:getcontentlength>
<lp1:resourcetype/><lp1:getetag>W/"77-abc"</lp1:getetag></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response>
</D:multistatus>`;
    expect(parseMultistatus(xml)).toEqual([{ href: 'http://h/dav/a.md', isCollection: false, etag: '77-abc', mtime: Date.UTC(2026, 9, 15, 10), size: 5 }]);
  });

  it('accepts a default namespace without prefix', () => {
    const xml = `<multistatus xmlns="DAV:"><response><href>/dav/x/</href><propstat><prop><resourcetype><collection></collection></resourcetype></prop><status>HTTP/1.1 200 OK</status></propstat></response></multistatus>`;
    expect(parseMultistatus(xml)).toEqual([{ href: '/dav/x/', isCollection: true, etag: '', mtime: 0, size: 0 }]);
  });

  it('keeps numeric-looking etags as strings', () => {
    const xml = `<d:multistatus xmlns:d="DAV:"><d:response><d:href>/a</d:href><d:propstat><d:prop><d:getetag>00123</d:getetag><d:resourcetype/></d:prop></d:propstat></d:response></d:multistatus>`;
    expect(parseMultistatus(xml)[0]?.etag).toBe('00123');
  });

  it('skips responses with an error status and ignores failed propstats', () => {
    const xml = `<d:multistatus xmlns:d="DAV:">
<d:response><d:href>/gone</d:href><d:status>HTTP/1.1 404 Not Found</d:status></d:response>
<d:response><d:href>/ok</d:href><d:propstat><d:prop><d:getetag>"x"</d:getetag></d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>
<d:propstat><d:prop><d:getcontentlength>99</d:getcontentlength></d:prop><d:status>HTTP/1.1 403 Forbidden</d:status></d:propstat></d:response>
</d:multistatus>`;
    expect(parseMultistatus(xml)).toEqual([{ href: '/ok', isCollection: false, etag: 'x', mtime: 0, size: 0 }]);
  });

  it('tolerates garbage values (bad dates, negative sizes) and missing hrefs', () => {
    const xml = `<d:multistatus xmlns:d="DAV:">
<d:response><d:propstat><d:prop><d:getetag>"x"</d:getetag></d:prop></d:propstat></d:response>
<d:response><d:href>/a</d:href><d:propstat><d:prop><d:getlastmodified>wczoraj</d:getlastmodified><d:getcontentlength>-5</d:getcontentlength></d:prop></d:propstat></d:response>
</d:multistatus>`;
    expect(parseMultistatus(xml)).toEqual([{ href: '/a', isCollection: false, etag: '', mtime: 0, size: 0 }]);
  });

  it('returns [] for an empty multistatus', () => {
    expect(parseMultistatus('<d:multistatus xmlns:d="DAV:"></d:multistatus>')).toEqual([]);
    expect(parseMultistatus('<d:multistatus xmlns:d="DAV:"/>')).toEqual([]);
  });

  it('throws a parse error for non-multistatus documents', () => {
    for (const bad of ['', '<html><body>Login</body></html>', 'not xml at all', '<a><b></a>']) {
      let err: unknown;
      try {
        parseMultistatus(bad);
      } catch (e) {
        err = e;
      }
      expect(isWebDavError(err, 'parse')).toBe(true);
    }
  });
});

describe('helpers', () => {
  it('parseStatusLine extracts the code', () => {
    expect(parseStatusLine('HTTP/1.1 207 Multi-Status')).toBe(207);
    expect(parseStatusLine('garbage')).toBeUndefined();
  });

  it('PROPFIND_BODY requests the needed properties', () => {
    for (const p of ['getetag', 'getlastmodified', 'getcontentlength', 'resourcetype']) expect(PROPFIND_BODY).toContain(`<d:${p}/>`);
  });
});
