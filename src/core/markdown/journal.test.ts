import { describe, expect, it } from 'vitest';
import { DEFAULT_VAULT_CONFIG, type VaultConfig } from '../types';
import { journalDateFromPath, journalPathForDate } from './journal';

const cfg = (over: Partial<VaultConfig>): VaultConfig => ({ ...DEFAULT_VAULT_CONFIG, ...over });

describe('journalPathForDate', () => {
  it('uses the default folder and format', () => {
    expect(journalPathForDate('2026-10-14', DEFAULT_VAULT_CONFIG)).toBe('journals/2026-10-14.md');
  });

  it('supports custom formats, nested folders and an empty folder', () => {
    expect(journalPathForDate('2026-10-14', cfg({ journalFileFormat: 'yyyy_MM_dd' }))).toBe('journals/2026_10_14.md');
    expect(journalPathForDate('2026-01-05', cfg({ journalFileFormat: 'yyyy/MM/yyyy-MM-dd', journalsFolder: 'Dziennik/' }))).toBe(
      'Dziennik/2026/01/2026-01-05.md',
    );
    expect(journalPathForDate('2026-10-14', cfg({ journalsFolder: '' }))).toBe('2026-10-14.md');
  });

  it('converts Moment.js formats (Obsidian settings)', () => {
    expect(journalPathForDate('2026-10-04', cfg({ journalFileFormat: 'YYYY-MM-DD' }))).toBe('journals/2026-10-04.md');
    expect(journalPathForDate('2026-10-04', cfg({ journalFileFormat: 'D MMM YYYY' }))).toBe('journals/4 Oct 2026.md');
  });

  it('falls back to yyyy-MM-dd for invalid formats', () => {
    expect(journalPathForDate('2026-10-14', cfg({ journalFileFormat: 'yyyy-MM-dd jjj' }))).toBe('journals/2026-10-14.md');
    expect(journalPathForDate('2026-10-14', cfg({ journalFileFormat: '' }))).toBe('journals/2026-10-14.md');
  });
});

describe('journalDateFromPath', () => {
  it('recognises journal pages in the journals folder', () => {
    expect(journalDateFromPath('journals/2026-10-14.md', DEFAULT_VAULT_CONFIG)).toBe('2026-10-14');
    expect(journalDateFromPath('Journals/2026-10-14.md', DEFAULT_VAULT_CONFIG)).toBe('2026-10-14');
    expect(journalDateFromPath('journals/2026_10_14.md', DEFAULT_VAULT_CONFIG)).toBe('2026-10-14');
  });

  it('round-trips with journalPathForDate for many formats', () => {
    const formats = ['yyyy-MM-dd', 'yyyy_MM_dd', 'dd.MM.yyyy', 'yyyy/MM/dd', 'YYYY-MM-DD', 'MMM do, yyyy'];
    for (const f of formats) {
      const c = cfg({ journalFileFormat: f });
      for (const d of ['2026-01-01', '2026-02-28', '2028-02-29', '2026-12-31']) {
        expect(journalDateFromPath(journalPathForDate(d, c), c)).toBe(d);
      }
    }
  });

  it('rejects other folders, non-markdown files, invalid and lenient dates', () => {
    expect(journalDateFromPath('pages/2026-10-14.md', DEFAULT_VAULT_CONFIG)).toBeNull();
    expect(journalDateFromPath('journals/2026-10-14.png', DEFAULT_VAULT_CONFIG)).toBeNull();
    expect(journalDateFromPath('journals/2026-02-30.md', DEFAULT_VAULT_CONFIG)).toBeNull();
    expect(journalDateFromPath('journals/2026-1-5.md', DEFAULT_VAULT_CONFIG)).toBeNull();
    expect(journalDateFromPath('journals/Notatki.md', DEFAULT_VAULT_CONFIG)).toBeNull();
    expect(journalDateFromPath('journals/sub/2026-10-14.md', DEFAULT_VAULT_CONFIG)).toBeNull();
    expect(journalDateFromPath('journals2026-10-14.md', DEFAULT_VAULT_CONFIG)).toBeNull();
  });

  it('works with the vault root as journals folder', () => {
    const c = cfg({ journalsFolder: '' });
    expect(journalDateFromPath('2026-10-14.md', c)).toBe('2026-10-14');
    expect(journalDateFromPath('Projekt.md', c)).toBeNull();
  });
});

describe('journal formats with repeated tokens and localised names (review regressions)', () => {
  it('round-trips nested folder formats that repeat date tokens', () => {
    for (const f of ['yyyy/MM/yyyy-MM-dd', 'YYYY/MM/YYYY-MM-DD', 'YYYY/MM-MMMM/YYYY-MM-DD-dddd', 'yyyy/yyyy-MM-dd EEEE']) {
      const c = cfg({ journalFileFormat: f });
      for (const d of ['2026-01-05', '2026-10-14', '2028-02-29', '2026-12-31']) {
        const p = journalPathForDate(d, c);
        expect(journalDateFromPath(p, c), `${f} ${p}`).toBe(d);
      }
    }
  });

  it('rejects nested paths whose parts disagree', () => {
    const c = cfg({ journalFileFormat: 'yyyy/MM/yyyy-MM-dd' });
    expect(journalDateFromPath('journals/2025/10/2026-10-14.md', c)).toBeNull();
    expect(journalDateFromPath('journals/2026/11/2026-10-14.md', c)).toBeNull();
  });

  it('recognises weekday / month names written in Polish', () => {
    const c = cfg({ journalFileFormat: 'yyyy-MM-dd EEEE' });
    expect(journalDateFromPath('journals/2026-10-14 środa.md', c)).toBe('2026-10-14');
    expect(journalDateFromPath('journals/2026-10-14 Wednesday.md', c)).toBe('2026-10-14');
    expect(journalDateFromPath('journals/2026-10-14 czwartek.md', c)).toBeNull();
  });

  it('falls back to the default journals folder when the key is missing', () => {
    const partial = { ...DEFAULT_VAULT_CONFIG } as Partial<VaultConfig>;
    delete partial.journalsFolder;
    expect(journalPathForDate('2026-10-14', partial as VaultConfig)).toBe('journals/2026-10-14.md');
    expect(journalDateFromPath('journals/2026-10-14.md', partial as VaultConfig)).toBe('2026-10-14');
    expect(journalDateFromPath('2026-10-14.md', partial as VaultConfig)).toBeNull();
  });
});
