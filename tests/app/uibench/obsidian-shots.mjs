#!/usr/bin/env node
// obsidian-shots.mjs — the Obsidian half of the UI benchmark (docs/UI.md "UI benchmark (Brulion vs
// Obsidian)"): starts the official Obsidian desktop app (Electron) on the bench vault in a private
// profile, drives it over the Chrome DevTools Protocol and saves, for every screen of screens.json,
// a PNG and a layout dump in the schema of Brulion's --dump-layout (src/app/ui/layoutdump.h).
// Node ≥ 22 (built-in WebSocket and fetch), no npm packages. Run by bench.sh under Xvfb:
//
//   node tests/app/uibench/obsidian-shots.mjs --launch "<command…>" --vault <vault-obsidian> \
//     --screens tests/app/uibench/screens.json [--only id,id] [--sizes desktop,phone] \
//     [--themes light,dark] --out <out>/shots/obsidian --profile <out>/env/obsidian-profile \
//     --now "2026-10-14 10:00" --lang pl --version 1.14.4 [--fonts <fonts.conf>] [--port 9333]
//
// --launch is split at whitespace (paths without spaces): `appimage-run -w <extracted AppImage>`
// on NixOS (an FHS environment around the AppImage's files, extracted once by bench.sh), the
// extracted `squashfs-root/obsidian` elsewhere. Obsidian's private API (app.commands, app.setting,
// internalPlugins, the EmulateMobile key) is pinned by the version: every step checks its effect
// and fails loudly. Writes <out>/obsidian-manifest.json: {environment per size, shots, failures,
// warnings}.
import { spawn } from 'node:child_process';
import { mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

const VAULT_ID = 'b7c1e0f2a3d4e5f6';
const J = (v) => JSON.stringify(v);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function parseArgs(argv) {
  const args = { port: 9333, lang: 'pl', sizes: null, themes: null, only: null };
  for (let i = 0; i < argv.length; ++i) {
    const key = argv[i].replace(/^--/, '');
    const value = argv[i + 1];
    if (['launch', 'vault', 'screens', 'out', 'profile', 'now', 'lang', 'version', 'fonts'].includes(key)) {
      args[key] = value;
      ++i;
    } else if (key === 'port') {
      args.port = Number(value);
      ++i;
    } else if (['only', 'sizes', 'themes'].includes(key)) {
      args[key] = value.split(',').filter(Boolean);
      ++i;
    } else {
      throw new Error(`unknown option ${argv[i]}`);
    }
  }
  for (const k of ['launch', 'vault', 'screens', 'out', 'profile', 'now', 'version'])
    if (!args[k]) throw new Error(`--${k} is required`);
  return args;
}

// A copy of the minimal DevTools client of scripts/wasm-smoke.mjs (class Cdp), with a timeout per
// call and the connection's failure passed on.
class Cdp {
  constructor(url) {
    this.ws = new WebSocket(url);
    this.nextId = 1;
    this.pending = new Map();
    this.listeners = [];
    this.ws.addEventListener('message', (ev) => {
      const msg = JSON.parse(ev.data);
      if (msg.id && this.pending.has(msg.id)) {
        const { resolve, reject, timer } = this.pending.get(msg.id);
        clearTimeout(timer);
        this.pending.delete(msg.id);
        if (msg.error) reject(new Error(`${msg.error.message} (${msg.error.code})`));
        else resolve(msg.result);
      } else if (msg.method) {
        for (const l of this.listeners) l(msg);
      }
    });
    this.ws.addEventListener('close', () => {
      for (const { reject, timer } of this.pending.values()) {
        clearTimeout(timer);
        reject(new Error('DevTools connection closed'));
      }
      this.pending.clear();
    });
  }
  open() {
    return new Promise((resolve, reject) => {
      this.ws.addEventListener('open', resolve, { once: true });
      this.ws.addEventListener('error', () => reject(new Error('DevTools connection failed')), { once: true });
    });
  }
  send(method, params = {}, timeoutMs = 60000) {
    const id = this.nextId++;
    this.ws.send(JSON.stringify({ id, method, params }));
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`${method}: no answer in ${timeoutMs} ms`));
      }, timeoutMs);
      this.pending.set(id, { resolve, reject, timer });
    });
  }
  on(fn) {
    this.listeners.push(fn);
  }
  close() {
    this.ws.close();
  }
}

// ---- Profile and process ------------------------------------------------------------------------

function writeProfile(profile, vault) {
  for (const d of ['userdata', 'home', 'xdg/config', 'xdg/data', 'xdg/cache', 'xdg/state'])
    mkdirSync(join(profile, d), { recursive: true });
  writeFileSync(join(profile, 'userdata', 'obsidian.json'), J({
    updateDisabled: true,
    vaults: { [VAULT_ID]: { path: resolve(vault), ts: Date.now(), open: true } },
  }));
}

