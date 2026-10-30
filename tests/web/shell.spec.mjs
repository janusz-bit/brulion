// The page around the app (wasm/index.html), in Chromium and Firefox:
//
//  1. what a link to the site hands the app (its arguments), with the app stubbed (qtloader.js
//     records them; no wasm runs): the flows that work on the open notebook
//     (?selftest=webpictures,pen) and ?route= / ?now= only in a tab marked for tests
//     (sessionStorage, which a link cannot set — docs/WEB.md "Uruchamianie lokalnie"), and then
//     each one argument "--option=value", so a value never becomes an option of its own
//     (?route=--vault=… must not open another notebook);
//  2. the browser's timeouts for Qt's timers: every wait gets 2 ms more, 0 ms stays (stubbed
//     app); with the real app in a browser that runs timeouts a millisecond early, Qt's timers
//     keep firing — the drawing editor saves 1.2 s after the last line without any input
//     (docs/WEB.md "Ograniczenia techniczne").
import { expect, test } from './lib/fixtures.mjs';
import { decodeDrawing, line, markTestTab, mouseStroke, openApp, requireWebGl2, screenOffset, waitGlobal } from './lib/brulion.mjs';
import { readFile, seed } from './lib/idbfs.mjs';

const CLOCK = '2026-11-06 09:00';

const CASES = [
  {
    name: 'link bez znacznika karty: bez testów notatnika, trasy i zegara',
    mark: false,
    query: { selftest: 'webpictures,pen,storage', route: '--vault=/tmp/n', now: CLOCK },
    args: ['--selftest-storage'],
  },
  {
    name: 'link bez znacznika karty z pustym selftest',
    mark: false,
    query: { selftest: '', route: '--demo', now: CLOCK },
    args: [],
  },
  {
    name: 'karta testowa, pusty selftest: trasa i zegar pominięte',
    mark: true,
    query: { selftest: '', route: 'journals', now: CLOCK },
    args: [],
  },
  {
    name: 'karta testowa, nieznany test (zażółć): trasa i zegar pominięte',
    mark: true,
    query: { selftest: 'zażółć,PEN', route: 'journals', now: CLOCK },
    args: [],
  },
  {
    name: 'karta testowa, test bez notatnika (storage): trasa pominięta',
    mark: true,
    query: { selftest: 'storage', route: '--onboarding', now: CLOCK },
    args: ['--selftest-storage'],
  },
  {
    name: 'karta testowa: wartość nigdy nie jest osobną opcją',
    mark: true,
    query: { selftest: 'pen', route: '--vault=/tmp/n', now: CLOCK },
    args: ['--selftest-pen', '--route=--vault=/tmp/n', `--now=${CLOCK}`],
  },
  {
    name: 'karta testowa: trasa JSON z polskimi znakami, zły zegar pominięty',
    mark: true,
    query: { selftest: 'webpictures', route: '{"view":"page","path":"Łąka żółwia.md"}', now: '2026-11-06T09:00' },
    args: ['--selftest-webpictures', '--route={"view":"page","path":"Łąka żółwia.md"}'],
  },
  {
    name: 'karta testowa: za długa trasa pominięta',
    mark: true,
    query: { selftest: 'pen,webpictures', route: 'x'.repeat(2001), now: CLOCK },
    args: ['--selftest-pen', '--selftest-webpictures', `--now=${CLOCK}`],
  },
];

/** The app's files replaced: qtloader.js records the arguments, the wasm is an empty module. */
async function stubApp(page) {
  const at = (name) => (url) => url.pathname.endsWith(`/brulion/${name}`);
  await page.route(at('qtloader.js'), (route) => route.fulfill({
    contentType: 'text/javascript',
    body: 'window.qtLoad = function (config) { window.__brulionArgs = config.arguments.slice(); '
      + 'return new Promise(function () {}); };',
  }));
  await page.route(at('brulion.js'), (route) => route.fulfill({
    contentType: 'text/javascript',
    body: 'window.brulion_entry = function () {};',
  }));
  await page.route(at('brulion.wasm'), (route) => route.fulfill({
    contentType: 'application/wasm',
    body: Buffer.from([0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00]),
  }));
}

