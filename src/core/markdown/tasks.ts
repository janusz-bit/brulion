/**
 * Task markers (docs/SYNTAX.md §5): Markdown checkboxes `- [ ]` and Logseq keywords
 * (`TODO`, `DOING`, `DONE`, …) with their status mapping in both directions.
 */
import type { TaskStatus } from '../types';

/** Checkbox character -> status. Unknown characters (theme "custom statuses") count as todo. */
const CHECKBOX_STATUS: Readonly<Record<string, TaskStatus>> = {
  ' ': 'todo',
  x: 'done',
  X: 'done',
  '/': 'doing',
  '-': 'cancelled',
  '>': 'deferred',
};

/** Status -> checkbox character written by Brulion. */
export const STATUS_CHAR: Readonly<Record<TaskStatus, string>> = {
  todo: ' ',
  doing: '/',
  done: 'x',
  cancelled: '-',
  deferred: '>',
};

/** Logseq task keyword -> status. */
export const LOGSEQ_KEYWORDS: Readonly<Record<string, TaskStatus>> = {
  TODO: 'todo',
  LATER: 'todo',
  DOING: 'doing',
  NOW: 'doing',
  'IN-PROGRESS': 'doing',
  DONE: 'done',
  CANCELED: 'cancelled',
  CANCELLED: 'cancelled',
  WAITING: 'deferred',
  WAIT: 'deferred',
};

const CHECKBOX_RE = /^\[([^\]\r\n])\](?=[ \t]|$)/;
const KEYWORD_RE = /^(TODO|LATER|DOING|NOW|IN-PROGRESS|DONE|CANCELED|CANCELLED|WAITING|WAIT)(?=[ \t]|$)/;

/** Status of a checkbox character. */
export function statusFromCheckbox(ch: string): TaskStatus {
  return CHECKBOX_STATUS[ch] ?? 'todo';
}

/** Status of a Logseq keyword (todo for unknown words). */
export function statusFromKeyword(word: string): TaskStatus {
  return LOGSEQ_KEYWORDS[word] ?? 'todo';
}

/** Checkbox character for `status`, keeping the written `X` / custom char when the status is unchanged. */
export function checkboxCharFor(status: TaskStatus, previous?: string): string {
  if (previous !== undefined && statusFromCheckbox(previous) === status) return previous;
  return STATUS_CHAR[status];
}

/**
 * Logseq keyword for `status`, following the workflow of the previous keyword
 * (`LATER`/`NOW` vs `TODO`/`DOING`) and its spelling of "cancelled".
 */
export function keywordFor(status: TaskStatus, previous?: string): string {
  if (previous !== undefined && statusFromKeyword(previous) === status) return previous;
  const nowLater = previous === 'LATER' || previous === 'NOW';
  switch (status) {
    case 'todo':
      return nowLater ? 'LATER' : 'TODO';
    case 'doing':
      return nowLater ? 'NOW' : 'DOING';
    case 'done':
      return 'DONE';
    case 'cancelled':
      return previous === 'CANCELLED' ? 'CANCELLED' : 'CANCELED';
    case 'deferred':
      return previous === 'WAIT' ? 'WAIT' : 'WAITING';
  }
}

/** A task marker found at the start of a list item's content. */
export interface TaskMarker {
  kind: 'checkbox' | 'keyword';
  status: TaskStatus;
  /** Raw marker: the checkbox character or the keyword. */
  marker: string;
  /** Index of the marker start (`[` or keyword). */
  from: number;
  /** Index after the marker (before following whitespace). */
  to: number;
  /** Index after the marker and the whitespace following it. */
  contentStart: number;
}

/**
 * Detect a checkbox (`[ ]`, `[x]`, …) or Logseq keyword at `start` in a list item line.
 * `masked` is used for detection (so masked comments never form a marker), `text` for the
 * marker character.
 */
export function matchTaskMarker(text: string, masked: string, start: number): TaskMarker | null {
  const rest = masked.slice(start);
  let kind: TaskMarker['kind'];
  let marker: string;
  let len: number;
  const cb = CHECKBOX_RE.exec(rest);
  if (cb) {
    kind = 'checkbox';
    marker = text[start + 1] ?? ' ';
    len = 3;
  } else {
    const kw = KEYWORD_RE.exec(rest);
    if (!kw) return null;
    kind = 'keyword';
    marker = kw[1]!;
    len = marker.length;
  }
  let contentStart = start + len;
  while (masked[contentStart] === ' ' || masked[contentStart] === '\t') contentStart++;
  return {
    kind,
    status: kind === 'checkbox' ? statusFromCheckbox(marker) : statusFromKeyword(marker),
    marker,
    from: start,
    to: start + len,
    contentStart,
  };
}
