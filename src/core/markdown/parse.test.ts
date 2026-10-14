import { describe, expect, it } from 'vitest';
import { DEFAULT_VAULT_CONFIG, type Block, type ParsedNote } from '../types';
import { blockAtLine, parseNote } from './parse';

/** A realistic note mixing Obsidian (frontmatter, Tasks emoji, callouts) and Logseq syntax. */
const FIXTURE = `---
title: Tydzień 42
tags: [projekt, "Dom/Remont"]
aliases:
  - Remont łazienki
  - Łazienka
created: 2026-10-14
related: "[[Budżet]]"
---
type:: plan
status:: [[W toku]]

# Remont łazienki #dom

Plan prac na ten tydzień. Zobacz [[Budżet#Koszty|koszty]] i [instrukcję](docs/Instrukcja%20montażu.md#Krok%202). ^plan

## Zadania

- [ ] Kupić płytki 📅 2026-10-16 ⏫ #zakupy/budowlane
  - [x] Zmierzyć ścianę ✅ 2026-10-13
  - [/] Wybrać fugę ⏳ 2026-10-15 10:00 🛫 2026-10-14
  - [-] Zamówić kuriera ❌ 2026-10-12
  - [>] Zapytać sąsiada
- [ ] Zapłacić fakturę 📅 2026-10-20 🔁 co miesiąc ⏰! 2026-10-20 08:00 ^faktura
  kwota:: 1200 zł
  tags:: [[Finanse]], #rachunki
- TODO [#B] Zadzwonić do hydraulika
  SCHEDULED: <2026-10-15 Thu 09:00 .+1w>
  id:: 64f0a1b2-0000-4c4c-8888-123456789abc
- LATER Przeczytać ((faktura)) i !((plan))
- Spotkanie z ekipą ⏰ 2026-10-15 (@2026-10-15 07:30)

> [!warning] Uwaga
> - [ ] Wyłączyć wodę ⏰ 2026-10-16 07:00
> Tekst z #ostrzeżenie

\`\`\`calc
płytki = 12 * 45 zł
- [ ] nie zadanie #nie-tag [[Nie link]] ⏰ 2026-10-15 10:00
\`\`\`

| Pozycja | Cena |
|---|---|
| [[Płytki]] | =120*12 |
^koszty

%% Ukryty [[Komentarz]] #ukryty %%
Odnośnik https://sklep.example.com/plytki?id=5. \`#kod\` i \\#escaped.

---
Stopka
`;

const PATH = 'Projekty/Remont łazienki.md';

function checkInvariants(note: ParsedNote, content: string): void {
  const lineCount = content.split(/\r\n|\r|\n/).length;
  const visit = (b: Block, parent: Block | null): void => {
    expect(b.line).toBeGreaterThanOrEqual(0);
    expect(b.line).toBeLessThanOrEqual(b.ownEndLine);
    expect(b.ownEndLine).toBeLessThanOrEqual(b.endLine);
    expect(b.endLine).toBeLessThan(lineCount);
    if (parent) {
      expect(b.line).toBeGreaterThanOrEqual(parent.line);
      expect(b.endLine).toBeLessThanOrEqual(parent.endLine);
      expect(b.depth).toBeGreaterThan(parent.depth);
    }
    for (const c of b.children) visit(c, b);
  };
  for (const b of note.blocks) visit(b, null);
  for (const l of note.links) {
    expect(l.from).toBeLessThan(l.to);
    expect(l.to).toBeLessThanOrEqual(content.length);
  }
}

