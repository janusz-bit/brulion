/**
 * Case and diacritic folding used by the natural-language parsers.
 *
 * Folding lower-cases and strips diacritics ("Środę" -> "srode", "Łódź" -> "lodz") so that
 * patterns can be written in plain ASCII and users may type without Polish letters.
 * {@link foldWithMap} additionally returns an index map so matches found in the folded
 * text can be mapped back to exact offsets in the original string.
 */

/** Folded text plus, for every UTF-16 unit of `text`, the offset of its source character. */
export interface FoldedText {
  text: string;
  /** `map[i]` = offset in the original string of folded unit `i`; `map[text.length]` = original length. */
  map: number[];
}

const MARKS_RE = /\p{M}/gu;

/** Fold one code point (lower-case, strip combining marks, ł -> l). May return '' (combining marks). */
function foldChar(ch: string): string {
  let lower = ch.toLowerCase();
  if (lower === 'ł') return 'l';
  // Multi-unit lowercase expansions (e.g. "İ") are folded via NFD below as well.
  lower = lower.normalize('NFD').replace(MARKS_RE, '');
  return lower;
}

/** Lower-case and strip diacritics. "Poniedziałek" -> "poniedzialek". */
export function foldText(s: string): string {
  let out = '';
  for (const ch of s) out += foldChar(ch);
  return out;
}

/** Like {@link foldText} but also returns an index map back into the original string. */
export function foldWithMap(input: string): FoldedText {
  let text = '';
  const map: number[] = [];
  let offset = 0;
  for (const ch of input) {
    const folded = foldChar(ch);
    for (let k = 0; k < folded.length; k++) map.push(offset);
    text += folded;
    offset += ch.length;
  }
  map.push(input.length);
  return { text, map };
}

/** Map a folded [start, end) range back to the original string's offsets. */
export function unfoldRange(f: FoldedText, start: number, end: number): { start: number; end: number } {
  const s = f.map[start] ?? f.map[f.map.length - 1] ?? 0;
  const e = f.map[end] ?? f.map[f.map.length - 1] ?? 0;
  return { start: s, end: e };
}
