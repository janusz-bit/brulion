// The clipper's options (docs/CLIPPER.md "Options"): the connection to Brulion, the notebook and where
// clips go, the templates per place, tags of every clip, pictures, and templates of one's own.

import { localize, t } from './lib/api.js';
import * as host from './lib/host.js';
import { checkFolder } from './lib/request.js';
import { loadSettings, MAX_CUSTOM_TEMPLATES, saveSettings } from './lib/settings.js';
import { pickTemplate, templateOptions } from './lib/templates.js';

const $ = (id) => document.getElementById(id);
let settings = null;
let hello = null;
let editing = null; // the id of the template of one's own being edited

function fillTemplates() {
  const options = templateOptions(hello?.templates, settings.customTemplates, t);
  for (const [id, value, place] of [
    ['template-page', settings.templatePage, 'page'],
    ['template-block', settings.templateBlock, 'journal'],
  ]) {
    const select = $(id);
    select.replaceChildren(...options.map((o) => new Option(o.label, o.value)));
    select.value = pickTemplate(options, value, place);
  }
}

function fillCustomList() {
  const list = $('custom-list');
  list.replaceChildren();
  for (const custom of settings.customTemplates) {
    const item = document.createElement('div');
    item.className = 'template-item';
    const name = document.createElement('span');
    name.textContent = custom.name;
    const edit = document.createElement('button');
    edit.type = 'button';
    edit.textContent = t('templateEdit');
    edit.addEventListener('click', () => {
      editing = custom.id;
      $('custom-name').value = custom.name;
      $('custom-text').value = custom.text;
      $('custom-name').focus();
    });
    const remove = document.createElement('button');
    remove.type = 'button';
    remove.textContent = t('templateDelete');
    remove.addEventListener('click', async () => {
      settings = await saveSettings({ customTemplates: settings.customTemplates.filter((c) => c.id !== custom.id) });
      fillCustomList();
      fillTemplates();
    });
    item.append(name, edit, remove);
    list.append(item);
  }
}

async function connect() {
  $('connection').textContent = t('loading');
  $('connect-help').hidden = true;
  try {
    hello = await host.request('hello', { vault: settings.notebook });
    const notebook = hello.notebook;
    $('connection').textContent = notebook
      ? t('connectedTo', [hello.version, notebook.name])
      : hello.notebookError?.message ?? t('hostError_no_vault');
    const select = $('notebook');
    const list = (hello.notebooks ?? []).filter((n) => n.exists !== false);
    select.replaceChildren(new Option(t('currentNotebook'), ''), ...list.map((n) => new Option(`${n.name} — ${n.path}`, n.path)));
    select.value = list.some((n) => n.path === settings.notebook) ? settings.notebook : '';
    $('folders').replaceChildren(...(hello.folders ?? []).map((f) => new Option(f, f)));
  } catch (error) {
    hello = null;
    $('connection').textContent = error?.code === 'not_connected' ? t('notConnectedTitle') : String(error?.message ?? error);
    $('connect-help').hidden = false;
    $('notebook').replaceChildren(new Option(t('currentNotebook'), ''));
  }
  fillTemplates();
}

async function init() {
  localize(document);
  settings = await loadSettings();
  $('place').value = settings.place;
  $('folder').value = settings.folder ?? t('defaultFolder');
  $('journal-link').checked = settings.journalLink;
  $('heading-page').value = settings.headingPage;
  $('heading').value = settings.heading;
  $('tags').value = settings.tags;
  $('pictures').checked = settings.downloadPictures;
  fillCustomList();
  await connect();
}

$('test').addEventListener('click', connect);

$('settings').addEventListener('submit', async (event) => {
  event.preventDefault();
  const folder = checkFolder($('folder').value);
  if (folder === null) {
    $('saved').textContent = t('errorFolder');
    $('saved').hidden = false;
    return;
  }
  settings = await saveSettings({
    notebook: $('notebook').value,
    place: $('place').value,
    folder,
    journalLink: $('journal-link').checked,
    headingPage: $('heading-page').value.trim(),
    heading: $('heading').value.trim(),
    templatePage: $('template-page').value,
    templateBlock: $('template-block').value,
    tags: $('tags').value,
    downloadPictures: $('pictures').checked,
  });
  $('saved').textContent = t('settingsSaved');
  $('saved').hidden = false;
  setTimeout(() => ($('saved').hidden = true), 2500);
});

$('custom-new').addEventListener('click', () => {
  editing = null;
  $('custom-name').value = '';
  $('custom-text').value = '';
  $('custom-name').focus();
});

$('custom-save').addEventListener('click', async () => {
  const name = $('custom-name').value.trim();
  const text = $('custom-text').value;
  if (!name || !text.trim()) return;
  let list = settings.customTemplates;
  if (editing && list.some((c) => c.id === editing)) {
    list = list.map((c) => (c.id === editing ? { ...c, name, text } : c));
  } else {
    if (list.length >= MAX_CUSTOM_TEMPLATES) return;
    editing = `t${Date.now().toString(36)}`;
    list = [...list, { id: editing, name, text }];
  }
  settings = await saveSettings({ customTemplates: list });
  fillCustomList();
  fillTemplates();
  $('saved').textContent = t('settingsSaved');
  $('saved').hidden = false;
});

init();