describe('parseNote — realistic mixed note', () => {
  const note = parseNote(PATH, FIXTURE);

  it('reads title, frontmatter, page properties and aliases', () => {
    expect(note.title).toBe('Remont łazienki');
    expect(note.path).toBe(PATH);
    expect(note.frontmatter).toEqual({
      title: 'Tydzień 42',
      tags: ['projekt', 'Dom/Remont'],
      aliases: ['Remont łazienki', 'Łazienka'],
      created: '2026-10-14',
      related: '[[Budżet]]',
    });
    expect(note.properties).toEqual({
      title: 'Tydzień 42',
      tags: ['projekt', 'Dom/Remont'],
      aliases: ['Remont łazienki', 'Łazienka'],
      created: '2026-10-14',
      related: '[[Budżet]]',
      type: 'plan',
      status: ['W toku'],
    });
    expect(note.aliases).toEqual(['Remont łazienki', 'Łazienka']);
    expect(note.journalDate).toBeUndefined();
  });

  it('collects page tags (frontmatter + blocks), lower-cased, never from code/comments/escapes', () => {
    expect(note.tags).toEqual(['projekt', 'dom/remont', 'dom', 'zakupy/budowlane', 'rachunki', 'finanse', 'ostrzeżenie']);
  });

  it('collects links in document order, skipping code and comments', () => {
    const summary = note.links.map((l) => [l.kind, l.target, l.heading ?? l.blockId ?? '', l.display ?? '']);
    expect(summary).toEqual([
      ['wiki', 'Budżet', '', ''],
      ['wiki', 'W toku', '', ''],
      ['wiki', 'Budżet', 'Koszty', 'koszty'],
      ['markdown', 'docs/Instrukcja montażu.md', 'Krok 2', 'instrukcję'],
      ['wiki', 'Finanse', '', ''],
      ['blockref', 'faktura', 'faktura', ''],
      ['blockembed', 'plan', 'plan', ''],
      ['wiki', 'Płytki', '', ''],
      ['url', 'https://sklep.example.com/plytki?id=5', '', ''],
    ]);
    for (const l of note.links) {
      const src = FIXTURE.slice(l.from, l.to);
      expect(src.length).toBeGreaterThan(0);
      expect(FIXTURE.split('\n')[l.line]).toContain(src);
    }
    expect(note.links[0]!.line).toBe(7);
  });

  it('builds the outline with depths and line ranges', () => {
    const roots = note.blocks.map((b) => `${b.kind}@${b.line}`);
    expect(roots).toEqual([
      'frontmatter@0',
      'frontmatter@9',
      'heading@12',
      'paragraph@14',
      'heading@16',
      'listItem@18',
      'listItem@23',
      'listItem@26',
      'listItem@29',
      'listItem@30',
      'callout@32',
      'code@36',
      'table@41',
      'paragraph@47',
      'hr@49',
      'paragraph@50',
    ]);
    const first = note.blocks[5]!;
    expect(first).toMatchObject({ line: 18, ownEndLine: 18, endLine: 22, depth: 0 });
    expect(first.children.map((c) => [c.line, c.depth])).toEqual([
      [19, 1],
      [20, 1],
      [21, 1],
      [22, 1],
    ]);
    expect(note.allBlocks.map((b) => b.line)).toEqual(note.allBlocks.map((b) => b.line).sort((a, b) => a - b));
    checkInvariants(note, FIXTURE);
  });

  it('cleans block text (markers, tokens, ids and properties removed)', () => {
    const byLine = (l: number) => note.allBlocks.find((b) => b.line === l)!;
    expect(byLine(12).text).toBe('Remont łazienki #dom');
    expect(byLine(14).text).toBe(
      'Plan prac na ten tydzień. Zobacz [[Budżet#Koszty|koszty]] i [instrukcję](docs/Instrukcja%20montażu.md#Krok%202).',
    );
    expect(byLine(18).text).toBe('Kupić płytki #zakupy/budowlane');
    expect(byLine(23).text).toBe('Zapłacić fakturę');
    expect(byLine(26).text).toBe('Zadzwonić do hydraulika');
    expect(byLine(30).text).toBe('Spotkanie z ekipą');
    expect(byLine(32).text).toBe('Uwaga');
    expect(byLine(36).text).toBe('płytki = 12 * 45 zł\n- [ ] nie zadanie #nie-tag [[Nie link]] ⏰ 2026-10-15 10:00');
    expect(byLine(36).lang).toBe('calc');
    expect(byLine(41).text).toBe('| Pozycja | Cena |\n|---|---|\n| [[Płytki]] | =120*12 |');
    expect(byLine(23).raw).toBe(FIXTURE.split('\n').slice(23, 26).join('\n'));
  });

  it('reads tasks (Obsidian Tasks + Logseq)', () => {
    expect(note.tasks).toEqual([
      { status: 'todo', marker: ' ', text: 'Kupić płytki #zakupy/budowlane', line: 18, due: '2026-10-16', priority: 'high' },
      { status: 'done', marker: 'x', text: 'Zmierzyć ścianę', line: 19, doneDate: '2026-10-13' },
      { status: 'doing', marker: '/', text: 'Wybrać fugę', line: 20, scheduled: '2026-10-15T10:00', start: '2026-10-14' },
      { status: 'cancelled', marker: '-', text: 'Zamówić kuriera', line: 21, cancelledDate: '2026-10-12' },
      { status: 'deferred', marker: '>', text: 'Zapytać sąsiada', line: 22 },
      { status: 'todo', marker: ' ', text: 'Zapłacić fakturę', line: 23, due: '2026-10-20', recurrence: 'every month' },
      {
        status: 'todo',
        marker: 'TODO',
        text: 'Zadzwonić do hydraulika',
        line: 26,
        scheduled: '2026-10-15T09:00',
        recurrence: 'every week when done',
        priority: 'medium',
      },
      { status: 'todo', marker: 'LATER', text: 'Przeczytać ((faktura)) i !((plan))', line: 29 },
      { status: 'todo', marker: ' ', text: 'Wyłączyć wodę\nTekst z #ostrzeżenie'.split('\n')[0], line: 33 },
    ]);
  });

  it('reads reminders, alarms and compat reminders with recurrence', () => {
    expect(note.reminders).toEqual([
      { at: '2026-10-20T08:00', alarm: true, text: 'Zapłacić fakturę', line: 23, recurrence: 'every month' },
      { at: '2026-10-15T09:00', alarm: false, text: 'Spotkanie z ekipą', line: 30 },
      { at: '2026-10-15T07:30', alarm: false, text: 'Spotkanie z ekipą', line: 30 },
      { at: '2026-10-16T07:00', alarm: false, text: 'Wyłączyć wodę\nTekst z #ostrzeżenie', line: 33 },
    ]);
  });

  it('reads block properties, block ids and Logseq ids', () => {
    const faktura = note.blockIds['faktura']!;
    expect(faktura.line).toBe(23);
    expect(faktura.properties).toEqual({ kwota: '1200 zł', tags: ['Finanse', 'rachunki'] });
    expect(faktura.tags).toEqual(['rachunki', 'finanse']);
    expect(note.blockIds['plan']!.kind).toBe('paragraph');
    expect(note.blockIds['64f0a1b2-0000-4c4c-8888-123456789abc']!.line).toBe(26);
    expect(note.blockIds['koszty']!.kind).toBe('table');
    expect(Object.keys(note.blockIds).sort()).toEqual(['64f0a1b2-0000-4c4c-8888-123456789abc', 'faktura', 'koszty', 'plan']);
    expect(note.allBlocks.find((b) => b.kind === 'callout')!.properties).toEqual({ calloutType: 'warning' });
  });

  it('lists top-level headings', () => {
    expect(note.headings).toEqual([
      { level: 1, text: 'Remont łazienki #dom', line: 12 },
      { level: 2, text: 'Zadania', line: 16 },
    ]);
  });

  it('gives identical results for CRLF content (offsets shift, lines do not)', () => {
    const crlf = FIXTURE.replace(/\n/g, '\r\n');
    const n2 = parseNote(PATH, crlf);
    expect(n2.tasks).toEqual(note.tasks);
    expect(n2.reminders).toEqual(note.reminders);
    expect(n2.tags).toEqual(note.tags);
    expect(n2.allBlocks.map((b) => [b.kind, b.line, b.endLine, b.text])).toEqual(
      note.allBlocks.map((b) => [b.kind, b.line, b.endLine, b.text]),
    );
    expect(n2.links.map((l) => crlf.slice(l.from, l.to))).toEqual(note.links.map((l) => FIXTURE.slice(l.from, l.to)));
    expect(n2.links.map((l) => l.line)).toEqual(note.links.map((l) => l.line));
    checkInvariants(n2, crlf);
  });
});

