/**
 * Obsidian callouts: `> [!type]± Title` (docs/SYNTAX.md §11). Pure parsing (callouts.test.ts).
 */
import type { PreviewIcon } from './dom';

export type CalloutType = 'note' | 'tip' | 'warning' | 'danger' | 'info' | 'todo' | 'quote';

export const CALLOUT_TYPES: readonly CalloutType[] = ['note', 'tip', 'warning', 'danger', 'info', 'todo', 'quote'];

/** Obsidian's other callout names mapped onto Brulion's seven looks. */
const ALIASES: Record<string, CalloutType> = {
  note: 'note',
  seealso: 'note',
  example: 'note',
  abstract: 'info',
  summary: 'info',
  tldr: 'info',
  info: 'info',
  question: 'info',
  help: 'info',
  faq: 'info',
  todo: 'todo',
  tip: 'tip',
  hint: 'tip',
  important: 'tip',
  success: 'tip',
  check: 'tip',
  done: 'tip',
  warning: 'warning',
  caution: 'warning',
  attention: 'warning',
  danger: 'danger',
  error: 'danger',
  bug: 'danger',
  failure: 'danger',
  fail: 'danger',
  missing: 'danger',
  quote: 'quote',
  cite: 'quote',
};

export const CALLOUT_ICON: Record<CalloutType, PreviewIcon> = {
  note: 'pencil',
  tip: 'lightbulb',
  warning: 'warning',
  danger: 'flame',
  info: 'info',
  todo: 'circleCheck',
  quote: 'quote',
};

export interface CalloutHead {
  type: CalloutType;
  /** Type as written (lower-cased), e.g. "hint". */
  name: string;
  /** `-` collapsed by default, `+` expanded (foldable), '' not foldable. */
  fold: '' | '+' | '-';
  /** Title text ('' = default title for the type). */
  title: string;
  /** Offset of the first `>` within the line. */
  quoteFrom: number;
  /** Offset where the title starts (after `> [!type]± `). */
  titleFrom: number;
}

const HEAD_RE = /^([ \t]*)(>[ \t]?)\[!([A-Za-z][\w-]*)\]([+-]?)([ \t]*)(.*)$/;

/** Parse a callout header line; null for other lines. */
export function parseCalloutHead(line: string): CalloutHead | null {
  const m = HEAD_RE.exec(line);
  if (!m) return null;
  const name = m[3]!.toLowerCase();
  const quoteFrom = m[1]!.length;
  const titleFrom = quoteFrom + m[2]!.length + m[3]!.length + 3 + m[4]!.length + m[5]!.length;
  return { type: ALIASES[name] ?? 'note', name, fold: m[4] as CalloutHead['fold'], title: m[6]!.trim(), quoteFrom, titleFrom };
}

/** Length of the quote prefix (`> `, `>`, nested `> > `) at the start of a blockquote line. */
export function quotePrefixLength(line: string): number {
  const m = /^[ \t]*(?:>[ \t]?)+/.exec(line);
  return m ? m[0].length : 0;
}
