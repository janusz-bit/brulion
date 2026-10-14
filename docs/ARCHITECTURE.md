# Brulion — architecture

Brulion is an offline-first Markdown notes app (Obsidian-like files, Logseq-like outliner and
journals) that runs as:

* **Desktop (NixOS/Linux)** — Tauri 2 shell (Rust) + the web frontend; vault = a folder on disk
  (typically a Syncthing folder); reminders via system notifications while the app lives in the tray.
* **Android** — the same Tauri 2 shell built for Android; vault = a folder on shared storage
  (Syncthing-Fork) or app storage + Nextcloud sync; reminders scheduled natively (fire when closed).
* **Browser (PWA)** — the same frontend served statically (GitHub Pages / self-hosted), works
  offline via a service worker; vault = IndexedDB, or a local folder through the File System
  Access API (Chromium); sync with Nextcloud over WebDAV.

```
┌──────────────────────────── src/app (React UI) ─────────────────────────────┐
│ shell/ (layout, store, commands)  editor/ (CodeMirror 6)  views/ (journals,  │
│ calendar, tasks, graph, search, sync, import/export, history, settings)      │
├──────────────────────────── src/app/services ───────────────────────────────┤
│ Workspace: Vault + VaultIndex + History + ReminderScheduler + SyncScheduler  │
├──────────────────────────── src/core (pure TS) ─────────────────────────────┤
│ markdown  dates  calc  render  index(+query, search)  storage(vault, idb)    │
│ reminders(+calendar)  sync(webdav, engine, merge, syncthing)  import  export │
│ i18n  paths  types                                                           │
├──────────────────────────── src/platform ───────────────────────────────────┤
│ web.ts (IndexedDB / FS Access / fetch / Notification API)                    │
│ tauri.ts (Rust fs commands / plugin-notification / plugin-http / dialog)     │
├──────────────────────────── src-tauri (Rust) ───────────────────────────────┤
│ fs commands (atomic writes, recursive list, notify watcher), tray,           │
│ android plugin (all-files access), official plugins                          │
└──────────────────────────────────────────────────────────────────────────────┘
```

## Principles

1. **Files are the truth.** Every note is a `.md` file in a folder. No database owns the notes.
   Indexes are rebuilt from files; device-local bookkeeping lives in IndexedDB (`LocalStore`).
2. **Offline first.** Every feature works without network. Sync is opportunistic, runs in the
   background, never blocks editing, never loses data (conflict → automatic 3-way merge,
   otherwise keep both versions and offer a merge UI).
3. **Compatible.** Obsidian (+Tasks plugin) and Logseq can open the same vault (docs/SYNTAX.md).
4. **Pure core.** `src/core` has no React/Tauri/DOM-global imports (DOMParser/DOMPurify use is
   allowed only in `render` and importers, injected or guarded). Platform code is behind
   interfaces in `src/core/types.ts`.
5. **Polish first.** UI language defaults to Polish, English available. Natural-language date
   input understands Polish and English.

## Module ownership and public APIs

Each module exposes its public API from `src/core/<module>/index.ts`. Other modules import
only from that index. Signatures below are the contract; modules may add more exports.

### `src/core/paths.ts` (done)
`normalizePath, joinPath, dirname, basename, extname, isMarkdown, stripMdExtension, pageTitle,
isIgnoredPath, isTempFile, parseSyncthingConflict, isConflictCopy, sanitizeFileName, uniquePath`.

### `src/core/dates`
```ts
todayLocal(now?: Date): LocalDate
toLocalDate(d: Date): LocalDate
toLocalDateTime(d: Date): LocalDateTime
parseLocal(s: LocalDate | LocalDateTime): Date            // local-time Date
isLocalDate(s: string): boolean; isLocalDateTime(s: string): boolean
addDaysLocal(d: LocalDate, n: number): LocalDate
compareLocal(a: string, b: string): number                 // date < datetime on same day
interface NaturalDateResult { date: LocalDate; time?: string; matched: string; index: number }
parseNaturalDate(input: string, now: Date, opts?: { locale?: Locale; weekStartsOn?: 0 | 1 }): NaturalDateResult | null
interface RecurrenceRule { freq: 'day' | 'week' | 'month' | 'year'; interval: number;
  weekdays?: number[]; monthDay?: number | 'last'; weekdaysOnly?: boolean; whenDone?: boolean }
parseRecurrence(text: string): RecurrenceRule | null        // canonical English + Polish
formatRecurrence(rule: RecurrenceRule, locale: Locale): string
canonicalRecurrence(text: string): string | null           // parse + format 'en'
repeaterToRule(repeater: string): RecurrenceRule | null     // Logseq "+1w", ".+1d", "++2m"
nextOccurrence(rule: RecurrenceRule, from: LocalDate | LocalDateTime, completedOn?: LocalDate): LocalDate | LocalDateTime
occurrencesBetween(rule, anchor: LocalDate | LocalDateTime, from: LocalDate, to: LocalDate, max?: number): (LocalDate | LocalDateTime)[]
type DateRange = { from: LocalDate; to: LocalDate }
resolveDateExpr(expr: string, now: Date, weekStartsOn?: 0 | 1): DateRange | 'overdue' | 'none' | 'any' | null
formatDateHuman(d: LocalDate | LocalDateTime, locale: Locale, now?: Date): string   // "dziś 09:00", "jutro", "pt 17 paź"
```

