/**
 * Metadata tokens (SYNTAX §5-6) render as chips instead of raw emoji + ISO dates:
 *
 * * `📅 ⏳ 🛫 ➕ ✅ ❌ <date>` → `<span class="chip chip-due" title="2026-10-15">📅 jutro</span>`
 *   (human date via `formatDateHuman`, `ctx.locale`, `ctx.now`); a due date before today on a
 *   block that is not a done / cancelled task adds `chip-overdue`, today adds `chip-today`;
 * * `⏰ <date> [time]` / `⏰! …` (alarm) / `(@date time)` → `chip-reminder` / `chip-alarm`;
 * * `🔁 rule` → `chip-recurrence` (rule localised when it parses);
 * * Logseq `SCHEDULED: <…>` / `DEADLINE: <…>` → scheduled / due chips;
 * * priority emoji stay as written, wrapped in `<span class="priority priority-high">`; Logseq
 *   `[#A]` shows as a priority badge; Obsidian Tasks `🆔 ⛔ 🏁` become small meta chips.
 *
 * Tokens are only recognised in plain text (never in code, links or URLs).
 */
import type { MarkdownIt, RendererRule, StateCore, Token } from 'markdown-it';
import { TOKENS, findTokens, type Token as MetaToken } from '@/core/markdown';
import { formatDateHuman, formatRecurrence, parseRecurrence } from '@/core/dates';
import type { Priority, TaskStatus } from '../../types';
import { classNames, escapeHtml, renderAttrs } from '../escape';
import { getEnv, tr } from '../env';
import type { RenderEnv } from '../types';
import type { TaskInfo } from './tasks';

/** Meta of `chip` tokens. */
export type ChipMeta = { token: MetaToken; raw: string; status?: TaskStatus };

const LOGSEQ_LETTER: Readonly<Record<Priority, string>> = { highest: 'A', high: 'A', medium: 'B', low: 'C', lowest: 'C' };

/** Split text tokens of `children` at metadata tokens. */
function splitChildren(state: StateCore, children: Token[], status: TaskStatus | undefined): Token[] {
  const out: Token[] = [];
  let linkDepth = 0;
  for (const child of children) {
    if (child.type === 'link_open') linkDepth++;
    else if (child.type === 'link_close') linkDepth = Math.max(0, linkDepth - 1);
    if (child.type !== 'text' || linkDepth > 0) {
      out.push(child);
      continue;
    }
    const found = findTokens(child.content);
    if (found.length === 0) {
      out.push(child);
      continue;
    }
    let last = 0;
    for (const t of found) {
      if (t.from > last) out.push(textToken(state, child.content.slice(last, t.from)));
      const chip = new state.Token('chip', 'span', 0);
      chip.meta = { token: t, raw: child.content.slice(t.from, t.to), status } satisfies ChipMeta;
      chip.content = chip.meta.raw as string;
      out.push(chip);
      last = t.to;
    }
    if (last < child.content.length) out.push(textToken(state, child.content.slice(last)));
  }
  return out;
}

function textToken(state: StateCore, content: string): Token {
  const t = new state.Token('text', '', 0);
  t.content = content;
  return t;
}

/** The `chips` core rule (runs after `text_join`, so text tokens are merged). */
export function chipsRule(state: StateCore): void {
  const statusStack: (TaskStatus | undefined)[] = [];
  for (const tok of state.tokens) {
    if (tok.type === 'list_item_open') statusStack.push((tok.meta?.task as TaskInfo | undefined)?.status);
    else if (tok.type === 'list_item_close') statusStack.pop();
    else if (tok.type === 'inline' && tok.children && tok.children.length > 0) {
      tok.children = splitChildren(state, tok.children, statusStack[statusStack.length - 1]);
    }
  }
}

/** ISO local date / date-time of a dated token (`2026-10-15` or `2026-10-15T09:00`). */
function isoOf(t: MetaToken): string {
  return t.time ? `${t.value}T${t.time}` : (t.value ?? '');
}

