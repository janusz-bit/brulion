// The Web Clipper in a real Chromium (docs/CLIPPER.md "Tests"): the unpacked extension with brulion-cli as
// its native messaging host (the manifest `brulion-cli clip manifest` writes, in the profile's
// NativeMessagingHosts folder), a notebook of its own (BRULION_VAULT), and a Polish article served on
// 127.0.0.1. Browsers grant activeTab only on a real click, so the test loads a copy of the
// extension that may also read the test server's pages (host_permissions) — everything else is the
// extension as it ships: the popup (its form, the preview, "Clip", the result), the background's
// context-menu clips (selection into the journal, a screenshot, a link) and the popup without a host.
//
// Run: scripts/web-test.sh --clipper (BRULION_CLI: the brulion-cli to use).
import { test, expect, chromium } from '@playwright/test';
import { execFileSync } from 'node:child_process';
import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import http from 'node:http';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { picturePng } from './lib/servers.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../..');
const cli = process.env.BRULION_CLI || join(root, 'build-clang/src/cli/brulion-cli');
const EXTENSION_ID = 'calcbkckojhmdfecpcohaangneahmkgg';

const ARTICLE = `<!doctype html>
<html lang="pl"><head><meta charset="utf-8">
<title>Żółw stepowy w ogrodzie — Przyroda Dziś</title>
<meta property="og:title" content="Żółw stepowy w ogrodzie">
<meta property="og:site_name" content="Przyroda Dziś">
<meta name="author" content="Zofia Łęcka">
<meta property="article:published_time" content="2026-11-19T08:15:00+01:00">
<script>window.licznik = 1;</script>
</head><body>
<nav class="menu"><a href="/">Strona główna</a> · <a href="/gady">Gady</a> · <a href="/kontakt">Kontakt</a></nav>
<main><article>
<h1>Żółw stepowy w ogrodzie</h1>
<p id="pierwszy">Żółw stepowy to gad, który <b>źle znosi</b> chłodne i wilgotne jesienie, dlatego od października potrzebuje ciepłego, suchego miejsca. Więcej w <a href="/poradniki/zimowanie">poradniku o zimowaniu</a>.</p>
<figure><img src="/zdjecia/zolw.png" alt="Żółw na trawie"><figcaption>Żółw na trawie. Fot. J. Kowalski</figcaption></figure>
<p>Zimowanie trwa od listopada do marca. Przed nim żółw przestaje jeść, a jego jelita powinny być puste; temperatura w miejscu zimowania nie może spaść poniżej czterech stopni ani wzrosnąć ponad osiem.</p>
<h2>Co jeść, a czego unikać</h2>
<ul><li>Mniszek lekarski — liście i kwiaty</li><li>Koniczyna i babka lancetowata</li><li>Nie podawać owoców ani mięsa</li></ul>
<pre><code class="language-python">def temperatura(c):
    return c * 9 / 5 + 32
</code></pre>
<p>Przed zimowaniem warto zważyć żółwia i zapisywać jego wagę co dwa tygodnie: utrata ponad dziesięciu procent masy to sygnał, że trzeba go wybudzić i pokazać weterynarzowi.</p>
<p><a id="poradnik" href="/poradniki/zimowanie">Poradnik zimowania</a></p>
</article></main>
<footer>© 2026 Przyroda Dziś</footer>
</body></html>`;

