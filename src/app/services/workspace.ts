/**
 * Workspace service (docs/UI.md): glues the core modules for one open vault — Vault (files),
 * VaultIndex (search, links, tasks), History, ReminderScheduler and, when Nextcloud is
 * configured, a SyncScheduler. Views and editors go through it for every change, so the
 * index follows immediately and writes to one file never interleave.
 *
 * It structurally satisfies `EditorWorkspace` (src/app/editor/context.ts).
 *
 * Created with {@link openWorkspace}; `ready` resolves when the initial indexing is done
 * (the workspace is usable before that; `progress` / `onProgress` report it).
 */
import { todayLocal } from '@/core/dates';
import { VaultIndex } from '@/core/index';
import {
  applyTemplate,
  ensureBlockId as ensureBlockIdIn,
  journalDateFromPath,
  journalPathForDate,
  renameLinksInContent,
  setTaskStatus as setTaskStatusIn,
  toggleTask as toggleTaskIn,
} from '@/core/markdown';
import {
  basename,
  dirname,
  extname,
  isConflictCopy,
  isIgnoredPath,
  isMarkdown,
  joinPath,
  normalizePath,
  pageTitle,
  sanitizeFileName,
  uniquePath,
} from '@/core/paths';
import { ReminderScheduler } from '@/core/reminders';
import { CONFIG_PATH, History, isTextPath, LocalStore, Vault, type ChangeSource, type VaultChange } from '@/core/storage';
import {
  findSyncthingConflicts,
  nextcloudDavUrl,
  SyncEngine,
  SyncScheduler,
  WebDavClient,
  type ConflictCopy,
  type SyncReport,
  type SyncStatus,
} from '@/core/sync';
import type {
  ExportedFile,
  FileSystemAdapter,
  FileStat,
  Importer,
  LocalDate,
  LocalDateTime,
  Locale,
  PickedFile,
  Platform,
  TaskStatus,
  VaultConfig,
  VaultLocation,
  VaultPath,
} from '@/core/types';
import type { FileChange } from '@/app/editor/context';
import type { NextcloudSettings, ReminderSettings } from '../state/settings';
import { vaultKey } from '../state/settings';
import {
  addTokenToBlock,
  appendBlock,
  appendImportSection,
  buildCaptureBlock,
  mimeForPath,
  pathForTitle,
  reminderToken,
  type CaptureOptions,
} from './content';

/** A conflict copy (Syncthing `.sync-conflict-` or Brulion `.conflict-`) and its original. */
export type ConflictInfo = ConflictCopy;

/** What `importFiles` did. */
export interface ImportReport {
  /** Files written at their own path (new files). */
  created: VaultPath[];
  /** Existing journal pages the imported content was appended to. */
  appended: VaultPath[];
  /** Files that collided with existing ones and were written under a new name. */
  renamed: { from: VaultPath; to: VaultPath }[];
  warnings: string[];
  errors: { path: VaultPath; message: string }[];
}

export interface ImportProgress {
  done: number;
  total: number;
}

export interface WorkspaceOptions {
  /** Device name (conflict copies of the WebDAV sync). */
  device?: string;
  /** Clock (journals, quick capture, task completion dates). */
  now?: () => Date;
  locale?: Locale;
  reminders?: ReminderSettings;
  /** Nextcloud sync settings; sync is configured when enabled. */
  nextcloud?: NextcloudSettings;
  /** Use this file system instead of `platform.openFileSystem(location)` (tests). */
  fs?: FileSystemAdapter;
  /** Use this device-local store instead of `LocalStore.open('vault:<key>')` (tests). */
  store?: LocalStore;
  /** Markdown files read per indexing step (default 40). */
  indexChunk?: number;
  /** Watch the file system for external changes (default true). */
  watch?: boolean;
}

/** Most attachments preloaded as blob URLs for embeds (memory bound for big vaults). */
const MAX_PRELOADED_ASSETS = 300;

/** Reasonable delay before re-planning reminders after edits. */
const REMINDER_REFRESH_MS = 1500;

/** Let the browser breathe between indexing chunks. */
function yieldToEventLoop(): Promise<void> {
  const sched = (globalThis as { scheduler?: { yield?: () => Promise<void> } }).scheduler;
  if (typeof sched?.yield === 'function') return sched.yield();
  return new Promise((resolve) => setTimeout(resolve, 0));
}

function errorMessage(e: unknown): string {
  return e instanceof Error ? e.message : String(e);
}

/** Markdown files that belong in the index (conflict copies and ignored paths do not). */
export function isIndexable(path: VaultPath): boolean {
  return isMarkdown(path) && !isConflictCopy(path) && !isIgnoredPath(path);
}

/** Non-Markdown files the index knows as link targets (`![[photo.png]]`). */
export function isAttachment(path: VaultPath): boolean {
  return path !== '' && !isMarkdown(path) && !isConflictCopy(path) && !isIgnoredPath(path) && path !== CONFIG_PATH;
}

type Listener = () => void;

