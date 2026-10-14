/**
 * Pure logic behind <ReminderDialog>: the recurrence picker draft ⇄ RecurrenceRule mapping
 * and the quick-pick times.
 */
import {
  addDaysLocal,
  addMinutesLocal,
  compareLocal,
  formatRecurrence,
  MAX_INTERVAL,
  normalizeRule,
  parseRecurrence,
  toLocalDate,
  toLocalDateTime,
  weekdayOf,
  type RecurrenceRule,
} from '@/core/dates';
import type { LocalDate, LocalDateTime, Locale } from '@/core/types';

export type RepeatKind = 'none' | 'daily' | 'weekdays' | 'weekly' | 'monthly' | 'yearly' | 'custom';
export type RepeatUnit = RecurrenceRule['freq'];

/** UI state of the recurrence picker. */
export interface RepeatDraft {
  kind: RepeatKind;
  /** Weekly / custom-weeks: chosen days (0 = Sunday … 6 = Saturday). Empty = the anchor's weekday. */
  weekdays: number[];
  /** Custom: every `interval` `unit`s. */
  interval: number;
  unit: RepeatUnit;
  /** Kept from an existing rule ("when done"); the picker has no control for it. */
  whenDone?: boolean;
  /** Kept from an existing monthly rule ("on the 15th", "on the last"). */
  monthDay?: number | 'last';
}

export const REPEAT_KINDS: readonly RepeatKind[] = ['none', 'daily', 'weekdays', 'weekly', 'monthly', 'yearly', 'custom'];

export const DEFAULT_REPEAT: RepeatDraft = { kind: 'none', weekdays: [], interval: 2, unit: 'week' };

function clampInterval(n: number): number {
  return Number.isFinite(n) && n >= 1 ? Math.min(Math.floor(n), MAX_INTERVAL) : 1;
}

/**
 * Picker state → rule (null for "none"). `anchor` (the reminder date) fills in the weekday for
 * a weekly rule without chosen days, so "co tydzień" on a Thursday is "every week on Thursday".
 */
export function draftToRule(d: RepeatDraft, anchor?: LocalDate | LocalDateTime | null): RecurrenceRule | null {
  const anchorDay = anchor ? weekdayOf(anchor) : undefined;
  const days = d.weekdays.length > 0 ? [...d.weekdays] : anchorDay !== undefined ? [anchorDay] : undefined;
  let rule: RecurrenceRule;
  switch (d.kind) {
    case 'none':
      return null;
    case 'daily':
      rule = { freq: 'day', interval: 1 };
      break;
    case 'weekdays':
      rule = { freq: 'day', interval: 1, weekdaysOnly: true };
      break;
    case 'weekly':
      rule = { freq: 'week', interval: 1, weekdays: days };
      break;
    case 'monthly':
      rule = { freq: 'month', interval: 1 };
      if (d.monthDay !== undefined) rule.monthDay = d.monthDay;
      break;
    case 'yearly':
      rule = { freq: 'year', interval: 1 };
      break;
    case 'custom': {
      rule = { freq: d.unit, interval: clampInterval(d.interval) };
      if (d.unit === 'week' && days) rule.weekdays = days;
      if (d.unit === 'month' && d.monthDay !== undefined) rule.monthDay = d.monthDay;
      break;
    }
  }
  if (d.whenDone) rule.whenDone = true;
  return normalizeRule(rule);
}

/** Rule (or rule text in any supported phrasing) → picker state. */
export function ruleToDraft(input: RecurrenceRule | string | null | undefined): RepeatDraft {
  const rule = typeof input === 'string' ? parseRecurrence(input) : input;
  if (!rule) return { ...DEFAULT_REPEAT };
  const r = normalizeRule(rule);
  const base: RepeatDraft = { ...DEFAULT_REPEAT, interval: r.interval, unit: r.freq };
  if (r.whenDone) base.whenDone = true;
  if (r.monthDay !== undefined) base.monthDay = r.monthDay;
  if (r.freq === 'day' && r.interval === 1) return { ...base, kind: r.weekdaysOnly ? 'weekdays' : 'daily' };
  if (r.freq === 'week') {
    const weekdays = r.weekdays ?? (r.weekdaysOnly ? [1, 2, 3, 4, 5] : []);
    return { ...base, kind: r.interval === 1 ? 'weekly' : 'custom', weekdays };
  }
  if (r.freq === 'month' && r.interval === 1) return { ...base, kind: 'monthly' };
  if (r.freq === 'year' && r.interval === 1) return { ...base, kind: 'yearly' };
  // Business days every n days has no picker control: keep it as plain "every n days".
  return { ...base, kind: 'custom' };
}

/** Canonical file text (English) and the UI label for a draft; null for "none". */
export function describeDraft(
  d: RepeatDraft,
  anchor: LocalDate | LocalDateTime | null | undefined,
  locale: Locale,
): { rule: RecurrenceRule; canonical: string; label: string } | null {
  const rule = draftToRule(d, anchor);
  if (!rule) return null;
  return { rule, canonical: formatRecurrence(rule, 'en'), label: formatRecurrence(rule, locale) };
}

/** Toggle a weekday in a list, keeping Monday-first order. */
export function toggleWeekday(days: readonly number[], day: number): number[] {
  const set = new Set(days);
  if (set.has(day)) set.delete(day);
  else set.add(day);
  return [...set].sort((a, b) => ((a + 6) % 7) - ((b + 6) % 7));
}

export type QuickPickId = 'in10' | 'in60' | 'tonight' | 'tomorrowMorning' | 'monday';

export interface QuickPick {
  id: QuickPickId;
  at: LocalDateTime;
  /** In the past (e.g. "tonight 19:00" after 19:00). */
  disabled: boolean;
}

/**
 * Quick reminder times relative to `now`: in 10 min, in an hour, tonight 19:00, tomorrow
 * 8:00 and next Monday 9:00 (the Monday after today; on Sunday that is tomorrow).
 */
export function quickPicks(now: Date): QuickPick[] {
  const nowDt = toLocalDateTime(now);
  const today = toLocalDate(now);
  const daysToMonday = ((1 - weekdayOf(today) + 7) % 7) || 7;
  const tonight = `${today}T19:00`;
  return [
    { id: 'in10', at: addMinutesLocal(nowDt, 10), disabled: false },
    { id: 'in60', at: addMinutesLocal(nowDt, 60), disabled: false },
    { id: 'tonight', at: tonight, disabled: compareLocal(tonight, nowDt) <= 0 },
    { id: 'tomorrowMorning', at: `${addDaysLocal(today, 1)}T08:00`, disabled: false },
    { id: 'monday', at: `${addDaysLocal(today, daysToMonday)}T09:00`, disabled: false },
  ];
}

/** Result of the dialog. `recurrence` is canonical English rule text (written into the file). */
export interface ReminderValue {
  at: LocalDateTime;
  alarm: boolean;
  recurrence?: string;
}
