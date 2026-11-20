// Clipping a tab (docs/CLIPPER.md): read it (content/extract.js in the page's isolated world), take a
// picture of its visible part, and hand the clip to Brulion's host. Used by the popup (it reads the
// page to fill its form) and the background (context menus, the shortcut, the popup's clips, which
// go on when the popup closes).

import { api } from './api.js';
import * as host from './host.js';

/**
 * Read the tab: {url, title, author, articleHtml, selectionHtml, …}, or null when the browser lets no
 * extension read the page (its own pages, the extension store, a PDF viewer).
 */
export async function extractFromTab(tabId, { page = false, article = true } = {}) {
  try {
    await api.scripting.executeScript({ target: { tabId }, files: ['vendor/Readability.js', 'content/extract.js'] });
    const [result] = await api.scripting.executeScript({
      target: { tabId },
      func: (options) => globalThis.brulionClipperExtract(options),
      args: [{ page, article }],
    });
    return result?.result ?? null;
  } catch {
    return null;
  }
}

/** The visible part of the tab's window as a PNG data: URL. */
export async function captureVisible(windowId) {
  return api.tabs.captureVisibleTab(windowId, { format: 'png' });
}

/** Save `clip` into `vault` ("" = Brulion's current notebook). */
export async function saveClip(vault, clip, onProgress) {
  return host.clip({ vault, clip }, onProgress);
}

/** Show `path` (a page of `vault`) in Brulion. */
export async function openInBrulion(vault, path, line = 0) {
  return host.request('open', { vault, path, line });
}
