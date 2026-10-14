import { describe, expect, it } from 'vitest';
import { extractInline, parseMdDestination, parseMdLink, parseWikiInner, safeDecode, uniqueTags } from './inline';

const links = (s: string) => extractInline(s).links;
const tags = (s: string) => extractInline(s).tags;

describe('wiki links and embeds', () => {
  it('parses targets, aliases, headings and block ids', () => {
    expect(links('see [[Page]] and [[Other Page|alias]]')).toEqual([
      { kind: 'wiki', target: 'Page', line: 0, from: 4, to: 12 },
      { kind: 'wiki', target: 'Other Page', display: 'alias', line: 0, from: 17, to: 37 },
    ]);
    expect(links('[[Page#Heading]]')[0]).toMatchObject({ target: 'Page', heading: 'Heading' });
    expect(links('[[Page#^abc123]]')[0]).toMatchObject({ target: 'Page', blockId: 'abc123' });
    expect(links('[[Page#H1#H2|x]]')[0]).toMatchObject({ target: 'Page', heading: 'H1#H2', display: 'x' });
  });

  it('supports same-page links', () => {
    expect(links('[[#^abc]]')[0]).toMatchObject({ kind: 'wiki', target: '', blockId: 'abc' });
    expect(links('[[#Sekcja]]')[0]).toMatchObject({ target: '', heading: 'Sekcja' });
  });

  it('parses embeds of pages and attachments', () => {
    expect(links('![[Page#^id]]')[0]).toMatchObject({ kind: 'embed', target: 'Page', blockId: 'id', from: 0, to: 13 });
    expect(links('![[zdjęcie.png|300]]')[0]).toMatchObject({ kind: 'embed', target: 'zdjęcie.png', display: '300' });
  });

  it('handles the escaped table pipe', () => {
    expect(links('| [[Page\\|Alias]] |')[0]).toMatchObject({ target: 'Page', display: 'Alias' });
  });

  it('ignores empty, unclosed and multi-line wiki links', () => {
    expect(links('[[]] [[ ]] [[open')).toEqual([]);
    expect(links('[[a\nb]]')).toEqual([]);
    expect(links('[[a [[b]]')).toMatchObject([{ target: 'b' }]);
  });

  it('respects backslash escapes', () => {
    expect(links('\\[[not a link]]')).toEqual([]);
  });
});

describe('block references', () => {
  it('parses ((ref)) and !((embed))', () => {
    expect(links('see ((abc-123)) and !((64f0a1b2-0000-4c4c-8888-123456789abc))')).toEqual([
      { kind: 'blockref', target: 'abc-123', blockId: 'abc-123', line: 0, from: 4, to: 15 },
      { kind: 'blockembed', target: '64f0a1b2-0000-4c4c-8888-123456789abc', blockId: '64f0a1b2-0000-4c4c-8888-123456789abc', line: 0, from: 20, to: 61 },
    ]);
    expect(links('((not valid))')).toEqual([]);
  });
});

describe('markdown links', () => {
  it('parses external and relative links', () => {
    expect(links('[Brulion](https://example.com/a?b=c#frag)')[0]).toMatchObject({
      kind: 'markdown',
      target: 'https://example.com/a?b=c#frag',
      display: 'Brulion',
    });
    expect(links('[x](folder/Old%20Name.md#My%20Heading)')[0]).toMatchObject({
      target: 'folder/Old Name.md',
      heading: 'My Heading',
    });
    expect(links('[x](Page.md#^blk)')[0]).toMatchObject({ target: 'Page.md', blockId: 'blk' });
    expect(links('[x](#local)')[0]).toMatchObject({ target: '', heading: 'local' });
    expect(links('[x](<Some Page.md> "title")')[0]).toMatchObject({ target: 'Some Page.md' });
  });

  it('parses images with alt text as display', () => {
    expect(links('![wykres](assets/wykres.png)')[0]).toMatchObject({ kind: 'markdown', target: 'assets/wykres.png', display: 'wykres', from: 0 });
  });

  it('balances parentheses and brackets', () => {
    expect(links('[a [b] c](x(1).md)')[0]).toMatchObject({ target: 'x(1).md', display: 'a [b] c' });
  });

  it('is not confused by footnotes, bare brackets or empty destinations', () => {
    expect(links('note[^1] and [label] and [x]()')).toEqual([]);
  });

  it('keeps malformed percent escapes', () => {
    expect(links('[x](100%25%zz.md)')[0]!.target).toBe('100%%zz.md');
  });
});

