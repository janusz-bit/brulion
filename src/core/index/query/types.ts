/**
 * Query language model (docs/SYNTAX.md §10): what {@link parseQuery} produces and
 * {@link runQuery} consumes.
 */
import type { BlockRef, Locale, ParsedNote, Priority, ReminderRef, TaskRef, TaskStatus, VaultPath } from '@/core/types';

/** What a query returns. */
export type QueryType = 'tasks' | 'blocks' | 'pages' | 'reminders';

/**
 * Date fields: task dates (`due`, `scheduled`, `start`, `done`, `created`), reminder times
 * (`remind`) and the journal date of the page (`date`).
 */
export type DateField = 'due' | 'scheduled' | 'start' | 'done' | 'created' | 'remind' | 'date';

/** Comparison written before a date expression (`due:<=+7d`); `=` = within the range. */
export type DateOp = '=' | '<' | '<=' | '>' | '>=';

/** One date condition: operator + expression understood by `resolveDateExpr`. */
export interface DateCondition {
  op: DateOp;
  expr: string;
}

/**
 * A filter. Filters are ANDed; the values of one filter are ORed (`tag:a,b` = a or b);
 * `negate` (written `-tag:x`) inverts the whole filter.
 */
export type QueryFilter =
  | { kind: 'status'; values: TaskStatus[]; negate: boolean }
  | { kind: 'priority'; values: (Priority | 'none')[]; negate: boolean }
  /** Tag names, lower-case without `#`; `a` also matches nested `a/b`. */
  | { kind: 'tag'; values: string[]; negate: boolean }
  /** Page names (title, path without `.md`, alias or link target); `*` wildcards; `this` = current page. */
  | { kind: 'page'; values: string[]; negate: boolean }
  /** Path prefixes (case/diacritic-insensitive); `*` wildcards. */
  | { kind: 'path'; values: string[]; negate: boolean }
  /** Pages the item links to (`ref:[[Jan]]`, also written as a bare `[[Jan]]`). */
  | { kind: 'ref'; values: string[]; negate: boolean }
  /** Text the item contains (case/diacritic-insensitive). */
  | { kind: 'text'; values: string[]; negate: boolean }
  | { kind: 'date'; field: DateField; values: DateCondition[]; negate: boolean };

/** Sort keys (`sort:due,-priority`). */
export type SortKey = 'due' | 'scheduled' | 'start' | 'done' | 'created' | 'remind' | 'date' | 'priority' | 'status' | 'page' | 'path';

export interface SortSpec {
  key: SortKey;
  /** Written with a leading `-`. Missing values stay last either way. */
  desc: boolean;
}

/** Group keys (`group:page`). */
export type GroupKey = 'page' | 'due' | 'scheduled' | 'date' | 'tag' | 'status' | 'priority';

/** A validated query. */
export interface ParsedQuery {
  /** The query text as written. */
  source: string;
  /** Explicit `type:` or inferred (task filters → tasks, `remind:` → reminders, else blocks). */
  type: QueryType;
  explicitType: boolean;
  filters: QueryFilter[];
  sort: SortSpec[];
  group?: GroupKey;
  limit?: number;
  /** Language for group labels (from `parseQuery` options; default Polish). */
  locale?: Locale;
}

/** Error codes of {@link parseQuery} (the message is localised). */
export type QueryErrorCode =
  | 'unknownKey'
  | 'emptyValue'
  | 'badValue'
  | 'badDate'
  | 'opWithKeyword'
  | 'badLimit'
  | 'unclosedQuote'
  | 'notNegatable'
  | 'typeConflict'
  | 'duplicate';

/** A query that could not be parsed. */
export interface QueryError {
  /** Human-readable, localised message. */
  error: string;
  code: QueryErrorCode;
  /** Offset in the source where the problem starts. */
  position: number;
}

/** A page returned by `type:pages`. */
export interface PageRef {
  path: VaultPath;
  title: string;
  note: ParsedNote;
}

/** Items of one group (`group:` queries), in result order. */
export interface QueryGroup<T> {
  /** Machine key: path, date, tag, status, priority; `''` for "none". */
  key: string;
  /** Display label (localised for "none" / statuses / priorities). */
  label: string;
  items: T[];
}

interface ResultOf<K extends QueryType, T> {
  type: K;
  /** Matching items after sorting and `limit`. */
  items: T[];
  /** Present for `group:` queries (built from `items`). */
  groups?: QueryGroup<T>[];
  /** Number of matches before `limit`. */
  total: number;
}

/** Result of {@link runQuery}; `items` depend on `type`. */
export type QueryResult =
  | ResultOf<'tasks', TaskRef>
  | ResultOf<'blocks', BlockRef>
  | ResultOf<'pages', PageRef>
  | ResultOf<'reminders', ReminderRef>;