function launch(cmd, profile, port, fonts, lang) {
  const parts = cmd.trim().split(/\s+/);
  const env = {
    ...process.env,
    HOME: join(profile, 'home'),
    XDG_CONFIG_HOME: join(profile, 'xdg/config'),
    XDG_DATA_HOME: join(profile, 'xdg/data'),
    XDG_CACHE_HOME: join(profile, 'xdg/cache'),
    XDG_STATE_HOME: join(profile, 'xdg/state'),
    TZ: 'Europe/Warsaw',
    LANG: 'pl_PL.UTF-8',
    LANGUAGE: lang,
  };
  if (fonts) env.FONTCONFIG_FILE = fonts;
  const child = spawn(parts[0], [...parts.slice(1),
    `--user-data-dir=${join(profile, 'userdata')}`, `--remote-debugging-port=${port}`,
    '--no-sandbox', '--disable-gpu', '--force-device-scale-factor=1', '--force-color-profile=srgb',
    '--disable-lcd-text', `--lang=${lang}`, '--proxy-server=http://127.0.0.1:9', '--proxy-bypass-list=<-loopback>',
  ], { env, stdio: ['ignore', 'pipe', 'pipe'], detached: true });
  let log = '';
  const keep = (b) => { log = (log + b.toString()).slice(-20000); };
  child.stdout.on('data', keep);
  child.stderr.on('data', keep);
  child.log = () => log;
  return child;
}

function kill(child) {
  if (!child || child.exitCode !== null) return;
  try { process.kill(-child.pid, 'SIGTERM'); } catch { /* gone */ }
}

async function pageTarget(port, timeoutMs = 60000) {
  const deadline = Date.now() + timeoutMs;
  let last = '';
  while (Date.now() < deadline) {
    try {
      const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
      const page = list.find((t) => t.type === 'page' && t.url.startsWith('app://obsidian.md') && !/starter/.test(t.url));
      if (page) return page;
      last = list.map((t) => `${t.type} ${t.url}`).join(', ');
    } catch (e) {
      last = e.message;
    }
    await sleep(500);
  }
  throw new Error(`no Obsidian page on port ${port} in ${timeoutMs} ms (${last})`);
}

async function evaluate(cdp, expr, timeoutMs = 60000) {
  const r = await cdp.send('Runtime.evaluate', {
    expression: `(async () => { ${expr} })()`, awaitPromise: true, returnByValue: true,
  }, timeoutMs);
  if (r.exceptionDetails) {
    const d = r.exceptionDetails;
    throw new Error(d.exception?.description ?? d.text ?? 'exception');
  }
  return r.result?.value;
}

// ---- Page state -----------------------------------------------------------------------------------

// A running clock shifted to `now` (local wall-clock time of the page): moment, the Calendar
// plugin and "today" agree with Brulion's --now; timers keep working.
function dateShim(now) {
  const iso = now.replace(' ', 'T') + ':00';
  return `(() => { const RealDate = Date; const offset = new RealDate(${J(iso)}).getTime() - RealDate.now();
  function FakeDate(...a) { if (!new.target) return new FakeDate().toString();
    return a.length ? new RealDate(...a) : new RealDate(RealDate.now() + offset); }
  FakeDate.prototype = RealDate.prototype; Object.setPrototypeOf(FakeDate, RealDate);
  FakeDate.now = () => RealDate.now() + offset; FakeDate.parse = RealDate.parse; FakeDate.UTC = RealDate.UTC;
  globalThis.Date = FakeDate; })();`;
}

async function reloadAndWait(cdp) {
  await cdp.send('Page.reload', { ignoreCache: true });
  await sleep(1000);
  const deadline = Date.now() + 60000;
  for (;;) {
    try {
      if (await evaluate(cdp, 'return !!(window.app?.workspace?.layoutReady);', 5000)) break;
    } catch { /* the page is still loading */ }
    if (Date.now() > deadline) throw new Error('Obsidian: the workspace never became ready');
    await sleep(300);
  }
  await evaluate(cdp, `await new Promise(r => app.workspace.onLayoutReady(r));
    await new Promise(r => app.metadataCache.resolved ? r() : (app.metadataCache.on('resolved', r), setTimeout(r, 10000)));
    await document.fonts.ready;`);
}

async function environment(cdp) {
  return evaluate(cdp, `return {
    appVersion: (navigator.userAgent.match(/obsidian\\/([\\d.]+)/i) || [])[1] || null,
    userAgent: navigator.userAgent,
    language: window.i18next?.language ?? window.moment?.locale() ?? null,
    isMobile: !!app.isMobile,
    isPhone: document.body.classList.contains('is-phone'),
    frameless: document.body.classList.contains('is-frameless'),
    viewport: {w: innerWidth, h: innerHeight, dpr: devicePixelRatio},
    fonts: getComputedStyle(document.body).fontFamily,
    plugins: Object.keys(app.plugins?.plugins ?? {}),
    today: window.moment ? moment().format('YYYY-MM-DD HH:mm') : String(new Date()),
    timezone: Intl.DateTimeFormat().resolvedOptions().timeZone,
    bodyClasses: document.body.className,
  };`);
}

