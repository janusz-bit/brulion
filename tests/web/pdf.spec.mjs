// PDFs in the web build, in Chromium and Firefox (docs/WEB.md "PDF w przeglądarce"): no PDF renderer
// in the browser, so a PDF embedded in a note is the file's chip with a page icon, and a click on
// it — or on a link to the PDF — opens the PDF in a new browser tab (a blob: URL of the bytes,
// typed application/pdf) where the browser's own viewer scrolls, zooms and searches; the app's
// route stays the note (no page shows the PDF's bytes). The app reports where the chip and the
// link are on screen (?selftest=pdf: src/app/diag/webselftest.h), the test clicks them with the
// real mouse. A route to the PDF shows the fallback panel with "Otwórz w nowej karcie". A tab
// opened outside any click (?selftest=pdfauto) is either opened or blocked — then the PDF is
// downloaded and the app says so; the outcome per browser is recorded. Firefox under Playwright
// has its PDF viewer (pdf.js) turned off in the test profile, so there the new tab turns into a
// download of the same blob and closes: the download is checked instead (its name ends in .pdf
// because the blob is typed application/pdf; an HTML blob would show as a page).
import { readFileSync } from 'node:fs';
import { expect, test } from './lib/fixtures.mjs';
import { openApp, requireWebGl2, screenOffset, TODAY, waitGlobal } from './lib/brulion.mjs';
import { seed } from './lib/idbfs.mjs';

const JOURNAL = `journals/${TODAY}.md`;
const PDF_PATH = 'assets/wykład.pdf';
const PDF = readFileSync(new URL('../data/pdf/wyklad.pdf', import.meta.url));

/** The notebook: today's journal with the PDF embedded and linked. */
async function seedPdf(page, origin) {
  await seed(page, origin, {
    files: {
      [JOURNAL]: '- Slajdy ![[wykład.pdf]]\n- [[wykład.pdf]]\n- Zażółć gęślą jaźń\n',
      [PDF_PATH]: PDF,
    },
  });
}

/** A click at the centre of `rect` (window coordinates of the app) that opens a tab: the tab. */
async function clickOpensTab(page, context, rect, offset, fx = 0.5) {
  const [tab] = await Promise.all([
    context.waitForEvent('page', { timeout: 20_000 }),
    page.mouse.click(offset.x + rect.x + rect.w * fx, offset.y + rect.y + rect.h / 2),
  ]);
  await tab.waitForLoadState('domcontentloaded').catch(() => {});
  return tab;
}

/** The tab's document as fetched from its own URL: {url, type, bytes, start}. */
async function blobOf(tab) {
  return tab.evaluate(async () => {
    const response = await fetch(location.href);
    const buffer = new Uint8Array(await response.arrayBuffer());
    return {
      url: location.href,
      type: response.headers.get('content-type'),
      bytes: buffer.length,
      start: String.fromCharCode(...buffer.slice(0, 5)),
    };
  });
}

/**
 * What the tab opened: its document ({how: 'tab', url, type, bytes, start}) or, where the browser
 * saves a PDF instead of showing it (Firefox under Playwright: pdf.js off), the download the tab
 * turned into before it closed ({how: 'download', url, name, bytes, start}). `before`: the number
 * of downloads before the click.
 */
async function openedDocument(tab, watched, before) {
  try {
    return { how: 'tab', ...(await blobOf(tab)) };
  } catch (error) {
    if (!tab.isClosed() && watched.downloads.length === before) throw error;
  }
  await expect.poll(() => watched.downloads.length, { timeout: 20_000 }).toBeGreaterThan(before);
  const download = watched.downloads[before];
  const bytes = readFileSync(await download.path());
  return {
    how: 'download',
    url: download.url(),
    name: download.suggestedFilename(),
    bytes: bytes.length,
    start: bytes.subarray(0, 5).toString('latin1'),
  };
}

/** The checks of a document the chip or the link opened (see openedDocument). */
async function expectPdf(tab, opened, origin) {
  expect(opened.url.startsWith(`blob:${origin}/`)).toBe(true);
  expect(opened.bytes).toBe(PDF.length);
  expect(opened.start).toBe('%PDF-');
  if (opened.how === 'tab') {
    expect(opened.type).toContain('application/pdf');
    expect(await tab.evaluate(() => window.opener)).toBeNull(); // the opener cut
  } else {
    expect(opened.name).toBe('wykład.pdf');
  }
}

