// Password-protected published pages in Chromium and Firefox (docs/UI.md "Publishing" →
// "Password-protected pages", core publish/protect.h): brulion-cli publishes a small Polish
// notebook into a folder with `--protect on` (the pages' password from standard input, a home of
// its own), a static server on 127.0.0.1 serves it. The lock page asks for the password — a wrong
// one shows a message and nothing of the notes; the right one opens the index with the browser's
// WebCrypto; its link opens another page without asking again (the key kept in sessionStorage,
// never the password), that page's picture is decrypted into a blob: URL and drawn; a new browser
// context asks again; "Remember" off asks on every page; over plain http on a name that is no
// secure context (Chromium) the page says it needs https.
//
// Run: scripts/web-test.sh --publish (BRULION_CLI: the brulion-cli to use).
import { test, expect } from '@playwright/test';
import { execFileSync } from 'node:child_process';
import { mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from 'node:fs';
import http from 'node:http';
import { tmpdir } from 'node:os';
import { dirname, extname, join, normalize, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
import { PICTURE, picturePng } from './lib/servers.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../..');
const cli = process.env.BRULION_CLI || join(root, 'build-clang/src/cli/brulion-cli');
const PASSWORD = 'Źdźbło trawy na łące';
const TEXT_ONE = 'Zażółć gęślą jaźń — pierwsza notatka';
const TEXT_TWO = 'Drugi tekst o łąkach i źródłach';

let work;
let www;
let server;
let port;

function serve(dir) {
  const types = { '.html': 'text/html; charset=utf-8', '.css': 'text/css' };
  return http.createServer((req, res) => {
    const path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    let file = normalize(join(dir, path));
    if (!file.startsWith(dir + sep) && file !== dir) {
      res.writeHead(403);
      res.end();
      return;
    }
    try {
      if (statSync(file).isDirectory()) file = join(file, 'index.html');
      const data = readFileSync(file);
      res.writeHead(200, { 'Content-Type': types[extname(file)] || 'application/octet-stream', Connection: 'close' });
      res.end(data);
    } catch {
      res.writeHead(404, { Connection: 'close' });
      res.end('brak');
    }
  });
}

test.beforeAll(async () => {
  work = mkdtempSync(join(tmpdir(), 'brulion-publish-'));
  const home = join(work, 'home');
  mkdirSync(home, { recursive: true });
  const vault = join(work, 'Notatnik');
  www = join(work, 'www');
  const env = {
    ...process.env,
    HOME: home,
    XDG_CONFIG_HOME: join(home, 'config'),
    XDG_DATA_HOME: join(home, 'data'),
    XDG_STATE_HOME: join(home, 'state'),
    XDG_CACHE_HOME: join(home, 'cache'),
    LANGUAGE: 'en',
  };
  const run = (args, input) => execFileSync(cli, ['--vault', vault, ...args], { env, input, encoding: 'utf8' });
  execFileSync(cli, ['init', vault], { env });
  mkdirSync(join(vault, 'Blog'), { recursive: true });
  mkdirSync(join(vault, 'assets'), { recursive: true });
  writeFileSync(join(vault, 'Blog/Pierwszy wpis.md'), `# Pierwszy wpis\n\n${TEXT_ONE}, zobacz [[Drugi wpis]].\n`);
  writeFileSync(join(vault, 'Blog/Drugi wpis.md'), `${TEXT_TWO}.\n\n![[łąka.png]]\n`);
  writeFileSync(join(vault, 'assets/łąka.png'), picturePng());
  run(['publish', 'add', 'folder', 'Rodzina', '--path', www, '--protect', 'on', '--title', 'Sekretny dziennik']);
  run(['publish', 'include', 'Rodzina', '--folder', 'Blog']);
  run(['publish', 'secret', 'Rodzina', '--pages', '--stdin'], `${PASSWORD}\n`);
  run(['publish', 'run', 'Rodzina']);
  // Only ciphertext in the folder.
  const files = [];
  const walk = (d) => {
    for (const name of readdirSync(d)) {
      const p = join(d, name);
      if (statSync(p).isDirectory()) walk(p);
      else files.push(p);
    }
  };
  walk(www);
  for (const f of files) {
    const data = readFileSync(f);
    for (const secret of ['Zażółć', 'łąkach', 'Pierwszy wpis', 'Sekretny dziennik', 'łąka.png', PASSWORD])
      expect(data.includes(Buffer.from(secret)), `${f}: ${secret}`).toBe(false);
  }

  server = serve(www);
  await new Promise((ok) => server.listen(0, '127.0.0.1', ok));
  port = server.address().port;
});

test.afterAll(async () => {
  server?.close();
  if (work) rmSync(work, { recursive: true, force: true });
});

/** Types the password into the lock page and opens it. */
async function unlock(page, password, remember = true) {
  await expect(page.locator('#brulion-password')).toBeVisible();
  await page.fill('#brulion-password', password);
  await page.locator('#brulion-remember').setChecked(remember);
  await page.locator('#brulion-unlock button[type="submit"]').click();
}

test('a wrong password shows nothing, the right one opens the pages and their picture', async ({ page }) => {
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  await page.goto(`http://127.0.0.1:${port}/`);
  await expect(page).toHaveTitle('Protected page');
  await unlock(page, 'Zdzblo trawy na lace');
  await expect(page.locator('#brulion-note')).toHaveText('Wrong password.');
  await expect(page.locator('body')).not.toContainText('Pierwszy wpis');
  expect(await page.evaluate(() => sessionStorage.length)).toBe(0);

  await unlock(page, PASSWORD);
  await expect(page.locator('body')).toContainText('Pierwszy wpis');
  await expect(page).toHaveTitle(/Sekretny dziennik/);
  await expect(page.locator('#brulion-unlock')).toHaveCount(0);
  // The key is kept for the session, never the password.
  const kept = await page.evaluate(() => Object.keys(sessionStorage).map((k) => [k, sessionStorage.getItem(k)]));
  expect(kept).toHaveLength(1);
  expect(kept[0][0]).toMatch(/^brulion-key:/);
  expect(kept[0][1]).not.toContain(PASSWORD);
  expect(Buffer.from(kept[0][1], 'base64')).toHaveLength(32);

  // A link to another protected page: opened without asking.
  await page.getByRole('link', { name: 'Pierwszy wpis' }).first().click();
  await expect(page.locator('body')).toContainText(TEXT_ONE);
  await expect(page).toHaveTitle('Pierwszy wpis – Sekretny dziennik');
  expect(new URL(page.url()).pathname).toMatch(/^\/p\/[0-9a-f]{20}\.html$/);
  await page.getByRole('link', { name: 'Drugi wpis' }).first().click();
  await expect(page.locator('body')).toContainText(TEXT_TWO);
  // Its picture: fetched, decrypted, drawn.
  const img = page.locator('main img').first();
  await expect(img).toHaveAttribute('src', /^blob:/);
  await expect.poll(() => img.evaluate((el) => el.complete && el.naturalWidth)).toBe(PICTURE.width);
  expect(errors).toEqual([]);
});

test('another browser asks again; without "Remember" every page asks', async ({ browser }) => {
  const context = await browser.newContext();
  const page = await context.newPage();
  await page.goto(`http://127.0.0.1:${port}/`);
  await unlock(page, PASSWORD, false);
  await expect(page.locator('body')).toContainText('Drugi wpis');
  expect(await page.evaluate(() => sessionStorage.length)).toBe(0);
  await page.getByRole('link', { name: 'Drugi wpis' }).first().click();
  await expect(page.locator('#brulion-password')).toBeVisible();
  await expect(page.locator('body')).not.toContainText(TEXT_TWO);
  await unlock(page, PASSWORD);
  await expect(page.locator('body')).toContainText(TEXT_TWO);
  await context.close();
});

test('over plain http on another name the page says it needs https', async ({ page, browserName }) => {
  test.skip(browserName !== 'chromium', 'the host mapping is Chromium\'s');
  await page.goto(`http://insecure.brulion.test:${port}/`);
  await expect(page.locator('#brulion-note')).toContainText('https');
  await expect(page.locator('#brulion-unlock button[type="submit"]')).toBeDisabled();
});
