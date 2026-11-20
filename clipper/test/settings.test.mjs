// Settings and template lists (clipper/lib/settings.js, clipper/lib/templates.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { DEFAULT_SETTINGS, MAX_CUSTOM_TEMPLATES, normalizeSettings, templateFor } from '../lib/settings.js';
import { pickTemplate, templateOptions } from '../lib/templates.js';

test('normalizeSettings fills defaults and drops malformed values', () => {
  assert.deepEqual(normalizeSettings(undefined), { ...DEFAULT_SETTINGS });
  const s = normalizeSettings({ place: 'garden', folder: 5, downloadPictures: 'no', journalLink: 'yes', customTemplates: [{ name: 'Mój', text: '{{title}}' }, null, { name: 1 }, { id: 'bad id!', name: ' ', text: 'x' }] });
  assert.equal(s.place, 'page');
  assert.equal(s.folder, null);
  assert.equal(s.downloadPictures, true);
  assert.equal(s.journalLink, false);
  assert.deepEqual(s.customTemplates, [
    { id: 't1', name: 'Mój', text: '{{title}}' },
    { id: 't2', name: '#2', text: 'x' },
  ]);
  const many = normalizeSettings({ customTemplates: Array.from({ length: 80 }, (_, i) => ({ id: `c${i}`, name: `n${i}`, text: 'x' })) });
  assert.equal(many.customTemplates.length, MAX_CUSTOM_TEMPLATES);
});

test('templateFor: a page and a block have their own', () => {
  const s = normalizeSettings({ templatePage: 'builtin:obsidian', templateBlock: 'custom:t1' });
  assert.equal(templateFor(s, 'page'), 'builtin:obsidian');
  assert.equal(templateFor(s, 'journal'), 'custom:t1');
  assert.equal(templateFor(s, 'heading'), 'custom:t1');
});

test('templateOptions: built-ins (translated), the notebook\'s pages, one\'s own', () => {
  const t = (key) => ({ template_brulion: 'Strona Brulionu' })[key] ?? key;
  const options = templateOptions(
    [{ id: 'brulion', name: 'Brulion', builtin: true }, { id: 'link', name: 'Link and selection', builtin: true }, { page: 'templates/Przepis.md', name: 'Przepis', builtin: false }],
    [{ id: 't1', name: 'Mój' }],
    t,
  );
  assert.deepEqual(options, [
    { value: 'builtin:brulion', label: 'Strona Brulionu', group: 'builtin' },
    { value: 'builtin:link', label: 'Link and selection', group: 'builtin' },
    { value: 'page:templates/Przepis.md', label: 'Przepis', group: 'notebook' },
    { value: 'custom:t1', label: 'Mój', group: 'custom' },
  ]);
  assert.equal(templateOptions(undefined, [], (k) => k).length, 4); // no host: the four built-ins
  assert.equal(pickTemplate(options, 'custom:t1', 'page'), 'custom:t1');
  assert.equal(pickTemplate(options, 'custom:gone', 'page'), 'builtin:brulion');
  assert.equal(pickTemplate(options, 'custom:gone', 'journal'), 'builtin:link');
});
