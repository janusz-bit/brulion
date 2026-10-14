/**
 * Query execution (docs/SYNTAX.md §10) over a {@link VaultIndex}.
 *
 * Semantics:
 * * tags: a block's own tags, its ancestors' tags and the page-level tags (frontmatter /
 *   leading `tags::`) — so tasks under `- Projekt #praca` match `tag:praca`; `tag:a` also
 *   matches nested `a/b`. Pages match on all their tags.
 * * dates compare by day (`due:<=+7d`); `overdue` = before today and the task is not done or
 *   cancelled (for `remind:` — before now); `none` = missing, `any` = present. Missing dates
 *   never satisfy a range and sort last in both directions.
 * * text: case- and diacritic-insensitive "contains" on the block text (pages: title + content).
 * * `type:blocks` drops blocks whose ancestor also matched (they are shown inside it).
 */
import { compareLocal, resolveDateExpr, toLocalDateTime, todayLocal, type DateExprResult } from '@/core/dates';
import { stripMdExtension } from '@/core/paths';
import type { Block, Locale, ParsedNote, Priority, Reminder, ReminderRef, Task, TaskRef, TaskStatus, VaultPath, BlockRef } from '@/core/types';
import { linkKey } from '../resolve';
import { compareCode, compareNames, escapeRegExp, fold, foldedText } from '../text';
import type { VaultIndex } from '../vaultIndex';
import { noneLabel, priorityLabel, statusLabel } from './messages';
import type { DateCondition, DateField, GroupKey, PageRef, ParsedQuery, QueryFilter, QueryGroup, QueryResult, SortKey } from './types';

/** Items returned when the query has no `limit:`. */
export const DEFAULT_QUERY_LIMIT = 500;

/** Extra context for {@link runQuery}. */
export interface RunQueryOptions {
  /** Page containing the query block: `page:this` and relative `ref:` targets use it. */
  currentPath?: VaultPath;
  /** Language of group labels (default: the query's locale, else Polish). */
  locale?: Locale;
}

/** Uniform view of a candidate item. */
interface View {
  path: VaultPath;
  note: ParsedNote;
  block?: Block;
  task?: Task;
  reminder?: Reminder;
}

interface Candidate<T> {
  item: T;
  view: View;
}

/** Per-run context (resolved expressions and caches). */
interface Ctx {
  index: VaultIndex;
  today: string;
  nowLocal: string;
  now: Date;
  weekStartsOn: 0 | 1;
  locale: Locale;
  currentPath?: VaultPath;
  exprs: Map<string, DateExprResult>;
  resolved: Map<string, VaultPath | null>;
  refLines: Map<string, Set<number>>;
}

const THIS_PAGE = new Set(['this', 'current', 'ta', 'ten', 'biezaca', 'biezacy', 'tutaj']);
const PRIORITY_RANK: Record<Priority | 'none', number> = { highest: 0, high: 1, medium: 2, none: 3, low: 4, lowest: 5 };
const STATUS_RANK: Record<TaskStatus, number> = { doing: 0, todo: 1, deferred: 2, done: 3, cancelled: 4 };
const CLOSED = new Set<TaskStatus>(['done', 'cancelled']);

// ---------------------------------------------------------------------------------------------
// Per-note metadata (cached per parsed note object)
// ---------------------------------------------------------------------------------------------

interface NoteMeta {
  parent: Map<Block, Block | null>;
  /** Page-level tags (frontmatter / leading properties), not tags of ordinary blocks. */
  pageTags: string[];
  /** Effective tags per block (own + ancestors + page-level). */
  effective: Map<Block, string[]>;
  folded?: string;
}

const metaCache = new WeakMap<ParsedNote, NoteMeta>();

function metaOf(note: ParsedNote): NoteMeta {
  let m = metaCache.get(note);
  if (m) return m;
  const parent = new Map<Block, Block | null>();
  const walk = (blocks: Block[], p: Block | null): void => {
    for (const b of blocks) {
      parent.set(b, p);
      if (b.children.length) walk(b.children, b);
    }
  };
  walk(note.blocks, null);
  m = { parent, pageTags: pageLevelTags(note), effective: new Map() };
  metaCache.set(note, m);
  return m;
}

