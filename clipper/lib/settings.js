// The clipper's settings (docs/CLIPPER.md "Options"), kept in the browser's local extension storage:
// the notebook, where a clip goes, the templates, tags, pictures, and the user's own templates.
// normalizeSettings() is pure (tests: clipper/test/).

import { api } from './api.js';

export const PLACES = ['page', 'journal', 'heading'];
/** Most templates of one's own, and their largest text (the host takes 64 KiB). */
export const MAX_CUSTOM_TEMPLATES = 50;
export const MAX_TEMPLATE_CHARS = 64 * 1024;

export const DEFAULT_SETTINGS = Object.freeze({
  notebook: '', // "" = Brulion's current notebook
  place: 'page',
  folder: null, // null = the folder for clips in the browser's language ("Clippings", "Wycinki")
  headingPage: '',
  heading: '',
  journalLink: false,
  templatePage: 'builtin:brulion', // a new page
  templateBlock: 'builtin:link', // today's journal, a heading's section
  tags: '',
  downloadPictures: true,
  customTemplates: [], // [{id, name, text}]
});

function text(value, fallback, max = 1000) {
  return typeof value === 'string' ? value.slice(0, max) : fallback;
}

/** `raw` (what storage holds, maybe from an older version) as valid settings. */
export function normalizeSettings(raw) {
  const s = raw && typeof raw === 'object' ? raw : {};
  const out = { ...DEFAULT_SETTINGS };
  out.notebook = text(s.notebook, '', 4096);
  out.place = PLACES.includes(s.place) ? s.place : 'page';
  out.folder = typeof s.folder === 'string' ? s.folder.slice(0, 1000) : null;
  out.headingPage = text(s.headingPage, '', 4096);
  out.heading = text(s.heading, '', 300);
  out.journalLink = s.journalLink === true;
  out.templatePage = text(s.templatePage, DEFAULT_SETTINGS.templatePage, 4096);
  out.templateBlock = text(s.templateBlock, DEFAULT_SETTINGS.templateBlock, 4096);
  out.tags = text(s.tags, '', 2000);
  out.downloadPictures = s.downloadPictures !== false;
  const custom = Array.isArray(s.customTemplates) ? s.customTemplates : [];
  out.customTemplates = custom
    .filter((c) => c && typeof c === 'object' && typeof c.text === 'string' && typeof c.name === 'string')
    .slice(0, MAX_CUSTOM_TEMPLATES)
    .map((c, i) => ({
      id: typeof c.id === 'string' && /^[A-Za-z0-9_-]{1,40}$/.test(c.id) ? c.id : `t${i + 1}`,
      name: c.name.trim().slice(0, 100) || `#${i + 1}`,
      text: c.text.slice(0, MAX_TEMPLATE_CHARS),
    }));
  return out;
}

/** The template a place starts with (its option value: "builtin:…", "page:…", "custom:…"). */
export function templateFor(settings, place) {
  return place === 'page' ? settings.templatePage : settings.templateBlock;
}

export async function loadSettings() {
  const stored = await api.storage.local.get('settings');
  return normalizeSettings(stored?.settings);
}

export async function saveSettings(changes) {
  const current = await loadSettings();
  const next = normalizeSettings({ ...current, ...changes });
  await api.storage.local.set({ settings: next });
  return next;
}