describe('parseNote — focused features', () => {
  it('handles empty and whitespace-only notes', () => {
    for (const s of ['', '\n\n', '   \r\n\t']) {
      const n = parseNote('Pusta.md', s);
      expect(n.blocks).toEqual([]);
      expect(n.title).toBe('Pusta');
      expect(n.tags).toEqual([]);
    }
  });

  it('sets journalDate for journal pages using the config', () => {
    expect(parseNote('journals/2026-10-14.md', '- x').journalDate).toBe('2026-10-14');
    const cfg = { ...DEFAULT_VAULT_CONFIG, journalsFolder: 'Dziennik', journalFileFormat: 'yyyy_MM_dd' };
    expect(parseNote('Dziennik/2026_10_14.md', '', cfg).journalDate).toBe('2026-10-14');
    expect(parseNote('journals/2026_10_14.md', '', cfg).journalDate).toBeUndefined();
  });

  it('uses config.defaultReminderTime for date-only reminders', () => {
    const cfg = { ...DEFAULT_VAULT_CONFIG, defaultReminderTime: '7:15' };
    expect(parseNote('a.md', 'x ⏰ 2026-10-15', cfg).reminders[0]!.at).toBe('2026-10-15T07:15');
    const bad = { ...DEFAULT_VAULT_CONFIG, defaultReminderTime: 'rano' };
    expect(parseNote('a.md', 'x ⏰ 2026-10-15', bad).reminders[0]!.at).toBe('2026-10-15T09:00');
  });

  it('finds reminders in any block but never in code', () => {
    const src = '# Spotkanie ⏰ 2026-10-15 10:00\n\nAkapit\ndruga linia ⏰! 2026-10-16 06:30 🔁 every day\n\n`⏰ 2026-10-17 09:00`\n\n```\n⏰ 2026-10-18 09:00\n```';
    const n = parseNote('a.md', src);
    expect(n.reminders).toEqual([
      { at: '2026-10-15T10:00', alarm: false, text: 'Spotkanie', line: 0 },
      { at: '2026-10-16T06:30', alarm: true, text: 'Akapit\ndruga linia', line: 3, recurrence: 'every day' },
    ]);
  });

  it('reads Polish tags and nested tags', () => {
    const n = parseNote('a.md', 'Lista #zakupy/spożywcze i #Żółw oraz #[[Lista życzeń]]');
    expect(n.tags).toEqual(['zakupy/spożywcze', 'żółw', 'lista życzeń']);
    expect(n.blocks[0]!.tags).toEqual(['zakupy/spożywcze', 'żółw', 'lista życzeń']);
  });

  it('reads frontmatter tags in all common forms', () => {
    expect(parseNote('a.md', '---\ntags: a, b c\n---\n').tags).toEqual(['a', 'b', 'c']);
    expect(parseNote('a.md', '---\ntags: "#a #b"\n---\n').tags).toEqual(['a', 'b']);
    // In YAML an unquoted " #" starts a comment.
    expect(parseNote('a.md', '---\ntags: a, #b\n---\n').tags).toEqual(['a']);
    expect(parseNote('a.md', '---\ntag: [X, "#y"]\n---\n').tags).toEqual(['x', 'y']);
    expect(parseNote('a.md', '---\ntags:\n  - Praca\n  - praca\n---\n').tags).toEqual(['praca']);
  });

  it('reads aliases from frontmatter strings and Logseq alias::', () => {
    expect(parseNote('a.md', '---\nalias: Jeden, Dwa\n---\n').aliases).toEqual(['Jeden', 'Dwa']);
    expect(parseNote('a.md', 'alias:: [[Trzy]], Cztery\n\n- x').aliases).toEqual(['Trzy', 'Cztery']);
  });

  it('reads Logseq page properties and their tags', () => {
    const n = parseNote('a.md', 'tags:: [[Książki]], fantastyka\nautor:: [[Lem]]\n\n- Solaris');
    expect(n.properties).toEqual({ tags: ['Książki', 'fantastyka'], autor: ['Lem'] });
    expect(n.tags).toEqual(['książki', 'fantastyka']);
    expect(n.blocks[0]!.kind).toBe('frontmatter');
    expect(n.links.map((l) => l.target)).toEqual(['Książki', 'Lem']);
  });

  it('parses property values (lists only for refs)', () => {
    const n = parseNote('a.md', '- item\n  a:: [[X]], #y\n  b:: plain, text\n  c::\n  D-e_f:: 5\n  std::vector is not a property');
    expect(n.blocks[0]!.properties).toEqual({ a: ['X', 'y'], b: 'plain, text', c: '', 'd-e_f': '5' });
    expect(n.blocks[0]!.text).toBe('item\nstd::vector is not a property');
  });

  it('keeps block ids from first or last line and strips them from text', () => {
    const n = parseNote('a.md', '- item ^first\n  second line\n\npara line 1\npara line 2 ^last\n\n## Head ## ^h1');
    expect(Object.keys(n.blockIds)).toEqual(['first', 'last', 'h1']);
    expect(n.blockIds['first']!.text).toBe('item\nsecond line');
    expect(n.blockIds['last']!.text).toBe('para line 1\npara line 2');
    expect(n.blockIds['h1']!.text).toBe('Head');
    expect(n.headings[0]).toEqual({ level: 2, text: 'Head', line: 6 });
  });

  it('keeps the first block for duplicate ids', () => {
    const n = parseNote('a.md', 'a ^dup\n\nb ^dup');
    expect(n.blockIds['dup']!.text).toBe('a');
  });

  it('strips Logseq drawers and planning lines from text', () => {
    const src = '- DONE Zrobione\n  :LOGBOOK:\n  CLOCK: [2026-10-14 Wed 10:00]--[2026-10-14 Wed 11:00] =>  01:00:00\n  :END:\n  DEADLINE: <2026-10-20 Tue>\n  notatka';
    const n = parseNote('a.md', src);
    expect(n.blocks[0]!.text).toBe('Zrobione\nnotatka');
    expect(n.tasks[0]).toMatchObject({ status: 'done', marker: 'DONE', due: '2026-10-20' });
    expect(n.links).toEqual([]);
  });

  it('prefers emoji dates over Logseq planning and emoji priority over [#A]', () => {
    const n = parseNote('a.md', '- TODO [#A] x 📅 2026-10-01 🔽\n  DEADLINE: <2026-10-20 Tue +1m>');
    expect(n.tasks[0]).toMatchObject({ due: '2026-10-01', priority: 'low', recurrence: 'every month' });
  });

  it('only treats list items as tasks', () => {
    const n = parseNote('a.md', '[ ] not a task\n\nTODO not a task\n\n1. [x] ordered task');
    expect(n.tasks.map((t) => t.text)).toEqual(['ordered task']);
  });

  it('treats unknown checkbox characters as todo and keeps the marker', () => {
    const n = parseNote('a.md', '- [?] question\n- [X] upper');
    expect(n.tasks.map((t) => [t.status, t.marker])).toEqual([
      ['todo', '?'],
      ['done', 'X'],
    ]);
  });

  it('ignores tasks and links hidden in %% comments', () => {
    const n = parseNote('a.md', 'tekst\n\n%%\n- [ ] ukryte ⏰ 2026-10-15 09:00 [[X]]\n%%\n\n- [ ] widoczne');
    expect(n.tasks.map((t) => t.text)).toEqual(['widoczne']);
    expect(n.reminders).toEqual([]);
    expect(n.links).toEqual([]);
  });

  it('keeps inline markdown in text and does not strip tokens inside code spans', () => {
    const n = parseNote('a.md', '- [ ] **Ważne** `📅 2026-10-15` [[Link]] 📅 2026-10-16');
    expect(n.tasks[0]).toMatchObject({ text: '**Ważne** `📅 2026-10-15` [[Link]]', due: '2026-10-16' });
  });

  it('reads tasks inside callouts with correct lines and depths', () => {
    const n = parseNote('a.md', '> [!todo] Dziś\n> - [ ] a\n>   - [x] b ✅ 2026-10-14');
    expect(n.tasks.map((t) => [t.text, t.line, t.status])).toEqual([
      ['a', 1, 'todo'],
      ['b', 2, 'done'],
    ]);
    expect(n.allBlocks.map((b) => b.depth)).toEqual([0, 1, 2]);
  });

  it('extracts wiki links from frontmatter values', () => {
    const src = '---\nup: "[[Rodzic]]"\nlist: [[Dziecko]]\n---\n';
    const n = parseNote('a.md', src);
    expect(n.links.map((l) => [l.target, l.line, src.slice(l.from, l.to)])).toEqual([
      ['Rodzic', 1, '[[Rodzic]]'],
      ['Dziecko', 2, '[[Dziecko]]'],
    ]);
    expect(n.properties['list']).toEqual(['[[Dziecko]]']);
  });

  it('blockAtLine returns the innermost block', () => {
    const n = parseNote('a.md', '- a\n  - b\n  more a\n\npara');
    expect(blockAtLine(n, 1)!.text).toBe('b');
    expect(blockAtLine(n, 2)!.text).toBe('a\nmore a');
    expect(blockAtLine(n, 3)).toBeNull();
    expect(blockAtLine(n, 4)!.kind).toBe('paragraph');
  });
});

