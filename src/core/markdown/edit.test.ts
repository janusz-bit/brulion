import { describe, expect, it } from 'vitest';
import { ensureBlockId, generateBlockId, listItemRange, setTaskStatus, toggleTask } from './edit';
import { parseNote } from './parse';

const TODAY = '2026-10-14';

describe('setTaskStatus — checkboxes', () => {
  it('sets each status character', () => {
    expect(setTaskStatus('- [ ] a', 0, 'doing', TODAY)).toBe('- [/] a');
    expect(setTaskStatus('- [ ] a', 0, 'deferred', TODAY)).toBe('- [>] a');
    expect(setTaskStatus('- [/] a', 0, 'todo', TODAY)).toBe('- [ ] a');
  });

  it('adds ✅ today when completing (before a trailing block id)', () => {
    expect(setTaskStatus('- [ ] a 📅 2026-10-15', 0, 'done', TODAY)).toBe('- [x] a 📅 2026-10-15 ✅ 2026-10-14');
    expect(setTaskStatus('- [ ] a ^id1', 0, 'done', TODAY)).toBe('- [x] a ✅ 2026-10-14 ^id1');
    expect(setTaskStatus('- [ ] a   ', 0, 'done', TODAY)).toBe('- [x] a ✅ 2026-10-14');
  });

  it('removes ✅ when leaving done and swaps ✅/❌ for cancelled', () => {
    expect(setTaskStatus('- [x] a ✅ 2026-10-13 ^id1', 0, 'todo', TODAY)).toBe('- [ ] a ^id1');
    expect(setTaskStatus('- [x] a ✅ 2026-10-13', 0, 'cancelled', TODAY)).toBe('- [-] a ❌ 2026-10-14');
    expect(setTaskStatus('- [-] a ❌ 2026-10-01 b', 0, 'done', TODAY)).toBe('- [x] a b ✅ 2026-10-14');
  });

  it('keeps an existing uppercase X and returns unchanged content for the same status', () => {
    const src = '- [X] a ✅ 2026-10-01';
    expect(setTaskStatus(src, 0, 'done', TODAY)).toBe(src);
  });

  it('works for ordered items, nested items and items inside callouts', () => {
    expect(setTaskStatus('1. [ ] a', 0, 'done', TODAY)).toBe('1. [x] a ✅ 2026-10-14');
    expect(setTaskStatus('- p\n\t- [ ] a', 1, 'done', TODAY)).toBe('- p\n\t- [x] a ✅ 2026-10-14');
    expect(setTaskStatus('> [!todo]\n> - [ ] a', 1, 'done', TODAY)).toBe('> [!todo]\n> - [x] a ✅ 2026-10-14');
  });

  it('targets the first line of the list item for any of its lines', () => {
    expect(setTaskStatus('- [ ] a\n  details\n  - child', 1, 'done', TODAY)).toBe('- [x] a ✅ 2026-10-14\n  details\n  - child');
  });

  it('does not touch tokens inside inline code', () => {
    expect(setTaskStatus('- [x] use `✅ 2026-01-01`', 0, 'todo', TODAY)).toBe('- [ ] use `✅ 2026-01-01`');
  });
});

describe('setTaskStatus — Logseq keywords', () => {
  it('maps statuses to keywords of the same workflow', () => {
    expect(setTaskStatus('- TODO a', 0, 'doing', TODAY)).toBe('- DOING a');
    expect(setTaskStatus('- LATER a', 0, 'doing', TODAY)).toBe('- NOW a');
    expect(setTaskStatus('- NOW a', 0, 'todo', TODAY)).toBe('- LATER a');
    expect(setTaskStatus('- TODO a', 0, 'cancelled', TODAY)).toBe('- CANCELED a ❌ 2026-10-14');
    expect(setTaskStatus('- TODO a', 0, 'deferred', TODAY)).toBe('- WAITING a');
    expect(setTaskStatus('- DOING [#A] a', 0, 'done', TODAY)).toBe('- DONE [#A] a ✅ 2026-10-14');
    expect(setTaskStatus('- DONE a ✅ 2026-10-01', 0, 'todo', TODAY)).toBe('- TODO a');
  });
});

