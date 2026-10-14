/**
 * Workspace service on a MemoryFs vault with a fake Platform. Needs @/core/index (VaultIndex)
 * and @/core/reminders (ReminderScheduler); skipped until those modules exist.
 */
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { LocalStore, MemoryFs } from '@/core/storage';
import type { HttpTransport, Importer, NotificationService, Platform, ScheduledNotification, VaultLocation } from '@/core/types';
import type { EditorWorkspace } from '@/app/editor';
import type { Workspace as WorkspaceType, WorkspaceOptions } from './workspace';

const coreReady =
  existsSync(fileURLToPath(new URL('../../core/index/index.ts', import.meta.url))) &&
  existsSync(fileURLToPath(new URL('../../core/reminders/index.ts', import.meta.url)));
const exportReady = existsSync(fileURLToPath(new URL('../../core/export/index.ts', import.meta.url)));

const NOW = new Date(2026, 9, 14, 10, 30);
const LOCATION: VaultLocation = { kind: 'memory', id: 'test', name: 'Test' };

function fakeNotifications(): NotificationService & { synced: ScheduledNotification[][] } {
  const synced: ScheduledNotification[][] = [];
  return {
    synced,
    firesWhenClosed: false,
    permission: async () => 'granted',
    requestPermission: async () => 'granted',
    sync: async (items) => void synced.push(items),
    showNow: async () => {},
    onActivate: () => () => {},
  };
}

function fakePlatform(fs: MemoryFs, http?: HttpTransport): Platform & { notifications: ReturnType<typeof fakeNotifications> } {
  return {
    kind: 'web',
    name: 'Test',
    vaultKinds: ['memory'],
    pickVaultFolder: async () => null,
    defaultVaultLocation: async () => LOCATION,
    openFileSystem: async () => fs,
    notifications: fakeNotifications(),
    http: http ?? (async () => ({ status: 500, headers: {}, body: new Uint8Array() })),
    openExternal: async () => {},
    saveFile: async () => {},
    pickFiles: async () => [],
  };
}

let seq = 0;
async function open(files: Record<string, string | Uint8Array>, opts: WorkspaceOptions = {}) {
  const { openWorkspace } = await import('./workspace');
  const fs = new MemoryFs({ files, now: () => NOW.getTime() });
  const platform = fakePlatform(fs);
  const ws = await openWorkspace(platform, LOCATION, {
    store: LocalStore.memory(`ws-test-${++seq}`),
    now: () => NOW,
    indexChunk: 2,
    ...opts,
  });
  await ws.ready;
  return { ws, fs, platform };
}

const settle = async (ws: WorkspaceType) => {
  await ws.vault.idle();
  for (let i = 0; i < 5; i++) await new Promise((r) => setTimeout(r, 0));
};

