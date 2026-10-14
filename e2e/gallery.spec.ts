/**
 * Component gallery (gallery.html → src/app/dev/gallery-entry.tsx) in light and dark, on the
 * desktop and mobile projects. Writes e2e/screenshots/<project>-<scheme>-gallery.png (+ tiles
 * and per-section images, see capture()) for agents/humans to look at; fails on console errors.
 */
import { capture, expect, expectScheme, RENDERED_CHILD, SCHEMES, settle, test } from './harness';

for (const scheme of SCHEMES) {
  test.describe(`gallery ${scheme}`, () => {
    test.use({ colorScheme: scheme });

    test('renders without errors and is captured', { tag: ['@shots', '@gallery'] }, async ({ page }, testInfo) => {
      // theme=system: the gallery follows the emulated prefers-color-scheme like the app's default
      // setting does (its own default, "both", renders light and dark side by side).
      const response = await page.goto('gallery.html?theme=system');
      expect(response?.ok(), 'gallery.html is served').toBe(true);
      // `vite preview` falls back to index.html for unknown paths — make sure this is the gallery.
      // vite.config.ts builds gallery.html only when src/app/dev/gallery-entry.tsx exists.
      await expect(page, 'gallery.html is not in the build: is there src/app/dev/gallery-entry.tsx?').toHaveTitle(
        /galeria/i,
      );
      await expect(page.locator(RENDERED_CHILD).first()).toBeVisible();
      await expectScheme(page, scheme);
      await settle(page);
      const files = await capture(page, testInfo, scheme, 'gallery', { sections: '[data-section]' });
      expect(files.length).toBeGreaterThan(0);
    });
  });
}
