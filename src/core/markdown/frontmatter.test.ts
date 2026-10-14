import { describe, expect, it } from 'vitest';
import { findFrontmatter, parseFrontmatter, parseYaml, resolvePlain, stringifyFrontmatter, stringifyYaml } from './frontmatter';

describe('parseFrontmatter — location', () => {
  it('returns empty data and offset 0 without frontmatter', () => {
    expect(parseFrontmatter('# Title\n')).toEqual({ data: {}, bodyStart: 0, bodyStartLine: 0 });
    expect(parseFrontmatter('')).toEqual({ data: {}, bodyStart: 0, bodyStartLine: 0 });
  });

  it('finds bodyStart / bodyStartLine after the closing delimiter', () => {
    const src = '---\ntitle: Dom\n---\nBody';
    const r = parseFrontmatter(src);
    expect(r.data).toEqual({ title: 'Dom' });
    expect(r.bodyStart).toBe(src.indexOf('Body'));
    expect(r.bodyStartLine).toBe(3);
  });

  it('handles CRLF, BOM, "..." closing and trailing spaces', () => {
    const src = '\uFEFF--- \r\na: 1\r\n...\r\nBody';
    const r = parseFrontmatter(src);
    expect(r.data).toEqual({ a: 1 });
    expect(src.slice(r.bodyStart)).toBe('Body');
    expect(r.bodyStartLine).toBe(3);
  });

  it('handles empty frontmatter and frontmatter at end of file', () => {
    expect(parseFrontmatter('---\n---\nx')).toEqual({ data: {}, bodyStart: 8, bodyStartLine: 2 });
    const r = parseFrontmatter('---\na: b\n---');
    expect(r.data).toEqual({ a: 'b' });
    expect(r.bodyStart).toBe(12);
  });

  it('requires an opening line at the very start and a closing line', () => {
    expect(parseFrontmatter('\n---\na: 1\n---\n').data).toEqual({});
    expect(parseFrontmatter('---\na: 1\nno closing').data).toEqual({});
    expect(findFrontmatter('---')).toBeNull();
  });
});

describe('parseYaml — scalars', () => {
  it('resolves plain scalars', () => {
    expect(parseYaml('a: text\nb: 12\nc: -3.5\nd: true\ne: False\nf: null\ng: ~\nh:\ni: 2026-10-14\nj: 007\nk: 1e3')).toEqual({
      a: 'text',
      b: 12,
      c: -3.5,
      d: true,
      e: false,
      f: null,
      g: null,
      h: null,
      i: '2026-10-14',
      j: '007',
      k: 1000,
    });
  });

  it('keeps huge integers and times as strings', () => {
    expect(parseYaml('id: 12345678901234567890\nt: 10:30')).toEqual({ id: '12345678901234567890', t: '10:30' });
  });

  it('handles quoting, escapes and comments', () => {
    const y = [
      `a: "zażółć \\"gęślą\\" \\n jaźń \\u0105 \\x41"`,
      `b: 'it''s # not a comment'`,
      `c: value # comment`,
      `d: "#tag" # c`,
      `e: http://example.com/#frag`,
      `# full line comment`,
      `"quoted key": 1`,
      `'single key': 2`,
    ].join('\n');
    expect(parseYaml(y)).toEqual({
      a: 'zażółć "gęślą" \n jaźń ą A',
      b: "it's # not a comment",
      c: 'value',
      d: '#tag',
      e: 'http://example.com/#frag',
      'quoted key': 1,
      'single key': 2,
    });
  });

  it('joins multi-line plain and quoted scalars', () => {
    expect(parseYaml('a: one\n  two\n  three\nb: "x\n  y"')).toEqual({ a: 'one two three', b: 'x y' });
  });

  it('resolvePlain exported helper', () => {
    expect(resolvePlain(' 42 ')).toBe(42);
    expect(resolvePlain('.5')).toBe(0.5);
    expect(resolvePlain('abc')).toBe('abc');
  });
});

