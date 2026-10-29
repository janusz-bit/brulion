#!/usr/bin/env node
// Headless-browser smoke test of the browser build (dist-wasm/), dependency-free: drives
// Chromium over the DevTools protocol (Node >= 22 for the global WebSocket). See docs/WEB.md.
//
//   node scripts/wasm-smoke.mjs --url http://127.0.0.1:8123/brulion/ --profile /tmp/p \
//       [--loads 2] [--selftest] [--expect-first N] [--notify] [--glyphs] [--keyboard] [--keyboard-phone] \
//       [--two-tabs] [--lang pl-PL] \
//       [--index-notes N] [--copy] [--screenshot shot.png] [--loader-screenshot loading.png] [--timeout 120]
//
// Opens the page `--loads` times in one browser (reloads), each time waiting until the app
// has replaced the loading screen. With --selftest the page runs `?selftest=storage`
// (src/platform/wasm/wasmselftest.h): every load must find the counters of the previous one
// (+1), and --expect-first N demands that the first load counts N (use the same --profile
// across runs to test persistence across browser restarts, and stop the web server to test
// offline start from the service worker cache). --notify grants the notification permission,
// lets the app show a test notification (?selftest=notify), clicks it and expects the click to
// reach the app. --glyphs runs ?selftest=glyphs: the bundled fallback fonts must be installed and
// draw every emoji and symbol Brulion writes (src/app/ui/fallbackfonts.h — Qt for WebAssembly
// itself has no emoji font, every ⏰ 🔔 📅 was an empty box). --keyboard stubs window.visualViewport
// (a phone browser's on-screen keyboard shrinks it) and runs ?selftest=keyboard: the app must
// report how much of the page the keyboard covers (KeyboardInsets, src/app/ui/keyboardsources.h)
// as the stub changes, 0 for malformed values and for a pinch-zoomed page, and put a page the
// browser scrolled (iOS) back at the top; before that the shell alone (the app held back) must send
// self-test results only to ?report=/__report… on the page's own site, never to an address the URL
// parser makes another site's ("/<tab>/host", "//host"). --keyboard-phone does the same on an emulated phone
// (412×915 CSS px, DPR 2.625, touch) with today's journal written into IndexedDB and a block
// edited with a tap: the page asks for interactive-widget=resizes-content (and resizes-visual with
// ?keyboard=visual), Qt's hidden keyboard <input> stays pinned in the window's corner, a keyboard
// that shrinks the visual viewport (resizes-visual, iOS) lifts the docked toolbar onto it, a page
// the browser shrank itself (resizes-content: the emulated screen made lower) gets no overlap,
// the keyboard still counts as shown and the toolbar ends at the window's bottom — also with a
// stale innerHeight (the old formula's double count) —, and Qt's containers never stay scrolled.
// --two-tabs opens a second tab,
// which must wait ("open in another tab") until the first one leaves the app. --index-notes N
// writes a vault of N generated notes into IndexedDB before the first load (as the app's IDBFS
// mount stores it, named the current vault): every load must then report a complete index
// (brulionShell.indexed(), wasm/index.html) with no input at all — Qt's WebAssembly timers once
// stalled until the mouse moved (docs/WEB.md); the first load (of 200 notes or more) must save the
// index cache (brulionShell.indexCacheSaved()) and every later one take nearly every note from it
// (brulionShell.indexStats(): indexedFromCache >= N - 1; docs/PERFORMANCE.md, L2). --copy writes a notebook with today's journal into
// IndexedDB the same way and copies blocks with real input: a click on the first block's text,
// Escape (the block selected), Shift+Down (the next one with its child), Ctrl+C — the browser's
// clipboard (navigator.clipboard, permission granted) must then hold their Markdown as text/plain
// and Brulion's HTML as text/html (docs/WEB.md "Schowek"). --loader-screenshot throttles the
// first download and captures the loading screen. Exit code 0 = all checks passed.
//
// Chromium: $CHROME or chromium/chromium-browser/google-chrome on PATH. WebGL 2 comes from
// SwiftShader (software). Set BRULION_CHROME_NO_SANDBOX=1 (or CI=true) where the sandbox
// cannot start (containers, GitHub Actions on Ubuntu 24.04).
import { spawn, execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { setTimeout as sleep } from 'node:timers/promises';

function parseArgs(argv) {
  const args = { loads: 1, timeout: 120, selftest: false, notify: false, glyphs: false, keyboard: false, lang: 'pl-PL' };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    const next = () => {
      if (i + 1 >= argv.length) throw new Error(`${a} needs a value`);
      return argv[++i];
    };
    if (a === '--url') args.url = next();
    else if (a === '--profile') args.profile = next();
    else if (a === '--loads') args.loads = Number(next());
    else if (a === '--selftest') args.selftest = true;
    else if (a === '--notify') args.notify = true;
    else if (a === '--glyphs') args.glyphs = true;
    else if (a === '--keyboard') args.keyboard = true;
    else if (a === '--keyboard-phone') args.keyboardPhone = true;
    else if (a === '--two-tabs') args.twoTabs = true;
    else if (a === '--lang') args.lang = next();
    else if (a === '--loader-screenshot') args.loaderScreenshot = next();
    else if (a === '--expect-first') args.expectFirst = Number(next());
    else if (a === '--index-notes') args.indexNotes = Number(next());
    else if (a === '--copy') args.copy = true;
    else if (a === '--screenshot') args.screenshot = next();
    else if (a === '--timeout') args.timeout = Number(next());
    else throw new Error(`unknown option ${a}`);
  }
  if (!args.url || !args.profile) throw new Error('--url and --profile are required');
  return args;
}

