import { markdownLanguage } from '@codemirror/lang-markdown';
import type { MarkdownParser } from '@lezer/markdown';
import { describe, expect, it } from 'vitest';
import { highlightTree } from '@lezer/highlight';
import { brulionHighlightStyle } from './highlight';
import { brulionMarkdown, tagBodyLength } from './syntax';

const parser = (markdownLanguage.parser as MarkdownParser).configure(brulionMarkdown);

/** All nodes as "Name:text" (document order), optionally only the given names. */
function nodes(doc: string, only?: string[]): string[] {
  const out: string[] = [];
  parser.parse(doc).iterate({
    enter(n) {
      if (n.name === 'Document') return;
      if (only && !only.includes(n.name)) return;
      out.push(`${n.name}:${doc.slice(n.from, n.to)}`);
    },
  });
  return out;
}

describe('syntax: wiki links and embeds', () => {
  it('parses [[Page]] with target', () => {
    expect(nodes('see [[Projekty/Dom]] now', ['WikiLink', 'WikiTarget', 'WikiMark'])).toEqual([
      'WikiLink:[[Projekty/Dom]]',
      'WikiMark:[[',
      'WikiTarget:Projekty/Dom',
      'WikiMark:]]',
    ]);
  });

  it('parses alias, heading and block targets', () => {
    expect(nodes('[[Strona#Nagłówek|alias ą]]', ['WikiTarget', 'WikiAlias'])).toEqual([
      'WikiTarget:Strona#Nagłówek',
      'WikiAlias:alias ą',
    ]);
    expect(nodes('[[#^abc123]]', ['WikiTarget'])).toEqual(['WikiTarget:#^abc123']);
  });

  it('parses embeds', () => {
    expect(nodes('![[photo.png]]', ['Embed', 'WikiTarget'])).toEqual(['Embed:![[photo.png]]', 'WikiTarget:photo.png']);
    expect(nodes('![[Strona#^id1]]', ['Embed'])).toEqual(['Embed:![[Strona#^id1]]']);
  });

  it('ignores unclosed, empty and multi-line links', () => {
    expect(nodes('[[open', ['WikiLink'])).toEqual([]);
    expect(nodes('[[]]', ['WikiLink'])).toEqual([]);
    expect(nodes('[[a\nb]]', ['WikiLink'])).toEqual([]);
  });

  it('does not see links inside code', () => {
    expect(nodes('`[[x]]`', ['WikiLink'])).toEqual([]);
    expect(nodes('```\n[[x]]\n```', ['WikiLink'])).toEqual([]);
  });

  it('keeps standard markdown links', () => {
    expect(nodes('[a](b.md)', ['Link', 'WikiLink'])).toEqual(['Link:[a](b.md)']);
  });
});

describe('syntax: block refs', () => {
  it('parses ((id)) and !((id))', () => {
    expect(nodes('ref ((abc-123)) and !((xyz789))', ['BlockRef', 'BlockEmbed', 'BlockRefId'])).toEqual([
      'BlockRef:((abc-123))',
      'BlockRefId:abc-123',
      'BlockEmbed:!((xyz789))',
      'BlockRefId:xyz789',
    ]);
  });
  it('rejects invalid ids', () => {
    expect(nodes('((a b))', ['BlockRef'])).toEqual([]);
    expect(nodes('(())', ['BlockRef'])).toEqual([]);
  });
});

describe('syntax: tags', () => {
  it('parses Unicode tags', () => {
    expect(nodes('#praca i #życie/rodzina ok', ['Tag'])).toEqual(['Tag:#praca', 'Tag:#życie/rodzina']);
    expect(nodes('(#zadanie)', ['TagName'])).toEqual(['TagName:zadanie']);
  });
  it('needs a non-digit and a valid predecessor', () => {
    expect(nodes('issue #1 and a#b', ['Tag'])).toEqual([]);
    expect(nodes('#2026-plan', ['Tag'])).toEqual(['Tag:#2026-plan']);
  });
  it('parses #[[multi word]]', () => {
    expect(nodes('#[[dom i ogród]] x', ['Tag', 'TagName'])).toEqual(['Tag:#[[dom i ogród]]', 'TagName:dom i ogród']);
  });
  it('is not a heading and not inside URLs or code', () => {
    expect(nodes('# Tytuł', ['Tag'])).toEqual([]);
    expect(nodes('https://example.com/#frag', ['Tag'])).toEqual([]);
    expect(nodes('`#tag`', ['Tag'])).toEqual([]);
    expect(nodes('[x](http://a.pl/#b)', ['Tag'])).toEqual([]);
  });
  it('tagBodyLength', () => {
    expect(tagBodyLength('ąę_1 x')).toBe(4);
    expect(tagBodyLength('123')).toBe(0);
    expect(tagBodyLength('a/')).toBe(1);
  });
});

describe('syntax: highlight, comments, block ids', () => {
  it('parses ==highlight==', () => {
    expect(nodes('to jest ==ważne== tak', ['Highlight'])).toEqual(['Highlight:==ważne==']);
  });
  it('parses inline %%comments%%', () => {
    expect(nodes('a %%ukryte%% b', ['Comment'])).toEqual(['Comment:%%ukryte%%']);
  });
  it('parses block comments', () => {
    const doc = 'przed\n\n%%\nlinia 1\n\nlinia 2\n%%\npo';
    expect(nodes(doc, ['CommentBlock'])).toEqual(['CommentBlock:%%\nlinia 1\n\nlinia 2\n%%']);
  });
  it('parses ^block-id at line end', () => {
    expect(nodes('- zadanie ^abc-12\n- inne', ['BlockId'])).toEqual(['BlockId:^abc-12']);
    expect(nodes('a ^id b', ['BlockId'])).toEqual([]);
    expect(nodes('x^id', ['BlockId'])).toEqual([]);
  });
});