export class Workspace {
  readonly platform: Platform;
  readonly location: VaultLocation;
  /** Stable id of the location (see `vaultKey`). */
  readonly key: string;
  readonly vault: Vault;
  readonly index: VaultIndex;
  readonly history: History;
  readonly store: LocalStore;
  /** Resolves when the initial indexing is done. */
  readonly ready: Promise<void>;
  progress: { indexed: number; total: number } = { indexed: 0, total: 0 };

  private readonly now: () => Date;
  private locale: Locale;
  private readonly device: string;
  private readonly indexChunk: number;
  private readonly chains = new Map<VaultPath, Promise<void>>();
  /** Bumped on every change of a path; stale initial-index reads are dropped. */
  private readonly gens = new Map<VaultPath, number>();
  private changeChain: Promise<void> = Promise.resolve();
  private readonly changeListeners = new Set<Listener>();
  private readonly progressListeners = new Set<(p: { indexed: number; total: number }) => void>();
  private readonly syncListeners = new Set<(s: SyncStatus) => void>();
  private notifyScheduled = false;
  private syncScheduler: SyncScheduler | null = null;
  private syncUnsubscribe: (() => void) | null = null;
  private currentSyncStatus: SyncStatus = { state: 'disabled' };
  private reminderSettings: ReminderSettings;
  private reminderScheduler: ReminderScheduler;
  private remindersRunning = false;
  private reminderTimer: ReturnType<typeof setTimeout> | null = null;
  private readonly assetUrls = new Map<VaultPath, string>();
  private readonly assetLoading = new Set<VaultPath>();
  private readonly assetsReady = new Set<VaultPath>();
  private assetTimer: ReturnType<typeof setTimeout> | null = null;
  private convertFileSrc: ((abs: string) => string) | null = null;
  private readonly unsubscribers: (() => void)[] = [];
  private closed = false;

  private constructor(init: {
    platform: Platform;
    location: VaultLocation;
    vault: Vault;
    history: History;
    store: LocalStore;
    opts: WorkspaceOptions;
  }) {
    const { platform, location, vault, history, store, opts } = init;
    this.platform = platform;
    this.location = location;
    this.key = vaultKey(location);
    this.vault = vault;
    this.history = history;
    this.store = store;
    this.now = opts.now ?? (() => new Date());
    this.locale = opts.locale ?? 'pl';
    this.device = opts.device ?? 'Brulion';
    this.indexChunk = Math.max(1, opts.indexChunk ?? 40);
    this.reminderSettings = opts.reminders ?? { enabled: true, sound: true, horizonDays: 30 };
    this.index = new VaultIndex(vault.config);
    this.reminderScheduler = this.createReminderScheduler();
    this.unsubscribers.push(vault.onChange((changes) => this.onVaultChanges(changes)));
    this.ready = this.indexAll().catch((e: unknown) => {
      console.warn('[workspace] indexing failed', e);
    });
  }

  /** Open a vault (see {@link openWorkspace}). */
  static async open(platform: Platform, location: VaultLocation, opts: WorkspaceOptions = {}): Promise<Workspace> {
    const fs = opts.fs ?? (await platform.openFileSystem(location));
    const key = vaultKey(location);
    const store = opts.store ?? (await LocalStore.openOrMemory(`vault:${key}`));
    const history = new History(store);
    const vault = new Vault(fs, { id: key, name: location.name || fs.label, store, history });
    await vault.open();
    const ws = new Workspace({ platform, location, vault, history, store, opts });
    if (location.kind === 'tauri' && platform.kind !== 'web') await ws.loadTauriAssets();
    if (opts.watch !== false) vault.startWatching();
    void history.prune().catch((e: unknown) => console.warn('[workspace] history prune failed', e));
    void ws.ready.then(() => {
      if (ws.closed) return;
      ws.applyReminderSettings(ws.reminderSettings);
    });
    ws.configureSync(opts.nextcloud);
    return ws;
  }

  // -------------------------------------------------------------------------------------------
  // Basics
  // -------------------------------------------------------------------------------------------

  get name(): string {
    return this.vault.name;
  }

  get config(): VaultConfig {
    return this.vault.config;
  }

  /** Reminder scheduler (in-app timers / native schedule of the platform). */
  get reminders(): ReminderScheduler {
    return this.reminderScheduler;
  }

  /** The sync scheduler when Nextcloud sync is configured. */
  get sync(): SyncScheduler | null {
    return this.syncScheduler;
  }

  get syncStatus(): SyncStatus {
    return { ...this.currentSyncStatus };
  }

  /** Current date for journal operations. */
  today(): LocalDate {
    return todayLocal(this.now());
  }

  setLocale(locale: Locale): void {
    this.locale = locale;
  }

  read(path: VaultPath): Promise<string> {
    return this.vault.read(normalizePath(path));
  }

  readBinary(path: VaultPath): Promise<Uint8Array> {
    return this.vault.readBinary(normalizePath(path));
  }

  exists(path: VaultPath): boolean {
    return this.vault.exists(normalizePath(path));
  }