// A vault of `count` generated notes (Polish text, links, tags, tasks) written into IndexedDB the
// way Emscripten's IDBFS stores the app's mounts (src/platform/wasm/wasmstorage.h): one database
// per mount point (version 21, store FILE_DATA keyed by absolute path, index "timestamp"; files
// {timestamp, mode, contents}, folders {timestamp, mode}), plus the app's settings.ini naming it
// the current vault — and, given `journal`, today's journal note (journals/yyyy-MM-dd.md by the
// browser's local date, the app's clock). Runs in a page of the site's origin.
function seedVaultScript(count, journal = null) {
  return `(async () => {
    const root = '/home/web_user/Brulion';
    const appData = '/home/web_user/.local/share/Brulion/Brulion';
    const words = ['zażółć', 'gęślą', 'jaźń', 'notatka', 'spotkanie', 'projekt', 'ogród', 'książka',
      'źródło', 'łódź', 'ćwiczenia', 'pomysł', 'wtorek', 'zakupy', 'rower', 'przegląd'];
    const enc = new TextEncoder();
    const timestamp = new Date();
    const entries = [];
    const dirs = new Set();
    for (let i = 0; i < ${count}; i++) {
      const dir = root + '/Folder ' + (i % 10);
      dirs.add(dir);
      let text = '# Notatka ' + i + '\\n\\n';
      for (let p = 0; p < 5; p++) {
        const line = [];
        for (let k = 0; k < 16; k++) line.push(words[(i * 7 + p * 5 + k * 3) % words.length]);
        text += '- ' + line.join(' ') + ' [[Notatka ' + ((i * 31 + p) % ${count}) + ']] #temat' + ((i + p) % 20) + '\\n';
      }
      text += '- [ ] zadanie ' + i + '\\n';
      entries.push([dir + '/Notatka ' + i + '.md', { timestamp, mode: 33188, contents: enc.encode(text) }]);
    }
    const journal = ${JSON.stringify(journal)};
    if (journal !== null) {
      const now = new Date();
      const day = now.getFullYear() + '-' + String(now.getMonth() + 1).padStart(2, '0') + '-'
        + String(now.getDate()).padStart(2, '0');
      dirs.add(root + '/journals');
      entries.push([root + '/journals/' + day + '.md', { timestamp, mode: 33188, contents: enc.encode(journal) }]);
    }
    for (const d of dirs) entries.push([d, { timestamp, mode: 16877 }]);
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
    const settings = enc.encode('[vaults]\\ncurrent=' + root + '\\n');
    await put(root, entries);
    await put(appData, [[appData + '/settings.ini', { timestamp, mode: 33188, contents: settings }]]);
    return entries.length;
  })()`;
}

// Today's journal for --copy: the first two top blocks (the second with its child) are selected
// and copied; their Markdown is what the clipboard must hold (markdown::blocksMarkdown: as
// written, LF, no trailing line break).
const copyJournal = [
  '- Pierwszy blok — zażółć gęślą jaźń',
  '- Drugi blok z **pogrubieniem** i [[Łódź]]',
  '  - dziecko drugiego bloku',
  '- Trzeci blok zostaje',
  '',
].join('\n');
const copyExpected = copyJournal.split('\n').slice(0, 3).join('\n');

// A visualViewport the test controls (installed before the page's own scripts): its height is
// the page's minus a keyboard, divided by a pinch zoom — window.__brulionKeyboard(px,
// offsetTop, scale). window.__brulionScroll(y) scrolls the page as iOS does (to the input element
// Qt keeps for the keyboard; scrollY and scrollTo are stubbed too), __brulionScrollY() reads it.
// window.__brulionInnerHeight(px) makes innerHeight lie (a stale value, as Chromium reports
// while it resizes the page; null: the real one again).
const viewportStub = `(() => {
  const vv = new EventTarget();
  let keyboard = 0;
  let offset = 0;
  let scale = 1;
  let scrollY = 0;
  const page = () => document.documentElement.clientHeight || window.innerHeight;
  Object.defineProperty(vv, 'width', { get: () => window.innerWidth / scale });
  Object.defineProperty(vv, 'height', { get: () => (page() - keyboard) / scale });
  Object.defineProperty(vv, 'offsetTop', { get: () => offset });
  Object.defineProperty(vv, 'offsetLeft', { get: () => 0 });
  Object.defineProperty(vv, 'scale', { get: () => scale });
  Object.defineProperty(window, 'visualViewport', { value: vv, configurable: true });
  Object.defineProperty(window, 'scrollY', { get: () => scrollY, configurable: true });
  window.scrollTo = (x, y) => { scrollY = typeof x === 'object' && x ? (x.top || 0) : (y || 0); };
  window.__brulionKeyboard = (px, top, zoom) => {
    keyboard = px;
    offset = top || 0;
    scale = zoom || 1;
    vv.dispatchEvent(new Event('resize'));
  };
  window.__brulionScroll = (y) => {
    scrollY = y;
    vv.dispatchEvent(new Event('scroll'));
  };
  window.__brulionScrollY = () => scrollY;
  const realInnerHeight = Object.getOwnPropertyDescriptor(window, 'innerHeight')
    ?? Object.getOwnPropertyDescriptor(Window.prototype, 'innerHeight');
  window.__brulionInnerHeight = (px) => {
    if (px === null || px === undefined) Object.defineProperty(window, 'innerHeight', { ...realInnerHeight, configurable: true });
    else Object.defineProperty(window, 'innerHeight', { get: () => px, configurable: true });
    vv.dispatchEvent(new Event('resize'));
  };
})()`;

