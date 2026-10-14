/**
 * One open note in a CodeMirror view: loading, debounced saving, following external changes
 * (minimal-diff replace, or a 3-way merge when there are unsaved edits), targeting a line or
 * block and outliner zoom. React-free so it can be tested in jsdom; NoteEditor wraps it.
 *
 * Saving: edits are written `saveDelay` ms after the last change and immediately on blur,
 * `visibilitychange` (hidden), `beforeunload`, `pagehide`, switching to read-only and
 * `destroy()`. Saves and reloads of external changes run one at a time on one queue, so a
 * reload never races a save. A save writes the editor's newest text (the text at flush time
 * once the view is gone) through `workspace.modify`: when the file changed since the editor
 * last read it (another editor of the note, sync, quick capture), the change is merged instead
 * of overwritten. Line endings of the file (LF / CRLF) are kept.
 */
import { Annotation, EditorSelection, Transaction, type Extension } from '@codemirror/state';
import { EditorView } from '@codemirror/view';
import { isNotFoundError } from '@/core/storage';
import { merge3 } from '@/core/sync';
import type { VaultPath } from '@/core/types';
import type { EditorWorkspace, FileChange } from './context';
import { detectEol, diffChanges, normalizeEol, withEol } from './diff';
import { clearFocusedEditor, getFocusedEditor, setFocusedEditor } from './focus';
import { setZoom, zoomCrumbs, zoomField, zoomRangeForLine, type ZoomCrumb } from './zoom';

/** Marks transactions that apply external file changes (not user edits). */
export const externalChange = Annotation.define<boolean>();

/**
 * Saves still being written, per workspace and path. A session opening a path waits for them
 * before reading, so a note re-opened right after its editor closed (navigation, a remount)
 * never loads the text from before the last keystrokes.
 */
const pendingSaves = new WeakMap<EditorWorkspace, Map<VaultPath, Promise<void>>>();

function trackPendingSave(ws: EditorWorkspace, path: VaultPath, save: Promise<void>): void {
  let map = pendingSaves.get(ws);
  if (!map) pendingSaves.set(ws, (map = new Map()));
  map.set(path, save);
  void save.finally(() => {
    if (map.get(path) === save) map.delete(path);
  });
}

/** Resolves when saves of `path` started by (possibly destroyed) sessions have finished. */
export function pendingSave(ws: EditorWorkspace, path: VaultPath): Promise<void> {
  return pendingSaves.get(ws)?.get(path) ?? Promise.resolve();
}

export type SessionStatus = 'loading' | 'ready' | 'error';

export interface SessionCallbacks {
  onStatus?(status: SessionStatus, error?: unknown): void;
  onSaveError?(error: unknown): void;
  /** An external change was merged into unsaved edits (`clean` = no conflicting regions). */
  onMerged?(clean: boolean): void;
  onZoom?(crumbs: ZoomCrumb[] | null, line: number | null): void;
  onFocus?(focused: boolean): void;
  onDeleted?(): void;
  onRenamed?(path: VaultPath): void;
}

export interface RevealTarget {
  /** 0-based line. */
  line?: number;
  blockId?: string;
}

export interface SessionOptions {
  path: VaultPath;
  workspace: EditorWorkspace;
  parent: HTMLElement;
  /** Extensions for the view (built by the caller, e.g. editorExtensions()). */
  extensions: () => Extension[];
  readOnly?: boolean;
  saveDelay?: number;
  callbacks?: SessionCallbacks;
}

/** 0-based line of a block id (` ^id` at a line end, or a Logseq `id:: id` property). */
export function lineOfBlockId(text: string, id: string): number | null {
  const lines = text.split('\n');
  const esc = id.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const caret = new RegExp(`(^|\\s)\\^${esc}\\s*$`);
  const prop = new RegExp(`^\\s*id::\\s*${esc}\\s*$`, 'i');
  for (let i = 0; i < lines.length; i++) {
    const l = lines[i]!;
    if (caret.test(l)) {
      // A standalone `^id` line belongs to the block above it.
      if (/^\s*\^/.test(l) && i > 0) return i - 1;
      return i;
    }
    if (prop.test(l)) {
      // Logseq: the property sits under the block's first line.
      for (let j = i - 1; j >= 0; j--) if (/^\s*([-*+]|\d+[.)])\s/.test(lines[j]!)) return j;
      return i;
    }
  }
  return null;
}