  /** Non-ignored files (attachments and conflict copies included). */
  files(): FileStat[] {
    return this.vault.files();
  }

  /** Called after the index or the file list changed (coalesced). Returns an unsubscribe function. */
  subscribe(cb: Listener): () => void {
    this.changeListeners.add(cb);
    return () => {
      this.changeListeners.delete(cb);
    };
  }

  /** Initial indexing progress. */
  onProgress(cb: (p: { indexed: number; total: number }) => void): () => void {
    this.progressListeners.add(cb);
    return () => {
      this.progressListeners.delete(cb);
    };
  }

  /** File changes for editors (= `vault.onChange`; own writes are reported too). */
  onFileChanged(cb: (changes: readonly FileChange[]) => void): () => void {
    return this.vault.onChange(cb);
  }

  /** Template files (Markdown files under the templates folder), sorted. */
  listTemplates(): VaultPath[] {
    const folder = normalizePath(this.config.templatesFolder);
    if (!folder) return [];
    const prefix = `${folder.toLowerCase()}/`;
    return this.vault
      .markdownFiles()
      .map((f) => f.path)
      .filter((p) => p.toLowerCase().startsWith(prefix))
      .sort((a, b) => a.localeCompare(b));
  }

  // -------------------------------------------------------------------------------------------
  // Writing
  // -------------------------------------------------------------------------------------------

  /** Write a text file (editor saves). Writes to one path are serialised. */
  write(path: VaultPath, content: string, source: ChangeSource = 'user'): Promise<void> {
    const p = normalizePath(path);
    return this.serial(p, async () => {
      await this.vault.write(p, content, source);
      this.indexNow(p, content);
    });
  }

  /**
   * Read-modify-write one file, serialised with other writes to it. `fn` returning the same
   * string writes nothing. Returns the new content.
   */
  modify(path: VaultPath, fn: (content: string) => string, source: ChangeSource = 'user'): Promise<string> {
    const p = normalizePath(path);
    return this.serial(p, async () => {
      const old = await this.vault.read(p);
      const next = fn(old);
      if (next !== old) {
        await this.vault.write(p, next, source);
        this.indexNow(p, next);
      }
      return next;
    });
  }

  /** Wait for all pending writes. */
  async flush(): Promise<void> {
    for (let i = 0; i < 10 && this.chains.size > 0; i++) {
      await Promise.all(this.chains.values());
    }
  }

  /** Save `.brulion/config.json` (synced with the vault) and re-index with it. */
  async saveConfig(cfg: VaultConfig): Promise<void> {
    await this.vault.saveConfig(cfg);
    this.index.setConfig(this.vault.config);
    this.scheduleNotify();
    this.scheduleReminderRefresh();
  }

  // -------------------------------------------------------------------------------------------
  // Pages
  // -------------------------------------------------------------------------------------------

  /** Path of the journal page of `date` (existing one, else the configured file name). */
  journalPath(date: LocalDate): VaultPath {
    const known = this.index.journals().find((j) => j.date === date);
    return known?.path ?? journalPathForDate(date, this.config);
  }

  /** The journal page of `date`, created from the journal template when missing. */
  async openJournal(date: LocalDate): Promise<VaultPath> {
    const path = this.journalPath(date);
    if (this.vault.exists(path)) return path;
    return this.serial(path, async () => {
      if (this.vault.exists(path)) return path;
      const content = await this.journalContent(date, path);
      await this.vault.write(path, content, 'user');
      this.indexNow(path, content);
      return path;
    });
  }

  /** Content of a new journal page: the journal template applied to `date`. */
  async journalContent(date: LocalDate, path: VaultPath = journalPathForDate(date, this.config)): Promise<string> {
    const tpl = normalizePath(this.config.journalTemplate);
    if (!tpl || !this.vault.exists(tpl)) return '';
    try {
      const template = await this.vault.read(tpl);
      const now = this.now();
      const [y, m, d] = date.split('-').map(Number);
      const at = new Date(y!, m! - 1, d!, now.getHours(), now.getMinutes());
      return applyTemplate(template, { now: at, title: pageTitle(path), locale: this.locale }).text;
    } catch (e) {
      console.warn('[workspace] journal template failed', e);
      return '';
    }
  }

  /** Create a new page (unique file name). `title` may contain `/` for sub-folders. */
  async createPage(title: string, folder?: string, content = ''): Promise<VaultPath> {
    const path = pathForTitle(title, folder ?? this.config.newPagesFolder);
    const created = await this.vault.create(path, content, 'user');
    this.indexNow(created, content);
    return created;
  }