describe('parseNote — robustness', () => {
  it('never throws on random Markdown-ish input and keeps invariants', () => {
    const pieces = ['- ', '  ', '\t', '[ ]', '[x]', '#', '##', '>', '[!note]', '```', '~~~', '|', '---', '%%', '`', '[[', ']]', '((', '))', '^id', '::', 'TODO ', '📅 2026-10-15', '⏰ 2026-10-15 09:00', '🔁 every day', '<div>', '\n', '\n', '\r\n', 'zażółć', 'x', ' ', '1. ', '\\', '<!--', '-->', '$'];
    let seed = 42;
    const rnd = (n: number): number => {
      seed = (seed * 1103515245 + 12345) % 2147483648;
      return seed % n;
    };
    for (let k = 0; k < 300; k++) {
      let s = '';
      const len = 1 + rnd(120);
      for (let i = 0; i < len; i++) s += pieces[rnd(pieces.length)];
      let note: ParsedNote | undefined;
      expect(() => (note = parseNote('fuzz.md', s))).not.toThrow();
      checkInvariants(note!, s);
    }
  });

  it('handles binary-ish and huge single-line content', () => {
    expect(() => parseNote('a.md', '\u0000�\uD800'.repeat(1000))).not.toThrow();
    expect(() => parseNote('a.md', '#'.repeat(100000))).not.toThrow();
    expect(() => parseNote('a.md', '[['.repeat(50000))).not.toThrow();
  });

  it('parses a 5000-line note in under 300 ms', () => {
    const chunk = [
      '## Sekcja',
      '- [ ] Zadanie [[Strona]] #tag/pod 📅 2026-10-15 ⏫ ^id{n}',
      '  - podpunkt z `kodem` i https://example.com/x',
      '  - TODO Logseq [#A] zadanie',
      '    SCHEDULED: <2026-10-15 Thu 09:00 .+1w>',
      'Akapit z ((abc123)) i [link](Strona%20B.md) ⏰ 2026-10-15 09:00',
      '> [!note] Uwaga',
      '> - [x] w callout ✅ 2026-10-14',
      '```js',
      'const x = 1; // [[nie]]',
      '```',
      '| a | b |',
      '|---|---|',
      '| 1 | =A1 |',
      '',
      'key:: value',
      '',
    ];
    const lines: string[] = [];
    for (let n = 0; lines.length < 5000; n++) for (const l of chunk) lines.push(l.replace('{n}', String(n)));
    const src = lines.slice(0, 5000).join('\n');
    // Warm up the JIT, then take the best of a few runs. The parser's own cost is measured as
    // process CPU time where available, so a busy machine (parallel test workers, other
    // processes) does not make the test flaky; wall-clock time is the fallback.
    for (let k = 0; k < 3; k++) parseNote('perf.md', src);
    let best = Infinity;
    let note: ParsedNote | undefined;
    // Best of 10: the Pi is often shared with other heavy processes; the threshold is unchanged.
    for (let k = 0; k < 10; k++) {
      const t0 = performance.now();
      const c0 = typeof process !== 'undefined' && process.cpuUsage ? process.cpuUsage() : null;
      note = parseNote('perf.md', src);
      const wall = performance.now() - t0;
      const used = c0 ? process.cpuUsage(c0) : null;
      const cpu = used ? (used.user + used.system) / 1000 : Infinity;
      best = Math.min(best, wall, cpu);
    }
    expect(note!.tasks.length).toBeGreaterThan(800);
    expect(note!.allBlocks.length).toBeGreaterThan(2500);
    expect(best).toBeLessThan(300);
  }, 120_000);
});

