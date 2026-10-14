/**
 * Query parser (docs/SYNTAX.md §10).
 *
 * ```
 * type:tasks status:todo,doing tag:praca -tag:prywatne due:<=+7d sort:due,-priority limit:20
 * "dokładna fraza" słowo #tag [[Strona]] page:this path:Projekty/ ref:[[Jan]] group:page
 * ```
 *
 * Tokens are separated by whitespace or newlines; `"…"` (also Polish „…” and “…”) and
 * `[[…]]` may contain spaces; `//` starts a comment line. `key:value` filters accept comma
 * lists (ORed); a leading `-` negates a filter or excludes a word. Date filters take an
 * optional `<`, `<=`, `>`, `>=` before the expression. Bare words and quoted phrases search
 * the text; `#tag` is `tag:tag`; `[[Page]]` is `ref:Page`. Polish key and value aliases are
 * accepted (`termin:jutro`, `status:otwarte`, `typ:zadania`, …).
 */
import { resolveDateExpr } from '@/core/dates';
import type { Locale, Priority, TaskStatus } from '@/core/types';
import { fold } from '../text';
import { errorMessage } from './messages';
import type {
  DateCondition,
  DateField,
  DateOp,
  GroupKey,
  ParsedQuery,
  QueryError,
  QueryErrorCode,
  QueryFilter,
  QueryType,
  SortKey,
  SortSpec,
} from './types';

/** Largest accepted `limit:`. */
export const MAX_QUERY_LIMIT = 10000;

/** Canonical filter keys. */
type Key =
  | 'type'
  | 'status'
  | 'priority'
  | 'tag'
  | 'page'
  | 'path'
  | 'ref'
  | 'text'
  | DateField
  | 'sort'
  | 'group'
  | 'limit';

/** Key spellings (folded) → canonical key. */
const KEY_ALIASES: Record<string, Key> = {
  type: 'type', typ: 'type',
  status: 'status', stan: 'status',
  priority: 'priority', priorytet: 'priority', prio: 'priority',
  tag: 'tag', tags: 'tag', tagi: 'tag',
  page: 'page', strona: 'page',
  path: 'path', sciezka: 'path', folder: 'path',
  ref: 'ref', link: 'ref', links: 'ref', linki: 'ref', odnosnik: 'ref',
  text: 'text', tekst: 'text',
  due: 'due', termin: 'due', deadline: 'due',
  scheduled: 'scheduled', zaplanowane: 'scheduled', plan: 'scheduled',
  start: 'start', poczatek: 'start',
  done: 'done', zrobione: 'done', wykonane: 'done',
  created: 'created', utworzone: 'created',
  remind: 'remind', reminder: 'remind', przypomnienie: 'remind',
  date: 'date', data: 'date', dzien: 'date',
  sort: 'sort', sortuj: 'sort',
  group: 'group', grupuj: 'group',
  limit: 'limit',
};

const PUBLIC_KEYS = 'type, status, tag, page, path, ref, due, scheduled, start, done, created, remind, date, priority, text, sort, group, limit';

const TYPE_ALIASES: Record<string, QueryType> = {
  tasks: 'tasks', task: 'tasks', zadania: 'tasks', zadanie: 'tasks',
  blocks: 'blocks', block: 'blocks', bloki: 'blocks', blok: 'blocks',
  pages: 'pages', page: 'pages', strony: 'pages', strona: 'pages', notes: 'pages', notatki: 'pages',
  reminders: 'reminders', reminder: 'reminders', przypomnienia: 'reminders', przypomnienie: 'reminders',
};

const ALL_STATUSES: TaskStatus[] = ['todo', 'doing', 'done', 'cancelled', 'deferred'];