describe('syntax: task tokens', () => {
  it('parses date tokens with optional time', () => {
    const doc = '- [ ] Zadzwoń 📅 2026-10-15 ⏳ 2026-10-14 09:30 ✅ 2026-10-14';
    expect(nodes(doc, ['TaskToken'])).toEqual([
      'TaskToken:📅 2026-10-15',
      'TaskToken:⏳ 2026-10-14 09:30',
      'TaskToken:✅ 2026-10-14',
    ]);
  });
  it('parses reminders, alarms and recurrence', () => {
    const doc = 'Leki ⏰ 2026-10-15 08:00 🔁 every day ⏰! 2026-10-16 06:30 #zdrowie';
    expect(nodes(doc, ['TaskTokenMark', 'TaskTokenValue'])).toEqual([
      'TaskTokenMark:⏰',
      'TaskTokenValue:2026-10-15 08:00',
      'TaskTokenMark:🔁',
      'TaskTokenValue:every day',
      'TaskTokenMark:⏰!',
      'TaskTokenValue:2026-10-16 06:30',
    ]);
    expect(nodes(doc, ['Tag'])).toEqual(['Tag:#zdrowie']);
  });
  it('parses priority emoji', () => {
    expect(nodes('- [ ] a ⏫ b', ['PriorityToken'])).toEqual(['PriorityToken:⏫']);
  });
  it('ignores emoji without a date', () => {
    expect(nodes('📅 jutro', ['TaskToken'])).toEqual([]);
  });
});

describe('syntax: properties and frontmatter', () => {
  it('parses key:: value lines with inline content in values', () => {
    const doc = 'status:: w toku\nprojekt:: [[Dom]], #remont';
    expect(nodes(doc, ['PropertyLine', 'PropertyKey', 'PropertyValue', 'WikiLink', 'Tag'])).toEqual([
      'PropertyLine:status:: w toku',
      'PropertyKey:status',
      'PropertyValue:w toku',
      'PropertyLine:projekt:: [[Dom]], #remont',
      'PropertyKey:projekt',
      'PropertyValue:[[Dom]], #remont',
      'WikiLink:[[Dom]]',
      'Tag:#remont',
    ]);
  });
  it('parses properties inside list items', () => {
    const doc = '- zadanie\n  priorytet:: wysoki';
    expect(nodes(doc, ['PropertyKey'])).toEqual(['PropertyKey:priorytet']);
  });
  it('does not treat text in the middle of a line as a property', () => {
    expect(nodes('to nie key:: value', ['PropertyLine'])).toEqual([]);
  });
  it('parses YAML frontmatter instead of a setext heading', () => {
    const doc = '---\ntytuł: Dom\ntags: [a, b]\n---\n# Nagłówek';
    expect(nodes(doc, ['Frontmatter', 'FrontmatterContent', 'SetextHeading2', 'ATXHeading1'])).toEqual([
      'Frontmatter:---\ntytuł: Dom\ntags: [a, b]\n---',
      'FrontmatterContent:tytuł: Dom\ntags: [a, b]',
      'ATXHeading1:# Nagłówek',
    ]);
  });
  it('only at the start of the document', () => {
    expect(nodes('a\n\n---\nb: c\n---', ['Frontmatter'])).toEqual([]);
  });
});

describe('syntax: task states', () => {
  it('[/] [-] [>] are tasks like [ ] and [x]', () => {
    expect(nodes('- [/] w toku\n- [-] anulowane\n- [>] odłożone\n- [x] zrobione', ['TaskMarker'])).toEqual([
      'TaskMarker:[/]',
      'TaskMarker:[-]',
      'TaskMarker:[>]',
      'TaskMarker:[x]',
    ]);
    expect(nodes('[/] not in a list', ['TaskMarker'])).toEqual([]);
  });
});

describe('syntax: highlighting classes', () => {
  function classes(doc: string): string[] {
    const out: string[] = [];
    highlightTree(parser.parse(doc), brulionHighlightStyle, (from, to, cls) => out.push(`${doc.slice(from, to)}=${cls}`));
    return out;
  }
  it('styles block refs, task markers and keeps list text plain', () => {
    const c = classes('- [/] Płytki ((plytki)) #tag');
    expect(c).toContain('[/]=b-md-taskmarker');
    expect(c).toContain('plytki=b-md-blockref');
    expect(c).toContain('#tag=b-md-tag');
    expect(c.some((x) => x.startsWith(' Płytki'))).toBe(false);
  });
});

describe('syntax: GFM still works', () => {
  it('tables, tasks, strikethrough', () => {
    const doc = '| a | b |\n|---|---|\n| 1 | =A1*2 |\n\n- [x] done ~~old~~';
    const names = nodes(doc).map((n) => n.split(':')[0]);
    expect(names).toContain('Table');
    expect(names).toContain('Task');
    expect(names).toContain('Strikethrough');
  });
  it('no superscript / emoji shortcodes', () => {
    const names = nodes('x^2^ :smile:').map((n) => n.split(':')[0]);
    expect(names).not.toContain('Superscript');
    expect(names).not.toContain('Emoji');
  });
});
