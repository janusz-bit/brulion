#!/usr/bin/env node
// The browser build (dist-wasm/) shows a drawing's embed in the app's theme: the drawing image
// provider (src/app/drawing/drawingimageprovider.h) runs on the GUI thread there and reads the
// notebook from IDBFS. Chromium (headless, over the DevTools protocol — the minimal client of
// svg-theme-check.mjs; Node >= 22) gets a notebook written into IndexedDB as the app's IDBFS mount
// stores it (as scripts/wasm-smoke.mjs --copy does): today's journal embedding `Rysunek.svg` (the
// golden drawing of tst_drawing, "Jak motyw": an ink stroke and a highlighter stroke on lined
// paper), and settings.ini with the app's theme (`ui/themeMode`) light, then dark. Once the app is
// up, a screenshot is decoded in a page of its own (Qt's WebGL canvas cannot be read back): the
// drawing's paper — #ffffff / #121212, the area where most of its pixels are — must hold the ink
// in the theme's colour, #1f2430 / #caced8 (± 2), and no paper of the other theme anywhere.
//
//   python3 -m http.server 8123 --directory <a folder with dist-wasm/ as brulion/> &
//   node tests/app/drawing/wasm-drawing-check.mjs --url http://127.0.0.1:8123/brulion/ \
//       [--themes light,dark] [--shots <dir>] [--timeout 120]
//
// Chromium: $CHROME or chromium/chromium-browser/google-chrome on PATH; BRULION_CHROME_NO_SANDBOX=1
// (or CI=true) where its sandbox cannot start. Exit code 0 = every check passed.
import { spawn, execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { setTimeout as sleep } from 'node:timers/promises';

const here = dirname(fileURLToPath(import.meta.url));

function parseArgs(argv) {
  const args = { themes: ['light', 'dark'], timeout: 120, drawing: join(here, '../../data/drawing/auto_theme.svg') };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    const next = () => {
      if (i + 1 >= argv.length) throw new Error(`${a} needs a value`);
      return argv[++i];
    };
    if (a === '--url') args.url = next();
    else if (a === '--themes') args.themes = next().split(',');
    else if (a === '--shots') args.shots = next();
    else if (a === '--timeout') args.timeout = Number(next());
    else if (a === '--drawing') args.drawing = next();
    else throw new Error(`unknown option ${a}`);
  }
  if (!args.url) throw new Error('--url is required');
  return args;
}

const colours = {
  light: { paper: [0xff, 0xff, 0xff], ink: [0x1f, 0x24, 0x30] },
  dark: { paper: [0x12, 0x12, 0x12], ink: [0xca, 0xce, 0xd8] },
};

// The notebook and the settings written into IndexedDB the way Emscripten's IDBFS stores the
// app's mounts (src/platform/wasm/wasmstorage.h; scripts/wasm-smoke.mjs seedVaultScript): one
// database per mount point (version 21, store FILE_DATA keyed by absolute path, index
// "timestamp"; files {timestamp, mode, contents}, folders {timestamp, mode}). Today's journal by
// the browser's local date (the app's clock). Runs in a page of the site's origin.
function seedScript(drawingBase64, theme) {
  return `(async () => {
    const root = '/home/web_user/Brulion';
    const appData = '/home/web_user/.local/share/Brulion/Brulion';
    const enc = new TextEncoder();
    const timestamp = new Date();
    const now = new Date();
    const day = now.getFullYear() + '-' + String(now.getMonth() + 1).padStart(2, '0') + '-'
      + String(now.getDate()).padStart(2, '0');
    const drawing = Uint8Array.from(atob(${JSON.stringify(drawingBase64)}), (c) => c.charCodeAt(0));
    const entries = [
      [root + '/journals', { timestamp, mode: 16877 }],
      [root + '/journals/' + day + '.md', { timestamp, mode: 33188,
        contents: enc.encode('- Notatki z wykładu — zażółć gęślą jaźń: ![[Rysunek.svg]]\\n') }],
      [root + '/Rysunek.svg', { timestamp, mode: 33188, contents: drawing }],
    ];
    const put = (dbName, rows) => new Promise((resolve, reject) => {
      const req = indexedDB.open(dbName, 21);
      req.onupgradeneeded = () => {
        const store = req.result.createObjectStore('FILE_DATA');
        store.createIndex('timestamp', 'timestamp', { unique: false });
      };
      req.onerror = () => reject(req.error);
      req.onsuccess = () => {
        const tx = req.result.transaction(['FILE_DATA'], 'readwrite');
        for (const [key, value] of rows) tx.objectStore('FILE_DATA').put(value, key);
        tx.oncomplete = () => { req.result.close(); resolve(rows.length); };
        tx.onerror = () => reject(tx.error);
      };
    });
    const settings = enc.encode('[ui]\\nthemeMode=${theme}\\n\\n[vaults]\\ncurrent=' + root + '\\n');
    await put(root, entries);
    await put(appData, [[appData + '/settings.ini', { timestamp, mode: 33188, contents: settings }]]);
    return entries.length;
  })()`;
}

