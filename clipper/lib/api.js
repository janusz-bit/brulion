// The WebExtension API of the browser the clipper runs in (docs/CLIPPER.md): `browser` in Firefox,
// `chrome` in Chromium browsers — both return promises in Manifest V3. Also the translations
// (_locales/<language>/messages.json) and the page's texts filled from them.

export const api = globalThis.browser ?? globalThis.chrome;

/** The text `key` in the browser's language (English when there is no Polish one). */
export function t(key, substitutions) {
  const text = api?.i18n?.getMessage(key, substitutions);
  return text || key;
}

/**
 * Fill the texts of `root`: `data-i18n` (the text), `data-i18n-placeholder`, `data-i18n-title` and
 * `data-i18n-aria-label` name messages.
 */
export function localize(root) {
  for (const element of root.querySelectorAll('[data-i18n]')) element.textContent = t(element.dataset.i18n);
  for (const element of root.querySelectorAll('[data-i18n-placeholder]'))
    element.setAttribute('placeholder', t(element.dataset.i18nPlaceholder));
  for (const element of root.querySelectorAll('[data-i18n-title]'))
    element.setAttribute('title', t(element.dataset.i18nTitle));
  for (const element of root.querySelectorAll('[data-i18n-aria-label]'))
    element.setAttribute('aria-label', t(element.dataset.i18nAriaLabel));
  const language = api?.i18n?.getUILanguage?.() ?? 'en';
  root.documentElement?.setAttribute('lang', language.toLowerCase().startsWith('pl') ? 'pl' : 'en');
}

/** Is this Firefox (no service worker, no notification buttons)? */
export function isFirefox() {
  return typeof globalThis.browser !== 'undefined' && typeof api?.runtime?.getBrowserInfo === 'function';
}
