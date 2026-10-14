/**
 * Playwright e2e / visual-check suite (see e2e/README.md).
 *
 * Browser: Chromium from nixpkgs via CHROMIUM_PATH (scripts/e2e.sh resolves and exports it):
 * Playwright's own browser downloads don't run on NixOS. Without CHROMIUM_PATH a `chromium` on
 * PATH is used, otherwise Playwright's bundled browser (`npx playwright install chromium` on CI).
 *
 * Server: a production build (`npm run build`, both index.html and gallery.html) written to
 * e2e/.dist — separate from dist/ so concurrent builds don't pull files from under the
 * preview server — served by `vite preview`.
 *
 * Env knobs:
 *   CHROMIUM_PATH    Chromium executable.
 *   E2E_BASE_URL     Test an already running server instead (e.g. `npx vite` →
 *                    http://localhost:1420/); no build, no webServer.
 *   E2E_SKIP_BUILD=1 Serve the existing e2e/.dist without rebuilding.
 *   E2E_PORT         Preview port (default 4173, strict: fails if taken).
 *   E2E_DPR          Device pixel ratio of the mobile project (default 1; Pixel 7 is 2.625).
 */
import { existsSync } from 'node:fs';
import { delimiter, join } from 'node:path';
import { defineConfig, devices } from '@playwright/test';

const port = Number(process.env.E2E_PORT ?? 4173);
const base = process.env.BRULION_BASE ?? '/';
const externalBaseURL = process.env.E2E_BASE_URL;
const baseURL = externalBaseURL ?? `http://127.0.0.1:${port}${base}`;
const outDir = 'e2e/.dist';

function findChromium(): string | undefined {
  if (process.env.CHROMIUM_PATH) return process.env.CHROMIUM_PATH;
  for (const dir of (process.env.PATH ?? '').split(delimiter)) {
    for (const name of ['chromium', 'chromium-browser']) {
      const candidate = join(dir, name);
      if (dir && existsSync(candidate)) return candidate;
    }
  }
  return undefined;
}

const preview = `npx vite preview --outDir ${outDir} --host 127.0.0.1 --port ${port} --strictPort`;
const build = `npm run build -- --outDir ${outDir} --emptyOutDir`;

export default defineConfig({
  testDir: 'e2e',
  testMatch: '**/*.spec.ts',
  outputDir: 'test-results',
  // Raspberry Pi 4: one browser at a time.
  workers: 1,
  fullyParallel: false,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 1 : 0,
  // Full-page capture + cropping of the long gallery takes ~10–30 s per test on the Pi.
  timeout: 120_000,
  expect: { timeout: 10_000 },
  reporter: process.env.CI ? [['list'], ['html', { open: 'never' }]] : [['list']],
  use: {
    baseURL,
    browserName: 'chromium',
    headless: true,
    locale: 'pl-PL',
    timezoneId: 'Europe/Warsaw',
    reducedMotion: 'reduce',
    // The PWA service worker would cache between navigations and hide fresh builds.
    serviceWorkers: 'block',
    screenshot: 'only-on-failure',
    trace: process.env.CI ? 'retain-on-failure' : 'off',
    launchOptions: {
      executablePath: findChromium(),
      // Playwright already passes --no-sandbox (chromiumSandbox defaults to false).
      args: ['--disable-dev-shm-usage'],
    },
  },
  projects: [
    {
      name: 'desktop',
      use: { viewport: { width: 1280, height: 800 }, deviceScaleFactor: 1 },
    },
    {
      name: 'mobile',
      use: {
        ...devices['Pixel 7'],
        viewport: { width: 412, height: 915 },
        screen: { width: 412, height: 915 },
        deviceScaleFactor: Number(process.env.E2E_DPR ?? 1),
        isMobile: true,
        hasTouch: true,
      },
    },
  ],
  webServer: externalBaseURL
    ? undefined
    : {
        command: process.env.E2E_SKIP_BUILD ? preview : `${build} && ${preview}`,
        url: baseURL,
        // Production build on the Pi can take a few minutes once the app grows.
        timeout: 300_000,
        // Never silently test whatever else is listening on the port.
        reuseExistingServer: false,
        stdout: 'ignore',
        stderr: 'pipe',
        gracefulShutdown: { signal: 'SIGTERM', timeout: 5_000 },
      },
});
