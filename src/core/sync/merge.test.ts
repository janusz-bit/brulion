import { describe, expect, it } from 'vitest';
import { joinLines, merge3, mergeJson3, splitText, unionLines } from './merge';

describe('splitText / joinLines', () => {
  it('round-trips LF, CRLF, final newline and empty text', () => {
    for (const s of ['', 'a', 'a\n', 'a\nb', 'a\r\nb\r\n', '\n', 'a\n\n', 'ząb\r\nżółw']) {
      const t = splitText(s);
      expect(joinLines(t.lines, t.eol ?? '\n', t.finalNewline)).toBe(s);
    }
  });

  it('detects the dominant line ending', () => {
    expect(splitText('a\r\nb\r\nc\n').eol).toBe('\r\n');
    expect(splitText('a\nb\nc\r\n').eol).toBe('\n');
    expect(splitText('abc').eol).toBeNull();
  });
});

describe('merge3', () => {
  it('takes one-sided changes', () => {
    const base = '# Lista\n- mleko\n- chleb\n- masło\n';
    const local = '# Lista zakupów\n- mleko\n- chleb\n- masło\n';
    const remote = '# Lista\n- mleko\n- chleb\n- masło\n- jajka\n';
    const r = merge3(base, local, remote);
    expect(r.clean).toBe(true);
    expect(r.text).toBe('# Lista zakupów\n- mleko\n- chleb\n- masło\n- jajka\n');
  });

  it('short-cuts identical / unchanged sides', () => {
    expect(merge3('a\n', 'a\n', 'b\n')).toMatchObject({ text: 'b\n', clean: true });
    expect(merge3('a\n', 'b\n', 'a\n')).toMatchObject({ text: 'b\n', clean: true });
    expect(merge3('a\n', 'c\n', 'c\n')).toMatchObject({ text: 'c\n', clean: true });
  });

  it('merges journal appends on two devices (union of insertions)', () => {
    const base = '- 08:00 kawa\n';
    const local = '- 08:00 kawa\n- 09:00 spotkanie z Łucją\n';
    const remote = '- 08:00 kawa\n- 12:30 obiad — żurek\n';
    const r = merge3(base, local, remote);
    expect(r.clean).toBe(true);
    expect(r.text).toBe('- 08:00 kawa\n- 09:00 spotkanie z Łucją\n- 12:30 obiad — żurek\n');
    expect(r.hunks).toEqual([{ kind: 'ok', lines: ['- 08:00 kawa', '- 09:00 spotkanie z Łucją', '- 12:30 obiad — żurek'] }]);
  });

  it('drops remote insertions identical to local ones in a union', () => {
    const base = '# Dziennik\n';
    const local = '# Dziennik\n- [ ] kupić mleko\n- zadzwonić do mamy\n';
    const remote = '# Dziennik\n- [ ] kupić mleko\n- podlać kwiaty\n';
    const r = merge3(base, local, remote);
    expect(r.clean).toBe(true);
    expect(r.text).toBe('# Dziennik\n- [ ] kupić mleko\n- zadzwonić do mamy\n- podlać kwiaty\n');
  });

  it('does not duplicate lines inserted in a different order', () => {
    const r = merge3('x\n', 'x\n- b\n- c\n', 'x\n- c\n- b\n');
    expect(r.clean).toBe(true);
    expect(r.text).toBe('x\n- b\n- c\n');
  });

  it('merges independent edits in different places', () => {
    const base = 'a\nb\nc\nd\ne\nf\n';
    const local = 'A\nb\nc\nd\ne\nf\n';
    const remote = 'a\nb\nc\nd\ne\nF\n';
    expect(merge3(base, local, remote)).toMatchObject({ text: 'A\nb\nc\nd\ne\nF\n', clean: true });
  });

  it('accepts identical changes on both sides (false conflict)', () => {
    const r = merge3('a\nb\nc\n', 'a\nB\nc\n', 'a\nB\nc\n');
    expect(r).toMatchObject({ text: 'a\nB\nc\n', clean: true });
    const r2 = merge3('a\nb\nc\nd\n', 'a\nB\nc\nD\n', 'a\nB\nc\nd\n');
    expect(r2).toMatchObject({ text: 'a\nB\nc\nD\n', clean: true });
  });

  it('reports a real conflict, keeping the local side in the text', () => {
    const base = 'tytuł\nZadanie: kupić chleb\nkoniec\n';
    const local = 'tytuł\nZadanie: kupić bułki\nkoniec\n';
    const remote = 'tytuł\nZadanie: kupić rogale\nkoniec\n';
    const r = merge3(base, local, remote);
    expect(r.clean).toBe(false);
    expect(r.text).toBe(local);
    expect(r.hunks).toEqual([
      { kind: 'ok', lines: ['tytuł'] },
      { kind: 'conflict', base: ['Zadanie: kupić chleb'], local: ['Zadanie: kupić bułki'], remote: ['Zadanie: kupić rogale'] },
      { kind: 'ok', lines: ['koniec'] },
    ]);
  });

  it('treats delete vs modify of the same line as a conflict', () => {
    const r = merge3('a\nb\nc\n', 'a\nc\n', 'a\nB\nc\n');
    expect(r.clean).toBe(false);
    expect(r.text).toBe('a\nc\n');
  });

  it('merges against an empty base as a union (first sync)', () => {
    const r = merge3('', '# Dziennik\n- rano\n', '# Dziennik\n- wieczorem\n');
    expect(r).toMatchObject({ text: '# Dziennik\n- rano\n- wieczorem\n', clean: true });
    expect(merge3('', 'a\nb\n', 'a\nb\nc\n')).toMatchObject({ text: 'a\nb\nc\n', clean: true });
  });

  it('ignores CRLF vs LF differences and keeps the local style', () => {
    const base = 'a\r\nb\r\nc\r\n';
    const local = 'a\r\nb\r\nc\r\nd\r\n';
    const remote = 'A\nb\nc\n';
    const r = merge3(base, local, remote);
    expect(r).toMatchObject({ text: 'A\r\nb\r\nc\r\nd\r\n', clean: true });
  });

  it('merges the final newline three-way', () => {
    // Local removed the final newline, remote changed something else.
    expect(merge3('a\nb\n', 'a\nb', 'A\nb\n').text).toBe('A\nb');
    // Remote added a final newline, local appended a line.
    expect(merge3('a\nb', 'a\nb\nc', 'a\nb\n').text).toBe('a\nb\nc\n');
    // Both without a final newline.
    expect(merge3('a', 'a\nb', 'z\na').text).toBe('z\na\nb');
  });

  it('handles empty local or remote', () => {
    expect(merge3('', '', 'x\n')).toMatchObject({ text: 'x\n', clean: true });
    expect(merge3('', 'x\n', '')).toMatchObject({ text: 'x\n', clean: true });
    expect(merge3('a\n', '', 'a\nb\n').clean).toBe(false);
  });

  it('works on Polish text with diacritics at line boundaries', () => {
    const base = 'Zażółć\ngęślą\njaźń\n';
    const local = 'Zażółć!\ngęślą\njaźń\n';
    const remote = 'Zażółć\ngęślą\njaźń\nŹdźbło\n';
    expect(merge3(base, local, remote)).toMatchObject({ text: 'Zażółć!\ngęślą\njaźń\nŹdźbło\n', clean: true });
  });
});

