// Password-protected published pages in real browsers (docs/UI.md "Publishing" → "Password-protected
// pages"): a site brulion-cli publishes into a folder, served on 127.0.0.1, opened in Chromium and
// Firefox. Run through `scripts/web-test.sh --publish` (no web build needed; $BRULION_CLI names
// brulion-cli).
import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: '.',
  testMatch: 'publish.e2e.mjs',
  workers: 1,
  fullyParallel: false,
  timeout: 120_000,
  expect: { timeout: 30_000 },
  retries: process.env.CI ? 1 : 0,
  forbidOnly: !!process.env.CI,
  outputDir: 'test-results-publish',
  reporter: [['list'], ['html', { open: 'never', outputFolder: 'playwright-report-publish' }]],
  use: { trace: 'retain-on-failure', screenshot: 'only-on-failure', locale: 'pl-PL' },
  projects: [
    {
      name: 'chromium',
      use: {
        browserName: 'chromium',
        channel: 'chromium',
        // insecure.brulion.test: the site over plain http on a name that is no secure context.
        launchOptions: { args: ['--host-resolver-rules=MAP insecure.brulion.test 127.0.0.1'] },
      },
    },
    {
      name: 'firefox',
      use: {
        browserName: 'firefox',
        headless: true,
        launchOptions: { firefoxUserPrefs: { 'network.dns.forceResolve': '127.0.0.1' } },
      },
    },
  ],
});
