import { describe, expect, it } from 'vitest';
import { isQueryError, parseQuery, runQuery } from './index';
import { VaultIndex } from './vaultIndex';

const WORDS = ['notatka', 'zadanie', 'żółw', 'łódź', 'projekt', 'spotkanie', 'raport', 'zakupy', 'kawa', 'ogród', 'dom', 'praca'];

/** Deterministic synthetic note number `i` (~30 lines: journal-ish outline with tasks and links). */
function syntheticNote(i: number, total: number): { path: string; content: string } {
  const lines: string[] = [];
  const day = String((i % 28) + 1).padStart(2, '0');
  const month = String((i % 12) + 1).padStart(2, '0');
  const journal = i % 3 === 0;
  const path = journal ? `journals/2025-${month}-${day}-${i}.md` : `Strony/Folder ${i % 40}/Strona ${i}.md`;
  if (i % 5 === 0) lines.push('---', `aliases: [Alias ${i}]`, `tags: [temat${i % 30}]`, '---');
  lines.push(`# Notatka ${i}`, '');
  for (let k = 0; k < 24; k++) {
    const w = WORDS[(i + k) % WORDS.length]!;
    const target = `Strona ${(i * 7 + k * 13) % total}`;
    switch (k % 6) {
      case 0:
        lines.push(`- [ ] ${w} numer ${k} 📅 2026-${month}-${day} #${w}`);
        break;
      case 1:
        lines.push(`  - szczegóły ${w} [[${target}]] ^b${i}x${k}`);
        break;
      case 2:
        lines.push(`- Tekst o ${w} i ${WORDS[(k * 3) % WORDS.length]} w kontekście ((b${(i + 1) % total}x1))`);
        break;
      case 3:
        lines.push(`- [x] zrobione ${w} ✅ 2026-${month}-${day}`);
        break;
      case 4:
        lines.push(`- przypomnienie ${w} ⏰ 2026-${month}-${day} 09:30`);
        break;
      default:
        lines.push(`Akapit ${k}: ${w} ${w} [[Nieistniejąca ${k}]] https://example.com/${i}`);
    }
  }
  return { path, content: lines.join('\n') };
}

describe('VaultIndex performance', () => {
  it('indexes 3000 notes (~30 lines each) within a few seconds and stays responsive', () => {
    const total = 3000;
    const notes = Array.from({ length: total }, (_, i) => syntheticNote(i, total));
    const index = new VaultIndex();
    const t0 = performance.now();
    for (const n of notes) index.update(n.path, n.content);
    const indexing = performance.now() - t0;

    expect(index.paths()).toHaveLength(total);
    // Generous bound for a Raspberry Pi 4 under load (~4 s there when idle, most of it parseNote;
    // well under 1 s on a desktop).
    expect(indexing).toBeLessThan(20000);

    // The full-text index is built lazily: the first search pays for it.
    const tSearch = performance.now();
    const hits = index.search('zolw');
    const firstSearch = performance.now() - tSearch;
    expect(firstSearch).toBeLessThan(20000);

    const t1 = performance.now();
    expect(index.search('spotkanie raport').length).toBeGreaterThan(0);
    const tasks = index.tasks();
    const tags = index.tags();
    const suggestions = index.suggestPages('stro', 20);
    const blocks = index.suggestBlocks('tekst kawa', 20);
    const backlinks = index.backlinks('Strony/Folder 7/Strona 7.md');
    const graph = index.graph({ tags: true, missing: true });
    const q = parseQuery('status:open due:<=2026-06-30 tag:kawa sort:due limit:50');
    if (isQueryError(q)) throw new Error(q.error);
    const result = runQuery(q, index, new Date(2026, 5, 1));
    const queries = performance.now() - t1;

    expect(hits.length).toBeGreaterThan(0);
    expect(tasks.length).toBeGreaterThan(total * 4);
    expect(tags.length).toBeGreaterThan(10);
    expect(suggestions).toHaveLength(20);
    expect(blocks.length).toBeGreaterThan(0);
    expect(backlinks.length).toBeGreaterThan(0);
    expect(graph.nodes.length).toBeGreaterThan(total);
    expect(result.total).toBeGreaterThan(0);
    expect(queries).toBeLessThan(10000);

    // Incremental edits stay cheap.
    const t2 = performance.now();
    for (let i = 0; i < 50; i++) index.update(notes[i]!.path, `${notes[i]!.content}\n- edycja ${i}`);
    index.remove(notes[60]!.path);
    index.rename(notes[61]!.path, 'Nowe/Strona 61.md', notes[61]!.content);
    const edits = performance.now() - t2;
    expect(edits).toBeLessThan(5000);
    index.dispose();
    console.info(
      `[perf] index ${total} notes: ${indexing.toFixed(0)} ms; first search (builds the search index): ${firstSearch.toFixed(0)} ms; ` +
        `other queries: ${queries.toFixed(0)} ms; 52 edits: ${edits.toFixed(0)} ms`,
    );
  }, 60000);
});
