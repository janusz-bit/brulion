/**
 * Shared contracts between Brulion modules.
 *
 * Everything in src/core is platform-independent TypeScript (no React, no Tauri imports).
 * Platform specifics live behind the interfaces below and are implemented in src/platform.
 * See docs/SYNTAX.md for the Markdown dialect and docs/ARCHITECTURE.md for module ownership.
 */

/** POSIX-style path relative to the vault root, no leading slash: "journals/2026-10-14.md". */
export type VaultPath = string;

/** Local date "YYYY-MM-DD" (no time zone). */
export type LocalDate = string;

/** Local date-time "YYYY-MM-DDTHH:mm" (no time zone, wall-clock time). */
export type LocalDateTime = string;

// ---------------------------------------------------------------------------
// File system
// ---------------------------------------------------------------------------

export interface FileStat {
  path: VaultPath;
  kind: 'file' | 'dir';
  /** Bytes; 0 for directories. */
  size: number;
  /** Modification time, ms since epoch. */
  mtime: number;
}

export type FsEventType = 'create' | 'modify' | 'delete' | 'rename';

export interface FsEvent {
  type: FsEventType;
  path: VaultPath;
  /** Only for 'rename'. */
  oldPath?: VaultPath;
}

/**
 * A vault's storage backend. Paths are always VaultPath. Implementations:
 * memory (tests), IndexedDB (browser), File System Access API (Chromium), Tauri (native).
 * Writes must be atomic where the backend allows it (write temp + rename) so that
 * Syncthing / Nextcloud never pick up half-written files.
 */
export interface FileSystemAdapter {
  readonly kind: 'memory' | 'idb' | 'fsaccess' | 'tauri';
  /** Human readable location, e.g. "/home/jan/Notatki" or "Przeglądarka (IndexedDB)". */
  readonly label: string;
  /** Recursive listing of all files and directories (ignored paths are filtered by the caller). */
  list(): Promise<FileStat[]>;
  stat(path: VaultPath): Promise<FileStat | null>;
  readText(path: VaultPath): Promise<string>;
  readBinary(path: VaultPath): Promise<Uint8Array>;
  /** Creates parent directories as needed. Returns the new stat. */
  writeText(path: VaultPath, content: string): Promise<FileStat>;
  writeBinary(path: VaultPath, data: Uint8Array): Promise<FileStat>;
  /** Removes a file, or a directory recursively. No error if missing. */
  remove(path: VaultPath): Promise<void>;
  /** Renames/moves a file or directory; creates parent dirs of `to`. */
  rename(from: VaultPath, to: VaultPath): Promise<void>;
  mkdir(path: VaultPath): Promise<void>;
  /**
   * Optional: notify about changes made by other programs (Syncthing, Nextcloud client,
   * another editor). Returns an unsubscribe function. Backends without native watching
   * may poll. Events caused by this adapter's own writes may be reported too; the vault
   * deduplicates them by comparing content hashes.
   */
  watch?(onEvents: (events: FsEvent[]) => void): () => void;
}

// ---------------------------------------------------------------------------
// Parsed Markdown model (produced by src/core/markdown)
// ---------------------------------------------------------------------------

export type BlockKind =
  | 'listItem'
  | 'paragraph'
  | 'heading'
  | 'code'
  | 'table'
  | 'quote'
  | 'callout'
  | 'hr'
  | 'html'
  | 'frontmatter';

export interface Block {
  /** Explicit block id (`^abc123` or Logseq `id::`), if any. */
  id?: string;
  kind: BlockKind;
  /** 0-based first line (inclusive). */
  line: number;
  /** 0-based last line (inclusive), including nested children for list items. */
  endLine: number;
  /** 0-based last line of this block's own content (excluding children). */
  ownEndLine: number;
  /** Outline depth: 0 for top-level. */
  depth: number;
  /** Raw source of the block's own lines (without children). */
  raw: string;
  /**
   * Plain-ish text content: list marker, checkbox, task keyword, block id, property lines
   * and metadata tokens removed; inline markdown kept.
   */
  text: string;
  /** Heading level 1-6 for kind 'heading'. */
  headingLevel?: number;
  /** Language for kind 'code' (e.g. "calc", "query", "ts"). */
  lang?: string;
  properties: Record<string, PropertyValue>;
  task?: Task;
  /** Reminders found in this block. */
  reminders: Reminder[];
  /** Tags found in this block's own content (lower-cased, without '#'). */
  tags: string[];
  children: Block[];
}

export type PropertyValue = string | string[];

export type TaskStatus = 'todo' | 'doing' | 'done' | 'cancelled' | 'deferred';
export type Priority = 'highest' | 'high' | 'medium' | 'low' | 'lowest';