async function boot(cdp, { size, lang, now, version }) {
  await cdp.send('Page.enable');
  await cdp.send('Runtime.enable');
  await cdp.send('Page.addScriptToEvaluateOnNewDocument', { source: dateShim(now) });
  await cdp.send('Emulation.setTimezoneOverride', { timezoneId: 'Europe/Warsaw' });
  await cdp.send('Emulation.setFocusEmulationEnabled', { enabled: true });
  // Before the reload: the phone layout (is-phone) is decided while the app loads.
  await cdp.send('Emulation.setDeviceMetricsOverride', {
    width: size.width, height: size.height, deviceScaleFactor: size.scale, mobile: false,
    screenWidth: size.width, screenHeight: size.height,
  });
  await evaluate(cdp, `localStorage.setItem('most-recently-installed-version', ${J(version)});
    localStorage.setItem('enable-plugin-${VAULT_ID}', 'true');
    localStorage.setItem('language', ${J(lang)});
    ${size.touch ? "localStorage.setItem('EmulateMobile', '1');" : "localStorage.removeItem('EmulateMobile');"}`);
  await reloadAndWait(cdp);
}

async function setTheme(cdp, theme) {
  const result = await evaluate(cdp, `
    const want = ${J(theme)};
    const ids = Object.keys(app.commands.commands).filter(id => id.startsWith('theme:'));
    const byId = ids.find(id => id === 'theme:use-' + want);
    const byName = ids.find(id => (want === 'dark' ? /dark|ciemn/i : /light|jasn/i).test(app.commands.commands[id].name));
    const id = byId ?? byName;
    let used = id ?? null;
    if (id) app.commands.executeCommandById(id);
    if (!document.body.classList.contains('theme-' + want)) {
      used = 'updateTheme';
      app.vault.setConfig?.('theme', want === 'dark' ? 'obsidian' : 'moonstone');
      app.updateTheme?.(want === 'dark' ? 'obsidian' : 'moonstone');
    }
    await new Promise(r => setTimeout(r, 300));
    return {ok: document.body.classList.contains('theme-' + want), used, ids};`);
  if (!result.ok) throw new Error(`Obsidian: theme ${theme} not applied (commands ${result.ids.join(', ')})`);
  return result.used;
}

async function closeModals(cdp) {
  await evaluate(cdp, 'if (app.setting?.isOpen) app.setting.close();');
  for (let i = 0; i < 4; ++i) {
    const open = await evaluate(cdp,
      `return !!document.querySelector('.modal-container, .menu, .suggestion-container, .prompt');`);
    if (!open) return;
    await key(cdp, 'Escape');
    await sleep(200);
  }
  await evaluate(cdp, `document.querySelectorAll('.modal-container .modal-close-button').forEach(b => b.click());
    app.setting?.close?.();
    document.querySelectorAll('.menu, .suggestion-container').forEach(e => e.remove());`);
}

// The pointer outside the page: no hover highlight where Xvfb's pointer happens to be.
async function parkPointer(cdp) {
  await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: -40, y: -40 });
}

async function resetWorkspace(cdp, baseline, size) {
  await closeModals(cdp);
  await parkPointer(cdp);
  await evaluate(cdp, `await app.workspace.changeLayout(${J(baseline)});
    document.querySelectorAll('style[data-uibench]').forEach(s => s.remove());
    ${size.touch ? 'app.workspace.leftSplit.collapse(); app.workspace.rightSplit.collapse();'
      : `app.workspace.leftSplit.expand(); app.workspace.rightSplit.collapse();
         const fe = app.workspace.getLeavesOfType('file-explorer')[0]; if (fe) app.workspace.setActiveLeaf?.(fe, {focus: false});
         if (fe) fe.parent?.selectTab?.(fe);`}
    await new Promise(r => setTimeout(r, 200));`);
}

const KEYS = {
  Escape: { key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27 },
  Enter: { key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13, text: '\r' },
  ArrowDown: { key: 'ArrowDown', code: 'ArrowDown', windowsVirtualKeyCode: 40 },
  ArrowUp: { key: 'ArrowUp', code: 'ArrowUp', windowsVirtualKeyCode: 38 },
  Tab: { key: 'Tab', code: 'Tab', windowsVirtualKeyCode: 9 },
};

async function key(cdp, name) {
  const k = KEYS[name];
  if (!k) throw new Error(`unknown key ${name}`);
  await cdp.send('Input.dispatchKeyEvent', { type: 'keyDown', ...k });
  await cdp.send('Input.dispatchKeyEvent', { type: 'keyUp', ...k, text: undefined });
}