### `src/core/markdown`
```ts
parseNote(path: VaultPath, content: string, config?: VaultConfig): ParsedNote
parseFrontmatter(content: string): { data: Record<string, unknown>; bodyStart: number; bodyStartLine: number } // YAML subset, no deps
stringifyFrontmatter(data: Record<string, unknown>): string
extractInline(text: string, baseOffset?: number, line?: number): { links: Link[]; tags: string[] }
stripTokens(text: string): string
TOKENS                                                   // emoji signifiers (see SYNTAX §5-6)
formatReminderToken(at: LocalDateTime, alarm?: boolean): string
// pure edit helpers: content in -> content out
setTaskStatus(content: string, line: number, status: TaskStatus, today: LocalDate): string
toggleTask(content: string, line: number, today: LocalDate): string
ensureBlockId(content: string, line: number): { content: string; id: string }
generateBlockId(): string
renameLinksInContent(content: string, oldName: string, newName: string, oldPath?: VaultPath, newPath?: VaultPath): string
journalPathForDate(date: LocalDate, config: VaultConfig): VaultPath
journalDateFromPath(path: VaultPath, config: VaultConfig): LocalDate | null
applyTemplate(template: string, vars: { now: Date; title: string; locale: Locale }): { text: string; cursor: number | null }
// outline helpers used by the editor
listItemRange(content: string, line: number): { from: number; to: number } | null   // item + children lines
```

### `src/core/calc`
```ts
interface CalcLineResult { line: number; input: string; value?: string; error?: string; assigned?: string; numeric?: number }
evaluateCalc(source: string, opts?: { scope?: Record<string, unknown>; locale?: Locale }): { lines: CalcLineResult[]; scope: Record<string, unknown> }
evaluateInline(expr: string, scope: Record<string, unknown>, locale?: Locale): { value?: string; error?: string }
pageScope(content: string, uptoLine?: number, locale?: Locale): Record<string, unknown>
interface TableModel { header: string[]; aligns: ('left' | 'center' | 'right' | null)[]; rows: string[][] }
parseTable(markdown: string): TableModel | null
serializeTable(model: TableModel): string               // aligned GFM
evaluateTable(model: TableModel, locale?: Locale): { values: (string | null)[][]; errors: (string | null)[][] }
findTables(content: string): { from: number; to: number; startLine: number; endLine: number }[]
formatNumber(n: number, locale: Locale): string
```

### `src/core/render`
```ts
interface RenderContext {
  path: VaultPath; locale: Locale;
  resolveLink(target: string, from: VaultPath): VaultPath | null;
  href(path: VaultPath | null, target: string, heading?: string, blockId?: string): string;
  assetUrl(path: VaultPath): string | null;
  embed?(target: string, heading: string | undefined, blockId: string | undefined, depth: number): string | null;
  blockRef?(id: string): { path: VaultPath; text: string } | null;
}
renderMarkdown(content: string, ctx: RenderContext): string        // sanitised HTML
sanitizeHtml(html: string): string
```