export interface Task {
  status: TaskStatus;
  /** Raw status char/keyword as written: ' ', 'x', '/', '-', '>', 'TODO', 'DONE', ... */
  marker: string;
  /** Text without checkbox and metadata tokens. */
  text: string;
  line: number;
  due?: LocalDate | LocalDateTime;
  scheduled?: LocalDate | LocalDateTime;
  start?: LocalDate | LocalDateTime;
  created?: LocalDate;
  doneDate?: LocalDate;
  cancelledDate?: LocalDate;
  /** Canonical English recurrence rule, e.g. "every week on Monday". */
  recurrence?: string;
  priority?: Priority;
}

export interface Reminder {
  /** When it fires (local wall-clock time). */
  at: LocalDateTime;
  /** Alarm-style (`⏰!`): loud, persistent. */
  alarm: boolean;
  /** Canonical recurrence rule if the block has `🔁`. */
  recurrence?: string;
  /** Block text with tokens stripped — used as notification body. */
  text: string;
  line: number;
}

export type LinkKind = 'wiki' | 'embed' | 'blockref' | 'blockembed' | 'markdown' | 'url';

export interface Link {
  kind: LinkKind;
  /** Raw target: page name / path / url / block id (for blockref). */
  target: string;
  /** Text after `|` or markdown link text. */
  display?: string;
  /** Heading after `#` (without `^`). */
  heading?: string;
  /** Block id after `#^`, or the id of a `((id))` ref. */
  blockId?: string;
  line: number;
  /** Character offsets within the whole document. */
  from: number;
  to: number;
}

export interface Heading {
  level: number;
  text: string;
  line: number;
}

export interface ParsedNote {
  path: VaultPath;
  /** File base name without `.md`. */
  title: string;
  frontmatter: Record<string, unknown>;
  /** Page-level properties (frontmatter scalars/arrays + leading `key::` lines). */
  properties: Record<string, PropertyValue>;
  aliases: string[];
  /** Page-level tags: frontmatter tags + all block tags (deduplicated, lower-cased). */
  tags: string[];
  /** Outline roots in document order. */
  blocks: Block[];
  /** All blocks flattened in document order (pre-order). */
  allBlocks: Block[];
  links: Link[];
  headings: Heading[];
  tasks: Task[];
  reminders: Reminder[];
  /** Block id -> block, for blocks with explicit ids. */
  blockIds: Record<string, Block>;
  /** If the file is a journal page, its date. */
  journalDate?: LocalDate;
}

// ---------------------------------------------------------------------------
// Vault configuration (`.brulion/config.json`, synced) and device settings (local)
// ---------------------------------------------------------------------------

export interface VaultConfig {
  version: 1;
  journalsFolder: string;
  journalFileFormat: string;
  newPagesFolder: string;
  attachmentsFolder: string;
  templatesFolder: string;
  journalTemplate: string;
  defaultReminderTime: string;
  weekStartsOn: 0 | 1;
}

export const DEFAULT_VAULT_CONFIG: VaultConfig = {
  version: 1,
  journalsFolder: 'journals',
  journalFileFormat: 'yyyy-MM-dd',
  newPagesFolder: '',
  attachmentsFolder: 'assets',
  templatesFolder: 'templates',
  journalTemplate: 'templates/journal.md',
  defaultReminderTime: '09:00',
  weekStartsOn: 1,
};

export type Locale = 'pl' | 'en';

// ---------------------------------------------------------------------------
// Index (src/core/index)
// ---------------------------------------------------------------------------

export interface BlockRef {
  path: VaultPath;
  block: Block;
}

export interface TaskRef {
  path: VaultPath;
  task: Task;
  block: Block;
}

export interface ReminderRef {
  path: VaultPath;
  reminder: Reminder;
  block: Block;
  /** Present when the block is a task. */
  task?: Task;
}

export interface Backlink {
  /** File containing the link. */
  path: VaultPath;
  link: Link;
  /** The block containing the link (for context display). */
  block?: Block;
}

export interface SearchHit {
  path: VaultPath;
  title: string;
  score: number;
  /** Best matching line (0-based) and a short excerpt. */
  line?: number;
  excerpt?: string;
}

export interface GraphData {
  nodes: { id: VaultPath; title: string; size: number; tag?: boolean; missing?: boolean }[];
  edges: { source: VaultPath; target: VaultPath }[];
}

// ---------------------------------------------------------------------------
// Notifications / reminders scheduling (implemented per platform)
// ---------------------------------------------------------------------------

