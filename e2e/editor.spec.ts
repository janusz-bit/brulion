/**
 * Editor playground (editor.html → src/app/dev/editor-playground.tsx): screenshots in light and
 * dark on desktop and mobile, plus editing interactions (completion, outliner keys, task toggle,
 * slash menu). Every test fails on console errors (harness).
 *
 * Files: e2e/screenshots/<project>-<scheme>-editor.png (+ tiles), …-editor-menus.png (slash and
 * [[ completion open), mobile only: …-editor-toolbar.png (focused editor with the toolbar).
 */
import { join } from 'node:path';
import type { Locator, Page } from '@playwright/test';
import { capture, expect, expectScheme, SCHEMES, screenshotDir, settle, test } from './harness';

const PAGE = '[data-testid=page-editor]';

async function open(page: Page, query = ''): Promise<Locator> {
  const response = await page.goto(`editor.html?theme=system${query}`);
  expect(response?.ok(), 'editor.html is served').toBe(true);
  // vite preview falls back to index.html for unknown paths.
  await expect(page, 'editor.html is not in the build: is there src/app/dev/editor-playground.tsx?').toHaveTitle(/edytor/i);
  const content = page.locator(`${PAGE} .cm-content`);
  await expect(content).toBeVisible();
  await expect(page.locator(`${PAGE} .b-editor`)).toHaveAttribute('data-status', 'ready');
  // CodeMirror measures line heights on the next frames; clicks before that can miss lines.
  await settle(page);
  return content;
}

/** Text of the editor line containing `text` (raw Markdown, tabs included). */
async function lineText(page: Page, text: string): Promise<string> {
  return (await page.locator(`${PAGE} .cm-line`, { hasText: text }).first().textContent()) ?? '';
}

/** Put the cursor at the end of the line containing `text`. */
async function caretAtEnd(page: Page, text: string): Promise<void> {
  await page.locator(`${PAGE} .cm-line`, { hasText: text }).first().click();
  await page.keyboard.press('End');
  // With an Android user agent CodeMirror delays Enter/Backspace key events and drops them
  // when the editor DOM changed within the last 50 ms (it then expects the IME's own DOM
  // change). Let the click's redraw settle so a following Enter is never lost.
  await page.waitForTimeout(120);
}

for (const scheme of SCHEMES) {
  test.describe(`editor ${scheme}`, () => {
    test.use({ colorScheme: scheme });

    test('playground renders and is captured', { tag: ['@shots', '@editor'] }, async ({ page }, testInfo) => {
      await open(page);
      await expectScheme(page, scheme);
      await settle(page);
      const files = await capture(page, testInfo, scheme, 'editor', { tiles: true });
      expect(files.length).toBeGreaterThan(0);
    });

    test('completion menus are captured', { tag: ['@shots', '@editor'] }, async ({ page }, testInfo) => {
      await open(page, '&note=journals/2026-10-14.md');
      await caretAtEnd(page, 'notatki głosowe');
      await page.keyboard.press('Enter');
      await page.keyboard.type('/prz');
      await expect(page.locator('.cm-tooltip-autocomplete')).toBeVisible();
      await settle(page);
      const dir = screenshotDir(testInfo);
      await page.screenshot({ path: join(dir, `${testInfo.project.name}-${scheme}-editor-slash.png`) });
      await page.keyboard.press('Escape');
      await caretAtEnd(page, 'Przeczytać rozdział');
      await page.keyboard.press('Enter');
      await page.keyboard.type('[[re');
      await expect(page.locator('.cm-tooltip-autocomplete li').first()).toBeVisible();
      await settle(page);
      await page.screenshot({ path: join(dir, `${testInfo.project.name}-${scheme}-editor-link.png`) });
    });

    test('mobile toolbar above the keyboard', { tag: ['@shots', '@editor'] }, async ({ page }, testInfo) => {
      test.skip(testInfo.project.name !== 'mobile', 'touch toolbar is for coarse pointers');
      await open(page, '&note=journals/2026-10-14.md');
      await caretAtEnd(page, 'Podlać kwiaty');
      const toolbar = page.getByRole('toolbar', { name: 'Narzędzia edytora' });
      await expect(toolbar).toBeVisible();
      await expect(toolbar.getByRole('button', { name: 'Zwiększ wcięcie' })).toBeVisible();
      await settle(page);
      await page.screenshot({ path: join(screenshotDir(testInfo), `${testInfo.project.name}-${scheme}-editor-toolbar.png`) });
      // Toolbar buttons act on the editor without stealing focus.
      await toolbar.getByRole('button', { name: 'Zwiększ wcięcie' }).click();
      await expect.poll(() => lineText(page, 'Podlać kwiaty')).toMatch(/^\t- \[ \] Podlać kwiaty/);
      await expect(toolbar).toBeVisible();
    });
  });
}

