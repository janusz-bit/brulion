/**
 * Task / reminder tokens (`📅 2026-10-15`, `⏰! 2026-10-15 06:30`, `🔁 every week`, `⏫`) as chip
 * models: label text, icon and tone. Pure (no DOM) — see tokens.test.ts.
 */
import { compareLocal, formatDateHuman, formatRecurrence, normalizeTime, parseRecurrence, toLocalDate, toLocalDateTime } from '@/core/dates';
import type { Translator } from '@/core/i18n';
import type { LocalDate, LocalDateTime, Locale, Priority, TaskStatus } from '@/core/types';
import type { PreviewIcon } from './dom';

export type TokenKind = 'due' | 'scheduled' | 'start' | 'created' | 'done' | 'cancelled' | 'recurrence' | 'reminder' | 'alarm';

const MARK_KIND: Record<string, TokenKind> = {
  '📅': 'due',
  '📆': 'due',
  '🗓': 'due',
  '⏳': 'scheduled',
  '⌛': 'scheduled',
  '🛫': 'start',
  '➕': 'created',
  '✅': 'done',
  '❌': 'cancelled',
  '🔁': 'recurrence',
  '⏰': 'reminder',
  '⏰!': 'alarm',
};

/** Kind of a `TaskTokenMark` text (variation selectors ignored). */
export function tokenKind(mark: string): TokenKind | null {
  return MARK_KIND[mark.replace(/\uFE0F/g, '')] ?? null;
}

/** Date kinds edited with the date picker. */
export const DATE_KINDS: ReadonlySet<TokenKind> = new Set<TokenKind>(['due', 'scheduled', 'start']);

export type ChipTone = 'neutral' | 'muted' | 'danger' | 'warning' | 'success' | 'reminder' | 'alarm' | 'info';

export interface ChipModel {
  kind: TokenKind;
  text: string;
  icon: PreviewIcon;
  tone: ChipTone;
  /** Tooltip / accessible label. */
  title: string;
  /** Extra state classes ('is-overdue', 'is-today', 'is-past'). */
  flags: string[];
}

export interface ChipEnv {
  now: Date;
  locale: Locale;
  t: Translator;
  /** Status of the task the token belongs to (null outside tasks). */
  taskStatus?: TaskStatus | null;
}

/**
 * `2026-10-15` / `2026-10-15 9:30` (file syntax) → LocalDate / LocalDateTime
 * (`2026-10-15T09:30`); null when malformed.
 */
export function tokenDateValue(value: string): LocalDate | LocalDateTime | null {
  const m = /^(\d{4}-\d{2}-\d{2})(?:[ \tT]+(\d{1,2}:\d{2}))?$/.exec(value.trim());
  if (!m) return null;
  if (!m[2]) return m[1]!;
  const time = normalizeTime(m[2]);
  return time ? `${m[1]}T${time}` : m[1]!;
}

/** LocalDate / LocalDateTime → the file syntax (`2026-10-15 09:30`). */
export function fileDateValue(value: LocalDate | LocalDateTime): string {
  return value.replace('T', ' ');
}

const ICONS: Record<TokenKind, PreviewIcon> = {
  due: 'calendar',
  scheduled: 'hourglass',
  start: 'start',
  created: 'plus',
  done: 'check',
  cancelled: 'ban',
  recurrence: 'repeat',
  reminder: 'alarm',
  alarm: 'bell',
};

function closed(status: TaskStatus | null | undefined): boolean {
  return status === 'done' || status === 'cancelled';
}

/** Chip for a token of `kind` with value text `value` (date(-time) or recurrence rule). */
export function chipFor(kind: TokenKind, value: string, env: ChipEnv): ChipModel {
  const label = env.t(`preview.token.${kind}`);
  const flags: string[] = [];
  if (kind === 'recurrence') {
    const rule = parseRecurrence(value.trim());
    const text = rule ? formatRecurrence(rule, env.locale) : value.trim() || label;
    return { kind, text, icon: ICONS[kind], tone: closed(env.taskStatus) ? 'muted' : 'neutral', title: `${label}: ${text}`, flags };
  }
  const date = tokenDateValue(value);
  if (!date) return { kind, text: value, icon: ICONS[kind], tone: 'neutral', title: label, flags };
  const text = formatDateHuman(date, env.locale, env.now);
  let tone: ChipTone = 'neutral';
  const today = toLocalDate(env.now);
  const day = date.slice(0, 10);
  const isClosed = closed(env.taskStatus);
  switch (kind) {
    case 'due':
    case 'scheduled':
    case 'start':
      if (isClosed) tone = 'muted';
      else if (compareLocal(day, today) < 0 && kind !== 'start') {
        tone = kind === 'due' ? 'danger' : 'warning';
        flags.push('is-overdue');
      } else if (day === today) {
        tone = kind === 'due' ? 'warning' : 'info';
        flags.push('is-today');
      }
      break;
    case 'reminder':
    case 'alarm': {
      tone = kind;
      const at = date.length > 10 ? date : `${date}T00:00`;
      if (isClosed || compareLocal(at, toLocalDateTime(env.now)) < 0) flags.push('is-past');
      break;
    }
    case 'done':
      tone = 'success';
      break;
    case 'created':
    case 'cancelled':
      tone = 'muted';
      break;
  }
  const title = flags.includes('is-overdue') ? `${label}: ${text} — ${env.t('preview.token.overdue')}` : `${label}: ${text}`;
  return { kind, text, icon: ICONS[kind], tone, title, flags };
}

/**
 * The longest leading part of a `🔁` value that is a valid rule (the token's text runs to the
 * next token, so `🔁 every week [[Ogród]]` carries trailing text). Length in characters of
 * `value`, or null when no prefix parses.
 */
export function recurrencePrefix(value: string): number | null {
  const words = [...value.matchAll(/\S+/g)].slice(0, 12);
  for (let k = words.length; k > 0; k--) {
    const last = words[k - 1]!;
    const end = last.index + last[0].length;
    if (parseRecurrence(value.slice(0, end).trim())) return end;
  }
  return null;
}

// ---------------------------------------------------------------------------------------------
// Priorities
// ---------------------------------------------------------------------------------------------

const PRIORITY_EMOJI: Record<string, Priority> = {
  '🔺': 'highest',
  '⏫': 'high',
  '🔼': 'medium',
  '🔽': 'low',
  '⏬': 'lowest',
};

export function priorityOf(emoji: string): Priority | null {
  return PRIORITY_EMOJI[emoji.replace(/\uFE0F/g, '')] ?? null;
}

export const PRIORITY_ICON: Record<Priority, PreviewIcon> = {
  highest: 'chevronsUp',
  high: 'chevronUp',
  medium: 'chevronUp',
  low: 'chevronDown',
  lowest: 'chevronsDown',
};