export class EditorSession {
  path: VaultPath;
  view: EditorView | null = null;
  status: SessionStatus = 'loading';
  /** Text known to be in the file (normalised to '\n'); null before loading. */
  private lastSaved: string | null = null;
  /** Text currently being written. */
  private inflight: string | null = null;
  /** Text of the latest flush request (what is written once the view is gone). */
  private latest: string | null = null;
  private reloadQueued = false;
  /** A file change arrived before the view existed: re-read once it does. */
  private changedWhileLoading = false;
  private exists = true;
  private deleted = false;
  private eol: '\n' | '\r\n' = '\n';
  private timer: ReturnType<typeof setTimeout> | null = null;
  private chain: Promise<void> = Promise.resolve();
  private destroyed = false;
  private readonly cleanups: (() => void)[] = [];
  private pendingReveal: RevealTarget | null = null;
  private pendingZoom: number | null | undefined = undefined;
  private wantFocus = false;
  /** Last breadcrumbs reported through onZoom ('' = not zoomed). */
  private crumbsKey = '';
  private readonly opts: SessionOptions;

  constructor(opts: SessionOptions) {
    this.opts = opts;
    this.path = opts.path;
  }

  private get cb(): SessionCallbacks {
    return this.opts.callbacks ?? {};
  }

  /** Load the file and create the view. */
  async start(init: { reveal?: RevealTarget; zoomLine?: number | null; autoFocus?: boolean } = {}): Promise<void> {
    if (init.reveal) this.pendingReveal = init.reveal;
    if (init.zoomLine !== undefined) this.pendingZoom = init.zoomLine;
    if (init.autoFocus) this.wantFocus = true;
    this.cleanups.push(this.opts.workspace.onFileChanged((changes) => this.onFileChanged(changes)));
    let raw: string;
    try {
      await pendingSave(this.opts.workspace, this.path);
      if (this.destroyed) return;
      raw = await this.opts.workspace.read(this.path);
    } catch (e) {
      if (!isNotFoundError(e)) {
        if (!this.destroyed) this.setStatus('error', e);
        return;
      }
      raw = '';
      this.exists = false;
    }
    if (this.destroyed) return;
    this.eol = detectEol(raw);
    const text = normalizeEol(raw);
    this.lastSaved = text;
    this.createView(text);
    this.setStatus('ready');
    // The file changed while it was being read: the read may have returned the older text.
    if (this.changedWhileLoading) this.queueReload();
  }

  private setStatus(status: SessionStatus, error?: unknown): void {
    this.status = status;
    this.cb.onStatus?.(status, error);
  }

  private createView(text: string): void {
    const listener = EditorView.updateListener.of((u) => {
      if (u.docChanged && !u.transactions.every((tr) => tr.annotation(externalChange))) this.scheduleSave();
      if (u.focusChanged) {
        const focused = u.view.hasFocus;
        if (focused) setFocusedEditor(u.view, this.path);
        else void this.flush();
        this.cb.onFocus?.(focused);
      }
      const z0 = u.startState.field(zoomField, false);
      const z1 = u.state.field(zoomField, false);
      if (z0 !== z1 && (!z0 || !z1 || z0.from !== z1.from || u.docChanged)) {
        // Typing inside a zoomed item maps the range on every keystroke: report only real
        // changes of the breadcrumbs (no re-render of the host per keystroke).
        const crumbs = zoomCrumbs(u.state);
        const key = crumbs ? JSON.stringify(crumbs) : '';
        if (key !== this.crumbsKey) {
          this.crumbsKey = key;
          this.cb.onZoom?.(crumbs, crumbs ? crumbs[crumbs.length - 1]!.line : null);
        }
      }
    });
    this.view = new EditorView({
      doc: text,
      parent: this.opts.parent,
      extensions: [this.opts.extensions(), listener],
    });
    const onVisibility = () => {
      if (document.visibilityState === 'hidden') void this.flush();
    };
    const onUnload = () => void this.flush();
    document.addEventListener('visibilitychange', onVisibility);
    window.addEventListener('beforeunload', onUnload);
    window.addEventListener('pagehide', onUnload);
    this.cleanups.push(() => {
      document.removeEventListener('visibilitychange', onVisibility);
      window.removeEventListener('beforeunload', onUnload);
      window.removeEventListener('pagehide', onUnload);
    });
    if (this.pendingZoom !== undefined) {
      this.setZoomLine(this.pendingZoom);
      this.pendingZoom = undefined;
    }
    if (this.pendingReveal) {
      this.reveal(this.pendingReveal);
      this.pendingReveal = null;
    }
    if (this.wantFocus) this.focus();
  }