// Formulas of three kinds: KaTeX's MathML with its TeX, MathJax 2's script (which Readability drops: the
// extension keeps it as an element of its copy) beside the rendered spans, and a MathML without any TeX.
const FORMULAS = `<!doctype html>
<html lang="pl"><head><meta charset="utf-8"><title>Wzory w ogrodzie</title></head><body>
<nav class="menu"><a href="/">Strona główna</a></nav>
<main><article>
<h1>Wzory w ogrodzie</h1>
<p>Ogrodnik liczy pole grządki, a potem wysiewa na niej nasiona w równych odstępach, dlatego od wiosny potrzebuje prostych wzorów, które da się zapisać na kartce i sprawdzić po pracy.</p>
<p>Pole koła: <span class="katex"><span class="katex-mathml"><math><semantics><mrow><mi>P</mi><mo>=</mo><mi>π</mi><msup><mi>r</mi><mn>2</mn></msup></mrow><annotation encoding="application/x-tex">P = \\pi r^2</annotation></semantics></math></span><span class="katex-html" aria-hidden="true"><span>PIERWSZA KOPIA</span></span></span>, a obwód: <span class="MathJax_Preview"></span><span class="MathJax" id="MathJax-Element-1-Frame"><span>DRUGA KOPIA</span></span><script type="math/tex" id="MathJax-Element-1">L = 2\\pi r</script>.</p>
<p>Średnia z pomiarów: <math><mover><mi>x</mi><mo>¯</mo></mover><mo>=</mo><mfrac><mn>1</mn><mi>n</mi></mfrac><munderover><mo>∑</mo><mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow><mi>n</mi></munderover><msub><mi>x</mi><mi>i</mi></msub></math>, czyli tyle, ile wyszło po podzieleniu sumy przez liczbę grządek.</p>
<p>Wzory z kartki warto przepisać do zeszytu ogrodnika razem z datą siewu i uwagami o pogodzie, bo za rok będą potrzebne do porównania plonów z poprzednim sezonem.</p>
</article></main>
</body></html>`;

let server;
let base;
let work;
let vault;
let userData;
let context;
let worker;
const today = (() => {
  const d = new Date();
  const pad = (n) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
})();

function hostManifestPath() {
  return join(userData, 'NativeMessagingHosts', 'io.github.januszbit.brulion.json');
}

function writeHostManifest() {
  const manifest = execFileSync(cli, ['clip', 'manifest', 'chromium', '--program', cli], { encoding: 'utf8' });
  mkdirSync(dirname(hostManifestPath()), { recursive: true });
  writeFileSync(hostManifestPath(), manifest);
}

async function tabOf(url) {
  return worker.evaluate(async (u) => (await chrome.tabs.query({ url: u }))[0]?.id, url);
}

async function setSettings(settings) {
  await worker.evaluate((s) => chrome.storage.local.set({ settings: s }), settings);
}

async function quickClip(tabId, kind, info = {}) {
  await worker.evaluate(
    async ([id, k, i]) => {
      const tab = await chrome.tabs.get(id);
      await globalThis.brulionClipper.quickClip(tab, k, i);
    },
    [tabId, kind, info],
  );
}

/** BRULION_CLIPPER_SCREENSHOTS=<dir>: the popup and the options page in the light and the dark. */
async function screenshots(page, name) {
  const dir = process.env.BRULION_CLIPPER_SCREENSHOTS;
  if (!dir) return;
  mkdirSync(dir, { recursive: true });
  for (const scheme of ['light', 'dark']) {
    await page.emulateMedia({ colorScheme: scheme });
    await page.screenshot({ path: join(dir, `${name}-${scheme}.png`), fullPage: true });
  }
  await page.emulateMedia({ colorScheme: 'light' });
}

function readNote(path) {
  return readFileSync(join(vault, path), 'utf8');
}

test.describe.configure({ mode: 'serial' });

test.beforeAll(async () => {
  expect(existsSync(cli), `brulion-cli at ${cli} (BRULION_CLI)`).toBe(true);
  server = http.createServer((req, res) => {
    const path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    if (path === '/artykul.html') {
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(ARTICLE);
    } else if (path === '/wzory.html') {
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(FORMULAS);
    } else if (path === '/zdjecia/zolw.png') {
      res.writeHead(200, { 'content-type': 'image/png' });
      res.end(picturePng());
    } else {
      res.writeHead(404);
      res.end('brak');
    }
  });
  await new Promise((ok) => server.listen(0, '127.0.0.1', ok));
  base = `http://127.0.0.1:${server.address().port}`;

  work = mkdtempSync(join(tmpdir(), 'brulion-clipper-'));
  const home = join(work, 'home');
  mkdirSync(home, { recursive: true });
  vault = join(work, 'Notatnik');
  execFileSync(cli, ['init', vault]);
  userData = join(work, 'profile');
  writeHostManifest();

  // The extension as it ships, plus the test server's pages (no click grants activeTab here).
  const extension = join(work, 'extension');
  cpSync(join(root, 'clipper'), extension, { recursive: true, filter: (src) => !src.includes(`${join('clipper', 'test')}`) });
  const manifest = JSON.parse(readFileSync(join(extension, 'manifest.json'), 'utf8'));
  manifest.host_permissions = ['<all_urls>'];
  writeFileSync(join(extension, 'manifest.json'), JSON.stringify(manifest, null, 2));

  context = await chromium.launchPersistentContext(userData, {
    channel: 'chromium',
    headless: true,
    locale: 'pl-PL',
    viewport: { width: 1280, height: 800 },
    args: [`--disable-extensions-except=${extension}`, `--load-extension=${extension}`],
    env: {
      ...process.env,
      HOME: home,
      XDG_CONFIG_HOME: join(home, 'config'),
      XDG_DATA_HOME: join(home, 'data'),
      XDG_STATE_HOME: join(home, 'state'),
      XDG_CACHE_HOME: join(home, 'cache'),
      BRULION_VAULT: vault,
    },
  });
  worker = context.serviceWorkers()[0] ?? (await context.waitForEvent('serviceworker'));
  expect(new URL(worker.url()).host).toBe(EXTENSION_ID);
});

