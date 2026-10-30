// Driving Brulion's web build in the browser tests: start the app with its self-tests, wait for
// their results (JavaScript globals the app publishes: src/app/diag/webselftest.h), the pen
// (Chromium's DevTools pen — trusted pointer events with pressure — and pointer events made by
// the page, in every engine), the file the browser received, pictures (PNG) and drawings
// (the scene inside Brulion's SVG).
import { expect } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { inflateSync } from 'node:zlib';
import { PNG } from 'pngjs';

/** The fixed clock of every test (?now=): the journal of 2026-10-14 is "today". */
export const NOW = '2026-10-14 10:00';
export const TODAY = '2026-10-14';

/** Fails (never skips) when the browser has no WebGL 2, which Qt for WebAssembly needs. */
export async function requireWebGl2(page, origin, browserName) {
  const response = await page.goto(`${origin}/__blank.html`).catch((e) => {
    throw new Error(`the test host names do not resolve in ${browserName} (${origin}): ${e.message}`);
  });
  expect(response?.ok(), `${origin}/__blank.html`).toBeTruthy();
  const webgl2 = await page.evaluate(() => {
    try {
      return !!document.createElement('canvas').getContext('webgl2');
    } catch {
      return false;
    }
  });
  if (!webgl2) throw new Error(`no WebGL 2 in ${browserName} — run under xvfb-run (scripts/web-test.sh)`);
}

/**
 * Collects the page's console and errors (attached to the test on failure) and the downloads it
 * starts. Call before the first navigation.
 */
export function watchPage(page) {
  const log = [];
  const errors = [];
  const downloads = [];
  page.on('console', (m) => log.push(`[${m.type()}] ${m.text()}`));
  page.on('pageerror', (e) => {
    log.push(`[pageerror] ${e.message}`);
    errors.push(e);
  });
  page.on('download', (d) => downloads.push(d));
  return { log, errors, downloads };
}

export async function attachLog(testInfo, watched) {
  await testInfo.attach('console.txt', { body: watched.log.join('\n'), contentType: 'text/plain' });
}

/**
 * Opens the app at `${origin}/brulion/` with `params` ({selftest, route, now…}; ?nosw always: the
 * service worker may refuse a self-signed site and is not what is tested) and waits until its
 * loading screen is gone. Fails at once when the page shows a problem instead.
 */
export async function openApp(page, origin, params = {}) {
  const url = new URL(`${origin}/brulion/`);
  url.searchParams.set('nosw', '');
  url.searchParams.set('now', NOW);
  for (const [k, v] of Object.entries(params)) url.searchParams.set(k, typeof v === 'string' ? v : JSON.stringify(v));
  await page.goto(url.href);
  await page.waitForFunction(() => {
    const loader = document.getElementById('loader');
    if (loader && loader.getAttribute('role') === 'alert') {
      throw new Error('the page shows a problem: ' + document.getElementById('title').textContent + ' '
        + (document.getElementById('status').textContent || '') + ' ' + (document.getElementById('details').textContent || ''));
    }
    return loader && loader.hidden;
  }, null, { timeout: 180_000, polling: 250 });
}

/** The global `name` once `pred(value)` holds (pred runs in the page). */
export async function waitGlobal(page, name, pred = '(v) => !!v', timeout = 90_000) {
  const handle = await page.waitForFunction(({ name, pred }) => {
    const v = globalThis[name];
    // eslint-disable-next-line no-new-func
    return v !== undefined && new Function('return ' + pred)()(v) ? v : undefined;
  }, { name, pred: String(pred) }, { timeout, polling: 100 });
  return handle.jsonValue();
}

/** Where Qt's screen is in the page (window coordinates of the app are relative to it). */
export async function screenOffset(page) {
  return page.evaluate(() => {
    const r = document.getElementById('screen').getBoundingClientRect();
    return { x: r.x, y: r.y };
  });
}

const sleep = (ms) => new Promise((ok) => setTimeout(ok, ms));

/**
 * Chromium: a pen over the DevTools protocol (Input.dispatchMouseEvent with pointerType "pen" and
 * `force` → trusted pointer events with pressure). button "left" (the tip) or "right" (the barrel
 * button).
 */
