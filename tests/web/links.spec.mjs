// What a link to the site hands the app (wasm/index.html → the app's arguments), in Chromium and
// Firefox, with the app itself stubbed (qtloader.js records its arguments; no wasm runs): the
// flows that work on the open notebook (?selftest=webpictures,pen) and ?route= / ?now= only in a
// tab marked for tests (sessionStorage, which a link cannot set — docs/WEB.md "Parametry
// adresu"), and then each one argument "--option=value", so a value never becomes an option of
// its own (?route=--vault=… must not open another notebook).
import { expect, test } from './lib/fixtures.mjs';
import { markTestTab, requireWebGl2 } from './lib/brulion.mjs';

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