describe('urls', () => {
  it('detects bare urls and trims trailing punctuation', () => {
    expect(links('Zobacz https://example.com/a_(b). Koniec')[0]).toMatchObject({ kind: 'url', target: 'https://example.com/a_(b)' });
    expect(links('(https://example.com/x)')[0]!.target).toBe('https://example.com/x');
    expect(links('HTTPS://EXAMPLE.COM, ok')[0]!.target).toBe('HTTPS://EXAMPLE.COM');
    expect(links('xhttps://no.pl')).toEqual([]);
    expect(links('https://')).toEqual([]);
  });

  it('detects autolinks and email autolinks', () => {
    expect(links('<https://a.pl/x y>')).toEqual([]);
    expect(links('<https://a.pl/x>')[0]).toMatchObject({ kind: 'url', target: 'https://a.pl/x', from: 0, to: 16 });
    expect(links('<jan@example.com>')[0]).toMatchObject({ kind: 'url', target: 'mailto:jan@example.com' });
  });
});

describe('tags', () => {
  it('finds tags with Unicode letters, nesting and #[[multi word]]', () => {
    expect(tags('#zakupy/spożywcze #Łódź #a_b-c #[[dwa słowa]]')).toEqual(['zakupy/spożywcze', 'Łódź', 'a_b-c', 'dwa słowa']);
  });

  it('requires a non-digit and a valid preceding character', () => {
    expect(tags('#1 #2026 #1a (#ok) a#nie x.#nie')).toEqual(['1a', 'ok']);
  });

  it('ignores headings, code, links, urls and escapes', () => {
    expect(tags('# Heading')).toEqual([]);
    expect(tags('`#code` ``#x``')).toEqual([]);
    expect(tags('[[Page#Heading]] [x](#anchor) https://x.pl/#frag \\#esc %%#hidden%%')).toEqual([]);
  });

  it('deduplicates case-insensitively keeping the first spelling', () => {
    expect(tags('#Praca #praca #PRACA')).toEqual(['Praca']);
  });

  it('strips trailing slashes and normalises NFD to NFC', () => {
    expect(tags('#a/b/')).toEqual(['a/b']);
    const nfd = '#spoz\u0307ywcze'; // "ż" decomposed
    expect(tags(nfd)).toEqual(['spożywcze']);
  });

  it('does not treat Logseq priorities as tags', () => {
    expect(tags('TODO [#A] zadanie')).toEqual([]);
  });
});

describe('offsets and lines', () => {
  it('applies baseOffset and line numbers across lines', () => {
    const r = extractInline('first\nsecond [[P]]', 100, 7);
    expect(r.links[0]).toMatchObject({ line: 8, from: 113, to: 118 });
  });

  it('counts CRLF line breaks', () => {
    const r = extractInline('a\r\nb [[P]]', 10, 0);
    expect(r.links[0]).toMatchObject({ line: 1, from: 15, to: 20 });
  });

  it('never throws on garbage', () => {
    const junk = '[[(([](<>)!#\\`%%]]((!![[#^|]]](((';
    expect(() => extractInline(junk.repeat(50))).not.toThrow();
  });

  it('stays fast on pathological brackets', () => {
    const t0 = performance.now();
    extractInline('[['.repeat(20000) + '[('.repeat(20000));
    expect(performance.now() - t0).toBeLessThan(1000);
  });
});

describe('helpers', () => {
  it('parseWikiInner / parseMdDestination / safeDecode / uniqueTags', () => {
    expect(parseWikiInner('  A  #  H  | d ')).toEqual({ target: 'A', heading: 'H', display: 'd' });
    expect(parseWikiInner('|x')).toBeNull();
    expect(parseMdDestination('C:\\notes\\a.md')).toEqual({ target: 'C:\\notes\\a.md' });
    expect(parseMdDestination('mailto:a@b.pl')).toEqual({ target: 'mailto:a@b.pl' });
    expect(safeDecode('Za%C5%BC%C3%B3%C5%82%C4%87')).toBe('Zażółć');
    expect(uniqueTags(['A', 'a', 'B'])).toEqual(['A', 'B']);
    expect(parseMdLink('[a](b', 0)).toBeNull();
  });
});

describe('scanInline performance (review regressions)', () => {
  const time = (f: () => void): number => {
    const t0 = performance.now();
    f();
    return performance.now() - t0;
  };

  it('trims trailing parentheses of bare URLs in linear time', () => {
    const s = 'see https://a.b/' + '()'.repeat(5000) + ')'.repeat(20000);
    let res: ReturnType<typeof extractInline> | undefined;
    expect(time(() => (res = extractInline(s)))).toBeLessThan(1000);
    expect(res!.links[0]!.target).toBe('https://a.b/' + '()'.repeat(5000));
  });

  it('handles many wiki links on one long line in linear time', () => {
    const s = '[[a]] '.repeat(60000);
    let res: ReturnType<typeof extractInline> | undefined;
    expect(time(() => (res = extractInline(s)))).toBeLessThan(1500);
    expect(res!.links.length).toBe(60000);
  });

  it('rejects unclosed brackets quickly', () => {
    expect(time(() => extractInline('!['.repeat(100000)))).toBeLessThan(1000);
    expect(time(() => extractInline('[x '.repeat(100000)))).toBeLessThan(1000);
  });
});