describe('setTaskStatus — non-task lines', () => {
  it('turns list items and plain lines into tasks', () => {
    expect(setTaskStatus('  * Kup chleb', 0, 'todo', TODAY)).toBe('  * [ ] Kup chleb');
    expect(setTaskStatus('Kup chleb', 0, 'todo', TODAY)).toBe('- [ ] Kup chleb');
    expect(setTaskStatus('  wcięty', 0, 'doing', TODAY)).toBe('  - [/] wcięty');
    expect(setTaskStatus('-', 0, 'todo', TODAY)).toBe('- [ ] ');
    expect(setTaskStatus('', 0, 'todo', TODAY)).toBe('- [ ] ');
    expect(setTaskStatus('Zrobione', 0, 'done', TODAY)).toBe('- [x] Zrobione ✅ 2026-10-14');
    expect(setTaskStatus('> cytat', 0, 'todo', TODAY)).toBe('> - [ ] cytat');
  });

  it('leaves code, frontmatter and out-of-range lines unchanged', () => {
    const code = '```\n- [ ] x\n```';
    expect(setTaskStatus(code, 1, 'done', TODAY)).toBe(code);
    const fm = '---\na: 1\n---\n';
    expect(setTaskStatus(fm, 1, 'done', TODAY)).toBe(fm);
    expect(setTaskStatus('- [ ] a', 5, 'done', TODAY)).toBe('- [ ] a');
    expect(setTaskStatus('- [ ] a', -1, 'done', TODAY)).toBe('- [ ] a');
    expect(setTaskStatus('- [ ] a', 0.5, 'done', TODAY)).toBe('- [ ] a');
  });

  it('preserves CRLF and the rest of the document byte for byte', () => {
    const src = 'head\r\n- [ ] zadanie\r\ntail\r\n';
    expect(setTaskStatus(src, 1, 'done', TODAY)).toBe('head\r\n- [x] zadanie ✅ 2026-10-14\r\ntail\r\n');
  });
});