const STATUS_ALIASES: Record<string, TaskStatus[]> = {
  todo: ['todo'], 'do-zrobienia': ['todo'], dozrobienia: ['todo'],
  doing: ['doing'], 'in-progress': ['doing'], 'w-toku': ['doing'], wtoku: ['doing'],
  done: ['done'], zrobione: ['done'], wykonane: ['done'],
  cancelled: ['cancelled'], canceled: ['cancelled'], anulowane: ['cancelled'],
  deferred: ['deferred'], waiting: ['deferred'], odlozone: ['deferred'],
  open: ['todo', 'doing'], otwarte: ['todo', 'doing'],
  closed: ['done', 'cancelled'], zamkniete: ['done', 'cancelled'],
  all: ALL_STATUSES, any: ALL_STATUSES, wszystkie: ALL_STATUSES,
};

const PRIORITY_ALIASES: Record<string, Priority | 'none'> = {
  highest: 'highest', najwyzszy: 'highest',
  high: 'high', wysoki: 'high',
  medium: 'medium', sredni: 'medium',
  low: 'low', niski: 'low',
  lowest: 'lowest', najnizszy: 'lowest',
  none: 'none', normal: 'none', brak: 'none', normalny: 'none',
};

const SORT_ALIASES: Record<string, SortKey> = {
  due: 'due', termin: 'due',
  scheduled: 'scheduled', zaplanowane: 'scheduled',
  start: 'start',
  done: 'done', zrobione: 'done',
  created: 'created', utworzone: 'created',
  remind: 'remind', przypomnienie: 'remind',
  date: 'date', data: 'date',
  priority: 'priority', priorytet: 'priority',
  status: 'status',
  page: 'page', title: 'page', strona: 'page', tytul: 'page',
  path: 'path', sciezka: 'path',
};

const GROUP_ALIASES: Record<string, GroupKey> = {
  page: 'page', strona: 'page',
  due: 'due', termin: 'due',
  scheduled: 'scheduled', zaplanowane: 'scheduled',
  date: 'date', data: 'date',
  tag: 'tag',
  status: 'status',
  priority: 'priority', priorytet: 'priority',
};

/** Filters / sort / group keys that only make sense for tasks. */
const TASK_FILTERS = new Set<string>(['status', 'priority', 'due', 'scheduled', 'start', 'done', 'created']);
/** Filters that cannot be used with `type:pages`. */
const NOT_FOR_PAGES = new Set<string>([...TASK_FILTERS, 'remind']);
const PAGE_SORTS = new Set<SortKey>(['page', 'path', 'date']);
const PAGE_GROUPS = new Set<GroupKey>(['page', 'tag', 'date']);

/** Opening quote → accepted closing quotes. */
const QUOTES: Record<string, string> = { '"': '"', '„': '”“"', '“': '”"', '«': '»' };

/** Fixed reference date used only to validate date expressions. */
const REF_DATE = new Date(2026, 0, 15, 12, 0);

/** Own-property lookup (keys come from user text). */
function lookup<T>(table: Record<string, T>, key: string): T | undefined {
  return Object.prototype.hasOwnProperty.call(table, key) ? table[key] : undefined;
}

/** Parse failure carried through the parser. */
class QuerySyntaxError extends Error {
  constructor(
    readonly code: QueryErrorCode,
    readonly position: number,
    readonly params: Record<string, string | number> = {},
  ) {
    super(code);
  }
}

/** A whitespace-separated token as written (quotes kept). */
interface Token {
  raw: string;
  start: number;
}

function isSpace(ch: string): boolean {
  return /\s/.test(ch);
}

/** End offset (exclusive) of the quoted run starting at `i`, or -1 when unclosed. */
function quoteEnd(src: string, i: number): number {
  const closers = lookup(QUOTES, src[i]!) ?? '"';
  for (let j = i + 1; j < src.length; j++) if (closers.includes(src[j]!)) return j + 1;
  return -1;
}

/** Split the source into tokens (quotes and `[[…]]` may contain spaces). */
export function tokenizeQuery(src: string): Token[] {
  const tokens: Token[] = [];
  let i = 0;
  while (i < src.length) {
    if (isSpace(src[i]!)) {
      i++;
      continue;
    }
    if (src.startsWith('//', i)) {
      const nl = src.indexOf('\n', i);
      i = nl === -1 ? src.length : nl + 1;
      continue;
    }
    const start = i;
    while (i < src.length && !isSpace(src[i]!)) {
      if (lookup(QUOTES, src[i]!) !== undefined) {
        const end = quoteEnd(src, i);
        if (end === -1) throw new QuerySyntaxError('unclosedQuote', i);
        i = end;
      } else if (src.startsWith('[[', i)) {
        const close = src.indexOf(']]', i + 2);
        i = close === -1 ? i + 2 : close + 2;
      } else i++;
    }
    tokens.push({ raw: src.slice(start, i), start });
  }
  return tokens;
}