describe('parseYaml — collections', () => {
  it('parses inline arrays and maps', () => {
    expect(parseYaml('tags: [a, "b c", 3, true]\nm: {x: 1, "y z": [p, q]}\nempty: []\nem: {}')).toEqual({
      tags: ['a', 'b c', 3, true],
      m: { x: 1, 'y z': ['p', 'q'] },
      empty: [],
      em: {},
    });
  });

  it('parses multi-line flow collections', () => {
    expect(parseYaml('tags: [\n  a,\n  b\n]\nnext: 1')).toEqual({ tags: ['a', 'b'], next: 1 });
  });

  it('reads unquoted [[links]] as nested lists', () => {
    expect(parseYaml('related: [[Strona A]]')).toEqual({ related: [['Strona A']] });
  });

  it('parses block arrays, indented or not', () => {
    expect(parseYaml('aliases:\n  - Dom\n  - "Mój dom"\ntags:\n- a\n- b # c\n')).toEqual({ aliases: ['Dom', 'Mój dom'], tags: ['a', 'b'] });
  });

  it('parses nested maps and sequences of maps', () => {
    const y = 'author:\n  name: Jan\n  links:\n    - a\n    - b\nitems:\n  - name: x\n    qty: 2\n  - name: y\n    qty: 3\n  - plain\n';
    expect(parseYaml(y)).toEqual({
      author: { name: 'Jan', links: ['a', 'b'] },
      items: [{ name: 'x', qty: 2 }, { name: 'y', qty: 3 }, 'plain'],
    });
  });

  it('parses block scalars with chomping', () => {
    const y = 'lit: |\n  line 1\n  line 2\n\nfold: >\n  a\n  b\n\n  c\nstrip: |-\n  x\nkeep: |+\n  y\n\nlast: 1';
    expect(parseYaml(y)).toEqual({ lit: 'line 1\nline 2\n', fold: 'a b\nc\n', strip: 'x', keep: 'y\n\n', last: 1 });
  });

  it('keeps comment-like lines inside block scalars', () => {
    expect(parseYaml('a: |\n  # not a comment\n  text\nb: 2')).toEqual({ a: '# not a comment\ntext\n', b: 2 });
  });

  it('nulls for nested values that are missing', () => {
    expect(parseYaml('a:\nb: 1')).toEqual({ a: null, b: 1 });
  });
});

describe('parseYaml — malformed input never throws', () => {
  it('skips garbage lines and keeps what parsed', () => {
    expect(parseYaml('a: 1\n???\n  stray: deeper\nb: [unclosed\nc: 3')).toMatchObject({ a: 1, c: 3 });
    expect(parseYaml('- top level list\n- x')).toEqual({});
    expect(parseYaml('just text')).toEqual({});
    expect(parseYaml('a: "unterminated\nb: 2')).toMatchObject({ a: expect.any(String) });
  });

  it('defines dangerous keys as plain own properties', () => {
    const r = parseYaml('__proto__: polluted\nconstructor: x');
    expect(Object.getPrototypeOf(r)).toBe(Object.prototype);
    expect(Object.prototype.hasOwnProperty.call(r, '__proto__')).toBe(true);
    expect(({} as Record<string, unknown>).polluted).toBeUndefined();
  });

  it('survives deep nesting and random input', () => {
    const deep = Array.from({ length: 200 }, (_, i) => ' '.repeat(i * 2) + `k${i}:`).join('\n');
    expect(() => parseYaml(deep)).not.toThrow();
    expect(() => parseYaml('a: ' + '['.repeat(5000))).not.toThrow();
    const chars = 'ab: -[]{}"\'|>#\n\t ,!&*';
    let seed = 7;
    for (let n = 0; n < 200; n++) {
      let s = '';
      for (let i = 0; i < 80; i++) {
        seed = (seed * 1103515245 + 12345) % 2147483648;
        s += chars[seed % chars.length];
      }
      expect(() => parseYaml(s)).not.toThrow();
    }
  });

  it('duplicate keys: last wins', () => {
    expect(parseYaml('a: 1\na: 2')).toEqual({ a: 2 });
  });
});

