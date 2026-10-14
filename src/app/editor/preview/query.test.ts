import { describe, expect, it } from 'vitest';
import { VaultIndex } from '@/core/index';
import { DEFAULT_VAULT_CONFIG } from '@/core/types';
import { TinyIndex } from '../fake-workspace';
import { evaluateQuery, queryIndex, querySource } from './query';

const FILES: Record<string, string> = {
  'Projekty/Dom.md': `# Dom
- [ ] Zadzwonić do hydraulika 📅 2026-10-15 #remont
- [/] Wybrać płytki łazienkowe 📅 2026-10-12
- [x] Kupić farbę ✅ 2026-10-10
- [ ] Urodziny Ani ⏰ 2026-10-20 09:00
`,
  'Praca.md': `- [ ] Raport kwartalny 📅 2026-10-16 #praca
- Spotkanie zespołu ⏰! 2026-10-15 06:30
`,
};

function tiny(): TinyIndex {
  const idx = new TinyIndex(DEFAULT_VAULT_CONFIG);
  for (const [p, c] of Object.entries(FILES)) idx.update(p, c);
  return idx;
}

const opts = { now: new Date(2026, 9, 14, 10, 30), locale: 'pl' as const, weekStartsOn: 1 as const, path: 'Projekty/Dom.md' };

describe('query index', () => {
  it('uses a VaultIndex as it is', () => {
    const vi = new VaultIndex(DEFAULT_VAULT_CONFIG);
    expect(queryIndex(vi, DEFAULT_VAULT_CONFIG, 0)).toBe(vi);
  });

  it('snapshots other indexes and rebuilds when their version changes', () => {
    const idx = tiny();
    const a = queryIndex(idx, DEFAULT_VAULT_CONFIG, 0);
    expect(a).toBeInstanceOf(VaultIndex);
    expect(queryIndex(idx, DEFAULT_VAULT_CONFIG, 5)).toBe(a);
    idx.update('Nowa.md', '- [ ] świeże zadanie');
    const b = queryIndex(idx, DEFAULT_VAULT_CONFIG, 0);
    expect(b).not.toBe(a);
    expect(b.tasks().some((t) => t.task.text.includes('świeże'))).toBe(true);
  });
});

describe('evaluateQuery', () => {
  it('returns open tasks with their dates, sorted by due', () => {
    const v = evaluateQuery('type:tasks status:open sort:due', queryIndex(tiny(), DEFAULT_VAULT_CONFIG, 0), opts);
    expect(v.error).toBeUndefined();
    const rows = v.groups.flatMap((g) => g.rows);
    expect(rows.map((r) => r.kind === 'task' && r.text)).toEqual(['Wybrać płytki łazienkowe', 'Zadzwonić do hydraulika #remont', 'Raport kwartalny #praca', 'Urodziny Ani']);
    expect(rows[0]).toMatchObject({ kind: 'task', status: 'doing', due: '2026-10-12', path: 'Projekty/Dom.md', line: 2 });
    expect(v.total).toBe(4);
  });

  it('groups by page', () => {
    const v = evaluateQuery('type:tasks status:open group:page', queryIndex(tiny(), DEFAULT_VAULT_CONFIG, 0), opts);
    expect(v.groups.length).toBe(2);
    expect(v.groups.every((g) => g.label !== null && g.rows.length > 0)).toBe(true);
  });

  it('keeps reminders as reminders (also on tasks)', () => {
    const v = evaluateQuery('type:reminders', queryIndex(tiny(), DEFAULT_VAULT_CONFIG, 0), opts);
    const rows = v.groups.flatMap((g) => g.rows);
    expect(rows.map((r) => r.kind)).toEqual(['reminder', 'reminder']);
    expect(rows.find((r) => r.kind === 'reminder' && r.alarm)).toMatchObject({ at: '2026-10-15T06:30', path: 'Praca.md' });
  });

  it('reports syntax errors (localised)', () => {
    const v = evaluateQuery('limit:dużo', queryIndex(tiny(), DEFAULT_VAULT_CONFIG, 0), opts);
    expect(v.error).toBeTruthy();
    expect(v.groups).toEqual([]);
  });
});

describe('querySource', () => {
  it('extracts the query body (comments dropped)', () => {
    expect(querySource('```query\n# Przykład: zadania\ntype:tasks status:open\n\ntag:praca\n// koniec\n```')).toBe('type:tasks status:open\ntag:praca');
    expect(querySource('```query\ntype:tasks')).toBe('type:tasks');
    expect(querySource('```query\n#praca\n```')).toBe('#praca');
  });
});