/** Remove one level of surrounding quotes. */
function unquote(s: string): string {
  const t = s.trim();
  const closers = lookup(QUOTES, t[0] ?? '');
  if (closers !== undefined && t.length >= 2 && closers.includes(t[t.length - 1]!)) return t.slice(1, -1);
  return t;
}

/** Remove surrounding `[[ ]]`. */
function unbracket(s: string): string {
  const t = s.trim();
  return t.startsWith('[[') && t.endsWith(']]') ? t.slice(2, -2).trim() : t;
}

/** Split a value on commas outside quotes and `[[…]]`; unquote; drop empty parts. */
export function splitValues(value: string): string[] {
  const parts: string[] = [];
  let cur = '';
  for (let i = 0; i < value.length; i++) {
    const ch = value[i]!;
    if (lookup(QUOTES, ch) !== undefined) {
      const end = quoteEnd(value, i);
      const stop = end === -1 ? value.length : end;
      cur += value.slice(i, stop);
      i = stop - 1;
    } else if (value.startsWith('[[', i)) {
      const close = value.indexOf(']]', i + 2);
      const stop = close === -1 ? value.length : close + 2;
      cur += value.slice(i, stop);
      i = stop - 1;
    } else if (ch === ',') {
      parts.push(cur);
      cur = '';
    } else cur += ch;
  }
  parts.push(cur);
  return parts.map((p) => unquote(p).trim()).filter((p) => p !== '');
}

