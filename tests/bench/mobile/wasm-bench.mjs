#!/usr/bin/env node
// Browser (WebAssembly) benchmark of a dist-wasm/ site in headless Chromium, dependency-free
// (DevTools protocol over the global WebSocket, Node >= 22), modelled on scripts/wasm-smoke.mjs.
//
//   node tests/bench/mobile/wasm-bench.mjs --dist dist-wasm --out results/ [--label cur]
//       [--loads 5] [--visits first|repeat|both] [--encoding none|gzip|br] [--throttle KBPS,RTT_MS]
//       [--vault DIR] [--idbfs] [--idle 20] [--port 0] [--timeout 180] [--cpu-throttle N]
//
// Serves the site itself (127.0.0.1, a localhost origin, so the service worker works), optionally
// with precompressed bodies (Content-Encoding gzip/br, as a CDN would) and emulated network speed.
// Hooks installed before any page script (Page.addScriptToEvaluateOnNewDocument) timestamp:
//   * WebAssembly.compile / compileStreaming (start/end; streaming overlaps the download) and
//     instantiate, the Emscripten factory (brulion_entry)
//     being called and its module resolving (main() has run);
//   * the shell bridge of the app (window.brulionShell.stage("storage"|"ui"), done() = the app's
//     UI exists and the loading screen goes): storage = IDBFS load starts, ui = loaded;
//     index_ready = brulionShell.indexedAt (every note indexed; builds since N13 only — before
//     it the index stalled until the next input, so "settled" excluded indexing there);
//   * every WebGL draw call (frames = draw bursts > 8 ms apart): first_draw, last activity;
//   * long tasks (main thread blocked > 50 ms, PerformanceObserver);
// "settled" = the end of the last 100 ms with the main thread > 20 % busy (CDP TaskDuration) before
// 1.5 s of quiet (the app indexes in short tasks); main_thread_busy_ms = all main-thread work of
// the load. Memory: the wasm linear memory (exported WebAssembly.Memory), JS heap
// (performance.memory) and the RSS of Chromium's renderer and GPU processes.
//   --visits first   every load in a fresh profile (first visit: download + compile)
//   --visits repeat  one profile: load 1 is a first visit, the others are repeat visits (service
//                    worker cache, V8 code cache)
//   --vault DIR      before the first load, writes the folder's notes into IndexedDB exactly as
//                    the app's IDBFS mount stores them (+ settings.ini naming it the current
//                    vault), so the app opens it at start; DIR is read once (.md files)
//   --idbfs          after the last load settles: IDBFS write-back timings (FS.syncfs(false)) for
//                    one changed note and for every note touched, then one more load measuring
//                    the IDBFS read (stage storage → ui)
//   --wait-cache S   after the first repeat-visit load, wait (at most S s) until the app saved its
//                    index cache (brulionShell.indexCacheSavedAt, ~5 s after the index is ready)
//                    and 4 s more for the IDBFS write-back, so the repeat visits take their notes
//                    from it (indexed_from_cache); without it they close sooner and parse (cold)
//   --idle S         CPU time of the renderer and draws while the settled app is left alone
//   --idle-input     during --idle, move the mouse every 100 ms (diagnostic)
// Output: <out>/<label>-loads.jsonl (one JSON per load), <out>/<label>-summary.json (+ table on
// stdout), screenshots <out>/<label>-load<N>.png.
import { spawn, execFileSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { join, extname, relative } from 'node:path';
import { setTimeout as sleep } from 'node:timers/promises';
import { gzipSync, brotliCompressSync, constants as zc } from 'node:zlib';
import { tmpdir } from 'node:os';

function parseArgs(argv) {
  const a = { loads: 5, visits: 'both', encoding: 'none', timeout: 180, label: 'cur', port: 0, idle: 0,
    idbfs: false, cpuThrottle: 1 };
  for (let i = 0; i < argv.length; i++) {
    const k = argv[i];
    const v = () => argv[++i];
    if (k === '--dist') a.dist = v();
    else if (k === '--out') a.out = v();
    else if (k === '--label') a.label = v();
    else if (k === '--loads') a.loads = Number(v());
    else if (k === '--visits') a.visits = v();
    else if (k === '--encoding') a.encoding = v();
    else if (k === '--throttle') a.throttle = v();
    else if (k === '--vault') a.vault = v();
    else if (k === '--idbfs') a.idbfs = true;
    else if (k === '--idle') a.idle = Number(v());
    else if (k === '--wait-cache') a.waitCache = Number(v());
    else if (k === '--port') a.port = Number(v());
    else if (k === '--timeout') a.timeout = Number(v());
    else if (k === '--cpu-throttle') a.cpuThrottle = Number(v());
    else if (k === '--profile-root') a.profileRoot = v();
    else if (k === '--idle-input') a.idleInput = true;
    else throw new Error(`unknown option ${k}`);
  }
  if (!a.dist || !a.out) throw new Error('--dist and --out are required');
  return a;
}

const MIME = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.mjs': 'text/javascript',
  '.wasm': 'application/wasm', '.json': 'application/json', '.webmanifest': 'application/manifest+json',
  '.svg': 'image/svg+xml', '.png': 'image/png', '.ico': 'image/x-icon', '.css': 'text/css', '.data': 'application/octet-stream' };

