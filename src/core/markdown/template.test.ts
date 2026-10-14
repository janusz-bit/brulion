import { describe, expect, it } from 'vitest';
import { applyTemplate } from './template';
import { formatDate, isMomentFormat, momentToDateFns, parseDateStrict, toDateFnsFormat } from './dateformat';

// Wednesday, 14 October 2026, 09:05 local time.
const now = new Date(2026, 9, 14, 9, 5);

describe('applyTemplate', () => {
  it('fills date, time, title and weekday', () => {
    const r = applyTemplate('# {{title}}\n{{date}} {{time}} ({{weekday}})', { now, title: 'Dziennik', locale: 'pl' });
    expect(r).toEqual({ text: '# Dziennik\n2026-10-14 09:05 (środa)', cursor: null });
    expect(applyTemplate('{{weekday}}', { now, title: '', locale: 'en' }).text).toBe('Wednesday');
  });

  it('formats {{date:FORMAT}} with Polish names for locale pl', () => {
    expect(applyTemplate('{{date:EEEE, d MMMM yyyy}}', { now, title: '', locale: 'pl' }).text).toBe('środa, 14 października 2026');
    expect(applyTemplate('{{date:EEEE, d MMMM yyyy}}', { now, title: '', locale: 'en' }).text).toBe('Wednesday, 14 October 2026');
    expect(applyTemplate('{{time:HH:mm:ss}}', { now, title: '', locale: 'pl' }).text).toBe('09:05:00');
  });

  it('accepts Moment.js formats from Obsidian templates', () => {
    expect(applyTemplate('{{date:YYYY-MM-DD}} {{date:dddd D MMMM}}', { now, title: '', locale: 'pl' }).text).toBe(
      '2026-10-14 środa 14 października',
    );
  });

  it('removes {{cursor}} and returns its offset', () => {
    const r = applyTemplate('# {{title}}\n\n- {{cursor}}\n{{cursor}}end', { now, title: 'Żółw', locale: 'pl' });
    expect(r.text).toBe('# Żółw\n\n- \nend');
    expect(r.cursor).toBe('# Żółw\n\n- '.length);
  });

  it('is tolerant: spaces, case, unknown variables and invalid formats stay as written', () => {
    const r = applyTemplate('{{ Title }} {{unknown}} {{date:bad format}} {{date:}} {{', { now, title: 'T', locale: 'pl' });
    expect(r.text).toBe('T {{unknown}} {{date:bad format}} 2026-10-14 {{');
  });

  it('does not re-expand variables inside the title', () => {
    expect(applyTemplate('{{title}}', { now, title: '{{date}}', locale: 'pl' }).text).toBe('{{date}}');
  });
});

describe('date formats', () => {
  it('detects and converts Moment.js formats', () => {
    expect(isMomentFormat('YYYY-MM-DD')).toBe(true);
    expect(isMomentFormat('yyyy-MM-dd')).toBe(false);
    expect(isMomentFormat("yyyy 'Y'")).toBe(false);
    expect(momentToDateFns('YYYY-MM-DD[T]HH:mm')).toBe("yyyy-MM-dd'T'HH:mm");
    expect(momentToDateFns('dddd, Do MMMM')).toBe('EEEE, do MMMM');
    expect(momentToDateFns("D MMM 'YY")).toBe("d MMM ''yy");
    expect(toDateFnsFormat('yyyy_MM_dd')).toBe('yyyy_MM_dd');
  });

  it('formatDate returns null for invalid input', () => {
    expect(formatDate(new Date(Number.NaN), 'yyyy')).toBeNull();
    expect(formatDate(now, 'yyyy-MM-dd hello')).toBeNull();
    expect(formatDate(now, 'LLLL', 'pl')).toBe('październik');
  });

  it('parseDateStrict requires an exact round trip', () => {
    expect(parseDateStrict('2026-10-14', 'yyyy-MM-dd')?.getDate()).toBe(14);
    expect(parseDateStrict('2026-10-4', 'yyyy-MM-dd')).toBeNull();
    expect(parseDateStrict('2026-02-30', 'yyyy-MM-dd')).toBeNull();
    expect(parseDateStrict('x', 'not a [valid')).toBeNull();
  });
});

describe('Moment.js weekday tokens (review regressions)', () => {
  it('treats ddd / dddd as Moment.js weekday names (date-fns has no such tokens)', () => {
    expect(applyTemplate('{{date:dddd}}', { now, title: '', locale: 'pl' }).text).toBe('środa');
    expect(applyTemplate('{{date:dddd}}', { now, title: '', locale: 'en' }).text).toBe('Wednesday');
    expect(applyTemplate('{{date:ddd}}', { now, title: '', locale: 'en' }).text).toBe('Wed');
    expect(isMomentFormat('dddd')).toBe(true);
    expect(isMomentFormat('dd.MM.yyyy')).toBe(false);
    expect(isMomentFormat('yyyy-MM-dd dddd')).toBe(false);
    expect(isMomentFormat('EEEE')).toBe(false);
  });

  it('treats an AM/PM marker without date-fns years as Moment.js', () => {
    expect(applyTemplate('{{time:h:mm A}}', { now, title: '', locale: 'en' }).text).toBe('9:05 AM');
    expect(isMomentFormat("yyyy 'A'")).toBe(false);
  });

  it('parses nested formats that repeat a unit (date-fns refuses them)', () => {
    expect(parseDateStrict('2026/10/2026-10-14', 'yyyy/MM/yyyy-MM-dd')?.getDate()).toBe(14);
    expect(parseDateStrict('2026/09/2026-10-14', 'yyyy/MM/yyyy-MM-dd')).toBeNull();
    expect(parseDateStrict("2026/x/2026-10-14", "yyyy/'x'/yyyy-MM-dd")?.getMonth()).toBe(9);
  });
});
