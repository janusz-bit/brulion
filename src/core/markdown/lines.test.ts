import { describe, expect, it } from 'vitest';
import { blankOut, charsForColumns, eolFor, indentOf, isBlank, joinLines, splitLines } from './lines';

describe('splitLines', () => {
  it('splits on \\n, \\r\\n and \\r like CodeMirror and remembers terminators', () => {
    const l = splitLines('a\nb\r\nc\rd');
    expect(l.texts).toEqual(['a', 'b', 'c', 'd']);
    expect(l.starts).toEqual([0, 2, 5, 7]);
    expect(l.eols).toEqual(['\n', '\r\n', '\r', '']);
  });

  it('always yields at least one line and a trailing empty line after a final break', () => {
    expect(splitLines('').texts).toEqual(['']);
    expect(splitLines('x\n').texts).toEqual(['x', '']);
    expect(splitLines('\r\n\r\n').texts).toEqual(['', '', '']);
  });

  it('round-trips through joinLines byte for byte', () => {
    for (const s of ['', 'a', 'a\r\nb\nc\r', 'zażółć\r\ngęślą\njaźń\n']) expect(joinLines(splitLines(s))).toBe(s);
  });

  it('eolFor prefers the line terminator, then the first used one, then \\n', () => {
    const l = splitLines('a\r\nb');
    expect(eolFor(l, 0)).toBe('\r\n');
    expect(eolFor(l, 1)).toBe('\r\n');
    expect(eolFor(splitLines('single'), 0)).toBe('\n');
  });
});

describe('indentation helpers', () => {
  it('measures columns with tab stops of 4', () => {
    expect(indentOf('abc')).toEqual({ col: 0, chars: 0 });
    expect(indentOf('  - x')).toEqual({ col: 2, chars: 2 });
    expect(indentOf('\t- x')).toEqual({ col: 4, chars: 1 });
    expect(indentOf('  \t- x')).toEqual({ col: 4, chars: 3 });
    expect(indentOf('\t\tx')).toEqual({ col: 8, chars: 2 });
  });

  it('charsForColumns removes at most the requested columns', () => {
    expect(charsForColumns('    text', 2)).toBe(2);
    expect(charsForColumns('\ttext', 2)).toBe(1);
    expect(charsForColumns('\t  text', 6)).toBe(3);
    expect(charsForColumns('\ttext', 4)).toBe(1);
    expect(charsForColumns(' text', 4)).toBe(1);
  });

  it('isBlank / blankOut', () => {
    expect(isBlank('')).toBe(true);
    expect(isBlank(' \t ')).toBe(true);
    expect(isBlank(' x')).toBe(false);
    expect(blankOut('a😀\nb')).toBe('   \n ');
  });
});
