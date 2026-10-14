import { describe, expect, it } from 'vitest';
import { parseCalloutHead, quotePrefixLength } from './callouts';

describe('parseCalloutHead', () => {
  it('reads type, fold and title', () => {
    expect(parseCalloutHead('> [!tip] Cel projektu')).toEqual({ type: 'tip', name: 'tip', fold: '', title: 'Cel projektu', quoteFrom: 0, titleFrom: 9 });
    expect(parseCalloutHead('> [!warning]- Uwaga')).toMatchObject({ type: 'warning', fold: '-', title: 'Uwaga' });
    expect(parseCalloutHead('>[!NOTE]+')).toMatchObject({ type: 'note', fold: '+', title: '' });
  });

  it('maps Obsidian aliases onto the seven looks', () => {
    expect(parseCalloutHead('> [!hint]')?.type).toBe('tip');
    expect(parseCalloutHead('> [!error] Błąd')?.type).toBe('danger');
    expect(parseCalloutHead('> [!faq]')?.type).toBe('info');
    expect(parseCalloutHead('> [!cite]')?.type).toBe('quote');
    expect(parseCalloutHead('> [!custom] Zażółć gęślą jaźń')).toMatchObject({ type: 'note', name: 'custom', title: 'Zażółć gęślą jaźń' });
  });

  it('handles indentation (callouts in list items)', () => {
    const h = parseCalloutHead('\t> [!info] W liście')!;
    expect(h.quoteFrom).toBe(1);
    expect('\t> [!info] W liście'.slice(h.titleFrom)).toBe('W liście');
  });

  it('rejects ordinary quotes', () => {
    expect(parseCalloutHead('> zwykły cytat')).toBeNull();
    expect(parseCalloutHead('[!note] bez cytatu')).toBeNull();
  });
});

describe('quotePrefixLength', () => {
  it('measures single and nested quote marks', () => {
    expect(quotePrefixLength('> tekst')).toBe(2);
    expect(quotePrefixLength('>tekst')).toBe(1);
    expect(quotePrefixLength('> > zagnieżdżony')).toBe(4);
    expect(quotePrefixLength('tekst')).toBe(0);
  });
});
