/**
 * Pointer handling of the live preview (event delegation on `data-lp` attributes):
 *
 * * rendered links / refs / tags / embed headers open their target (Ctrl/⌘ or middle click: in
 *   the side panel) without moving the cursor — clicking elsewhere edits (Obsidian);
 * * bullets zoom into their block, chevrons fold, checkboxes toggle (right click / long press:
 *   status menu);
 * * chips edit their value: 📅⏳🛫 date picker, ⏰ reminder dialog (right click: snooze / delete
 *   menu), 🔁 recurrence dialog; other chips put the cursor into the token;
 * * table cells move the cursor to the cell's source, the table toolbar edits the table;
 * * query results toggle tasks in their files and open them.
 *
 * Positions are looked up at event time with `view.posAtDOM(widget root)`.
 */
import { foldable, foldedRanges, foldEffect, syntaxTree, unfoldEffect } from '@codemirror/language';
import { ChangeSet, EditorSelection, Prec, type EditorState, type Extension } from '@codemirror/state';
import { EditorView } from '@codemirror/view';
import type { SyntaxNode } from '@lezer/common';
import { addMinutesLocal, addDaysLocal, isLocalDateTime, joinDateTime, toLocalDateTime } from '@/core/dates';
import { formatReminderToken, setTaskStatus, toggleTask } from '@/core/markdown';
import type { LocalDateTime, TaskStatus, VaultPath } from '@/core/types';
import { editorContext, editorDialogs, type EditorContext, type EditorDialogRequest } from '../context';
import { minimalChange } from '../diff';
import { trackRange } from '../tracker';
import { zoomToLine } from '../zoom';
import { blockCandidates, blockPreview, setFrontmatterOpen, toggleCallout, type BlockCandidate } from './blocks';
import { closePreviewMenu, openPreviewMenu, type PreviewMenuItem } from './menu';
import { renderedLinkOf } from './render';
import { nowOf, todayOf, translatorOf } from './state';
import { applyTableAction, tableCellSpans, type TableAction } from './tables';
import { fileDateValue, tokenDateValue, tokenKind, type TokenKind } from './tokens';

/** Actions that open something (allowed with the middle button, Ctrl/⌘ = side panel). */
const LINK_ACTIONS = new Set(['link', 'url', 'tag', 'blockref', 'open', 'open-asset', 'anchor', 'query-open']);

const STATUSES: TaskStatus[] = ['todo', 'doing', 'done', 'cancelled', 'deferred'];

// ---------------------------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------------------------

/** Replace the document with `next` as a minimal change (keeps the selection mapped). */
export function applyContent(view: EditorView, next: string, userEvent: string): boolean {
  const state = view.state;
  const text = state.doc.toString();
  const ch = minimalChange(text, next);
  if (!ch) return false;
  const changes = ChangeSet.of([ch], text.length);
  view.dispatch({ changes, selection: state.selection.map(changes, 1), userEvent });
  return true;
}

/** Toggle the task on a 1-based line (todo/doing → done with ✅, done/cancelled → todo). */
export function toggleTaskAtLine(view: EditorView, lineNo: number): boolean {
  if (view.state.readOnly) return false;
  return applyContent(view, toggleTask(view.state.doc.toString(), lineNo - 1, todayOf(view.state)), 'input.task');
}

export function setTaskStatusAtLine(view: EditorView, lineNo: number, status: TaskStatus): boolean {
  if (view.state.readOnly) return false;
  return applyContent(view, setTaskStatus(view.state.doc.toString(), lineNo - 1, status, todayOf(view.state)), 'input.task');
}

interface TaskWorkspace {
  toggleTask?(path: VaultPath, line: number): Promise<void>;
}

/** Toggle a task in another note (query results): `workspace.toggleTask` or read → edit → write. */
export async function toggleTaskInFile(ctx: EditorContext, path: VaultPath, line0: number, today: string): Promise<void> {
  const ws = ctx.workspace as EditorContext['workspace'] & TaskWorkspace;
  if (typeof ws.toggleTask === 'function') return ws.toggleTask(path, line0);
  const content = await ws.read(path);
  const next = toggleTask(content, line0, today);
  if (next !== content) await ws.write(path, next);
}

