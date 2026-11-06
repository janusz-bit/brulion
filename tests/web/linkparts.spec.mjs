// The path parts of a link to a page in a folder in the web build (docs/SYNTAX.md §2 "Path parts",
// docs/WEB.md "Browser tests (Playwright)"), in Chromium and Firefox: [[Projekty/Remont mieszkania]]
// in today's journal is drawn as two links; the app reports where they are on screen and the route
// shown (?selftest=linkparts: src/app/diag/webselftest.h); the real browser mouse — and, in a
// phone-sized Chromium with touch, a real tap — on "Projekty" opens the folder's pages (All pages
// with the folder's path), on "Remont mieszkania" the page. The journal is never changed.
import { expect, test } from './lib/fixtures.mjs';
import { openApp, requireWebGl2, screenOffset, TODAY, waitGlobal } from './lib/brulion.mjs';
import { readFile, seed } from './lib/idbfs.mjs';

const JOURNAL = `journals/${TODAY}.md`;
const ROUTE = { view: 'page', path: JOURNAL };
const LINE = '- Zapytać się [[Projekty/Remont mieszkania]] o plan\n';
const FILES = {
  [JOURNAL]: LINE,
  'Projekty/Remont mieszkania.md': '- Kupić płytki\n',
};

/** The part's and the page's boxes once the journal shows the link. */
async function partsShown(page) {
  const r = await waitGlobal(page, 'brulionLinkPartsSelfTest',
    (v) => v.ready && v.parts.some((p) => p.href.startsWith('part:1:')) && v.parts.some((p) => p.href.startsWith('page:')),
    150_000);
  return {
    part: r.parts.find((p) => p.href.startsWith('part:1:')),
    page: r.parts.find((p) => p.href.startsWith('page:Projekty%2FRemont')),
  };
}

/** The route once it is `view` with `path`. */
function routeIs(page, view, path) {
  return waitGlobal(page, 'brulionLinkPartsSelfTest',
    `(v) => v.route.view === ${JSON.stringify(view)} && v.route.path === ${JSON.stringify(path)}`, 30_000);
}

/** Clicks (or taps) the centre of `box` (window coordinates of the app). */
async function hit(page, box, touch) {
  const off = await screenOffset(page);
  const x = off.x + box.x + box.width / 2;
  const y = off.y + box.y + box.height / 2;
  if (touch) await page.touchscreen.tap(x, y);
  else await page.mouse.click(x, y);
}

async function run(page, origin, touch) {
  await seed(page, origin, { files: FILES });
  await openApp(page, origin, { selftest: 'linkparts', route: ROUTE });
  let boxes = await partsShown(page);
  expect(boxes.part.href).toBe('part:1:Projekty%2FRemont%20mieszkania');
  expect(boxes.part.x + boxes.part.width).toBeLessThan(boxes.page.x); // "Projekty" / "Remont mieszkania"

  // "Projekty": no page of that name, a folder with a page: its pages.
  await hit(page, boxes.part, touch);
  await routeIs(page, 'allPages', 'Projekty');

  // Back to the journal (Alt+←), then "Remont mieszkania": the page.
  await page.keyboard.press('Alt+ArrowLeft');
  await routeIs(page, 'page', JOURNAL);
  boxes = await partsShown(page);
  await hit(page, boxes.page, touch);
  await routeIs(page, 'page', 'Projekty/Remont mieszkania.md');

  // The journal is as written.
  const saved = await readFile(page, JOURNAL);
  expect(Buffer.from(saved).toString('utf8')).toBe(LINE);
}

test.describe('Części linku w przeglądarce', () => {
  test('mysz: część ścieżki otwiera folder, ostatnia część stronę', async ({ page, servers, watched, browserName }) => {
    void watched;
    const origin = servers.page.origin;
    await requireWebGl2(page, origin, browserName);
    await run(page, origin, false);
  });

  test('telefon: stuknięcie w część ścieżki (Chromium z dotykiem)', async ({ browser, servers, browserName }) => {
    test.skip(browserName !== 'chromium', 'Playwright has touch contexts for Chromium only');
    const context = await browser.newContext({
      viewport: { width: 412, height: 915 }, hasTouch: true, isMobile: true, deviceScaleFactor: 1,
      ignoreHTTPSErrors: true, locale: 'pl-PL', timezoneId: 'Europe/Warsaw',
    });
    const page = await context.newPage();
    try {
      const origin = servers.page.origin;
      await requireWebGl2(page, origin, browserName);
      await run(page, origin, true);
    } finally {
      await context.close();
    }
  });
});