describe('parseNote — review regressions', () => {
  it('recognises a tag right after a blockquote marker', () => {
    expect(parseNote('a.md', '>#cytat tekst').tags).toEqual(['cytat']);
    expect(parseNote('a.md', '> [!note]\n>#uwaga').tags).toEqual(['uwaga']);
  });

  it('parses list items and tasks inside plain blockquotes as child blocks', () => {
    const src = 'Wstęp\n\n> Cytat #ważne\n> - [ ] zadanie 📅 2026-10-15 #praca\n>   - [x] pod ✅ 2026-10-14\n> ~~~\n> #nie [[Nie]] ⏰ 2026-10-15 09:00\n> ~~~\n\nPo';
    const n = parseNote('a.md', src);
    expect(n.tasks.map((t) => [t.text, t.line, t.status])).toEqual([
      ['zadanie #praca', 3, 'todo'],
      ['pod', 4, 'done'],
    ]);
    expect(n.tasks[0]!.due).toBe('2026-10-15');
    const quote = n.blocks[1]!;
    expect(quote).toMatchObject({ kind: 'quote', line: 2, endLine: 7, ownEndLine: 2, depth: 0, text: '' });
    expect(quote.children.map((b) => [b.kind, b.line, b.depth])).toEqual([
      ['paragraph', 2, 1],
      ['listItem', 3, 1],
      ['code', 5, 1],
    ]);
    expect(quote.children[1]!.children[0]).toMatchObject({ kind: 'listItem', line: 4, depth: 2 });
    expect(n.tags.sort()).toEqual(['praca', 'ważne']);
    expect(n.links).toEqual([]);
    expect(n.reminders).toEqual([]);
    expect(n.blocks[2]).toMatchObject({ kind: 'paragraph', line: 9 });
  });

  it('keeps quotes without nested structure as single leaf blocks', () => {
    const n = parseNote('a.md', '> linia 1\n> linia 2 ^cyt\n>\n> akapit 2');
    expect(n.blocks).toHaveLength(1);
    expect(n.blocks[0]).toMatchObject({ kind: 'quote', line: 0, endLine: 3, ownEndLine: 3, children: [] });
  });

  it('does not let %% inside a fenced code block of a list item hide the rest of the note', () => {
    const src = '- ```mermaid\n  %% komentarz diagramu\n  graph TD\n  ```\n- [ ] widoczne #t ⏰ 2026-10-15 08:00\n- koniec %%c%%';
    const n = parseNote('a.md', src);
    expect(n.tasks.map((t) => t.text)).toEqual(['widoczne #t']);
    expect(n.tags).toEqual(['t']);
    expect(n.reminders.map((r) => r.at)).toEqual(['2026-10-15T08:00']);
    expect(n.blocks.map((b) => b.line)).toEqual([0, 4, 5]);
  });

  it('does not let %% inside fenced code in a callout hide later tasks', () => {
    const src = '> [!info] Diagram\n> ```mermaid\n> %% komentarz\n> graph TD\n> ```\n\n- [ ] zadanie po callout\n\n%% prawdziwy komentarz %%';
    const n = parseNote('a.md', src);
    expect(n.tasks.map((t) => t.text)).toEqual(['zadanie po callout']);
  });
});