describe('setTaskStatus — recurring tasks (Obsidian Tasks semantics)', () => {
  it('inserts the next occurrence above with dates shifted relative to the due date', () => {
    const src = '  - [ ] Zapłać 📅 2026-10-15 ⏳ 2026-10-13 🛫 2026-10-12 🔁 every week ⏰ 2026-10-15 09:00 ➕ 2026-10-01 ^pay\n  - inne';
    expect(setTaskStatus(src, 0, 'done', TODAY)).toBe(
      [
        '  - [ ] Zapłać 📅 2026-10-22 ⏳ 2026-10-20 🛫 2026-10-19 🔁 every week ⏰ 2026-10-22 09:00 ➕ 2026-10-14',
        '  - [x] Zapłać 📅 2026-10-15 ⏳ 2026-10-13 🛫 2026-10-12 🔁 every week ⏰ 2026-10-15 09:00 ➕ 2026-10-01 ✅ 2026-10-14 ^pay',
        '  - inne',
      ].join('\n'),
    );
  });

  it('computes "when done" rules from today', () => {
    expect(setTaskStatus('- [ ] Podlać 🔁 every 3 days when done 📅 2026-10-01', 0, 'done', TODAY)).toBe(
      '- [ ] Podlać 🔁 every 3 days when done 📅 2026-10-17\n- [x] Podlać 🔁 every 3 days when done 📅 2026-10-01 ✅ 2026-10-14',
    );
  });

  it('understands Polish rules (normalised to canonical English) and keeps times', () => {
    expect(setTaskStatus('- [ ] Czynsz 📅 2026-01-31 10:00 🔁 co miesiąc', 0, 'done', '2026-01-30')).toBe(
      '- [ ] Czynsz 📅 2026-02-28 10:00 🔁 every month\n- [x] Czynsz 📅 2026-01-31 10:00 🔁 every month ✅ 2026-01-30',
    );
    expect(setTaskStatus('- [ ] a 🔁 co tydzień w poniedziałek, piątek', 0, 'doing', TODAY)).toBe('- [/] a 🔁 every week on Monday, Friday');
    expect(setTaskStatus('- [ ] a 🔁 kiedyś tam', 0, 'doing', TODAY)).toBe('- [/] a 🔁 kiedyś tam');
  });

  it('uses scheduled / start / reminder as reference when there is no due date', () => {
    expect(setTaskStatus('- [ ] a ⏳ 2026-10-14 🔁 every day', 0, 'done', TODAY).split('\n')[0]).toBe('- [ ] a ⏳ 2026-10-15 🔁 every day');
    expect(setTaskStatus('- [ ] a ⏰! 2026-10-14 06:30 🔁 every weekday', 0, 'done', TODAY).split('\n')[0]).toBe(
      '- [ ] a ⏰! 2026-10-15 06:30 🔁 every weekday',
    );
    expect(setTaskStatus('- [ ] a (@2026-10-16 07:00) 🔁 every week', 0, 'done', TODAY).split('\n')[0]).toBe(
      '- [ ] a (@2026-10-23 07:00) 🔁 every week',
    );
  });

  it('copies a recurring task without dates as is', () => {
    expect(setTaskStatus('- [ ] Rozciąganie 🔁 every day', 0, 'done', TODAY)).toBe(
      '- [ ] Rozciąganie 🔁 every day\n- [x] Rozciąganie 🔁 every day ✅ 2026-10-14',
    );
  });

  it('does not create a new instance for unparseable rules or when not completing', () => {
    expect(setTaskStatus('- [ ] a 🔁 every blue moon 📅 2026-10-15', 0, 'done', TODAY)).toBe(
      '- [x] a 🔁 every blue moon 📅 2026-10-15 ✅ 2026-10-14',
    );
    expect(setTaskStatus('- [ ] a 🔁 every day 📅 2026-10-15', 0, 'cancelled', TODAY)).toBe(
      '- [-] a 🔁 every day 📅 2026-10-15 ❌ 2026-10-14',
    );
    expect(setTaskStatus('- [x] a 🔁 every day 📅 2026-10-15 ✅ 2026-10-14', 0, 'done', TODAY)).toBe(
      '- [x] a 🔁 every day 📅 2026-10-15 ✅ 2026-10-14',
    );
  });

  it('turns a recurring non-task line into a task and copies it', () => {
    expect(setTaskStatus('- Stretch 🔁 every day 📅 2026-10-14', 0, 'done', TODAY)).toBe(
      '- [ ] Stretch 🔁 every day 📅 2026-10-15\n- [x] Stretch 🔁 every day 📅 2026-10-14 ✅ 2026-10-14',
    );
  });

  it('repeats Logseq tasks with SCHEDULED repeaters (planning lines copied and shifted)', () => {
    const src = '- TODO Zadzwoń\n  SCHEDULED: <2026-10-15 Thu 09:00 +1w>\n  - dziecko';
    expect(setTaskStatus(src, 0, 'done', TODAY)).toBe(
      [
        '- TODO Zadzwoń',
        '  SCHEDULED: <2026-10-22 Thu 09:00 +1w>',
        '- DONE Zadzwoń ✅ 2026-10-14',
        '  SCHEDULED: <2026-10-15 Thu 09:00 +1w>',
        '  - dziecko',
      ].join('\n'),
    );
    const whenDone = setTaskStatus('- LATER a\n  DEADLINE: <2026-10-01 Thu .+1w>', 0, 'done', TODAY);
    expect(whenDone.split('\n').slice(0, 2)).toEqual(['- LATER a', '  DEADLINE: <2026-10-21 Wed .+1w>']);
  });

  it('works inside callouts and keeps CRLF', () => {
    expect(setTaskStatus('> [!todo]\r\n> - [ ] x 🔁 every day 📅 2026-10-14\r\n', 1, 'done', TODAY)).toBe(
      '> [!todo]\r\n> - [ ] x 🔁 every day 📅 2026-10-15\r\n> - [x] x 🔁 every day 📅 2026-10-14 ✅ 2026-10-14\r\n',
    );
  });

  it('produces content that parses back to the expected tasks', () => {
    const out = setTaskStatus('- [ ] Faktura 📅 2026-10-20 🔁 every month ^f', 0, 'done', TODAY);
    const tasks = parseNote('a.md', out).tasks;
    expect(tasks.map((t) => [t.status, t.due, t.doneDate])).toEqual([
      ['todo', '2026-11-20', undefined],
      ['done', '2026-10-20', '2026-10-14'],
    ]);
    expect(Object.keys(parseNote('a.md', out).blockIds)).toEqual(['f']);
  });
});

