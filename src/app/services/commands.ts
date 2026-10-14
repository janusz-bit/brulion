/**
 * Command registry: everything the command palette (Ctrl/⌘+K), global keyboard shortcuts and
 * menus can run. Commands are plain objects; views may register their own:
 *
 * ```ts
 * useEffect(() => registerCommand({ id: 'calendar.week', title: 'calendar.cmd.week', group: 'navigation',
 *   run: (ctx) => ctx.navigate({ view: 'calendar', mode: 'week' }) }), []);
 * ```
 *
 * `title` is an i18n key (or plain text when no message exists). `shortcut` uses the Kbd
 * notation ("Mod+K", "Alt+ArrowLeft") — set `global: true` to also bind it in the shell's
 * keyboard handler (editor shortcuts are bound by the editor itself and only shown).
 */
import type { EditorView } from '@codemirror/view';
import type { IconName } from '@/app/components';
import {
  cycleTask,
  getFocusedEditor,
  indentItems,
  moveItemDown,
  moveItemUp,
  openLinkAtCursor,
  outdentItems,
  selectBlock,
  toggleFoldAll,
  toggleInline,
  zoomIn,
  zoomOut,
  type FocusedEditor,
} from '@/app/editor';
import { addDaysLocal } from '@/core/dates';
import type { Translator } from '@/core/i18n';
import { translatorFor } from '@/app/components';
import type { VaultPath } from '@/core/types';
import { useAppStore, type AppState, type Route } from '../state/store';
import { toggleFavourite } from '../state/settings';
import type { Workspace } from './workspace';

export type CommandGroup = 'navigation' | 'journal' | 'create' | 'page' | 'editor' | 'view' | 'sync' | 'app';

export interface CommandContext {
  state: AppState;
  workspace: Workspace | null;
  t: Translator;
  /** Focused (or last focused) note editor, if any. */
  editor: FocusedEditor | null;
  /** Page shown in the main pane, if any. */
  pagePath: VaultPath | null;
  navigate: AppState['navigate'];
}

export interface Command {
  id: string;
  /** i18n key (falls back to the text itself). */
  title: string;
  group: CommandGroup;
  icon?: IconName;
  /** Kbd notation, e.g. "Mod+K". */
  shortcut?: string;
  /** Bind `shortcut` globally in the shell (otherwise it is only displayed). */
  global?: boolean;
  /** Extra words the palette matches (both languages welcome). */
  keywords?: string[];
  /** Available in this context (default: always). */
  when?(ctx: CommandContext): boolean;
  run(ctx: CommandContext): void | Promise<void>;
}

const registry = new Map<string, Command>();
const listeners = new Set<() => void>();

function emit(): void {
  for (const l of Array.from(listeners)) l();
}

/** Add (or replace) a command; returns a function that removes it. */
export function registerCommand(cmd: Command): () => void {
  registry.set(cmd.id, cmd);
  emit();
  return () => {
    if (registry.get(cmd.id) === cmd) {
      registry.delete(cmd.id);
      emit();
    }
  };
}

export function registerCommands(cmds: readonly Command[]): () => void {
  const offs = cmds.map(registerCommand);
  return () => offs.forEach((off) => off());
}

export function subscribeCommands(cb: () => void): () => void {
  listeners.add(cb);
  return () => listeners.delete(cb);
}

export function getCommand(id: string): Command | undefined {
  return registry.get(id);
}

/** All registered commands (in registration order). */
export function allCommands(): Command[] {
  return [...registry.values()];
}

/** Context for running commands, from the current store state. */
export function commandContext(state: AppState = useAppStore.getState()): CommandContext {
  const route = state.route;
  return {
    state,
    workspace: state.workspace,
    t: translatorFor(state.locale),
    editor: getFocusedEditor(),
    pagePath: route.view === 'page' ? route.path : null,
    navigate: state.navigate,
  };
}

/** Localised title of a command. */
export function commandTitle(cmd: Command, t: Translator): string {
  return t(cmd.title);
}

/** Commands available in `ctx`. */
export function availableCommands(ctx: CommandContext = commandContext()): Command[] {
  return allCommands().filter((c) => {
    try {
      return c.when ? c.when(ctx) : true;
    } catch {
      return false;
    }
  });
}

