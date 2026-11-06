// The single-file version (scripts/wasm-single-file.py; the release's brulion-web-<version>.html,
// docs/WEB.md "Single HTML file") opened from disk, file://, in Chromium and Firefox: the app starts
// from the page alone — no request leaves it, no service worker —, and its notes survive a reload
// (the storage self-test's counters in IndexedDB grow by one). The file: $BRULION_SINGLE_FILE
// (scripts/web-test.sh --single-file), default ../../dist-wasm-single/brulion.html.
import { existsSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { expect, test } from './lib/fixtures.mjs';
import { waitGlobal } from './lib/brulion.mjs';

const here = fileURLToPath(new URL('.', import.meta.url));
const file = resolve(process.env.BRULION_SINGLE_FILE || resolve(here, '../../dist-wasm-single/brulion.html'));

/** Loads the page (or reloads it) and returns the storage self-test's result. */
async function startApp(page, navigate) {
  await page.evaluate(() => { delete globalThis.brulionSelfTest; }).catch(() => {});
  await navigate();
  await page.waitForFunction(() => {
    const loader = document.getElementById('loader');
    if (loader && loader.getAttribute('role') === 'alert') {
      throw new Error('the page shows a problem: ' + document.getElementById('title').textContent + ' '
        + (document.getElementById('details').textContent || ''));
    }
    return loader && loader.hidden;
  }, null, { timeout: 180_000, polling: 250 });
  return waitGlobal(page, 'brulionSelfTest', (v) => v.flushed !== undefined);
}

// `watched`: an uncaught error of the page fails the test (lib/fixtures.mjs).
// eslint-disable-next-line no-unused-vars
test('jeden plik HTML z dysku: start bez serwera, notatki zostają po przeładowaniu', async ({ page, watched }) => {
  expect(existsSync(file), `${file} (scripts/wasm-build.sh, or scripts/web-test.sh --single-file)`).toBeTruthy();
  const page_ = pathToFileURL(file).href;
  const outside = [];
  page.on('request', (r) => {
    const url = r.url();
    if (url.split('?')[0] !== page_ && !url.startsWith('data:') && !url.startsWith('blob:')) outside.push(url);
  });

  const first = await startApp(page, () => page.goto(`${page_}?selftest=storage`));
  expect(first.backed, `IndexedDB from file:// (${first.note ?? ''})`).toBe(true);
  expect(first.flushed, `the first write-back (${first.note ?? ''})`).toBe(true);
  const before = first.counters.map((c) => c.now);
  expect(before.length).toBeGreaterThan(0);

  const second = await startApp(page, () => page.reload());
  expect(second.flushed, `the write-back after the reload (${second.note ?? ''})`).toBe(true);
  expect(second.counters.map((c) => c.now), 'the counters of the first start, each + 1')
    .toEqual(before.map((n) => n + 1));

  expect(outside, 'requests the page made besides itself').toEqual([]);
  const workers = await page.evaluate(async () => {
    try {
      return 'serviceWorker' in navigator ? (await navigator.serviceWorker.getRegistrations()).length : 0;
    } catch {
      return 0; // file:// has none
    }
  });
  expect(workers, 'service workers of the page').toBe(0);
  // The packed brulion.wasm leaves the page once read (wasm/index.html).
  expect(await page.evaluate(() => document.getElementById('brulion-wasm')?.textContent)).toBe('');
});