describe('toggleTask', () => {
  it('toggles open tasks to done and closed tasks to todo', () => {
    expect(toggleTask('- [ ] a', 0, TODAY)).toBe('- [x] a ✅ 2026-10-14');
    expect(toggleTask('- [/] a', 0, TODAY)).toBe('- [x] a ✅ 2026-10-14');
    expect(toggleTask('- [>] a', 0, TODAY)).toBe('- [x] a ✅ 2026-10-14');
    expect(toggleTask('- [x] a ✅ 2026-10-01', 0, TODAY)).toBe('- [ ] a');
    expect(toggleTask('- [-] a ❌ 2026-10-01', 0, TODAY)).toBe('- [ ] a');
    expect(toggleTask('- WAITING a', 0, TODAY)).toBe('- DONE a ✅ 2026-10-14');
  });

  it('turns non-tasks into todo tasks and ignores code', () => {
    expect(toggleTask('- a', 0, TODAY)).toBe('- [ ] a');
    expect(toggleTask('```\nx\n```', 1, TODAY)).toBe('```\nx\n```');
  });
});

describe('generateBlockId', () => {
  it('returns 6 chars of [a-z0-9] and varies', () => {
    const ids = new Set<string>();
    for (let i = 0; i < 200; i++) {
      const id = generateBlockId();
      expect(id).toMatch(/^[a-z0-9]{6}$/);
      ids.add(id);
    }
    expect(ids.size).toBeGreaterThan(190);
  });

  it('falls back to Math.random without crypto', () => {
    const original = globalThis.crypto;
    Object.defineProperty(globalThis, 'crypto', { value: undefined, configurable: true });
    try {
      expect(generateBlockId()).toMatch(/^[a-z0-9]{6}$/);
    } finally {
      Object.defineProperty(globalThis, 'crypto', { value: original, configurable: true });
    }
  });
});

describe('ensureBlockId', () => {
  const idOf = (content: string, line: number): string | undefined => {
    const n = parseNote('a.md', content);
    return Object.entries(n.blockIds).find(([, b]) => b.line <= line && line <= b.endLine)?.[0];
  };

  it('returns existing ^ids and Logseq id:: unchanged', () => {
    expect(ensureBlockId('- a ^abc\n  more', 1)).toEqual({ content: '- a ^abc\n  more', id: 'abc' });
    expect(ensureBlockId('- a\n  id:: 6f1c', 0)).toEqual({ content: '- a\n  id:: 6f1c', id: '6f1c' });
    expect(ensureBlockId('| a | b |\n|---|---|\n^tbl', 0).id).toBe('tbl');
  });

  it('appends to the first line of list items, headings and callouts', () => {
    const li = ensureBlockId('- a\n  more\n  - child', 1);
    expect(li.id).toMatch(/^[a-z0-9]{6}$/);
    expect(li.content).toBe(`- a ^${li.id}\n  more\n  - child`);
    const h = ensureBlockId('## Tytuł ##  ', 0);
    expect(h.content).toBe(`## Tytuł ## ^${h.id}`);
    const c = ensureBlockId('> [!note] T\n> body', 0);
    expect(c.content).toBe(`> [!note] T ^${c.id}\n> body`);
    expect(idOf(c.content, 0)).toBe(c.id);
  });

  it('appends to the last line of paragraphs and quotes', () => {
    const p = ensureBlockId('p1\np2\n\nx', 0);
    expect(p.content).toBe(`p1\np2 ^${p.id}\n\nx`);
    expect(idOf(p.content, 0)).toBe(p.id);
    const q = ensureBlockId('> a\n> b', 0);
    expect(q.content).toBe(`> a\n> b ^${q.id}`);
  });

  it('puts the id on its own line after code blocks and tables', () => {
    const code = ensureBlockId('```\nx\n```\nafter', 1);
    expect(code.content).toBe(`\`\`\`\nx\n\`\`\`\n^${code.id}\n\nafter`);
    expect(parseNote('a.md', code.content).blockIds[code.id]!.kind).toBe('code');
    const table = ensureBlockId('| a | b |\r\n|---|---|\r\n| 1 | 2 |', 2);
    expect(table.content).toBe(`| a | b |\r\n|---|---|\r\n| 1 | 2 |\r\n^${table.id}`);
    expect(parseNote('a.md', table.content).blockIds[table.id]!.kind).toBe('table');
  });

  it('is idempotent', () => {
    const first = ensureBlockId('- zadanie', 0);
    expect(ensureBlockId(first.content, 0)).toEqual(first);
  });

  it('handles list items whose first line opens a code fence', () => {
    const r = ensureBlockId('- ```\n  code\n  ```', 0);
    expect(r.content).toBe(`- \`\`\`\n  code\n  \`\`\`\n  ^${r.id}`);
    expect(parseNote('a.md', r.content).blockIds[r.id]!.line).toBe(0);
  });

  it('returns an empty id for blank lines and frontmatter', () => {
    expect(ensureBlockId('a\n\nb', 1)).toEqual({ content: 'a\n\nb', id: '' });
    expect(ensureBlockId('---\nx: 1\n---\n', 1)).toEqual({ content: '---\nx: 1\n---\n', id: '' });
    expect(ensureBlockId('', 0)).toEqual({ content: '', id: '' });
  });
});