/** Run a command by id (when available). Errors become an error toast. Returns whether it ran. */
export async function runCommand(id: string, ctx: CommandContext = commandContext()): Promise<boolean> {
  const cmd = registry.get(id);
  if (!cmd) return false;
  if (cmd.when && !cmd.when(ctx)) return false;
  try {
    await cmd.run(ctx);
    return true;
  } catch (e) {
    console.error(`[commands] ${id} failed`, e);
    ctx.state.toast({ kind: 'error', message: ctx.t('shell.error.command', { error: e instanceof Error ? e.message : String(e) }) });
    return false;
  }
}

// ---------------------------------------------------------------------------------------------
// Built-in commands
// ---------------------------------------------------------------------------------------------

const hasVault = (ctx: CommandContext): boolean => ctx.workspace !== null && ctx.state.status === 'ready';
const onPage = (ctx: CommandContext): boolean => hasVault(ctx) && ctx.pagePath !== null;
const withEditor = (ctx: CommandContext): boolean => hasVault(ctx) && ctx.editor !== null && ctx.editor.view.dom.isConnected;

function nav(id: string, title: string, route: Route, icon: IconName, extra: Partial<Command> = {}): Command {
  return { id, title, group: 'navigation', icon, when: hasVault, run: (ctx) => ctx.navigate(route), ...extra };
}

async function openJournalAt(ctx: CommandContext, days: number): Promise<void> {
  const ws = ctx.workspace;
  if (!ws) return;
  const path = await ws.openJournal(addDaysLocal(ws.today(), days));
  ctx.navigate({ view: 'page', path });
}

type EditorCmd = (target: EditorView) => boolean;

function editorCommand(id: string, title: string, run: EditorCmd, shortcut?: string, icon?: IconName): Command {
  return {
    id,
    title,
    group: 'editor',
    icon,
    shortcut,
    when: withEditor,
    run: (ctx) => {
      const view = ctx.editor?.view;
      if (!view) return;
      view.focus();
      run(view);
    },
  };
}