// The verbs of screens.json "obsidian.steps" (brulion-uimetrics check-manifest knows their arities).
// A string is evaluated in the page (inside an async function); a function does the step itself.
const STEPS = {
  open: (p) => `const f = app.vault.getAbstractFileByPath(${J(p)}); if (!f) throw new Error('no file ' + ${J(p)});
    await app.workspace.getLeaf(false).openFile(f, {active: true});
    await new Promise(r => setTimeout(r, 300));`,
  blur: () => `document.activeElement?.blur(); window.getSelection()?.removeAllRanges();`,
  cursorText: (text, offset) => `const ed = app.workspace.activeEditor?.editor; if (!ed) throw new Error('no editor');
    const lines = ed.getValue().split('\\n');
    const line = lines.findIndex(l => l.includes(${J(text)})); if (line < 0) throw new Error('no line ' + ${J(text)});
    ed.focus(); ed.setCursor({line, ch: Math.min(${Number(offset)}, lines[line].length)});
    await new Promise(r => setTimeout(r, 200));`,
  caretSteady: () => `const s = document.createElement('style'); s.dataset.uibench = '1';
    s.textContent = '.cm-cursorLayer{animation:none!important;opacity:1!important} .cm-cursor{visibility:visible!important;display:block!important}';
    document.head.append(s);`,
  command: (id) => `if (!app.commands.commands[${J(id)}] && !app.commands.editorCommands?.[${J(id)}]) throw new Error('no command ' + ${J(id)});
    if (!app.commands.executeCommandById(${J(id)})) throw new Error('command failed ' + ${J(id)});
    await new Promise(r => setTimeout(r, 300));`,
  type: (text) => async (cdp) => { await cdp.send('Input.insertText', { text }); },
  key: (name) => async (cdp) => { await key(cdp, name); },
  leftSidebar: (state) => `app.workspace.leftSplit.${state === 'open' ? 'expand' : 'collapse'}();
    await new Promise(r => setTimeout(r, 400));`,
  rightSidebar: (state) => `app.workspace.rightSplit.${state === 'open' ? 'expand' : 'collapse'}();
    await new Promise(r => setTimeout(r, 400));`,
  sidePane: (type) => `let leaf = app.workspace.getLeavesOfType(${J(type)})[0];
    if (!leaf) leaf = await app.workspace.ensureSideLeaf(${J(type)}, 'right', {active: true, reveal: true});
    if (!leaf) throw new Error('no side pane ' + ${J(type)});
    await app.workspace.revealLeaf(leaf);
    await new Promise(r => setTimeout(r, 300));`,
  search: (q) => `const p = app.internalPlugins.getPluginById('global-search'); if (!p?.enabled) throw new Error('no search');
    p.instance.openGlobalSearch(${J(q)});
    await new Promise(r => setTimeout(r, 300)); document.activeElement?.blur();`,
  // Obsidian 1.14 opens the settings in a window of their own on desktops; the bench keeps them in
  // the main window (its in-window settings modal, as before 1.14): one 1280×800 picture like
  // Brulion's settings view.
  settings: (tab) => `if (typeof app.setting.shouldUsePopout === 'function') app.setting.shouldUsePopout = () => false;
    app.setting.open(); app.setting.openTabById(${J(tab)});
    if (app.setting.activeTab?.id !== ${J(tab)}) throw new Error('no settings tab ' + ${J(tab)});
    await new Promise(r => setTimeout(r, 300));
    if (!document.querySelector('.modal.mod-settings')?.isConnected) throw new Error('the settings are not in the window');`,
  scroll: (selector) => `const el = document.querySelector(${J(selector)}); if (el) el.scrollTop = 0;`,
  wait: (ms) => async () => { await sleep(Number(ms)); },
  eval: (js) => js,
};

async function runStep(cdp, step) {
  const [verb, ...args] = step;
  const make = STEPS[verb];
  if (!make) throw new Error(`unknown step ${verb}`);
  const action = make(...args);
  if (typeof action === 'function') await action(cdp);
  else await evaluate(cdp, action);
}

async function settle(cdp, extraMs) {
  await evaluate(cdp, `await document.fonts.ready;
    await new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)));
    await new Promise((resolve) => {
      let timer = setTimeout(done, 400);
      const limit = setTimeout(done, 5000);
      const mo = new MutationObserver(() => { clearTimeout(timer); timer = setTimeout(done, 400); });
      mo.observe(document.body, {subtree: true, childList: true, attributes: true, characterData: true});
      function done() { mo.disconnect(); clearTimeout(timer); clearTimeout(limit); resolve(); }
    });`, 20000);
  if (extraMs) await sleep(extraMs);
}

// ---- The layout dump (schema 1 of src/app/ui/layoutdump.h, from the DOM) ---------------------------