// ---------------------------------------------------------------------------------------------
// Positions
// ---------------------------------------------------------------------------------------------

/** Document position of the widget/mark an element belongs to. */
function posOf(view: EditorView, el: Element): number | null {
  const root = el.closest('[data-lp-root]') ?? el;
  try {
    return view.posAtDOM(root, 0);
  } catch {
    return null;
  }
}

/** The innermost node named `name` starting at `pos`. */
function nodeAt(state: EditorState, pos: number, names: readonly string[]): SyntaxNode | null {
  for (const side of [1, -1] as const) {
    for (let n: SyntaxNode | null = syntaxTree(state).resolveInner(pos, side); n; n = n.parent) {
      if (names.includes(n.name) && n.from <= pos && n.to >= pos) return n;
    }
  }
  return null;
}

function candidateAt(state: EditorState, pos: number, kind: BlockCandidate['kind']): BlockCandidate | null {
  return blockCandidates(state).find((c) => c.kind === kind && pos >= c.from && pos <= c.to) ?? null;
}

function placeCursor(view: EditorView, pos: number): void {
  view.dispatch({ selection: EditorSelection.cursor(Math.min(pos, view.state.doc.length)), scrollIntoView: true, userEvent: 'select.pointer' });
  view.focus();
}

// ---------------------------------------------------------------------------------------------
// Opening links
// ---------------------------------------------------------------------------------------------

function headingLine(state: EditorState, heading: string): number | null {
  const wanted = heading.trim().toLowerCase();
  for (let n = 1; n <= state.doc.lines; n++) {
    const m = /^ {0,3}#{1,6}[ \t]+(.*?)[ \t#]*$/.exec(state.doc.line(n).text);
    if (m && m[1]!.trim().toLowerCase() === wanted) return n - 1;
  }
  return null;
}

/** Open a wiki-style target (`Page`, `Page#Heading`, `#Heading`, `#^id`). */
export function openWikiTarget(view: EditorView, target: string, newPane: boolean): boolean {
  const ctx = view.state.facet(editorContext);
  if (!ctx) return false;
  if (target.startsWith('#')) {
    const frag = target.slice(1);
    if (frag.startsWith('^')) ctx.navigate({ view: 'page', path: ctx.path, blockId: frag.slice(1) }, { newPane });
    else {
      const line = headingLine(view.state, frag);
      ctx.navigate(line === null ? { view: 'page', path: ctx.path } : { view: 'page', path: ctx.path, line }, { newPane });
    }
    return true;
  }
  ctx.openLink(target || ctx.path, { newPane });
  return true;
}

function openBlock(ctx: EditorContext, id: string, newPane: boolean): boolean {
  const ref = ctx.workspace.index.blockById(id);
  if (!ref) return false;
  ctx.navigate({ view: 'page', path: ref.path, blockId: id, line: ref.block.line }, { newPane });
  return true;
}

function openUrl(ctx: EditorContext, url: string): boolean {
  void ctx.workspace.platform.openExternal(url).catch(() => {});
  return true;
}

// ---------------------------------------------------------------------------------------------
// Chips
// ---------------------------------------------------------------------------------------------

interface TokenAt {
  kind: TokenKind;
  from: number;
  to: number;
  valueFrom: number;
  valueTo: number;
  value: string;
}

function tokenAt(state: EditorState, pos: number): TokenAt | null {
  const node = nodeAt(state, pos, ['TaskToken']);
  if (!node) return null;
  const m = node.getChild('TaskTokenMark');
  const v = node.getChild('TaskTokenValue');
  const kind = m ? tokenKind(state.sliceDoc(m.from, m.to)) : null;
  if (!kind || !m) return null;
  return {
    kind,
    from: node.from,
    to: node.to,
    valueFrom: v ? v.from : m.to,
    valueTo: v ? v.to : m.to,
    value: v ? state.sliceDoc(v.from, v.to) : '',
  };
}

/** Other tokens of the same kind group on a line (the `🔁` rule of a reminder's block). */
function recurrenceOnLine(state: EditorState, lineFrom: number, lineTo: number): TokenAt | null {
  let found: TokenAt | null = null;
  syntaxTree(state).iterate({
    from: lineFrom,
    to: lineTo,
    enter: (n) => {
      if (found || n.name !== 'TaskToken') return !found;
      const t = tokenAt(state, n.from);
      if (t?.kind === 'recurrence') found = t;
      return false;
    },
  });
  return found;
}