/** Normalise a tag value: no `#`, no `[[ ]]`, NFC, lower-case. */
function tagValue(v: string): string {
  return unbracket(v.replace(/^#/, '')).normalize('NFC').toLowerCase();
}

/** Parse `[op]expr` of a date filter. */
function dateCondition(key: string, v: string, pos: number): DateCondition {
  const m = /^(<=|>=|<|>|=)?\s*([\s\S]*)$/.exec(v);
  const op = (m?.[1] ?? '=') as DateOp;
  const expr = (m?.[2] ?? '').trim();
  if (!expr) throw new QuerySyntaxError('emptyValue', pos, { key, example: `${key}:today` });
  const r = resolveDateExpr(expr, REF_DATE, 1);
  if (r === null) throw new QuerySyntaxError('badDate', pos, { key, value: expr });
  if (typeof r === 'string' && op !== '=') throw new QuerySyntaxError('opWithKeyword', pos, { key, op, value: expr });
  return { op, expr };
}

/** Map values through an alias table, failing with a helpful list of allowed values. */
function mapValues<T>(key: string, values: string[], table: Record<string, T>, pos: number, allowed: string): T[] {
  const out: T[] = [];
  for (const v of values) {
    const hit = lookup(table, fold(v).replace(/[\s_]+/g, '-'));
    if (hit === undefined) throw new QuerySyntaxError('badValue', pos, { key, value: v, allowed });
    out.push(hit);
  }
  return out;
}

/** Mutable parse state. */
interface State {
  filters: QueryFilter[];
  sort: SortSpec[];
  type?: QueryType;
  group?: GroupKey;
  limit?: number;
  /** Raw keys used (for type inference / validation), with their first position and value. */
  used: { key: string; value: string; pos: number }[];
}

/** Parse one `key:value` token into the state. */
function applyKey(st: State, key: Key, written: string, rawValue: string, negate: boolean, pos: number): void {
  const values = splitValues(rawValue);
  if (values.length === 0) throw new QuerySyntaxError('emptyValue', pos, { key: written, example: exampleFor(key) });
  st.used.push({ key, value: values.join(','), pos });
  const noNegation = (): void => {
    if (negate) throw new QuerySyntaxError('notNegatable', pos, { key: written });
  };
  switch (key) {
    case 'type': {
      noNegation();
      if (st.type !== undefined) throw new QuerySyntaxError('duplicate', pos, { key: written });
      const allowed = 'tasks, blocks, pages, reminders';
      if (values.length > 1) throw new QuerySyntaxError('badValue', pos, { key: written, value: values.join(','), allowed });
      [st.type] = mapValues(written, values, TYPE_ALIASES, pos, allowed);
      return;
    }
    case 'status': {
      const statuses = mapValues(written, values, STATUS_ALIASES, pos, 'todo, doing, done, cancelled, deferred, open, closed').flat();
      st.filters.push({ kind: 'status', values: [...new Set(statuses)], negate });
      return;
    }
    case 'priority':
      st.filters.push({
        kind: 'priority',
        values: [...new Set(mapValues(written, values, PRIORITY_ALIASES, pos, 'highest, high, medium, low, lowest, none'))],
        negate,
      });
      return;
    case 'tag': {
      const tags = values.map(tagValue).filter(Boolean);
      if (tags.length === 0) throw new QuerySyntaxError('emptyValue', pos, { key: written, example: exampleFor(key) });
      st.filters.push({ kind: 'tag', values: tags, negate });
      return;
    }
    case 'page':
    case 'path':
    case 'ref':
      st.filters.push({ kind: key, values: values.map(unbracket).filter(Boolean), negate });
      return;
    case 'text':
      st.filters.push({ kind: 'text', values, negate });
      return;
    case 'sort': {
      noNegation();
      for (const v of values) {
        const desc = v.startsWith('-');
        const name = desc ? v.slice(1) : v.replace(/^\+/, '');
        const [k] = mapValues(written, [name], SORT_ALIASES, pos, 'due, scheduled, start, done, created, remind, date, priority, status, page, path');
        st.sort.push({ key: k!, desc });
      }
      return;
    }
    case 'group': {
      noNegation();
      if (st.group !== undefined) throw new QuerySyntaxError('duplicate', pos, { key: written });
      if (values.length > 1) throw new QuerySyntaxError('badValue', pos, { key: written, value: values.join(','), allowed: 'page, due, scheduled, date, tag, status, priority' });
      [st.group] = mapValues(written, values, GROUP_ALIASES, pos, 'page, due, scheduled, date, tag, status, priority');
      return;
    }
    case 'limit': {
      noNegation();
      const v = values[0]!;
      const n = /^\d+$/.test(v) ? Number(v) : NaN;
      if (values.length > 1 || !Number.isInteger(n) || n < 1 || n > MAX_QUERY_LIMIT) {
        throw new QuerySyntaxError('badLimit', pos, { value: values.join(','), max: MAX_QUERY_LIMIT });
      }
      st.limit = n;
      return;
    }
    default: {
      // Date fields.
      const conds = values.map((v) => dateCondition(written, v, pos));
      st.filters.push({ kind: 'date', field: key, values: conds, negate });
    }
  }
}

function exampleFor(key: Key): string {
  switch (key) {
    case 'type':
      return 'type:tasks';
    case 'status':
      return 'status:open';
    case 'priority':
      return 'priority:high';
    case 'tag':
      return 'tag:praca';
    case 'page':
      return 'page:Projekt';
    case 'path':
      return 'path:journals/';
    case 'ref':
      return 'ref:[[Jan Kowalski]]';
    case 'text':
      return 'text:"lista zakupów"';
    case 'sort':
      return 'sort:due';
    case 'group':
      return 'group:page';
    case 'limit':
      return 'limit:20';
    default:
      return `${key}:today`;
  }
}

/** Parse one token into the state. */
function applyToken(st: State, tok: Token): void {
  let raw = tok.raw;
  let negate = false;
  let pos = tok.start;
  if (raw.startsWith('-') && raw.length > 1) {
    negate = true;
    raw = raw.slice(1);
    pos++;
  } else if (raw === '-') return;

  if (lookup(QUOTES, raw[0]!) !== undefined) {
    const phrase = unquote(raw);
    if (phrase.trim()) st.filters.push({ kind: 'text', values: [phrase], negate });
    return;
  }
  if (raw.startsWith('#') && raw.length > 1 && raw[1] !== ' ') {
    const tag = tagValue(raw.slice(1));
    if (tag) st.filters.push({ kind: 'tag', values: [tag], negate });
    return;
  }
  if (raw.startsWith('[[') && raw.endsWith(']]')) {
    const name = unbracket(raw);
    if (name) st.filters.push({ kind: 'ref', values: [name], negate });
    return;
  }
  const km = /^([\p{L}_][\p{L}\p{N}_-]*):([\s\S]*)$/u.exec(raw);
  if (km) {
    const written = km[1]!;
    const key = lookup(KEY_ALIASES, fold(written));
    if (!key) throw new QuerySyntaxError('unknownKey', pos, { key: written, allowed: PUBLIC_KEYS });
    applyKey(st, key, written, km[2]!, negate, pos);
    return;
  }
  st.filters.push({ kind: 'text', values: [raw], negate });
}

/** Type from the filters when no `type:` was written. */
function inferType(st: State): QueryType {
  const taskish =
    st.used.some((u) => TASK_FILTERS.has(u.key)) ||
    st.sort.some((s) => TASK_FILTERS.has(s.key)) ||
    (st.group !== undefined && TASK_FILTERS.has(st.group));
  if (taskish) return 'tasks';
  if (st.used.some((u) => u.key === 'remind') || st.sort.some((s) => s.key === 'remind')) return 'reminders';
  return 'blocks';
}

/** Reject filters that make no sense for `type:pages`. */
function validatePages(st: State): void {
  for (const u of st.used) {
    if (NOT_FOR_PAGES.has(u.key)) throw new QuerySyntaxError('typeConflict', u.pos, { key: u.key, value: u.value, type: 'pages' });
  }
  for (const s of st.sort) {
    if (!PAGE_SORTS.has(s.key)) {
      const u = st.used.find((x) => x.key === 'sort');
      throw new QuerySyntaxError('typeConflict', u?.pos ?? 0, { key: 'sort', value: s.key, type: 'pages' });
    }
  }
  if (st.group !== undefined && !PAGE_GROUPS.has(st.group)) {
    const u = st.used.find((x) => x.key === 'group');
    throw new QuerySyntaxError('typeConflict', u?.pos ?? 0, { key: 'group', value: st.group, type: 'pages' });
  }
}

/** Options of {@link parseQuery}. */
export interface ParseQueryOptions {
  /** Language of error messages and group labels (default Polish). */
  locale?: Locale;
}

/**
 * Parse and validate a query (the body of a ```` ```query ```` block). Returns the
 * {@link ParsedQuery}, or a {@link QueryError} with a localised, helpful message.
 */
export function parseQuery(src: string, opts: ParseQueryOptions = {}): ParsedQuery | QueryError {
  const locale = opts.locale ?? 'pl';
  const st: State = { filters: [], sort: [], used: [] };
  try {
    for (const tok of tokenizeQuery(src)) applyToken(st, tok);
    const explicitType = st.type !== undefined;
    const type = st.type ?? inferType(st);
    if (type === 'pages') validatePages(st);
    const q: ParsedQuery = { source: src, type, explicitType, filters: st.filters, sort: st.sort, locale };
    if (st.group !== undefined) q.group = st.group;
    if (st.limit !== undefined) q.limit = st.limit;
    return q;
  } catch (e) {
    if (e instanceof QuerySyntaxError) return { error: errorMessage(locale, e.code, e.params), code: e.code, position: e.position };
    throw e;
  }
}

/** Type guard: did {@link parseQuery} fail? */
export function isQueryError(r: ParsedQuery | QueryError): r is QueryError {
  return 'error' in r;
}

/** Date filter keys (for completion UIs). */
export const QUERY_DATE_FIELDS: readonly DateField[] = ['due', 'scheduled', 'start', 'done', 'created', 'remind', 'date'];

/** Canonical filter keys (for completion UIs). */
export const QUERY_KEYS: readonly string[] = PUBLIC_KEYS.split(', ');
