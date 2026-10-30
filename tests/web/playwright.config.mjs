// Browser tests of the web build (docs/WEB.md "Testy w przeglądarkach"): the site from
// $BRULION_DIST (default ../../dist-wasm, scripts/wasm-build.sh) on local https and http servers
// (lib/servers.mjs), in Chromium and Firefox. Run through scripts/web-test.sh (Firefox draws
// WebGL 2 only with an X display: the script provides one with xvfb-run).
import { defineConfig } from '@playwright/test';

// The test host names, resolved to the local servers inside the browsers (no /etc/hosts).
const chromiumHosts = '--host-resolver-rules=MAP brulion.test 127.0.0.1, MAP *.brulion.test 127.0.0.1';

export default defineConfig({
  testDir: '.',
  testMatch: '*.spec.mjs',
  workers: 1,
  fullyParallel: false,
  timeout: 300_000,
  expect: { timeout: 30_000 },
  retries: process.env.CI ? 1 : 0,
  forbidOnly: !!process.env.CI,
  outputDir: 'test-results',
  reporter: [['list'], ['html', { open: 'never', outputFolder: 'playwright-report' }]],
  use: {
    ignoreHTTPSErrors: true,
    locale: 'pl-PL',
    timezoneId: 'Europe/Warsaw',
    colorScheme: 'light',
    viewport: { width: 1280, height: 800 },
    deviceScaleFactor: 1,
    acceptDownloads: true,
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
  },
  projects: [
    {
      name: 'chromium',
      use: {
        browserName: 'chromium',
        channel: 'chromium', // the new headless mode of the full browser
        launchOptions: {
          args: [chromiumHosts, '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
        },
      },
    },
    {
      name: 'firefox',
      use: {
        browserName: 'firefox',
        // WebGL 2 in Firefox needs an X display (Xvfb): headed when there is one.
        headless: !process.env.DISPLAY,
        launchOptions: {
          firefoxUserPrefs: {
            'network.dns.forceResolve': '127.0.0.1',
            'webgl.force-enabled': true,
          },
        },
      },
    },
  ],
});