function openDialog(view: EditorView, request: EditorDialogRequest): void {
  view.state.facet(editorDialogs)(request);
}

interface TrackedEdit {
  range(): { from: number; to: number };
  apply(insert: string, extra?: (from: number, to: number) => { from: number; to: number; insert: string }[]): void;
  dispose(): void;
}

/** Replace a range (followed across edits) when a dialog submits. */
function trackedEdit(view: EditorView, from: number, to: number): TrackedEdit {
  const tracked = trackRange(view, from, to);
  return {
    range: () => tracked.range(),
    apply(insert, extra) {
      const r = tracked.range();
      tracked.dispose();
      if (!view.dom.isConnected || view.state.readOnly) return;
      const changes = [{ from: r.from, to: r.to, insert }, ...(extra?.(r.from, r.to) ?? [])];
      view.dispatch({ changes, userEvent: 'input.token' });
    },
    dispose: () => tracked.dispose(),
  };
}

function editDate(view: EditorView, tok: TokenAt): void {
  const t = translatorOf(view.state);
  const initial = tokenDateValue(tok.value) ?? undefined;
  const edit = trackedEdit(view, tok.valueFrom, tok.valueTo);
  view.dispatch({ selection: EditorSelection.cursor(tok.to) });
  openDialog(view, {
    kind: 'date',
    title: t(`preview.date.${tok.kind}`),
    withTime: !!initial && isLocalDateTime(initial),
    initial,
    onSubmit: (v) => edit.apply(fileDateValue(v)),
    onCancel: () => edit.dispose(),
  });
}

function editRecurrence(view: EditorView, tok: TokenAt): void {
  const edit = trackedEdit(view, tok.valueFrom, tok.valueTo);
  view.dispatch({ selection: EditorSelection.cursor(tok.to) });
  openDialog(view, {
    kind: 'recurrence',
    initial: tok.value,
    onSubmit: (v) => edit.apply(tok.valueFrom === tok.valueTo ? ` ${v.rule}` : v.rule),
    onCancel: () => edit.dispose(),
  });
}

function reminderAt(view: EditorView, tok: TokenAt): LocalDateTime | undefined {
  const v = tokenDateValue(tok.value);
  if (!v) return undefined;
  if (isLocalDateTime(v)) return v;
  const ctx = view.state.facet(editorContext);
  const at = joinDateTime(v, ctx?.workspace.config.defaultReminderTime ?? '09:00');
  return isLocalDateTime(at) ? at : undefined;
}

/** Remove a token (`[from, to)`) and the blanks before it. */
function removeToken(view: EditorView, tok: { from: number; to: number }): void {
  if (view.state.readOnly || !view.dom.isConnected) return;
  const line = view.state.doc.lineAt(tok.from);
  let from = tok.from;
  while (from > line.from && /[ \t]/.test(view.state.sliceDoc(from - 1, from))) from--;
  view.dispatch({ changes: { from, to: tok.to }, userEvent: 'delete.token' });
}

function editReminder(view: EditorView, tok: TokenAt): void {
  const state = view.state;
  const line = state.doc.lineAt(tok.from);
  const rec = recurrenceOnLine(state, line.from, line.to);
  const edit = trackedEdit(view, tok.from, tok.to);
  const recTrack = rec ? trackRange(view, rec.valueFrom, rec.valueTo) : null;
  const context = line.text
    .replace(/^[ \t]*(?:[-*+]|\d+[.)])[ \t]+(?:\[.\][ \t]+)?/, '')
    .replace(/(?:📅|⏳|🛫|➕|✅|❌|⏰!?|🔁)️?[^📅⏳🛫➕✅❌⏰🔁]*/gu, '')
    .trim();
  view.dispatch({ selection: EditorSelection.cursor(tok.to) });
  const initial: { at?: LocalDateTime; alarm?: boolean; recurrence?: string } = { alarm: tok.kind === 'alarm' };
  const at = reminderAt(view, tok);
  if (at) initial.at = at;
  if (rec?.value) initial.recurrence = rec.value;
  const request: EditorDialogRequest & { onRemove?: () => void } = {
    kind: 'reminder',
    initial,
    context,
    onSubmit: (v) => {
      edit.apply(formatReminderToken(v.at, v.alarm), (_from, to) => {
        const r = recTrack?.range();
        recTrack?.dispose();
        if (!v.recurrence) return [];
        return r ? [{ from: r.from, to: r.to, insert: v.recurrence }] : [{ from: to, to, insert: ` 🔁 ${v.recurrence}` }];
      });
    },
    onCancel: () => {
      edit.dispose();
      recTrack?.dispose();
    },
    // Forward-compatible: the dialog host shows "Usuń przypomnienie" once it passes this on.
    onRemove: () => {
      const r = edit.range();
      edit.dispose();
      recTrack?.dispose();
      removeToken(view, r);
    },
  };
  openDialog(view, request);
}

