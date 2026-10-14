import { it } from 'vitest';
import { parseNote } from '@/core/markdown';
import { VaultIndex } from './vaultIndex';
const WORDS = ['notatka', 'zadanie', 'żółw', 'łódź', 'projekt', 'spotkanie', 'raport', 'zakupy', 'kawa', 'ogród', 'dom', 'praca'];
function syntheticNote(i: number, total: number) {
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
      case 0: lines.push(`- [ ] ${w} numer ${k} 📅 2026-${month}-${day} #${w}`); break;
      case 1: lines.push(`  - szczegóły ${w} [[${target}]] ^b${i}x${k}`); break;
      case 2: lines.push(`- Tekst o ${w} i ${WORDS[(k * 3) % WORDS.length]} w kontekście ((b${(i + 1) % total}x1))`); break;
      case 3: lines.push(`- [x] zrobione ${w} ✅ 2026-${month}-${day}`); break;
      case 4: lines.push(`- przypomnienie ${w} ⏰ 2026-${month}-${day} 09:30`); break;
      default: lines.push(`Akapit ${k}: ${w} ${w} [[Nieistniejąca ${k}]] https://example.com/${i}`);
    }
  }
  return { path, content: lines.join('\n') };
}
it('profile', () => {
  const total = 3000;
  const notes = Array.from({ length: total }, (_, i) => syntheticNote(i, total));
  for (let r = 0; r < 1; r++) {
    let t = performance.now();
    for (const n of notes) parseNote(n.path, n.content);
    const parse = performance.now() - t;
    t = performance.now();
    const index = new VaultIndex();
    for (const n of notes) index.update(n.path, n.content);
    const idx = performance.now() - t;
    t = performance.now();
    index.flushSearch();
    const s = performance.now() - t;
    index.dispose();
    console.info(`round ${r}: parseNote only ${parse.toFixed(0)} ms; index.update ${idx.toFixed(0)} ms; search build ${s.toFixed(0)} ms`);
  }
}, 120000);