### `src/core/storage`
```ts
class MemoryFs implements FileSystemAdapter
class IdbFs implements FileSystemAdapter              // new IdbFs(vaultId, label?)
class FsAccessFs implements FileSystemAdapter         // new FsAccessFs(dirHandle) — polls for watch
saveDirectoryHandle(id: string, h: FileSystemDirectoryHandle): Promise<void>
loadDirectoryHandle(id: string): Promise<FileSystemDirectoryHandle | null>
class LocalStore { static open(namespace: string): Promise<LocalStore>; get<T>(key): Promise<T | undefined>;
  set(key, value): Promise<void>; delete(key): Promise<void>; keys(prefix?): Promise<string[]>; entries<T>(prefix): Promise<[string, T][]> }
type ChangeSource = 'user' | 'external' | 'sync' | 'import' | 'system'
interface VaultChange { type: FsEventType; path: VaultPath; oldPath?: VaultPath; source: ChangeSource }
class Vault {
  constructor(fs: FileSystemAdapter, opts: { id: string; name: string; store: LocalStore; history?: History })
  readonly id; readonly name; readonly fs; config: VaultConfig
  open(): Promise<void>                    // list + load config
  files(): FileStat[]                      // non-ignored files (conflict copies included)
  markdownFiles(): FileStat[]
  exists(path): boolean; stat(path): FileStat | undefined
  read(path): Promise<string>; readBinary(path): Promise<Uint8Array>
  write(path, content: string, source?: ChangeSource): Promise<void>   // no-op if unchanged; snapshots history
  writeBinary(path, data, source?): Promise<void>
  create(path, content): Promise<VaultPath>                            // unique path if taken
  delete(path, source?): Promise<void>                                 // -> .trash/
  rename(from, to, source?): Promise<void>
  saveConfig(cfg: VaultConfig): Promise<void>
  onChange(cb: (changes: VaultChange[]) => void): () => void
  startWatching(): void; stopWatching(): void
  rescan(): Promise<VaultChange[]>        // detect external changes by diffing stats
  trash(): Promise<FileStat[]>; restore(trashPath): Promise<VaultPath>
}
class History { constructor(store: LocalStore); snapshot(path, content, reason): Promise<void>;
  list(path): Promise<{ id: string; time: number; size: number; reason: string }[]>;
  get(id): Promise<string | undefined>; prune(): Promise<void> }
contentHash(s: string | Uint8Array): string             // fast stable hash (FNV-1a 64 hex)
```

### `src/core/index`
```ts
class VaultIndex {
  constructor(config?: VaultConfig)
  setConfig(c: VaultConfig): void
  update(path: VaultPath, content: string): void
  remove(path: VaultPath): void
  rename(oldPath: VaultPath, newPath: VaultPath, content: string): void
  clear(): void
  readonly version: number
  note(path): ParsedNote | undefined; notes(): ParsedNote[]; paths(): VaultPath[]
  content(path): string | undefined
  resolveLink(target: string, from?: VaultPath): VaultPath | null
  backlinks(path): Backlink[]
  unlinkedReferences(path): { path: VaultPath; line: number; excerpt: string }[]
  linksTo(path): VaultPath[]                       // files linking to path (for rename refactor)
  blockById(id: string): BlockRef | null
  tags(): { tag: string; count: number; paths: VaultPath[] }[]
  tasks(): TaskRef[]; reminders(): ReminderRef[]
  journals(): { date: LocalDate; path: VaultPath }[]    // newest first
  search(q: string, opts?: { limit?: number }): SearchHit[]
  suggestPages(prefix: string, limit?: number): { path: VaultPath; title: string; alias?: string }[]
  suggestBlocks(text: string, limit?: number): BlockRef[]
  graph(opts?: { tags?: boolean; missing?: boolean }): GraphData
  subscribe(cb: () => void): () => void            // batched notifications
}
parseQuery(src: string): ParsedQuery | { error: string }
runQuery(q: ParsedQuery, index: VaultIndex, now: Date, weekStartsOn?: 0 | 1): QueryResult
```

### `src/core/reminders`
```ts
upcomingNotifications(index: VaultIndex, now: Date, opts?: { horizonDays?: number; max?: number; config?: VaultConfig }): ScheduledNotification[]
class ReminderScheduler { constructor(index: VaultIndex, svc: NotificationService, store: LocalStore, opts?);
  start(): void; stop(): void; refresh(): Promise<void>; missed(): ReminderRef[] }
interface CalendarItem { date: LocalDate; time?: string; kind: 'journal' | 'due' | 'scheduled' | 'start' | 'reminder' | 'done';
  title: string; path: VaultPath; line: number; status?: TaskStatus; recurring?: boolean; alarm?: boolean }
calendarItems(index: VaultIndex, from: LocalDate, to: LocalDate): CalendarItem[]
snoozeReminder(content: string, line: number, newAt: LocalDateTime): string
```

