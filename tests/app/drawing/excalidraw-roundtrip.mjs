#!/usr/bin/env node
// A drawing Brulion wrote taken to the real excalidraw.com (docs/SYNTAX.md §2 "Drawings": the
// elements Brulion writes are Excalidraw's own). Chromium (headless, over the DevTools protocol —
// the minimal client of svg-theme-check.mjs; Node >= 22) opens excalidraw.com and the file is
// dropped on its canvas, as a drag from a file manager does: excalidraw.com reads the scene
// embedded in the SVG. Its scene as it keeps it (localStorage "excalidraw", which excalidraw.com
// writes after every change) is then compared with the file's, element by element:
//  * every element is there, of its type, not deleted, with its id, and nothing else is;
//  * shapes, lines and arrows: their place, size, points, angle, colours, fill, stroke, roundness,
//    arrowheads, bindings and groups as Brulion wrote them (± 0.01);
//  * text: its text as typed (`originalText`), its container and the container's boundElements,
//    its font, size and alignment (its box may differ: excalidraw.com measures in its own fonts —
//    told, not failed).
// excalidraw.com's scene is written to <out.excalidraw>, for Brulion to open again
// (tst_drawingcanvas excalidrawsVersionIsEditable with BRULION_DRAWING_CHECK=<out.excalidraw>).
//
//   node tests/app/drawing/excalidraw-roundtrip.mjs <drawing.svg> <out.excalidraw> \
//       [--url https://excalidraw.com/] [--timeout 60] [--shot <file.png>]
//   tests/app/drawing/excalidraw-roundtrip.sh   # the whole round trip (sample → excalidraw.com →
//                                               # Brulion → excalidraw.com), on a machine online
//
// Needs the internet (excalidraw.com), so it is no ctest. Chromium: $CHROME or chromium /
// chromium-browser / google-chrome on PATH; BRULION_CHROME_NO_SANDBOX=1 (or CI=true) where its
// sandbox cannot start. Exit code 0 = every check passed.
import { spawn, execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { setTimeout as sleep } from 'node:timers/promises';
import { inflateSync } from 'node:zlib';

const args = process.argv.slice(2);
const option = (name, fallback) => {
  const i = args.indexOf(name);
  if (i < 0) return fallback;
  const value = args[i + 1];
  args.splice(i, 2);
  return value;
};
const url = option('--url', 'https://excalidraw.com/');
const timeoutMs = Number(option('--timeout', '60')) * 1000;
const shot = option('--shot', '');
const [svgFile, outFile] = args;
if (!svgFile || !outFile) {
  console.error('usage: excalidraw-roundtrip.mjs <drawing.svg> <out.excalidraw> [--url …] [--timeout s] [--shot png]');
  process.exit(2);
}

/// The scene embedded in a drawing file (payload version 2, compressed or not; version 1).
function sceneOf(svg) {
  const m = /<!-- payload-start -->\s*([^<]*?)\s*<!-- payload-end -->/.exec(svg);
  if (!m) throw new Error(`${svgFile}: no embedded scene`);
  const wrapper = JSON.parse(Buffer.from(m[1], 'base64').toString('latin1'));
  if (!wrapper.encoded) return wrapper; // (payload version 1: the scene itself)
  const bytes = Buffer.from(wrapper.encoded, 'latin1');
  return JSON.parse((wrapper.compressed ? inflateSync(bytes) : bytes).toString('utf8'));
}

function findChrome() {
  if (process.env.CHROME) return process.env.CHROME;
  for (const name of ['chromium', 'chromium-browser', 'google-chrome', 'google-chrome-stable']) {
    try {
      return execFileSync('sh', ['-c', `command -v ${name}`], { encoding: 'utf8' }).trim();
    } catch {
      // try the next name
    }
  }
  throw new Error('Chromium not found: set CHROME');
}

class Cdp {
  constructor(wsUrl) {
    this.ws = new WebSocket(wsUrl);
    this.nextId = 1;
    this.pending = new Map();
    this.ws.addEventListener('message', (ev) => {
      const msg = JSON.parse(ev.data);
      if (msg.id && this.pending.has(msg.id)) {
        const { resolve, reject } = this.pending.get(msg.id);
        this.pending.delete(msg.id);
        if (msg.error) reject(new Error(`${msg.error.message} (${msg.error.code})`));
        else resolve(msg.result);
      }
    });
  }
  open() {
    return new Promise((resolve, reject) => {
      this.ws.addEventListener('open', resolve, { once: true });
      this.ws.addEventListener('error', () => reject(new Error('DevTools connection failed')), { once: true });
    });
  }
  send(method, params = {}) {
    const id = this.nextId++;
    this.ws.send(JSON.stringify({ id, method, params }));
    return new Promise((resolve, reject) => this.pending.set(id, { resolve, reject }));
  }
  close() {
    this.ws.close();
  }
}

const near = (a, b) => typeof a === 'number' && typeof b === 'number' && Math.abs(a - b) <= 0.01;
const same = (a, b) => JSON.stringify(a ?? null) === JSON.stringify(b ?? null);
const ids = (list) => (list ?? []).map((b) => b.id).sort();

/// What differs between Brulion's element `b` and excalidraw.com's `x`: [failures, notes].
function compare(b, x) {
  const fail = [];
  const note = [];
  const check = (what, ok, detail) => (ok ? null : fail.push(`${what}: ${detail}`));
  check('type', b.type === x.type, `${b.type} → ${x.type}`);
  check('deleted', !x.isDeleted, 'deleted');
  const geometry = ['x', 'y', 'width', 'height', 'angle'];
  for (const key of ['strokeColor', 'backgroundColor', 'fillStyle', 'strokeWidth', 'strokeStyle', 'opacity', 'locked'])
    check(key, same(b[key], x[key]), `${JSON.stringify(b[key])} → ${JSON.stringify(x[key])}`);
  check('groupIds', same(b.groupIds ?? [], x.groupIds ?? []), `${b.groupIds} → ${x.groupIds}`);
  check('boundElements', same(ids(b.boundElements), ids(x.boundElements)),
        `${ids(b.boundElements)} → ${ids(x.boundElements)}`);
  if (b.type === 'text') {
    for (const key of ['originalText', 'fontSize', 'fontFamily', 'textAlign', 'verticalAlign', 'containerId'])
      check(key, same(b[key], x[key]), `${JSON.stringify(b[key])} → ${JSON.stringify(x[key])}`);
    for (const key of geometry) {
      if (!near(b[key], x[key])) note.push(`${key} ${b[key]} → ${x[key]} (excalidraw.com's own font metrics)`);
    }
    if (b.text !== x.text) note.push(`wrapped ${JSON.stringify(b.text)} → ${JSON.stringify(x.text)}`);
  } else {
    for (const key of geometry) check(key, near(b[key], x[key]), `${b[key]} → ${x[key]}`);
    check('roundness', same(b.roundness, x.roundness), `${JSON.stringify(b.roundness)} → ${JSON.stringify(x.roundness)}`);
  }
  if (b.points) {
    const ok = Array.isArray(x.points) && x.points.length === b.points.length
               && b.points.every((p, i) => near(p[0], x.points[i][0]) && near(p[1], x.points[i][1]));
    check('points', ok, `${b.points.length} points → ${x.points?.length}`);
  }
  if (b.type === 'arrow' || b.type === 'line') {
    for (const key of ['startArrowhead', 'endArrowhead'])
      check(key, same(b[key], x[key]), `${JSON.stringify(b[key])} → ${JSON.stringify(x[key])}`);
    check('startBinding', same(b.startBinding?.elementId, x.startBinding?.elementId), 'changed');
    check('endBinding', same(b.endBinding?.elementId, x.endBinding?.elementId), 'changed');
  }
  if (typeof x.version !== 'number' || x.version < b.version) fail.push(`version ${b.version} → ${x.version}`);
  return [fail, note];
}

async function main() {
  const svg = readFileSync(svgFile);
  const ours = sceneOf(svg.toString('latin1')).elements.filter((e) => !e.isDeleted);
  const profile = mkdtempSync(join(tmpdir(), 'brulion-excalidraw-'));
  const noSandbox = process.env.BRULION_CHROME_NO_SANDBOX === '1' || process.env.CI === 'true';
  const chrome = spawn(findChrome(), [
    '--headless=new',
    `--user-data-dir=${profile}`,
    '--remote-debugging-port=0',
    '--no-first-run',
    '--no-default-browser-check',
    '--window-size=1400,900',
    ...(noSandbox ? ['--no-sandbox'] : []),
    'about:blank',
  ], { stdio: ['ignore', 'ignore', 'pipe'] });
  let chromeLog = '';
  chrome.stderr.on('data', (d) => {
    chromeLog += d;
  });
  const problems = [];
  try {
    let port;
    for (const end = Date.now() + 30000; Date.now() < end && !port; await sleep(200)) {
      const file = join(profile, 'DevToolsActivePort');
      if (existsSync(file)) port = Number(readFileSync(file, 'utf8').split('\n')[0]);
    }
    if (!port) throw new Error(`Chromium did not start:\n${chromeLog.slice(-2000)}`);
    const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
    const cdp = new Cdp(targets.find((t) => t.type === 'page').webSocketDebuggerUrl);
    await cdp.open();
    await cdp.send('Page.enable');
    await cdp.send('Runtime.enable');
    const evaluate = async (expression) => {
      const r = await cdp.send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
      if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description ?? r.exceptionDetails.text);
      return r.result.value;
    };
    await cdp.send('Page.navigate', { url });
    const end = Date.now() + timeoutMs;
    for (;; await sleep(250)) {
      if (Date.now() > end) throw new Error(`${url}: no Excalidraw canvas`);
      if (await evaluate(`!!document.querySelector('.excalidraw canvas')`).catch(() => false)) break;
    }
    await sleep(1500); // (its fonts and the welcome screen)
    // The file dropped on the canvas, as from a file manager.
    const dropped = await evaluate(`(() => {
      const bytes = Uint8Array.from(atob(${JSON.stringify(svg.toString('base64'))}), (c) => c.charCodeAt(0));
      const file = new File([bytes], ${JSON.stringify(svgFile.split('/').pop())}, { type: 'image/svg+xml' });
      const target = document.querySelector('.excalidraw canvas.interactive') || document.querySelector('.excalidraw canvas');
      const box = target.getBoundingClientRect();
      const at = { clientX: box.left + box.width / 2, clientY: box.top + box.height / 2, bubbles: true, cancelable: true };
      for (const type of ['dragenter', 'dragover', 'drop']) {
        const dataTransfer = new DataTransfer();
        dataTransfer.items.add(file);
        target.dispatchEvent(new DragEvent(type, { ...at, dataTransfer }));
      }
      return target.className;
    })()`);
    console.log(`dropped ${svgFile} on excalidraw.com (${dropped})`);
    let theirs = [];
    for (;; await sleep(300)) {
      const stored = await evaluate(`localStorage.getItem('excalidraw')`);
      theirs = (stored ? JSON.parse(stored) : []).filter((e) => !e.isDeleted);
      if (theirs.length >= ours.length || Date.now() > end) break;
    }
    await sleep(1000); // (a last save after text was measured)
    theirs = JSON.parse((await evaluate(`localStorage.getItem('excalidraw')`)) || '[]').filter((e) => !e.isDeleted);
    if (shot) {
      const { data } = await cdp.send('Page.captureScreenshot', { format: 'png' });
      writeFileSync(shot, Buffer.from(data, 'base64'));
    }
    const byId = new Map(theirs.map((e) => [e.id, e]));
    let notes = 0;
    for (const b of ours) {
      const x = byId.get(b.id);
      if (!x) {
        problems.push(`${b.type} ${b.id}: not in excalidraw.com's scene`);
        continue;
      }
      byId.delete(b.id);
      const [fail, note] = compare(b, x);
      for (const f of fail) problems.push(`${b.type} ${b.id}: ${f}`);
      for (const n of note) {
        ++notes;
        console.log(`note: ${b.type} ${b.id}: ${n}`);
      }
    }
    for (const x of byId.values()) problems.push(`${x.type} ${x.id}: in excalidraw.com's scene, not in the file`);
    console.log(`${ours.length} elements in the file, ${theirs.length} in excalidraw.com's scene, ${notes} notes`);
    const appState = JSON.parse((await evaluate(`localStorage.getItem('excalidraw-state')`)) || '{}');
    writeFileSync(outFile, JSON.stringify({
      type: 'excalidraw',
      version: 2,
      source: url,
      elements: theirs,
      appState: { viewBackgroundColor: appState.viewBackgroundColor ?? '#ffffff', gridSize: appState.gridSize ?? null },
      files: {},
    }, null, 2));
    console.log(`excalidraw.com's scene written to ${outFile}`);
    cdp.close();
  } finally {
    chrome.kill();
    await sleep(300);
    rmSync(profile, { recursive: true, force: true });
  }
  for (const p of problems) console.log(`FAIL: ${p}`);
  if (problems.length) process.exit(1);
  console.log('ok: excalidraw.com reads every element as Brulion wrote it');
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