describe('parseNote — maps are safe to index with any id or key (review regressions)', () => {
  it('blockIds and properties never expose Object.prototype members', () => {
    const n = parseNote('a.md', 'title:: T\n\n- a ^x1\n  key:: v');
    for (const k of ['constructor', 'toString', 'hasOwnProperty', 'valueOf', '__proto__']) {
      expect(n.blockIds[k], k).toBeUndefined();
      expect(n.properties[k], k).toBeUndefined();
      expect(n.allBlocks[1]!.properties[k], k).toBeUndefined();
    }
    expect(n.blockIds.x1?.kind).toBe('listItem');
    expect(n.properties).toEqual({ title: 'T' });
  });

  it('still finds blocks whose ids look like Object.prototype members', () => {
    const n = parseNote('a.md', '- a ^constructor\n- b ^toString\n  constructor:: 1\n  valueOf:: 2');
    // `string`-typed keys: TypeScript would type `.constructor` as Object's member.
    const get = <T>(m: Record<string, T>, k: string): T | undefined => m[k];
    expect(get(n.blockIds, 'constructor')?.line).toBe(0);
    expect(get(n.blockIds, 'toString')?.line).toBe(1);
    // Keys are lower-cased.
    expect(get(n.allBlocks[1]!.properties, 'constructor')).toBe('1');
    expect(get(n.allBlocks[1]!.properties, 'valueof')).toBe('2');
  });
});

describe('parseNote — hidden comments are not part of block text (review regressions)', () => {
  it('drops %%comments%% from block, task and reminder text but keeps code spans', () => {
    const n = parseNote('a.md', '- [ ] Zadzwonić %%numer w notatkach%% do mamy ⏰ 2026-10-15 09:00\n\nAkapit %%ukryty\nkomentarz%% widoczny `%%kod%%`');
    expect(n.tasks[0]!.text).toBe('Zadzwonić do mamy');
    expect(n.reminders[0]!.text).toBe('Zadzwonić do mamy');
    expect(n.allBlocks[1]!.text).toBe('Akapit\nwidoczny `%%kod%%`');
    expect(n.allBlocks[1]!.raw).toBe('Akapit %%ukryty\nkomentarz%% widoczny `%%kod%%`');
  });

  it('keeps the text of a task whose comment continues on the next line', () => {
    const n = parseNote('a.md', '- [x] zadanie ✅ 2026-10-14 %%notatka\n  ciąg%% dalej');
    expect(n.tasks[0]).toMatchObject({ text: 'zadanie', doneDate: '2026-10-14' });
    expect(n.allBlocks[0]!.text).toBe('zadanie\ndalej');
  });
});