test.afterAll(async () => {
  await context?.close();
  server?.close();
  if (work) rmSync(work, { recursive: true, force: true });
});

test('the popup clips the article into a new page with its picture', async () => {
  const page = await context.newPage();
  await page.goto(`${base}/artykul.html`);
  const tabId = await tabOf(`${base}/artykul.html`);
  const popup = await context.newPage();
  const errors = [];
  popup.on('pageerror', (e) => errors.push(String(e)));
  await popup.goto(`chrome-extension://${EXTENSION_ID}/popup.html?tab=${tabId}`);
  await expect(popup.locator('#form')).toBeVisible();
  // Polish texts (the browser's language).
  await expect(popup.locator('#save')).toHaveText('Wytnij');
  await expect(popup.locator('input[name="kind"][value="article"]')).toBeChecked();
  await expect(popup.locator('input[name="kind"][value="selection"]')).toBeDisabled();
  await expect(popup.locator('#title')).toHaveValue('Żółw stepowy w ogrodzie');
  await expect(popup.locator('#folder')).toHaveValue('Wycinki');
  await expect(popup.locator('#template')).toHaveValue('builtin:brulion');
  await popup.fill('#tags', 'gady, ogród zimą');
  await popup.setViewportSize({ width: 400, height: 640 });
  await screenshots(popup, 'popup');

  await popup.locator('#preview-box summary').click();
  await expect(popup.locator('#preview')).toContainText('source: http://127.0.0.1:');
  await expect(popup.locator('#preview')).toContainText('Żółw stepowy to gad, który **źle znosi**');
  expect(existsSync(join(vault, 'Wycinki'))).toBe(false); // a preview writes nothing

  await popup.click('#save');
  await expect(popup.locator('#result')).toBeVisible();
  await expect(popup.locator('#result-path')).toHaveText('Wycinki/Żółw stepowy w ogrodzie.md');
  await expect(popup.locator('#result-detail')).toContainText('1');
  await screenshots(popup, 'popup-result');
  const note = readNote('Wycinki/Żółw stepowy w ogrodzie.md');
  expect(note).toContain(`source: ${base}/artykul.html`);
  expect(note).toContain('author: Zofia Łęcka');
  expect(note).toContain('published: 2026-11-19');
  expect(note).toContain('tags: [gady, ogród zimą]');
  expect(note).toContain('![[zolw.png]]');
  expect(note).toContain('*Żółw na trawie. Fot. J. Kowalski*');
  expect(note).toContain('```python\ndef temperatura(c):');
  expect(note).toContain(`[poradniku o zimowaniu](${base}/poradniki/zimowanie)`);
  expect(note).not.toContain('Strona główna'); // Readability left the menu out
  expect(note).not.toContain('licznik');
  expect(readFileSync(join(vault, 'assets', 'zolw.png'))).toEqual(picturePng());
  // The next popup starts where this clip went.
  const stored = await worker.evaluate(() => chrome.storage.local.get('settings'));
  expect(stored.settings.folder).toBe('Wycinki');
  expect(errors).toEqual([]);
  await popup.close();
  await page.close();
});

