/**
 * What the editor needs from the rest of the app.
 *
 * `EditorWorkspace` is the subset of the Workspace service (docs/UI.md) used by the editor and
 * its CodeMirror extensions. It is shaped so that the real `Workspace` satisfies it
 * structurally: `index` is a `VaultIndex`, `platform` a `Platform`; the shell only has to add
 * `onFileChanged` (= `vault.onChange`) and, optionally, `listTemplates` and `modify`.
 *
 * `EditorContext` is the per-editor view of it (one note path) and is handed to CodeMirror
 * extensions through the `editorContext` facet, so widgets and commands can reach the vault:
 *
 * ```ts
 * const ctx = view.state.facet(editorContext);   // EditorContext | null
 * ```
 *
 * React side: the shell wraps editors in `<EditorHostProvider value={host}>`; `<NoteEditor>`
 * turns the host + its `path` into an EditorContext.
 */
import { Facet } from '@codemirror/state';
import type { EditorState } from '@codemirror/state';
import { createContext, useContext } from 'react';
import type { Translator } from '@/core/i18n';
import type {
  BlockRef,
  FsEventType,
  LocalDate,
  LocalDateTime,
  Locale,
  ParsedNote,
  VaultConfig,
  VaultPath,
} from '@/core/types';

// ---------------------------------------------------------------------------------------------
// Workspace subset
// ---------------------------------------------------------------------------------------------

/** One `[[` completion candidate (VaultIndex.suggestPages). */
export interface PageSuggestion {
  path: VaultPath;
  title: string;
  /** Set when the match came from a frontmatter alias. */
  alias?: string;
}

/** Index accessors the editor uses — a structural subset of `VaultIndex` (@/core/index). */
export interface EditorIndex {
  suggestPages(prefix: string, limit?: number): PageSuggestion[];
  suggestBlocks(text: string, limit?: number): BlockRef[];
  resolveLink(target: string, from?: VaultPath): VaultPath | null;
  blockById(id: string): BlockRef | null;
  note(path: VaultPath): ParsedNote | undefined;
  /**
   * Tags with usage counts (most used first is nice but not required). `tag` may be the
   * canonical lower-case form; `display` is the spelling as written (VaultIndex has both).
   */
  tags(): { tag: string; count: number; display?: string }[];
  content(path: VaultPath): string | undefined;
  paths(): VaultPath[];
}

/** A file change (same shape as the vault's `VaultChange`). */
export interface FileChange {
  type: FsEventType;
  path: VaultPath;
  oldPath?: VaultPath;
  /** 'user' | 'external' | 'sync' | 'import' | 'system' — informative only. */
  source?: string;
}

/** The parts of the Workspace service (docs/UI.md) the editor needs. */
export interface EditorWorkspace {
  readonly config: VaultConfig;
  readonly index: EditorIndex;
  readonly platform: { openExternal(url: string): Promise<void> };
  read(path: VaultPath): Promise<string>;
  /** Debounced editor saves go here (when `modify` is missing, and to create new notes). */
  write(path: VaultPath, content: string): Promise<void>;
  /**
   * Read-modify-write of one file, serialised with other writes to it (the Workspace has it).
   * Editors save through it so a save never overwrites a change that landed after the editor's
   * last read (another editor of the same note, sync, quick capture): the change is merged.
   * Without it, saves read the file first and then write (not atomic).
   */
  modify?(path: VaultPath, fn: (content: string) => string): Promise<string>;
  /**
   * Subscribe to file changes (own writes may be reported too — the editor compares contents).
   * Returns an unsubscribe function. The Workspace implements it as `vault.onChange`.
   */
  onFileChanged(cb: (changes: readonly FileChange[]) => void): () => void;
  /** URL to display an attachment (Tauri asset URL / blob URL); null when unknown. */
  assetUrl(path: VaultPath): string | null;
  /** Store pasted/dropped bytes in the attachments folder; returns the (unique) vault path. */
  saveAttachment(name: string, data: Uint8Array): Promise<VaultPath>;
  /** Id of the block at `line` of `path`, adding ` ^id` to the file when missing. */
  ensureBlockId(path: VaultPath, line: number): Promise<string>;
  /** Template files; default: Markdown files under `config.templatesFolder` from the index. */
  listTemplates?(): VaultPath[];
}

// ---------------------------------------------------------------------------------------------
// Routes, settings, dialogs
// ---------------------------------------------------------------------------------------------

/** The navigation targets the editor produces — a subset of the shell's `Route` (docs/UI.md). */
export type EditorRoute =
  | { view: 'page'; path: VaultPath; line?: number; blockId?: string }
  | { view: 'journals' }
  | { view: 'calendar'; date?: LocalDate }
  | { view: 'tags'; tag?: string }
  | { view: 'search'; q?: string };

/** Per-device editor settings (DeviceSettings.editor + font size). */
export interface EditorSettings {
  livePreview: boolean;
  spellcheck: boolean;
  /** Max line width in `rem` (NoteEditor sets `--editor-measure`); 0 = full width. */
  lineWidth: number;
  outlinerMode: boolean;
  vimMode: boolean;
  /** Editor font size in px (DeviceSettings.fontSize); undefined = theme default. */
  fontSize?: number;
}

