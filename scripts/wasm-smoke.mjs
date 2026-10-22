#!/usr/bin/env node
// Headless-browser smoke test of the browser build (dist-wasm/), dependency-free: drives
// Chromium over the DevTools protocol (Node >= 22 for the global WebSocket). See docs/WEB.md.
//
//   node scripts/wasm-smoke.mjs --url http://127.0.0.1:8123/brulion/ --profile /tmp/p \
//       [--loads 2] [--selftest] [--expect-first N] [--notify] [--glyphs] [--two-tabs] [--lang pl-PL] \
//       [--index-notes N] [--screenshot shot.png] [--loader-screenshot loading.png] [--timeout 120]
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
// itself has no emoji font, every ⏰ 🔔 📅 was an empty box). --two-tabs opens a second tab,
// which must wait ("open in another tab") until the first one leaves the app. --index-notes N
// writes a vault of N generated notes into IndexedDB before the first load (as the app's IDBFS
// mount stores it, named the current vault): every load must then report a complete index
// (brulionShell.indexed(), wasm/index.html) with no input at all — Qt's WebAssembly timers once
// stalled until the mouse moved (docs/WEB.md). --loader-screenshot throttles the first download
// and captures the loading screen. Exit code 0 = all checks passed.
//
// Chromium: $CHROME or chromium/chromium-browser/google-chrome on PATH. WebGL 2 comes from
// SwiftShader (software). Set BRULION_CHROME_NO_SANDBOX=1 (or CI=true) where the sandbox
// cannot start (containers, GitHub Actions on Ubuntu 24.04).
import { spawn, execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { setTimeout as sleep } from 'node:timers/promises';

function parseArgs(argv) {
  const args = { loads: 1, timeout: 120, selftest: false, notify: false, glyphs: false, lang: 'pl-PL' };
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
    else if (a === '--two-tabs') args.twoTabs = true;
    else if (a === '--lang') args.lang = next();
    else if (a === '--loader-screenshot') args.loaderScreenshot = next();
    else if (a === '--expect-first') args.expectFirst = Number(next());
    else if (a === '--index-notes') args.indexNotes = Number(next());
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
// the current vault. Runs in a page of the site's origin.
function seedVaultScript(count) {
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
  ];
  if (flows.length) url.searchParams.set('selftest', flows.join(','));
  if (args.notify) {
    await cdp.send('Browser.grantPermissions', { origin: url.origin, permissions: ['notifications'] });
  }
  const shoot = async (file) => {
    const shot = await cdp.send('Page.captureScreenshot', { format: 'png' });
    writeFileSync(file, Buffer.from(shot.data, 'base64'));
    console.log(`screenshot: ${file}`);
  };

  if (args.indexNotes > 0) {
    // Any document of the site's origin can write its IndexedDB; the logo does not start the app.
    const logo = new URL('icons/logo.svg', url).href;
    await cdp.send('Page.navigate', { url: logo });
    for (const end = deadline(30); Date.now() < end; await sleep(250)) {
      if ((await evaluate('location.href + " " + document.readyState').catch(() => '')) === `${logo} complete`) break;
    }
    const written = await evaluate(seedVaultScript(args.indexNotes), true).catch(() => undefined);
    console.log(`vault of ${args.indexNotes} notes written into IndexedDB (${written} entries)`);
    if (!written) problems.push('could not write the test vault into IndexedDB');
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
          screenElements: document.getElementById('screen')?.childElementCount ?? 0,
        };
      })()`);
      const appUp = state?.loaderHidden && (!args.selftest || state.selftest) && (!args.notify || state.notify)
        && (!args.glyphs || state.glyphs);
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
      console.log(`load ${load}: index of ${args.indexNotes} notes complete at ${indexedAt > 0 ? `${Math.round(indexedAt)} ms` : 'never'}`);
      if (!(indexedAt > 0)) problems.push(`load ${load}: the index did not complete within ${args.timeout} s without input`);
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
