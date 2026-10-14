import { describe, expect, it } from 'vitest';
import { DEFAULT_VAULT_CONFIG } from '../types';
import * as md from './index';

describe('@/core/markdown public API (docs/ARCHITECTURE.md)', () => {
  it('exports every contract function', () => {
    const names = [
      'parseNote',
      'parseFrontmatter',
      'stringifyFrontmatter',
      'extractInline',
      'stripTokens',
      'formatReminderToken',
      'setTaskStatus',
      'toggleTask',
      'ensureBlockId',
      'generateBlockId',
      'renameLinksInContent',
      'journalPathForDate',
      'journalDateFromPath',
      'applyTemplate',
      'listItemRange',
    ] as const;
    for (const n of names) expect(typeof md[n], n).toBe('function');
    expect(md.TOKENS.due).toBe('📅');
  });

  it('works end to end: template -> journal -> tasks -> rename', () => {
    const now = new Date(2026, 9, 14, 8, 0);
    const date = '2026-10-14';
    const path = md.journalPathForDate(date, DEFAULT_VAULT_CONFIG);
    const tpl = md.applyTemplate('# {{date:EEEE}}\n- [ ] {{cursor}}Przegląd [[Projekt]] ⏰ {{date}} 09:30', {
      now,
      title: 'x',
      locale: 'pl',
    });
    expect(tpl.text).toBe('# środa\n- [ ] Przegląd [[Projekt]] ⏰ 2026-10-14 09:30');
    const note = md.parseNote(path, tpl.text);
    expect(note.journalDate).toBe(date);
    expect(note.reminders[0]).toMatchObject({ at: '2026-10-14T09:30', text: 'Przegląd [[Projekt]]', line: 1 });
    const done = md.toggleTask(tpl.text, 1, date);
    expect(md.parseNote(path, done).tasks[0]).toMatchObject({ status: 'done', doneDate: date });
    expect(md.renameLinksInContent(done, 'Projekt', 'Remont')).toContain('[[Remont]]');
    expect(md.stripTokens('Przegląd ⏰ 2026-10-14 09:30')).toBe('Przegląd');
  });
});