/** "HH:mm" → minutes after midnight (9:00 when malformed). */
function timeMinutes(hm: string): number {
  const m = /^(\d{1,2}):(\d{2})$/.exec(hm.trim());
  return m ? Number(m[1]) * 60 + Number(m[2]) : 540;
}

function snooze(view: EditorView, tok: TokenAt, at: LocalDateTime): void {
  if (view.state.readOnly) return;
  view.dispatch({ changes: { from: tok.from, to: tok.to, insert: formatReminderToken(at, tok.kind === 'alarm') }, userEvent: 'input.token' });
}

function reminderMenu(view: EditorView, anchor: Element, tok: TokenAt): void {
  const t = translatorOf(view.state);
  const now = nowOf(view.state);
  const nowLocal = toLocalDateTime(now);
  const ctx = view.state.facet(editorContext);
  const morning = addMinutesLocal(addDaysLocal(nowLocal.slice(0, 10), 1), timeMinutes(ctx?.workspace.config.defaultReminderTime ?? '09:00'));
  const items: PreviewMenuItem[] = [
    { label: t('preview.reminder.edit'), icon: 'pencil', run: () => editReminder(view, tokenAt(view.state, tok.from) ?? tok) },
    { label: t('preview.reminder.snooze10'), icon: 'clock', run: () => snooze(view, tok, addMinutesLocal(nowLocal, 10)) },
    { label: t('preview.reminder.snooze60'), icon: 'clock', run: () => snooze(view, tok, addMinutesLocal(nowLocal, 60)) },
    { label: t('preview.reminder.tomorrow'), icon: 'calendar', run: () => snooze(view, tok, morning) },
    { label: t('preview.reminder.remove'), icon: 'trash', danger: true, run: () => removeToken(view, tok) },
  ];
  openPreviewMenu(anchor, t('preview.reminder.menu'), items);
}

function statusMenu(view: EditorView, anchor: Element, lineNo: number, current: TaskStatus | null): void {
  const t = translatorOf(view.state);
  const items: PreviewMenuItem[] = STATUSES.map((s) => ({
    label: t(`preview.task.status.${s}`),
    checked: s === current,
    run: () => setTaskStatusAtLine(view, lineNo, s),
  }));
  openPreviewMenu(anchor, t('preview.task.menu'), items);
}

function chipClick(view: EditorView, pos: number): boolean {
  const tok = tokenAt(view.state, pos);
  if (!tok) return false;
  if (view.state.readOnly) return true;
  if (tok.kind === 'due' || tok.kind === 'scheduled' || tok.kind === 'start') editDate(view, tok);
  else if (tok.kind === 'reminder' || tok.kind === 'alarm') editReminder(view, tok);
  else if (tok.kind === 'recurrence') editRecurrence(view, tok);
  else placeCursor(view, tok.to);
  return true;
}

// ---------------------------------------------------------------------------------------------
// Tables, queries, frontmatter
// ---------------------------------------------------------------------------------------------