const DOM_DUMP = String.raw`
const vw = innerWidth, vh = innerHeight;
const canvas = document.createElement('canvas'); canvas.width = 2; canvas.height = 1;
const ctx = canvas.getContext('2d', {willReadFrequently: true});
const cache = new Map();
const h2 = (n) => Math.max(0, Math.min(255, Math.round(n))).toString(16).padStart(2, '0');
// Any CSS colour (rgb(), oklch(), color(), color-mix() …) as "#rrggbbaa": painted over black and
// over white, which gives both the colour and its alpha.
function hex(css) {
  if (!css) return null;
  if (cache.has(css)) return cache.get(css);
  let m = css.match(/^rgba?\(\s*([\d.]+)[,\s]+([\d.]+)[,\s]+([\d.]+)(?:\s*[,/]\s*([\d.]+%?))?\s*\)$/);
  let out;
  if (m) {
    let a = m[4] === undefined ? 1 : (m[4].endsWith('%') ? parseFloat(m[4]) / 100 : parseFloat(m[4]));
    out = '#' + h2(+m[1]) + h2(+m[2]) + h2(+m[3]) + h2(a * 255);
  } else {
    ctx.globalCompositeOperation = 'copy';
    ctx.fillStyle = '#000'; ctx.fillRect(0, 0, 1, 1); ctx.fillStyle = '#fff'; ctx.fillRect(1, 0, 1, 1);
    ctx.globalCompositeOperation = 'source-over';
    ctx.fillStyle = '#0000'; ctx.fillStyle = css; ctx.fillRect(0, 0, 2, 1);
    const d = ctx.getImageData(0, 0, 2, 1).data;
    const a = 1 - (d[4] - d[0]) / 255;
    out = a <= 0.004 ? '#00000000' : '#' + h2(d[0] / a) + h2(d[1] / a) + h2(d[2] / a) + h2(a * 255);
  }
  cache.set(css, out);
  return out;
}
const alphaOf = (h) => h ? parseInt(h.slice(7, 9), 16) / 255 : 0;
const styleCache = new Map();
const cs = (el) => { let s = styleCache.get(el); if (!s) { s = getComputedStyle(el); styleCache.set(el, s); } return s; };
const r2 = (n) => Math.round(n * 100) / 100;
const rectArr = (r) => [r2(r.left), r2(r.top), r2(r.right - r.left), r2(r.bottom - r.top)];
const inter = (a, b) => ({left: Math.max(a.left, b.left), top: Math.max(a.top, b.top), right: Math.min(a.right, b.right), bottom: Math.min(a.bottom, b.bottom)});
const empty = (r) => r.right - r.left <= 0.5 || r.bottom - r.top <= 0.5;
const viewport = {left: 0, top: 0, right: vw, bottom: vh};
// The visible part of the page for an element: the viewport ∩ the boxes of ancestors that clip.
const clipCache = new Map();
function clipOf(el) {
  if (!el || el === document.documentElement) return viewport;
  if (clipCache.has(el)) return clipCache.get(el);
  let c = clipOf(el.parentElement);
  const s = cs(el);
  if (s.overflowX !== 'visible' || s.overflowY !== 'visible' || s.contain.includes('paint')) c = inter(c, el.getBoundingClientRect());
  clipCache.set(el, c);
  return c;
}
const visCache = new Map();
// Accumulated opacity, 0 when hidden.
function opacityOf(el) {
  if (!el || el === document.documentElement) return 1;
  if (visCache.has(el)) return visCache.get(el);
  const s = cs(el);
  let o = s.display === 'none' ? 0 : parseFloat(s.opacity) * opacityOf(el.parentElement);
  visCache.set(el, o);
  return o;
}
function occluded(el, r) {
  const x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
  const hit = document.elementFromPoint(x, y);
  if (!hit) return false;
  return !(hit === el || el.contains(hit) || hit.contains(el));
}
const skipTags = new Set(['SCRIPT', 'STYLE', 'TITLE', 'NOSCRIPT', 'TEMPLATE', 'OPTION']);
const fontPx = (s) => parseFloat(s.fontSize) || 0;

// ---- texts: text nodes grouped by their nearest non-inline ancestor (a paragraph with its links
// and tags is one text with colour fragments, as in Brulion's rich text).
const blocks = new Map();
const tw = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
const range = document.createRange();
for (let n = tw.nextNode(); n; n = tw.nextNode()) {
  if (!n.data.trim()) continue;
  const parent = n.parentElement;
  if (!parent || skipTags.has(parent.tagName) || parent.closest('svg')) continue;
  const ps = cs(parent);
  if (ps.visibility !== 'visible') continue;
  const op = opacityOf(parent);
  if (op <= 0.01) continue;
  let block = parent;
  while (block.parentElement && cs(block).display === 'inline' && block !== document.body) block = block.parentElement;
  const clip = clipOf(parent);
  range.selectNodeContents(n);
  const rects = [...range.getClientRects()].map(r => inter(r, clip)).filter(r => !empty(r));
  if (!rects.length) continue;
  let b = blocks.get(block);
  if (!b) { b = {el: block, frags: []}; blocks.set(block, b); }
  // An inline ancestor's own background (a tag pill, a highlight) under this text.
  let bg = null;
  for (let e = parent; e && e !== block; e = e.parentElement) {
    const c = hex(cs(e).backgroundColor);
    if (alphaOf(c) > 0.02) { bg = c; break; }
  }
  b.frags.push({text: n.data.replace(/\s+/g, ' '), parent, rects, color: hex(ps.color), alpha: op, bg,
                px: fontPx(ps), weight: parseInt(ps.fontWeight) || 400, family: ps.fontFamily});
}
const texts = [];
for (const b of blocks.values()) {
  const all = b.frags.flatMap(f => f.rects);
  const u = all.reduce((a, r) => ({left: Math.min(a.left, r.left), top: Math.min(a.top, r.top), right: Math.max(a.right, r.right), bottom: Math.max(a.bottom, r.bottom)}));
  // Lines: rectangles whose vertical centres differ by more than half the smaller height.
  const centres = all.map(r => [(r.top + r.bottom) / 2, r.bottom - r.top]).sort((a, c) => a[0] - c[0]);
  let lines = 0, last = -1e9, lastH = 0;
  for (const [c, h] of centres) { if (c - last > Math.min(h, lastH || h) / 2) { ++lines; last = c; lastH = h; } }
  const text = b.frags.map(f => f.text).join('').replace(/\s+/g, ' ').trim();
  if (!text) continue;
  // The fragment with the most characters gives the text's own colour, size and weight.
  const main = b.frags.reduce((a, f) => f.text.length > a.text.length ? f : a);
  const mixed = b.frags.some(f => f.color !== main.color || f.bg || Math.abs(f.alpha - main.alpha) > 0.01);
  const fragments = mixed ? b.frags.filter(f => f.text.trim()).map(f => {
    let color = f.color;
    if (f.alpha !== main.alpha && main.alpha > 0) {
      const a = Math.min(1, alphaOf(color) * f.alpha / main.alpha);
      color = color.slice(0, 7) + h2(a * 255);
    }
    const o = {text: f.text.trim().slice(0, 40), chars: f.text.trim().length, color};
    if (f.bg) o.bg = f.bg;
    return o;
  }) : [];
  const enabled = !b.el.closest('[disabled], .is-disabled, [aria-disabled="true"]');
  texts.push({type: b.el.tagName.toLowerCase() + (b.el.classList[0] ? '.' + b.el.classList[0] : ''), name: b.el.id || '',
              text: text.slice(0, 80), chars: text.length, rect: rectArr(u), color: main.color, alpha: r2(main.alpha),
              px: r2(main.px), weight: main.weight, family: main.family, lines: Math.max(1, lines), enabled,
              placeholder: false, occluded: occluded(b.el, u), fragments});
}
// Placeholders of empty fields.
for (const el of document.querySelectorAll('input[placeholder], textarea[placeholder]')) {
  if (el.value || !el.placeholder.trim() || opacityOf(el) <= 0.01 || cs(el).visibility !== 'visible') continue;
  const s = cs(el);
  const r0 = el.getBoundingClientRect();
  const r = inter({left: r0.left + parseFloat(s.paddingLeft), top: r0.top + parseFloat(s.paddingTop),
                   right: r0.right - parseFloat(s.paddingRight), bottom: r0.bottom - parseFloat(s.paddingBottom)}, clipOf(el));
  if (empty(r)) continue;
  texts.push({type: el.tagName.toLowerCase() + '::placeholder', name: el.id || '', text: el.placeholder.slice(0, 80),
              chars: el.placeholder.length, rect: rectArr(r), color: hex(getComputedStyle(el, '::placeholder').color),
              alpha: r2(opacityOf(el)), px: r2(fontPx(s)), weight: parseInt(s.fontWeight) || 400, family: s.fontFamily,
              lines: 1, enabled: !el.disabled, placeholder: true, occluded: occluded(el, r), fragments: []});
}

// ---- targets
const targetSel = 'a[href], button, input:not([type=hidden]), select, textarea, [role=button], [role=menuitem], [role=tab], ' +
  '[role=checkbox], [role=switch], [role=treeitem], [role=option], .clickable-icon, .tree-item-self, .nav-file-title, ' +
  '.nav-folder-title, .suggestion-item, .vertical-tab-nav-item, .checkbox-container, .workspace-tab-header, ' +
  '.mobile-navbar-action, .side-dock-ribbon-action, .view-action, .internal-link, .tag, .task-list-item-checkbox, ' +
  '[contenteditable=true]';
const candidates = new Set(document.querySelectorAll(targetSel));
for (const el of document.body.querySelectorAll('*')) {
  if (cs(el).cursor === 'pointer' && !(el.parentElement && cs(el.parentElement).cursor === 'pointer')) candidates.add(el);
}
const targetRects = new Map();
const targets = [];
for (const el of document.body.querySelectorAll('*')) {
  if (!candidates.has(el)) continue;
  const s = cs(el);
  if (s.visibility !== 'visible' || opacityOf(el) <= 0.01 || s.pointerEvents === 'none') continue;
  const r = inter(el.getBoundingClientRect(), clipOf(el.parentElement));
  if (empty(r)) continue;
  // A duplicate of its nearest candidate ancestor (a label filling its button) is the same target.
  let anc = el.parentElement;
  while (anc && !targetRects.has(anc)) anc = anc.parentElement;
  if (anc) {
    const a = targetRects.get(anc);
    if (Math.abs(a.left - r.left) <= 1 && Math.abs(a.top - r.top) <= 1 && Math.abs(a.right - r.right) <= 1 && Math.abs(a.bottom - r.bottom) <= 1) continue;
  }
  targetRects.set(el, r);
  const field = el.matches('input:not([type=checkbox]):not([type=radio]):not([type=button]):not([type=submit]), textarea, select, [contenteditable=true]');
  const inline = s.display === 'inline' && !!el.closest('p, li, .cm-line, td, th');
  const label = (el.getAttribute('aria-label') || el.textContent || el.getAttribute('title') || '').replace(/\s+/g, ' ').trim().slice(0, 60);
  const enabled = !el.matches(':disabled') && !el.closest('[disabled], .is-disabled, [aria-disabled="true"]');
  targets.push({type: el.tagName.toLowerCase() + (el.classList[0] ? '.' + el.classList[0] : ''), name: el.id || '',
                kind: field ? 'field' : 'button', label, rect: rectArr(r), enabled, inline, occluded: occluded(el, r)});
}

// ---- boxes: every visible element with a background or a border, in document order
const boxes = [];
let paint = 0;
for (const el of document.body.querySelectorAll('*')) {
  ++paint;
  const s = cs(el);
  if (s.visibility !== 'visible') continue;
  const op = opacityOf(el);
  if (op <= 0.01) continue;
  const color = hex(s.backgroundColor);
  const bw = Math.max(parseFloat(s.borderTopWidth) || 0, parseFloat(s.borderRightWidth) || 0,
                      parseFloat(s.borderBottomWidth) || 0, parseFloat(s.borderLeftWidth) || 0);
  const bc = hex(s.borderTopColor);
  const border = bw > 0 && s.borderTopStyle !== 'none' && alphaOf(bc) > 0.05 ? bw : 0;
  if (alphaOf(color) < 0.05 && !border) continue;
  const r = inter(el.getBoundingClientRect(), clipOf(el.parentElement));
  if (empty(r)) continue;
  boxes.push({type: el.tagName.toLowerCase() + (el.classList[0] ? '.' + el.classList[0] : ''), rect: rectArr(r), color,
              alpha: r2(op), border: r2(border), borderColor: border ? bc : '', radius: r2(parseFloat(s.borderTopLeftRadius) || 0), paint});
}

// ---- tokens (the theme's text colours and surfaces, resolved through a probe)
const probe = document.createElement('div');
probe.style.display = 'none';
document.body.append(probe);
const tokens = {};
for (const [name, v] of [['fg', '--text-normal'], ['fgMuted', '--text-muted'], ['fgFaint', '--text-faint'],
                         ['bg', '--background-primary'], ['bgElevated', '--background-primary-alt'],
                         ['bgSunken', '--background-secondary'], ['accent', '--interactive-accent'], ['link', '--link-color']]) {
  probe.style.color = 'var(' + v + ')';
  const c = hex(getComputedStyle(probe).color);
  if (c) tokens[name] = c;
}
probe.remove();
return JSON.stringify({schema: 1, app: 'obsidian', viewport: {w: vw, h: vh, dpr: devicePixelRatio}, texts, targets, boxes, tokens});
`;