describe('listItemRange', () => {
  const src = '- a\n  - b\n    more b\n  - c\n- d\n\ntext';

  it('covers the item and its children', () => {
    expect(listItemRange(src, 0)).toEqual({ from: 0, to: src.indexOf('\n- d') });
    const b = src.indexOf('  - b');
    expect(listItemRange(src, 1)).toEqual({ from: b, to: src.indexOf('\n  - c') });
    expect(listItemRange(src, 2)).toEqual({ from: b, to: src.indexOf('\n  - c') });
    expect(listItemRange(src, 4)).toEqual({ from: src.indexOf('- d'), to: src.indexOf('- d') + 3 });
  });

  it('returns null outside list items', () => {
    expect(listItemRange(src, 5)).toBeNull();
    expect(listItemRange(src, 6)).toBeNull();
    expect(listItemRange(src, 99)).toBeNull();
  });

  it('works with CRLF and in callouts', () => {
    const crlf = '- a\r\n  - b\r\n- c';
    expect(listItemRange(crlf, 0)).toEqual({ from: 0, to: 10 });
    const callout = '> [!todo]\n> - a\n>   - b\n> - c';
    expect(listItemRange(callout, 1)).toEqual({ from: 10, to: callout.indexOf('\n> - c') });
  });
});

describe('setTaskStatus — lines that cannot become tasks (review regressions)', () => {
  it('leaves table rows, headings, rules, callout headers, setext underlines and id lines unchanged', () => {
    const cases: [string, number][] = [
      ['| a |\n|---|\n| 1 |', 0],
      ['| a |\n|---|\n| 1 |', 1],
      ['| a |\n|---|\n| 1 |', 2],
      ['# Nagłówek', 0],
      ['Tytuł\n===', 0],
      ['Tytuł\n===', 1],
      ['---', 0],
      ['***', 0],
      ['> [!note] Uwaga\n> treść', 0],
      ['> [!note] Uwaga\n> ## Śródtytuł', 1],
      ['```\nx\n```\n^kod', 3],
      ['<div>\nx\n</div>', 1],
    ];
    for (const [src, line] of cases) {
      expect(setTaskStatus(src, line, 'todo', TODAY), `${src} @${line}`).toBe(src);
      expect(toggleTask(src, line, TODAY), `${src} @${line}`).toBe(src);
    }
  });

  it('inserts the checkbox after the quote prefix on blank lines inside callouts and quotes', () => {
    expect(toggleTask('> [!todo] Lista\n> ', 1, TODAY)).toBe('> [!todo] Lista\n> - [ ] ');
    expect(toggleTask('> [!todo] Lista\n>', 1, TODAY)).toBe('> [!todo] Lista\n> - [ ] ');
    expect(toggleTask('> > zagnieżdżony\n> >', 1, TODAY)).toBe('> > zagnieżdżony\n> > - [ ] ');
    expect(toggleTask('> > zagnieżdżony', 0, TODAY)).toBe('> > - [ ] zagnieżdżony');
    expect(toggleTask('> [!note]\n> > cytat w callout', 1, TODAY)).toBe('> [!note]\n> > - [ ] cytat w callout');
  });
});

