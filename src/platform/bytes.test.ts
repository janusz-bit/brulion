import { describe, expect, it } from 'vitest';
import { base64ToBytes, bytesToBase64, toArrayBufferView, toBytes } from './bytes';

const enc = new TextEncoder();

describe('base64', () => {
  it('encodes like Node Buffer (standard, padded)', () => {
    for (const s of ['', 'a', 'ab', 'abc', 'Zażółć gęślą jaźń', 'ĄĆĘŁŃÓŚŹŻ ąćęłńóśźż']) {
      const bytes = enc.encode(s);
      expect(bytesToBase64(bytes)).toBe(Buffer.from(bytes).toString('base64'));
    }
  });

  it('round-trips all byte values and large inputs (chunked)', () => {
    const all = Uint8Array.from({ length: 256 }, (_, i) => i);
    expect([...base64ToBytes(bytesToBase64(all))]).toEqual([...all]);
    // Three chunks of 0x8000 bytes (the encoder's String.fromCharCode batch size).
    const big = new Uint8Array(3 * 0x8000 + 7);
    for (let i = 0; i < big.length; i++) big[i] = (i * 31) % 256;
    const b64 = bytesToBase64(big);
    expect(b64).toBe(Buffer.from(big).toString('base64'));
    expect(Buffer.from(base64ToBytes(b64)).equals(Buffer.from(big))).toBe(true);
  });

  it('encodes a subarray view only', () => {
    const buf = Uint8Array.from([1, 2, 3, 4, 5]);
    expect(bytesToBase64(buf.subarray(1, 3))).toBe(Buffer.from([2, 3]).toString('base64'));
  });

  it('rejects malformed base64', () => {
    expect(() => base64ToBytes('@@@')).toThrow();
  });
});

describe('toBytes', () => {
  it('accepts Uint8Array, ArrayBuffer, other views and byte arrays', () => {
    const u = Uint8Array.from([1, 2]);
    expect(toBytes(u)).toBe(u);
    expect([...toBytes(Uint8Array.from([7, 8]).buffer)!]).toEqual([7, 8]);
    const dv = new DataView(Uint8Array.from([9, 10, 11]).buffer, 1, 2);
    expect([...toBytes(dv)!]).toEqual([10, 11]);
    expect([...toBytes([0, 255, 128])!]).toEqual([0, 255, 128]);
  });

  it('returns null for anything else', () => {
    expect(toBytes('abc')).toBeNull();
    expect(toBytes(null)).toBeNull();
    expect(toBytes([1, 256])).toBeNull();
    expect(toBytes([1.5])).toBeNull();
    expect(toBytes({ length: 2 })).toBeNull();
  });
});

describe('toArrayBufferView', () => {
  it('returns ArrayBuffer-backed arrays unchanged', () => {
    const u = Uint8Array.from([1, 2, 3]);
    expect(toArrayBufferView(u)).toBe(u);
  });

  it('copies SharedArrayBuffer-backed arrays', () => {
    const shared = new Uint8Array(new SharedArrayBuffer(3));
    shared.set([4, 5, 6]);
    const view = toArrayBufferView(shared);
    expect(view).not.toBe(shared);
    expect(view.buffer).toBeInstanceOf(ArrayBuffer);
    expect([...view]).toEqual([4, 5, 6]);
  });
});
