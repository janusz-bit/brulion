/**
 * The app end to end in a fresh browser (empty IndexedDB): welcome screen → "Przykładowy
 * brulion" (demo vault in IndexedDB) → journals → a page via the quick switcher (Ctrl+O) →
 * command palette (Ctrl+K) → settings → back navigation (browser back + the top bar button).
 * No console errors (harness). Screenshots: e2e/screenshots/<project>-<scheme>-app-*.png.
 */
import type { Page, TestInfo } from '@playwright/test';
import { capture, expect, expectScheme, SCHEMES, settle, test, type Scheme } from './harness';

const MAIN = '#b-main';

async function shot(page: Page, testInfo: TestInfo, scheme: Scheme, name: string): Promise<void> {
  await settle(page);
  await capture(page, testInfo, scheme, name, { tiles: false });
}

async function openDemo(page: Page): Promise<void> {
  await page.getByRole('button', { name: 'Otwórz przykładowy brulion' }).click();
  // Today's journal of the demo vault.
  await expect(page.locator(MAIN)).toContainText('Poranna kawa i plan dnia', { timeout: 30_000 });
}

for (const scheme of SCHEMES) {
  test.describe(`app flow ${scheme}`, () => {
    test.use({ colorScheme: scheme });

    test('onboarding → demo vault → switcher → palette → settings → back', { tag: ['@shots', '@app'] }, async ({ page }, testInfo) => {
      test.setTimeout(240_000);
      const mobile = testInfo.project.name === 'mobile';

      const response = await page.goto('./');
      expect(response?.ok(), 'index.html is served').toBe(true);
      await expectScheme(page, scheme);

      // 1. Welcome screen in a fresh browser.
      await expect(page.getByRole('heading', { name: 'Witaj w Brulionie', level: 1 })).toBeVisible({ timeout: 30_000 });
      await expect(page.getByRole('button', { name: 'Zacznij pisać' })).toBeVisible();
      await expect(page.locator('html')).toHaveAttribute('data-theme', scheme);
      await shot(page, testInfo, scheme, 'app-onboarding');

      // 2. The demo vault opens on the journals.
      await openDemo(page);
      await expect(page.locator('.b-topbar__name')).toHaveText('Dziennik');
      if (mobile) await expect(page.getByRole('navigation', { name: 'Główna nawigacja' })).toBeVisible();
      else await expect(page.getByRole('navigation', { name: 'Panel boczny' })).toBeVisible();
      await shot(page, testInfo, scheme, 'app-journals');

      if (mobile) {
        await page.getByRole('button', { name: 'Menu' }).click();
        const drawer = page.getByRole('dialog', { name: 'Panel boczny' });
        await expect(drawer.getByRole('button', { name: 'Wszystkie strony' })).toBeVisible();
        await shot(page, testInfo, scheme, 'app-drawer');
        await page.keyboard.press('Escape');
        await expect(drawer).toBeHidden();
      }

      // 3. Quick switcher (Ctrl+O): find a page by a diacritic-free prefix and open it.
      await page.keyboard.press('Control+o');
      const switcher = page.getByRole('combobox', { name: 'Otwórz stronę' });
      await expect(switcher).toBeFocused();
      await switcher.fill('ksiazki');
      await expect(page.getByRole('option', { name: /Książki 2026/ })).toBeVisible();
      await switcher.fill('remont');
      await expect(page.getByRole('option', { name: /Remont mieszkania/ }).first()).toBeVisible();
      await shot(page, testInfo, scheme, 'app-switcher');
      await page.keyboard.press('Enter');
      await expect(page.locator('.b-topbar__name')).toHaveText('Remont mieszkania');
      await expect(page.locator(MAIN)).toContainText('Budżet', { timeout: 20_000 });
      await shot(page, testInfo, scheme, 'app-page');

      // 4. Command palette (Ctrl+K) → settings.
      await page.keyboard.press('Control+k');
      const palette = page.getByRole('combobox', { name: 'Paleta poleceń' });
      await expect(palette).toBeFocused();
      await palette.fill('ustaw');
      await expect(page.getByRole('option', { name: /Ustawienia/ }).first()).toBeVisible();
      await shot(page, testInfo, scheme, 'app-palette');
      await page.keyboard.press('Enter');
      await expect(page.getByRole('heading', { name: 'Ustawienia', level: 1 })).toBeVisible();
      await expect(page.getByRole('heading', { name: 'Przypomnienia' })).toBeVisible();
      await shot(page, testInfo, scheme, 'app-settings');

      // 5. Back: browser back (= Android back gesture), then the top bar's back button.
      await page.goBack();
      await expect(page.locator('.b-topbar__name')).toHaveText('Remont mieszkania');
      await page.getByRole('button', { name: 'Wstecz' }).click();
      await expect(page.locator('.b-topbar__name')).toHaveText('Dziennik');
      await expect(page.locator(MAIN)).toContainText('Poranna kawa i plan dnia');

      // 6. Quick capture lands in today's journal.
      await page.getByRole('button', { name: 'Szybka notatka' }).first().click();
      const capture = page.getByRole('textbox', { name: 'Co zapisać?' });
      await expect(capture).toBeFocused();
      await capture.fill('Kupić mleko jutro o 18');
      await expect(page.getByText(/Przypomnienie: jutro/)).toBeVisible();
      await shot(page, testInfo, scheme, 'app-capture');
      await page.keyboard.press('Enter');
      await expect(page.getByText('Dodano do dzisiejszego dziennika.')).toBeVisible();
      await expect(page.locator(MAIN)).toContainText('Kupić mleko');

      // 7. A reload restores the vault (device settings) without the welcome screen.
      await page.reload();
      await expect(page.locator(MAIN)).toContainText('Poranna kawa i plan dnia', { timeout: 30_000 });
    });
  });
}