describe('unionLines', () => {
  it('keeps common lines once and appends remote-only lines', () => {
    expect(unionLines(['a', 'b'], ['a', 'c'])).toEqual(['a', 'b', 'c']);
    expect(unionLines([], ['x'])).toEqual(['x']);
    expect(unionLines(['x'], [])).toEqual(['x']);
  });

  it('treats duplicates as a multiset', () => {
    expect(unionLines(['- a'], ['- a', '- a'])).toEqual(['- a', '- a']);
  });
});

describe('mergeJson3', () => {
  it('merges keys changed on different sides', () => {
    const base = JSON.stringify({ journalsFolder: 'journals', weekStartsOn: 1 });
    const local = JSON.stringify({ journalsFolder: 'dziennik', weekStartsOn: 1 });
    const remote = JSON.stringify({ journalsFolder: 'journals', weekStartsOn: 0, extra: 'ź' });
    expect(JSON.parse(mergeJson3(base, local, remote)!)).toEqual({ journalsFolder: 'dziennik', weekStartsOn: 0, extra: 'ź' });
  });

  it('local wins for keys changed on both sides; deletions propagate', () => {
    const base = JSON.stringify({ a: 1, b: 2, c: 3 });
    const local = JSON.stringify({ a: 10, b: 2, c: 3 });
    const remote = JSON.stringify({ a: 20, c: 3 });
    expect(JSON.parse(mergeJson3(base, local, remote)!)).toEqual({ a: 10, c: 3 });
  });

  it('returns null for invalid JSON or non-objects', () => {
    expect(mergeJson3('{}', '{oops', '{}')).toBeNull();
    expect(mergeJson3('{}', '{}', '[1]')).toBeNull();
  });

  it('treats an invalid base as empty and ends with a newline', () => {
    const out = mergeJson3('garbage', '{"a":1}', '{"b":2}');
    expect(out).toBe('{\n  "a": 1,\n  "b": 2\n}\n');
  });

  it('keeps a "__proto__" key as data', () => {
    const out = mergeJson3('{}', '{"__proto__":{"x":1}}', '{}');
    expect(out).toContain('"__proto__"');
    expect(({} as Record<string, unknown>).x).toBeUndefined();
  });
});

describe('merge3 on large notes', () => {
  it('merges a long note with many blank lines quickly (prepend on one side, append on the other)', () => {
    const lines: string[] = [];
    for (let i = 0; i < 4000; i++) lines.push(i % 2 ? '' : `akapit numer ${i} — zażółć`);
    const base = `${lines.join('\n')}\n`;
    const t = performance.now();
    const r = merge3(base, `nowy początek\n${base}`, `${base}nowy koniec\n`);
    expect(performance.now() - t).toBeLessThan(2000);
    expect(r.clean).toBe(true);
    expect(r.text).toBe(`nowy początek\n${base}nowy koniec\n`);
  });

  it('handles notes with hundreds of thousands of lines without overflowing the stack', { timeout: 30_000 }, () => {
    const base = Array.from({ length: 200_000 }, (_, i) => `w${i}`).join('\n');
    const r = merge3(base, `L\n${base}`, `${base}\nR`);
    expect(r.clean).toBe(true);
    expect(r.text.startsWith('L\nw0\n')).toBe(true);
    expect(r.text.endsWith('w199999\nR')).toBe(true);
    // First sync of two big versions (empty base): a union, also without a stack overflow.
    const u = merge3('', base, `${base}\nnowa`);
    expect(u.clean).toBe(true);
    expect(u.text).toBe(`${base}\nnowa`);
  });
});