test('a selection from the context menu goes into today\'s journal', async () => {
  await setSettings({ place: 'journal', templateBlock: 'builtin:link', tags: 'gady' });
  const page = await context.newPage();
  await page.goto(`${base}/artykul.html`);
  await page.evaluate(() => {
    const range = document.createRange();
    range.selectNodeContents(document.getElementById('pierwszy'));
    getSelection().removeAllRanges();
    getSelection().addRange(range);
  });
  await quickClip(await tabOf(`${base}/artykul.html`), 'selection');
  const journal = readNote(`journals/${today}.md`);
  expect(journal).toContain(`- [Żółw stepowy w ogrodzie](${base}/artykul.html) #gady\n`);
  expect(journal).toContain('  > Żółw stepowy to gad, który **źle znosi** chłodne i wilgotne jesienie');
  await page.close();
});

test('a link from its context menu', async () => {
  const page = await context.newPage();
  await page.goto(`${base}/artykul.html`);
  await quickClip(await tabOf(`${base}/artykul.html`), 'link', {
    linkUrl: `${base}/poradniki/zimowanie`,
    linkText: 'Poradnik zimowania',
    pageUrl: `${base}/artykul.html`,
  });
  expect(readNote(`journals/${today}.md`)).toContain(`- [Poradnik zimowania](${base}/poradniki/zimowanie) #gady\n`);
  await page.close();
});

test('a screenshot of the visible part becomes a page with the picture', async () => {
  await setSettings({ place: 'page', folder: 'Zrzuty', templatePage: 'builtin:brulion' });
  const page = await context.newPage();
  await page.goto(`${base}/artykul.html`);
  await page.bringToFront();
  await quickClip(await tabOf(`${base}/artykul.html`), 'screenshot');
  const note = readNote('Zrzuty/Żółw stepowy w ogrodzie.md');
  expect(note).toMatch(/!\[\[Żółw stepowy w ogrodzie 1\.png\]\]/);
  const shot = readFileSync(join(vault, 'assets', 'Żółw stepowy w ogrodzie 1.png'));
  expect(shot.subarray(1, 4).toString()).toBe('PNG');
  await page.close();
});

test('formulas of an article become $…$ with their TeX', async () => {
  await setSettings({ place: 'page', folder: 'Wzory', templatePage: 'builtin:brulion' });
  const page = await context.newPage();
  await page.goto(`${base}/wzory.html`);
  const popup = await context.newPage();
  await popup.goto(`chrome-extension://${EXTENSION_ID}/popup.html?tab=${await tabOf(`${base}/wzory.html`)}`);
  await expect(popup.locator('#form')).toBeVisible();
  await popup.click('#save');
  await expect(popup.locator('#result')).toBeVisible();
  const note = readNote('Wzory/Wzory w ogrodzie.md');
  expect(note).toContain('Pole koła: $P = \\pi r^2$, a obwód: $L = 2\\pi r$.');
  expect(note).toContain('Średnia z pomiarów: $\\overline{x}=\\frac{1}{n}\\sum_{i=1}^{n}x_i$, czyli');
  expect(note).not.toContain('KOPIA');
  await popup.close();
  await page.close();
});

test('without the host the popup says how to connect and offers copying', async () => {
  rmSync(hostManifestPath());
  const page = await context.newPage();
  await page.goto(`${base}/artykul.html`);
  const popup = await context.newPage();
  await popup.goto(`chrome-extension://${EXTENSION_ID}/popup.html?tab=${await tabOf(`${base}/artykul.html`)}`);
  await expect(popup.locator('#disconnected')).toBeVisible();
  await expect(popup.locator('#install-command')).toHaveText('brulion-cli clip install');
  await expect(popup.locator('#copy-rich')).toBeVisible();
  await expect(popup.locator('#form')).toBeHidden();
  await popup.setViewportSize({ width: 400, height: 640 });
  await screenshots(popup, 'popup-disconnected');
  writeHostManifest();
  const options = await context.newPage();
  await options.setViewportSize({ width: 900, height: 900 });
  await options.goto(`chrome-extension://${EXTENSION_ID}/options.html`);
  await expect(options.locator('#connection')).toContainText('Notatnik');
  await screenshots(options, 'options');
  expect(readdirSync(join(vault, 'Zrzuty'))).toEqual(['Żółw stepowy w ogrodzie.md']);
});