// ---- static server --------------------------------------------------------------------------
function startServer(args, vaultJson) {
  const cache = new Map();
  const body = (file) => {
    const key = `${file}|${args.encoding}`;
    if (cache.has(key)) return cache.get(key);
    const raw = readFileSync(file);
    let data = raw;
    let enc = null;
    const compressible = /\.(wasm|js|html|json|webmanifest|svg|css)$/.test(file);
    if (compressible && args.encoding === 'gzip') { data = gzipSync(raw, { level: 9 }); enc = 'gzip'; }
    if (compressible && args.encoding === 'br') {
      data = brotliCompressSync(raw, { params: { [zc.BROTLI_PARAM_QUALITY]: 11, [zc.BROTLI_PARAM_SIZE_HINT]: raw.length } });
      enc = 'br';
    }
    const r = { data, enc, raw: raw.length };
    cache.set(key, r);
    return r;
  };
  // Compress every file before listening: compressing the 40 MB .wasm on its first request
  // (brotli q11: ~1 min) would block the server and distort or break the first load.
  if (args.encoding !== 'none') {
    const walk = (d) => {
      for (const n of readdirSync(d)) {
        const p = join(d, n);
        if (statSync(p).isDirectory()) walk(p);
        else body(p);
      }
    };
    walk(args.dist);
  }
  const server = createServer((req, res) => {
    const url = new URL(req.url, 'http://x');
    if (url.pathname === '/__bench/seed.html') {
      res.writeHead(200, { 'Content-Type': 'text/html', 'Cache-Control': 'no-store' });
      res.end('<!doctype html><title>seed</title>');
      return;
    }
    if (url.pathname === '/__bench/vault.json') {
      res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
      res.end(vaultJson || '{}');
      return;
    }
    let path = decodeURIComponent(url.pathname);
    if (path.endsWith('/')) path += 'index.html';
    const file = join(args.dist, path);
    if (!file.startsWith(args.dist) || !existsSync(file) || !statSync(file).isFile()) {
      res.writeHead(404);
      res.end();
      return;
    }
    const b = body(file);
    const headers = { 'Content-Type': MIME[extname(file)] || 'application/octet-stream', 'Content-Length': b.data.length,
      'Cache-Control': 'no-cache' };
    if (b.enc) headers['Content-Encoding'] = b.enc;
    res.writeHead(200, headers);
    res.end(b.data);
  });
  return new Promise((resolve) => server.listen(args.port, '127.0.0.1', () => resolve(server)));
}

// ---- Chromium / CDP ------------------------------------------------------------------------
function findChrome() {
  if (process.env.CHROME) return process.env.CHROME;
  for (const name of ['chromium', 'chromium-browser', 'google-chrome', 'google-chrome-stable']) {
    try {
      return execFileSync('sh', ['-c', `command -v ${name}`], { encoding: 'utf8' }).trim();
    } catch {
      // next
    }
  }
  throw new Error('Chromium not found: set CHROME');
}

