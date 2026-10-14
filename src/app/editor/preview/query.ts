/**
 * ```query blocks (docs/SYNTAX.md §10) rendered as live results while the cursor is outside:
 * `parseQuery` / `runQuery` from @/core/index against the workspace's VaultIndex. Workspaces
 * whose index is not a VaultIndex (FakeWorkspace, tests) get a cached VaultIndex snapshot built
 * from `paths()` + `content()`. Tasks in the results toggle in their own files (actions.ts).
 */
import type { EditorView } from '@codemirror/view';
import { formatDateHuman, toLocalDate } from '@/core/dates';
import { isQueryError, parseQuery, runQuery, VaultIndex, type QueryResult } from '@/core/index';
import { pageTitle } from '@/core/paths';
import type { Locale, TaskStatus, VaultConfig, VaultPath } from '@/core/types';
import { editorContext, type EditorIndex } from '../context';
import { h, icon } from './dom';
import { renderContextFor } from './embeds';
import { plainInline, renderInlineSafe } from './render';
import { PreviewWidget } from './widgets';

// ---------------------------------------------------------------------------------------------
// The index to query
// ---------------------------------------------------------------------------------------------

const snapshots = new WeakMap<object, { key: string; index: VaultIndex }>();

function isVaultIndex(index: EditorIndex): index is EditorIndex & VaultIndex {
  if (index instanceof VaultIndex) return true;
  const o = index as unknown as Record<string, unknown>;
  return typeof o.tasks === 'function' && typeof o.notes === 'function' && typeof o.reminders === 'function';
}

/**
 * A VaultIndex for `index`: itself when it is one, otherwise a snapshot rebuilt when the index's
 * `version` (or, without one, `fallbackVersion`) changes.
 */
export function queryIndex(index: EditorIndex, config: VaultConfig, fallbackVersion: number): VaultIndex {
  if (isVaultIndex(index)) return index;
  const v = (index as unknown as { version?: unknown }).version;
  const key = typeof v === 'number' ? `v${v}` : `f${fallbackVersion}`;
  const cached = snapshots.get(index);
  if (cached && cached.key === key) return cached.index;
  cached?.index.clear();
  const snapshot = new VaultIndex(config);
  for (const p of index.paths()) {
    const content = index.content(p);
    if (content !== undefined) snapshot.update(p, content);
  }
  snapshots.set(index, { key, index: snapshot });
  return snapshot;
}

// ---------------------------------------------------------------------------------------------
// Result normalisation (pure, tested)
// ---------------------------------------------------------------------------------------------

export type QueryRow =
  | { kind: 'task'; path: VaultPath; line: number; status: TaskStatus; text: string; due?: string }
  | { kind: 'reminder'; path: VaultPath; line: number; at: string; alarm: boolean; text: string }
  | { kind: 'block'; path: VaultPath; line: number; text: string }
  | { kind: 'page'; path: VaultPath; title: string };

export interface QueryView {
  groups: { label: string | null; rows: QueryRow[] }[];
  /** Matches before `limit`. */
  total: number;
  /** Rows shown. */
  shown: number;
  error?: string;
}

function rowsOf(res: QueryResult, items: readonly unknown[]): QueryRow[] {
  switch (res.type) {
    case 'tasks':
      return (items as typeof res.items).map((r) => {
        const row: QueryRow = { kind: 'task', path: r.path, line: r.task.line, status: r.task.status, text: r.task.text };
        if (r.task.due) row.due = r.task.due;
        return row;
      });
    case 'reminders':
      return (items as typeof res.items).map((r) => ({ kind: 'reminder', path: r.path, line: r.reminder.line, at: r.reminder.at, alarm: r.reminder.alarm, text: r.reminder.text }));
    case 'blocks':
      return (items as typeof res.items).map((r) => ({ kind: 'block', path: r.path, line: r.block.line, text: r.block.text }));
    case 'pages':
      return (items as typeof res.items).map((r) => ({ kind: 'page', path: r.path, title: r.title || pageTitle(r.path) }));
  }
}