export async function cdpPen(page) {
  const cdp = await page.context().newCDPSession(page);
  const send = (type, x, y, extra) => cdp.send('Input.dispatchMouseEvent', {
    type, x, y, pointerType: 'pen', modifiers: 0, ...extra,
  });
  const pen = {
    hover: (x, y) => send('mouseMoved', x, y, { button: 'none', buttons: 0, force: 0 }),
    down: (x, y, force, button = 'left') => send('mousePressed', x, y,
      { button, buttons: button === 'right' ? 2 : 1, clickCount: 1, force }),
    move: (x, y, force, button = 'left') => send('mouseMoved', x, y,
      { button, buttons: button === 'right' ? 2 : 1, force }),
    up: (x, y, button = 'left') => send('mouseReleased', x, y, { button, buttons: 0, clickCount: 1, force: 0 }),
    /** Down at the first point, a move to each next one (16 ms apart: Chrome merges moves within
     *  a frame), up exactly where the last move was. points: [{x, y, p}]. */
    async stroke(points, button = 'left') {
      await pen.hover(points[0].x, points[0].y);
      await pen.down(points[0].x, points[0].y, points[0].p, button);
      for (const pt of points.slice(1)) {
        await sleep(16);
        await pen.move(pt.x, pt.y, pt.p, button);
      }
      const last = points[points.length - 1];
      await sleep(16);
      await pen.up(last.x, last.y, button);
    },
    detach: () => cdp.detach(),
  };
  return pen;
}

/**
 * Every engine: an init script (before the page loads) that lets the page make pen pointer
 * events — window.__brulionPen(type, x, y, pressure, button, buttons) dispatches a PointerEvent
 * (pointerType "pen") on Qt's window element under (x, y) in its shadow root. Such events are
 * not trusted, and Qt's setPointerCapture() of their made-up pointer id would throw
 * NotFoundError: the test page lets that one error pass.
 */
export async function installSyntheticPen(page) {
  await page.addInitScript(() => {
    for (const name of ['setPointerCapture', 'releasePointerCapture']) {
      const original = Element.prototype[name];
      Element.prototype[name] = function (id) {
        try {
          return original.call(this, id);
        } catch (e) {
          if (e && e.name === 'NotFoundError') return undefined;
          throw e;
        }
      };
    }
    window.__brulionPen = (type, x, y, pressure, button, buttons) => {
      const host = document.getElementById('qt-shadow-container');
      const root = host && host.shadowRoot;
      const hit = root && root.elementFromPoint(x, y);
      const target = hit && hit.closest('.qt-window');
      if (!target) throw new Error(`no Qt window at ${x},${y} (${hit ? hit.className : 'nothing'})`);
      target.dispatchEvent(new PointerEvent(type, {
        bubbles: true, cancelable: true, composed: true, pointerId: 31, pointerType: 'pen', isPrimary: true,
        clientX: x, clientY: y, screenX: x, screenY: y, pressure, button, buttons, width: 1, height: 1,
      }));
    };
  });
}

/** Pen strokes made by the page (installSyntheticPen). eraser: the pen's eraser end (button 5,
 *  buttons 32). Up exactly at the last move's point. */
export async function syntheticStroke(page, points, { eraser = false } = {}) {
  const button = eraser ? 5 : 0;
  const buttons = eraser ? 32 : 1;
  const first = points[0];
  await page.evaluate(([x, y]) => window.__brulionPen('pointermove', x, y, 0, -1, 0), [first.x, first.y]);
  await page.evaluate(([x, y, p, b, bs]) => window.__brulionPen('pointerdown', x, y, p, b, bs),
    [first.x, first.y, first.p, button, buttons]);
  for (const pt of points.slice(1)) {
    await sleep(16);
    await page.evaluate(([x, y, p, bs]) => window.__brulionPen('pointermove', x, y, p, -1, bs), [pt.x, pt.y, pt.p, buttons]);
  }
  const last = points[points.length - 1];
  await sleep(16);
  await page.evaluate(([x, y, b]) => window.__brulionPen('pointerup', x, y, 0, b, 0), [last.x, last.y, button]);
}

/** A mouse line (trusted in every engine). */
export async function mouseStroke(page, points) {
  await page.mouse.move(points[0].x, points[0].y);
  await page.mouse.down();
  for (const pt of points.slice(1)) {
    await sleep(16);
    await page.mouse.move(pt.x, pt.y);
  }
  await page.mouse.up();
}

/** `n` + 1 points from (x0, y0) to (x1, y1), pressure from p0 to p1 (or by `pressure(i)`). */
export function line(x0, y0, x1, y1, n, p0 = 0.5, p1 = p0, pressure = null) {
  const out = [];
  for (let i = 0; i <= n; i++) {
    const t = i / n;
    out.push({ x: x0 + (x1 - x0) * t, y: y0 + (y1 - y0) * t, p: pressure ? pressure(i) : p0 + (p1 - p0) * t });
  }
  return out;
}

/**
 * Chromium on https (File System Access): showSaveFilePicker replaced by a recorder (the app's
 * export has no user gesture, which the real picker needs); showOpenFilePicker stays, so Qt
 * takes this path. window.__brulionSaved: [{name, b64}].
 */
