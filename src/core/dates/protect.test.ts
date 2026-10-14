import { describe, expect, it } from 'vitest';
import { foldText } from './fold';
import { protectedMask } from './protect';

/** The protected substrings of `text` (folded first, like the parser does). */
function spans(text: string): string[] {
  const folded = foldText(text);
  const mask = protectedMask(folded);
  const out: string[] = [];
  let start = -1;
  for (let i = 0; i <= folded.length; i++) {
    const on = i < folded.length && mask[i] === 1;
    if (on && start < 0) start = i;
    if (!on && start >= 0) {
      out.push(folded.slice(start, i));
      start = -1;
    }
  }
  return out;
}

describe('protectedMask', () => {
  it.each([
    ['Czytaj https://blog.pl/2026/10/15/wpis teraz', ['https://blog.pl/2026/10/15/wpis']],
    ['HTTPS://Example.com/Jutro', ['https://example.com/jutro']],
    ['mailto:jan@x.pl', ['mailto:jan@x.pl']],
    ['www.zażółć.pl/2026/10/15 i dalej', ['www.zazolc.pl/2026/10/15']],
    ['Zobacz [[Spotkanie w piątek]] dziś', ['[[spotkanie w piatek]]']],
    ['Osadź ![[Notatka#^abc123]]', ['![[notatka#^abc123]]']],
    ['Link [jutro](https://x.pl/a) i ![img](a.png)', ['[jutro](https://x.pl/a)', '![img](a.png)']],
    ['Kod `o 9` i `jutro`', ['`o 9`', '`jutro`']],
    ['#praca i #Łódź/2026-10 oraz (#jutro)', ['#praca', '#lodz/2026-10', '#jutro']],
    ['#[[w piątek]]', ['[[w piatek]]']],
    ['Napisz do jan.jutro@firma.pl.', ['jan.jutro@firma.pl']],
    ['Zadanie 📅 2026-10-15 i ⏳ 2026-10-16', ['📅 2026-10-15', '⏳ 2026-10-16']],
    ['Budzik ⏰! 2026-10-15 06:30 rano', ['⏰! 2026-10-15 06:30']],
    ['Budzik ⏰️ 2026-10-15T06:30', ['⏰ 2026-10-15t06:30']],
    ['Plugin (@2026-10-15 09:00)', ['(@2026-10-15 09:00)']],
  ])('%j', (input, expected) => {
    expect(spans(input)).toEqual(expected);
  });

  it.each([
    'Zadzwoń jutro o 18',
    'issue#12 and #1 or # heading',
    'a@b and x@y. too',
    '[niezamknięty link',
    '`niezamknięty kod',
    '[[niezamknięty wiki',
    'cena 5 zł @ sklep',
    '📅 bez daty',
    '',
  ])('protects nothing in %j', (input) => {
    expect(spans(input)).toEqual([]);
  });

  it('is linear on pathological inputs', () => {
    const t0 = performance.now();
    for (const s of [
      '[['.repeat(30_000),
      '[a'.repeat(30_000),
      '`a'.repeat(30_000),
      'a@'.repeat(30_000),
      `a@${'.'.repeat(60_000)}b`,
      `${'a.'.repeat(30_000)}@x`,
      `#${'1'.repeat(60_000)}`,
      '#1 '.repeat(20_000),
      'http:'.repeat(12_000),
    ]) {
      protectedMask(s);
    }
    expect(performance.now() - t0).toBeLessThan(3000);
  }, 60_000);
});
