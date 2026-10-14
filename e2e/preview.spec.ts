/**
 * Live preview in the editor playground (editor.html): screenshots of the rich project note
 * and today's journal in light and dark on desktop and mobile, a focused line with revealed
 * syntax, and pointer interactions in a real browser (checkbox, links, bullets, tables, chips).
 *
 * Files: e2e/screenshots/<project>-<scheme>-preview.png (+ tiles), …-preview-journal.png,
 * …-preview-focus.png.
 */
import { mkdirSync, readdirSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import type { Locator, Page, TestInfo } from '@playwright/test';
import { capture, expect, expectScheme, SCHEMES, screenshotDir, settle, test, type Scheme } from './harness';

const PAGE = '[data-testid=page-editor]';

async function open(page: Page, query = ''): Promise<Locator> {
  const response = await page.goto(`editor.html?theme=system${query}`);
  expect(response?.ok(), 'editor.html is served').toBe(true);
  await expect(page, 'editor.html is not in the build').toHaveTitle(/edytor/i);
  const content = page.locator(`${PAGE} .cm-content`);
  await expect(content).toBeVisible();
  await expect(page.locator(`${PAGE} .b-editor`)).toHaveAttribute('data-status', 'ready');
  await settle(page);
  return content;
}

function line(page: Page, text: string): Locator {
  return page.locator(`${PAGE} .cm-line`, { hasText: text }).first();
}

/** The document of the page editor (through the playground's debug hook). */
async function doc(page: Page): Promise<string> {
  return page.evaluate(() => window.brulionPlayground?.view('[data-testid=page-editor]')?.state.doc.toString() ?? '');
}

/** Click the page editor at the document position `offset` characters into the first `text`. */
async function clickText(page: Page, text: string, offset = 1): Promise<void> {
  const point = await page.evaluate(
    ([t, o]) => {
      const view = window.brulionPlayground?.view('[data-testid=page-editor]');
      if (!view) return null;
      const at = view.state.doc.toString().indexOf(t);
      if (at < 0) return null;
      const c = view.coordsAtPos(at + o, 1);
      return c ? { x: c.left + 1, y: (c.top + c.bottom) / 2 } : null;
    },
    [text, offset] as const,
  );
  expect(point, `"${text}" is on screen`).not.toBeNull();
  await page.mouse.click(point!.x, point!.y);
}

/**
 * The page editor scrolls inside its pane: save it in viewport-sized parts
 * `<project>-<scheme>-<name>.part-NN.png` (scrolling its `.cm-scroller`).
 */
async function captureParts(page: Page, testInfo: TestInfo, scheme: Scheme, name: string, max = 12): Promise<string[]> {
  const dir = screenshotDir(testInfo);
  mkdirSync(dir, { recursive: true });
  const prefix = `${testInfo.project.name}-${scheme}-${name}.part-`;
  for (const f of readdirSync(dir)) if (f.startsWith(prefix)) rmSync(join(dir, f));
  const pane = page.locator(PAGE);
  const scroller = page.locator(`${PAGE} .cm-scroller`);
  const files: string[] = [];
  for (let i = 0; i < max; i++) {
    const done = await scroller.evaluate((el, step) => {
      el.scrollTop = step * Math.max(1, el.clientHeight - 48);
      return el.scrollTop + el.clientHeight >= el.scrollHeight - 2;
    }, i);
    await settle(page);
    const file = join(dir, `${prefix}${String(i + 1).padStart(2, '0')}.png`);
    await pane.screenshot({ path: file });
    files.push(file);
    if (done) break;
  }
  return files;
}

for (const scheme of SCHEMES) {
  test.describe(`live preview ${scheme}`, () => {
    test.use({ colorScheme: scheme });

    test('project note is captured', { tag: ['@shots', '@preview'] }, async ({ page }, testInfo) => {
      await open(page);
      await expectScheme(page, scheme);
      await expect(page.locator(`${PAGE} .b-lp-props`)).toBeVisible();
      await settle(page);
      const files = await captureParts(page, testInfo, scheme, 'preview');
      expect(files.length).toBeGreaterThan(1);
      await expect(page.locator(`${PAGE} .b-lp-embed--image img`)).toBeVisible();
    });

    test('journal is captured', { tag: ['@shots', '@preview'] }, async ({ page }, testInfo) => {
      await open(page, '&note=journals/2026-10-14.md');
      await expect(page.locator(`${PAGE} .b-lp-bullet`).first()).toBeVisible();
      await capture(page, testInfo, scheme, 'preview-journal', { tiles: false });
    });

    test('focused line reveals its syntax', { tag: ['@shots', '@preview'] }, async ({ page }, testInfo) => {
      await open(page, '&note=journals/2026-10-14.md');
      // Cursor inside the reminder token: it shows as source, the due chip next to it stays.
      await clickText(page, '⏰ 2026-10-14 16:00', 5);
      await expect(line(page, 'Zadzwonić do hydraulika').locator('.b-lp-chip')).toHaveCount(1);
      await expect(line(page, 'Zadzwonić do hydraulika').locator('.b-lp-chip--due')).toHaveCount(1);
      await page.locator(`${PAGE} .b-lp-bullet`).nth(5).hover();
      await capture(page, testInfo, scheme, 'preview-focus', { tiles: false });
    });
  });
}

test.describe('live preview interactions', () => {
  test('checkbox toggles a task', { tag: '@preview' }, async ({ page }) => {
    await open(page);
    await line(page, 'Elektryk').locator('.b-lp-checkbox').click();
    await expect.poll(() => doc(page)).toContain('- [x] Elektryk: dodatkowe gniazdka nad blatem 📅 2026-10-20 ✅ 2026-10-14');
    await expect(line(page, 'Elektryk')).toHaveClass(/is-done/);
  });

  test('a rendered wiki link opens its page, a click beside it edits', { tag: '@preview' }, async ({ page }) => {
    await open(page, '&note=journals/2026-10-14.md');
    // Plain text: the cursor goes there, the link next to it stays rendered.
    await clickText(page, 'Poranna kawa', 3);
    await expect(page.locator('.pg__log')).toHaveText('');
    await expect(line(page, 'Poranna kawa').locator('.b-lp-wikilink')).toHaveCount(1);
    // The rendered link opens its page.
    await line(page, 'Poranna kawa').locator('.b-lp-wikilink').click();
    await expect(page.locator('.pg__log')).toHaveText('→ Projekty/Remont kuchni.md');
    await expect(page.locator(`${PAGE} .b-lp-props`)).toBeVisible();
  });

  test('a bullet zooms into its block', { tag: '@preview' }, async ({ page }) => {
    await open(page);
    await line(page, 'Wykończenie').locator('.b-lp-bullet').click();
    await expect(page.locator(`${PAGE} .cm-line`, { hasText: 'Demontaż' })).toHaveCount(0);
    await expect(line(page, 'Zamówić blat')).toBeVisible();
  });

  test('a table cell puts the cursor into the source', { tag: '@preview' }, async ({ page }) => {
    await open(page);
    await page.locator(`${PAGE} .b-lp-table`).scrollIntoViewIfNeeded();
    await page.locator(`${PAGE} .b-lp-table td[data-r="2"][data-c="2"]`).click();
    await expect(page.locator(`${PAGE} .b-lp-table`)).toHaveCount(0);
    await expect(line(page, '| Blat dębowy')).toBeVisible();
  });

  test('a due chip opens the date picker', { tag: '@preview' }, async ({ page }) => {
    await open(page);
    await line(page, 'Elektryk').locator('.b-lp-chip--due').click();
    await expect(page.locator('[role=dialog], .b-popover').first()).toBeVisible();
  });
});
