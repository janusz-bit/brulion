import { resetAppStore, useAppStore } from '../state/store';
import {
  allCommands,
  availableCommands,
  builtinCommands,
  commandContext,
  getCommand,
  matchesShortcut,
  parseShortcut,
  registerBuiltinCommands,
  registerCommand,
  runCommand,
  type CommandContext,
} from './commands';

const key = (k: string, mods: Partial<{ ctrl: boolean; meta: boolean; alt: boolean; shift: boolean; code: string }> = {}) => ({
  key: k,
  code: mods.code ?? '',
  ctrlKey: !!mods.ctrl,
  metaKey: !!mods.meta,
  altKey: !!mods.alt,
  shiftKey: !!mods.shift,
});

beforeEach(() => resetAppStore());

describe('shortcuts', () => {
  it('parses Kbd notation', () => {
    expect(parseShortcut('Mod+Shift+F')).toEqual({ mod: true, ctrl: false, meta: false, alt: false, shift: true, key: 'f' });
    expect(parseShortcut('Alt+ArrowLeft').key).toBe('arrowleft');
    expect(parseShortcut('Mod++').key).toBe('+');
    expect(parseShortcut('Mod+\\').key).toBe('\\');
  });

  it('matches Ctrl on Linux/Windows and ⌘ on Apple', () => {
    const sc = parseShortcut('Mod+K');
    expect(matchesShortcut(key('k', { ctrl: true }), sc, false)).toBe(true);
    expect(matchesShortcut(key('K', { ctrl: true }), sc, false)).toBe(true);
    expect(matchesShortcut(key('k', { meta: true }), sc, false)).toBe(false);
    expect(matchesShortcut(key('k', { meta: true }), sc, true)).toBe(true);
    expect(matchesShortcut(key('k', { ctrl: true, shift: true }), sc, false)).toBe(false);
    expect(matchesShortcut(key('k'), sc, false)).toBe(false);
  });

  it('falls back to the physical key (Shift changes the character, other layouts)', () => {
    expect(matchesShortcut(key('>', { ctrl: true, shift: true, code: 'Period' }), parseShortcut('Mod+Shift+.'), false)).toBe(true);
    expect(matchesShortcut(key('л', { ctrl: true, code: 'KeyK' }), parseShortcut('Mod+K'), false)).toBe(true);
    expect(matchesShortcut(key('ArrowLeft', { alt: true }), parseShortcut('Alt+ArrowLeft'), false)).toBe(true);
  });
});

describe('registry', () => {
  it('registers, replaces and removes commands', () => {
    const off = registerCommand({ id: 'test.x', title: 'X', group: 'app', run: () => {} });
    expect(getCommand('test.x')).toBeDefined();
    off();
    expect(getCommand('test.x')).toBeUndefined();
  });

  it('registers the built-ins once, with unique ids and i18n titles', () => {
    registerBuiltinCommands();
    registerBuiltinCommands();
    const ids = allCommands().map((c) => c.id);
    expect(new Set(ids).size).toBe(ids.length);
    for (const c of builtinCommands()) expect(c.title.startsWith('shell.')).toBe(true);
    expect(ids).toEqual(expect.arrayContaining(['app.palette', 'app.switcher', 'nav.search', 'journal.today', 'nav.settings', 'editor.toggleTask']));
  });

  it('filters by context: no vault → only app-level commands', () => {
    registerBuiltinCommands();
    const ids = availableCommands(commandContext()).map((c) => c.id);
    expect(ids).toContain('app.palette');
    expect(ids).toContain('app.toggleTheme');
    expect(ids).not.toContain('journal.today');
    expect(ids).not.toContain('page.rename');
  });

  it('page commands need a page, editor commands a focused editor', () => {
    registerBuiltinCommands();
    const fakeWs = { key: 'k', sync: null } as unknown as CommandContext['workspace'];
    useAppStore.setState({ status: 'ready', workspace: fakeWs as never });
    let ids = availableCommands(commandContext()).map((c) => c.id);
    expect(ids).toContain('journal.today');
    expect(ids).not.toContain('page.rename');
    expect(ids).not.toContain('sync.now');
    expect(ids).not.toContain('editor.indent');
    useAppStore.getState().navigate({ view: 'page', path: 'Notatki/Łąka.md' });
    ids = availableCommands(commandContext()).map((c) => c.id);
    expect(ids).toEqual(expect.arrayContaining(['page.rename', 'page.delete', 'page.history', 'page.favourite', 'nav.back']));
  });

  it('runs commands against the store', async () => {
    registerBuiltinCommands();
    useAppStore.setState({ status: 'ready', workspace: { key: 'k', sync: null } as never });
    expect(await runCommand('nav.tasks')).toBe(true);
    expect(useAppStore.getState().route).toEqual({ view: 'tasks' });
    await runCommand('nav.back');
    expect(useAppStore.getState().route).toEqual({ view: 'journals' });
    await runCommand('app.palette');
    expect(useAppStore.getState().paletteOpen).toBe(true);
    await runCommand('app.palette');
    expect(useAppStore.getState().paletteOpen).toBe(false);
    useAppStore.getState().navigate({ view: 'page', path: 'a.md' });
    await runCommand('page.favourite');
    expect(useAppStore.getState().settings.favourites.k).toEqual(['a.md']);
    await runCommand('page.rename');
    expect(useAppStore.getState().dialog).toEqual({ kind: 'rename', path: 'a.md' });
    await runCommand('app.language');
    expect(useAppStore.getState().locale).not.toBe(undefined);
    expect(await runCommand('does.not.exist')).toBe(false);
  });

  it('reports failures as an error toast', async () => {
    const off = registerCommand({
      id: 'test.fail',
      title: 'Fail',
      group: 'app',
      run: () => {
        throw new Error('ups');
      },
    });
    const err = vi.spyOn(console, 'error').mockImplementation(() => {});
    expect(await runCommand('test.fail')).toBe(false);
    expect(useAppStore.getState().toasts[0]).toEqual(expect.objectContaining({ kind: 'error' }));
    err.mockRestore();
    off();
  });
});
