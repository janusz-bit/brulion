/**
 * `@/core/index` — the in-memory vault index (links, backlinks, tags, tasks, reminders,
 * journals, search, suggestions, graph) and the query language (docs/SYNTAX.md §10).
 * See docs/ARCHITECTURE.md.
 */
export { VaultIndex } from './vaultIndex';
export type { MissingPage, PageSuggestion, ResolvedLink, TagInfo, UnlinkedReference } from './vaultIndex';
export { parseQuery, isQueryError, tokenizeQuery, MAX_QUERY_LIMIT, QUERY_DATE_FIELDS, QUERY_KEYS } from './query/parse';
export type { ParseQueryOptions } from './query/parse';
export { runQuery, DEFAULT_QUERY_LIMIT } from './query/run';
export type { RunQueryOptions } from './query/run';
export type {
  DateCondition,
  DateField,
  DateOp,
  GroupKey,
  PageRef,
  ParsedQuery,
  QueryError,
  QueryErrorCode,
  QueryFilter,
  QueryGroup,
  QueryResult,
  QueryType,
  SortKey,
  SortSpec,
} from './query/types';
export { cleanTarget, linkKey } from './resolve';
export { fold } from './text';
