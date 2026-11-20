// The clipper's popup (docs/CLIPPER.md "Clipping"): what to clip (article, selection, whole page,
// screenshot, link only), the title, where it goes (a new page in a folder, today's journal, the end
// of a heading's section), the template, tags and pictures, a preview of the Markdown, and the result
// with "Open in Brulion". Without the host it says how to connect and offers copying the page as
// rich text (which Brulion's editor converts when it is pasted, also on the phone and in the browser).
// The page's content is only ever shown as text here, never as HTML.

import { api, localize, t } from './lib/api.js';
import { extractFromTab, captureVisible, openInBrulion } from './lib/clip.js';
import * as host from './lib/host.js';
import { buildClipRequest, chooseKind, formProblem } from './lib/request.js';
import { loadSettings, saveSettings, templateFor } from './lib/settings.js';
import { pickTemplate, templateOptions } from './lib/templates.js';

const $ = (id) => document.getElementById(id);
const params = new URLSearchParams(location.search);

const state = {
  settings: null,
  tab: null,
  extraction: null,
  hello: null,
  templates: [],
  templateTouched: false,
  result: null,
  vault: '',
};

/** The tab to clip: the active one (or ?tab=<id> when the popup is opened as a page, as in tests). */
async function targetTab() {
  if (params.has('tab')) return api.tabs.get(Number(params.get('tab')));
  const [tab] = await api.tabs.query({ active: true, currentWindow: true });
  return tab;
}

function show(id, visible) {
  $(id).hidden = !visible;
}

function errorText(error) {
  if (error?.code) {
    const known = t(`hostError_${error.code}`);
    if (known !== `hostError_${error.code}`) return known;
  }
  return String(error?.message || error?.code || error);
}

function fillSelect(select, options, value) {
  select.replaceChildren();
  let group = null;
  let lastGroup = '';
  for (const o of options) {
    if (o.group && o.group !== lastGroup) {
      group = document.createElement('optgroup');
      group.label = t(`templateGroup_${o.group}`);
      select.append(group);
      lastGroup = o.group;
    }
    const option = new Option(o.label, o.value);
    (o.group ? group : select).append(option);
  }
  if (value !== undefined) select.value = value;
}

function kind() {
  return document.querySelector('input[name="kind"]:checked')?.value ?? 'article';
}

function setKind(value) {
  const input = document.querySelector(`input[name="kind"][value="${value}"]`);
  if (input && !input.disabled) input.checked = true;
}

function placeChanged() {
  const place = $('place').value;
  show('place-page', place === 'page');
  show('place-heading', place === 'heading');
  if (!state.templateTouched) $('template').value = pickTemplate(state.templates, templateFor(state.settings, place), place);
  if (place === 'heading') loadHeadings();
  previewSoon();
}

let pagesTimer = 0;
function searchPages() {
  clearTimeout(pagesTimer);
  pagesTimer = setTimeout(async () => {
    try {
      const answer = await host.request('pages', { vault: state.vault, query: $('heading-page').value, limit: 30 });
      const list = $('pages');
      list.replaceChildren(...answer.pages.map((p) => new Option(p.title, p.path)));
    } catch {
      // The list only helps typing.
    }
  }, 200);
}

let headingsFor = '';
async function loadHeadings() {
  const page = $('heading-page').value.trim();
  if (!page || page === headingsFor) return;
  headingsFor = page;
  const select = $('heading');
  try {
    const answer = await host.request('headings', { vault: state.vault, page });
    const options = answer.headings.map((h) => new Option(`${'#'.repeat(h.level)} ${h.text}`, h.text));
    select.replaceChildren(...options);
    const remembered = state.settings.headingPage === page ? state.settings.heading : '';
    if (remembered && answer.headings.some((h) => h.text === remembered)) select.value = remembered;
    $('problem').hidden = true;
  } catch (error) {
    select.replaceChildren();
    $('problem').textContent = errorText(error);
    $('problem').hidden = false;
  }
}

function form() {
  return {
    kind: kind(),
    title: $('title').value,
    place: $('place').value,
    folder: $('folder').value,
    headingPage: $('heading-page').value.trim(),
    heading: $('heading').value,
    journalLink: $('journal-link').checked,
    template: $('template').value,
    tags: $('tags').value,
    downloadPictures: $('pictures').checked,
  };
}

/** The clip of the form; reads the whole page or takes the screenshot when the kind needs it. */
async function request() {
  const f = form();
  const extra = {};
  let extraction = state.extraction;
  if (f.kind === 'page' && !extraction?.pageHtml && state.tab) {
    extraction = (await extractFromTab(state.tab.id, { page: true, article: false })) ?? extraction;
    if (extraction) state.extraction = { ...state.extraction, pageHtml: extraction.pageHtml };
    extraction = state.extraction;
  }
  if (f.kind === 'screenshot') extra.screenshot = await captureVisible(state.tab.windowId);
  return buildClipRequest({
    extraction,
    form: f,
    tab: { url: state.tab?.url, title: state.tab?.title },
    extra,
    customTemplates: state.settings.customTemplates,
  });
}