// In a page of its own: the screenshot's pixels — where each theme's paper is, and the ink on it.
function analyseScript(pngBase64, want, other) {
  return `(async () => {
    const blob = await (await fetch('data:image/png;base64,${pngBase64}')).blob();
    const bitmap = await createImageBitmap(blob);
    const c = new OffscreenCanvas(bitmap.width, bitmap.height);
    const g = c.getContext('2d');
    g.drawImage(bitmap, 0, 0);
    const { data, width, height } = g.getImageData(0, 0, bitmap.width, bitmap.height);
    const near = (i, colour) => Math.abs(data[i] - colour[0]) <= 2 && Math.abs(data[i + 1] - colour[1]) <= 2
      && Math.abs(data[i + 2] - colour[2]) <= 2;
    const want = ${JSON.stringify(want)};
    const other = ${JSON.stringify(other)};
    // The paper: the rows and columns with at least 40 of its pixels (a picture, not a stray
    // pixel of the interface).
    const rows = new Array(height).fill(0);
    const cols = new Array(width).fill(0);
    let otherPaper = 0;
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const i = (y * width + x) * 4;
        if (near(i, want.paper)) { rows[y]++; cols[x]++; }
        if (near(i, other.paper)) otherPaper++;
      }
    }
    const firstLast = (counts) => {
      let a = -1, b = -1;
      counts.forEach((n, i) => { if (n >= 40) { if (a < 0) a = i; b = i; } });
      return [a, b];
    };
    const [top, bottom] = firstLast(rows);
    const [left, right] = firstLast(cols);
    let ink = 0;
    for (let y = Math.max(0, top); y <= bottom; y++) {
      for (let x = Math.max(0, left); x <= right; x++) {
        if (near((y * width + x) * 4, want.ink)) ink++;
      }
    }
    return { width, height, box: [left, top, right, bottom], ink, otherPaper };
  })()`;
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
  constructor(url) {
    this.ws = new WebSocket(url);
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

/// One run of the app in a fresh browser profile with the app's theme `theme`.
async function checkTheme(args, theme, problems) {
  const profile = mkdtempSync(join(tmpdir(), 'brulion-wasm-drawing-'));
  const noSandbox = process.env.BRULION_CHROME_NO_SANDBOX === '1' || process.env.CI === 'true';
  const chrome = spawn(findChrome(), [
    '--headless=new',
    `--user-data-dir=${profile}`,
    '--remote-debugging-port=0',
    '--no-first-run',
    '--no-default-browser-check',
    '--disable-background-networking',
    '--disable-component-update',
    '--use-angle=swiftshader',
    '--enable-unsafe-swiftshader',
    '--ignore-gpu-blocklist',
    '--window-size=1280,800',
    '--hide-scrollbars',
    '--mute-audio',
    '--lang=pl-PL',
    ...(noSandbox ? ['--no-sandbox'] : []),
    'about:blank',
  ], { stdio: ['ignore', 'ignore', 'pipe'] });
  let chromeLog = '';
  chrome.stderr.on('data', (d) => {
    chromeLog += d;
  });
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
    const navigate = async (url) => {
      await cdp.send('Page.navigate', { url });
      for (const end = Date.now() + 30000; Date.now() < end; await sleep(200)) {
        if ((await evaluate('location.href + " " + document.readyState').catch(() => '')) === `${url} complete`) return;
      }
      throw new Error(`${url} did not load`);
    };

    // Any document of the site's origin can write its IndexedDB; the logo does not start the app.
    const site = new URL(args.url);
    await navigate(new URL('icons/logo.svg', site).href);
    const drawing = readFileSync(args.drawing).toString('base64');
    const written = await evaluate(seedScript(drawing, theme));
    console.log(`${theme}: notebook written into IndexedDB (${written} entries)`);

    await cdp.send('Page.navigate', { url: site.href });
    let up = false;
    for (const end = Date.now() + args.timeout * 1000; Date.now() < end && !up; await sleep(250)) {
      up = await evaluate(`!!document.getElementById('loader')?.hidden`).catch(() => false);
    }
    if (!up) {
      problems.push(`${theme}: the app did not start`);
      return;
    }
    // The journal laid out, the embed's picture loaded (the provider runs on the GUI thread).
    let result;
    for (const end = Date.now() + 30000; Date.now() < end; await sleep(1000)) {
      const shot = await cdp.send('Page.captureScreenshot', { format: 'png' });
      if (args.shots) {
        mkdirSync(args.shots, { recursive: true });
        writeFileSync(join(args.shots, `wasm-drawing-${theme}.png`), Buffer.from(shot.data, 'base64'));
      }
      // Decoded in a page of its own (the app's page keeps running meanwhile in its tab).
      const scratch = await cdp.send('Target.createTarget', { url: 'about:blank' });
      const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
      const scratchCdp = new Cdp(list.find((t) => t.id === scratch.targetId).webSocketDebuggerUrl);
      await scratchCdp.open();
      const r = await scratchCdp.send('Runtime.evaluate', {
        expression: analyseScript(shot.data, colours[theme], colours[theme === 'dark' ? 'light' : 'dark']),
        awaitPromise: true,
        returnByValue: true,
      });
      scratchCdp.close();
      await cdp.send('Target.closeTarget', { targetId: scratch.targetId });
      if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description ?? r.exceptionDetails.text);
      result = r.result.value;
      if (result.box[0] >= 0 && result.ink >= 20) break;
    }
    // (A few stray pixels of the other paper's colour may be text or icons: not a picture.)
    const ok = result.box[0] >= 0 && result.ink >= 20 && result.otherPaper < 400;
    console.log(`${ok ? 'ok' : 'FAIL'}: ${theme}: the drawing's paper ${JSON.stringify(colours[theme].paper)} at `
      + `${JSON.stringify(result.box)}, ${result.ink} ink pixels on it, ${result.otherPaper} pixels of the other paper`);
    if (!ok) problems.push(`${theme}: ${JSON.stringify(result)}`);
    cdp.close();
  } finally {
    chrome.kill();
    await sleep(500);
    rmSync(profile, { recursive: true, force: true });
  }
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const problems = [];
  for (const theme of args.themes) {
    if (!colours[theme]) throw new Error(`unknown theme ${theme}`);
    await checkTheme(args, theme, problems);
  }
  if (problems.length) {
    console.log(`wasm-drawing-check: ${problems.length} problem(s): ${problems.join('; ')}`);
    process.exit(1);
  }
  console.log('wasm-drawing-check: passed');
}

main().catch((e) => {
  console.error(e.stack ?? String(e));
  process.exit(1);
});