/** The built-in commands (labels are i18n keys in messages/shell.ts). */
export function builtinCommands(): Command[] {
  return [
    // App
    {
      id: 'app.palette',
      title: 'shell.cmd.palette',
      group: 'app',
      icon: 'keyboard',
      shortcut: 'Mod+K',
      global: true,
      run: (ctx) => ctx.state.setPaletteOpen(!useAppStore.getState().paletteOpen),
    },
    {
      id: 'app.switcher',
      title: 'shell.cmd.switcher',
      group: 'app',
      icon: 'search',
      shortcut: 'Mod+O',
      global: true,
      keywords: ['otwórz', 'open', 'strona', 'page'],
      when: hasVault,
      run: (ctx) => ctx.state.setSwitcherOpen(!useAppStore.getState().switcherOpen),
    },
    {
      id: 'app.capture',
      title: 'shell.cmd.capture',
      group: 'create',
      icon: 'inbox',
      keywords: ['szybka notatka', 'quick capture', 'dodaj', 'zapisz'],
      when: hasVault,
      run: (ctx) => ctx.state.setCaptureOpen(true),
    },
    {
      id: 'app.newPage',
      title: 'shell.cmd.newPage',
      group: 'create',
      icon: 'edit',
      keywords: ['utwórz', 'create', 'nowa', 'new'],
      when: hasVault,
      run: (ctx) => ctx.state.openDialog({ kind: 'new-page' }),
    },
    {
      id: 'app.toggleTheme',
      title: 'shell.cmd.toggleTheme',
      group: 'view',
      icon: 'moon',
      keywords: ['ciemny', 'jasny', 'dark', 'light', 'motyw', 'theme'],
      run: (ctx) => {
        const cur = ctx.state.theme;
        const dark =
          cur === 'dark' || (cur === 'system' && typeof matchMedia === 'function' && matchMedia('(prefers-color-scheme: dark)').matches);
        ctx.state.setTheme(dark ? 'light' : 'dark');
      },
    },
    {
      id: 'app.toggleSidebar',
      title: 'shell.cmd.toggleSidebar',
      group: 'view',
      icon: 'columns',
      shortcut: 'Mod+\\',
      global: true,
      when: hasVault,
      run: (ctx) => ctx.state.toggleSidebar(),
    },
    {
      id: 'app.toggleSidePanel',
      title: 'shell.cmd.toggleSidePanel',
      group: 'view',
      icon: 'columns',
      shortcut: 'Mod+Shift+\\',
      global: true,
      keywords: ['odnośniki', 'backlinks', 'konspekt', 'outline'],
      when: hasVault,
      run: (ctx) => ctx.state.toggleSidePanel(),
    },
    {
      id: 'app.language',
      title: 'shell.cmd.language',
      group: 'app',
      icon: 'info',
      keywords: ['język', 'language', 'english', 'polski'],
      run: (ctx) => ctx.state.setLocale(ctx.state.locale === 'pl' ? 'en' : 'pl'),
    },
    {
      id: 'app.switchVault',
      title: 'shell.cmd.switchVault',
      group: 'app',
      icon: 'folder',
      keywords: ['skarbiec', 'vault', 'folder'],
      run: (ctx) => ctx.state.openDialog({ kind: 'switch-vault' }),
    },

    // Navigation
    { ...nav('nav.back', 'shell.cmd.back', { view: 'journals' }, 'arrow-left'), shortcut: 'Alt+ArrowLeft', global: true, run: (ctx) => ctx.state.goBack(), when: (ctx) => hasVault(ctx) && ctx.state.back.length > 0 },
    {
      ...nav('nav.forward', 'shell.cmd.forward', { view: 'journals' }, 'arrow-right'),
      shortcut: 'Alt+ArrowRight',
      global: true,
      run: (ctx) => ctx.state.goForward(),
      when: (ctx) => hasVault(ctx) && ctx.state.forward.length > 0,
    },
    nav('nav.journals', 'shell.nav.journals', { view: 'journals' }, 'book', { keywords: ['journal', 'dziennik', 'dzisiaj'] }),
    nav('nav.calendar', 'shell.nav.calendar', { view: 'calendar' }, 'calendar', { keywords: ['calendar', 'kalendarz', 'agenda'] }),
    nav('nav.tasks', 'shell.nav.tasks', { view: 'tasks' }, 'tasks', { keywords: ['tasks', 'todo', 'zadania'] }),
    nav('nav.pages', 'shell.nav.pages', { view: 'pages' }, 'file', { keywords: ['pages', 'all', 'strony'] }),
    nav('nav.tags', 'shell.nav.tags', { view: 'tags' }, 'hash', { keywords: ['tags', 'tagi'] }),
    nav('nav.graph', 'shell.nav.graph', { view: 'graph' }, 'sparkles', { keywords: ['graph', 'graf', 'mapa'] }),
    nav('nav.search', 'shell.nav.search', { view: 'search' }, 'search', {
      shortcut: 'Mod+Shift+F',
      global: true,
      keywords: ['search', 'szukaj', 'znajdź', 'find'],
    }),
    nav('nav.sync', 'shell.nav.sync', { view: 'sync' }, 'repeat', { keywords: ['sync', 'synchronizacja', 'nextcloud', 'syncthing', 'konflikty'] }),
    nav('nav.importExport', 'shell.nav.importExport', { view: 'import-export' }, 'external', {
      keywords: ['import', 'export', 'eksport', 'obsidian', 'logseq', 'notion', 'evernote'],
    }),
    nav('nav.trash', 'shell.nav.trash', { view: 'trash' }, 'trash', { keywords: ['trash', 'kosz', 'usunięte'] }),
    {
      id: 'nav.settings',
      title: 'shell.nav.settings',
      group: 'navigation',
      icon: 'settings',
      shortcut: 'Mod+,',
      global: true,
      keywords: ['settings', 'ustawienia', 'preferencje', 'opcje'],
      when: hasVault,
      run: (ctx) => ctx.navigate({ view: 'settings' }),
    },

    // Journals
    {
      id: 'journal.today',
      title: 'shell.cmd.today',
      group: 'journal',
      icon: 'calendar-days',
      shortcut: 'Mod+J',
      global: true,
      keywords: ['dziś', 'today', 'dziennik'],
      when: hasVault,
      run: (ctx) => openJournalAt(ctx, 0),
    },
    {
      id: 'journal.tomorrow',
      title: 'shell.cmd.tomorrow',
      group: 'journal',
      icon: 'calendar-days',
      keywords: ['jutro', 'tomorrow'],
      when: hasVault,
      run: (ctx) => openJournalAt(ctx, 1),
    },
    {
      id: 'journal.yesterday',
      title: 'shell.cmd.yesterday',
      group: 'journal',
      icon: 'calendar-days',
      keywords: ['wczoraj', 'yesterday'],
      when: hasVault,
      run: (ctx) => openJournalAt(ctx, -1),
    },

    // Current page
    {
      id: 'page.rename',
      title: 'shell.cmd.rename',
      group: 'page',
      icon: 'pencil',
      keywords: ['rename', 'przenieś', 'move'],
      when: onPage,
      run: (ctx) => ctx.state.openDialog({ kind: 'rename', path: ctx.pagePath! }),
    },
    {
      id: 'page.delete',
      title: 'shell.cmd.delete',
      group: 'page',
      icon: 'trash',
      keywords: ['delete', 'kosz', 'remove'],
      when: onPage,
      run: (ctx) => ctx.state.openDialog({ kind: 'delete', path: ctx.pagePath! }),
    },
    {
      id: 'page.history',
      title: 'shell.cmd.history',
      group: 'page',
      icon: 'clock',
      keywords: ['history', 'wersje', 'versions', 'przywróć'],
      when: onPage,
      run: (ctx) => ctx.navigate({ view: 'history', path: ctx.pagePath! }),
    },
    {
      id: 'page.favourite',
      title: 'shell.cmd.favourite',
      group: 'page',
      icon: 'star',
      keywords: ['favourite', 'favorite', 'ulubione', 'gwiazdka'],
      when: onPage,
      run: (ctx) => {
        const key = ctx.workspace!.key;
        ctx.state.updateSettings((s) => toggleFavourite(s, key, ctx.pagePath!));
      },
    },
    {
      id: 'page.openSide',
      title: 'shell.cmd.openSide',
      group: 'page',
      icon: 'columns',
      when: onPage,
      run: (ctx) => ctx.state.openSidePanel({ view: 'page', path: ctx.pagePath! }),
    },
    {
      id: 'page.graph',
      title: 'shell.cmd.pageGraph',
      group: 'page',
      icon: 'sparkles',
      when: onPage,
      run: (ctx) => ctx.navigate({ view: 'graph', path: ctx.pagePath! }),
    },
    {
      id: 'page.copyLink',
      title: 'shell.cmd.copyLink',
      group: 'page',
      icon: 'link',
      keywords: ['link', 'kopiuj', 'copy', 'wikilink'],
      when: onPage,
      run: async (ctx) => {
        const title = ctx.pagePath!.replace(/\.md$/i, '').split('/').pop() ?? '';
        await navigator.clipboard?.writeText(`[[${title}]]`);
        ctx.state.toast({ kind: 'success', message: ctx.t('shell.toast.linkCopied') });
      },
    },

    // Sync
    {
      id: 'sync.now',
      title: 'shell.cmd.syncNow',
      group: 'sync',
      icon: 'repeat',
      keywords: ['sync', 'synchronizuj', 'nextcloud'],
      when: (ctx) => hasVault(ctx) && ctx.workspace!.sync !== null,
      run: async (ctx) => {
        const report = await ctx.workspace!.syncNow();
        if (report) ctx.state.toast({ kind: 'success', message: ctx.t('shell.toast.synced') });
      },
    },

    // Focused editor (shortcuts are bound by the editor keymap; shown here for discoverability)
    editorCommand('editor.toggleTask', 'shell.cmd.editor.toggleTask', cycleTask, 'Mod+Enter', 'check'),
    editorCommand('editor.indent', 'shell.cmd.editor.indent', indentItems, 'Tab'),
    editorCommand('editor.outdent', 'shell.cmd.editor.outdent', outdentItems, 'Shift+Tab'),
    editorCommand('editor.moveUp', 'shell.cmd.editor.moveUp', moveItemUp, 'Alt+ArrowUp', 'chevron-up'),
    editorCommand('editor.moveDown', 'shell.cmd.editor.moveDown', moveItemDown, 'Alt+ArrowDown', 'chevron-down'),
    editorCommand('editor.foldAll', 'shell.cmd.editor.foldAll', toggleFoldAll, 'Mod+Shift+.'),
    editorCommand('editor.zoomIn', 'shell.cmd.editor.zoomIn', zoomIn, 'Mod+Alt+ArrowRight'),
    editorCommand('editor.zoomOut', 'shell.cmd.editor.zoomOut', zoomOut, 'Mod+Alt+ArrowLeft'),
    editorCommand('editor.selectBlock', 'shell.cmd.editor.selectBlock', selectBlock, 'Mod+Shift+A'),
    editorCommand('editor.openLink', 'shell.cmd.editor.openLink', openLinkAtCursor, 'Alt+Enter', 'link'),
    editorCommand('editor.bold', 'shell.cmd.editor.bold', toggleInline('**'), 'Mod+B'),
    editorCommand('editor.italic', 'shell.cmd.editor.italic', toggleInline('*'), 'Mod+I'),
    editorCommand('editor.highlight', 'shell.cmd.editor.highlight', toggleInline('=='), 'Mod+Shift+H'),
    editorCommand('editor.code', 'shell.cmd.editor.code', toggleInline('`'), 'Mod+E'),
  ];
}

