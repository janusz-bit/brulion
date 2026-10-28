#!/usr/bin/env node
// The browser check of a drawing set to "Jak motyw" (docs/SYNTAX.md §2 "Drawings", "Light and
// dark"): its SVG holds the light picture in attributes and the dark colours in one
// `@media (prefers-color-scheme: dark)` block, so a browser (Obsidian, GitHub) shows it dark when
// its viewer prefers dark. Chromium (headless, over the DevTools protocol — the minimal client of
// scripts/wasm-smoke.mjs; Node >= 22 for the global WebSocket) loads the golden file of
// tst_drawing (svgAutoGolden) with the preferred colour scheme emulated light and dark:
//  * as a page of its own: the paper's computed fill is #ffffff / #121212;
//  * inside <img>, as notes embed it: read back through a canvas, the paper pixel is #ffffff /
//    #121212 and the ink is #1f2430 / #caced8 (± 2), hardly ever the other theme's;
//  * inside <img> on a page whose own colour scheme is the other one (`color-scheme` on its body:
//    an app's theme unlike the system's, as Obsidian's dark theme on a light system): the page's.
//
//   node tests/app/drawing/svg-theme-check.mjs [tests/data/drawing/auto_theme.svg]
//
// Chromium: $CHROME or chromium/chromium-browser/google-chrome on PATH; BRULION_CHROME_NO_SANDBOX=1
// (or CI=true) where its sandbox cannot start. Exit code 0 = every check passed.
import { spawn, execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { setTimeout as sleep } from 'node:timers/promises';

const here = dirname(fileURLToPath(import.meta.url));
const svgFile = process.argv[2] ?? join(here, '../../data/drawing/auto_theme.svg');

const themes = {
  light: { paper: [0xff, 0xff, 0xff], ink: [0x1f, 0x24, 0x30], other: [0xca, 0xce, 0xd8] },
  dark: { paper: [0x12, 0x12, 0x12], ink: [0xca, 0xce, 0xd8], other: [0x1f, 0x24, 0x30] },
};

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

/// `pixel` (RGB or RGBA) is `colour` (RGB) within `tolerance` a channel.
const near = (pixel, colour, tolerance = 2) => colour.every((v, i) => Math.abs(pixel[i] - v) <= tolerance);
const hex = (c) => '#' + c.slice(0, 3).map((v) => v.toString(16).padStart(2, '0')).join('');

async function main() {
  const svg = readFileSync(svgFile);
  if (!svg.includes('@media (prefers-color-scheme: dark)')) throw new Error(`${svgFile}: no dark style sheet`);
  const svgUrl = `data:image/svg+xml;base64,${svg.toString('base64')}`;
  const profile = mkdtempSync(join(tmpdir(), 'brulion-svg-theme-'));
  const noSandbox = process.env.BRULION_CHROME_NO_SANDBOX === '1' || process.env.CI === 'true';
  const chrome = spawn(findChrome(), [
    '--headless=new',
    `--user-data-dir=${profile}`,
    '--remote-debugging-port=0',
    '--no-first-run',
    '--no-default-browser-check',
    '--disable-background-networking',
    '--disable-component-update',
    '--window-size=800,600',
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
    const navigate = async (url) => {
      await cdp.send('Page.navigate', { url });
      for (const end = Date.now() + 15000; Date.now() < end; await sleep(100)) {
        if ((await evaluate('document.readyState').catch(() => '')) === 'complete') return;
      }
      throw new Error('the page did not load');
    };

    for (const [scheme, want] of Object.entries(themes)) {
      await cdp.send('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-color-scheme', value: scheme }] });
      // The file as a page of its own: the style sheet applies to the paper.
      await navigate(svgUrl);
      const fill = await evaluate(`getComputedStyle(document.querySelector('rect.p')).fill`);
      const fillOk = fill === `rgb(${want.paper.join(', ')})`;
      console.log(`${fillOk ? 'ok' : 'FAIL'}: ${scheme}, the file as a page: the paper's fill ${fill}`);
      if (!fillOk) problems.push(`${scheme}: the paper's fill is ${fill}`);

      // Inside <img>, as notes embed it: the pixels.
      await checkImg(scheme, '', want, `${scheme}, inside <img>`);
    }
    // An app whose own theme differs from the system's (Obsidian's dark theme on a light system:
    // its body has `color-scheme: dark`): Chromium shows an SVG image in the colour scheme its
    // <img> uses, so the drawing follows the app's theme — as Obsidian 1.14.4 was seen to do.
    await checkImg('light', 'color-scheme:dark', themes.dark, "the app's dark theme on a light system");
    await checkImg('dark', 'color-scheme:light', themes.light, "the app's light theme on a dark system");
    cdp.close();

    async function checkImg(scheme, bodyStyle, want, what) {
      await cdp.send('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-color-scheme', value: scheme }] });
      const html = `<!doctype html><body style="margin:0;${bodyStyle}"><img id="i" src="${svgUrl}"></body>`;
      await navigate(`data:text/html,${encodeURIComponent(html)}`);
      const pixels = await evaluate(`(async () => {
        const img = document.getElementById('i');
        await img.decode();
        const c = document.createElement('canvas');
        c.width = img.naturalWidth;
        c.height = img.naturalHeight;
        const g = c.getContext('2d');
        g.drawImage(img, 0, 0);
        const data = g.getImageData(0, 0, c.width, c.height).data;
        return { width: c.width, height: c.height, data: Array.from(data) };
      })()`);
      const at = (x, y) => pixels.data.slice((y * pixels.width + x) * 4, (y * pixels.width + x) * 4 + 4);
      const count = (colour) => {
        let n = 0;
        for (let i = 0; i < pixels.data.length; i += 4) {
          if (near(pixels.data.slice(i, i + 3), colour)) ++n;
        }
        return n;
      };
      const paper = at(2, 2);
      const ink = count(want.ink);
      const other = count(want.other);
      // (Antialiased edges are mixtures: the other theme's ink at most in a few of them.)
      const ok = near(paper, want.paper) && ink >= 20 && other * 10 < ink;
      console.log(`${ok ? 'ok' : 'FAIL'}: ${what}: paper ${hex(paper)}, ${ink} ink pixels ${hex(want.ink)}, `
        + `${other} of the other theme's ink`);
      if (!ok) problems.push(`${what}: paper ${hex(paper)}, ink ${ink}, other ${other}`);
    }
  } finally {
    chrome.kill();
    await sleep(300);
    rmSync(profile, { recursive: true, force: true });
  }
  if (problems.length) {
    console.log(`svg-theme-check: ${problems.length} problem(s)`);
    process.exit(1);
  }
  console.log('svg-theme-check: passed');
}

main().catch((e) => {
  console.error(e.stack ?? String(e));
  process.exit(1);
});
