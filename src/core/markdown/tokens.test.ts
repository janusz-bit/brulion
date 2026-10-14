import { describe, expect, it } from 'vitest';
import {
  TOKENS,
  blockIdAtEnd,
  findTokens,
  formatReminderToken,
  isStandaloneBlockId,
  parseLogseqTimestamp,
  recurrenceOf,
  removeRanges,
  stripTokens,
} from './tokens';

const kinds = (s: string): string[] => findTokens(s).map((t) => t.kind);

describe('TOKENS', () => {
  it('exposes the Obsidian Tasks signifiers', () => {
    expect(TOKENS.due).toBe('📅');
    expect(TOKENS.scheduled).toBe('⏳');
    expect(TOKENS.start).toBe('🛫');
    expect(TOKENS.created).toBe('➕');
    expect(TOKENS.done).toBe('✅');
    expect(TOKENS.cancelled).toBe('❌');
    expect(TOKENS.recurrence).toBe('🔁');
    expect(TOKENS.reminder).toBe('⏰');
    expect(TOKENS.alarm).toBe('⏰!');
    expect(TOKENS.priority).toEqual({ highest: '🔺', high: '⏫', medium: '🔼', low: '🔽', lowest: '⏬' });
  });
});

describe('findTokens — dates', () => {
  it('reads every date signifier', () => {
    const toks = findTokens('x 📅 2026-10-15 ⏳ 2026-10-14 🛫 2026-10-13 ➕ 2026-10-01 ✅ 2026-10-16 ❌ 2026-10-17');
    expect(toks.map((t) => [t.kind, t.value])).toEqual([
      ['due', '2026-10-15'],
      ['scheduled', '2026-10-14'],
      ['start', '2026-10-13'],
      ['created', '2026-10-01'],
      ['done', '2026-10-16'],
      ['cancelled', '2026-10-17'],
    ]);
  });

  it('accepts an optional time after a date (also T separator and H:mm)', () => {
    const [a, b, c] = findTokens('📅 2026-10-15 10:00 ⏳ 2026-10-15T07:05 🛫 2026-10-15 9:30');
    expect(a).toMatchObject({ kind: 'due', value: '2026-10-15', time: '10:00' });
    expect(b).toMatchObject({ kind: 'scheduled', time: '07:05' });
    expect(c).toMatchObject({ kind: 'start', time: '09:30' });
  });

  it('accepts emoji variants, variation selectors and no space', () => {
    expect(kinds('📆 2026-01-01 🗓️ 2026-01-02 ⌛2026-01-03 ⏳️ 2026-01-04')).toEqual(['due', 'due', 'scheduled', 'scheduled']);
  });

  it('rejects impossible dates and keeps invalid times out of the token', () => {
    expect(findTokens('📅 2026-02-30')).toEqual([]);
    const [t] = findTokens('📅 2026-10-15 25:00 rest');
    expect(t).toMatchObject({ value: '2026-10-15', from: 0 });
    expect(t!.time).toBeUndefined();
    expect('📅 2026-10-15 25:00 rest'.slice(t!.to)).toBe(' 25:00 rest');
  });

  it('records the value range for rewriting', () => {
    const s = 'a 📅 2026-10-15 10:00 b';
    const [t] = findTokens(s);
    expect(s.slice(t!.valueFrom, t!.valueTo)).toBe('2026-10-15 10:00');
    expect(s.slice(t!.from, t!.to)).toBe('📅 2026-10-15 10:00');
  });
});

describe('findTokens — reminders', () => {
  it('reads ⏰ reminders and ⏰! alarms with optional time', () => {
    const [a, b, c] = findTokens('⏰ 2026-10-15 09:00 ⏰! 2026-10-15 06:30 ⏰ 2026-10-16');
    expect(a).toMatchObject({ kind: 'reminder', value: '2026-10-15', time: '09:00', alarm: false });
    expect(b).toMatchObject({ kind: 'reminder', time: '06:30', alarm: true });
    expect(c).toMatchObject({ kind: 'reminder', value: '2026-10-16' });
    expect(c!.time).toBeUndefined();
  });

  it('reads obsidian-reminder (@date) syntax', () => {
    const s = 'call (@2026-10-15 09:00) and (@2026-10-16)';
    const [a, b] = findTokens(s);
    expect(a).toMatchObject({ kind: 'reminderCompat', value: '2026-10-15', time: '09:00' });
    expect(s.slice(a!.from, a!.to)).toBe('(@2026-10-15 09:00)');
    expect(s.slice(a!.valueFrom, a!.valueTo)).toBe('2026-10-15 09:00');
    expect(b).toMatchObject({ kind: 'reminderCompat', value: '2026-10-16' });
    expect(findTokens('(@2026-10-15 99:00)')).toEqual([]);
  });
});