/** Tags of the page itself: frontmatter `tags` and tags of the leading properties block. */
function pageLevelTags(note: ParsedNote): string[] {
  const inBlocks = new Set<string>();
  const inFront = new Set<string>();
  for (const b of note.allBlocks) for (const t of b.tags) (b.kind === 'frontmatter' ? inFront : inBlocks).add(t);
  const fm = new Set<string>();
  for (const k of ['tags', 'tag']) {
    const v = note.frontmatter[k];
    const parts = typeof v === 'string' ? v.split(/[,\s]+/) : Array.isArray(v) ? v.filter((x): x is string => typeof x === 'string') : [];
    for (const p of parts) fm.add(p.trim().replace(/^#/, '').normalize('NFC').toLowerCase());
  }
  return note.tags.filter((t) => inFront.has(t) || fm.has(t) || !inBlocks.has(t));
}

function effectiveTags(view: View): string[] {
  if (!view.block) return view.note.tags;
  const meta = metaOf(view.note);
  let tags = meta.effective.get(view.block);
  if (tags) return tags;
  const set = new Set<string>(meta.pageTags);
  for (let b: Block | null | undefined = view.block; b; b = meta.parent.get(b)) for (const t of b.tags) set.add(t);
  tags = [...set];
  meta.effective.set(view.block, tags);
  return tags;
}

function foldedContent(index: VaultIndex, note: ParsedNote): string {
  const meta = metaOf(note);
  return (meta.folded ??= `${fold(note.title)}\n${fold(index.content(note.path) ?? '')}`);
}

// ---------------------------------------------------------------------------------------------
// Filters
// ---------------------------------------------------------------------------------------------

/** Glob (`*` = anything) → anchored RegExp over folded text. */
function globRe(pattern: string, prefixOnly: boolean): RegExp {
  const body = pattern.split('*').map(escapeRegExp).join('.*');
  return new RegExp(`^${body}${prefixOnly ? '' : '$'}`, 's');
}

function resolveCached(ctx: Ctx, target: string): VaultPath | null {
  let r = ctx.resolved.get(target);
  if (r === undefined) {
    r = ctx.index.resolveLink(target, ctx.currentPath);
    ctx.resolved.set(target, r);
  }
  return r;
}

function pageMatches(value: string, note: ParsedNote, ctx: Ctx): boolean {
  const v = value.trim();
  const fv = fold(v);
  if (THIS_PAGE.has(fv)) return ctx.currentPath === note.path;
  const names = [note.title, stripMdExtension(note.path), ...note.aliases].map(fold);
  if (v.includes('*')) {
    const re = globRe(fv, false);
    return names.some((n) => re.test(n));
  }
  return names.includes(fv) || resolveCached(ctx, v) === note.path;
}

function pathMatches(value: string, path: VaultPath): boolean {
  const p = fold(value.trim()).replace(/^\.?\/+/, '');
  const fp = fold(path);
  return p.includes('*') ? globRe(p, true).test(fp) : fp.startsWith(p);
}

/** Lines of `path` with a link to the page named `value` (resolved, or same missing name). */
function refLines(ctx: Ctx, path: VaultPath, value: string): Set<number> {
  const cacheKey = `${path}\u0000${value}`;
  let lines = ctx.refLines.get(cacheKey);
  if (lines) return lines;
  lines = new Set();
  const target = THIS_PAGE.has(fold(value.trim())) ? (ctx.currentPath ?? null) : resolveCached(ctx, value);
  const key = linkKey(value);
  for (const l of ctx.index.outgoingLinks(path)) {
    const byId = l.link.kind === 'blockref' || l.link.kind === 'blockembed';
    const hit = target !== null ? l.target === target : !byId && l.target === null && linkKey(l.link.target) === key;
    if (hit) lines.add(l.link.line);
  }
  ctx.refLines.set(cacheKey, lines);
  return lines;
}

function refMatches(value: string, view: View, ctx: Ctx): boolean {
  const lines = refLines(ctx, view.path, value);
  if (lines.size === 0) return false;
  if (!view.block) return true;
  const meta = metaOf(view.note);
  for (let b: Block | null | undefined = view.block; b; b = meta.parent.get(b)) {
    for (const l of lines) if (l >= b.line && l <= b.ownEndLine) return true;
  }
  return false;
}

function textOf(view: View, ctx: Ctx): string {
  return view.block ? foldedText(view.block) : foldedContent(ctx.index, view.note);
}

/** Values of a date field for an item (a block may have several reminders). */
function dateValues(field: DateField, view: View): string[] {
  const t = view.task;
  let v: string | undefined;
  switch (field) {
    case 'due':
    case 'scheduled':
    case 'start':
    case 'created':
      v = t?.[field];
      break;
    case 'done':
      v = t?.doneDate;
      break;
    case 'date':
      v = view.note.journalDate;
      break;
    case 'remind':
      if (view.reminder) return [view.reminder.at];
      return (view.block ? view.block.reminders : view.note.reminders).map((r) => r.at);
  }
  return v ? [v] : [];
}

function exprOf(ctx: Ctx, expr: string): DateExprResult {
  let r = ctx.exprs.get(expr);
  if (r === undefined) {
    r = resolveDateExpr(expr, ctx.now, ctx.weekStartsOn);
    ctx.exprs.set(expr, r);
  }
  return r;
}

function isOverdue(field: DateField, value: string, view: View, ctx: Ctx): boolean {
  const open = !view.task || !CLOSED.has(view.task.status);
  if (field === 'remind') return open && compareLocal(value, ctx.nowLocal) < 0;
  const day = value.slice(0, 10);
  if (field === 'due' || field === 'scheduled' || field === 'start') return open && day < ctx.today;
  return day < ctx.today;
}

function dateMatches(field: DateField, cond: DateCondition, view: View, ctx: Ctx): boolean {
  const values = dateValues(field, view);
  const r = exprOf(ctx, cond.expr);
  if (r === null) return false;
  if (r === 'none') return values.length === 0;
  if (r === 'any') return values.length > 0;
  if (r === 'overdue') return values.some((v) => isOverdue(field, v, view, ctx));
  return values.some((v) => {
    const d = v.slice(0, 10);
    switch (cond.op) {
      case '<':
        return d < r.from;
      case '<=':
        return d <= r.to;
      case '>':
        return d > r.to;
      case '>=':
        return d >= r.from;
      default:
        return d >= r.from && d <= r.to;
    }
  });
}

function tagMatches(value: string, tags: readonly string[]): boolean {
  return tags.some((t) => t === value || t.startsWith(`${value}/`));
}

function test(f: QueryFilter, view: View, ctx: Ctx): boolean {
  switch (f.kind) {
    case 'status':
      return !!view.task && f.values.includes(view.task.status);
    case 'priority':
      return !!view.task && f.values.includes(view.task.priority ?? 'none');
    case 'tag': {
      const tags = effectiveTags(view);
      return f.values.some((v) => tagMatches(v, tags));
    }
    case 'page':
      return f.values.some((v) => pageMatches(v, view.note, ctx));
    case 'path':
      return f.values.some((v) => pathMatches(v, view.path));
    case 'ref':
      return f.values.some((v) => refMatches(v, view, ctx));
    case 'text': {
      const text = textOf(view, ctx);
      return f.values.some((v) => text.includes(fold(v)));
    }
    case 'date':
      return f.values.some((c) => dateMatches(f.field, c, view, ctx));
  }
}

function matchesAll(filters: readonly QueryFilter[], view: View, ctx: Ctx): boolean {
  for (const f of filters) if (test(f, view, ctx) === f.negate) return false;
  return true;
}

// ---------------------------------------------------------------------------------------------
// Sorting and grouping
// ---------------------------------------------------------------------------------------------

/** Sort value of an item; undefined = missing (sorts last). */
function sortValue(key: SortKey, view: View): string | number | undefined {
  switch (key) {
    case 'priority':
      return view.task ? PRIORITY_RANK[view.task.priority ?? 'none'] : undefined;
    case 'status':
      return view.task ? STATUS_RANK[view.task.status] : undefined;
    case 'page':
      return view.note.title;
    case 'path':
      return view.path;
    default: {
      const values = dateValues(key, view);
      if (values.length === 0) return undefined;
      return values.reduce((a, b) => (compareLocal(a, b) <= 0 ? a : b));
    }
  }
}

function compareValues(key: SortKey, a: string | number, b: string | number): number {
  if (typeof a === 'number' && typeof b === 'number') return a - b;
  if (key === 'page') return compareNames(String(a), String(b));
  if (key === 'path') return compareCode(String(a), String(b));
  return compareLocal(String(a), String(b));
}

function lineOf(view: View): number {
  return view.reminder?.line ?? view.block?.line ?? 0;
}

function defaultCompare(type: ParsedQuery['type'], a: View, b: View): number {
  if (type === 'pages') return compareNames(a.note.title, b.note.title) || compareCode(a.path, b.path);
  if (type === 'reminders') {
    const r = compareLocal(a.reminder?.at ?? '', b.reminder?.at ?? '');
    if (r !== 0) return r;
  }
  return compareCode(a.path, b.path) || lineOf(a) - lineOf(b);
}

function sortCandidates<T>(cands: Candidate<T>[], q: ParsedQuery): void {
  cands.sort((x, y) => {
    for (const s of q.sort) {
      const a = sortValue(s.key, x.view);
      const b = sortValue(s.key, y.view);
      if (a === undefined && b === undefined) continue;
      if (a === undefined) return 1;
      if (b === undefined) return -1;
      const c = compareValues(s.key, a, b);
      if (c !== 0) return s.desc ? -c : c;
    }
    return defaultCompare(q.type, x.view, y.view);
  });
}

/** Group keys of an item: [key, label, order] — several for tags. */
function groupsOf(group: GroupKey, view: View, ctx: Ctx): { key: string; label: string; order: string | number }[] {
  const loc = ctx.locale;
  switch (group) {
    case 'page':
      return [{ key: view.path, label: view.note.title, order: view.note.title }];
    case 'due':
    case 'scheduled':
    case 'date': {
      const d = dateValues(group, view)[0]?.slice(0, 10);
      return [d ? { key: d, label: d, order: d } : { key: '', label: noneLabel(loc, 'date'), order: '￿' }];
    }
    case 'tag': {
      const tags = effectiveTags(view);
      if (tags.length === 0) return [{ key: '', label: noneLabel(loc, 'tag'), order: '￿' }];
      return tags.map((t) => ({ key: t, label: `#${t}`, order: t }));
    }
    case 'status':
      return view.task
        ? [{ key: view.task.status, label: statusLabel(loc, view.task.status), order: STATUS_RANK[view.task.status] }]
        : [{ key: '', label: noneLabel(loc, 'status'), order: 99 }];
    case 'priority': {
      if (!view.task) return [{ key: '', label: noneLabel(loc, 'priority'), order: 99 }];
      const p = view.task.priority ?? 'none';
      return [{ key: p, label: priorityLabel(loc, p), order: PRIORITY_RANK[p] }];
    }
  }
}

function buildGroups<T>(cands: Candidate<T>[], group: GroupKey, ctx: Ctx): QueryGroup<T>[] {
  const map = new Map<string, QueryGroup<T> & { order: string | number }>();
  for (const c of cands) {
    for (const g of groupsOf(group, c.view, ctx)) {
      let entry = map.get(g.key);
      if (!entry) map.set(g.key, (entry = { key: g.key, label: g.label, order: g.order, items: [] }));
      entry.items.push(c.item);
    }
  }
  const out = [...map.values()].sort((a, b) => {
    if (typeof a.order === 'number' && typeof b.order === 'number') return a.order - b.order;
    if (group === 'page') return compareNames(String(a.order), String(b.order)) || compareCode(a.key, b.key);
    return compareCode(String(a.order), String(b.order));
  });
  return out.map(({ key, label, items }) => ({ key, label, items }));
}

// ---------------------------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------------------------

/** Drop blocks whose ancestor is also in the result (type:blocks). */
function collapseNested<T>(cands: Candidate<T>[]): Candidate<T>[] {
  const matched = new Set<Block>();
  for (const c of cands) if (c.view.block) matched.add(c.view.block);
  return cands.filter((c) => {
    if (!c.view.block) return true;
    const meta = metaOf(c.view.note);
    for (let p = meta.parent.get(c.view.block); p; p = meta.parent.get(p)) if (matched.has(p)) return false;
    return true;
  });
}

function finish<T>(all: Candidate<T>[], q: ParsedQuery, ctx: Ctx): { items: T[]; groups?: QueryGroup<T>[]; total: number } {
  let cands = all.filter((c) => matchesAll(q.filters, c.view, ctx));
  if (q.type === 'blocks') cands = collapseNested(cands);
  sortCandidates(cands, q);
  const total = cands.length;
  const limited = cands.slice(0, q.limit ?? DEFAULT_QUERY_LIMIT);
  const out: { items: T[]; groups?: QueryGroup<T>[]; total: number } = { items: limited.map((c) => c.item), total };
  if (q.group) out.groups = buildGroups(limited, q.group, ctx);
  return out;
}

function taskCandidates(index: VaultIndex): Candidate<TaskRef>[] {
  const out: Candidate<TaskRef>[] = [];
  for (const t of index.tasks()) {
    const note = index.note(t.path);
    if (note) out.push({ item: t, view: { path: t.path, note, block: t.block, task: t.task } });
  }
  return out;
}

function blockCandidates(index: VaultIndex): Candidate<BlockRef>[] {
  const out: Candidate<BlockRef>[] = [];
  for (const note of index.notes()) {
    for (const block of note.allBlocks) {
      if (block.kind === 'frontmatter') continue;
      const view: View = { path: note.path, note, block };
      if (block.task) view.task = block.task;
      out.push({ item: { path: note.path, block }, view });
    }
  }
  return out;
}

function pageCandidates(index: VaultIndex): Candidate<PageRef>[] {
  return index.notes().map((note) => ({ item: { path: note.path, title: note.title, note }, view: { path: note.path, note } }));
}

function reminderCandidates(index: VaultIndex): Candidate<ReminderRef>[] {
  const out: Candidate<ReminderRef>[] = [];
  for (const r of index.reminders()) {
    const note = index.note(r.path);
    if (!note) continue;
    const view: View = { path: r.path, note, block: r.block, reminder: r.reminder };
    if (r.task) view.task = r.task;
    out.push({ item: r, view });
  }
  return out;
}

/**
 * Run a parsed query against the index. `now` defines today / overdue; `weekStartsOn`
 * defines `this-week` etc. Never throws for a query returned by `parseQuery`.
 */
export function runQuery(q: ParsedQuery, index: VaultIndex, now: Date, weekStartsOn: 0 | 1 = 1, opts: RunQueryOptions = {}): QueryResult {
  const ctx: Ctx = {
    index,
    now,
    today: todayLocal(now),
    nowLocal: toLocalDateTime(now),
    weekStartsOn,
    locale: opts.locale ?? q.locale ?? 'pl',
    exprs: new Map(),
    resolved: new Map(),
    refLines: new Map(),
  };
  if (opts.currentPath !== undefined) ctx.currentPath = opts.currentPath;
  switch (q.type) {
    case 'tasks':
      return { type: 'tasks', ...finish(taskCandidates(index), q, ctx) };
    case 'blocks':
      return { type: 'blocks', ...finish(blockCandidates(index), q, ctx) };
    case 'pages':
      return { type: 'pages', ...finish(pageCandidates(index), q, ctx) };
    case 'reminders':
      return { type: 'reminders', ...finish(reminderCandidates(index), q, ctx) };
  }
}
