import { foldChar, foldString, fuzzyFilter, fuzzyMatch, highlightSegments } from './fuzzy';

describe('fold', () => {
  it('folds Polish diacritics one character at a time', () => {
    expect(foldString('Zażółć Gęślą Jaźń ŁÓDŹ')).toBe('zazolc gesla jazn lodz');
    expect(foldChar('Ł')).toBe('l');
    expect(foldString('ąćęłńóśźż').length).toBe(9);
  });
});

describe('fuzzyMatch', () => {
  it('matches case- and diacritic-insensitively', () => {
    expect(fuzzyMatch('zrodlo', 'Źródło wiedzy')).not.toBeNull();
    expect(fuzzyMatch('LODZ', 'Wycieczka do Łodzi')).not.toBeNull();
    expect(fuzzyMatch('xyz', 'Remont mieszkania')).toBeNull();
  });

  it('returns the matched indices for highlighting', () => {
    expect(fuzzyMatch('rem', 'Remont')!.indices).toEqual([0, 1, 2]);
    expect(fuzzyMatch('rm', 'Remont mieszkania')!.indices).toEqual([0, 7]);
  });

  it('requires every word of the query (any order)', () => {
    expect(fuzzyMatch('mieszk rem', 'Remont mieszkania')).not.toBeNull();
    expect(fuzzyMatch('rem kuchni', 'Remont mieszkania')).toBeNull();
  });

  it('ranks prefix and word-start matches above scattered ones', () => {
    const prefix = fuzzyMatch('dzi', 'Dziennik')!.score;
    const word = fuzzyMatch('dzi', 'Mój dziennik')!.score;
    const scattered = fuzzyMatch('dzi', 'Odpowiedzialność')!.score;
    expect(prefix).toBeGreaterThan(word);
    expect(word).toBeGreaterThan(scattered);
  });

  it('matches everything for an empty query', () => {
    expect(fuzzyMatch('  ', 'cokolwiek')).toEqual({ score: 0, indices: [] });
  });
});

describe('fuzzyFilter', () => {
  const items = ['Ustawienia', 'Dzisiejszy dziennik', 'Nowa strona', 'Synchronizuj teraz', 'Przełącz motyw'];

  it('filters and sorts by score', () => {
    const out = fuzzyFilter(items, 'dzien', (x) => [x]).map((r) => r.item);
    expect(out[0]).toBe('Dzisiejszy dziennik');
    expect(out).not.toContain('Nowa strona');
  });

  it('uses extra texts (keywords) with a lower weight', () => {
    const out = fuzzyFilter(items, 'theme', (x) => [x, ...(x === 'Przełącz motyw' ? ['theme', 'ciemny'] : [])]);
    expect(out.map((r) => r.item)).toEqual(['Przełącz motyw']);
  });

  it('keeps the original order for an empty query and honours the limit', () => {
    expect(fuzzyFilter(items, '', (x) => [x], 2).map((r) => r.item)).toEqual(['Ustawienia', 'Dzisiejszy dziennik']);
  });
});

describe('highlightSegments', () => {
  it('splits text into hit and plain parts', () => {
    expect(highlightSegments('Remont', [0, 1, 4])).toEqual([
      { text: 'Re', hit: true },
      { text: 'mo', hit: false },
      { text: 'n', hit: true },
      { text: 't', hit: false },
    ]);
    expect(highlightSegments('abc', [])).toEqual([{ text: 'abc', hit: false }]);
  });
});