class Cdp {
  constructor(url) {
    this.ws = new WebSocket(url);
    this.nextId = 1;
    this.pending = new Map();
    this.listeners = [];
    this.ws.addEventListener('message', (ev) => {
      const msg = JSON.parse(ev.data);
      if (msg.id && this.pending.has(msg.id)) {
        const { resolve, reject } = this.pending.get(msg.id);
        this.pending.delete(msg.id);
        if (msg.error) reject(new Error(`${msg.error.message} (${msg.error.code})`));
        else resolve(msg.result);
      } else if (msg.method) {
        for (const l of this.listeners) l(msg);
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
  on(fn) { this.listeners.push(fn); }
  close() { this.ws.close(); }
}

const HOOKS = `(() => {
  if (window.__bench) return;
  const B = window.__bench = { t: {}, stages: [], draws: 0, firstDraw: null, lastDraw: 0, frames: [], longtasks: [], titles: [] };
  const now = () => performance.now();
  let shell;
  Object.defineProperty(window, 'brulionShell', { configurable: true, get() { return shell; }, set(v) {
    const st = v.stage, dn = v.done;
    v.stage = function (name) { B.stages.push([name, now()]); return st.apply(this, arguments); };
    v.done = function () { if (!B.t.done) B.t.done = now(); return dn.apply(this, arguments); };
    shell = v;
  } });
  let entry;
  Object.defineProperty(window, 'brulion_entry', { configurable: true, get() { return entry; }, set(f) {
    entry = function () {
      B.t.entry = now();
      const p = f.apply(this, arguments);
      Promise.resolve(p).then((m) => { B.t.moduleReady = now(); window.__benchModule = m; }, () => {});
      return p;
    };
  } });
  const compile = WebAssembly.compile;
  WebAssembly.compile = function (buf) {
    B.t.compileStart = now(); B.wasmBytes = buf && buf.byteLength;
    return compile.apply(this, arguments).then((m) => { B.t.compileEnd = now(); return m; });
  };
  const compileStreaming = WebAssembly.compileStreaming;
  if (compileStreaming) WebAssembly.compileStreaming = function (source) {
    B.t.compileStart = now(); B.t.streaming = true;
    return compileStreaming.apply(this, arguments).then((m) => { B.t.compileEnd = now(); return m; });
  };
  const inst = WebAssembly.instantiate;
  WebAssembly.instantiate = function () {
    if (!B.t.instStart) B.t.instStart = now();
    return inst.apply(this, arguments).then((r) => {
      if (!B.t.instEnd) B.t.instEnd = now();
      const i = r && (r.instance || r);
      const ex = i && i.exports;
      if (ex) window.__benchMemory = ex.memory || Object.values(ex).find((x) => x instanceof WebAssembly.Memory);
      return r;
    });
  };
  const mark = () => {
    const t = now();
    B.draws++;
    if (B.firstDraw === null) B.firstDraw = t;
    if (!B.lastDraw || t - B.lastDraw > 8) { if (B.frames.length < 50000) B.frames.push(t); }
    B.lastDraw = t;
  };
  for (const P of [self.WebGL2RenderingContext && WebGL2RenderingContext.prototype, self.WebGLRenderingContext && WebGLRenderingContext.prototype]) {
    if (!P) continue;
    for (const fn of ['drawElements', 'drawArrays', 'drawElementsInstanced', 'drawArraysInstanced', 'drawRangeElements']) {
      const o = P[fn];
      if (o) P[fn] = function () { mark(); return o.apply(this, arguments); };
    }
  }
  try {
    new PerformanceObserver((l) => { for (const e of l.getEntries()) B.longtasks.push([e.startTime, e.duration]); })
      .observe({ type: 'longtask', buffered: true });
  } catch (e) {}
})();`;

// IndexedDB exactly as Emscripten's IDBFS (DB per mount point, version 21, store FILE_DATA keyed by
// absolute path, index "timestamp"; files {timestamp: Date, mode, contents: Uint8Array}).
const SEED = `(async () => {
  const vault = await (await fetch('/__bench/vault.json')).json();
  const put = (dbName, entries) => new Promise((resolve, reject) => {
    const req = indexedDB.open(dbName, 21);
    req.onupgradeneeded = () => {
      const db = req.result;
      const store = db.objectStoreNames.contains('FILE_DATA') ? req.transaction.objectStore('FILE_DATA') : db.createObjectStore('FILE_DATA');
      if (!store.indexNames.contains('timestamp')) store.createIndex('timestamp', 'timestamp', { unique: false });
    };
    req.onerror = () => reject(req.error);
    req.onsuccess = () => {
      const db = req.result;
      const tx = db.transaction(['FILE_DATA'], 'readwrite');
      const store = tx.objectStore('FILE_DATA');
      for (const [k, v] of entries) store.put(v, k);
      tx.oncomplete = () => { db.close(); resolve(entries.length); };
      tx.onerror = () => reject(tx.error);
    };
  });
  const enc = new TextEncoder();
  const ts = new Date(Date.parse('2026-10-17T08:00:00Z'));
  const dirs = new Set();
  const entries = [];
  const root = vault.root;
  for (const [rel, text] of Object.entries(vault.files)) {
    const parts = rel.split('/');
    for (let i = 1; i < parts.length; i++) dirs.add(root + '/' + parts.slice(0, i).join('/'));
    entries.push([root + '/' + rel, { timestamp: ts, mode: 33188, contents: enc.encode(text) }]);
  }
  for (const d of dirs) entries.push([d, { timestamp: ts, mode: 16877 }]);
  const n = await put(root, entries);
  const app = vault.appData;
  const settings = '[vaults]\\ncurrent=' + root + '\\n';
  const m = await put(app, [[app + '/settings.ini', { timestamp: ts, mode: 33188, contents: enc.encode(settings) }]]);
  return { notes: Object.keys(vault.files).length, entries: n + m };
})()`;

function readVault(dir) {
  const files = {};
  let bytes = 0;
  const walk = (d) => {
    for (const name of readdirSync(d)) {
      const p = join(d, name);
      const s = statSync(p);
      if (s.isDirectory()) walk(p);
      else if (name.endsWith('.md')) {
        files[relative(dir, p)] = readFileSync(p, 'utf8');
        bytes += s.size;
      }
    }
  };
  walk(dir);
  return { json: JSON.stringify({ root: '/home/web_user/Brulion', appData: '/home/web_user/.local/share/Brulion/Brulion', files }),
    notes: Object.keys(files).length, bytes };
}

function rendererPids(rootPid) {
  // Every process in the browser's tree with its command line and RSS (KB).
  const out = execFileSync('ps', ['-e', '-o', 'pid=,ppid=,rss=,args='], { encoding: 'utf8' });
  const procs = out.trim().split('\n').map((l) => {
    const m = /^\s*(\d+)\s+(\d+)\s+(\d+)\s+(.*)$/.exec(l);
    return m ? { pid: Number(m[1]), ppid: Number(m[2]), rss: Number(m[3]), args: m[4] } : null;
  }).filter(Boolean);
  const tree = new Set([rootPid]);
  for (let changed = true; changed;) {
    changed = false;
    for (const p of procs) if (!tree.has(p.pid) && tree.has(p.ppid)) { tree.add(p.pid); changed = true; }
  }
  return procs.filter((p) => tree.has(p.pid));
}

function cpuTicks(pid) {
  try {
    const s = readFileSync(`/proc/${pid}/stat`, 'utf8');
    const f = s.slice(s.lastIndexOf(')') + 2).split(' ');
    return Number(f[11]) + Number(f[12]);
  } catch {
    return 0;
  }
}

async function launch(args, profile) {
  mkdirSync(profile, { recursive: true });
  rmSync(join(profile, 'DevToolsActivePort'), { force: true });
  rmSync(join(profile, 'Default', 'Sessions'), { recursive: true, force: true });
  const noSandbox = process.env.BRULION_CHROME_NO_SANDBOX === '1' || process.env.CI === 'true';
  const chrome = spawn(findChrome(), [
    '--headless=new', `--user-data-dir=${profile}`, '--remote-debugging-port=0', '--no-first-run',
    '--no-default-browser-check', '--disable-background-networking', '--disable-component-update',
    '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist',
    '--window-size=1280,800', '--hide-scrollbars', '--mute-audio', '--lang=pl-PL', '--accept-lang=pl-PL,pl',
    '--enable-precise-memory-info', ...(noSandbox ? ['--no-sandbox'] : []), 'about:blank',
  ], { stdio: ['ignore', 'ignore', 'pipe'] });
  let log = '';
  chrome.stderr.on('data', (d) => { log += d; });
  let port;
  for (const end = Date.now() + 30000; Date.now() < end && !port; await sleep(100)) {
    const f = join(profile, 'DevToolsActivePort');
    if (existsSync(f)) port = Number(readFileSync(f, 'utf8').split('\n')[0]);
  }
  if (!port) throw new Error(`Chromium did not start: ${log.slice(-1500)}`);
  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const page = targets.find((t) => t.type === 'page');
  const cdp = new Cdp(page.webSocketDebuggerUrl);
  await cdp.open();
  const problems = [];
  const consoleLog = [];
  cdp.on((msg) => {
    if (msg.method === 'Runtime.consoleAPICalled' && consoleLog.length < 2000) {
      const text = msg.params.args.map((x) => x.value ?? x.description ?? '').join(' ');
      consoleLog.push(`${msg.params.type}: ${text}`.slice(0, 400));
      if (process.env.BENCH_CONSOLE) console.log(`[console.${msg.params.type}] ${text.slice(0, 300)}`);
    }
    if (msg.method === 'Runtime.exceptionThrown') {
      const d = msg.params.exceptionDetails;
      problems.push(d.exception?.description ?? d.text);
    }
  });
  await cdp.send('Runtime.enable');
  await cdp.send('Page.enable');
  await cdp.send('Network.enable');
  await cdp.send('Page.addScriptToEvaluateOnNewDocument', { source: HOOKS });
  if (args.cpuThrottle > 1) await cdp.send('Emulation.setCPUThrottlingRate', { rate: args.cpuThrottle });
  const close = async () => {
    try {
      const v = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
      const b = new Cdp(v.webSocketDebuggerUrl);
      await b.open();
      await b.send('Browser.close').catch(() => {});
    } catch {
      chrome.kill('SIGTERM');
    }
    cdp.close();
    for (const end = Date.now() + 20000; Date.now() < end && chrome.exitCode === null; await sleep(100));
    if (chrome.exitCode === null) chrome.kill('SIGKILL');
  };
  return { chrome, cdp, problems, close, consoleLog };
}

async function evaluate(cdp, expression, awaitPromise = false) {
  const r = await cdp.send('Runtime.evaluate', { expression, awaitPromise, returnByValue: true });
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description ?? r.exceptionDetails.text);
  return r.result.value;
}

async function measureLoad(args, b, url, n, kind) {
  const { cdp, chrome } = b;
  if (args.throttle) {
    const [kbps, rtt] = args.throttle.split(',').map(Number);
    await cdp.send('Network.emulateNetworkConditions', { offline: false, latency: rtt || 0,
      downloadThroughput: (kbps * 1000) / 8, uploadThroughput: (kbps * 1000) / 8 });
  }
  const wall0 = Date.now();
  await cdp.send('Performance.enable', { timeDomain: 'timeTicks' }).catch(() => {});
  await cdp.send('Page.navigate', { url });
  // "Settled": the shell reported the UI, the loading screen is gone, and the page's main thread
  // was busy (CDP Performance.getMetrics TaskDuration) less than 10 % over the last 1.5 s — the
  // app indexes in short tasks that are neither long tasks nor draws. busyEnd = the last 100 ms
  // poll with > 20 % busy.
  let state;
  const samples = [];
  let busyEnd = 0;
  let task0 = null;
  for (const end = Date.now() + args.timeout * 1000; Date.now() < end; await sleep(100)) {
    state = await evaluate(cdp, `(() => {
      const B = window.__bench; if (!B) return null;
      const loader = document.getElementById('loader');
      const now = performance.now();
      const lt = B.longtasks.length ? Math.max(...B.longtasks.map(([s, d]) => s + d)) : 0;
      return { done: B.t.done || null, hidden: !!loader && loader.hidden, now, last: Math.max(B.lastDraw || 0, lt),
        details: document.getElementById('details')?.textContent || '' };
    })()`).catch(() => null);
    const met = await cdp.send('Performance.getMetrics').catch(() => null);
    const task = met ? (met.metrics.find((x) => x.name === 'TaskDuration') || {}).value : null;
    if (!state || task == null) continue;
    if (task0 === null) task0 = task;
    const prev = samples[samples.length - 1];
    samples.push([state.now, task]);
    if (prev && (task - prev[1]) * 1000 > 0.2 * (state.now - prev[0])) busyEnd = state.now;
    if (state.details) break;
    const win = samples.filter(([t]) => t >= state.now - 1500);
    const busy = win.length > 1 ? (win[win.length - 1][1] - win[0][1]) * 1000 : Infinity;
    if (state.done && state.hidden && state.now - state.done > 1500 && win.length > 10 && busy < 150) break;
  }
  const busyTotalMs = samples.length ? (samples[samples.length - 1][1] - (task0 ?? 0)) * 1000 : null;
  const r = await evaluate(cdp, `(() => {
    const B = window.__bench;
    const nav = performance.getEntriesByType('navigation')[0];
    const res = {};
    for (const e of performance.getEntriesByType('resource')) {
      const name = e.name.split('/').pop().split('?')[0];
      if (/\\.(wasm|js)$/.test(name)) res[name] = { start: e.startTime, end: e.responseEnd, transfer: e.transferSize,
        encoded: e.encodedBodySize, decoded: e.decodedBodySize };
    }
    const lt = B.longtasks;
    const last = Math.max(B.lastDraw || 0, lt.length ? Math.max(...lt.map(([s, d]) => s + d)) : 0);
    const mod = window.__benchModule;
    const mem = window.__benchMemory;
    return { t: B.t, stages: B.stages, firstDraw: B.firstDraw, settled: last, draws: B.draws, frames: B.frames.length,
      longtasks: lt.map(([s, d]) => [Math.round(s), Math.round(d)]), wasmBytes: B.wasmBytes,
      nav: nav ? { responseEnd: nav.responseEnd, domContentLoaded: nav.domContentLoadedEventEnd, load: nav.loadEventEnd } : null,
      res, title: document.title, swControlled: !!(navigator.serviceWorker && navigator.serviceWorker.controller),
      // Set by the shell when the app has indexed every note (src/platform/wasm/wasmshell.cpp;
      // builds before it lack it, and before N13 their index stalled until input anyway).
      indexedAt: window.brulionShell && window.brulionShell.indexedAt > 0 ? window.brulionShell.indexedAt : null,
      // How many notes came from the index cache (builds since wave 5; null before).
      indexedFromCache: window.brulionShell ? window.brulionShell.indexedFromCache ?? null : null,
      wasmHeap: mem ? mem.buffer.byteLength : (mod && mod.HEAP8 ? mod.HEAP8.length : null),
      jsHeap: performance.memory ? { used: performance.memory.usedJSHeapSize, total: performance.memory.totalJSHeapSize } : null,
      details: document.getElementById('details')?.textContent || '' };
  })()`);
  const procs = rendererPids(chrome.pid);
  const renderer = procs.filter((p) => /--type=renderer/.test(p.args)).sort((a, c) => c.rss - a.rss)[0];
  const gpu = procs.find((p) => /--type=gpu-process/.test(p.args));
  r.rss = { rendererKB: renderer?.rss ?? null, gpuKB: gpu?.rss ?? null, totalKB: procs.reduce((s, p) => s + p.rss, 0) };
  r.rendererPid = renderer?.pid;
  r.busyEnd = busyEnd;
  r.busyTotalMs = busyTotalMs;
  r.load = n;
  r.kind = kind;
  r.wallMs = Date.now() - wall0;
  const shot = await cdp.send('Page.captureScreenshot', { format: 'png' });
  writeFileSync(join(args.out, `${args.label}-load${n}.png`), Buffer.from(shot.data, 'base64'));
  if (args.throttle) {
    await cdp.send('Network.emulateNetworkConditions', { offline: false, latency: 0, downloadThroughput: -1, uploadThroughput: -1 });
  }
  return r;
}

function metricsOf(r) {
  const stage = (name) => (r.stages.find(([s]) => s === name) || [])[1];
  const wasm = r.res['brulion.wasm'] || {};
  const m = {
    wasm_download_end_ms: wasm.end, wasm_transfer_B: wasm.transfer, wasm_encoded_B: wasm.encoded,
    compile_ms: r.t.compileEnd && r.t.compileStart ? r.t.compileEnd - r.t.compileStart : undefined,
    compile_end_ms: r.t.compileEnd, module_ready_ms: r.t.moduleReady, storage_stage_ms: stage('storage'),
    ui_stage_ms: stage('ui'), idbfs_load_ms: stage('ui') && stage('storage') ? stage('ui') - stage('storage') : undefined,
    shell_done_ms: r.t.done, first_draw_ms: r.firstDraw ?? undefined, index_ready_ms: r.indexedAt ?? undefined,
    indexed_from_cache: r.indexedFromCache ?? undefined,
    last_draw_or_longtask_ms: r.settled,
    settled_ms: r.busyEnd || undefined, main_thread_busy_ms: r.busyTotalMs ?? undefined, draws: r.draws, frames: r.frames,
    longtasks: r.longtasks.length, longtask_total_ms: r.longtasks.reduce((s, [, d]) => s + d, 0),
    longtask_max_ms: r.longtasks.reduce((s, [, d]) => Math.max(s, d), 0),
    wasm_heap_MB: r.wasmHeap ? r.wasmHeap / 1048576 : undefined, js_heap_MB: r.jsHeap ? r.jsHeap.used / 1048576 : undefined,
    renderer_rss_MB: r.rss.rendererKB ? r.rss.rendererKB / 1024 : undefined, gpu_rss_MB: r.rss.gpuKB ? r.rss.gpuKB / 1024 : undefined,
    browser_rss_MB: r.rss.totalKB / 1024,
  };
  return Object.fromEntries(Object.entries(m).filter(([, v]) => v !== undefined && v !== null));
}

function summarize(rows) {
  const keys = [...new Set(rows.flatMap((r) => Object.keys(r.m)))];
  const out = {};
  for (const kind of [...new Set(rows.map((r) => r.kind))]) {
    out[kind] = {};
    for (const k of keys) {
      const v = rows.filter((r) => r.kind === kind && typeof r.m[k] === 'number').map((r) => r.m[k]).sort((a, b) => a - b);
      if (!v.length) continue;
      const med = v.length % 2 ? v[(v.length - 1) / 2] : (v[v.length / 2 - 1] + v[v.length / 2]) / 2;
      out[kind][k] = { median: med, min: v[0], max: v[v.length - 1], n: v.length };
    }
  }
  return out;
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  args.dist = (await import('node:path')).resolve(args.dist);
  mkdirSync(args.out, { recursive: true });
  const vault = args.vault ? readVault(args.vault) : null;
  if (vault) console.log(`vault: ${vault.notes} notes, ${vault.bytes} B`);
  const server = await startServer(args, vault?.json);
  const base = `http://127.0.0.1:${server.address().port}`;
  const url = `${base}/`;
  console.log(`serving ${args.dist} at ${base} (encoding ${args.encoding}${args.throttle ? `, throttle ${args.throttle}` : ''})`);
  const profileRoot = args.profileRoot || join(tmpdir(), `brulion-wasm-bench-${process.pid}`);
  const rows = [];
  const jsonl = join(args.out, `${args.label}-loads.jsonl`);
  writeFileSync(jsonl, '');
  const record = (r) => {
    const m = metricsOf(r);
    rows.push({ kind: r.kind, m });
    writeFileSync(jsonl, JSON.stringify({ ...r, metrics: m }) + '\n', { flag: 'a' });
    console.log(`${r.kind} load ${r.load}: ${Object.entries(m).map(([k, v]) => `${k}=${typeof v === 'number' ? Math.round(v * 10) / 10 : v}`).join(' ')}`);
    if (r.details) console.log(`  problem: ${r.details}`);
  };
  const seed = async (b) => {
    if (!vault) return;
    await b.cdp.send('Page.navigate', { url: `${base}/__bench/seed.html` });
    await sleep(500);
    const t0 = Date.now();
    const s = await evaluate(b.cdp, SEED, true);
    console.log(`seeded IndexedDB: ${JSON.stringify(s)} in ${Date.now() - t0} ms`);
  };

  let n = 0;
  if (args.visits === 'first' || args.visits === 'both') {
    for (let i = 0; i < args.loads; i++) {
      const profile = join(profileRoot, `first-${i}`);
      rmSync(profile, { recursive: true, force: true });
      const b = await launch(args, profile);
      await seed(b);
      record(await measureLoad(args, b, url, ++n, 'first'));
      await b.close();
      rmSync(profile, { recursive: true, force: true });
    }
  }
  let keep = null;
  if (args.visits === 'repeat' || args.visits === 'both' || args.idbfs || args.idle) {
    const profile = join(profileRoot, 'repeat');
    rmSync(profile, { recursive: true, force: true });
    keep = await launch(args, profile);
    await seed(keep);
    record(await measureLoad(args, keep, url, ++n, 'repeat-first'));
    if (args.waitCache) {
      let saved = null;
      for (const end = Date.now() + args.waitCache * 1000; Date.now() < end && !(saved > 0); await sleep(250)) {
        saved = await evaluate(keep.cdp, 'window.brulionShell ? window.brulionShell.indexCacheSavedAt ?? null : null');
      }
      console.log(`index cache saved at ${saved > 0 ? `${Math.round(saved)} ms` : 'never'}`);
      await sleep(4000);
    }
    const reps = args.visits === 'first' ? 0 : args.loads;
    for (let i = 0; i < reps; i++) {
      // A new browser on the same profile: service worker cache + V8 code cache, no memory cache.
      await keep.close();
      keep = await launch(args, profile);
      record(await measureLoad(args, keep, url, ++n, 'repeat'));
    }
  }
  const extra = {};
  if (keep && args.idle) {
    const pid = (rendererPids(keep.chrome.pid).filter((p) => /--type=renderer/.test(p.args)).sort((a, c) => c.rss - a.rss)[0] || {}).pid;
    const gpu = (rendererPids(keep.chrome.pid).find((p) => /--type=gpu-process/.test(p.args)) || {}).pid;
    const d0 = await evaluate(keep.cdp, 'window.__bench.draws');
    const c0 = cpuTicks(pid), g0 = cpuTicks(gpu), b0 = cpuTicks(keep.chrome.pid);
    if (args.idleInput) {
      // Diagnostic: mouse moves over an empty part of the window every 100 ms (does the app's
      // event loop only advance zero-interval timers when other events arrive?).
      for (let k = 0; k < args.idle * 10; k++) {
        await keep.cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: 1250 - (k % 2), y: 790 });
        await sleep(100);
      }
    } else {
      await sleep(args.idle * 1000);
    }
    const d1 = await evaluate(keep.cdp, 'window.__bench.draws');
    const c1 = cpuTicks(pid), g1 = cpuTicks(gpu), b1 = cpuTicks(keep.chrome.pid);
    extra.idle = { seconds: args.idle, renderer_cpu_pct: ((c1 - c0) / (args.idle * 100)) * 100,
      gpu_cpu_pct: ((g1 - g0) / (args.idle * 100)) * 100, browser_cpu_pct: ((b1 - b0) / (args.idle * 100)) * 100,
      draws_per_s: (d1 - d0) / args.idle };
    console.log(`idle: ${JSON.stringify(extra.idle)}`);
    const shot = await keep.cdp.send('Page.captureScreenshot', { format: 'png' });
    writeFileSync(join(args.out, `${args.label}-after-idle.png`), Buffer.from(shot.data, 'base64'));
    extra.idle.visibility = await evaluate(keep.cdp, 'document.visibilityState + "/" + document.hasFocus()');
    // Force a repaint (hover over the sidebar's second entry) and capture again: tells a stale
    // frame from a state that really did not change.
    const dh0 = await evaluate(keep.cdp, 'window.__bench.draws');
    for (let k = 0; k < 10; k++) {
      await keep.cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: 90 + k, y: 88 });
      await sleep(100);
    }
    await sleep(1000);
    extra.idle.draws_after_hover = (await evaluate(keep.cdp, 'window.__bench.draws')) - dh0;
    const shot2 = await keep.cdp.send('Page.captureScreenshot', { format: 'png' });
    writeFileSync(join(args.out, `${args.label}-after-idle-hover.png`), Buffer.from(shot2.data, 'base64'));
  }
  if (keep && args.idbfs) {
    // Write-back of one changed note, then of every note (mtime changed), timed around FS.syncfs(false)
    // through the app's own flush (window.brulionStorage), polling its busy flag.
    const flushTimed = `(async () => {
      const st = window.brulionStorage; const FS = window.__benchModule.FS;
      const wait = async () => { while (st.busy || st.timer) await new Promise((r) => setTimeout(r, 2)); };
      await wait();
      const out = {};
      const p = '/home/web_user/Brulion/bench-idbfs.md';
      FS.writeFile(p, 'zażółć gęślą jaźń ' + Date.now());
      let t = performance.now(); st.flush(); await wait(); out.one_note_ms = performance.now() - t;
      t = performance.now(); st.flush(); await wait(); out.nothing_changed_ms = performance.now() - t;
      const files = [];
      const walk = (d) => { for (const n of FS.readdir(d)) { if (n === '.' || n === '..') continue; const q = d + '/' + n;
        const s = FS.stat(q); if (FS.isDir(s.mode)) walk(q); else files.push(q); } };
      walk('/home/web_user/Brulion');
      const when = Date.now() + 60000;
      t = performance.now(); for (const f of files) FS.utime(f, when, when); out.touch_all_ms = performance.now() - t;
      t = performance.now(); st.flush(); await wait(); out.all_notes_ms = performance.now() - t;
      out.files = files.length;
      return out;
    })()`;
    extra.idbfs = [];
    for (let i = 0; i < 3; i++) {
      const res = await evaluate(keep.cdp, flushTimed, true);
      extra.idbfs.push(res);
      console.log(`idbfs write-back ${i + 1}: ${JSON.stringify(res)}`);
    }
    await keep.close();
    keep = await launch(args, join(profileRoot, 'repeat'));
    for (let i = 0; i < 3; i++) record(await measureLoad(args, keep, url, ++n, 'idbfs-reload'));
  }
  if (keep) await keep.close();
  server.close();
  const summary = { label: args.label, encoding: args.encoding, throttle: args.throttle || null, vault: vault ? { notes: vault.notes, bytes: vault.bytes } : null,
    cpuThrottle: args.cpuThrottle, kinds: summarize(rows), ...extra };
  writeFileSync(join(args.out, `${args.label}-summary.json`), JSON.stringify(summary, null, 1));
  for (const [kind, ms] of Object.entries(summary.kinds)) {
    console.log(`\n${args.label} ${kind}:`);
    for (const [k, v] of Object.entries(ms)) console.log(`  ${k.padEnd(22)} ${String(Math.round(v.median * 10) / 10).padStart(10)}  (${Math.round(v.min * 10) / 10}–${Math.round(v.max * 10) / 10}, n=${v.n})`);
  }
  rmSync(profileRoot, { recursive: true, force: true });
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