let previewTimer = 0;
function previewSoon() {
  if (!$('preview-box').open) return;
  clearTimeout(previewTimer);
  previewTimer = setTimeout(preview, 250);
}

async function preview() {
  const out = $('preview');
  if (kind() === 'screenshot') {
    out.textContent = t('previewScreenshot');
    return;
  }
  out.textContent = t('loading');
  try {
    const clip = await request();
    const answer = await host.request('preview', { vault: state.vault, clip });
    out.textContent = answer.result.markdown + (answer.result.truncated ? '\n…' : '');
  } catch (error) {
    out.textContent = errorText(error);
  }
}

async function save(event) {
  event.preventDefault();
  const problem = formProblem(form());
  if (problem) {
    $('problem').textContent = t(problem);
    $('problem').hidden = false;
    return;
  }
  $('problem').hidden = true;
  $('save').disabled = true;
  let clip;
  try {
    clip = await request();
  } catch (error) {
    $('save').disabled = false;
    $('problem').textContent = errorText(error);
    $('problem').hidden = false;
    return;
  }
  show('form', false);
  show('status', true);
  $('status-text').textContent = t('saving');
  const token = `${Date.now().toString(36)}${Math.random().toString(36).slice(2, 8)}`;
  const onProgress = (message) => {
    if (message?.type !== 'clip-progress' || message.token !== token) return;
    const bar = $('progress');
    bar.hidden = false;
    bar.max = message.total || 1;
    bar.value = message.done;
    $('status-text').textContent = t('savingPictures', [String(message.done), String(message.total)]);
  };
  api.runtime.onMessage.addListener(onProgress);
  const answer = await api.runtime.sendMessage({ type: 'clip', vault: state.vault, clip, token });
  api.runtime.onMessage.removeListener(onProgress);
  show('progress', false);
  if (!answer?.ok) {
    show('status', false);
    show('form', true);
    $('save').disabled = false;
    $('problem').textContent = answer?.code ? errorText(answer) : t('hostError_disconnected');
    $('problem').hidden = false;
    return;
  }
  state.result = answer.result;
  // The next popup starts where this clip went.
  const f = form();
  const remembered = { notebook: state.vault, place: f.place };
  if (f.place === 'page') {
    remembered.folder = f.folder;
    remembered.journalLink = f.journalLink;
    remembered.templatePage = f.template;
  } else {
    remembered.templateBlock = f.template;
  }
  if (f.place === 'heading') {
    remembered.headingPage = f.headingPage;
    remembered.heading = f.heading;
  }
  saveSettings(remembered);
  const pictures = answer.result.pictures ?? {};
  $('status-text').textContent = answer.result.created ? t('savedNewPage') : t('savedBlock');
  $('result-path').textContent = answer.result.path;
  const failed = pictures.failed?.length ?? 0;
  const details = [];
  if (pictures.saved) details.push(t('picturesSaved', [String(pictures.saved)]));
  if (failed) details.push(t('picturesFailed', [String(failed)]));
  if (pictures.kept && !clip.downloadPictures) details.push(t('picturesKept', [String(pictures.kept)]));
  $('result-detail').textContent = details.join(' · ');
  show('result', true);
  $('open').focus();
}

async function openResult() {
  const result = state.result;
  if (!result) return;
  $('open').disabled = true;
  try {
    await openInBrulion(state.vault, result.open?.path ?? result.path, result.open?.line ?? 0);
    window.close();
  } catch (error) {
    $('result-detail').textContent = errorText(error);
    $('open').disabled = false;
  }
}

/** Without the host: the page's HTML on the clipboard, which Brulion's editor turns into Markdown. */
async function copyRich() {
  const e = state.extraction ?? {};
  const url = e.url || state.tab?.url || '';
  const title = e.title || state.tab?.title || url;
  const link = document.createElement('a');
  link.href = url;
  link.textContent = title;
  const html = (e.selectionHtml || e.articleHtml || '') + `<p>${link.outerHTML}</p>`;
  const text = `${e.selectionText ? `${e.selectionText}\n\n` : ''}${title}\n${url}`;
  try {
    await navigator.clipboard.write([
      new ClipboardItem({
        'text/html': new Blob([html], { type: 'text/html' }),
        'text/plain': new Blob([text], { type: 'text/plain' }),
      }),
    ]);
  } catch {
    await navigator.clipboard.writeText(text);
  }
  show('copied', true);
}