let builtinsRegistered: (() => void) | null = null;

/** Register the built-in commands once (idempotent). */
export function registerBuiltinCommands(): () => void {
  builtinsRegistered ??= registerCommands(builtinCommands());
  return builtinsRegistered;
}

// ---------------------------------------------------------------------------------------------
// Shortcuts
// ---------------------------------------------------------------------------------------------

export interface ParsedShortcut {
  mod: boolean;
  ctrl: boolean;
  meta: boolean;
  alt: boolean;
  shift: boolean;
  /** Lower-cased `KeyboardEvent.key`. */
  key: string;
}

/** Parse "Mod+Shift+F" (Kbd notation). */
export function parseShortcut(s: string): ParsedShortcut {
  const parts = s.split('+');
  // "Mod++" style: a trailing empty part means the key is '+'.
  let key = parts.pop() ?? '';
  if (key === '' && parts.length > 0 && parts[parts.length - 1] === '') {
    parts.pop();
    key = '+';
  }
  const mods = parts.map((p) => p.toLowerCase());
  return {
    mod: mods.includes('mod'),
    ctrl: mods.includes('ctrl'),
    meta: mods.includes('meta') || mods.includes('cmd'),
    alt: mods.includes('alt') || mods.includes('option'),
    shift: mods.includes('shift'),
    key: key.toLowerCase(),
  };
}

