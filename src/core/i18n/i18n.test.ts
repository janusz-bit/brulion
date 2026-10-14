import { describe, expect, it } from 'vitest';
import { createTranslator, missingKeys, plural } from './index';

describe('i18n', () => {
  it('translates with Polish plurals', () => {
    const t = createTranslator('pl');
    expect(t('common.notes', { count: 1 })).toBe('1 notatka');
    expect(t('common.notes', { count: 3 })).toBe('3 notatki');
    expect(t('common.notes', { count: 5 })).toBe('5 notatek');
    expect(t('common.notes', { count: 22 })).toBe('22 notatki');
  });
  it('translates English', () => {
    const t = createTranslator('en');
    expect(t('common.notes', { count: 1 })).toBe('1 note');
    expect(t('common.notes', { count: 2 })).toBe('2 notes');
  });
  it('falls back to key', () => {
    expect(createTranslator('pl')('nope.key')).toBe('nope.key');
  });
  it('plural helper', () => {
    expect(plural('pl', 12, { one: '# dzień', few: '# dni', many: '# dni' })).toBe('12 dni');
  });
  it('has no missing keys between languages', () => {
    expect(missingKeys()).toEqual({ pl: [], en: [] });
  });
});
