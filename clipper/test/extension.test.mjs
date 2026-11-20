// The extension as a whole: the manifest (its fixed ids match the host's: src/core/clipper/nativehost.cpp),
// the translations (English and Polish have the same keys, every key used exists), the page reader's
// pure helpers (content/extract.js in a VM).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync, readdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const read = (path) => readFileSync(join(root, path), 'utf8');
const manifest = JSON.parse(read('manifest.json'));
const locales = Object.fromEntries(['en', 'pl'].map((l) => [l, JSON.parse(read(`_locales/${l}/messages.json`))]));

test('the manifest key gives the Chromium id the host allows', () => {
  const id = createHash('sha256').update(Buffer.from(manifest.key, 'base64')).digest('hex').slice(0, 32)
    .replace(/[0-9a-f]/g, (c) => 'abcdefghijklmnop'[parseInt(c, 16)]);
  assert.equal(id, 'calcbkckojhmdfecpcohaangneahmkgg');
  const host = readFileSync(join(root, '../src/core/clipper/nativehost.cpp'), 'utf8');
  assert.ok(host.includes(`u"${id}"_s`), 'nativehost.cpp names the same Chromium id');
  assert.ok(host.includes(`u"${manifest.browser_specific_settings.gecko.id}"_s`), 'and the same Firefox id');
});

test('the manifest asks only for what the clipper needs', () => {
  assert.equal(manifest.manifest_version, 3);
  assert.deepEqual([...manifest.permissions].sort(), ['activeTab', 'contextMenus', 'nativeMessaging', 'notifications', 'scripting', 'storage']);
  assert.equal(manifest.host_permissions, undefined, 'no host permissions: pages are read only after a click (activeTab)');
  assert.equal(manifest.content_scripts, undefined, 'nothing runs in pages by itself');
  for (const file of [manifest.background.service_worker, manifest.action.default_popup, manifest.options_ui.page, ...Object.values(manifest.icons)])
    assert.doesNotThrow(() => read(file), file);
});

test('English and Polish have the same messages', () => {
  assert.deepEqual(Object.keys(locales.pl).sort(), Object.keys(locales.en).sort());
  for (const [key, value] of Object.entries(locales.pl)) {
    assert.ok(value.message.trim(), `pl ${key} is empty`);
    const dollars = (s) => (s.match(/\$\d/g) ?? []).sort().join();
    assert.equal(dollars(value.message), dollars(locales.en[key].message), `the substitutions of ${key}`);
  }
});

test('every message the extension uses exists', () => {
  const used = new Set();
  for (const m of JSON.stringify(manifest).matchAll(/__MSG_(\w+)__/g)) used.add(m[1]);
  const files = ['popup.html', 'options.html', 'popup.js', 'options.js', 'background.js', ...readdirSync(join(root, 'lib')).map((f) => `lib/${f}`)];
  for (const file of files) {
    const text = read(file);
    for (const m of text.matchAll(/data-i18n(?:-[a-z-]+)?="(\w+)"/g)) used.add(m[1]);
    for (const m of text.matchAll(/\bt\('(\w+)'/g)) used.add(m[1]);
    for (const m of text.matchAll(/title: '(menu\w+)'/g)) used.add(m[1]);
    for (const m of text.matchAll(/return '(error\w+)'/g)) used.add(m[1]);
  }
  for (const id of ['brulion', 'obsidian', 'joplin', 'link']) used.add(`template_${id}`);
  for (const group of ['builtin', 'notebook', 'custom']) used.add(`templateGroup_${group}`);
  for (const key of used) assert.ok(locales.en[key], `no message ${key}`);
});

test('the page reader picks the largest srcset candidate', () => {
  const context = { document: {}, window: {}, Node: {} };
  vm.createContext(context);
  vm.runInContext(read('content/extract.js'), context);
  const { largestSrcset } = context.brulionClipperHelpers;
  assert.equal(largestSrcset('a.jpg 640w, b.jpg 1280w, c.jpg 320w'), 'b.jpg');
  assert.equal(largestSrcset('a.jpg, b.jpg 2x, c.jpg 1.5x'), 'b.jpg');
  assert.equal(largestSrcset('https://cdn.example/w_300,h_200/zdjęcie.jpg 300w, https://cdn.example/w_900,h_600/zdjęcie.jpg 900w'), 'https://cdn.example/w_900,h_600/zdjęcie.jpg');
  assert.equal(largestSrcset(''), '');
  assert.equal(largestSrcset(undefined), '');
  assert.equal(largestSrcset(',,, ,'), '');
});
