/** Focus helpers for dialogs and composite widgets (browser-only). */
import type { KeyboardEvent as ReactKeyboardEvent } from 'react';

const FOCUSABLE = [
  'a[href]',
  'area[href]',
  'button:not([disabled])',
  'input:not([disabled]):not([type="hidden"])',
  'select:not([disabled])',
  'textarea:not([disabled])',
  'iframe',
  'audio[controls]',
  'video[controls]',
  '[contenteditable]:not([contenteditable="false"])',
  '[tabindex]',
].join(',');

function isVisible(el: HTMLElement): boolean {
  // jsdom has no layout: treat everything as visible there unless hidden explicitly.
  if (el.hidden || el.closest('[hidden], [inert]')) return false;
  if (typeof el.checkVisibility === 'function') {
    try {
      return el.checkVisibility();
    } catch {
      return true;
    }
  }
  return true;
}

/** Tabbable descendants of `root` in DOM order (tabindex >= 0, enabled, visible). */
export function getFocusable(root: HTMLElement): HTMLElement[] {
  const out: HTMLElement[] = [];
  for (const el of root.querySelectorAll<HTMLElement>(FOCUSABLE)) {
    if (el.tabIndex < 0) continue;
    if (el.getAttribute('aria-hidden') === 'true') continue;
    if (!isVisible(el)) continue;
    out.push(el);
  }
  return out;
}

/**
 * Keep Tab / Shift+Tab inside `root`: call from a keydown handler. Returns true when the
 * event was handled (focus wrapped).
 */
export function trapTabKey(e: KeyboardEvent | ReactKeyboardEvent, root: HTMLElement): boolean {
  if (e.key !== 'Tab') return false;
  const items = getFocusable(root);
  if (items.length === 0) {
    e.preventDefault();
    root.focus();
    return true;
  }
  const first = items[0]!;
  const last = items[items.length - 1]!;
  const active = document.activeElement;
  const inside = active instanceof Node && root.contains(active);
  if (e.shiftKey) {
    if (!inside || active === first || active === root) {
      e.preventDefault();
      last.focus();
      return true;
    }
  } else if (!inside || active === last) {
    e.preventDefault();
    first.focus();
    return true;
  }
  return false;
}

/**
 * Pick the element to focus when a dialog opens: an explicit `[data-autofocus]`, else the
 * first tabbable element that is not a close button, else the root itself.
 */
export function initialFocusTarget(root: HTMLElement): HTMLElement {
  const explicit = root.querySelector<HTMLElement>('[data-autofocus]');
  if (explicit) return explicit;
  const items = getFocusable(root).filter((el) => !el.hasAttribute('data-dialog-close'));
  return items[0] ?? root;
}

/** Focus without scrolling the page (where supported). */
export function focusQuietly(el: HTMLElement | null | undefined): void {
  if (!el) return;
  try {
    el.focus({ preventScroll: true });
  } catch {
    el.focus();
  }
}