  /** The page a link target points to; a missing page is created (in `newPagesFolder`). */
  async resolveOrCreate(target: string, from?: VaultPath): Promise<VaultPath> {
    const clean = target.replace(/#.*$/, '').trim();
    if (!clean) {
      if (from) return from;
      throw new Error('Empty link target');
    }
    const found = this.index.resolveLink(clean, from);
    if (found) return found;
    // A missing attachment ("![[foto.png]]") is not created as a page ("Wersja 1.2" is a page).
    if (!isMarkdown(clean) && mimeForPath(clean) !== 'application/octet-stream') return normalizePath(clean);
    return this.createPage(clean);
  }

  /**
   * Rename/move a page and rewrite the links to it in every linking file. `newTitleOrPath`
   * is a new title (same folder) or, when it contains `/`, a vault path.
   */
  async renamePage(path: VaultPath, newTitleOrPath: string): Promise<VaultPath> {
    const from = normalizePath(path);
    const input = newTitleOrPath.trim();
    if (!input) throw new Error('Empty name');
    const ext = extname(from) || '.md';
    let to: VaultPath;
    if (input.includes('/')) {
      // A path: every segment sanitised; the file keeps its extension.
      const segments = input
        .split('/')
        .map((x) => x.trim())
        .filter((x) => x !== '' && x !== '.' && x !== '..')
        .map((x) => sanitizeFileName(x));
      let last = segments.pop() ?? basename(from);
      if (extname(last).toLowerCase() !== ext.toLowerCase()) last += ext;
      to = normalizePath(joinPath(...segments, last));
    } else {
      const base = extname(input).toLowerCase() === ext.toLowerCase() ? input.slice(0, -ext.length) : input;
      to = normalizePath(joinPath(dirname(from), `${sanitizeFileName(base)}${ext}`));
    }
    if (to === from) return from;
    const linking = this.index.linksTo(from);
    await this.waitFor(from);
    await this.vault.rename(from, to, 'user');
    if (isIndexable(to)) {
      const content = await this.vault.read(to);
      this.bumpGen(from);
      this.bumpGen(to);
      if (isIndexable(from)) this.index.rename(from, to, content);
      else this.index.update(to, content);
    } else if (isAttachment(to)) {
      this.index.rename(from, to, '');
    } else {
      this.bumpGen(from);
      this.index.remove(from);
    }
    const oldName = pageTitle(from);
    const newName = pageTitle(to);
    for (const p of linking) {
      const target = p === from ? to : p;
      if (!isMarkdown(target) || !this.vault.exists(target)) continue;
      try {
        await this.modify(target, (c) => renameLinksInContent(c, oldName, newName, from, to));
      } catch (e) {
        console.warn('[workspace] could not update links in', target, e);
      }
    }
    this.scheduleNotify();
    return to;
  }

  /** Move a page (or attachment / folder) to the trash. */
  async deletePage(path: VaultPath): Promise<void> {
    const p = normalizePath(path);
    await this.waitFor(p);
    await this.vault.delete(p, 'user');
    if (isIndexable(p) || isAttachment(p)) {
      this.bumpGen(p);
      this.index.remove(p);
    }
    this.scheduleNotify();
  }

  /** Restore an item from the trash; returns where it went. */
  async restore(trashPath: VaultPath): Promise<VaultPath> {
    const to = await this.vault.restore(normalizePath(trashPath), 'user');
    if (isIndexable(to)) this.indexNow(to, await this.vault.read(to));
    else if (isAttachment(to)) this.index.updateAttachment(to);
    return to;
  }

  // -------------------------------------------------------------------------------------------
  // Blocks and tasks (pure markdown helpers applied to the file)
  // -------------------------------------------------------------------------------------------

  async toggleTask(path: VaultPath, line: number): Promise<void> {
    const today = this.today();
    await this.modify(path, (c) => toggleTaskIn(c, line, today));
  }

  async setTaskStatus(path: VaultPath, line: number, status: TaskStatus): Promise<void> {
    const today = this.today();
    await this.modify(path, (c) => setTaskStatusIn(c, line, status, today));
  }

  /** Id of the block at `line`, adding ` ^id` when missing. */
  async ensureBlockId(path: VaultPath, line: number): Promise<string> {
    let id = '';
    await this.modify(path, (c) => {
      const r = ensureBlockIdIn(c, line);
      id = r.id;
      return r.content;
    });
    return id;
  }

  /** Add `⏰ at` (or `⏰! at` for an alarm) to the block at `line`. */
  async addReminder(path: VaultPath, line: number, at: LocalDateTime, alarm: boolean): Promise<void> {
    await this.modify(path, (c) => addTokenToBlock(c, line, reminderToken(at, alarm)));
  }

  /**
   * Append a captured item to today's journal: "Kupić mleko jutro o 18" →
   * "- [ ] Kupić mleko ⏰ 2026-10-15 18:00". Returns where it was written.
   */
  async quickCapture(text: string, opts: CaptureOptions = {}): Promise<{ path: VaultPath; line: number }> {
    const block = buildCaptureBlock(text, this.now(), { locale: this.locale, weekStartsOn: this.config.weekStartsOn, ...opts });
    const path = await this.openJournal(this.today());
    let line = 0;
    await this.modify(path, (c) => {
      const r = appendBlock(c, block);
      line = r.line;
      return r.content;
    });
    return { path, line };
  }

  // -------------------------------------------------------------------------------------------
  // Attachments
  // -------------------------------------------------------------------------------------------

  /** Store bytes in the attachments folder under a unique name. */
  async saveAttachment(name: string, data: Uint8Array): Promise<VaultPath> {
    const folder = normalizePath(this.config.attachmentsFolder);
    const file = sanitizeFileName(basename(name.replace(/\\/g, '/')) || 'plik');
    const wanted = uniquePath(normalizePath(joinPath(folder, file)), (p) => this.vault.exists(p));
    const path = await this.vault.create(wanted, data, 'user');
    this.index.updateAttachment(path);
    // The bytes are at hand: the editor can show a pasted image at once.
    if (!this.convertFileSrc && !this.assetUrls.has(path) && typeof URL.createObjectURL === 'function') {
      this.assetUrls.set(path, URL.createObjectURL(new Blob([data as BlobPart], { type: mimeForPath(path) })));
    }
    this.scheduleNotify();
    return path;
  }

  /**
   * URL to display an attachment. Tauri folder vaults: an asset-protocol URL. Otherwise a
   * blob URL; the first call starts loading it and returns null — `subscribe` listeners and
   * index subscribers (the editor's live preview) are notified when it is ready. Attachments
   * embedded in notes are preloaded, pasted ones are available at once.
   */
  assetUrl(path: VaultPath): string | null {
    const p = normalizePath(path);
    if (!p || !this.vault.exists(p)) return null;
    if (this.convertFileSrc && this.location.kind === 'tauri') {
      return this.convertFileSrc(`${this.location.path.replace(/[\\/]+$/, '')}/${p}`);
    }
    const cached = this.assetUrls.get(p);
    if (cached) return cached;
    if (!this.assetLoading.has(p) && typeof URL !== 'undefined' && typeof URL.createObjectURL === 'function') {
      this.assetLoading.add(p);
      void this.loadAsset(p);
    }
    return null;
  }

  private async loadAsset(p: VaultPath): Promise<void> {
    try {
      const data = await this.vault.readBinary(p);
      if (this.closed) return;
      const url = URL.createObjectURL(new Blob([data as BlobPart], { type: mimeForPath(p) }));
      this.assetUrls.set(p, url);
      this.assetsReady.add(p);
      this.scheduleAssetNotify();
      this.scheduleNotify();
    } catch (e) {
      console.warn('[workspace] could not load attachment', p, e);
    } finally {
      this.assetLoading.delete(p);
    }
  }

  /** Attachment a link target in `from` points to (exact path, relative, attachments folder, unique name). */
  resolveAttachment(target: string, from?: VaultPath): VaultPath | null {
    const clean = normalizePath(target.replace(/[#?].*$/, ''));
    if (!clean) return null;
    const viaIndex = this.index.resolveLink(clean, from);
    if (viaIndex && !isMarkdown(viaIndex) && this.vault.exists(viaIndex)) return viaIndex;
    const candidates = [clean];
    if (from) candidates.push(normalizePath(joinPath(dirname(from), clean)));
    candidates.push(normalizePath(joinPath(this.config.attachmentsFolder, clean)));
    for (const c of candidates) if (this.vault.stat(c)?.kind === 'file') return c;
    const name = basename(clean).toLowerCase();
    const matches = this.vault.files().filter((f) => basename(f.path).toLowerCase() === name);
    if (matches.length === 0) return null;
    matches.sort((a, b) => a.path.length - b.path.length || a.path.localeCompare(b.path));
    return matches[0]!.path;
  }

  /** Start loading blob URLs for the attachments a note embeds (web vaults; the editor asks synchronously). */
  private preloadEmbeds(path: VaultPath): void {
    if (this.convertFileSrc || typeof URL === 'undefined' || typeof URL.createObjectURL !== 'function') return;
    const note = this.index.note(path);
    if (!note) return;
    for (const link of note.links) {
      if (link.kind !== 'embed' || isMarkdown(link.target)) continue;
      if (!/\.(png|jpe?g|gif|webp|avif|bmp|ico|svg|mp3|m4a|ogg|oga|wav|flac|mp4|webm|mov|pdf)$/i.test(link.target)) continue;
      if (this.assetUrls.size + this.assetLoading.size >= MAX_PRELOADED_ASSETS) return;
      const resolved = this.resolveAttachment(link.target, path);
      if (resolved) this.assetUrl(resolved);
    }
  }

  /**
   * Tell index subscribers (the editor preview re-renders on index changes) that attachment
   * URLs became available, by re-registering one loaded attachment (the index itself does not
   * change). Batched: one notification for a burst of loads.
   */
  private scheduleAssetNotify(): void {
    if (this.assetTimer) return;
    this.assetTimer = setTimeout(() => {
      this.assetTimer = null;
      const p = [...this.assetsReady].find((x) => this.vault.exists(x));
      this.assetsReady.clear();
      if (!p || this.closed || !this.index.attachments().includes(p)) return;
      this.index.removeAttachment(p);
      this.index.updateAttachment(p);
    }, 60);
  }

  private dropAsset(p: VaultPath): void {
    const url = this.assetUrls.get(p);
    if (!url) return;
    this.assetUrls.delete(p);
    URL.revokeObjectURL(url);
  }

  private async loadTauriAssets(): Promise<void> {
    try {
      const core = await import('@tauri-apps/api/core');
      this.convertFileSrc = (abs) => core.convertFileSrc(abs);
    } catch (e) {
      console.warn('[workspace] asset protocol unavailable; using blob URLs', e);
    }
  }

  // -------------------------------------------------------------------------------------------
  // Import / export
  // -------------------------------------------------------------------------------------------

  /**
   * Run an importer and write its files into the vault. Collisions: an existing journal page
   * gets the imported content appended under "## Import (<source>)"; any other existing file
   * keeps its content and the imported one gets a unique name.
   */
  async importFiles(
    importer: Importer,
    files: PickedFile[],
    targetFolder: string,
    opts: { onProgress?: (p: ImportProgress) => void } = {},
  ): Promise<ImportReport> {
    const result = await importer.run(files, { targetFolder: normalizePath(targetFolder), vaultConfig: this.config });
    const report: ImportReport = { created: [], appended: [], renamed: [], warnings: [...result.warnings], errors: [] };
    const total = result.files.length;
    let done = 0;
    for (const f of result.files) {
      const path = normalizePath(f.path);
      try {
        if (!path || isIgnoredPath(path)) throw new Error('Invalid path');
        if (this.vault.exists(path)) {
          if (typeof f.content === 'string' && isMarkdown(path) && journalDateFromPath(path, this.config)) {
            const text = f.content;
            await this.modify(path, (c) => appendImportSection(c, text, importer.name), 'import');
            report.appended.push(path);
          } else {
            const to = await this.vault.create(uniquePath(path, (p) => this.vault.exists(p)), f.content, 'import');
            this.indexNow(to, typeof f.content === 'string' ? f.content : '');
            report.renamed.push({ from: path, to });
          }
        } else {
          const to = await this.vault.create(path, f.content, 'import');
          this.indexNow(to, typeof f.content === 'string' ? f.content : '');
          if (to === path) report.created.push(to);
          else report.renamed.push({ from: path, to });
        }
      } catch (e) {
        report.errors.push({ path: path || f.path, message: errorMessage(e) });
      }
      done++;
      opts.onProgress?.({ done, total });
      if (done % 25 === 0) await yieldToEventLoop();
    }
    this.scheduleNotify();
    return report;
  }

  /** Run an exporter (by id) over the whole vault or `scope.paths`. */
  async exportFiles(exporterId: string, scope: { paths?: VaultPath[] } = {}): Promise<ExportedFile> {
    // Loaded on demand: exporters pull in zip/HTML code the app does not need at startup.
    const { exporters } = await import('@/core/export');
    const exporter = exporters.find((e) => e.id === exporterId);
    if (!exporter) throw new Error(`Unknown exporter: ${exporterId}`);
    await this.flush();
    const paths = scope.paths?.map(normalizePath) ?? this.vault.files().filter((f) => !isConflictCopy(f.path)).map((f) => f.path);
    const files: { path: VaultPath; content: string | Uint8Array }[] = [];
    for (const p of paths) {
      if (!this.vault.exists(p)) continue;
      files.push({ path: p, content: isTextPath(p) ? await this.vault.read(p) : await this.vault.readBinary(p) });
    }
    const notes = new Map<VaultPath, NonNullable<ReturnType<VaultIndex['note']>>>();
    for (const f of files) {
      const n = this.index.note(f.path);
      if (n) notes.set(f.path, n);
    }
    return exporter.run({ files, vaultConfig: this.config, notes, vaultName: this.name });
  }

  // -------------------------------------------------------------------------------------------
  // Sync and conflicts
  // -------------------------------------------------------------------------------------------

  /** (Re)configure Nextcloud sync; disabled or incomplete settings stop it. */
  configureSync(settings: NextcloudSettings | undefined): void {
    this.syncUnsubscribe?.();
    this.syncUnsubscribe = null;
    this.syncScheduler?.stop();
    this.syncScheduler = null;
    if (this.closed || !settings?.enabled || !settings.server.trim() || !settings.username.trim()) {
      this.setSyncStatus({ state: 'disabled' });
      return;
    }
    const client = new WebDavClient({
      baseUrl: nextcloudDavUrl(settings.server, settings.username, settings.folder || 'Brulion'),
      username: settings.username,
      password: settings.password,
      transport: this.platform.http,
    });
    const engine = new SyncEngine({ vault: this.vault, client, store: this.store, device: this.device });
    const scheduler = new SyncScheduler({ engine, intervalMs: Math.max(1, settings.intervalMin) * 60_000 });
    this.syncScheduler = scheduler;
    this.syncUnsubscribe = scheduler.subscribe((s) => this.setSyncStatus(s));
    scheduler.start();
  }

  /** Sync now (null when sync is not configured, offline or failed — see `syncStatus`). */
  async syncNow(): Promise<SyncReport | null> {
    if (!this.syncScheduler) return null;
    await this.flush();
    return this.syncScheduler.syncNow();
  }

  onSyncStatus(cb: (s: SyncStatus) => void): () => void {
    this.syncListeners.add(cb);
    cb(this.syncStatus);
    return () => {
      this.syncListeners.delete(cb);
    };
  }

  private setSyncStatus(s: SyncStatus): void {
    this.currentSyncStatus = s;
    for (const cb of Array.from(this.syncListeners)) {
      try {
        cb({ ...s });
      } catch (e) {
        console.warn('[workspace] sync status listener failed', e);
      }
    }
  }

  /** Conflict copies in the vault (Syncthing and Brulion). */
  conflicts(): ConflictInfo[] {
    return findSyncthingConflicts(this.vault.files());
  }

  /** Save the merged text to the original and move the conflict copy to the trash. */
  async resolveConflict(original: VaultPath, copy: VaultPath, mergedContent: string): Promise<void> {
    await this.write(original, mergedContent);
    await this.waitFor(copy);
    if (this.vault.exists(copy)) await this.vault.delete(normalizePath(copy), 'user');
    this.scheduleNotify();
  }

  // -------------------------------------------------------------------------------------------
  // Reminders
  // -------------------------------------------------------------------------------------------

  /** Apply reminder settings: start/stop the scheduler (disabled clears scheduled ones). */
  applyReminderSettings(s: ReminderSettings): void {
    const horizonChanged = s.horizonDays !== this.reminderSettings.horizonDays;
    this.reminderSettings = s;
    if (this.closed) return;
    if (!s.enabled) {
      if (this.remindersRunning) this.reminders.stop();
      this.remindersRunning = false;
      void this.platform.notifications.sync([]).catch((e: unknown) => console.warn('[workspace] clearing reminders failed', e));
      return;
    }
    if (horizonChanged) {
      // The scheduler takes its horizon at construction: replace it.
      if (this.remindersRunning) this.reminderScheduler.stop();
      this.remindersRunning = false;
      this.reminderScheduler = this.createReminderScheduler();
    }
    try {
      if (!this.remindersRunning) {
        this.reminders.start();
        this.remindersRunning = true;
      } else {
        void this.reminders.refresh().catch((e: unknown) => console.warn('[workspace] reminder refresh failed', e));
      }
    } catch (e) {
      console.warn('[workspace] reminders could not start', e);
    }
  }

  private createReminderScheduler(): ReminderScheduler {
    return new ReminderScheduler(this.index, this.platform.notifications, this.store, {
      horizonDays: this.reminderSettings.horizonDays,
    });
  }

  private scheduleReminderRefresh(): void {
    if (!this.remindersRunning || this.closed) return;
    if (this.reminderTimer) clearTimeout(this.reminderTimer);
    this.reminderTimer = setTimeout(() => {
      this.reminderTimer = null;
      if (this.remindersRunning && !this.closed) {
        this.reminders.refresh().catch((e: unknown) => console.warn('[workspace] reminder refresh failed', e));
      }
    }, REMINDER_REFRESH_MS);
  }

  // -------------------------------------------------------------------------------------------
  // Closing
  // -------------------------------------------------------------------------------------------

  /** Flush pending writes, stop schedulers and watchers, release blob URLs. */
  async close(): Promise<void> {
    if (this.closed) return;
    await this.flush();
    this.closed = true;
    if (this.reminderTimer) clearTimeout(this.reminderTimer);
    if (this.assetTimer) clearTimeout(this.assetTimer);
    if (this.remindersRunning) this.reminders.stop();
    this.remindersRunning = false;
    this.syncUnsubscribe?.();
    this.syncScheduler?.stop();
    this.syncScheduler = null;
    for (const u of this.unsubscribers.splice(0)) u();
    await this.vault.close();
    // Copy: dropAsset deletes from the map.
    for (const p of Array.from(this.assetUrls.keys())) this.dropAsset(p);
    this.changeListeners.clear();
    this.progressListeners.clear();
    this.syncListeners.clear();
  }

  get isClosed(): boolean {
    return this.closed;
  }

  // -------------------------------------------------------------------------------------------
  // Internals
  // -------------------------------------------------------------------------------------------

  private serial<T>(path: VaultPath, fn: () => Promise<T>): Promise<T> {
    const prev = this.chains.get(path) ?? Promise.resolve();
    const run = prev.then(fn, fn);
    const tail = run.then(
      () => undefined,
      () => undefined,
    );
    this.chains.set(path, tail);
    void tail.then(() => {
      if (this.chains.get(path) === tail) this.chains.delete(path);
    });
    return run;
  }

  /** Wait for pending writes to `path`. */
  private async waitFor(path: VaultPath): Promise<void> {
    await this.chains.get(normalizePath(path));
  }

  private gen(path: VaultPath): number {
    return this.gens.get(path) ?? 0;
  }

  private bumpGen(path: VaultPath): void {
    this.gens.set(path, this.gen(path) + 1);
  }

  /** Put known content into the index right away (the change event then finds it current). */
  private indexNow(path: VaultPath, content: string): void {
    if (!isIndexable(path)) {
      if (isAttachment(path)) this.index.updateAttachment(path);
      this.scheduleNotify();
      return;
    }
    this.bumpGen(path);
    if (this.index.content(path) !== content) this.index.update(path, content);
    this.preloadEmbeds(path);
    this.scheduleNotify();
    this.scheduleReminderRefresh();
  }

  private async indexAll(): Promise<void> {
    for (const f of this.vault.files()) if (isAttachment(f.path)) this.index.updateAttachment(f.path);
    const files = this.vault.markdownFiles().filter((f) => isIndexable(f.path));
    this.progress = { indexed: 0, total: files.length };
    this.emitProgress();
    for (let i = 0; i < files.length; i += this.indexChunk) {
      if (this.closed) return;
      const slice = files.slice(i, i + this.indexChunk);
      const read = await Promise.all(
        slice.map(async (f) => {
          const gen = this.gen(f.path);
          try {
            return { path: f.path, gen, content: await this.vault.read(f.path) };
          } catch (e) {
            console.warn('[workspace] could not read', f.path, e);
            return null;
          }
        }),
      );
      for (const r of read) {
        if (!r || this.gen(r.path) !== r.gen || !this.vault.exists(r.path)) continue;
        this.index.update(r.path, r.content);
      }
      this.progress = { indexed: Math.min(files.length, i + slice.length), total: files.length };
      this.emitProgress();
      this.scheduleNotify();
      await yieldToEventLoop();
    }
    for (const f of files) this.preloadEmbeds(f.path);
    this.scheduleNotify();
  }

  private emitProgress(): void {
    for (const cb of Array.from(this.progressListeners)) {
      try {
        cb({ ...this.progress });
      } catch (e) {
        console.warn('[workspace] progress listener failed', e);
      }
    }
  }

  private scheduleNotify(): void {
    if (this.notifyScheduled) return;
    this.notifyScheduled = true;
    queueMicrotask(() => {
      this.notifyScheduled = false;
      for (const cb of Array.from(this.changeListeners)) {
        try {
          cb();
        } catch (e) {
          console.warn('[workspace] change listener failed', e);
        }
      }
    });
  }

  private onVaultChanges(changes: VaultChange[]): void {
    for (const c of changes) {
      this.bumpGen(c.path);
      if (c.oldPath) this.bumpGen(c.oldPath);
      // Our own new files (saveAttachment) keep the URL made from their bytes.
      if (!(c.type === 'create' && c.source === 'user')) this.dropAsset(c.path);
      if (c.oldPath) this.dropAsset(c.oldPath);
    }
    this.syncScheduler?.notifyChanges(changes);
    this.changeChain = this.changeChain.then(() => this.applyChanges(changes)).catch((e: unknown) => {
      console.warn('[workspace] applying file changes to the index failed', e);
    });
  }

  private async applyChanges(changes: VaultChange[]): Promise<void> {
    let touched = false;
    for (const c of changes) {
      if (this.closed) return;
      if (c.path === CONFIG_PATH) {
        this.index.setConfig(this.vault.config);
        touched = true;
        continue;
      }
      const md = isIndexable(c.path);
      const attachment = isAttachment(c.path);
      if (c.type === 'delete') {
        if (md || attachment) this.index.remove(c.path);
        touched ||= md;
        continue;
      }
      if (c.type === 'rename' && c.oldPath !== undefined) {
        if (md) {
          const content = await this.safeRead(c.path);
          if (content === null) continue;
          // Handles a missing old entry and an unchanged index (renamePage already moved it).
          if (this.index.content(c.path) !== content || this.index.content(c.oldPath) !== undefined) this.index.rename(c.oldPath, c.path, content);
          touched = true;
        } else if (attachment) {
          this.index.rename(c.oldPath, c.path, '');
        } else {
          this.index.remove(c.oldPath);
        }
        continue;
      }
      if (attachment) {
        this.index.updateAttachment(c.path);
        continue;
      }
      if (!md) continue;
      const content = await this.safeRead(c.path);
      if (content === null) continue;
      if (this.index.content(c.path) !== content) this.index.update(c.path, content);
      this.preloadEmbeds(c.path);
      touched = true;
    }
    if (touched) this.scheduleReminderRefresh();
    this.scheduleNotify();
  }

  private async safeRead(path: VaultPath): Promise<string | null> {
    if (!this.vault.exists(path)) return null;
    try {
      return await this.vault.read(path);
    } catch (e) {
      console.warn('[workspace] could not read changed file', path, e);
      return null;
    }
  }
}

/** Open a vault on a platform (docs/UI.md `openWorkspace`). */
export function openWorkspace(platform: Platform, location: VaultLocation, opts: WorkspaceOptions = {}): Promise<Workspace> {
  return Workspace.open(platform, location, opts);
}
