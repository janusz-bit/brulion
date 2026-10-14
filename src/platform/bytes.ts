/**
 * Small byte helpers shared by the platform implementations (no DOM / Tauri imports).
 */

const BASE64_CHUNK = 0x8000;

/** Standard, padded base64 of `bytes` (what the Rust side expects for `{ base64 }` bodies). */
export function bytesToBase64(bytes: Uint8Array): string {
  let binary = '';
  for (let i = 0; i < bytes.length; i += BASE64_CHUNK) {
    binary += String.fromCharCode(...bytes.subarray(i, i + BASE64_CHUNK));
  }
  return btoa(binary);
}

/** Inverse of {@link bytesToBase64}. Throws on malformed input. */
export function base64ToBytes(b64: string): Uint8Array {
  const binary = atob(b64);
  const out = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) out[i] = binary.charCodeAt(i);
  return out;
}

/**
 * View `data` as an ArrayBuffer-backed Uint8Array (required by `Blob`, `fetch` bodies in
 * TypeScript's DOM types). Copies only when the bytes live in a SharedArrayBuffer.
 */
export function toArrayBufferView(data: Uint8Array): Uint8Array<ArrayBuffer> {
  return data.buffer instanceof ArrayBuffer ? (data as Uint8Array<ArrayBuffer>) : new Uint8Array(data);
}

/**
 * Normalise a binary IPC/HTTP result to a Uint8Array: ArrayBuffer (Tauri raw responses),
 * any ArrayBufferView, or a plain number array (JSON fallback). Returns null for anything else.
 */
export function toBytes(value: unknown): Uint8Array | null {
  if (value instanceof Uint8Array) return value;
  // Tag check: an ArrayBuffer from another realm (iframe, test environment) fails instanceof.
  if (value instanceof ArrayBuffer || Object.prototype.toString.call(value) === '[object ArrayBuffer]') {
    return new Uint8Array(value as ArrayBuffer);
  }
  if (ArrayBuffer.isView(value)) return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
  if (Array.isArray(value) && value.every((n) => Number.isInteger(n) && n >= 0 && n <= 255)) {
    return Uint8Array.from(value as number[]);
  }
  return null;
}