export const DEFAULT_EDITOR_SETTINGS: EditorSettings = {
  livePreview: true,
  spellcheck: true,
  lineWidth: 46,
  outlinerMode: true,
  vimMode: false,
};

/** Recurrence picked in the "Powtarzaj…" dialog: canonical English rule text. */
export interface RecurrencePick {
  rule: string;
}

/**
 * Dialogs extensions can ask the React host (NoteEditor → dialogs.tsx) to show. The host keeps
 * the editor focused/restored after closing; `onSubmit` is not called when cancelled, `onCancel`
 * is (also when the editor goes away or another dialog replaces this one).
 */
export type EditorDialogRequest = (
  | {
      kind: 'date';
      title: string;
      withTime?: boolean;
      initial?: LocalDate | LocalDateTime;
      onSubmit(value: LocalDate | LocalDateTime): void;
    }
  | {
      kind: 'reminder';
      initial?: { at?: LocalDateTime; alarm?: boolean; recurrence?: string };
      /** Block text shown as context in the dialog. */
      context?: string;
      onSubmit(value: { at: LocalDateTime; alarm: boolean; recurrence?: string }): void;
    }
  | { kind: 'recurrence'; initial?: string; onSubmit(value: RecurrencePick): void }
  | { kind: 'template'; templates: VaultPath[]; onSubmit(path: VaultPath): void }
) & { onCancel?(): void };

export type DialogOpener = (request: EditorDialogRequest) => void;

// ---------------------------------------------------------------------------------------------
// EditorContext + CodeMirror facets
// ---------------------------------------------------------------------------------------------

export interface EditorContext {
  path: VaultPath;
  workspace: EditorWorkspace;
  t: Translator;
  locale: Locale;
  now(): Date;
  navigate(route: EditorRoute, opts?: { newPane?: boolean }): void;
  /** Resolves (or creates) the page for a link target and opens it. */
  openLink(target: string, opts?: { newPane?: boolean }): void;
  assetUrl(path: VaultPath): string | null;
}

/** The current editor's context (null when an editor is created without one, e.g. in tests). */
export const editorContext = Facet.define<EditorContext, EditorContext | null>({
  combine: (values) => values[0] ?? null,
});

/** Editor settings for extensions (spellcheck, outliner mode, ...). */
export const editorSettings = Facet.define<Partial<EditorSettings>, EditorSettings>({
  combine: (values) => Object.assign({}, DEFAULT_EDITOR_SETTINGS, ...values),
});

/** How extensions open React dialogs; no-op without a host. */
export const editorDialogs = Facet.define<DialogOpener, DialogOpener>({
  combine: (values) => values[0] ?? (() => {}),
});

export function getEditorContext(state: EditorState): EditorContext | null {
  return state.facet(editorContext);
}

/** `now()` of the editor context, or the real clock. */
export function editorNow(state: EditorState): Date {
  return state.facet(editorContext)?.now() ?? new Date();
}

/** Template files of the vault (sorted). */
export function templatePaths(ws: EditorWorkspace): VaultPath[] {
  if (ws.listTemplates) return ws.listTemplates();
  const folder = ws.config.templatesFolder.replace(/\/+$/, '');
  if (!folder) return [];
  const prefix = `${folder.toLowerCase()}/`;
  return ws.index
    .paths()
    .filter((p) => p.toLowerCase().startsWith(prefix) && p.toLowerCase().endsWith('.md'))
    .sort((a, b) => a.localeCompare(b));
}

// ---------------------------------------------------------------------------------------------
// React: the host the shell provides
// ---------------------------------------------------------------------------------------------

/** Everything an editor needs except its path. */
export interface EditorHost {
  workspace: EditorWorkspace;
  t: Translator;
  locale: Locale;
  now(): Date;
  navigate(route: EditorRoute, opts?: { newPane?: boolean }): void;
  openLink(target: string, opts?: { newPane?: boolean; from?: VaultPath }): void;
  settings?: Partial<EditorSettings>;
}

export const EditorHostContext = createContext<EditorHost | null>(null);
export const EditorHostProvider = EditorHostContext.Provider;

export function useEditorHost(): EditorHost {
  const host = useContext(EditorHostContext);
  if (!host) throw new Error('NoteEditor needs an <EditorHostProvider> (src/app/editor/context.ts)');
  return host;
}

/**
 * EditorContext for one note. `t` and `locale` are read from the host on use; `path` may be
 * reassigned later (the note was renamed while open), links then resolve from the new location.
 */
export function contextFor(host: EditorHost, path: VaultPath): EditorContext {
  const ctx: EditorContext = {
    path,
    workspace: host.workspace,
    // Getters: a host whose language changes at runtime is followed.
    get t() {
      return host.t;
    },
    get locale() {
      return host.locale;
    },
    now: () => host.now(),
    navigate: (route, opts) => host.navigate(route, opts),
    openLink: (target, opts) => host.openLink(target, { ...opts, from: ctx.path }),
    assetUrl: (p) => host.workspace.assetUrl(p),
  };
  return ctx;
}