export async function recordSaves(page) {
  await page.addInitScript(() => {
    if (typeof window.showSaveFilePicker !== 'function') return;
    window.showSaveFilePicker = async (options) => {
      const chunks = [];
      const take = async (d) => {
        if (d && typeof d === 'object' && 'type' in d && 'data' in d && !ArrayBuffer.isView(d)) return take(d.data);
        if (d instanceof Blob) chunks.push(new Uint8Array(await d.arrayBuffer()));
        else if (ArrayBuffer.isView(d)) chunks.push(new Uint8Array(d.buffer.slice(d.byteOffset, d.byteOffset + d.byteLength)));
        else if (d instanceof ArrayBuffer) chunks.push(new Uint8Array(d.slice(0)));
        else if (typeof d === 'string') chunks.push(new TextEncoder().encode(d));
      };
      return {
        kind: 'file',
        name: options && options.suggestedName,
        createWritable: async () => ({
          write: take,
          close: async () => {
            const size = chunks.reduce((n, c) => n + c.length, 0);
            const all = new Uint8Array(size);
            let at = 0;
            for (const c of chunks) {
              all.set(c, at);
              at += c.length;
            }
            let s = '';
            for (let i = 0; i < all.length; i += 0x8000) s += String.fromCharCode.apply(null, all.subarray(i, i + 0x8000));
            (window.__brulionSaved = window.__brulionSaved || []).push({ name: options && options.suggestedName, b64: btoa(s) });
          },
        }),
      };
    };
  });
}

/** The file the page saved: from the recorder (recordSaves) or as a download. {name, bytes, how}. */
export async function savedFile(page, watched, timeout = 30_000) {
  const end = Date.now() + timeout;
  for (;;) {
    const recorded = await page.evaluate(() => (window.__brulionSaved || [])[0] || null);
    if (recorded) return { name: recorded.name, bytes: Buffer.from(recorded.b64, 'base64'), how: 'showSaveFilePicker' };
    if (watched.downloads.length) {
      const d = watched.downloads[0];
      const path = await d.path();
      return { name: d.suggestedFilename(), bytes: readFileSync(path), how: 'download' };
    }
    if (Date.now() > end) throw new Error('the browser received no file (neither showSaveFilePicker nor a download)');
    await sleep(250);
  }
}

/** The Excalidraw scene inside Brulion's drawing SVG (null: none). */
export function decodeDrawing(svg) {
  const text = Buffer.from(svg).toString('latin1');
  const m = /<!-- payload-start -->\s*([A-Za-z0-9+/=\s]+?)\s*<!-- payload-end -->/.exec(text);
  if (!m) return null;
  const wrapper = JSON.parse(Buffer.from(m[1].replace(/\s+/g, ''), 'base64').toString('latin1'));
  if (!('encoded' in wrapper)) return wrapper; // an old export: the scene itself
  const bytes = Buffer.from(wrapper.encoded, 'latin1');
  const json = wrapper.compressed ? inflateSync(bytes).toString('utf8') : bytes.toString('utf8');
  return JSON.parse(json);
}

/** A PNG (a Buffer) decoded: {width, height, data (RGBA)}. */
export function png(buffer) {
  return PNG.sync.read(buffer);
}

function luminance(img, x, y) {
  const i = (y * img.width + x) * 4;
  return 0.299 * img.data[i] + 0.587 * img.data[i + 1] + 0.114 * img.data[i + 2];
}

/** The longest run of ink (luminance < 150) in column x between y0 and y1 (max over x−1…x+1). */
export function inkThickness(img, x, y0, y1) {
  let best = 0;
  for (let cx = Math.round(x) - 1; cx <= Math.round(x) + 1; cx++) {
    let run = 0;
    for (let y = Math.max(0, Math.round(y0)); y <= Math.min(img.height - 1, Math.round(y1)); y++) {
      if (luminance(img, cx, y) < 150) {
        run++;
        best = Math.max(best, run);
      } else {
        run = 0;
      }
    }
  }
  return best;
}

/** How many pixels are within `tol` of `rgb` (in a rectangle, default all). */
export function countColour(img, rgb, tol = 8, rect = { x: 0, y: 0, width: img.width, height: img.height }) {
  let n = 0;
  for (let y = Math.max(0, rect.y); y < Math.min(img.height, rect.y + rect.height); y++) {
    for (let x = Math.max(0, rect.x); x < Math.min(img.width, rect.x + rect.width); x++) {
      const i = (y * img.width + x) * 4;
      if (Math.abs(img.data[i] - rgb[0]) <= tol && Math.abs(img.data[i + 1] - rgb[1]) <= tol
        && Math.abs(img.data[i + 2] - rgb[2]) <= tol) n++;
    }
  }
  return n;
}

/** The share of light pixels (luminance > 200) in a band of rows: paper with nothing on it. */
export function lightShare(img, x0, x1, y0, y1) {
  let light = 0;
  let all = 0;
  for (let y = Math.max(0, Math.round(y0)); y <= Math.min(img.height - 1, Math.round(y1)); y++) {
    for (let x = Math.max(0, Math.round(x0)); x <= Math.min(img.width - 1, Math.round(x1)); x++) {
      all++;
      if (luminance(img, x, y) > 200) light++;
    }
  }
  return all ? light / all : 0;
}