function dateChip(env: RenderEnv, field: 'due' | 'scheduled' | 'start' | 'created' | 'done' | 'cancelled', t: MetaToken, status: TaskStatus | undefined, extra: Record<string, string | undefined> = {}): string {
  const iso = isoOf(t);
  const date = t.value ?? '';
  const open = status !== 'done' && status !== 'cancelled';
  const overdue = field === 'due' && open && date !== '' && date < env.today;
  const today = (field === 'due' || field === 'scheduled' || field === 'start') && date === env.today;
  const label = formatDateHuman(iso, env.locale, env.now);
  const attrs = renderAttrs({
    class: classNames('chip', `chip-${field}`, overdue && 'chip-overdue', today && 'chip-today'),
    title: iso,
    'data-date': iso,
    'data-field': field,
    'aria-label': `${tr(env, `render.chip.${field}`)}${overdue ? ` (${tr(env, 'render.chip.overdue')})` : ''}: ${label}`,
    ...extra,
  });
  return `<span${attrs}>${TOKENS[field]} ${escapeHtml(label)}</span>`;
}

/** HTML of a chip for metadata token `meta.token`. */
export function renderChip(env: RenderEnv, meta: ChipMeta): string {
  const t = meta.token;
  switch (t.kind) {
    case 'due':
    case 'scheduled':
    case 'start':
    case 'created':
    case 'done':
    case 'cancelled':
      return dateChip(env, t.kind, t, meta.status);
    case 'planning':
      return dateChip(env, t.field ?? 'scheduled', t, meta.status, { 'data-repeater': t.repeater });
    case 'reminder':
    case 'reminderCompat': {
      const iso = isoOf(t);
      const alarm = t.alarm === true;
      const label = formatDateHuman(iso, env.locale, env.now);
      const attrs = renderAttrs({
        class: classNames('chip', alarm ? 'chip-alarm' : 'chip-reminder'),
        title: iso,
        'data-date': iso,
        'data-field': alarm ? 'alarm' : 'reminder',
        'aria-label': `${tr(env, alarm ? 'render.chip.alarm' : 'render.chip.reminder')}: ${label}`,
      });
      return `<span${attrs}>${alarm ? TOKENS.alarm : TOKENS.reminder} ${escapeHtml(label)}</span>`;
    }
    case 'recurrence': {
      const rule = t.canonical ? parseRecurrence(t.canonical) : null;
      const label = rule ? formatRecurrence(rule, env.locale) : (t.value ?? '');
      const attrs = renderAttrs({
        class: 'chip chip-recurrence',
        title: t.canonical ?? t.value ?? '',
        'data-field': 'recurrence',
        'aria-label': `${tr(env, 'render.chip.recurrence')}: ${label}`,
      });
      return `<span${attrs}>${TOKENS.recurrence} ${escapeHtml(label)}</span>`;
    }
    case 'priority': {
      const p = t.priority ?? 'medium';
      return `<span${renderAttrs({ class: `priority priority-${p}`, 'data-priority': p, title: tr(env, `render.priority.${p}`) })}>${escapeHtml(meta.raw.trim())}</span>`;
    }
    case 'logseqPriority': {
      const p = t.priority ?? 'medium';
      const letter = /\[#([ABC])\]/.exec(meta.raw)?.[1] ?? LOGSEQ_LETTER[p];
      return `<span${renderAttrs({ class: `priority priority-logseq priority-${p}`, 'data-priority': p, title: tr(env, `render.priority.${p}`) })}>${escapeHtml(letter)}</span>`;
    }
    case 'taskId':
    case 'dependsOn':
    case 'onCompletion':
      return `<span${renderAttrs({ class: `chip chip-meta chip-${t.kind}`, title: t.value })}>${escapeHtml(meta.raw.trim())}</span>`;
  }
  return escapeHtml(meta.raw);
}

/** Install chips. */
export function chipsPlugin(md: MarkdownIt): void {
  md.core.ruler.push('chips', chipsRule);
  const render: RendererRule = (tokens, idx, _options, mdEnv) => {
    const token = tokens[idx]!;
    const env = getEnv(mdEnv);
    const meta = token.meta as ChipMeta | null;
    if (!env || !meta) return escapeHtml(token.content);
    return renderChip(env, meta);
  };
  md.renderer.rules.chip = render;
}
