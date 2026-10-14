import { describe, expect, it } from 'vitest';
import { isPlatformError, PlatformError } from './errors';
import { checkExternalUrl, redactUrl } from './url';

describe('checkExternalUrl', () => {
  it('allows web, mail, phone and app links', () => {
    expect(checkExternalUrl('https://example.com/a?b=1')).toBe('https://example.com/a?b=1');
    expect(checkExternalUrl('  http://example.com ')).toBe('http://example.com/');
    expect(checkExternalUrl('mailto:jan@example.com')).toBe('mailto:jan@example.com');
    expect(checkExternalUrl('tel:+48123456789')).toBe('tel:+48123456789');
    expect(checkExternalUrl('obsidian://open?vault=Notatki')).toBe('obsidian://open?vault=Notatki');
  });

  it('percent-encodes Polish characters in the URL', () => {
    expect(checkExternalUrl('https://pl.wikipedia.org/wiki/Łódź')).toBe('https://pl.wikipedia.org/wiki/%C5%81%C3%B3d%C5%BA');
  });

  it('refuses script-capable and local schemes', () => {
    for (const url of ['javascript:alert(1)', 'JAVASCRIPT:alert(1)', ' javascript:alert(1)', 'data:text/html,<b>x</b>', 'file:///etc/passwd', 'vbscript:x', 'blob:https://x/1']) {
      let err: unknown;
      try {
        checkExternalUrl(url);
      } catch (e) {
        err = e;
      }
      expect(isPlatformError(err, 'invalid-url'), url).toBe(true);
    }
  });

  it('refuses relative or malformed URLs', () => {
    expect(() => checkExternalUrl('notatki/strona.md')).toThrow(PlatformError);
    expect(() => checkExternalUrl('')).toThrow(PlatformError);
    expect(() => checkExternalUrl('http://')).toThrow(PlatformError);
  });
});

describe('redactUrl', () => {
  it('removes credentials', () => {
    expect(redactUrl('https://jan:tajne@cloud.example.com/remote.php/dav')).toBe('https://cloud.example.com/remote.php/dav');
    expect(redactUrl('https://cloud.example.com/x')).toBe('https://cloud.example.com/x');
  });

  it('handles unparsable input', () => {
    expect(redactUrl('//jan:tajne@host/x')).toBe('//host/x');
    expect(redactUrl('nic')).toBe('nic');
  });
});
