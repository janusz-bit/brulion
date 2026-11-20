// The clip request the extension builds (clipper/lib/request.js): kinds, tags, folders, templates,
// places; Polish text. Run: node --test clipper/test/ (ctest tst_clipper_extension).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildClipRequest, checkFolder, chooseKind, formProblem, splitTags, templateChoice, MAX_HTML_CHARS } from '../lib/request.js';

const extraction = {
  url: 'https://przyroda.example/artykuly/zolw.html',
  baseUrl: 'https://przyroda.example/artykuly/',
  title: 'Żółw stepowy — Przyroda Dziś',
  author: 'Zofia Łęcka',
  site: 'Przyroda Dziś',
  published: '2026-11-19T08:15:00+01:00',
  description: 'Jak zimuje żółw',
  language: 'pl',
  image: 'https://przyroda.example/og.jpg',
  articleHtml: '<p>Żółw stepowy…</p>',
  pageHtml: '',
  selectionHtml: '<p>gad, który <b>źle znosi</b> chłód</p>',
  selectionText: 'gad, który źle znosi chłód',
};

test('chooseKind prefers the selection, then the article, then the page', () => {
  assert.equal(chooseKind(extraction), 'selection');
  assert.equal(chooseKind({ ...extraction, selectionHtml: '', selectionText: '' }), 'article');
  assert.equal(chooseKind({ url: 'x' }), 'page');
  assert.equal(chooseKind(null), 'bookmark');
  assert.equal(chooseKind(extraction, 'article'), 'article');
  assert.equal(chooseKind({ url: 'x' }, 'article'), 'page'); // no article: not offered
  assert.equal(chooseKind(null, 'screenshot'), 'screenshot');
});

test('splitTags trims, drops # and duplicates (any letter case)', () => {
  assert.deepEqual(splitTags(' #do-przeczytania, Gady; gady ,, #Ogród zimą '), ['do-przeczytania', 'Gady', 'Ogród zimą']);
  assert.deepEqual(splitTags(''), []);
  assert.deepEqual(splitTags(undefined), []);
  assert.equal(splitTags(Array.from({ length: 50 }, (_, i) => `t${i}`).join(',')).length, 32);
});

test('checkFolder refuses what the host refuses', () => {
  assert.equal(checkFolder(''), '');
  assert.equal(checkFolder('  Wycinki / Gady '), 'Wycinki/Gady');
  assert.equal(checkFolder('Źródła/Przyroda'), 'Źródła/Przyroda');
  for (const bad of ['..', '../x', 'a/../b', '/etc', 'C:\\x', 'C:/x', 'a\\b', '.git', 'a/.brulion', 'a//b', 'a/ /b', 'x\u0007y', 'a/'.repeat(13) + 'b'])
    assert.equal(checkFolder(bad), null, bad);
});

test('templateChoice reads the option values', () => {
  assert.deepEqual(templateChoice('builtin:obsidian'), { builtin: 'obsidian' });
  assert.deepEqual(templateChoice('page:templates/Artykuł.md'), { page: 'templates/Artykuł.md' });
  assert.deepEqual(templateChoice('custom:t1', [{ id: 't1', name: 'Mój', text: '# {{title}}' }]), { text: '# {{title}}' });
  assert.deepEqual(templateChoice('custom:gone', []), { builtin: 'brulion' });
  assert.deepEqual(templateChoice(''), { builtin: 'brulion' });
});

test('buildClipRequest: an article into a new page', () => {
  const clip = buildClipRequest({
    extraction,
    form: { kind: 'article', title: ' Żółw ', place: 'page', folder: 'Wycinki', journalLink: true, template: 'builtin:brulion', tags: 'gady', downloadPictures: true },
    tab: { url: 'https://other.example/', title: 'Karta' },
  });
  assert.equal(clip.kind, 'article');
  assert.equal(clip.url, extraction.url);
  assert.equal(clip.title, 'Żółw');
  assert.equal(clip.html, extraction.articleHtml);
  assert.equal(clip.selectionHtml, extraction.selectionHtml);
  assert.deepEqual(clip.tags, ['gady']);
  assert.deepEqual(clip.place, { kind: 'page', folder: 'Wycinki', journalLink: true });
  assert.deepEqual(clip.template, { builtin: 'brulion' });
  assert.equal(clip.downloadPictures, true);
});

test('buildClipRequest: the journal, a heading, a screenshot, a link', () => {
  const journal = buildClipRequest({ extraction, form: { kind: 'selection', place: 'journal', template: 'builtin:link', downloadPictures: false }, tab: {} });
  assert.deepEqual(journal.place, { kind: 'journal' });
  assert.equal(journal.html, undefined);
  assert.equal(journal.downloadPictures, false);
  assert.equal(journal.title, extraction.title); // an empty title: the page's
  const heading = buildClipRequest({ extraction, form: { kind: 'bookmark', place: 'heading', headingPage: 'Czytelnia.md', heading: 'Do przeczytania' }, tab: {} });
  assert.deepEqual(heading.place, { kind: 'heading', page: 'Czytelnia.md', heading: 'Do przeczytania' });
  const shot = buildClipRequest({ extraction: null, form: { kind: 'screenshot', place: 'page' }, tab: { url: 'https://a.example/', title: 'A' }, extra: { screenshot: 'data:image/png;base64,AAAA' } });
  assert.equal(shot.screenshot, 'data:image/png;base64,AAAA');
  assert.equal(shot.url, 'https://a.example/');
  assert.equal(shot.title, 'A');
  const link = buildClipRequest({ extraction: null, form: { kind: 'link', place: 'journal' }, tab: {}, extra: { linkUrl: 'https://b.example/', linkText: 'B' } });
  assert.equal(link.linkUrl, 'https://b.example/');
  assert.equal(link.linkText, 'B');
});

test('buildClipRequest: a bad folder is left to the notebook default, sizes are cut', () => {
  const clip = buildClipRequest({ extraction: { ...extraction, articleHtml: 'x'.repeat(MAX_HTML_CHARS + 10) }, form: { kind: 'article', place: 'page', folder: '../poza' }, tab: {} });
  assert.equal(clip.place.folder, undefined);
  assert.equal(clip.html.length, MAX_HTML_CHARS);
});

test('formProblem names what is missing', () => {
  assert.equal(formProblem({ place: 'page', folder: '..' }), 'errorFolder');
  assert.equal(formProblem({ place: 'heading', headingPage: '', heading: 'x' }), 'errorNoPage');
  assert.equal(formProblem({ place: 'heading', headingPage: 'a.md', heading: '' }), 'errorNoHeading');
  assert.equal(formProblem({ place: 'journal' }), '');
});
