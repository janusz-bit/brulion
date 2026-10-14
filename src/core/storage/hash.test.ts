import { describe, expect, it } from 'vitest';
import { contentHash, decodeUtf8, encodeUtf8, hashBytes, utf8Length } from './hash';

/** Reference FNV-1a 64 using BigInt. */
function referenceFnv1a64(bytes: Uint8Array): string {
  let h = 0xcbf29ce484222325n;
  const prime = 0x100000001b3n;
  const mask = (1n << 64n) - 1n;
  for (const b of bytes) {
    h ^= BigInt(b);
    h = (h * prime) & mask;
  }
  return h.toString(16).padStart(16, '0');
}

describe('contentHash', () => {
  it('matches published FNV-1a 64 test vectors', () => {
    expect(contentHash('')).toBe('cbf29ce484222325');
    expect(contentHash('a')).toBe('af63dc4c8601ec8c');
    expect(contentHash('foobar')).toBe('85944171f73967e8');
  });

  it('matches a BigInt reference implementation', () => {
    const samples = [
      'Zażółć gęślą jaźń',
      'ĄĆĘŁŃÓŚŹŻ ąćęłńóśźż',
      '- [ ] Zadzwonić do mamy ⏰ 2026-10-14 18:00 🔁 every week',
      '😀👍🏽 emoji + 漢字',
      '\u0000ÿĀ￿',
      'x'.repeat(10_000),
    ];
    for (const s of samples) expect(contentHash(s)).toBe(referenceFnv1a64(new TextEncoder().encode(s)));
    const random = new Uint8Array(4096);
    for (let i = 0; i < random.length; i++) random[i] = (i * 7919 + 13) & 0xff;
    expect(hashBytes(random)).toBe(referenceFnv1a64(random));
  });

  it('hashes strings over their UTF-8 bytes', () => {
    const s = 'Łódź — źdźbło';
    expect(contentHash(s)).toBe(contentHash(new TextEncoder().encode(s)));
  });

  it('is always 16 lower-case hex characters', () => {
    for (const s of ['', 'a', 'b', 'ą', 'longer text '.repeat(50)]) expect(contentHash(s)).toMatch(/^[0-9a-f]{16}$/);
  });

  it('distinguishes small changes, NFC vs NFD and line endings', () => {
    expect(contentHash('notatka')).not.toBe(contentHash('notatkа')); // Cyrillic а
    expect(contentHash('ż'.normalize('NFC'))).not.toBe(contentHash('ż'.normalize('NFD')));
    expect(contentHash('a\nb')).not.toBe(contentHash('a\r\nb'));
  });

  it('is fast enough for large notes', () => {
    const big = 'Lorem ipsum ąę '.repeat(70_000); // ~1 MB
    const t = performance.now();
    contentHash(big);
    expect(performance.now() - t).toBeLessThan(3000);
  });
});

describe('utf8Length', () => {
  it('counts bytes like TextEncoder', () => {
    for (const s of ['', 'abc', 'zażółć', '€', '😀', 'a😀b', '漢字', 'ĄĆĘŁŃÓŚŹŻ']) {
      expect(utf8Length(s)).toBe(new TextEncoder().encode(s).byteLength);
    }
  });

  it('counts lone surrogates as U+FFFD (3 bytes)', () => {
    expect(utf8Length('\ud800')).toBe(3);
    expect(utf8Length('a\udc00b')).toBe(5);
    expect(utf8Length('\ud83d')).toBe(new TextEncoder().encode('\ud83d').byteLength);
  });
});

describe('encodeUtf8 / decodeUtf8', () => {
  it('round-trips Polish text', () => {
    const s = 'Gęś ćwierka nad łąką';
    expect(decodeUtf8(encodeUtf8(s))).toBe(s);
  });

  it('replaces invalid sequences and drops a BOM', () => {
    expect(decodeUtf8(new Uint8Array([0xff, 0x61]))).toBe('�a');
    expect(decodeUtf8(new Uint8Array([0xef, 0xbb, 0xbf, 0x61]))).toBe('a');
  });
});