function pngSize(buf) {
  if (buf.length < 24 || buf.toString('ascii', 1, 4) !== 'PNG') throw new Error('not a PNG');
  return { w: buf.readUInt32BE(16), h: buf.readUInt32BE(20) };
}

async function shoot(cdp, base, size) {
  await parkPointer(cdp);
  await sleep(100);
  const shot = await cdp.send('Page.captureScreenshot', { format: 'png', fromSurface: true, captureBeyondViewport: false });
  const png = Buffer.from(shot.data, 'base64');
  const dump = await evaluate(cdp, DOM_DUMP);
  const { w, h } = pngSize(png);
  const ew = Math.round(size.width * size.scale), eh = Math.round(size.height * size.scale);
  if (Math.abs(w - ew) > 1 || Math.abs(h - eh) > 1)
    throw new Error(`screenshot ${w}×${h}, expected ${ew}×${eh}`);
  writeFileSync(`${base}.png`, png);
  writeFileSync(`${base}.layout.json`, JSON.stringify(JSON.parse(dump), null, 1));
}

// ---- Main ----------------------------------------------------------------------------------------

async function startSession(args, size) {
  rmSync(join(args.profile, 'userdata', 'DevToolsActivePort'), { force: true });
  const child = launch(args.launch, args.profile, args.port, args.fonts, args.lang);
  try {
    const target = await pageTarget(args.port);
    const cdp = new Cdp(target.webSocketDebuggerUrl);
    await cdp.open();
    await boot(cdp, { size, lang: args.lang, now: args.now, version: args.version });
    return { child, cdp };
  } catch (e) {
    kill(child);
    throw new Error(`${e.message}\n--- Obsidian's output ---\n${child.log()}`);
  }
}