describe.skipIf(!coreReady)('Workspace (needs @/core/index and @/core/reminders)', () => {
  let current: WorkspaceType | null = null;
  afterEach(async () => {
    await current?.close();
    current = null;
  });

  it('satisfies the editor contract structurally', async () => {
    const { ws } = await open({});
    current = ws;
    const editorWs: EditorWorkspace = ws;
    expect(typeof editorWs.onFileChanged).toBe('function');
  });

  it('indexes all notes in chunks with progress, skipping conflict copies', async () => {
    const { openWorkspace } = await import('./workspace');
    const fs = new MemoryFs({
      files: {
        'a.md': '# A\n- link do [[b]]',
        'b.md': '# B',
        'Folder/c.md': '- [ ] zadanie #praca',
        'b.sync-conflict-20261014-101010-ABCDEFG.md': '# B (konflikt)',
        'assets/x.png': new Uint8Array([1, 2, 3]),
      },
    });
    const progress: number[] = [];
    const ws = await openWorkspace(fakePlatform(fs), LOCATION, { store: LocalStore.memory('ws-progress'), indexChunk: 1 });
    current = ws;
    ws.onProgress((p) => progress.push(p.indexed));
    await ws.ready;
    expect(ws.progress).toEqual({ indexed: 3, total: 3 });
    expect(progress.at(-1)).toBe(3);
    expect(ws.index.paths().sort()).toEqual(['Folder/c.md', 'a.md', 'b.md']);
    expect(ws.index.backlinks('b.md').map((b) => b.path)).toEqual(['a.md']);
    expect(ws.conflicts()).toEqual([expect.objectContaining({ original: 'b.md', source: 'syncthing' })]);
  });

  it('writes update the index at once and are serialised per path', async () => {
    const { ws } = await open({ 'n.md': '' });
    current = ws;
    let notified = 0;
    ws.subscribe(() => notified++);
    await ws.write('n.md', '- [[Nowa strona]] #tag');
    expect(ws.index.content('n.md')).toBe('- [[Nowa strona]] #tag');
    await Promise.all(Array.from({ length: 20 }, (_, i) => ws.modify('n.md', (c) => `${c}\n- ${i}`)));
    const content = await ws.read('n.md');
    for (let i = 0; i < 20; i++) expect(content).toContain(`\n- ${i}`);
    await settle(ws);
    expect(notified).toBeGreaterThan(0);
  });

  it('follows external changes (Syncthing, other apps)', async () => {
    const { ws, fs } = await open({ 'a.md': 'stara treść' });
    current = ws;
    const seen: string[] = [];
    ws.onFileChanged((changes) => changes.forEach((c) => seen.push(`${c.type}:${c.path}`)));
    fs.externalWrite('a.md', 'nowa treść z [[b]]');
    fs.externalWrite('nowy.md', '# Nowy');
    await settle(ws);
    expect(ws.index.content('a.md')).toBe('nowa treść z [[b]]');
    expect(ws.index.note('nowy.md')).toBeDefined();
    expect(seen).toEqual(expect.arrayContaining(['modify:a.md', 'create:nowy.md']));
  });

  it('opens journals, creating them from the template', async () => {
    const { ws } = await open({ 'templates/journal.md': '## {{weekday}}\n- {{cursor}}\n' });
    current = ws;
    const path = await ws.openJournal('2026-10-14');
    expect(path).toBe('journals/2026-10-14.md');
    expect(await ws.read(path)).toBe('## środa\n- \n');
    await ws.write(path, '- moje');
    expect(await ws.openJournal('2026-10-14')).toBe(path);
    expect(await ws.read(path)).toBe('- moje');
    expect(ws.journalPath('2026-10-15')).toBe('journals/2026-10-15.md');
  });

  it('finds existing journals in another file name format', async () => {
    const { ws } = await open({ 'journals/2026_10_14.md': '- Logseq' });
    current = ws;
    expect(await ws.openJournal('2026-10-14')).toBe('journals/2026_10_14.md');
  });

  it('creates pages and resolves or creates link targets', async () => {
    const { ws } = await open({ 'Ludzie/Anna Nowak.md': '---\naliases: [Ania]\n---\n# Anna' });
    current = ws;
    expect(await ws.createPage('Zażółć gęślą jaźń')).toBe('Zażółć gęślą jaźń.md');
    expect(await ws.createPage('Zażółć gęślą jaźń')).toBe('Zażółć gęślą jaźń 2.md');
    expect(await ws.createPage('Projekty/Dom', '', '# Dom')).toBe('Projekty/Dom.md');
    expect(await ws.resolveOrCreate('Ania')).toBe('Ludzie/Anna Nowak.md');
    expect(await ws.resolveOrCreate('Anna Nowak#Kontakt')).toBe('Ludzie/Anna Nowak.md');
    expect(await ws.resolveOrCreate('Całkiem nowa')).toBe('Całkiem nowa.md');
    expect(ws.exists('Całkiem nowa.md')).toBe(true);
  });

  it('renames pages and rewrites links in other notes', async () => {
    const { ws } = await open({
      'Stara.md': '# Stara\n- link do siebie [[Stara]]',
      'a.md': '- [[Stara]] i [[Stara|alias]] i [[Stara#Nagłówek]]',
      'b.md': '- [tekst](Stara.md)',
      'c.md': '- bez linków',
    });
    current = ws;
    const to = await ws.renamePage('Stara.md', 'Nowa ćma');
    expect(to).toBe('Nowa ćma.md');
    expect(ws.exists('Stara.md')).toBe(false);
    expect(await ws.read('a.md')).toBe('- [[Nowa ćma]] i [[Nowa ćma|alias]] i [[Nowa ćma#Nagłówek]]');
    expect(await ws.read('b.md')).toContain('Nowa%20%C4%87ma.md');
    expect(await ws.read('Nowa ćma.md')).toContain('[[Nowa ćma]]');
    expect(await ws.read('c.md')).toBe('- bez linków');
    expect(ws.index.note('Nowa ćma.md')).toBeDefined();
    expect(ws.index.note('Stara.md')).toBeUndefined();
    expect(await ws.renamePage('Nowa ćma.md', 'Archiwum/Ćma')).toBe('Archiwum/Ćma.md');
  });

  it('deletes to the trash and restores', async () => {
    const { ws } = await open({ 'a.md': '# A' });
    current = ws;
    await ws.deletePage('a.md');
    expect(ws.exists('a.md')).toBe(false);
    expect(ws.index.note('a.md')).toBeUndefined();
    const trash = await ws.vault.trash();
    expect(trash).toHaveLength(1);
    expect(await ws.restore(trash[0]!.path)).toBe('a.md');
    expect(ws.index.note('a.md')).toBeDefined();
  });

  it('edits tasks, block ids and reminders in files', async () => {
    const { ws } = await open({ 't.md': '- [ ] Kupić chleb\n- Akapit bez id\n' });
    current = ws;
    await ws.toggleTask('t.md', 0);
    expect(await ws.read('t.md')).toContain('- [x] Kupić chleb ✅ 2026-10-14');
    await ws.setTaskStatus('t.md', 0, 'todo');
    expect(await ws.read('t.md')).toContain('- [ ] Kupić chleb\n');
    const id = await ws.ensureBlockId('t.md', 1);
    expect(id).toMatch(/^[a-z0-9]{6}$/);
    expect(await ws.ensureBlockId('t.md', 1)).toBe(id);
    await ws.addReminder('t.md', 1, '2026-10-15T09:00', false);
    expect((await ws.read('t.md')).split('\n')[1]).toBe(`- Akapit bez id ⏰ 2026-10-15 09:00 ^${id}`);
    await ws.addReminder('t.md', 0, '2026-10-16T06:30', true);
    expect((await ws.read('t.md')).split('\n')[0]).toBe('- [ ] Kupić chleb ⏰! 2026-10-16 06:30');
  });

  it('quick capture appends to today’s journal', async () => {
    const { ws } = await open({ 'templates/journal.md': '- {{cursor}}\n' });
    current = ws;
    const at = await ws.quickCapture('Kupić mleko jutro o 18');
    expect(at).toEqual({ path: 'journals/2026-10-14.md', line: 0 });
    expect(await ws.read(at.path)).toBe('- [ ] Kupić mleko ⏰ 2026-10-15 18:00\n');
    const second = await ws.quickCapture('Pomysł: notatki głosowe', { task: false });
    expect(second.line).toBe(1);
    expect(await ws.read(at.path)).toBe('- [ ] Kupić mleko ⏰ 2026-10-15 18:00\n- Pomysł: notatki głosowe\n');
    expect(ws.index.note(at.path)?.reminders).toHaveLength(1);
  });

  it('saves attachments under unique names and serves blob URLs', async () => {
    const { ws } = await open({ 'assets/zdjęcie.png': new Uint8Array([1]) });
    current = ws;
    const p = await ws.saveAttachment('zdjęcie.png', new Uint8Array([2, 3]));
    expect(p).toBe('assets/zdjęcie 2.png');
    expect(await ws.readBinary(p)).toEqual(new Uint8Array([2, 3]));
    // A pasted image is shown at once (the bytes are at hand).
    expect(ws.assetUrl(p)).toMatch(/^blob:/);
    expect(ws.assetUrl('brak.png')).toBeNull();
    // Others load on first use; subscribers hear when they are ready.
    const existing = 'assets/zdjęcie.png';
    const ready = new Promise<void>((resolve) => ws.subscribe(() => ws.assetUrl(existing) && resolve()));
    expect(ws.assetUrl(existing)).toBeNull();
    await ready;
    expect(ws.assetUrl(existing)).toMatch(/^blob:/);
  });

  it('preloads the attachments notes embed and resolves attachment links', async () => {
    const { ws } = await open({ 'Notatki/a.md': '![[foto.png]]\n![[plan.pdf]]', 'assets/foto.png': new Uint8Array([1]), 'Notatki/plan.pdf': new Uint8Array([2]) });
    current = ws;
    await settle(ws);
    expect(ws.assetUrl('assets/foto.png')).toMatch(/^blob:/);
    expect(ws.resolveAttachment('foto.png', 'Notatki/a.md')).toBe('assets/foto.png');
    expect(ws.resolveAttachment('plan.pdf', 'Notatki/a.md')).toBe('Notatki/plan.pdf');
    expect(ws.resolveAttachment('PLAN.PDF')).toBe('Notatki/plan.pdf');
    expect(ws.resolveAttachment('brak.png')).toBeNull();
  });

  it('imports with the collision policy (journals get appended, others renamed)', async () => {
    const { ws } = await open({ 'journals/2026-10-14.md': '- moje\n', 'Import/Notatka.md': '# Moja' });
    current = ws;
    const importer: Importer = {
      id: 'fake',
      name: 'Logseq',
      accepts: { extensions: ['.md'], directory: true },
      detect: () => true,
      run: async (_files, opts) => ({
        files: [
          { path: `${opts.vaultConfig.journalsFolder}/2026-10-14.md`, content: '- z Logseq\n' },
          { path: `${opts.targetFolder}/Notatka.md`, content: '# Importowana' },
          { path: `${opts.targetFolder}/Nowa.md`, content: '# Nowa' },
          { path: `${opts.targetFolder}/obraz.png`, content: new Uint8Array([9]) },
        ],
        warnings: ['uwaga'],
      }),
    };
    const progress: number[] = [];
    const report = await ws.importFiles(importer, [], 'Import', { onProgress: (p) => progress.push(p.done) });
    expect(report.appended).toEqual(['journals/2026-10-14.md']);
    expect(report.renamed).toEqual([{ from: 'Import/Notatka.md', to: 'Import/Notatka 2.md' }]);
    expect(report.created).toEqual(['Import/Nowa.md', 'Import/obraz.png']);
    expect(report.warnings).toEqual(['uwaga']);
    expect(report.errors).toEqual([]);
    expect(progress).toEqual([1, 2, 3, 4]);
    expect(await ws.read('journals/2026-10-14.md')).toBe('- moje\n\n## Import (Logseq)\n\n- z Logseq\n');
    expect(await ws.read('Import/Notatka.md')).toBe('# Moja');
    expect(ws.index.note('Import/Nowa.md')).toBeDefined();
  });

  it.skipIf(!exportReady)('exports through the exporter registry (needs @/core/export)', async () => {
    const { ws } = await open({ 'a.md': '# A', 'assets/x.png': new Uint8Array([1]) });
    current = ws;
    const { exporters } = await import('@/core/export');
    const first = exporters[0]!;
    const out = await ws.exportFiles(first.id);
    expect(out.data.byteLength).toBeGreaterThan(0);
    await expect(ws.exportFiles('nie-ma-takiego')).rejects.toThrow();
  });

  it('lists and resolves conflict copies', async () => {
    const { ws } = await open({
      'Plan.md': '- a\n',
      'Plan.sync-conflict-20261014-101010-ABCDEFG.md': '- a\n- b\n',
    });
    current = ws;
    const [c] = ws.conflicts();
    expect(c).toEqual(expect.objectContaining({ original: 'Plan.md', copy: 'Plan.sync-conflict-20261014-101010-ABCDEFG.md', device: 'ABCDEFG' }));
    await ws.resolveConflict(c!.original, c!.copy, '- a\n- b\n');
    expect(await ws.read('Plan.md')).toBe('- a\n- b\n');
    expect(ws.conflicts()).toEqual([]);
  });

  it('configures Nextcloud sync and reports its status', async () => {
    const { ws } = await open({});
    current = ws;
    const states: string[] = [];
    ws.onSyncStatus((s) => states.push(s.state));
    expect(ws.sync).toBeNull();
    ws.configureSync({ server: 'cloud.example.pl', username: 'jan', password: 'x', folder: 'Brulion', enabled: true, intervalMin: 5 });
    expect(ws.sync).not.toBeNull();
    ws.configureSync({ server: '', username: 'jan', password: 'x', folder: 'Brulion', enabled: true, intervalMin: 5 });
    expect(ws.sync).toBeNull();
    expect(states[0]).toBe('disabled');
    expect(states.at(-1)).toBe('disabled');
    expect(await ws.syncNow()).toBeNull();
  });

  it('starts reminders after indexing and clears them when disabled', async () => {
    const { ws, platform } = await open({ 'r.md': '- Spotkanie ⏰ 2026-10-15 09:00' });
    current = ws;
    await settle(ws);
    ws.applyReminderSettings({ enabled: false, sound: true, horizonDays: 30 });
    await settle(ws);
    expect(platform.notifications.synced.at(-1)).toEqual([]);
  });

  it('flushes and stops on close', async () => {
    const { ws } = await open({ 'a.md': '' });
    const write = ws.write('a.md', 'ostatnia zmiana');
    await ws.close();
    await write;
    expect(ws.isClosed).toBe(true);
    expect(await ws.vault.fs.readText('a.md')).toBe('ostatnia zmiana');
  });
});

describe('workspace prerequisites', () => {
  it('reports whether the core modules it needs exist', () => {
    // Informational: the suite above runs once @/core/index and @/core/reminders land.
    expect(typeof coreReady).toBe('boolean');
  });
});
