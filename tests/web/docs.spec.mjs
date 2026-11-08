// The documentation notebook in the web build (docs/WEB.md "Data in the browser", docs/UI.md "The
// documentation notebook"), in Chromium and Firefox: Ctrl+Shift+B in the user's notebook opens
// "Dokumentacja Brulionu" (the tests' browser speaks Polish; "Brulion Documentation" in English),
// whose pages the app writes into its data folder — a mount kept in IndexedDB — exactly as
// shipped (src/app/shell/guide/<language>/). After a reload the app starts in the user's notebook
// again (the documentation is never the start notebook), the pages are still there and the key
// opens them once more; the user's note is never changed.
import { readFileSync, readdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { expect, test } from './lib/fixtures.mjs';
import { openApp, requireWebGl2, screenOffset } from './lib/brulion.mjs';
import { VAULT, listAppData, readAppData, readFile, seed } from './lib/idbfs.mjs';

const guide = resolve(fileURLToPath(new URL('.', import.meta.url)), '../../src/app/shell/guide');
const FOLDERS = { pl: 'Dokumentacja Brulionu', en: 'Brulion Documentation' };
const CONTENTS = { pl: 'Spis treści.md', en: 'Contents.md' };

const NOTE = 'Zakupy na weekend.md';
const NOTE_TEXT = '- Kupić żółty ser i ćwierć kilo śliwek\n';

/** The shipped guide pages of `language` (file names). */
function shippedPages(language) {
  return readdirSync(resolve(guide, language)).filter((f) => f.endsWith('.md')).sort();
}

/** The `current=` notebook of the app's settings.ini as written back to IndexedDB. */
async function currentVault(page) {
  const ini = await readAppData(page, 'settings.ini');
  const line = (ini ? ini.toString('utf8') : '').split('\n').find((l) => l.startsWith('current='));
  return line ? line.slice('current='.length).trim() : null;
}

/** The language whose documentation folder holds every shipped page, once one does. */
async function docsWritten(page) {
  let found = null;
  await expect.poll(async () => {
    for (const language of Object.keys(FOLDERS)) {
      const written = (await listAppData(page, FOLDERS[language])).filter((p) => p.endsWith('.md') && !p.includes('/'));
      const missing = shippedPages(language).filter((p) => !written.includes(p));
      if (written.length && !missing.length) {
        found = language;
        return language;
      }
    }
    return null;
  }, { timeout: 60_000, intervals: [250] }).not.toBeNull();
  return found;
}

/** Ctrl+Shift+B with the keyboard focus in the app (a click into the page first). */
async function toggleDocs(page) {
  const off = await screenOffset(page);
  await page.mouse.click(off.x + 900, off.y + 600);
  await page.keyboard.press('Control+Shift+KeyB');
}

test.describe('Dokumentacja w przeglądarce', () => {
  test('Ctrl+Shift+B otwiera notatnik dokumentacji, który zostaje po przeładowaniu', async ({ page, servers, watched, browserName }) => {
    void watched;
    const origin = servers.page.origin;
    await requireWebGl2(page, origin, browserName);
    await seed(page, origin, { files: { [NOTE]: NOTE_TEXT } });
    await openApp(page, origin, { route: { view: 'page', path: NOTE } });
    await expect.poll(() => currentVault(page), { timeout: 30_000 }).toBe(VAULT);

    await toggleDocs(page);
    const language = await docsWritten(page);
    const folder = FOLDERS[language];
    // Each page exactly as shipped, the table of contents too.
    for (const name of shippedPages(language)) {
      const stored = await readAppData(page, `${folder}/${name}`);
      expect(stored, name).not.toBeNull();
      expect(stored.equals(readFileSync(resolve(guide, language, name))), name).toBe(true);
    }
    expect(shippedPages(language)).toContain(CONTENTS[language]);
    await expect.poll(() => currentVault(page), { timeout: 30_000 }).toMatch(new RegExp(`/${folder}$`));

    // A new start: the user's notebook, the documentation kept.
    await openApp(page, origin, {});
    await expect.poll(() => currentVault(page), { timeout: 30_000 }).toBe(VAULT);
    expect(await docsWritten(page)).toBe(language);
    await toggleDocs(page);
    await expect.poll(() => currentVault(page), { timeout: 30_000 }).toMatch(new RegExp(`/${folder}$`));

    expect((await readFile(page, NOTE)).toString('utf8')).toBe(NOTE_TEXT);
  });
});
