import { evaluateCalc, evaluateInline, evaluateTable, findCalcBlocks, findTables, pageScope, parseTable } from '@/core/calc';
import { parseNote } from '@/core/markdown';
import { isMarkdown } from '@/core/paths';
import { DEFAULT_VAULT_CONFIG, type VaultPath } from '@/core/types';
import { demoVaultFiles, GUIDE_PAGE, seedDemoVault, type DemoTarget } from './demo-vault';

const NOW = new Date(2026, 9, 14, 10, 30); // Wednesday
const files = demoVaultFiles(NOW);
const text = (p: VaultPath) => {
  const c = files[p];
  if (typeof c !== 'string') throw new Error(`${p} is not text`);
  return c;
};
const markdown = Object.keys(files).filter(isMarkdown);

describe('demo vault content', () => {
  it('has the guide, journals, projects, books, recipes and templates', () => {
    expect(Object.keys(files)).toEqual(
      expect.arrayContaining([
        GUIDE_PAGE,
        'journals/2026-10-14.md',
        'journals/2026-10-13.md',
        'journals/2026-10-12.md',
        'journals/2026-10-07.md',
        'Projekty/Remont mieszkania.md',
        'Książki 2026.md',
        'Przepisy.md',
        'templates/journal.md',
        'templates/Spotkanie.md',
        'assets/szkic-mieszkania.svg',
      ]),
    );
    expect(files['assets/szkic-mieszkania.svg']).toBeInstanceOf(Uint8Array);
  });

  it('explains every feature on the guide page', () => {
    const guide = text(GUIDE_PAGE);
    for (const needle of [
      '[[Remont mieszkania]]',
      '#przewodnik',
      '((kawa))',
      '^kawa',
      '- [ ]',
      '📅',
      '⏳',
      '🔁 every week',
      '⏰ 2026-10-15 09:00',
      '⏰! 2026-10-16 07:00',
      '```calc',
      '=sum(above)',
      '```query',
      '![[',
      '> [!note]',
      'templates',
      'Kalendarz',
      'Syncthing',
      'Nextcloud',
      'Import',
      'Ctrl+K',
    ]) {
      expect(guide, needle).toContain(needle);
    }
  });

  it('uses dates relative to now', () => {
    const later = demoVaultFiles(new Date(2027, 0, 5, 8, 0));
    expect(Object.keys(later)).toContain('journals/2027-01-05.md');
    expect(text('journals/2026-10-14.md')).toContain('⏰ 2026-10-14 16:00');
    // Next Sunday after Wednesday 14 Oct is the 18th.
    expect(text('Przepisy.md')).toContain('⏳ 2026-10-18');
  });

  it('parses into notes with tasks, reminders, links and tags', () => {
    const notes = markdown.map((p) => parseNote(p, text(p), DEFAULT_VAULT_CONFIG));
    const guide = notes.find((n) => n.path === GUIDE_PAGE)!;
    expect(guide.aliases).toEqual(expect.arrayContaining(['Start']));
    expect(guide.tasks.length).toBeGreaterThanOrEqual(8);
    expect(guide.reminders.some((r) => r.alarm)).toBe(true);
    expect(guide.blockIds.kawa).toBeDefined();
    expect(guide.tags).toEqual(expect.arrayContaining(['przewodnik', 'dom/remont']));
    const today = notes.find((n) => n.path === 'journals/2026-10-14.md')!;
    expect(today.journalDate).toBe('2026-10-14');
    expect(today.links.map((l) => l.target)).toEqual(expect.arrayContaining(['Remont mieszkania', 'Witaj w Brulionie']));
  });

  it('has calc blocks and formula tables that evaluate without errors', () => {
    let blocks = 0;
    let tables = 0;
    for (const p of markdown) {
      const content = text(p);
      for (const b of findCalcBlocks(content)) {
        blocks++;
        const { lines } = evaluateCalc(b.source, { locale: 'pl' });
        expect(lines.filter((l) => l.error).map((l) => `${p}:${l.input}: ${l.error}`)).toEqual([]);
      }
      for (const loc of findTables(content)) {
        const model = parseTable(content.slice(loc.from, loc.to));
        if (!model) continue;
        tables++;
        const { errors } = evaluateTable(model, 'pl');
        expect(errors.flat().filter(Boolean), p).toEqual([]);
      }
    }
    expect(blocks).toBeGreaterThanOrEqual(3);
    expect(tables).toBeGreaterThanOrEqual(3);
  });

  it('has inline calculations that use page values', () => {
    const books = text('Książki 2026.md');
    expect(evaluateInline('cel / 12', pageScope(books), 'pl').value).toBe('2');
    const guide = text(GUIDE_PAGE);
    const line = guide.split('\n').findIndex((l) => l.includes('`= kawa * 2`'));
    expect(evaluateInline('kawa * 2', pageScope(guide, line), 'pl').value).toBe('28');
  });

  it('only links to pages that exist (or are meant to be created)', () => {
    const titles = new Set(markdown.map((p) => p.replace(/\.md$/, '').split('/').pop()!.toLowerCase()));
    for (const p of markdown) titles.add(p.replace(/\.md$/, '').toLowerCase());
    titles.add('ania');
    const intentionallyMissing = new Set(['pomysły na weekend']);
    for (const p of markdown) {
      const note = parseNote(p, text(p), DEFAULT_VAULT_CONFIG);
      for (const link of note.links) {
        if (link.kind !== 'wiki' && link.kind !== 'embed') continue;
        const target = link.target.toLowerCase();
        if (!target || /\.(svg|png)$/.test(target)) continue;
        expect(titles.has(target) || intentionallyMissing.has(target), `${p} → ${link.target}`).toBe(true);
      }
    }
  });
});

describe('seedDemoVault', () => {
  function fakeTarget(existing: VaultPath[] = []): DemoTarget & { written: Map<VaultPath, string | Uint8Array> } {
    const written = new Map<VaultPath, string | Uint8Array>();
    return {
      written,
      config: DEFAULT_VAULT_CONFIG,
      exists: (p) => existing.includes(p) || written.has(p),
      write: async (p, c) => void written.set(p, c),
      vault: { writeBinary: async (p, d) => void written.set(p, d) },
    };
  }

  it('writes every file once and never overwrites', async () => {
    const target = fakeTarget([GUIDE_PAGE]);
    const paths = await seedDemoVault(target, NOW);
    expect(paths).not.toContain(GUIDE_PAGE);
    expect(target.written.get('journals/2026-10-14.md')).toContain('Poranna kawa');
    expect(target.written.get('assets/szkic-mieszkania.svg')).toBeInstanceOf(Uint8Array);
    expect(await seedDemoVault(target, NOW)).toEqual([]);
  });
});