### `src/core/sync`
```ts
class WebDavClient { constructor(o: { baseUrl: string; username: string; password: string; transport: HttpTransport });
  test(): Promise<{ ok: boolean; error?: string }>; list(): Promise<RemoteEntry[]>; get(path): Promise<{ data: Uint8Array; etag: string }>;
  put(path, data, opts?: { ifMatch?: string; ifNoneMatch?: boolean }): Promise<{ etag?: string }>; delete(path): Promise<void>; mkcol(path): Promise<void> }
nextcloudDavUrl(server: string, user: string, folder: string): string
merge3(base: string, local: string, remote: string): { text: string; clean: boolean }   // line diff3 + union for pure additions
class SyncEngine { constructor(o: { vault: Vault; client: WebDavClient; store: LocalStore; device: string }); sync(o?: { signal?: AbortSignal; onProgress?: (p: SyncProgress) => void }): Promise<SyncReport> }
class SyncScheduler { ... periodic + on change + on online }
findSyncthingConflicts(files: FileStat[]): { original: VaultPath; copy: VaultPath; time: string; device: string }[]
class SyncthingClient { status(): Promise<SyncthingStatus>; folders() ... }
STIGNORE_TEMPLATE: string
```

### `src/core/import` / `src/core/export`
`importers: Importer[]` (obsidian, logseq, notion, evernote, keep, joplin, simplenote, roam, bear,
markdown/text, html, ics) and `exporters: Exporter[]` (markdown zip, html site, single html, json,
logseq, ics, opml). Each in its own file; `index.ts` exports the registries and `detectImporter`.

### `src/core/i18n`
`createTranslator(locale): (key, params?) => string`, `plural(locale, n, forms)`. Messages are
split per area in `src/core/i18n/messages/<area>.ts` (each exports `{ pl: {...}, en: {...} }`) so
parallel work never edits the same file; `index.ts` merges them.

### `src/platform`
`getPlatform(): Promise<Platform>` — `web.ts` and `tauri.ts` implement `Platform` from types.ts.

### `src-tauri`
Rust commands (all paths absolute vault root + relative path): `fs_list`, `fs_stat`, `fs_read_text`,
`fs_read_binary`, `fs_write_text` (atomic temp+rename), `fs_write_binary`, `fs_remove`, `fs_rename`,
`fs_mkdir`, `watch_start`, `watch_stop` (emits `brulion://fs-events`), `default_vault_dir`,
`write_export` (save dialog target). Plugins: notification, http, dialog, opener, os,
single-instance + window-state (desktop). Tray icon with show/quit (desktop). Android: local plugin
`brulion-android` (Kotlin) for MANAGE_EXTERNAL_STORAGE check/request.

## Tauri IPC contract (shared by `src/platform/tauri.ts` and `src-tauri/`)

All vault commands take `root` = absolute vault folder path and `path` = VaultPath (relative,
`/`-separated). Rust must reject any `path` that is absolute or escapes `root` (`..`).

| Command | Args (JS camelCase) | Returns |
|---|---|---|
| `fs_list` | `{ root }` | `FileStatDto[]` — recursive, skips `.git`, `node_modules` |
| `fs_stat` | `{ root, path }` | `FileStatDto \| null` |
| `fs_read_text` | `{ root, path }` | `string` |
| `fs_read_binary` | `{ root, path }` | raw bytes (`tauri::ipc::Response`) → `ArrayBuffer` in JS |
| `fs_write_text` | `{ root, path, content }` | `FileStatDto` (atomic: temp file `.brulion-tmp-*` + rename) |
| `fs_write_binary` | raw body = bytes, headers `x-root`, `x-path` (encodeURIComponent) | `FileStatDto` (atomic) |
| `fs_remove` | `{ root, path }` | `null` (recursive for dirs, ok if missing) |
| `fs_rename` | `{ root, from, to }` | `null` (creates parent dirs) |
| `fs_mkdir` | `{ root, path }` | `null` (recursive) |
| `watch_start` | `{ root }` | `null`; emits `brulion://fs-events` with `{ root, events: FsEvent[] }` (debounced ~300 ms, relative paths, ignored dirs filtered) |
| `watch_stop` | `{ root }` | `null` |
| `default_vault_dir` | – | `string` (desktop: `~/Brulion`; Android: `/storage/emulated/0/Brulion` if all-files access, else app data dir + `/vault`) |
| `write_export` | raw body = bytes, header `x-path` (absolute target from save dialog) | `null` |

`FileStatDto = { path: string; kind: 'file' | 'dir'; size: number; mtime: number /* ms */ }`.

Android-only plugin `brulion-android` (`invoke('plugin:brulion-android|<cmd>')`):
`has_all_files_access → { granted: boolean }`, `request_all_files_access → null` (opens the
system settings screen; the app re-checks on resume).

Notifications use the official `@tauri-apps/plugin-notification`: Android schedules natively
(`schedule: Schedule.at(date, false, true)`, channels `reminders` (high) and `alarms` (max,
vibration)); desktop shows immediately while the in-app scheduler keeps timers (app stays in
the tray). Notification `extra` carries `{ path, line }`.
