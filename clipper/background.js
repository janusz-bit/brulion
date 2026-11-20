// The clipper's background (docs/CLIPPER.md): the context menus (article, selection, page,
// screenshot, link, picture), the quick-clip shortcut, the popup's clips (saved here, so closing the
// popup does not stop them) and the notifications with "Open in Brulion". A service worker in
// Chromium browsers, an event page in Firefox.

import { api, t, isFirefox } from './lib/api.js';
import { captureVisible, extractFromTab, openInBrulion, saveClip } from './lib/clip.js';
import { HostError } from './lib/host.js';
import { buildClipRequest, chooseKind } from './lib/request.js';
import { loadSettings, templateFor } from './lib/settings.js';

const MENUS = [
  { id: 'clip-article', contexts: ['page', 'frame'], title: 'menuArticle' },
  { id: 'clip-selection', contexts: ['selection'], title: 'menuSelection' },
  { id: 'clip-page', contexts: ['page', 'frame'], title: 'menuPage' },
  { id: 'clip-screenshot', contexts: ['page', 'frame'], title: 'menuScreenshot' },
  { id: 'clip-link', contexts: ['link'], title: 'menuLink' },
  { id: 'clip-picture', contexts: ['image'], title: 'menuPicture' },
];

async function createMenus() {
  await api.contextMenus.removeAll();
  for (const menu of MENUS) api.contextMenus.create({ id: menu.id, contexts: menu.contexts, title: t(menu.title) });
}

api.runtime.onInstalled.addListener(() => createMenus());
api.runtime.onStartup?.addListener(() => createMenus());

/** Where finished clips can be opened from their notification: id → {vault, path, line}. */
const notified = new Map();

function notify(id, title, message, openable) {
  const options = { type: 'basic', iconUrl: api.runtime.getURL('icons/brulion-128.png'), title, message };
  // Firefox's notifications have no buttons: a click opens the page.
  if (openable && !isFirefox()) options.buttons = [{ title: t('openInBrulion') }];
  api.notifications.create(id, options);
}

function errorText(error) {
  if (error instanceof HostError) {
    const known = t(`hostError_${error.code}`);
    if (known !== `hostError_${error.code}`) return known;
    return error.message || error.code;
  }
  return String(error?.message ?? error);
}

/** The popups that are open (a clip they started notifies only when its popup is gone). */
const popups = new Set();
api.runtime.onConnect.addListener((port) => {
  if (port.name !== 'popup') return;
  popups.add(port);
  port.onDisconnect.addListener(() => popups.delete(port));
});

async function runClip({ vault, clip, notifyAlways, token }) {
  const id = `clip-${token}`;
  try {
    const result = await saveClip(vault, clip, (done, total) => {
      api.runtime.sendMessage({ type: 'clip-progress', token, done, total }).catch(() => {});
    });
    notified.set(id, { vault, path: result.open?.path ?? result.path, line: result.open?.line ?? 0 });
    if (notifyAlways || popups.size === 0) {
      const failed = result.pictures?.failed?.length ?? 0;
      const message = failed > 0 ? t('savedWithFailedPictures', [result.path, String(failed)]) : t('savedTo', [result.path]);
      notify(id, t('savedTitle'), message, true);
    }
    return { ok: true, result };
  } catch (error) {
    if (notifyAlways || popups.size === 0) notify(id, t('failedTitle'), errorText(error), false);
    return { ok: false, code: error?.code ?? 'error', message: errorText(error), details: error?.details ?? {} };
  }
}

/** A clip from a context menu or the shortcut, with the settings' defaults. */
async function quickClip(tab, kind, info = {}) {
  if (!tab?.id) return;
  const settings = await loadSettings();
  const wantsPage = kind === 'page';
  const extraction = kind === 'link' || kind === 'picture' ? null : await extractFromTab(tab.id, { page: wantsPage });
  const extra = {};
  if (kind === 'screenshot') {
    try {
      extra.screenshot = await captureVisible(tab.windowId);
    } catch (error) {
      notify(`clip-${Date.now()}`, t('failedTitle'), errorText(error), false);
      return;
    }
  }
  if (kind === 'link') {
    extra.linkUrl = info.linkUrl ?? '';
    extra.linkText = info.selectionText || info.linkText || '';
  }
  if (kind === 'picture') extra.pictureUrl = info.srcUrl ?? '';
  const chosen = ['link', 'picture', 'screenshot'].includes(kind) ? kind : chooseKind(extraction, kind ?? 'auto');
  const place = settings.place;
  const form = {
    kind: chosen,
    title: kind === 'link' ? extra.linkText : '',
    place,
    folder: settings.folder ?? t('defaultFolder'),
    headingPage: settings.headingPage,
    heading: settings.heading,
    journalLink: settings.journalLink,
    template: templateFor(settings, place),
    tags: settings.tags,
    downloadPictures: settings.downloadPictures,
  };
  const clip = buildClipRequest({
    extraction,
    form,
    tab: { url: tab.url, title: tab.title },
    extra,
    customTemplates: settings.customTemplates,
  });
  if (kind === 'link' && info.pageUrl) clip.url = clip.url || info.pageUrl;
  await runClip({ vault: settings.notebook, clip, notifyAlways: true, token: Date.now().toString(36) });
}

api.contextMenus.onClicked.addListener((info, tab) => {
  const kind = String(info.menuItemId).replace(/^clip-/, '');
  quickClip(tab, kind, info);
});

api.commands.onCommand.addListener(async (command, tab) => {
  if (command !== 'quick-clip') return;
  const active = tab ?? (await api.tabs.query({ active: true, currentWindow: true }))[0];
  quickClip(active, 'auto');
});

async function openFromNotification(id) {
  const target = notified.get(id);
  if (!target) return;
  try {
    await openInBrulion(target.vault, target.path, target.line);
  } catch (error) {
    notify(`${id}-open`, t('failedTitle'), errorText(error), false);
  }
  api.notifications.clear(id);
}
api.notifications.onClicked.addListener((id) => openFromNotification(id));
api.notifications.onButtonClicked?.addListener((id) => openFromNotification(id));

api.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (sender.id !== api.runtime.id || message?.type !== 'clip') return false;
  runClip({ vault: message.vault ?? '', clip: message.clip, notifyAlways: false, token: message.token }).then(sendResponse);
  return true; // the answer comes later
});

// Tests (tests/web/clipper.spec.mjs) start a clip as a context menu would.
globalThis.brulionClipper = { quickClip, runClip };
