/**
 * Minimal PNG crop (node:zlib only, no dependencies) used to cut tiles and sections out of one
 * full-page screenshot instead of taking many screenshots (slow on the Raspberry Pi: every
 * full-page capture re-lays out and rasterises the whole page).
 *
 * Supports what Chromium produces: 8-bit RGB / RGBA, non-interlaced. decodePng() returns null for
 * anything else; callers then keep only the full-page image.
 */
import { crc32, deflateSync, inflateSync } from 'node:zlib';

export interface RawImage {
  width: number;
  height: number;
  /** Bytes per pixel: 3 (RGB) or 4 (RGBA). */
  bpp: 3 | 4;
  /** Unfiltered pixel rows, `width * bpp` bytes each. */
  data: Uint8Array;
}

const SIGNATURE = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

export function decodePng(png: Uint8Array): RawImage | null {
  const buf = Buffer.from(png.buffer, png.byteOffset, png.byteLength);
  if (buf.length < 8 || !buf.subarray(0, 8).equals(SIGNATURE)) return null;
  let width = 0;
  let height = 0;
  let bpp: 3 | 4 | null = null;
  const idat: Buffer[] = [];
  for (let pos = 8; pos + 8 <= buf.length; ) {
    const len = buf.readUInt32BE(pos);
    const type = buf.toString('latin1', pos + 4, pos + 8);
    const body = buf.subarray(pos + 8, pos + 8 + len);
    if (type === 'IHDR') {
      width = body.readUInt32BE(0);
      height = body.readUInt32BE(4);
      const [depth, colorType, , , interlace] = [body[8], body[9], body[10], body[11], body[12]];
      if (depth !== 8 || interlace !== 0) return null;
      bpp = colorType === 2 ? 3 : colorType === 6 ? 4 : null;
      if (!bpp) return null;
    } else if (type === 'IDAT') {
      idat.push(body);
    } else if (type === 'IEND') {
      break;
    }
    pos += 12 + len;
  }
  if (!bpp || !width || !height) return null;

  const filtered = inflateSync(Buffer.concat(idat));
  const stride = width * bpp;
  if (filtered.length < height * (stride + 1)) return null;
  const data = new Uint8Array(height * stride);
  for (let y = 0; y < height; y++) {
    const filter = filtered[y * (stride + 1)];
    const src = y * (stride + 1) + 1;
    const row = y * stride;
    const prev = row - stride; // only read when y > 0
    for (let i = 0; i < stride; i++) {
      const raw = filtered[src + i]!;
      const a = i >= bpp ? data[row + i - bpp]! : 0;
      const b = y > 0 ? data[prev + i]! : 0;
      let v: number;
      switch (filter) {
        case 0:
          v = raw;
          break;
        case 1:
          v = raw + a;
          break;
        case 2:
          v = raw + b;
          break;
        case 3:
          v = raw + ((a + b) >> 1);
          break;
        case 4: {
          const c = i >= bpp && y > 0 ? data[prev + i - bpp]! : 0;
          const p = a + b - c;
          const pa = Math.abs(p - a);
          const pb = Math.abs(p - b);
          const pc = Math.abs(p - c);
          v = raw + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
          break;
        }
        default:
          return null;
      }
      data[row + i] = v & 0xff;
    }
  }
  return { width, height, bpp, data };
}

function chunk(type: string, body: Uint8Array): Buffer {
  const head = Buffer.alloc(8);
  head.writeUInt32BE(body.length, 0);
  head.write(type, 4, 'latin1');
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body, crc32(head.subarray(4, 8))) >>> 0, 0);
  return Buffer.concat([head, body, crc]);
}

/** Encodes the rectangle (clamped to the image) as a PNG; null if it is empty. */
export function cropPng(img: RawImage, x: number, y: number, w: number, h: number): Buffer | null {
  const x0 = Math.max(0, Math.min(img.width, Math.round(x)));
  const y0 = Math.max(0, Math.min(img.height, Math.round(y)));
  const x1 = Math.max(x0, Math.min(img.width, Math.round(x + w)));
  const y1 = Math.max(y0, Math.min(img.height, Math.round(y + h)));
  const cw = x1 - x0;
  const ch = y1 - y0;
  if (cw === 0 || ch === 0) return null;

  const stride = img.width * img.bpp;
  const out = new Uint8Array(ch * (cw * img.bpp + 1));
  for (let r = 0; r < ch; r++) {
    const dst = r * (cw * img.bpp + 1);
    out[dst] = 0; // filter: none
    const src = (y0 + r) * stride + x0 * img.bpp;
    out.set(img.data.subarray(src, src + cw * img.bpp), dst + 1);
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(cw, 0);
  ihdr.writeUInt32BE(ch, 4);
  ihdr[8] = 8;
  ihdr[9] = img.bpp === 4 ? 6 : 2;
  return Buffer.concat([
    SIGNATURE,
    chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(out, { level: 6 })),
    chunk('IEND', new Uint8Array(0)),
  ]);
}
