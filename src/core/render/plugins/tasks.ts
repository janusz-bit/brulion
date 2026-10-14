/**
 * Task list items (SYNTAX §5): `- [ ]`, `- [x]`, `- [/]`, `- [-]`, `- [>]` and Logseq keywords
 * (`TODO`, `DOING`, `DONE`, `LATER`, `NOW`, `WAITING`, `CANCELED`...).
 *
 * The list item gets `class="task-list-item task-<status>" data-task="<status>"`, its list
 * `contains-task-list`, and the item text starts with
 * `<input type="checkbox" class="task-checkbox" data-line="<source line>" data-status>` —
 * disabled unless `ctx.interactive` — followed by the text wrapped in `<span class="task-text">`.
 * Logseq keywords other than TODO/DONE stay visible as `<span class="task-keyword">`.
 */
import type { MarkdownIt, RendererRule, StateCore, Token } from 'markdown-it';
import { statusFromCheckbox, statusFromKeyword } from '@/core/markdown';
import type { TaskStatus } from '../../types';
import { escapeHtml, renderAttrs } from '../escape';
import { dataLine, getEnv, tr } from '../env';

/** Task information stored on `list_item_open` and its first `inline` token. */
export type TaskInfo = {
  status: TaskStatus;
  /** Checkbox character or Logseq keyword as written. */
  marker: string;
  kind: 'checkbox' | 'keyword';
  /** 0-based source line of the list item. */
  line: number;
};

const CHECKBOX_RE = /^\[([^\]\r\n])\](?=[ \t]|$)/;
const KEYWORD_RE = /^(TODO|LATER|DOING|NOW|IN-PROGRESS|DONE|CANCELED|CANCELLED|WAITING|WAIT)(?=[ \t]|$)/;
/** Keywords shown next to the checkbox (TODO / DONE are fully expressed by the checkbox). */
const HIDDEN_KEYWORDS = new Set(['TODO', 'DONE']);

/** Detect a task marker at the start of a list item's text: info and the text after it. */
export function parseTaskMarker(content: string, line: number): { task: TaskInfo; rest: string } | null {
  const cb = CHECKBOX_RE.exec(content);
  if (cb) {
    const marker = cb[1]!;
    return { task: { status: statusFromCheckbox(marker), marker, kind: 'checkbox', line }, rest: content.slice(3).replace(/^[ \t]+/, '') };
  }
  const kw = KEYWORD_RE.exec(content);
  if (kw) {
    const marker = kw[1]!;
    return { task: { status: statusFromKeyword(marker), marker, kind: 'keyword', line }, rest: content.slice(marker.length).replace(/^[ \t]+/, '') };
  }
  return null;
}

/** Index of the list (`bullet_list_open` / `ordered_list_open`) containing the item at `itemIdx`. */
function parentListIndex(tokens: readonly Token[], itemIdx: number): number {
  const level = tokens[itemIdx]!.level - 1;
  for (let k = itemIdx - 1; k >= 0; k--) {
    const t = tokens[k]!;
    if (t.level === level && (t.type === 'bullet_list_open' || t.type === 'ordered_list_open')) return k;
  }
  return -1;
}

/** The `tasks` core rule (before inline parsing): detect markers and strip them from the text. */
export function tasksRule(state: StateCore): void {
  if (state.inlineMode) return;
  const tokens = state.tokens;
  for (let i = 0; i < tokens.length; i++) {
    const item = tokens[i]!;
    if (item.type !== 'list_item_open') continue;
    const p = tokens[i + 1];
    const inline = tokens[i + 2];
    if (p?.type !== 'paragraph_open' || inline?.type !== 'inline') continue;
    const found = parseTaskMarker(inline.content, item.map?.[0] ?? p.map?.[0] ?? 0);
    if (!found) continue;
    inline.content = found.rest;
    inline.meta = { ...(inline.meta ?? {}), task: found.task };
    item.meta = { ...(item.meta ?? {}), task: found.task };
    item.attrJoin('class', `task-list-item task-${found.task.status}`);
    item.attrSet('data-task', found.task.status);
    const list = parentListIndex(tokens, i);
    const listToken = list >= 0 ? tokens[list]! : null;
    if (listToken && !String(listToken.attrGet('class') ?? '').includes('contains-task-list')) listToken.attrJoin('class', 'contains-task-list');
  }
}

/** The `task_checkboxes` core rule (after inline parsing): prepend checkbox and keyword tokens. */
export function taskCheckboxesRule(state: StateCore): void {
  if (state.inlineMode) return;
  for (const tok of state.tokens) {
    const task = tok.type === 'inline' ? (tok.meta?.task as TaskInfo | undefined) : undefined;
    if (!task || !tok.children || tok.meta?.taskRendered) continue;
    tok.meta = { ...tok.meta, taskRendered: true };
    const checkbox = new state.Token('task_checkbox', 'input', 0);
    checkbox.meta = { task };
    const prefix: Token[] = [checkbox];
    if (task.kind === 'keyword' && !HIDDEN_KEYWORDS.has(task.marker)) {
      const kw = new state.Token('task_keyword', 'span', 0);
      kw.meta = { task };
      kw.content = task.marker;
      prefix.push(kw);
    }
    prefix.push(new state.Token('task_text_open', 'span', 1));
    tok.children = [...prefix, ...tok.children, new state.Token('task_text_close', 'span', -1)];
  }
}

/** Install task list rendering. */
export function tasksPlugin(md: MarkdownIt): void {
  md.core.ruler.before('inline', 'tasks', tasksRule);
  // After block ids (which look at the last text of the item) and text_join.
  md.core.ruler.push('task_checkboxes', taskCheckboxesRule);
  const checkbox: RendererRule = (tokens, idx, _options, mdEnv) => {
    const task = tokens[idx]!.meta?.task as TaskInfo | undefined;
    const env = getEnv(mdEnv);
    if (!task) return '';
    return `<input${renderAttrs({
      type: 'checkbox',
      class: 'task-checkbox',
      checked: task.status === 'done',
      disabled: !env?.ctx.interactive,
      'data-line': env ? dataLine(env, task.line) : task.line,
      'data-status': task.status,
      'data-marker': task.marker,
      'aria-label': env ? tr(env, `render.task.${task.status}`) : undefined,
    })}> `;
  };
  const keyword: RendererRule = (tokens, idx) => {
    const task = tokens[idx]!.meta?.task as TaskInfo | undefined;
    if (!task) return '';
    return `<span${renderAttrs({ class: `task-keyword task-keyword-${task.status}`, 'data-keyword': task.marker })}>${escapeHtml(task.marker)}</span> `;
  };
  md.renderer.rules.task_checkbox = checkbox;
  md.renderer.rules.task_keyword = keyword;
  md.renderer.rules.task_text_open = () => '<span class="task-text">';
  md.renderer.rules.task_text_close = () => '</span>';
}
