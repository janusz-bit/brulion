/**
 * Fast, stable, non-cryptographic content hashing (FNV-1a 64-bit) and UTF-8 helpers.
 * Used to detect unchanged writes and to recognise the echo of our own writes in file
 * watcher events. Strings are hashed over their UTF-8 bytes, so
 * `contentHash(s) === contentHash(new TextEncoder().encode(s))`.
 */

const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8');

/** FNV-1a 64 offset basis 0xcbf29ce484222325, split into two unsigned 32-bit halves. */
const OFFSET_HI = 0xcbf29ce4;
const OFFSET_LO = 0x84222325;
/** Low part of the FNV 64-bit prime 0x100000001b3 (= 2^40 + 0x1b3). */
const PRIME_LO = 0x1b3;
const TWO_32 = 4294967296;

/** FNV-1a 64-bit hash over raw bytes, as 16 lower-case hex characters. */
export function hashBytes(bytes: Uint8Array): string {
  let hi = OFFSET_HI;
  let lo = OFFSET_LO;
  for (let i = 0; i < bytes.length; i++) {
    const x = (lo ^ bytes[i]!) >>> 0;
    // (hi:x) * (2^40 + 0x1b3) mod 2^64. All intermediate products stay below 2^53.
    const loMul = x * PRIME_LO;
    const loLow = loMul >>> 0;
    const carry = (loMul - loLow) / TWO_32;
    hi = (hi * PRIME_LO + carry + ((x << 8) >>> 0)) >>> 0;
    lo = loLow;
  }
  return hi.toString(16).padStart(8, '0') + lo.toString(16).padStart(8, '0');
}

/**
 * Stable content hash: FNV-1a 64-bit over the UTF-8 bytes of a string (or over raw bytes),
 * returned as 16 hex characters.
 */
export function contentHash(input: string | Uint8Array): string {
  return hashBytes(typeof input === 'string' ? encoder.encode(input) : input);
}

/** Number of bytes `s` occupies when encoded as UTF-8 (lone surrogates count as U+FFFD). */
export function utf8Length(s: string): number {
  let n = 0;
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    if (c < 0x80) n += 1;
    else if (c < 0x800) n += 2;
    else if (c >= 0xd800 && c <= 0xdbff && i + 1 < s.length) {
      const next = s.charCodeAt(i + 1);
      if (next >= 0xdc00 && next <= 0xdfff) {
        n += 4;
        i++;
      } else n += 3;
    } else n += 3;
  }
  return n;
}

/** Encode a string as UTF-8. */
export function encodeUtf8(s: string): Uint8Array {
  return encoder.encode(s);
}

/** Decode UTF-8 bytes (invalid sequences become U+FFFD, a leading BOM is dropped). */
export function decodeUtf8(bytes: Uint8Array): string {
  return decoder.decode(bytes);
}