export interface ScheduledNotification {
  /** Stable id derived from path + line + time (so rescheduling is idempotent). */
  id: string;
  /** Integer id for native APIs that require one (Android). */
  nativeId: number;
  title: string;
  body: string;
  at: LocalDateTime;
  alarm: boolean;
  /** Deep link payload: which note/line to open on tap. */
  path: VaultPath;
  line: number;
}

export interface NotificationService {
  /** Whether the platform can fire notifications while the app is closed. */
  readonly firesWhenClosed: boolean;
  permission(): Promise<'granted' | 'denied' | 'prompt'>;
  requestPermission(): Promise<'granted' | 'denied' | 'prompt'>;
  /** Replace the full set of pending notifications with `items` (idempotent). */
  sync(items: ScheduledNotification[]): Promise<void>;
  /** Show immediately (used by the in-app scheduler and for testing). */
  showNow(item: ScheduledNotification): Promise<void>;
  /** Called when the user taps a notification. */
  onActivate(cb: (item: Pick<ScheduledNotification, 'path' | 'line'>) => void): () => void;
}

// ---------------------------------------------------------------------------
// Platform (src/platform)
// ---------------------------------------------------------------------------

export type PlatformKind = 'tauri-desktop' | 'tauri-android' | 'web';

/** Where a vault lives. Serialised into device settings. */
export type VaultLocation =
  | { kind: 'tauri'; path: string; name: string }
  | { kind: 'fsaccess'; handleId: string; name: string }
  | { kind: 'idb'; id: string; name: string }
  | { kind: 'memory'; id: string; name: string };

export interface HttpRequest {
  method: string;
  url: string;
  headers?: Record<string, string>;
  body?: string | Uint8Array;
}

export interface HttpResponse {
  status: number;
  headers: Record<string, string>;
  /** Raw body bytes. */
  body: Uint8Array;
}

/** Minimal HTTP transport so WebDAV works through Tauri (no CORS) and fetch (browser). */
export type HttpTransport = (req: HttpRequest) => Promise<HttpResponse>;

export interface Platform {
  readonly kind: PlatformKind;
  /** Human-friendly platform name. */
  readonly name: string;
  /** Which vault location kinds this platform supports, in order of preference. */
  readonly vaultKinds: VaultLocation['kind'][];
  /** Let the user pick a folder (desktop dialog / FS Access API). null = cancelled. */
  pickVaultFolder(): Promise<VaultLocation | null>;
  /** Suggested default vault location for first run. */
  defaultVaultLocation(): Promise<VaultLocation>;
  openFileSystem(location: VaultLocation): Promise<FileSystemAdapter>;
  readonly notifications: NotificationService;
  readonly http: HttpTransport;
  openExternal(url: string): Promise<void>;
  /** Android: "All files access" needed to read a Syncthing folder on shared storage. */
  storageAccess?: {
    granted(): Promise<boolean>;
    request(): Promise<void>;
  };
  /** Save a file the user exports (download in browser, save dialog on desktop). */
  saveFile(name: string, data: Uint8Array, mime: string): Promise<void>;
  /** Let the user pick files to import. */
  pickFiles(opts: { accept?: string[]; multiple?: boolean; directory?: boolean }): Promise<PickedFile[]>;
}

export interface PickedFile {
  /** Relative path inside the picked directory, or just the file name. */
  path: string;
  data: Uint8Array;
}

// ---------------------------------------------------------------------------
// Import / export (src/core/import, src/core/export)
// ---------------------------------------------------------------------------

/** A file produced by an importer, to be written into the vault. */
export interface ImportedFile {
  path: VaultPath;
  content: string | Uint8Array;
}

export interface ImportResult {
  files: ImportedFile[];
  warnings: string[];
}

export interface Importer {
  id: string;
  /** Display name, e.g. "Obsidian". */
  name: string;
  /** Short description of what the user should pick (localised by UI via id). */
  accepts: { extensions: string[]; directory: boolean };
  /** Cheap heuristic: does this input look like it comes from this app? */
  detect(input: PickedFile[]): boolean;
  run(input: PickedFile[], opts: ImportOptions): Promise<ImportResult>;
}

export interface ImportOptions {
  /** Vault folder to import into, e.g. "Import/Notion". */
  targetFolder: string;
  vaultConfig: VaultConfig;
}

export interface ExportedFile {
  name: string;
  mime: string;
  data: Uint8Array;
}

export interface Exporter {
  id: string;
  name: string;
  run(input: ExportInput): Promise<ExportedFile>;
}

export interface ExportInput {
  /** Files to export (already filtered by the UI: whole vault, folder or single page). */
  files: { path: VaultPath; content: string | Uint8Array }[];
  vaultConfig: VaultConfig;
  /** Parsed notes for the markdown files, keyed by path. */
  notes: Map<VaultPath, ParsedNote>;
  vaultName: string;
}
