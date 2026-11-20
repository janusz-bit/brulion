// The Web Clipper in a real Chromium (docs/CLIPPER.md "Tests"): the unpacked extension (clipper/)
// with brulion-cli as its native messaging host, on a local page. Run through
// `scripts/web-test.sh --clipper` (no web build needed; $BRULION_CLI names brulion-cli).
import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: '.',
  testMatch: 'clipper.e2e.mjs',
  workers: 1,
  fullyParallel: false,
  timeout: 180_000,
  expect: { timeout: 30_000 },
  retries: process.env.CI ? 1 : 0,
  forbidOnly: !!process.env.CI,
  outputDir: 'test-results-clipper',
  reporter: [['list'], ['html', { open: 'never', outputFolder: 'playwright-report-clipper' }]],
  use: { trace: 'retain-on-failure', screenshot: 'only-on-failure' },
});