  /** The view exists and the session was not destroyed. */
  get alive(): boolean {
    return !this.destroyed && this.view !== null;
  }

  /**
   * Read-only sessions never save (the view's editability is configured separately). Edits
   * typed before switching to read-only are saved right away.
   */
  setReadOnly(readOnly: boolean): void {
    if (readOnly && !this.opts.readOnly && this.dirty) void this.flush();
    this.opts.readOnly = readOnly;
    if (!readOnly && this.dirty) this.scheduleSave();
  }

  focus(): void {
    const view = this.view;
    if (!view) {
      this.wantFocus = true;
      return;
    }
    view.focus();
  }

  /** Current editor text ('\n' line endings). */
  text(): string | null {
    return this.view ? this.view.state.doc.toString() : null;
  }

  /** Unsaved local edits? */
  get dirty(): boolean {
    const t = this.text();
    return t !== null && t !== this.lastSaved && t !== this.inflight;
  }

  // -------------------------------------------------------------------------------------------
  // Saving
  // -------------------------------------------------------------------------------------------

  scheduleSave(): void {
    if (this.opts.readOnly) return;
    if (this.timer) clearTimeout(this.timer);
    this.timer = setTimeout(() => {
      this.timer = null;
      void this.flush();
    }, this.opts.saveDelay ?? 400);
  }

  /** Run `step` after everything queued before it (saves and reloads never overlap). */
  private enqueue(step: () => Promise<void>): Promise<void> {
    this.chain = this.chain.then(step).catch((e: unknown) => console.warn('[editor] save queue', e));
    return this.chain;
  }

  /** Write pending edits now (resolves when written). */
  flush(): Promise<void> {
    if (this.timer) {
      clearTimeout(this.timer);
      this.timer = null;
    }
    const text = this.text();
    if (text === null || this.opts.readOnly) return this.chain;
    this.latest = text;
    const path = this.path;
    const saved = this.enqueue(() => this.write());
    trackPendingSave(this.opts.workspace, path, saved);
    return saved;
  }

  private async write(): Promise<void> {
    // A save queued behind others writes the newest text, not the one from when it was queued.
    const text = (this.alive ? this.text() : null) ?? this.latest;
    const base = this.lastSaved;
    if (text === null || base === null || text === base) return;
    if (!this.exists && text === '') return;
    this.inflight = text;
    let ok = false;
    try {
      const saved = await this.save(text, base);
      this.lastSaved = saved.text;
      this.exists = true;
      this.deleted = false;
      ok = true;
      if (saved.text !== text) {
        // Someone else's change was merged into this save: show it in the editor too.
        const current = this.text();
        if (current !== null && this.alive) this.applyText(current === text ? saved.text : merge3(text, current, saved.text).text);
        this.cb.onMerged?.(saved.clean);
      }
    } catch (e) {
      this.cb.onSaveError?.(e);
    } finally {
      this.inflight = null;
    }
    // Typing during the save (or a merge) leaves the editor ahead of the file: save again.
    if (ok && !this.destroyed && !this.timer && this.dirty) this.scheduleSave();
  }

  /**
   * Write `text`, an edit of `base` (the file as the editor last knew it). When the file no
   * longer is `base`, the two changes are merged (conflicting regions keep the editor's text,
   * see merge3) rather than the other change being overwritten.
   */
  private async save(text: string, base: string): Promise<{ text: string; clean: boolean }> {
    const ws = this.opts.workspace;
    let result = { text, clean: true };
    const merge = (raw: string): string => {
      const disk = normalizeEol(raw);
      if (disk === base || disk === text) result = { text, clean: true };
      else {
        const m = merge3(base, text, disk);
        result = { text: m.text, clean: m.clean };
      }
      return withEol(result.text, this.eol);
    };
    if (this.exists && !this.deleted) {
      try {
        if (ws.modify) await ws.modify(this.path, merge);
        else {
          const raw = await ws.read(this.path);
          const next = merge(raw);
          if (next !== raw) await ws.write(this.path, next);
        }
        return result;
      } catch (e) {
        // Deleted meanwhile: typing into the open note creates it again (below).
        if (!isNotFoundError(e)) throw e;
      }
    }
    await ws.write(this.path, withEol(text, this.eol));
    return { text, clean: true };
  }

  // -------------------------------------------------------------------------------------------
  // External changes
  // -------------------------------------------------------------------------------------------

