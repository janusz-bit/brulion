/**
 * The app (index.html) boots: #root gets content and nothing is logged via console.error or
 * thrown uncaught. Deliberately generic so it keeps holding while the UI evolves. Also writes
 * e2e/screenshots/<project>-<scheme>-app.png.
 */
import { capture, expect, expectScheme, SCHEMES, settle, test } from './harness';

for (const scheme of SCHEMES) {
  test.describe(`app ${scheme}`, () => {
    test.use({ colorScheme: scheme });

    test('boots: #root renders, no errors', { tag: ['@smoke', '@shots'] }, async ({ page }, testInfo) => {
      const response = await page.goto('./');
      expect(response?.ok(), 'index.html is served').toBe(true);
      await expect(page.locator('#root > *').first()).toBeAttached();
      await expectScheme(page, scheme);
      await settle(page);
      await capture(page, testInfo, scheme, 'app', { tiles: false });
    });
  });
}