// Today's journal for --keyboard-phone: long Polish blocks (two lines on a phone, so a tap in the
// middle of the screen lands on text).
const phoneJournal = Array.from({ length: 40 }, (_, i) =>
  `- Blok ${i + 1}: zażółć gęślą jaźń, źródło łódź ćwiczenia — notatka o spotkaniu i ogrodzie, pomysł na wtorek`)
  .join('\n') + '\n';

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
  on(fn) {
    this.listeners.push(fn);
  }
  close() {
    this.ws.close();
  }
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  mkdirSync(args.profile, { recursive: true });
  rmSync(join(args.profile, 'DevToolsActivePort'), { force: true });
  // A restarted Chromium (also headless) restores the last session's tabs: a restored Brulion
  // tab would hold the storage lock and the page driven here would wait ("open in another tab").
  rmSync(join(args.profile, 'Default', 'Sessions'), { recursive: true, force: true });

  const noSandbox = process.env.BRULION_CHROME_NO_SANDBOX === '1' || process.env.CI === 'true';
  const chromeArgs = [
    '--headless=new',
    `--user-data-dir=${args.profile}`,
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
    `--lang=${args.lang}`,
    `--accept-lang=${args.lang},${args.lang.split('-')[0]}`,
    ...(noSandbox ? ['--no-sandbox'] : []),
    'about:blank',
  ];
  const chrome = spawn(findChrome(), chromeArgs, { stdio: ['ignore', 'ignore', 'pipe'] });
  let chromeLog = '';
  chrome.stderr.on('data', (d) => {
    chromeLog += d;
  });

  const deadline = (seconds) => Date.now() + seconds * 1000;
  let port;
  for (const end = deadline(30); Date.now() < end && !port; await sleep(200)) {
    const file = join(args.profile, 'DevToolsActivePort');
    if (existsSync(file)) port = Number(readFileSync(file, 'utf8').split('\n')[0]);
  }
  if (!port) throw new Error(`Chromium did not start:\n${chromeLog.slice(-2000)}`);

  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const page = targets.find((t) => t.type === 'page');
  const cdp = new Cdp(page.webSocketDebuggerUrl);
  await cdp.open();

  const problems = [];
  cdp.on((msg) => {
    if (msg.method === 'Runtime.consoleAPICalled') {
      const text = msg.params.args.map((a) => a.value ?? a.description ?? '').join(' ');
      console.log(`[console.${msg.params.type}] ${text}`);
    } else if (msg.method === 'Runtime.exceptionThrown') {
      const d = msg.params.exceptionDetails;
      const text = d.exception?.description ?? d.text;
      console.log(`[exception] ${text}`);
      problems.push(`uncaught exception: ${text}`);
    } else if (msg.method === 'Log.entryAdded') {
      const e = msg.params.entry;
      if (e.level === 'error') console.log(`[log.${e.level}] ${e.text} ${e.url ?? ''}`);
    }
  });
  await cdp.send('Runtime.enable');
  await cdp.send('Page.enable');
  await cdp.send('Log.enable');
  await cdp.send('Network.enable');

  const evaluate = async (expression, awaitPromise = false) => {
    const r = await cdp.send('Runtime.evaluate', { expression, awaitPromise, returnByValue: true });
    if (r.exceptionDetails) return undefined;
    return r.result.value;
  };

  const url = new URL(args.url);
  const flows = [
    ...(args.selftest ? ['storage'] : []),
    ...(args.notify ? ['notify'] : []),
    ...(args.glyphs ? ['glyphs'] : []),
    ...(args.keyboard || args.keyboardPhone ? ['keyboard'] : []),
  ];
  if (flows.length) url.searchParams.set('selftest', flows.join(','));
  if (args.notify) {
    await cdp.send('Browser.grantPermissions', { origin: url.origin, permissions: ['notifications'] });
  }
  if (args.keyboard || args.keyboardPhone) {
    await cdp.send('Page.addScriptToEvaluateOnNewDocument', { source: viewportStub });
  }
  // A phone: Pixel-class 1080×2400 at 2.625 (412×915 CSS px), a touch screen.
  const phone = { width: 412, height: 915, deviceScaleFactor: 2.625, mobile: true };
  if (args.keyboardPhone) {
    await cdp.send('Emulation.setDeviceMetricsOverride', phone);
    await cdp.send('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 });
  }
  const shoot = async (file) => {
    const shot = await cdp.send('Page.captureScreenshot', { format: 'png' });
    writeFileSync(file, Buffer.from(shot.data, 'base64'));
    console.log(`screenshot: ${file}`);
  };

  if (args.indexNotes > 0 || args.copy || args.keyboardPhone) {
    // Any document of the site's origin can write its IndexedDB; the logo does not start the app.
    const logo = new URL('icons/logo.svg', url).href;
    await cdp.send('Page.navigate', { url: logo });
    for (const end = deadline(30); Date.now() < end; await sleep(250)) {
      if ((await evaluate('location.href + " " + document.readyState').catch(() => '')) === `${logo} complete`) break;
    }
    const journal = args.copy ? copyJournal : args.keyboardPhone ? phoneJournal : null;
    const written = await evaluate(seedVaultScript(args.indexNotes || 0, journal), true)
      .catch(() => undefined);
    console.log(`vault of ${args.indexNotes || 0} notes${journal ? ' and today\'s journal' : ''} written into IndexedDB (${written} entries)`);
    if (!written) problems.push('could not write the test vault into IndexedDB');
  }

  if (args.keyboard) {
    // ?report= (wasm/index.html): self-test results go only to /__report on the page's own site.
    // Addresses the URL parser turns into another site's — a tab or a line break after the first
    // slash (the parser drops them: "/<tab>/host/x" is //host/x), "//host", a backslash, a full
    // URL — and other paths of the site are ignored. The shell alone is tested: its scripts are
    // held back (the app does not start, so the storage and its self-test counters stay as they
    // are) and its brulionShell.selftest() is called directly.
    const posts = [];
    cdp.on((msg) => {
      if (msg.method === 'Network.requestWillBeSent' && msg.params.request.method === 'POST') {
        posts.push(msg.params.request.url);
      } else if (msg.method === 'Fetch.requestPaused') {
        cdp.send('Fetch.failRequest', { requestId: msg.params.requestId, errorReason: 'BlockedByClient' }).catch(() => {});
      }
    });
    await cdp.send('Network.setBypassServiceWorker', { bypass: true });
    await cdp.send('Fetch.enable', { patterns: [{ urlPattern: '*brulion.js*' }, { urlPattern: '*brulion.wasm*' }] });
    const elsewhere = '127.0.0.2:9/leak';
    const reportCases = [
      ['/\t/' + elsewhere, null], ['/\n/' + elsewhere, null], ['/\r/' + elsewhere, null], ['//' + elsewhere, null],
      ['/\\' + elsewhere, null], ['http://' + elsewhere, null], ['/__report/../leak', null], ['/leak', null],
      ['/__report/smoke', `${url.origin}/__report/smoke`],
    ];
    for (const [report, expected] of reportCases) {
      const pageUrl = new URL(url.href);
      pageUrl.search = '';
      pageUrl.searchParams.set('nosw', '');
      pageUrl.searchParams.set('report', report);
      await cdp.send('Page.navigate', { url: pageUrl.href });
      let ready = false;
      for (const end = deadline(15); Date.now() < end && !ready; await sleep(100)) {
        ready = await evaluate(`location.href === ${JSON.stringify(pageUrl.href)}
                                && typeof window.brulionShell?.selftest === 'function'`);
      }
      posts.length = 0;
      await evaluate(`window.brulionShell.selftest('brulionReportProbe', '{"probe":true}')`);
      await sleep(700);
      const sent = [...posts];
      console.log(`report: ?report=${JSON.stringify(report)}: POSTs ${JSON.stringify(sent)}`);
      if (!ready) problems.push(`report: the shell did not load for ?report=${JSON.stringify(report)}`);
      else if (expected === null ? sent.length > 0 : sent.length === 0 || sent.some((u) => u !== expected)) {
        problems.push(`report: ?report=${JSON.stringify(report)} sent ${JSON.stringify(sent)}, expected ${expected ?? 'nothing'}`);
      }
    }
    await cdp.send('Fetch.disable');
    await cdp.send('Network.setBypassServiceWorker', { bypass: false });
  }

  const results = [];
  for (let load = 1; load <= args.loads; load++) {
    const started = Date.now();
    const throttle = load === 1 && args.loaderScreenshot;
    if (throttle) {
      // ~3 MiB/s: slow enough to see the download progress on the loading screen.
      await cdp.send('Network.emulateNetworkConditions', {
        offline: false, latency: 20, downloadThroughput: 3 * 1024 * 1024, uploadThroughput: -1,
      });
    }
    await cdp.send('Page.navigate', { url: url.href });
    if (throttle) {
      for (const end = deadline(15); Date.now() < end; await sleep(200)) {
        const status = await evaluate(`document.getElementById('status')?.textContent ?? ''`);
        const percent = Number((/(\d+)%/.exec(status ?? '') ?? [])[1] ?? -1);
        if (percent >= 35) break;
      }
      await shoot(args.loaderScreenshot);
      await cdp.send('Network.emulateNetworkConditions', {
        offline: false, latency: 0, downloadThroughput: -1, uploadThroughput: -1,
      });
    }
    let state;
    for (const end = deadline(args.timeout); Date.now() < end; await sleep(250)) {
      state = await evaluate(`(() => {
        const loader = document.getElementById('loader');
        return {
          loaderHidden: !!loader && loader.hidden,
          title: document.getElementById('title')?.textContent ?? null,
          status: document.getElementById('status')?.textContent ?? null,
          details: document.getElementById('details')?.textContent ?? null,
          selftest: window.brulionSelfTest ?? null,
          notify: window.brulionNotifySelfTest ?? null,
          glyphs: window.brulionGlyphSelfTest ?? null,
          keyboard: window.brulionKeyboardSelfTest ?? null,
          screenElements: document.getElementById('screen')?.childElementCount ?? 0,
        };
      })()`);
      const appUp = state?.loaderHidden && (!args.selftest || state.selftest) && (!args.notify || state.notify)
        && (!args.glyphs || state.glyphs) && (!(args.keyboard || args.keyboardPhone) || state.keyboard);
      const failed = state && !state.loaderHidden && state.details;
      if (appUp || failed) break;
    }
    const seconds = ((Date.now() - started) / 1000).toFixed(1);
    console.log(`load ${load}: ${seconds} s ${JSON.stringify(state)}`);
    results.push({ load, seconds: Number(seconds), ...state });
    if (!state?.loaderHidden) problems.push(`load ${load}: app did not start (${state?.title}: ${state?.details || state?.status})`);
    if (args.glyphs && state?.loaderHidden) {
      const fonts = state.glyphs?.fonts ?? [];
      for (const f of fonts) {
        console.log(`load ${load}: font ${f.family}: installed=${f.installed} emoji=${f.emojiFamily} `
          + `boxes before "${f.missingBefore}" after "${f.missingAfter}"`);
        if (!f.installed) problems.push(`load ${load}: fallback font ${f.family} not installed (loaded=${f.loaded})`);
        else if (f.missingAfter) problems.push(`load ${load}: ${f.family} leaves "${f.missingAfter}" as boxes`);
      }
      if (!state.glyphs?.ok || fonts.length === 0) problems.push(`load ${load}: glyph self-test failed: ${JSON.stringify(state.glyphs)}`);
    }
    if (args.indexNotes > 0 && state?.loaderHidden) {
      // No input from here on: the index must complete by itself.
      let indexedAt;
      for (const end = deadline(args.timeout); Date.now() < end && !(indexedAt > 0); await sleep(250)) {
        indexedAt = await evaluate('window.brulionShell?.indexedAt ?? null');
      }
      const stats = await evaluate('({ notes: window.brulionShell?.indexedNotes ?? null, fromCache: window.brulionShell?.indexedFromCache ?? null })');
      console.log(`load ${load}: index of ${args.indexNotes} notes complete at ${indexedAt > 0 ? `${Math.round(indexedAt)} ms` : 'never'}`
        + ` (${stats?.fromCache ?? '?'} of ${stats?.notes ?? '?'} from the index cache)`);
      if (!(indexedAt > 0)) problems.push(`load ${load}: the index did not complete within ${args.timeout} s without input`);
      if (args.indexNotes >= 200 && load >= 2 && !(stats?.fromCache >= args.indexNotes - 1)) {
        problems.push(`load ${load}: only ${stats?.fromCache} of ${args.indexNotes} notes came from the index cache`);
      }
      if (args.indexNotes >= 200 && load < args.loads) {
        // The cache is saved a few seconds after the index is ready (still without input), then
        // written back to IndexedDB like any file (at most 3 s later).
        let savedAt;
        for (const end = deadline(args.timeout); Date.now() < end && !(savedAt > 0); await sleep(250)) {
          savedAt = await evaluate('window.brulionShell?.indexCacheSavedAt ?? null');
        }
        console.log(`load ${load}: index cache saved at ${savedAt > 0 ? `${Math.round(savedAt)} ms` : 'never'}`);
        if (!(savedAt > 0)) problems.push(`load ${load}: the index cache was not saved within ${args.timeout} s`);
        await sleep(4000);
      }
    }
  }

  // Service worker: registered after the first start; wait until it controls the page.
  const sw = await evaluate(
    `(async () => {
      if (!('serviceWorker' in navigator)) return 'unsupported';
      const reg = await Promise.race([navigator.serviceWorker.ready, new Promise((r) => setTimeout(r, 30000))]);
      return reg ? (reg.active ? reg.active.state : 'no active worker') : 'timeout';
    })()`,
    true,
  );
  const cacheNames = await evaluate('caches.keys()', true);
  console.log(`service worker: ${sw}; caches: ${JSON.stringify(cacheNames)}`);

  if (args.notify) {
    // Click the notification the app showed and wait for the click to reach the app.
    const clicked = await evaluate(`(() => {
      const n = globalThis.brulionNotifications?.selftest;
      if (!n || !n.onclick) return false;
      n.onclick(new Event('click'));
      return true;
    })()`);
    let notify;
    for (const end = deadline(10); Date.now() < end; await sleep(200)) {
      notify = await evaluate('window.brulionNotifySelfTest ?? null');
      if (notify?.activated) break;
    }
    console.log(`notification: clicked=${clicked} ${JSON.stringify(notify)}`);
    if (notify?.permission !== 'granted') problems.push(`notification permission: ${notify?.permission}`);
    else if (!notify?.shown) problems.push('notification not shown');
    else if (!clicked) problems.push('notification not found in the page (shown via the service worker?)');
    else if (notify?.activated?.id !== 'selftest' || notify?.activated?.line !== 3) {
      problems.push(`notification click did not reach the app: ${JSON.stringify(notify?.activated)}`);
    }
  }

  if (args.keyboard) {
    // The stubbed visualViewport shrinks as a phone's keyboard comes up (Chrome on Android keeps
    // the page's size: interactive-widget=resizes-visual).
    const keyboardState = () => evaluate('window.brulionKeyboardSelfTest ?? null');
    const waitKeyboard = async (pred) => {
      let k;
      for (const end = deadline(10); Date.now() < end; await sleep(100)) {
        k = await keyboardState();
        if (k && pred(k)) return k;
      }
      return k;
    };
    // [what, the stub's call, the report expected, the page's scroll expected after it]
    const steps = [
      ['no keyboard', null, (k) => k.overlap === 0 && k.source === 'platform'],
      ['a 300 px keyboard', '__brulionKeyboard(300)', (k) => k.overlap === 300 && k.shown && k.source === 'platform'],
      ['a 250 px keyboard, the page scrolled 20 px (iOS)', '__brulionKeyboard(270, 20)', (k) => k.overlap === 250 && k.shown],
      ['malformed (NaN)', '__brulionKeyboard(NaN)', (k) => k.overlap === 0 && !k.shown],
      ['a 300 px keyboard again', '__brulionKeyboard(300)', (k) => k.overlap === 300],
      // A pinch-zoomed page (a desktop touchpad) shrinks the visual viewport too: no keyboard.
      ['a page zoomed in twice, no keyboard', '__brulionKeyboard(0, 0, 2)', (k) => k.overlap === 0 && !k.shown],
      ['a 300 px keyboard, not zoomed', '__brulionKeyboard(300, 0, 1)', (k) => k.overlap === 300 && k.shown],
      ['the browser scrolls the page 40 px (iOS)', '__brulionScroll(40)', (k) => k.overlap === 300 && k.shown, 0],
      ['the keyboard goes', '__brulionKeyboard(0)', (k) => k.overlap === 0 && !k.shown],
    ];
    for (const [what, call, pred, scrollAfter] of steps) {
      if (call !== null) await evaluate(`window.${call}`);
      const k = await waitKeyboard(pred);
      console.log(`keyboard: ${what}: ${JSON.stringify(k)}`);
      if (!k || !pred(k)) problems.push(`keyboard: ${what}: the app reported ${JSON.stringify(k)}`);
      if (scrollAfter !== undefined) {
        const scrollY = await evaluate('window.__brulionScrollY()');
        console.log(`keyboard: ${what}: the page's scroll ${scrollY}`);
        if (scrollY !== scrollAfter) problems.push(`keyboard: ${what}: the page stays scrolled ${scrollY} px`);
      }
      if (what === 'a 300 px keyboard' && args.screenshot) await shoot(args.screenshot.replace(/\.png$/, '-keyboard.png'));
    }
  }

  if (args.keyboardPhone) {
    // The phone's keyboard (docs/WEB.md "Klawiatura ekranowa"): the stubbed visualViewport for a
    // keyboard that only shrinks the visual viewport (resizes-visual: iOS Safari, ?keyboard=visual),
    // the emulated screen made lower for a page the browser shrank itself (resizes-content).
    const keyboardState = () => evaluate('window.brulionKeyboardSelfTest ?? null');
    const waitKeyboard = async (pred, seconds = 15) => {
      let k;
      for (const end = deadline(seconds); Date.now() < end; await sleep(100)) {
        k = await keyboardState();
        if (k && !k.animating && pred(k)) return k;
      }
      return k;
    };
    const near = (a, b, tolerance = 1) => typeof a === 'number' && Math.abs(a - b) <= tolerance;
    const tap = async (x, y) => {
      await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x, y }] });
      await sleep(60);
      await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
      await sleep(800);
    };
    const qtInput = `(() => {
      const root = document.getElementById('qt-shadow-container')?.shadowRoot;
      const input = root?.querySelector('.qt-window-input-element');
      const cs = input ? getComputedStyle(input) : null;
      const scrolled = root ? [...root.querySelectorAll('.qt-screen, .qt-window, .qt-window *')]
        .filter((e) => e.scrollTop || e.scrollLeft).map((e) => e.className || e.tagName) : null;
      return { style: !!root?.getElementById('brulion-keyboard-style'), focused: !!input && root.activeElement === input,
               left: cs?.left ?? null, top: cs?.top ?? null, scrolled,
               pageScroll: document.scrollingElement?.scrollTop ?? null };
    })()`;
    const mode = await evaluate(`({ meta: document.getElementById('brulion-viewport')?.content ?? null,
                                    mode: window.brulionShell?.keyboardMode ?? null })`);
    console.log(`keyboard-phone: viewport ${JSON.stringify(mode)}`);
    if (!/interactive-widget=resizes-content/.test(mode?.meta ?? '') || mode?.mode !== 'content') {
      problems.push(`keyboard-phone: the page does not ask for resizes-content: ${JSON.stringify(mode)}`);
    }
    const shot = async (name) => {
      if (args.screenshot) await shoot(args.screenshot.replace(/\.png$/, `-${name}.png`));
    };
    // A block low on the screen (80 % down: below where a keyboard would end) edited with a tap;
    // a few heights tried (fonts differ).
    let k;
    for (const y of [0.8, 0.75, 0.7, 0.6, 0.5].map((f) => Math.round(phone.height * f))) {
      await tap(phone.width / 2, y);
      k = await waitKeyboard((s) => s.sample?.editing && s.toolbarBottom >= 0, 6);
      console.log(`keyboard-phone: a tap at (${phone.width / 2}, ${y}): ${JSON.stringify(k)}`);
      if (k?.sample?.editing && k.toolbarBottom >= 0) break;
    }
    if (!k?.sample?.editing || !(k.toolbarBottom >= 0)) problems.push(`keyboard-phone: no block edited after a tap: ${JSON.stringify(k)}`);
    await shot('edit');
    const pin = await evaluate(qtInput);
    console.log(`keyboard-phone: Qt's keyboard input ${JSON.stringify(pin)}`);
    if (!pin?.style || pin.left !== '0px' || pin.top !== '0px') {
      problems.push(`keyboard-phone: Qt's keyboard input is not pinned in the window's corner: ${JSON.stringify(pin)}`);
    }
    // [what, the page's setup, the report expected]
    const H = phone.height;
    const steps = [
      ['visual: a 300 px keyboard', 'window.__brulionKeyboard(300)',
        (s) => near(s.overlap, 300) && s.shown && near(s.contentBottom, H - 300) && near(s.toolbarBottom, H - 300)],
      ['visual: the view panned to the field (offsetTop 120)', 'window.__brulionKeyboard(300, 120)',
        (s) => near(s.overlap, 180) && s.shown && near(s.toolbarBottom, H - 180)],
      ['visual: the keyboard goes', 'window.__brulionKeyboard(0)', (s) => s.overlap === 0 && !s.shown],
      ['content: the page shrank by 300 px', { height: H - 300 },
        (s) => s.overlap === 0 && s.shown && near(s.windowHeight, H - 300) && near(s.toolbarBottom, H - 300)
          && near(s.contentBottom, H - 300)],
      ['content: a stale innerHeight', 'window.__brulionInnerHeight(' + H + ')',
        (s) => s.overlap === 0 && s.shown && near(s.legacyOverlap, 300) && near(s.toolbarBottom, H - 300)],
      ['content: the keyboard hidden (Back)', { height: H, innerHeight: null },
        (s) => s.overlap === 0 && !s.shown && near(s.windowHeight, H) && near(s.toolbarBottom, H)],
      ['malformed (NaN)', 'window.__brulionKeyboard(NaN)', (s) => s.overlap === 0 && !s.shown],
      ['a page zoomed in twice, no keyboard', 'window.__brulionKeyboard(0, 0, 2)', (s) => s.overlap === 0 && !s.shown],
      ['no keyboard, not zoomed', 'window.__brulionKeyboard(0, 0, 1)', (s) => s.overlap === 0 && !s.shown],
    ];
    for (const [what, setup, pred] of steps) {
      if (typeof setup === 'string') {
        await evaluate(setup);
      } else {
        if ('innerHeight' in setup) await evaluate('window.__brulionInnerHeight(null)');
        await cdp.send('Emulation.setDeviceMetricsOverride', { ...phone, height: setup.height });
      }
      const got = await waitKeyboard(pred);
      console.log(`keyboard-phone: ${what}: ${JSON.stringify(got)}`);
      if (!got || !pred(got)) problems.push(`keyboard-phone: ${what}: the app reported ${JSON.stringify(got)}`);
      if (what.startsWith('content: the page shrank')) {
        await shot('content');
        // Qt's containers stay where they were (the edited block was below the shrunk window).
        const containers = await evaluate(qtInput);
        console.log(`keyboard-phone: ${what}: Qt's containers ${JSON.stringify(containers)}`);
        if (!containers || containers.scrolled === null || containers.scrolled.length || containers.pageScroll) {
          problems.push(`keyboard-phone: Qt's containers scrolled: ${JSON.stringify(containers)}`);
        }
      }
      if (what === 'visual: a 300 px keyboard') await shot('visual');
    }
    // ?keyboard=visual: the browser's default (the page keeps its size), for tests and diagnosis.
    const visualUrl = new URL(url.href);
    visualUrl.searchParams.set('keyboard', 'visual');
    await cdp.send('Page.navigate', { url: visualUrl.href });
    let visual;
    for (const end = deadline(20); Date.now() < end; await sleep(250)) {
      visual = await evaluate(`({ href: location.href, meta: document.getElementById('brulion-viewport')?.content ?? null,
                                   mode: window.brulionShell?.keyboardMode ?? null })`);
      if (visual?.href === visualUrl.href && visual.mode) break;
    }
    console.log(`keyboard-phone: ?keyboard=visual: ${JSON.stringify(visual)}`);
    if (!/interactive-widget=resizes-visual/.test(visual?.meta ?? '') || visual?.mode !== 'visual') {
      problems.push(`keyboard-phone: ?keyboard=visual does not switch to resizes-visual: ${JSON.stringify(visual)}`);
    }
  }

  if (args.copy) {
    // Blocks copied with real input (trusted mouse and key events): Qt writes the browser's
    // clipboard through the asynchronous Clipboard API, which needs a focused page and the user's
    // gesture (the key press). The test reads the clipboard back with the permission granted.
    await cdp.send('Browser.grantPermissions', {
      origin: url.origin, permissions: ['clipboardReadWrite', 'clipboardSanitizedWrite'],
    });
    await cdp.send('Emulation.setFocusEmulationEnabled', { enabled: true });
    const keys = {
      Escape: { code: 'Escape', vk: 27 }, ArrowDown: { code: 'ArrowDown', vk: 40 }, c: { code: 'KeyC', vk: 67 },
    };
    const press = async (key, modifiers = 0) => { // modifiers: 2 Ctrl, 8 Shift
      const k = keys[key];
      const common = { key, code: k.code, windowsVirtualKeyCode: k.vk, nativeVirtualKeyCode: k.vk, modifiers };
      await cdp.send('Input.dispatchKeyEvent', { type: 'rawKeyDown', ...common });
      await cdp.send('Input.dispatchKeyEvent', { type: 'keyUp', ...common });
      await sleep(400);
    };
    const click = async (x, y) => {
      await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y });
      await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', buttons: 1, clickCount: 1 });
      await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', buttons: 0, clickCount: 1 });
      await sleep(800);
    };
    const readText = () => evaluate('navigator.clipboard.readText().catch((e) => "(error) " + e.message)', true);
    const marker = 'brulion-smoke: nothing copied yet';
    // Today's first block on the 1280×800 window: the journal right of the sidebar, its first line
    // under the day's title, about 153 px down the page (a few heights tried: fonts differ).
    let copied;
    for (const y of [153, 147, 160, 167, 140]) {
      await evaluate(`navigator.clipboard.writeText(${JSON.stringify(marker)})`, true);
      await click(560, y);
      await press('Escape'); // the edited block becomes the selection
      await press('ArrowDown', 8); // Shift+Down: the next block, with its child
      await press('c', 2); // Ctrl+C
      for (const end = deadline(5); Date.now() < end; await sleep(250)) {
        copied = await readText();
        if (copied !== marker) break;
      }
      console.log(`copy: a click at (560, ${y}), Escape, Shift+Down, Ctrl+C: ${JSON.stringify(copied)}`);
      if (copied === copyExpected) break;
      await press('Escape'); // leave whatever was selected
    }
    if (args.screenshot) await shoot(args.screenshot.replace(/\.png$/, '-copy.png'));
    if (copied !== copyExpected) {
      problems.push(`copy: the clipboard holds ${JSON.stringify(copied)}, expected ${JSON.stringify(copyExpected)}`);
    } else {
      // The HTML flavour for mail and office apps: Brulion's own, with its generator mark.
      const html = await evaluate(`(async () => {
        for (const item of await navigator.clipboard.read()) {
          if (item.types.includes('text/html')) return await (await item.getType('text/html')).text();
        }
        return null;
      })().catch((e) => "(error) " + e.message)`, true);
      console.log(`copy: text/html ${JSON.stringify(html?.slice(0, 400))}`);
      if (!html || !html.includes('content="Brulion"') || !html.includes('<strong>pogrubieniem</strong>')) {
        problems.push(`copy: no Brulion HTML on the clipboard: ${JSON.stringify(html?.slice(0, 200))}`);
      }
    }
  }

  if (args.screenshot) {
    await sleep(500);
    await shoot(args.screenshot);
  }

  if (args.twoTabs) {
    // A second tab must not open the storage while the first one has it (Web Lock).
    const plainUrl = new URL(args.url).href;
    const info = await (await fetch(`http://127.0.0.1:${port}/json/new?${plainUrl}`, { method: 'PUT' })).json();
    const tab2 = new Cdp(info.webSocketDebuggerUrl);
    await tab2.open();
    await tab2.send('Runtime.enable');
    const evaluate2 = async (expression) => {
      const r = await tab2.send('Runtime.evaluate', { expression, returnByValue: true });
      return r.exceptionDetails ? undefined : r.result.value;
    };
    const tabState = `({ hidden: document.getElementById('loader')?.hidden ?? false,
                         status: document.getElementById('status')?.textContent ?? '' })`;
    let waiting;
    for (const end = deadline(30); Date.now() < end; await sleep(250)) {
      waiting = await evaluate2(tabState);
      if (waiting && /karcie|another tab/.test(waiting.status)) break;
    }
    console.log(`second tab while the first is open: ${JSON.stringify(waiting)}`);
    if (!waiting || waiting.hidden || !/karcie|another tab/.test(waiting.status)) {
      problems.push(`second tab did not wait for the first: ${JSON.stringify(waiting)}`);
    }
    // Leave the app in the first tab: the second one must take over (also when the first page
    // goes into the back/forward cache, which keeps it alive).
    await cdp.send('Page.navigate', { url: 'about:blank' });
    await tab2.send('Page.bringToFront').catch(() => {});
    let taken;
    for (const end = deadline(20); Date.now() < end; await sleep(250)) {
      taken = await evaluate2(tabState);
      if (taken?.hidden) break;
    }
    console.log(`second tab after the first navigated away: ${JSON.stringify(taken)}`);
    if (!taken?.hidden) {
      const locks = await tab2.send('Runtime.evaluate', {
        expression: 'navigator.locks.query()', awaitPromise: true, returnByValue: true,
      }).catch((e) => ({ error: String(e) }));
      console.log(`locks: ${JSON.stringify(locks.result?.value ?? locks)}`);
      problems.push(`second tab did not start after the first navigated away: ${JSON.stringify(taken)}`);
    }
    tab2.close();
    await fetch(`http://127.0.0.1:${port}/json/close/${info.id}`).catch(() => {});
  }

  if (args.selftest) {
    const counts = results.map((r) => r.selftest?.counters?.map((c) => c.now) ?? null);
    for (const [i, r] of results.entries()) {
      if (!r.selftest) problems.push(`load ${i + 1}: no self-test result`);
      else if (!r.selftest.backed) problems.push(`load ${i + 1}: IndexedDB not available (${r.selftest.note})`);
      else if (!r.selftest.flushed) problems.push(`load ${i + 1}: changes not written back (${r.selftest.note})`);
    }
    for (let i = 1; i < counts.length; i++) {
      const prev = counts[i - 1];
      const cur = counts[i];
      if (!prev || !cur || cur.some((n, k) => n !== prev[k] + 1)) {
        problems.push(`load ${i + 1}: counters ${JSON.stringify(cur)} after ${JSON.stringify(prev)} (expected +1)`);
      }
    }
    if (args.expectFirst !== undefined && counts[0] && counts[0].some((n) => n !== args.expectFirst)) {
      problems.push(`first load counted ${JSON.stringify(counts[0])}, expected ${args.expectFirst}`);
    }
    console.log(`self-test counters per load: ${JSON.stringify(counts)}`);
  }

  // Graceful shutdown so the profile (IndexedDB, service worker cache) is written to disk.
  try {
    const version = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
    const browser = new Cdp(version.webSocketDebuggerUrl);
    await browser.open();
    await browser.send('Browser.close').catch(() => {});
  } catch {
    chrome.kill('SIGTERM');
  }
  cdp.close();
  for (const end = deadline(20); Date.now() < end && chrome.exitCode === null; await sleep(200));
  if (chrome.exitCode === null) chrome.kill('SIGKILL');

  if (problems.length) {
    console.log(`FAILED:\n  ${problems.join('\n  ')}`);
    process.exit(1);
  }
  console.log('OK');
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
