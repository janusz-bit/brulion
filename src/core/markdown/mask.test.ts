import { describe, expect, it } from 'vitest';
import { splitLines } from './lines';
import { fenceOpen, isFenceClose, maskDocComments, maskInline } from './mask';

describe('maskInline', () => {
  it('masks code spans of any backtick length, keeping length', () => {
    const s = 'a `#x` b ``c ` d`` e';
    const m = maskInline(s);
    expect(m.length).toBe(s.length);
    expect(m).toBe('a ' + ' '.repeat(4) + ' b ' + ' '.repeat(9) + ' e');
  });

  it('leaves unmatched backticks and unclosed comments as text', () => {
    expect(maskInline('a ` b #tag')).toBe('a ` b #tag');
    expect(maskInline('50%% off #sale')).toBe('50%% off #sale');
    expect(maskInline('<!-- open')).toBe('<!-- open');
  });

  it('masks %%comments%% and html comments, also across lines', () => {
    expect(maskInline('x %%[[hidden]]%% y')).toBe('x ' + ' '.repeat(14) + ' y');
    expect(maskInline('a <!-- #c --> b')).toBe('a ' + ' '.repeat(11) + ' b');
    expect(maskInline('a %%one\ntwo%% b')).toBe('a ' + ' '.repeat(5) + '\n' + ' '.repeat(5) + ' b');
  });

  it('can keep comments when asked', () => {
    expect(maskInline('x %%c%% `y`', { comments: false })).toBe('x %%c%% ' + ' '.repeat(3));
  });

  it('honours backslash escapes', () => {
    expect(maskInline('\\`not code\\` `code`')).toBe('\\`not code\\` ' + ' '.repeat(6));
  });

  it('a code span hides %% inside it', () => {
    expect(maskInline('`%%` real %%c%%')).toBe(' '.repeat(4) + ' real ' + ' '.repeat(5));
  });

  it('is linear-ish on pathological backtick input', () => {
    const s = '`'.repeat(1) + ' x '.repeat(20000) + '``' + ' y'.repeat(20000);
    const t0 = performance.now();
    maskInline(s);
    expect(performance.now() - t0).toBeLessThan(500);
  });
});

describe('fences', () => {
  it('parses openers with info strings', () => {
    expect(fenceOpen('```ts title')).toEqual({ char: '`', len: 3, info: 'ts title' });
    expect(fenceOpen('  ~~~~')).toEqual({ char: '~', len: 4, info: '' });
    expect(fenceOpen('``not')).toBeNull();
    expect(fenceOpen('``` a`b')).toBeNull();
  });

  it('closes only with the same char and at least the same length', () => {
    const open = fenceOpen('````')!;
    expect(isFenceClose('```', open)).toBe(false);
    expect(isFenceClose('`````', open)).toBe(true);
    expect(isFenceClose('~~~~', open)).toBe(false);
    expect(isFenceClose('```` x', open)).toBe(false);
  });
});

describe('maskDocComments', () => {
  const run = (s: string): string => maskDocComments(s, splitLines(s));

  it('masks multi-line comments but keeps line breaks', () => {
    const s = 'a\n%%\n- [ ] hidden\n%%\nb';
    const m = run(s);
    expect(m.length).toBe(s.length);
    expect(m.split('\n')).toEqual(['a', '  ', '            ', '  ', 'b']);
  });

  it('ignores %% inside fenced code and inline code', () => {
    const s = '```\n%%\n```\n`%%` text';
    expect(run(s)).toBe(s);
  });

  it('treats an unclosed %% as literal text', () => {
    const s = 'start %% never closed\n- [ ] task';
    expect(run(s)).toBe(s);
  });

  it('keeps CRLF intact', () => {
    const s = 'x %%a\r\nb%% y';
    expect(run(s)).toBe('x    \r\n    y');
  });
});

describe('maskDocComments — fences inside containers (review regressions)', () => {
  const run = (s: string): string => maskDocComments(s, splitLines(s));

  it('ignores %% inside a fence opened on a list item marker line', () => {
    const s = '- ```mermaid\n  %% diagram comment\n  graph TD\n  ```\n- [ ] visible %%c%%';
    const m = run(s);
    expect(m.split('\n').slice(0, 4)).toEqual(s.split('\n').slice(0, 4));
    expect(m.split('\n')[4]).toBe('- [ ] visible      ');
  });

  it('ignores %% inside fences in quotes and callouts', () => {
    const s = '> [!note]\n> ```mermaid\n> %% c\n> ```\n\n- [ ] task\n%%x%%';
    const m = run(s);
    expect(m.split('\n').slice(0, 6)).toEqual(s.split('\n').slice(0, 6));
    expect(m.split('\n')[6]).toBe('     ');
  });

  it('ignores %% inside fences in nested quotes and ordered items', () => {
    const s = '> > 1. ~~~\n> >    %% x\n> >    ~~~\nafter %%y%%';
    expect(run(s).split('\n').slice(0, 3)).toEqual(s.split('\n').slice(0, 3));
    expect(run(s).split('\n')[3]).toBe('after      ');
  });
});

describe('maskInline — performance (review regressions)', () => {
  it('stays fast with many backtick runs of distinct lengths', () => {
    const s = Array.from({ length: 1500 }, (_, k) => '`'.repeat(k + 1)).join(' x ');
    const t0 = performance.now();
    const m = maskInline(s);
    expect(performance.now() - t0).toBeLessThan(1000);
    expect(m.length).toBe(s.length);
    // No run has a partner of the same length: nothing is masked.
    expect(m).toBe(s);
  });

  it('still pairs runs correctly when building the run index', () => {
    expect(maskInline('`a` ``b`` `c`` d`')).toBe(' '.repeat(3) + ' ' + ' '.repeat(5) + ' ' + ' '.repeat(7));
    expect(maskInline('``x` y``')).toBe(' '.repeat(8));
  });
});