async function endSession(session) {
  if (!session) return;
  try { await session.cdp.send('Browser.close', {}, 5000); } catch { /* closing anyway */ }
  session.cdp.close();
  await sleep(5000);
  kill(session.child);
  await sleep(500);
}

// Polish UI: Obsidian reads the language at start-up. When `localStorage.language` was not
// enough, the setting's select (General) is set and Obsidian restarted once.
async function ensureLanguage(args, size, session, warnings) {
  const lang = await evaluate(session.cdp, 'return (window.i18next?.language ?? moment.locale()) || "";');
  if (lang.startsWith(args.lang)) return session;
  const changed = await evaluate(session.cdp, `app.setting.open(); app.setting.openTabById('about');
    await new Promise(r => setTimeout(r, 500));
    const sel = [...document.querySelectorAll('.modal select')].find(s => [...s.options].some(o => o.value === ${J(args.lang)}));
    if (!sel) { app.setting.close(); return false; }
    sel.value = ${J(args.lang)}; sel.dispatchEvent(new Event('change'));
    await new Promise(r => setTimeout(r, 500)); app.setting.close(); return true;`);
  if (changed) {
    await endSession(session);
    session = await startSession(args, size);
    const now = await evaluate(session.cdp, 'return (window.i18next?.language ?? moment.locale()) || "";');
    if (now.startsWith(args.lang)) return session;
  }
  warnings.push({ size: size.name, warning: 'language', detail: `Obsidian runs in ${lang}, not ${args.lang}` });
  return session;
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const manifest = JSON.parse(readFileSync(args.screens, 'utf8'));
  const sizes = Object.entries(manifest.sizes).filter(([n]) => !args.sizes || args.sizes.includes(n))
    .map(([name, s]) => ({ name, ...s }));
  const themes = manifest.themes.filter((t) => !args.themes || args.themes.includes(t));
  const screens = manifest.screens.filter((s) => s.obsidian && (!args.only || args.only.includes(s.id)));
  if (args.only) for (const id of args.only)
    if (!manifest.screens.some((s) => s.id === id)) throw new Error(`unknown screen ${id}`);
  mkdirSync(args.out, { recursive: true });
  rmSync(args.profile, { recursive: true, force: true });
  writeProfile(args.profile, args.vault);

  const result = { version: args.version, environment: {}, shots: [], failures: [], warnings: [] };
  const save = () => writeFileSync(join(args.out, 'obsidian-manifest.json'), JSON.stringify(result, null, 2));
  for (const size of sizes) {
    const wanted = screens.filter((s) => s.sizes.includes(size.name));
    if (!wanted.length) continue;
    let session;
    try {
      session = await startSession(args, size);
      session = await ensureLanguage(args, size, session, result.warnings);
      const env = await environment(session.cdp);
      result.environment[size.name] = env;
      console.error(`obsidian: ${size.name}: ${J(env)}`);
      if (size.touch && !env.isPhone) throw new Error('the phone layout is not active (body.is-phone is false)');
      if (!size.touch && env.isMobile) throw new Error('the desktop layout is mobile');
      if (env.appVersion && env.appVersion !== args.version)
        result.warnings.push({ size: size.name, warning: 'version', detail: `Obsidian ${env.appVersion}` });
      if (!env.plugins.includes('calendar'))
        result.warnings.push({ size: size.name, warning: 'calendar', detail: 'the Calendar plugin did not load' });
      const baseline = await evaluate(session.cdp, 'return app.workspace.getLayout();');
      for (const theme of themes) {
        const used = await setTheme(session.cdp, theme);
        result.environment[size.name][`theme-${theme}`] = used;
        for (const screen of wanted) {
          if (screen.id === 'calendar' && !env.plugins.includes('calendar')) {
            result.failures.push({ screen: screen.id, size: size.name, theme, error: 'n/a: no Calendar plugin' });
            continue;
          }
          const base = join(args.out, `${screen.id}-${size.name}-${theme}`);
          let error = null;
          for (let attempt = 0; attempt < 2; ++attempt) {
            try {
              await resetWorkspace(session.cdp, baseline, size);
              for (const step of screen.obsidian.steps) await runStep(session.cdp, step);
              await settle(session.cdp, screen.obsidian.settleMs ?? 0);
              // UIBENCH_DEBUG_JS: evaluated before each shot, its result logged (developing steps).
              if (process.env.UIBENCH_DEBUG_JS) {
                const targets = await (await fetch(`http://127.0.0.1:${args.port}/json/list`)).json();
                console.error(`obsidian: debug ${screen.id}: ${J(await evaluate(session.cdp, process.env.UIBENCH_DEBUG_JS))} targets ${J(targets.map((t) => `${t.type} ${t.url}`))}`);
              }
              await shoot(session.cdp, base, size);
              error = null;
              break;
            } catch (e) {
              error = e.message;
              console.error(`obsidian: ${screen.id}-${size.name}-${theme}: ${e.message}${attempt ? '' : ' (retrying)'}`);
            }
          }
          if (error) result.failures.push({ screen: screen.id, size: size.name, theme, error });
          else {
            result.shots.push({ screen: screen.id, size: size.name, theme, png: `${screen.id}-${size.name}-${theme}.png` });
            console.error(`obsidian: ${base}.png`);
          }
          save();
        }
      }
    } catch (e) {
      console.error(`obsidian: ${size.name}: ${e.message}`);
      for (const theme of themes)
        for (const screen of wanted)
          if (!result.shots.some((s) => s.screen === screen.id && s.size === size.name && s.theme === theme)
              && !result.failures.some((f) => f.screen === screen.id && f.size === size.name && f.theme === theme))
            result.failures.push({ screen: screen.id, size: size.name, theme, error: e.message.split('\n')[0] });
      result.warnings.push({ size: size.name, warning: 'session', detail: e.message });
    } finally {
      await endSession(session);
      save();
    }
  }
  save();
  console.error(`obsidian: ${result.shots.length} shots, ${result.failures.length} failures`);
  process.exit(result.shots.length ? 0 : 1);
}

main().catch((e) => {
  console.error(`obsidian-shots: ${e.stack ?? e.message}`);
  process.exit(2);
});