test.describe('Parametry adresu: link nie uruchamia testów na cudzym notatniku', () => {
  for (const c of CASES) {
    // `watched`: the page's console is attached, an uncaught page error fails the test.
    test(c.name, async ({ page, servers, watched, browserName }) => { // eslint-disable-line no-unused-vars
      const site = servers.page;
      await requireWebGl2(page, site.origin, browserName);
      await stubApp(page);
      if (c.mark) await markTestTab(page);
      const url = new URL(`${site.origin}/brulion/`);
      url.searchParams.set('nosw', '');
      for (const [k, v] of Object.entries(c.query)) url.searchParams.set(k, v);
      await page.goto(url.href);
      const handle = await page.waitForFunction(() => window.__brulionArgs, null, { timeout: 30_000 });
      const args = await handle.jsonValue();
      expect(args).toEqual(c.args);
      // Nothing but the test flows, the route and the clock — each a single argument.
      for (const a of args) expect(a).toMatch(/^--(selftest-[a-z]+$|route=|now=)/);
    });
  }
});

test.describe('Opóźnienia przeglądarki dla zegarów Qt', () => {
  test('każde opóźnienie > 0 ms dostaje 2 ms zapasu, 0 ms zostaje', async ({ page, servers, watched, browserName }) => { // eslint-disable-line no-unused-vars
    const site = servers.page;
    await requireWebGl2(page, site.origin, browserName);
    await stubApp(page);
    // A recorder in place of the browser's setTimeout, before the page's script wraps it.
    await page.addInitScript(() => {
      const native = window.setTimeout;
      window.__timeouts = [];
      window.setTimeout = function (handler, delay, ...rest) {
        window.__timeouts.push(delay);
        return native.call(window, handler, delay, ...rest);
      };
    });
    await page.goto(`${site.origin}/brulion/?nosw`);
    await page.waitForFunction(() => window.__brulionArgs, null, { timeout: 30_000 });
    const seen = await page.evaluate(() => {
      window.__timeouts.length = 0;
      for (const d of [1199, 1, 0, 250.5]) window.setTimeout(() => {}, d); // as Qt calls it
      setTimeout(() => {}, 7); // a bare call
      setTimeout(() => {}); // no wait at all
      setTimeout(() => {}, '15'); // not a number: as given
      window.setTimeout((a, b) => { window.__args = [a, b]; }, 1, 'ą', 'ż');
      window.__cancelled = false;
      clearTimeout(window.setTimeout(() => { window.__cancelled = true; }, 5));
      return window.__timeouts.map(String);
    });
    expect(seen).toEqual(['1201', '3', '0', '252.5', '9', 'undefined', '15', '3', '7']);
    await page.waitForFunction(() => window.__args, null, { timeout: 5_000 });
    expect(await page.evaluate(() => window.__args)).toEqual(['ą', 'ż']);
    await page.waitForTimeout(100);
    expect(await page.evaluate(() => window.__cancelled)).toBe(false);
  });

  test('przeglądarka uruchamia setTimeout 1 ms za wcześnie: rysunek i tak się zapisuje',
    async ({ page, servers, watched, browserName }) => { // eslint-disable-line no-unused-vars
      const origin = servers.page.origin;
      await requireWebGl2(page, origin, browserName);
      // Every timeout with a wait runs 1 ms early (as Firefox's may, against Qt's coarse clock):
      // without the page's margin the first Qt timer due stops all of them — in Chromium every
      // time (the save never comes), Firefox runs its timeouts late enough here to pass anyway.
      await page.addInitScript(() => {
        const native = window.setTimeout;
        window.setTimeout = function (handler, delay, ...rest) {
          return native.call(window, handler, typeof delay === 'number' && delay > 0 ? Math.max(0, delay - 1) : delay,
            ...rest);
        };
      });
      const drawing = 'Rysunki/Zegar — żółw.svg';
      await seed(page, origin, { files: { [drawing]: Buffer.alloc(0) } }); // 0 bytes: a new drawing
      await openApp(page, origin, { selftest: 'pen', route: { view: 'drawing', path: drawing } });
      const report = await waitGlobal(page, 'brulionPenSelfTest', (v) => v.found && v.editable, 150_000);
      const off = await screenOffset(page);
      const c = report.canvas;
      const y = off.y + c.y + 0.5 * c.height;
      await mouseStroke(page, line(off.x + c.x + 0.25 * c.width, y, off.x + c.x + 0.75 * c.width, y, 30));
      await waitGlobal(page, 'brulionPenSelfTest', '(v) => v.strokeCount === 1', 15_000);
      // No input from here on: the editor's save timer (1.2 s) must fire by itself.
      await expect.poll(async () => {
        const bytes = await readFile(page, drawing);
        if (!bytes || !bytes.length) return -1;
        return (decodeDrawing(bytes)?.elements || []).filter((e) => e.type === 'freedraw' && !e.isDeleted).length;
      }, { message: 'the line in the saved drawing', timeout: 20_000 }).toBe(1);
      expect((await waitGlobal(page, 'brulionPenSelfTest')).modified).toBe(false);
    });
});