function tableClick(view: EditorView, el: HTMLElement, pos: number, action: string): boolean {
  const c = candidateAt(view.state, pos, 'table');
  if (!c) return true;
  const source = view.state.sliceDoc(c.contentFrom, c.to);
  if (action === 'cell') {
    const r = Number(el.dataset.r);
    const col = Number(el.dataset.c);
    const span = tableCellSpans(source)[r]?.[col];
    placeCursor(view, c.contentFrom + (span ? span.to : 0));
    return true;
  }
  if (view.state.readOnly) return true;
  const map: Record<string, TableAction> = { 'table-add-row': 'addRow', 'table-add-column': 'addColumn', 'table-align': 'align' };
  const res = applyTableAction(source, map[action]!);
  if (!res) return true;
  view.dispatch({
    changes: { from: c.contentFrom, to: c.to, insert: res.text },
    selection: res.cursor !== null ? EditorSelection.cursor(c.contentFrom + res.cursor) : undefined,
    userEvent: 'input.table',
    scrollIntoView: res.cursor !== null,
  });
  if (res.cursor !== null) view.focus();
  return true;
}

function frontmatterEdit(view: EditorView): void {
  const c = blockCandidates(view.state).find((b) => b.kind === 'frontmatter');
  if (!c) return;
  const doc = view.state.doc;
  const second = doc.lines >= 2 ? doc.line(2) : doc.line(1);
  placeCursor(view, Math.min(second.to, c.to));
}

// ---------------------------------------------------------------------------------------------
// Dispatcher
// ---------------------------------------------------------------------------------------------

/** Clickable elements: preview actions and links inside rendered HTML (core render markup). */
const RENDERED_TARGETS = '[data-lp], a[href], .blockref[data-id], .tag[data-tag]';

/** The action of an element: its `data-lp`, or 'anchor' for rendered links / refs / tags. */
function actionOf(el: HTMLElement): string {
  return el.dataset.lp ?? (el.matches('a[href], .blockref[data-id], .tag[data-tag]') ? 'anchor' : '');
}

function target(event: Event, view: EditorView): HTMLElement | null {
  const t = event.target;
  if (!(t instanceof Element)) return null;
  const el = t.closest(RENDERED_TARGETS);
  return el instanceof HTMLElement && view.contentDOM.contains(el) ? el : null;
}

/** Run the action of `el`; true when the event was consumed. */
export function runAction(view: EditorView, el: HTMLElement, opts: { newPane?: boolean } = {}): boolean {
  const state = view.state;
  const ctx = state.facet(editorContext);
  const action = actionOf(el);
  const newPane = !!opts.newPane;
  closePreviewMenu();
  switch (action) {
    case 'link':
    case 'open':
      return openWikiTarget(view, el.dataset.target ?? '', newPane);
    case 'url':
      return ctx ? openUrl(ctx, el.dataset.href ?? '') : false;
    case 'open-asset':
      return ctx ? openUrl(ctx, el.dataset.src ?? '') : false;
    case 'tag':
      if (!ctx) return false;
      ctx.navigate({ view: 'tags', tag: el.dataset.tag ?? '' }, { newPane });
      return true;
    case 'blockref':
      return ctx ? openBlock(ctx, el.dataset.id ?? '', newPane) : false;
    case 'anchor': {
      const link = renderedLinkOf(el);
      if (!link || !ctx) return true;
      if (link.kind === 'page') return openWikiTarget(view, link.target, newPane);
      if (link.kind === 'block') return openBlock(ctx, link.id, newPane) || true;
      if (link.kind === 'tag') {
        ctx.navigate({ view: 'tags', tag: link.tag }, { newPane });
        return true;
      }
      return openUrl(ctx, link.url);
    }
    case 'query-open': {
      if (!ctx) return false;
      const path = el.dataset.path ?? '';
      const line = el.dataset.line !== undefined ? Number(el.dataset.line) : undefined;
      ctx.navigate(line === undefined ? { view: 'page', path } : { view: 'page', path, line }, { newPane });
      return true;
    }
  }

  const pos = posOf(view, el);
  if (pos === null) return false;
  const lineNo = state.doc.lineAt(pos).number;
  switch (action) {
    case 'bullet': {
      const l = state.doc.line(lineNo);
      view.dispatch({ effects: zoomToLine(state, lineNo - 1), selection: EditorSelection.cursor(l.to), scrollIntoView: true });
      return true;
    }
    case 'fold': {
      const l = state.doc.line(lineNo);
      let folded: { from: number; to: number } | null = null;
      foldedRanges(state).between(l.to, l.to, (from, to) => {
        if (from === l.to) folded = { from, to };
      });
      if (folded) view.dispatch({ effects: unfoldEffect.of(folded) });
      else {
        const range = foldable(state, l.from, l.to);
        if (range) view.dispatch({ effects: foldEffect.of(range) });
      }
      return true;
    }
    case 'checkbox':
      toggleTaskAtLine(view, lineNo);
      return true;
    case 'checkbox-menu':
      if (!state.readOnly) statusMenu(view, el, lineNo, taskStatusOf(el));
      return true;
    case 'chip':
      return chipClick(view, pos);
    case 'priority':
    case 'calc': {
      const node = nodeAt(state, pos, action === 'calc' ? ['InlineCode'] : ['PriorityToken']);
      placeCursor(view, node ? (action === 'calc' ? node.to - 1 : node.to) : pos);
      return true;
    }
    case 'embed': {
      const node = nodeAt(state, pos, ['Embed', 'BlockEmbed', 'Image']);
      placeCursor(view, node ? node.to : pos);
      return true;
    }
    case 'cell':
    case 'table-add-row':
    case 'table-add-column':
    case 'table-align':
      return tableClick(view, el, pos, action);
    case 'table':
    case 'query':
    case 'frontmatter':
      return true;
    case 'query-edit': {
      const c = candidateAt(state, pos, 'query');
      if (c) placeCursor(view, state.doc.lineAt(c.from).to);
      return true;
    }
    case 'query-task': {
      if (!ctx || state.readOnly) return true;
      const path = el.dataset.path ?? '';
      const line0 = Number(el.dataset.line);
      if (path === ctx.path) toggleTaskAtLine(view, line0 + 1);
      else void toggleTaskInFile(ctx, path, line0, todayOf(state)).catch(() => {});
      return true;
    }
    case 'fm-toggle':
      view.dispatch({ effects: setFrontmatterOpen.of(!state.field(blockPreview).frontmatterOpen) });
      return true;
    case 'fm-edit':
      frontmatterEdit(view);
      return true;
    case 'callout-fold':
      view.dispatch({ effects: toggleCallout.of(state.doc.lineAt(pos).from) });
      return true;
    default:
      return false;
  }
}