/** A `runQuery` result as groups of display rows (one unlabelled group when not grouped). */
export function normalizeQueryResult(res: QueryResult): QueryView {
  const groups: QueryView['groups'] = res.groups
    ? res.groups.map((g) => ({ label: g.label, rows: rowsOf(res, g.items) }))
    : [{ label: null, rows: rowsOf(res, res.items) }];
  return { groups, total: res.total, shown: groups.reduce((n, g) => n + g.rows.length, 0) };
}

/** Body of the query (fence lines and `#` / `//` comment lines removed). */
export function querySource(fenced: string): string {
  const lines = fenced.split('\n');
  return lines
    .slice(1, lines.length > 1 && /^\s*(`{3,}|~{3,})\s*$/.test(lines[lines.length - 1] ?? '') ? -1 : undefined)
    .map((l) => l.trim())
    .filter((l) => l && !/^#(\s|$)/.test(l) && !l.startsWith('//'))
    .join('\n');
}

/** Parse and run a query; errors (syntax, runtime) become `{ error }`. */
export function evaluateQuery(source: string, index: VaultIndex, opts: { now: Date; locale: Locale; weekStartsOn: 0 | 1; path: VaultPath }): QueryView {
  try {
    const parsed = parseQuery(source, { locale: opts.locale });
    if (isQueryError(parsed)) return { groups: [], total: 0, shown: 0, error: parsed.error };
    return normalizeQueryResult(runQuery(parsed, index, opts.now, opts.weekStartsOn, { currentPath: opts.path, locale: opts.locale }));
  } catch (err) {
    return { groups: [], total: 0, shown: 0, error: String((err as Error)?.message ?? err) };
  }
}

// ---------------------------------------------------------------------------------------------
// Widget
// ---------------------------------------------------------------------------------------------

export interface QueryLabels {
  title: string;
  unavailable: string;
  empty: string;
  edit: string;
  /** Already formatted error prefix (e.g. "Błąd zapytania:"). */
  error: string;
  count: (n: number) => string;
  toggle: string;
  open: string;
}

export class QueryWidget extends PreviewWidget {
  constructor(
    readonly source: string,
    /** Vault version: a new version re-runs the query. */
    readonly version: number,
    readonly labels: QueryLabels,
    readonly editable: boolean,
  ) {
    super();
  }

  override eq(o: QueryWidget): boolean {
    return o.source === this.source && o.version === this.version && o.editable === this.editable && o.labels.title === this.labels.title;
  }

  override get estimatedHeight(): number {
    return 140;
  }

  toDOM(view: EditorView): HTMLElement {
    const ctx = view.state.facet(editorContext);
    const el = h('div', { class: 'b-lp-query', 'data-lp-root': '', 'data-lp': 'query' });
    const count = h('span', { class: 'b-lp-query__count' });
    el.appendChild(
      h(
        'div',
        { class: 'b-lp-query__head' },
        icon('search', 'b-lp-icon b-lp-query__icon'),
        h('span', { class: 'b-lp-query__title' }, this.labels.title),
        h('code', { class: 'b-lp-query__src', title: this.source }, this.source.replace(/\n/g, ' · ')),
        count,
        h('span', { class: 'b-lp-query__edit', 'data-lp': 'query-edit', title: this.labels.edit, 'aria-label': this.labels.edit, role: 'button' }, icon('pencil', 'b-lp-icon')),
      ),
    );
    const body = h('div', { class: 'b-lp-query__body' });
    el.appendChild(body);

    if (!ctx) {
      body.appendChild(h('p', { class: 'b-lp-query__note' }, icon('info', 'b-lp-icon'), h('span', {}, this.labels.unavailable)));
      return el;
    }
    let result: QueryView;
    try {
      const index = queryIndex(ctx.workspace.index, ctx.workspace.config, this.version);
      result = evaluateQuery(this.source, index, { now: ctx.now(), locale: ctx.locale, weekStartsOn: ctx.workspace.config.weekStartsOn, path: ctx.path });
    } catch (err) {
      result = { groups: [], total: 0, shown: 0, error: String((err as Error)?.message ?? err) };
    }
    if (result.error) {
      body.appendChild(h('p', { class: 'b-lp-query__note is-error' }, icon('warning', 'b-lp-icon'), h('span', {}, `${this.labels.error} ${result.error}`)));
      return el;
    }
    count.textContent = this.labels.count(result.total);
    if (result.shown === 0) {
      body.appendChild(h('p', { class: 'b-lp-query__note' }, this.labels.empty));
      return el;
    }
    const rc = renderContextFor(ctx, null);
    for (const g of result.groups) {
      if (!g.rows.length) continue;
      if (g.label !== null) body.appendChild(h('div', { class: 'b-lp-query__group' }, g.label));
      const list = h('ul', { class: 'b-lp-query__list' });
      for (const row of g.rows) list.appendChild(this.rowDOM(row, ctx.locale, ctx.now(), (text) => renderInlineSafe(text, { ...rc, path: row.path })));
      body.appendChild(list);
    }
    return el;
  }

  private rowDOM(row: QueryRow, locale: Locale, now: Date, inline: (text: string) => string): HTMLElement {
    const li = h('li', { class: `b-lp-query__row b-lp-query__row--${row.kind}` });
    const page = () => {
      const attrs: Record<string, string> = { class: 'b-lp-query__page', 'data-lp': 'query-open', 'data-path': row.path, title: this.labels.open };
      if (row.kind !== 'page') attrs['data-line'] = String(row.line);
      return h('span', attrs, pageTitle(row.path));
    };
    const text = (src: string) => {
      const span = h('span', { class: 'b-lp-query__text b-lp-rendered' });
      span.innerHTML = inline(src);
      return span;
    };
    switch (row.kind) {
      case 'task': {
        li.classList.add(`is-${row.status}`);
        const box = h(
          'span',
          {
            class: 'b-lp-checkbox',
            role: 'checkbox',
            'aria-checked': row.status === 'done' ? 'true' : row.status === 'doing' ? 'mixed' : 'false',
            'aria-label': this.labels.toggle,
            title: this.labels.toggle,
            'data-lp': this.editable ? 'query-task' : null,
            'data-path': row.path,
            'data-line': row.line,
          },
          row.status === 'done' ? icon('check', 'b-lp-icon b-lp-checkbox__mark') : null,
          row.status === 'cancelled' ? icon('minus', 'b-lp-icon b-lp-checkbox__mark') : null,
          row.status === 'deferred' ? icon('chevronRight', 'b-lp-icon b-lp-checkbox__mark') : null,
        );
        li.append(h('span', { class: `b-lp-task b-lp-task--${row.status}` }, box), text(row.text));
        if (row.due) {
          const overdue = row.status !== 'done' && row.status !== 'cancelled' && row.due.slice(0, 10) < toLocalDate(now);
          li.appendChild(h('span', { class: `b-lp-query__due${overdue ? ' is-overdue' : ''}` }, icon('calendar', 'b-lp-icon'), formatDateHuman(row.due, locale, now)));
        }
        li.appendChild(page());
        return li;
      }
      case 'reminder':
        li.append(
          h('span', { class: `b-lp-query__when${row.alarm ? ' is-alarm' : ''}` }, icon(row.alarm ? 'bell' : 'alarm', 'b-lp-icon'), formatDateHuman(row.at, locale, now)),
          h('span', { class: 'b-lp-query__text' }, plainInline(row.text)),
          page(),
        );
        return li;
      case 'block':
        li.append(h('span', { class: 'b-lp-query__dot', 'aria-hidden': 'true' }), text(row.text), page());
        return li;
      case 'page':
        li.append(
          icon('file', 'b-lp-icon'),
          h('span', { class: 'b-lp-query__pagelink', 'data-lp': 'query-open', 'data-path': row.path, title: this.labels.open }, row.title),
        );
        return li;
    }
  }
}