describe('tasks inside plain blockquotes (review regressions)', () => {
  it('toggles a task inside a quote and keeps the prefix', () => {
    const src = '> Cytat\n> - [ ] zadanie 🔁 every day 📅 2026-10-14';
    expect(toggleTask(src, 1, TODAY)).toBe(
      '> Cytat\n> - [ ] zadanie 🔁 every day 📅 2026-10-15\n> - [x] zadanie 🔁 every day 📅 2026-10-14 ✅ 2026-10-14',
    );
  });

  it('ensureBlockId gives a quote with nested blocks a separate id line after a blank line', () => {
    const src = '> - a\n> - b\n>\n> tekst\n\nPo';
    const r = ensureBlockId(src, 2);
    expect(r.content).toBe(`> - a\n> - b\n>\n> tekst\n\n^${r.id}\n\nPo`);
    const r3 = ensureBlockId('> - a\n>\n> - b\n## Dalej', 1);
    expect(r3.content).toBe(`> - a\n>\n> - b\n\n^${r3.id}\n\n## Dalej`);
    expect(parseNote('a.md', r3.content).blockIds[r3.id]).toMatchObject({ kind: 'quote', line: 0, endLine: 4 });
    const note = parseNote('a.md', r.content);
    expect(note.blockIds[r.id]?.kind).toBe('quote');
    expect(note.blockIds[r.id]?.line).toBe(0);
    // Lines of the nested blocks still get their own ids.
    const r2 = ensureBlockId(src, 1);
    expect(r2.content).toBe(`> - a\n> - b ^${r2.id}\n>\n> tekst\n\nPo`);
    expect(parseNote('a.md', r2.content).blockIds[r2.id]?.kind).toBe('listItem');
  });
});

describe('Logseq ++ repeaters (review regressions)', () => {
  it('moves a ++ repeater to the first occurrence after today, keeping the weekday', () => {
    const src = '- TODO podlać\n  SCHEDULED: <2026-10-01 Thu ++1w>';
    expect(toggleTask(src, 0, TODAY)).toBe(
      '- TODO podlać\n  SCHEDULED: <2026-10-15 Thu ++1w>\n- DONE podlać ✅ 2026-10-14\n  SCHEDULED: <2026-10-01 Thu ++1w>',
    );
  });

  it('a ++ repeater already in the future moves by one interval; + repeaters never catch up', () => {
    expect(toggleTask('- TODO a\n  DEADLINE: <2026-10-20 Tue ++1d>', 0, TODAY).split('\n')[1]).toBe('  DEADLINE: <2026-10-21 Wed ++1d>');
    expect(toggleTask('- TODO a\n  DEADLINE: <2026-10-01 Thu +1w>', 0, TODAY).split('\n')[1]).toBe('  DEADLINE: <2026-10-08 Thu +1w>');
  });
});

