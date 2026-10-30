// The fixtures of the browser tests: `servers` (lib/servers.mjs, once per worker, with a new
// certificate) and `watched` (the page's console, uncaught errors and downloads; the console is
// attached to every test, and an uncaught page error fails it).
import { test as base, expect } from '@playwright/test';
import { existsSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { attachLog, watchPage } from './brulion.mjs';
import { makeCertificate } from './cert.mjs';
import { startServers } from './servers.mjs';

const here = fileURLToPath(new URL('.', import.meta.url));

export const test = base.extend({
  servers: [async ({}, use) => { // eslint-disable-line no-empty-pattern
    const dist = resolve(process.env.BRULION_DIST || resolve(here, '../../../dist-wasm'));
    if (!existsSync(resolve(dist, 'index.html'))) {
      throw new Error(`no web build in ${dist} (scripts/wasm-build.sh; BRULION_DIST or scripts/web-test.sh --dist)`);
    }
    const { key, cert } = makeCertificate();
    const servers = await startServers({ dist, key, cert });
    await use(servers);
    await servers.stop();
  }, { scope: 'worker' }],

  watched: async ({ page }, use, testInfo) => {
    const watched = watchPage(page);
    await use(watched);
    await attachLog(testInfo, watched);
    expect(watched.errors.map((e) => e.message), 'uncaught errors in the page').toEqual([]);
  },
});

export { expect };
