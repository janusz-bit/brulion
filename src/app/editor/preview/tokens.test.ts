import { describe, expect, it } from 'vitest';
import { createTranslator } from '@/core/i18n';
import { chipFor, fileDateValue, priorityOf, recurrencePrefix, tokenDateValue, tokenKind, type ChipEnv } from './tokens';

const NOW = new Date(2026, 9, 14, 10, 30); // śr 14 paź 2026, 10:30
const pl: ChipEnv = { now: NOW, locale: 'pl', t: createTranslator('pl') };
const en: ChipEnv = { now: NOW, locale: 'en', t: createTranslator('en') };

describe('token kinds and values', () => {
  it('maps marks to kinds (with and without variation selectors)', () => {
    expect(tokenKind('📅')).toBe('due');
    expect(tokenKind('⏳')).toBe('scheduled');
    expect(tokenKind('🛫')).toBe('start');
    expect(tokenKind('✅')).toBe('done');
    expect(tokenKind('❌')).toBe('cancelled');
    expect(tokenKind('➕')).toBe('created');
    expect(tokenKind('🔁')).toBe('recurrence');
    expect(tokenKind('⏰')).toBe('reminder');
    expect(tokenKind('⏰️')).toBe('reminder');
    expect(tokenKind('⏰!')).toBe('alarm');
    expect(tokenKind('x')).toBeNull();
  });

  it('converts between file values and LocalDate(Time)', () => {
    expect(tokenDateValue('2026-10-15')).toBe('2026-10-15');
    expect(tokenDateValue('2026-10-15 9:05')).toBe('2026-10-15T09:05');
    expect(tokenDateValue('2026-10-15\t18:30')).toBe('2026-10-15T18:30');
    expect(tokenDateValue('15.10.2026')).toBeNull();
    expect(fileDateValue('2026-10-15T09:05')).toBe('2026-10-15 09:05');
    expect(fileDateValue('2026-10-15')).toBe('2026-10-15');
  });

  it('reads priorities', () => {
    expect(priorityOf('🔺')).toBe('highest');
    expect(priorityOf('⏫')).toBe('high');
    expect(priorityOf('🔼️')).toBe('medium');
    expect(priorityOf('🔽')).toBe('low');
    expect(priorityOf('⏬')).toBe('lowest');
    expect(priorityOf('a')).toBeNull();
  });
});

describe('chipFor', () => {
  it('formats dates the human way (Polish)', () => {
    expect(chipFor('due', '2026-10-17', pl).text).toBe('sob 17 paź');
    expect(chipFor('due', '2026-10-15', pl).text).toBe('jutro');
    expect(chipFor('scheduled', '2026-10-14', pl)).toMatchObject({ text: 'dziś', tone: 'info', flags: ['is-today'] });
    expect(chipFor('reminder', '2026-10-14 16:00', pl).text).toBe('dziś 16:00');
    expect(chipFor('due', '2027-01-05', pl).text).toBe('5 sty 2027');
  });

  it('marks overdue due dates in the danger tone, unless the task is closed', () => {
    const c = chipFor('due', '2026-10-12', pl);
    expect(c).toMatchObject({ tone: 'danger', flags: ['is-overdue'], icon: 'calendar' });
    expect(c.title).toContain('po terminie');
    expect(chipFor('due', '2026-10-12', { ...pl, taskStatus: 'done' }).tone).toBe('muted');
    expect(chipFor('due', '2026-10-14', pl).tone).toBe('warning');
    expect(chipFor('scheduled', '2026-10-01', pl).tone).toBe('warning');
    expect(chipFor('start', '2026-10-01', pl).tone).toBe('neutral');
  });

  it('reminders are amber, alarms carry a bell; past ones are flagged', () => {
    expect(chipFor('reminder', '2026-10-15 09:00', pl)).toMatchObject({ tone: 'reminder', icon: 'alarm', flags: [] });
    expect(chipFor('alarm', '2026-10-15 06:30', pl)).toMatchObject({ tone: 'alarm', icon: 'bell' });
    expect(chipFor('reminder', '2026-10-14 09:00', pl).flags).toContain('is-past');
    expect(chipFor('reminder', '2026-10-15 09:00', { ...pl, taskStatus: 'cancelled' }).flags).toContain('is-past');
  });

  it('shows recurrence rules in the UI language', () => {
    expect(chipFor('recurrence', 'every week', pl).text).toBe('co tydzień');
    expect(chipFor('recurrence', 'co 2 tygodnie', pl).text).toBe('co 2 tygodnie');
    expect(chipFor('recurrence', 'every week', en).text).toBe('every week');
    expect(chipFor('recurrence', 'jakoś czasem', pl).text).toBe('jakoś czasem');
  });

  it('done / created / cancelled chips', () => {
    expect(chipFor('done', '2026-10-14', pl)).toMatchObject({ tone: 'success', icon: 'check', text: 'dziś' });
    expect(chipFor('created', '2026-10-01', pl).tone).toBe('muted');
    expect(chipFor('cancelled', '2026-10-13', en)).toMatchObject({ text: 'yesterday', icon: 'ban' });
  });

  it('keeps malformed values as text', () => {
    expect(chipFor('due', 'kiedyś', pl).text).toBe('kiedyś');
  });
});

describe('recurrencePrefix', () => {
  it('finds the rule when text follows it', () => {
    const v = 'every week [[Ogród]] i coś';
    expect(v.slice(0, recurrencePrefix(v)!)).toBe('every week');
    expect(recurrencePrefix('every week on Monday, Friday')).toBe('every week on Monday, Friday'.length);
    expect(recurrencePrefix('co miesiąc 15.')).toBe('co miesiąc 15.'.length);
    expect(recurrencePrefix('nie reguła')).toBeNull();
  });
});
