import type { ReactNode } from 'react';
import { cx } from './hooks';

/** True on Apple platforms (⌘ instead of Ctrl). */
export function isApplePlatform(): boolean {
  if (typeof navigator === 'undefined') return false;
  const p =
    (navigator as Navigator & { userAgentData?: { platform?: string } }).userAgentData?.platform ??
    navigator.platform ??
    '';
  return /mac|iphone|ipad|ipod/i.test(p) || /Mac OS X/.test(navigator.userAgent ?? '');
}

const NAMES_APPLE: Record<string, string> = {
  mod: '⌘',
  cmd: '⌘',
  meta: '⌘',
  ctrl: '⌃',
  control: '⌃',
  alt: '⌥',
  option: '⌥',
  shift: '⇧',
};

const NAMES_OTHER: Record<string, string> = {
  mod: 'Ctrl',
  cmd: 'Ctrl',
  meta: 'Super',
  ctrl: 'Ctrl',
  control: 'Ctrl',
  alt: 'Alt',
  option: 'Alt',
  shift: 'Shift',
};

const COMMON: Record<string, string> = {
  arrowleft: '←',
  left: '←',
  arrowright: '→',
  right: '→',
  arrowup: '↑',
  up: '↑',
  arrowdown: '↓',
  down: '↓',
  enter: 'Enter',
  return: 'Enter',
  escape: 'Esc',
  esc: 'Esc',
  backspace: '⌫',
  delete: 'Del',
  tab: 'Tab',
  space: 'Space',
  pageup: 'PgUp',
  pagedown: 'PgDn',
  home: 'Home',
  end: 'End',
};

function own(rec: Record<string, string>, key: string): string | undefined {
  return Object.prototype.hasOwnProperty.call(rec, key) ? rec[key] : undefined;
}

/** Split "Mod+Shift+K" into raw key names; "+" itself is written "Mod++". */
export function splitShortcut(keys: string): string[] {
  const out: string[] = [];
  let cur = '';
  for (const c of keys.trim()) {
    if (c === '+' && cur.trim() !== '') {
      out.push(cur.trim());
      cur = '';
    } else {
      cur += c;
    }
  }
  if (cur.trim() !== '') out.push(cur.trim());
  return out;
}

/** Display names for a shortcut on the platform: "Mod+Shift+K" -> ["Ctrl", "Shift", "K"] / ["⌘", "⇧", "K"]. */
export function formatShortcut(keys: string, apple: boolean = isApplePlatform()): string[] {
  return splitShortcut(keys).map((key) => {
    const low = key.toLowerCase();
    return own(apple ? NAMES_APPLE : NAMES_OTHER, low) ?? own(COMMON, low) ?? (key.length === 1 ? key.toUpperCase() : key);
  });
}

export interface KbdProps {
  /** Shortcut like "Mod+K"; or pass children for a single literal key. */
  keys?: string;
  children?: ReactNode;
  className?: string;
  /** Force platform conventions (tests, gallery). */
  apple?: boolean;
}

export function Kbd({ keys, children, className, apple }: KbdProps) {
  if (!keys) return <kbd className={cx('b-kbd', className)}>{children}</kbd>;
  const parts = formatShortcut(keys, apple ?? isApplePlatform());
  return (
    <span className={cx('b-kbd-combo', className)}>
      <span className="b-sr-only">{parts.join(' + ')}</span>
      {parts.map((p, i) => (
        <kbd key={i} className="b-kbd" aria-hidden="true">
          {p}
        </kbd>
      ))}
    </span>
  );
}