/** Physical key → character, for layouts where Shift changes `key` (e.g. "Mod+Shift+." gives ">"). */
const CODE_KEYS: Record<string, string> = {
  Period: '.',
  Comma: ',',
  Backslash: '\\',
  Slash: '/',
  Minus: '-',
  Equal: '=',
  BracketLeft: '[',
  BracketRight: ']',
  Semicolon: ';',
  Quote: "'",
  Backquote: '`',
};

/** Does a keyboard event match a parsed shortcut? `apple`: Mod = ⌘, else Ctrl. */
export function matchesShortcut(
  e: Pick<KeyboardEvent, 'key' | 'code' | 'ctrlKey' | 'metaKey' | 'altKey' | 'shiftKey'>,
  sc: ParsedShortcut,
  apple: boolean,
): boolean {
  const wantCtrl = sc.ctrl || (sc.mod && !apple);
  const wantMeta = sc.meta || (sc.mod && apple);
  if (e.ctrlKey !== wantCtrl || e.metaKey !== wantMeta || e.altKey !== sc.alt || e.shiftKey !== sc.shift) return false;
  const key = e.key.toLowerCase();
  if (key === sc.key) return true;
  const fromCode = CODE_KEYS[e.code];
  if (fromCode && fromCode === sc.key) return true;
  // Letters on non-Latin layouts: fall back to the physical key.
  return /^key[a-z]$/i.test(e.code) && e.code.slice(3).toLowerCase() === sc.key;
}
