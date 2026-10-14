import { describe, expect, it } from 'vitest';
import {
  base64Encode,
  basicAuth,
  decodeSegment,
  encodePath,
  getHeader,
  hrefPathname,
  joinUrl,
  nextcloudDavUrl,
  normalizeEtag,
  quoteEtag,
  relativeTo,
  urlPathname,
} from './url';

describe('nextcloudDavUrl', () => {
  it('builds the files URL from host, user and folder', () => {
    expect(nextcloudDavUrl('https://cloud.example.com', 'jan', 'Notatki')).toBe('https://cloud.example.com/remote.php/dav/files/jan/Notatki');
  });

  it('defaults to https and trims trailing slashes', () => {
    expect(nextcloudDavUrl('cloud.example.com/', 'jan', 'Notatki/')).toBe('https://cloud.example.com/remote.php/dav/files/jan/Notatki');
    expect(nextcloudDavUrl('  cloud.example.com///  ', 'jan', '/Notatki')).toBe('https://cloud.example.com/remote.php/dav/files/jan/Notatki');
  });

  it('keeps an explicit http scheme, port and sub-path', () => {
    expect(nextcloudDavUrl('http://192.168.1.10:8080/nextcloud/', 'jan', 'N')).toBe('http://192.168.1.10:8080/nextcloud/remote.php/dav/files/jan/N');
  });

  it('handles nested folders and encodes every segment (Polish diacritics, spaces)', () => {
    expect(nextcloudDavUrl('cloud.example.com', 'jan', 'Dokumenty/Moje notatki/Zażółć')).toBe(
      'https://cloud.example.com/remote.php/dav/files/jan/Dokumenty/Moje%20notatki/Za%C5%BC%C3%B3%C5%82%C4%87',
    );
  });

  it('encodes the user name (e-mail logins, spaces)', () => {
    expect(nextcloudDavUrl('cloud.example.com', 'jan.kowalski@poczta.pl', 'N')).toBe('https://cloud.example.com/remote.php/dav/files/jan.kowalski%40poczta.pl/N');
    expect(nextcloudDavUrl('cloud.example.com', 'Łukasz Ż', 'N')).toBe('https://cloud.example.com/remote.php/dav/files/%C5%81ukasz%20%C5%BB/N');
  });

  it('works without a folder (whole user storage)', () => {
    expect(nextcloudDavUrl('cloud.example.com', 'jan', '')).toBe('https://cloud.example.com/remote.php/dav/files/jan');
    expect(nextcloudDavUrl('cloud.example.com', 'jan', '/')).toBe('https://cloud.example.com/remote.php/dav/files/jan');
  });

  it('accepts a pasted DAV or web UI URL as the server', () => {
    expect(nextcloudDavUrl('https://cloud.example.com/remote.php/dav/files/jan/', 'jan', 'N')).toBe('https://cloud.example.com/remote.php/dav/files/jan/N');
    expect(nextcloudDavUrl('https://cloud.example.com/nc/index.php/apps/files/?dir=/', 'jan', 'N')).toBe('https://cloud.example.com/nc/remote.php/dav/files/jan/N');
    expect(nextcloudDavUrl('https://cloud.example.com/apps/files/', 'jan', 'N')).toBe('https://cloud.example.com/remote.php/dav/files/jan/N');
  });

  it('normalises dot segments in the folder', () => {
    expect(nextcloudDavUrl('h.pl', 'jan', './a/../b//c')).toBe('https://h.pl/remote.php/dav/files/jan/b/c');
  });
});

describe('path encoding', () => {
  it('encodePath encodes segments and drops empty ones', () => {
    expect(encodePath('a b/ż#?.md')).toBe('a%20b/%C5%BC%23%3F.md');
    expect(encodePath('/a//b/')).toBe('a/b');
    expect(encodePath('')).toBe('');
  });

  it('joinUrl joins base and path, optional trailing slash', () => {
    expect(joinUrl('https://h/dav/', 'Zeszyt/Notatka.md')).toBe('https://h/dav/Zeszyt/Notatka.md');
    expect(joinUrl('https://h/dav', '', true)).toBe('https://h/dav/');
    expect(joinUrl('https://h/dav', 'folder', true)).toBe('https://h/dav/folder/');
  });

  it('decodeSegment keeps malformed escapes', () => {
    expect(decodeSegment('%C5%BC')).toBe('ż');
    expect(decodeSegment('100%')).toBe('100%');
  });
});