test.describe('editor interactions', () => {
  test('[[ completion inserts a page link', { tag: '@editor' }, async ({ page }) => {
    await open(page, '&note=Zakupy.md');
    await caretAtEnd(page, 'kawa ziarnista');
    await page.keyboard.press('Enter');
    await page.keyboard.type('[[Pro');
    const option = page.locator('.cm-tooltip-autocomplete li', { hasText: 'Projekt ogrodu' });
    await expect(option).toBeVisible();
    await option.click();
    await expect.poll(() => lineText(page, 'Projekt ogrodu')).toBe('- [ ] [[Projekt ogrodu]]');
  });

  test('Tab / Shift-Tab indent and outdent list items', { tag: '@editor' }, async ({ page }) => {
    await open(page, '&note=Zakupy.md');
    await caretAtEnd(page, 'chleb żytni');
    await page.keyboard.press('Tab');
    await expect.poll(() => lineText(page, 'chleb żytni')).toBe('\t- [ ] chleb żytni');
    await page.keyboard.press('Shift+Tab');
    await expect.poll(() => lineText(page, 'chleb żytni')).toBe('- [ ] chleb żytni');
  });

  test('Mod-Enter (toolbar on phones) toggles a task', { tag: '@editor' }, async ({ page }, testInfo) => {
    await open(page, '&note=Zakupy.md');
    await caretAtEnd(page, 'mleko owsiane');
    // CodeMirror handles Enter on Android through its delayed-key path, which drops modifiers:
    // phones toggle tasks with the toolbar button instead.
    const toggle =
      testInfo.project.name === 'mobile'
        ? () => page.getByRole('toolbar', { name: 'Narzędzia edytora' }).getByRole('button', { name: 'Przełącz zadanie' }).click()
        : () => page.keyboard.press('ControlOrMeta+Enter');
    await toggle();
    await expect.poll(() => lineText(page, 'mleko owsiane')).toBe('- [x] mleko owsiane ✅ 2026-10-14');
    await toggle();
    await expect.poll(() => lineText(page, 'mleko owsiane')).toBe('- [ ] mleko owsiane');
  });

  test('Enter continues the list', { tag: '@editor' }, async ({ page }) => {
    await open(page, '&note=Zakupy.md');
    await caretAtEnd(page, 'kawa ziarnista');
    await page.keyboard.press('Enter');
    await page.keyboard.type('herbata');
    await expect.poll(() => lineText(page, 'herbata')).toBe('- [ ] herbata');
  });

  test('slash menu opens and filters', { tag: '@editor' }, async ({ page }) => {
    await open(page, '&note=Zakupy.md');
    await caretAtEnd(page, 'kawa ziarnista');
    await page.keyboard.press('Enter');
    await page.keyboard.press('Backspace');
    await page.keyboard.type('/');
    const items = page.locator('.cm-tooltip-autocomplete li');
    await expect(items.first()).toBeVisible();
    expect(await items.count()).toBeGreaterThan(10);
    await page.keyboard.type('prz');
    await expect(items.first()).toContainText('Przypomnienie');
    await page.keyboard.press('Enter');
    // The reminder dialog opens; cancel returns to the editor.
    const dialog = page.getByRole('dialog');
    await expect(dialog).toBeVisible();
    await page.keyboard.press('Escape');
    await expect(dialog).toBeHidden();
  });

  test('outliner zoom shows one block with breadcrumbs', { tag: '@editor' }, async ({ page }) => {
    await open(page, '&zoom=20');
    const crumbs = page.getByRole('navigation', { name: 'Powiększony blok' });
    await expect(crumbs).toBeVisible();
    await expect(crumbs).toContainText('Wykończenie');
    await expect(page.locator(`${PAGE} .cm-line`, { hasText: 'Demontaż' })).toHaveCount(0);
    await crumbs.getByRole('button').first().click();
    await expect(page.locator(`${PAGE} .cm-line`, { hasText: 'Demontaż' })).toHaveCount(1);
  });
});