function taskStatusOf(el: Element): TaskStatus | null {
  const m = /b-lp-task--(\w+)/.exec(el.closest('.b-lp-task')?.className ?? '');
  return (m?.[1] as TaskStatus | undefined) ?? null;
}

let lastHandled = -1;

const handlers = EditorView.domEventHandlers({
  mousedown(event, view) {
    if (event.button !== 0 && event.button !== 1) return false;
    if (event.shiftKey || event.altKey) return false;
    const el = target(event, view);
    if (!el) return false;
    const action = actionOf(el);
    if (event.button === 1 && !LINK_ACTIONS.has(action)) return false;
    const handled = runAction(view, el, { newPane: event.button === 1 || event.ctrlKey || event.metaKey });
    if (handled) {
      event.preventDefault();
      lastHandled = event.timeStamp;
    }
    return handled;
  },
  click(event, view) {
    const el = target(event, view);
    // Never let rendered anchors navigate the page.
    if (el?.tagName === 'A' || (event.target instanceof Element && event.target.closest('.cm-content a[href]'))) event.preventDefault();
    if (lastHandled >= 0 && event.timeStamp - lastHandled < 1000) {
      lastHandled = -1;
      return true;
    }
    return false;
  },
  auxclick(event, view) {
    const el = target(event, view);
    if (el && event.button === 1) {
      event.preventDefault();
      return true;
    }
    return false;
  },
  contextmenu(event, view) {
    const el = target(event, view);
    if (!el || view.state.readOnly) return false;
    const action = el.dataset.lp;
    const pos = posOf(view, el);
    if (pos === null) return false;
    if (action === 'checkbox' || action === 'checkbox-menu') {
      event.preventDefault();
      statusMenu(view, el, view.state.doc.lineAt(pos).number, taskStatusOf(el));
      return true;
    }
    if (action === 'chip' && (el.dataset.kind === 'reminder' || el.dataset.kind === 'alarm')) {
      const tok = tokenAt(view.state, pos);
      if (!tok) return false;
      event.preventDefault();
      reminderMenu(view, el, tok);
      return true;
    }
    return false;
  },
});

export function previewHandlers(): Extension {
  return Prec.high(handlers);
}