describe('ensureBlockId — ids must survive a re-parse (review regressions)', () => {
  const resolves = (content: string, id: string, line: number): boolean => parseNote('a.md', content).blockIds[id]?.line === line;

  it('does not write an id into an unclosed code fence', () => {
    const src = 'Tekst\n\n```js\nconst a = 1;\n';
    expect(ensureBlockId(src, 3)).toEqual({ content: src, id: '' });
    const callout = '> [!note]\n> ```\n> kod';
    expect(ensureBlockId(callout, 2)).toEqual({ content: callout, id: '' });
  });

  it('separates the id line from an HTML block by a blank line', () => {
    const src = '<div>\nśrodek\n</div>\n\nDalej';
    const r = ensureBlockId(src, 1);
    expect(r.content).toBe(`<div>\nśrodek\n</div>\n\n^${r.id}\n\nDalej`);
    expect(resolves(r.content, r.id, 0)).toBe(true);
    const script = '<script>\nx()\n</script>\nDalej';
    const r2 = ensureBlockId(script, 0);
    expect(r2.content).toBe(`<script>\nx()\n</script>\n\n^${r2.id}\n\nDalej`);
    expect(resolves(r2.content, r2.id, 0)).toBe(true);
  });

  it('uses the last line of a list item when the first line ends inside a multi-line comment', () => {
    const src = '- zadanie %%początek\n  komentarza%% koniec';
    const r = ensureBlockId(src, 0);
    expect(r.content).toBe(`- zadanie %%początek\n  komentarza%% koniec ^${r.id}`);
    expect(resolves(r.content, r.id, 0)).toBe(true);
  });

  it('uses the first line of a paragraph when its last line ends inside a comment', () => {
    const src = 'pierwsza\ndruga %%komentarz\n\nciąg dalszy%%';
    const r = ensureBlockId(src, 1);
    expect(r.content).toBe(`pierwsza ^${r.id}\ndruga %%komentarz\n\nciąg dalszy%%`);
    expect(resolves(r.content, r.id, 0)).toBe(true);
  });

  it('gives up (empty id, content unchanged) when no placement works', () => {
    const src = 'a %%b\n\nc%%';
    expect(ensureBlockId(src, 0)).toEqual({ content: src, id: '' });
  });
});

describe('setTaskStatus — lines inside %% comments (review regressions)', () => {
  it('does not turn hidden comment lines into (invisible) tasks', () => {
    const src = 'Tekst %%ukryty\nkomentarz\n\nkoniec%% widoczny';
    for (const line of [1, 2]) {
      expect(setTaskStatus(src, line, 'todo', TODAY), `line ${line}`).toBe(src);
      expect(toggleTask(src, line, TODAY), `line ${line}`).toBe(src);
    }
    // A line that only starts inside the comment cannot be prefixed either.
    expect(toggleTask(src, 3, TODAY)).toBe(src);
  });

  it('still toggles tasks around and after comments', () => {
    expect(toggleTask('%%\nx\n%% - [ ] zadanie', 2, TODAY)).toBe('%%\nx\n%% - [x] zadanie ✅ 2026-10-14');
    // ✅ goes before a comment that continues on the next line (inside it, it would be hidden).
    const done = toggleTask('- [ ] zadanie %%notatka\nciąg%%', 0, TODAY);
    expect(done).toBe('- [x] zadanie ✅ 2026-10-14 %%notatka\nciąg%%');
    expect(parseNote('a.md', done).tasks[0]).toMatchObject({ status: 'done', doneDate: TODAY, text: 'zadanie' });
    expect(toggleTask(done, 0, TODAY)).toBe('- [ ] zadanie %%notatka\nciąg%%');
    // Recurring: the copy does not repeat the unterminated comment.
    expect(toggleTask('- [ ] a 📅 2026-10-14 🔁 every day %%x\ny%%', 0, TODAY)).toBe(
      '- [ ] a 📅 2026-10-15 🔁 every day\n- [x] a 📅 2026-10-14 🔁 every day ✅ 2026-10-14 %%x\ny%%',
    );
  });
});

describe('setTaskStatus — blank lines of an unclosed fence (review regressions)', () => {
  it('leaves blank lines after an unclosed fence alone (they are code until the end of the container)', () => {
    const src = 'Tekst\n```\nkod\n\n';
    expect(toggleTask(src, 3, TODAY)).toBe(src);
    expect(toggleTask(src, 4, TODAY)).toBe(src);
    const callout = '> [!note]\n> ```\n> kod\n>\n\n';
    expect(toggleTask(callout, 3, TODAY)).toBe(callout);
    // Outside the callout the fence is over.
    expect(toggleTask(callout, 4, TODAY)).toBe('> [!note]\n> ```\n> kod\n>\n- [ ] \n');
  });

  it('still converts blank lines after closed fences', () => {
    expect(toggleTask('```\nkod\n```\n', 3, TODAY)).toBe('```\nkod\n```\n- [ ] ');
  });
});