describe('findTokens — recurrence, priority and others', () => {
  it('reads the recurrence text up to the next token', () => {
    const [r, d] = findTokens('task 🔁 every week on Monday 📅 2026-10-19');
    expect(r).toMatchObject({ kind: 'recurrence', value: 'every week on Monday', canonical: 'every week on Monday' });
    expect(d!.kind).toBe('due');
  });

  it('canonicalises Polish rules and keeps unknown text raw', () => {
    expect(recurrenceOf(findTokens('🔁 co tydzień w poniedziałek, piątek')[0]!)).toBe('every week on Monday, Friday');
    expect(recurrenceOf(findTokens('🔁 co 2 tygodnie po wykonaniu')[0]!)).toBe('every 2 weeks when done');
    const raw = findTokens('🔁 every blue moon')[0]!;
    expect(raw.canonical).toBeUndefined();
    expect(recurrenceOf(raw)).toBe('every blue moon');
    expect(recurrenceOf(findTokens('🔁')[0]!)).toBeUndefined();
  });

  it('stops the rule before tags, block ids and line breaks', () => {
    expect(findTokens('🔁 every day #praca')[0]!.value).toBe('every day');
    expect(findTokens('🔁 every day ^abc123')[0]!.value).toBe('every day');
    expect(findTokens('🔁 every day\nnext line')[0]!.value).toBe('every day');
  });

  it('reads priority emojis and Logseq priorities', () => {
    expect(findTokens('🔺 ⏫ 🔼 🔽 ⏬').map((t) => t.priority)).toEqual(['highest', 'high', 'medium', 'low', 'lowest']);
    expect(findTokens('TODO [#A] x [#C]').map((t) => [t.kind, t.priority])).toEqual([
      ['logseqPriority', 'high'],
      ['logseqPriority', 'low'],
    ]);
  });

  it('recognises Obsidian Tasks id / depends-on / on-completion tokens', () => {
    expect(kinds('🆔 abc123 ⛔ x1,y2 🏁 delete')).toEqual(['taskId', 'dependsOn', 'onCompletion']);
  });

  it('reads Logseq planning items', () => {
    const [s, d] = findTokens('SCHEDULED: <2026-10-15 Thu 10:00 .+1w> DEADLINE: <2026-10-20 Tue>');
    expect(s).toMatchObject({ kind: 'planning', field: 'scheduled', value: '2026-10-15', time: '10:00', repeater: '.+1w' });
    expect(d).toMatchObject({ kind: 'planning', field: 'due', value: '2026-10-20' });
    expect(d!.repeater).toBeUndefined();
  });

  it('returns nothing for plain text quickly', () => {
    expect(findTokens('zwykły tekst bez tokenów')).toEqual([]);
  });
});

describe('parseLogseqTimestamp', () => {
  it('parses date, weekday, time ranges and repeaters', () => {
    expect(parseLogseqTimestamp('2026-10-15 Thu')).toEqual({ date: '2026-10-15' });
    expect(parseLogseqTimestamp('2026-10-15 czw 9:00-10:00 ++2d')).toEqual({ date: '2026-10-15', time: '09:00', repeater: '++2d' });
    expect(parseLogseqTimestamp('nonsense')).toBeNull();
    expect(parseLogseqTimestamp('2026-13-01')).toBeNull();
  });
});

describe('block ids', () => {
  it('finds an id at the end of a line', () => {
    expect(blockIdAtEnd('text ^abc-123')).toEqual({ id: 'abc-123', from: 4, to: 13 });
    expect(blockIdAtEnd('^solo')).toEqual({ id: 'solo', from: 0, to: 5 });
    expect(blockIdAtEnd('x^2')).toBeNull();
    expect(blockIdAtEnd('text ^id more')).toBeNull();
  });

  it('detects standalone id lines', () => {
    expect(isStandaloneBlockId('  ^tbl1  ')).toBe('tbl1');
    expect(isStandaloneBlockId('a ^tbl1')).toBeNull();
  });
});

describe('removeRanges', () => {
  it('drops the whitespace that separated a removed range', () => {
    expect(removeRanges('a XX b', [[2, 4]])).toBe('a b');
    expect(removeRanges('XX b', [[0, 2]])).toBe('b');
    expect(removeRanges('a XX', [[2, 4]])).toBe('a');
    expect(removeRanges('a XX YY b', [[5, 7], [2, 4]])).toBe('a b');
    expect(removeRanges('abc', [])).toBe('abc');
  });
});

describe('stripTokens', () => {
  it('removes all metadata tokens and keeps inline markdown', () => {
    expect(stripTokens('Kup **mleko** 📅 2026-10-15 ⏫ 🔁 every week ⏰ 2026-10-15 09:00 ^abc')).toBe('Kup **mleko**');
    expect(stripTokens('TODO [#A] x SCHEDULED: <2026-10-15 Thu>')).toBe('TODO x');
    expect(stripTokens('(@2026-10-15 09:00) zadzwoń do mamy')).toBe('zadzwoń do mamy');
  });

  it('keeps tokens inside inline code', () => {
    expect(stripTokens('use `📅 2026-10-15` syntax 📅 2026-10-16')).toBe('use `📅 2026-10-15` syntax');
  });

  it('handles multi-line text', () => {
    expect(stripTokens('a ✅ 2026-01-01\r\nb ^id')).toBe('a\r\nb');
  });
});

describe('formatReminderToken', () => {
  it('writes file form with a space separator', () => {
    expect(formatReminderToken('2026-10-15T09:00')).toBe('⏰ 2026-10-15 09:00');
    expect(formatReminderToken('2026-10-15T06:30', true)).toBe('⏰! 2026-10-15 06:30');
    expect(formatReminderToken('2026-10-15')).toBe('⏰ 2026-10-15');
  });

  it('round-trips through findTokens', () => {
    const [t] = findTokens(formatReminderToken('2026-12-24T18:45', true));
    expect(t).toMatchObject({ kind: 'reminder', value: '2026-12-24', time: '18:45', alarm: true });
  });
});