test.describe('PDF w przeglądarce', () => {
  test('miniatura to kafelek, klik otwiera nową kartę z PDF-em', async ({ page, context, servers, watched, browserName }, testInfo) => {
    const origin = servers.page.origin;
    await requireWebGl2(page, origin, browserName);
    await seedPdf(page, origin);
    await openApp(page, origin, { selftest: 'pdf', route: { view: 'page', path: JOURNAL } });
    const report = await waitGlobal(page, 'brulionPdfSelfTest', (v) => !!v.chip && !!v.link, 150_000);
    expect(report.chip.icon).toBe('file-text');
    expect(report.chip.text).toContain('wykład.pdf');
    const offset = await screenOffset(page);

    // The chip: a new tab with the PDF itself (a blob: URL of this site, application/pdf).
    let before = watched.downloads.length;
    const tab = await clickOpensTab(page, context, report.chip, offset);
    const first = await openedDocument(tab, watched, before);
    testInfo.annotations.push({ type: `${browserName}: chip tab`, description: JSON.stringify(first) });
    await expectPdf(tab, first, origin);
    // The app stays on the note: nothing showed the PDF's bytes.
    let now = await waitGlobal(page, 'brulionPdfSelfTest');
    expect(now.route.path).toBe(JOURNAL);
    if (!tab.isClosed()) await tab.close();

    // The link to it: the same.
    now = await waitGlobal(page, 'brulionPdfSelfTest', (v) => !!v.link);
    before = watched.downloads.length;
    const linkTab = await clickOpensTab(page, context, now.link, offset, 0.1);
    const second = await openedDocument(linkTab, watched, before);
    await expectPdf(linkTab, second, origin);
    if (!linkTab.isClosed()) await linkTab.close();
    now = await waitGlobal(page, 'brulionPdfSelfTest');
    expect(now.route.path).toBe(JOURNAL);
  });

  test('trasa do PDF-a: panel z „Otwórz w nowej karcie”', async ({ page, context, servers, watched, browserName }) => {
    void watched;
    const origin = servers.page.origin;
    await requireWebGl2(page, origin, browserName);
    await seedPdf(page, origin);
    await openApp(page, origin, { selftest: 'pdf', route: { view: 'page', path: PDF_PATH } });
    const report = await waitGlobal(page, 'brulionPdfSelfTest', (v) => v.message !== '' && v.button !== '', 150_000);
    expect(report.route.path).toBe(PDF_PATH);
    expect(report.message).toMatch(/nowej karcie|new tab/);
    expect(report.button).toMatch(/^(Otwórz w nowej karcie|Open in a new tab)$/);
  });

  test('karta otwarta bez kliknięcia: otwarta albo pobrana z komunikatem', async ({ page, servers, watched, browserName }, testInfo) => {
    const origin = servers.page.origin;
    await requireWebGl2(page, origin, browserName);
    await seedPdf(page, origin);
    const popup = page.context().waitForEvent('page', { timeout: 60_000 }).catch(() => null);
    await openApp(page, origin, { selftest: 'pdfauto', route: { view: 'page', path: JOURNAL } });
    const result = await waitGlobal(page, 'brulionPdfOpenSelfTest', (v) => !!v.path, 150_000);
    expect(result.path).toBe(PDF_PATH);
    expect(result.opened).toBe(true);
    expect(result.errors).toEqual([]);
    let outcome;
    if (result.notices.length > 0) {
      // Blocked: downloaded instead, and the app says so.
      expect(result.notices[0]).toMatch(/pobrany|downloaded/);
      await expect.poll(() => watched.downloads.length, { timeout: 20_000 }).toBeGreaterThan(0);
      expect(watched.downloads[0].suggestedFilename()).toBe('wykład.pdf');
      outcome = 'blocked: downloaded';
    } else {
      const tab = await popup;
      expect(tab).not.toBeNull();
      outcome = 'opened';
    }
    testInfo.annotations.push({ type: `${browserName}: a tab outside a click`, description: outcome });
  });
});