describe('stringifyFrontmatter', () => {
  it('returns empty string for empty data', () => {
    expect(stringifyFrontmatter({})).toBe('');
    expect(stringifyFrontmatter({ a: undefined })).toBe('');
  });

  it('writes Obsidian-style block lists and quotes when needed', () => {
    const out = stringifyFrontmatter({
      title: 'Mój dom',
      tags: ['dom', 'projekt'],
      aliases: [],
      count: 3,
      done: false,
      empty: null,
      date: '2026-10-14',
      link: '[[Strona]]',
      colon: 'a: b',
      num: '42',
      hash: '#tag',
    });
    expect(out).toBe(
      [
        '---',
        'title: Mój dom',
        'tags:',
        '  - dom',
        '  - projekt',
        'aliases: []',
        'count: 3',
        'done: false',
        'empty:',
        'date: 2026-10-14',
        'link: "[[Strona]]"',
        'colon: "a: b"',
        'num: "42"',
        'hash: "#tag"',
        '---',
        '',
      ].join('\n'),
    );
  });

  it('round-trips through parseFrontmatter', () => {
    const data = {
      title: 'Zażółć gęślą jaźń',
      'key with space': 'v',
      tags: ['a', 'b c', '#x', '1'],
      nested: { a: 1, b: ['x', 'y'], deeper: { z: 'ż' } },
      items: [{ name: 'x', qty: 2 }, { name: 'y' }, ['p', 'q'], 5, null],
      multi: 'line 1\nline 2\n',
      strip: 'a\nb',
      keep: 'k\n\n',
      lead: '  spaced\nx',
      quote: 'say "hi"',
      apostrophe: "it's",
      empty: '',
      bool: true,
      float: 1.5,
      neg: -2,
      control: 'tab\there',
    };
    const out = stringifyFrontmatter(data);
    expect(parseFrontmatter(out + 'body').data).toEqual(data);
    expect(parseFrontmatter(out + 'body').bodyStart).toBe(out.length);
  });

  it('stringifyYaml formats Dates as local dates', () => {
    expect(stringifyYaml({ d: new Date(2026, 9, 14), t: new Date(2026, 9, 14, 9, 5) })).toBe('d: 2026-10-14\nt: 2026-10-14 09:05\n');
  });
});

describe('stringifyYaml — interoperability with YAML 1.1 readers (review regressions)', () => {
  it('quotes strings that YAML 1.1 / other YAML 1.2 readers would read as booleans or numbers', () => {
    const data = {
      a: 'yes',
      b: 'No',
      c: 'on',
      d: 'OFF',
      e: 'y',
      f: '007',
      g: '0x1F',
      h: '0o17',
      i: '1_000',
      j: '.inf',
      k: '-.Inf',
      l: '.NaN',
      m: '+12',
      n: '1:20',
    };
    const yaml = stringifyYaml(data);
    for (const v of Object.values(data)) expect(yaml).toContain(JSON.stringify(v));
    expect(parseYaml(yaml)).toEqual(data);
  });

  it('keeps ordinary strings, dates and times plain', () => {
    expect(stringifyYaml({ a: 'yesterday', d: '2026-10-14', t: 'Notatka 1', x: 'none', ż: 'ółw' })).toBe(
      'a: yesterday\nd: 2026-10-14\nt: Notatka 1\nx: none\nż: ółw\n',
    );
    // Keys are quoted by the same rule (`n:` is `false:` for YAML 1.1 readers).
    expect(stringifyYaml({ n: 1 })).toBe('"n": 1\n');
    expect(parseYaml(stringifyYaml({ n: 1 }))).toEqual({ n: 1 });
  });
});

describe('block scalars at the end of the document (review regressions)', () => {
  it('keep chomping does not count the final line break as a blank line', () => {
    expect(parseYaml('k: |+\n  two\n\n')).toEqual({ k: 'two\n\n' });
    expect(parseYaml('k: |+\n  two\n')).toEqual({ k: 'two\n' });
    expect(parseYaml('k: |+\n  two\n\nz: 1')).toEqual({ k: 'two\n\n', z: 1 });
    for (const v of ['two\n\n', 'a\nb\n\n\n', 'zażółć\n']) {
      expect(parseFrontmatter(stringifyFrontmatter({ k: v, z: 1 })).data).toEqual({ k: v, z: 1 });
      expect(parseFrontmatter(stringifyFrontmatter({ k: v })).data).toEqual({ k: v });
    }
  });
});