describe('href handling', () => {
  it('urlPathname decodes full URLs and absolute paths', () => {
    expect(urlPathname('https://h/a%20b/%C5%BC.md?x=1')).toBe('/a b/ż.md');
    expect(urlPathname('/a%20b/')).toBe('/a b/');
  });

  it('hrefPathname resolves relative hrefs against the request URL', () => {
    expect(hrefPathname('notatka.md', 'https://h/dav/folder/')).toBe('/dav/folder/notatka.md');
    expect(hrefPathname('https://other/dav/x.md', 'https://h/dav/')).toBe('/dav/x.md');
    expect(hrefPathname('/dav/%C5%BC/', 'https://h/dav/')).toBe('/dav/ż/');
  });

  it('relativeTo strips the base, handles trailing/double slashes and case', () => {
    expect(relativeTo('/dav/files/jan/N/a.md', '/dav/files/jan/N')).toBe('a.md');
    expect(relativeTo('/dav/files/jan/N/', '/dav/files/jan/N')).toBe('');
    expect(relativeTo('/dav/files/jan/N//sub/', '/dav/files/jan/N/')).toBe('sub');
    expect(relativeTo('/DAV/files/jan/N/a.md', '/dav/files/jan/N')).toBe('a.md');
    expect(relativeTo('/dav/files/jan/Other/a.md', '/dav/files/jan/N')).toBeNull();
    expect(relativeTo('/dav/files/jan/N/../x', '/dav/files/jan/N')).toBeNull();
  });

  it('relativeTo compares NFC and NFD names equally', () => {
    const nfd = '/dav/Zażół/a.md'.normalize('NFD');
    expect(nfd).not.toBe(nfd.normalize('NFC'));
    expect(relativeTo(nfd, '/dav/Zażół'.normalize('NFC'))).toBe('a.md');
    expect(relativeTo('/dav/Zażół/b.md'.normalize('NFC'), '/dav/Zażół'.normalize('NFD'))).toBe('b.md');
  });
});

describe('headers, etags, auth', () => {
  it('getHeader is case-insensitive', () => {
    expect(getHeader({ ETag: '"x"' }, 'etag')).toBe('"x"');
    expect(getHeader({ etag: '"y"' }, 'ETag')).toBe('"y"');
    expect(getHeader({}, 'etag')).toBeUndefined();
    expect(getHeader(undefined, 'etag')).toBeUndefined();
  });

  it('normalizeEtag drops quotes, W/ and entities', () => {
    expect(normalizeEtag('"abc"')).toBe('abc');
    expect(normalizeEtag('W/"abc"')).toBe('abc');
    expect(normalizeEtag(' &quot;abc&quot; ')).toBe('abc');
    expect(normalizeEtag(undefined)).toBe('');
    expect(normalizeEtag('plain')).toBe('plain');
  });

  it('quoteEtag produces the If-Match form', () => {
    expect(quoteEtag('abc')).toBe('"abc"');
    expect(quoteEtag('"abc"')).toBe('"abc"');
  });

  it('base64Encode matches Buffer for all padding cases', () => {
    for (const s of ['', 'a', 'ab', 'abc', 'abcd', 'Zażółć gęślą jaźń', '\u0000ÿ']) {
      const bytes = new TextEncoder().encode(s);
      expect(base64Encode(bytes)).toBe(Buffer.from(bytes).toString('base64'));
    }
  });

  it('basicAuth is UTF-8 safe', () => {
    expect(basicAuth('jan', 'hasło')).toBe(`Basic ${Buffer.from('jan:hasło', 'utf8').toString('base64')}`);
  });
});