async function copyLink() {
  const url = state.extraction?.url || state.tab?.url || '';
  const title = state.extraction?.title || state.tab?.title || url;
  await navigator.clipboard.writeText(`[${title.replace(/[[\]]/g, '\\$&')}](${url})`);
  show('copied', true);
}

function fillNotebooks(notebooks) {
  const select = $('notebook');
  const list = (notebooks ?? []).filter((n) => n.exists !== false);
  select.replaceChildren(...list.map((n) => new Option(n.name || n.path, n.path)));
  const current = list.find((n) => n.path === state.settings.notebook) ?? list.find((n) => n.current) ?? list[0];
  state.vault = state.settings.notebook && list.some((n) => n.path === state.settings.notebook) ? state.settings.notebook : '';
  if (current) select.value = current.path;
  select.hidden = list.length < 2;
  select.title = current?.path ?? '';
}

function applyHello(hello) {
  state.hello = hello;
  state.templates = templateOptions(hello.templates, state.settings.customTemplates, t);
  fillSelect($('template'), state.templates);
  $('template').value = pickTemplate(state.templates, templateFor(state.settings, $('place').value), $('place').value);
  $('folders').replaceChildren(...(hello.folders ?? []).map((f) => new Option(f, f)));
}

async function changeNotebook() {
  state.vault = $('notebook').value;
  headingsFor = '';
  try {
    applyHello(await host.request('hello', { vault: state.vault }));
  } catch (error) {
    $('problem').textContent = errorText(error);
    $('problem').hidden = false;
  }
  if ($('place').value === 'heading') loadHeadings();
}

async function init() {
  localize(document);
  state.settings = await loadSettings();
  // Lets the background know the popup is open (a clip it saves notifies only once it closes).
  api.runtime.connect({ name: 'popup' });
  state.tab = await targetTab();
  const [hello, extraction] = await Promise.allSettled([
    host.request('hello', { vault: state.settings.notebook }),
    state.tab ? extractFromTab(state.tab.id) : Promise.resolve(null),
  ]);
  state.extraction = extraction.status === 'fulfilled' ? extraction.value : null;
  show('loading', false);
  if (hello.status === 'rejected') {
    show('disconnected', true);
    $('disconnected-detail').textContent =
      hello.reason?.code === 'not_connected' ? t('notConnectedDetail') : errorText(hello.reason);
    return;
  }
  if (hello.value.notebookError && !hello.value.notebook) {
    show('disconnected', true);
    $('install-command').textContent = 'brulion-cli init ~/Notatki';
    $('disconnected-detail').textContent = hello.value.notebookError.message || t('hostError_no_vault');
    return;
  }
  fillNotebooks(hello.value.notebooks);
  const s = state.settings;
  $('place').value = s.place;
  $('folder').value = s.folder ?? t('defaultFolder');
  $('journal-link').checked = s.journalLink;
  $('heading-page').value = s.headingPage;
  $('tags').value = s.tags;
  $('pictures').checked = s.downloadPictures;
  applyHello(hello.value);

  const e = state.extraction;
  const available = {
    article: Boolean(e?.articleHtml),
    selection: Boolean(e?.selectionHtml || e?.selectionText),
    page: Boolean(e),
    screenshot: Boolean(state.tab),
    bookmark: true,
  };
  for (const input of document.querySelectorAll('input[name="kind"]')) input.disabled = !available[input.value];
  setKind(chooseKind(e, 'auto'));
  $('title').value = (e?.articleTitle && e.articleTitle.length < (e.title?.length ?? 0) ? e.articleTitle : e?.title) || state.tab?.title || '';
  if (!e) {
    $('problem').textContent = t('cannotRead');
    $('problem').hidden = false;
  }
  placeChanged();
  show('form', true);
  $('title').focus();
}

$('place').addEventListener('change', placeChanged);
$('template').addEventListener('change', () => {
  state.templateTouched = true;
  previewSoon();
});
$('heading-page').addEventListener('input', searchPages);
$('heading-page').addEventListener('change', loadHeadings);
$('notebook').addEventListener('change', changeNotebook);
for (const id of ['title', 'folder', 'tags', 'heading', 'pictures']) $(id).addEventListener('change', previewSoon);
$('kinds').addEventListener('change', previewSoon);
$('preview-box').addEventListener('toggle', () => $('preview-box').open && preview());
$('form').addEventListener('submit', save);
$('open').addEventListener('click', openResult);
$('close').addEventListener('click', () => window.close());
$('copy-rich').addEventListener('click', copyRich);
$('copy-link').addEventListener('click', copyLink);
$('options').addEventListener('click', (event) => {
  event.preventDefault();
  api.runtime.openOptionsPage();
});

init();