  private onFileChanged(changes: readonly FileChange[]): void {
    for (const c of changes) {
      if (c.type === 'rename' && c.oldPath === this.path) {
        this.path = c.path;
        // Keep the registry's path current, without making this editor the focused one.
        if (this.view && getFocusedEditor()?.view === this.view) setFocusedEditor(this.view, this.path);
        this.cb.onRenamed?.(c.path);
      } else if (c.path === this.path) {
        if (c.type === 'delete') {
          this.deleted = true;
          this.cb.onDeleted?.();
        } else if (!this.view) this.changedWhileLoading = true;
        else this.queueReload();
      }
    }
  }

  /** Reload after the saves queued so far (one pending reload covers any number of events). */
  private queueReload(): void {
    if (this.reloadQueued) return;
    this.reloadQueued = true;
    void this.enqueue(() => {
      this.reloadQueued = false;
      return this.reloadExternal();
    });
  }

  /** Re-read the file and fold an external change into the editor. */
  async reloadExternal(): Promise<void> {
    if (!this.view || this.lastSaved === null) return;
    let raw: string;
    try {
      raw = await this.opts.workspace.read(this.path);
    } catch {
      return;
    }
    const view = this.view;
    if (this.destroyed || !view) return;
    const incoming = normalizeEol(raw);
    this.deleted = false;
    this.exists = true;
    // Our own write echoing back.
    if (incoming === this.lastSaved || incoming === this.inflight) return;
    const current = view.state.doc.toString();
    if (incoming === current) {
      this.lastSaved = incoming;
      return;
    }
    // The file's line endings may have changed too (e.g. converted to LF elsewhere).
    if (raw.includes('\n')) this.eol = detectEol(raw);
    const base = this.lastSaved;
    if (current === base && this.inflight === null) {
      this.applyText(incoming);
      this.lastSaved = incoming;
      return;
    }
    const merged = merge3(base, current, incoming);
    this.lastSaved = incoming;
    this.applyText(merged.text);
    this.cb.onMerged?.(merged.clean);
    if (merged.text !== incoming) this.scheduleSave();
  }

  /**
   * Replace the document with `text` using minimal changes, one per changed region, so the
   * selection and scroll position survive edits above and below them.
   */
  applyText(text: string): void {
    const view = this.view;
    if (!view || this.destroyed) return;
    const changes = diffChanges(view.state.doc.toString(), text);
    if (changes.length === 0) return;
    view.dispatch({
      changes,
      filter: false,
      annotations: [externalChange.of(true), Transaction.addToHistory.of(false), Transaction.remote.of(true)],
    });
  }

  // -------------------------------------------------------------------------------------------
  // Targets
  // -------------------------------------------------------------------------------------------

  /** Put the cursor on a line / block and scroll it into view. */
  reveal(target: RevealTarget): void {
    if (this.destroyed) return;
    const view = this.view;
    if (!view) {
      this.pendingReveal = target;
      return;
    }
    const doc = view.state.doc;
    let line = target.line;
    if (target.blockId) line = lineOfBlockId(doc.toString(), target.blockId) ?? line;
    if (line === undefined || line === null) return;
    const n = Math.max(1, Math.min(doc.lines, line + 1));
    const pos = doc.line(n).to;
    view.dispatch({
      selection: EditorSelection.cursor(pos),
      effects: EditorView.scrollIntoView(pos, { y: 'center' }),
    });
  }

  /** Zoom into the item at a 0-based line (null = whole page). */
  setZoomLine(line: number | null): void {
    if (this.destroyed) return;
    const view = this.view;
    if (!view) {
      this.pendingZoom = line;
      return;
    }
    const range = line === null ? null : zoomRangeForLine(view.state.doc, line);
    const current = view.state.field(zoomField, false) ?? null;
    if (!range && !current) return;
    if (range && current && range.from === current.from && range.to === current.to) return;
    view.dispatch({
      effects: [setZoom.of(range), EditorView.scrollIntoView(range ? range.from : view.state.selection.main.head, { y: 'start' })],
      selection: range ? EditorSelection.cursor(view.state.doc.lineAt(range.from).to) : undefined,
    });
  }

  // -------------------------------------------------------------------------------------------
  // Teardown
  // -------------------------------------------------------------------------------------------

  /** Flush pending edits and destroy the view. Returns the final save. */
  destroy(): Promise<void> {
    if (this.destroyed) return this.chain;
    const saved = this.flush();
    this.destroyed = true;
    for (const c of this.cleanups.splice(0)) c();
    if (this.view) {
      clearFocusedEditor(this.view);
      this.view.destroy();
    }
    return saved;
  }
}
