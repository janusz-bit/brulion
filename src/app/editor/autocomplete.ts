/**
 * Completion sources: `[[` pages (aliases, headings after `#`, blocks after `#^`, "create page"),
 * `((` block references (ids are created on accept), `#` tags, natural-language dates after
 * `📅 ⏳ 🛫 ⏰` or `@`, and the slash menu (slash.ts). One `autocompletion()` instance with all of
 * them is returned by {@link brulionAutocomplete}.
 */
import {
  autocompletion,
  type Completion,
  type CompletionContext,
  type CompletionResult,
  type CompletionSource,
} from '@codemirror/autocomplete';
import type { EditorState, Extension } from '@codemirror/state';
import type { EditorView } from '@codemirror/view';
import { foldText, formatDateHuman, parseNaturalDate } from '@/core/dates';
import { createTranslator, type Translator } from '@/core/i18n';
import { ensureBlockId, journalPathForDate, parseNote } from '@/core/markdown';
import { dirname, pageTitle, stripMdExtension } from '@/core/paths';
import type { Block, BlockRef, LocalDate, Locale, ParsedNote, VaultPath } from '@/core/types';
import { editorContext, editorNow, type EditorContext } from './context';
import { minimalChange } from './diff';
import { inNoCompleteContext } from './outliner';
import { slashCompletionSource } from './slash';
import { tagBodyLength } from './syntax';
import { trackRange } from './tracker';

const translators = new Map<Locale, Translator>();
function tFor(state: EditorState): Translator {
  const locale = state.facet(editorContext)?.locale ?? 'pl';
  let t = translators.get(locale);
  if (!t) translators.set(locale, (t = createTranslator(locale)));
  return t;
}

export { inNoCompleteContext };

function lineBefore(context: CompletionContext): { text: string; lineFrom: number } {
  const line = context.state.doc.lineAt(context.pos);
  return { text: line.text.slice(0, context.pos - line.from), lineFrom: line.from };
}

/** Extend `to` over closing characters that auto-pairing already inserted (`]]`, `))`). */
function closingEnd(state: EditorState, to: number, close: string): { to: number } {
  const after = state.sliceDoc(to, to + close.length);
  if (after === close) return { to: to + close.length };
  // `[[x]` + `]` typed partially: take what is there.
  if (close.length === 2 && after[0] === close[0]) return { to: to + 1 };
  return { to };
}

/**
 * When the cursor is inside an existing `[[target]]` / `((query))` (editing it), the rest of
 * the target after the cursor up to `]]`/`|` (or `))`) is replaced too, so accepting a
 * completion does not leave the old tail behind (`[[Do|m]]` → `[[Dom]]`, not `[[Dom]]m]]`).
 */
function existingTargetEnd(state: EditorState, pos: number, kind: 'wiki' | 'ref'): number {
  const line = state.doc.lineAt(pos);
  const after = state.sliceDoc(pos, line.to);
  const m = kind === 'wiki' ? /^[^[\]|\n]*(?=\]\]|\|)/.exec(after) : /^[^()\n]*(?=\)\))/.exec(after);
  return m ? pos + m[0].length : pos;
}

function truncate(s: string, n = 80): string {
  const one = s.replace(/\s+/g, ' ').trim();
  return one.length > n ? `${one.slice(0, n - 1)}…` : one;
}

// ---------------------------------------------------------------------------------------------
// [[ wiki links
// ---------------------------------------------------------------------------------------------

export interface WikiQuery {
  embed: boolean;
  kind: 'page' | 'heading' | 'block';
  /** Page part before `#` (empty = current page) for headings/blocks. */
  page: string;
  query: string;
  /** Offset of the query in the text before the cursor. */
  offset: number;
}

/** What is being typed inside an unfinished `[[…` before the cursor. */
export function wikiQueryAt(before: string): WikiQuery | null {
  const m = /(!?)\[\[([^[\]]*)$/.exec(before);
  if (!m) return null;
  const inner = m[2]!;
  if (inner.includes('|')) return null;
  const innerStart = before.length - inner.length;
  const hash = inner.indexOf('#');
  if (hash < 0) return { embed: m[1] === '!', kind: 'page', page: '', query: inner, offset: innerStart };
  const page = inner.slice(0, hash);
  const rest = inner.slice(hash + 1);
  if (rest.startsWith('^')) return { embed: m[1] === '!', kind: 'block', page, query: rest.slice(1), offset: innerStart + hash + 2 };
  return { embed: m[1] === '!', kind: 'heading', page, query: rest, offset: innerStart + hash + 1 };
}

/** Link target for a page: its title, or the full path when the title alone is ambiguous. */
export function linkTargetFor(ctx: EditorContext | null, path: VaultPath, title: string): string {
  if (!ctx) return title;
  const resolved = ctx.workspace.index.resolveLink(title, ctx.path);
  return resolved === null || resolved === path ? title : stripMdExtension(path);
}

function noteFor(state: EditorState, ctx: EditorContext | null, page: string): { note: ParsedNote | null; path: VaultPath | null } {
  if (!ctx) return { note: parseNote('', state.doc.toString()), path: null };
  if (page.trim() === '') return { note: parseNote(ctx.path, state.doc.toString(), ctx.workspace.config), path: ctx.path };
  const path = ctx.workspace.index.resolveLink(page.trim(), ctx.path);
  if (!path) return { note: null, path: null };
  if (path === ctx.path) return { note: parseNote(ctx.path, state.doc.toString(), ctx.workspace.config), path };
  return { note: ctx.workspace.index.note(path) ?? null, path };
}

function matches(text: string, q: string): boolean {
  return !q || foldText(text).includes(foldText(q));
}

export const wikiLinkSource: CompletionSource = (context) => {
  const { state, pos } = context;
  const { text, lineFrom } = lineBefore(context);
  const q = wikiQueryAt(text);
  if (!q || inNoCompleteContext(state, pos)) return null;
  const ctx = state.facet(editorContext);
  const t = tFor(state);
  const from = lineFrom + q.offset;
  const end = existingTargetEnd(state, pos, 'wiki');
  // Positions are taken when an option is applied (the result may have been mapped since).
  const insert = (target: string): Completion['apply'] => (view, _c, f, t) => {
    if (view.state.sliceDoc(t, t + 1) === '|') {
      // Editing the target of `[[target|alias]]`: the alias stays.
      const page = target.split('|')[0]!;
      view.dispatch({ changes: { from: f, to: t, insert: page }, selection: { anchor: f + page.length }, userEvent: 'input.complete' });
      return;
    }
    const { to } = closingEnd(view.state, t, ']]');
    view.dispatch({
      changes: { from: f, to, insert: `${target}]]` },
      selection: { anchor: f + target.length + 2 },
      userEvent: 'input.complete',
      scrollIntoView: true,
    });
  };

  if (q.kind === 'page') {
    if (!ctx) return null;
    const suggestions = ctx.workspace.index.suggestPages(q.query, 30);
    const options: Completion[] = suggestions.map((s, i) => {
      const target = linkTargetFor(ctx, s.path, s.title);
      const folder = dirname(s.path);
      return s.alias
        ? {
            label: s.alias,
            detail: t('editor.complete.alias', { title: s.title }),
            type: 'alias',
            boost: -i,
            apply: insert(`${target}|${s.alias}`),
          }
        : { label: s.title, detail: folder || undefined, type: 'page', boost: -i, apply: insert(target) };
    });
    const typed = q.query.trim();
    const exact = suggestions.some((s) => foldText(s.title) === foldText(typed) || (s.alias && foldText(s.alias) === foldText(typed)));
    if (typed && !exact && !q.embed) {
      options.push({
        label: typed,
        displayLabel: t('editor.complete.create', { name: typed }),
        detail: t('editor.complete.newPage'),
        type: 'create',
        boost: -1000,
        apply: insert(typed),
      });
    }
    return options.length ? { from, to: end, options, filter: false } : null;
  }

  const { note, path } = noteFor(state, ctx, q.page);
  if (!note) return null;
  if (q.kind === 'heading') {
    const options = note.headings
      .filter((h) => matches(h.text, q.query))
      .slice(0, 50)
      .map((h, i): Completion => ({ label: h.text, detail: '#'.repeat(h.level), type: 'heading', boost: -i, apply: insert(h.text) }));
    return options.length ? { from, to: end, options, filter: false } : null;
  }
  const blocks = note.allBlocks.filter((b) => b.kind !== 'frontmatter' && b.text.trim() !== '' && matches(b.text, q.query)).slice(0, 50);
  const options = blocks.map(
    (b, i): Completion => ({
      label: truncate(b.text),
      detail: b.id ? `^${b.id}` : t('editor.complete.block'),
      type: 'block',
      boost: -i,
      apply: (view, _c, f, t) =>
        void insertBlockId(view, { path: path ?? ctx?.path ?? '', block: b }, f, closingEnd(view.state, t, ']]').to, (id) => `${id}]]`),
    }),
  );
  return options.length ? { from, to: end, options, filter: false } : null;
};

// ---------------------------------------------------------------------------------------------
// (( block references
// ---------------------------------------------------------------------------------------------

/** Query typed after an unfinished `((` (or `!((`), with its offset. */
export function blockRefQueryAt(before: string): { query: string; offset: number } | null {
  const m = /!?\(\(([^()]*)$/.exec(before);
  if (!m) return null;
  return { query: m[1]!, offset: before.length - m[1]!.length };
}

/**
 * Insert a reference to `ref`'s block at [from, to): uses the existing id, or creates one —
 * in the current document directly (same transaction), elsewhere via workspace.ensureBlockId.
 */
export async function insertBlockId(
  view: EditorView,
  ref: BlockRef,
  from: number,
  to: number,
  format: (id: string) => string,
): Promise<void> {
  const ctx = view.state.facet(editorContext);
  const put = (id: string, f: number, tt: number, extra?: { from: number; to: number; insert: string } | null) => {
    const text = format(id);
    const changes = extra ? [extra, { from: f, to: tt, insert: text }] : [{ from: f, to: tt, insert: text }];
    const set = view.state.changes(changes);
    view.dispatch({ changes: set, selection: { anchor: set.mapPos(f + text.length, 1) }, userEvent: 'input.complete', scrollIntoView: true });
  };
  if (ref.block.id) return put(ref.block.id, from, to);
  if (!ctx || ref.path === ctx.path || ref.path === '') {
    const doc = view.state.doc.toString();
    const res = ensureBlockId(doc, ref.block.line);
    if (!res.id) return;
    const ch = minimalChange(doc, res.content);
    const overlaps = ch && ch.from < to && ch.to > from;
    return put(res.id, from, to, overlaps ? null : ch);
  }
  const tracked = trackRange(view, from, to);
  try {
    const id = await ctx.workspace.ensureBlockId(ref.path, ref.block.line);
    const r = tracked.range();
    if (id) put(id, r.from, r.to);
  } catch (e) {
    console.warn(tFor(view.state)('editor.complete.blockIdError', { error: String(e) }));
  } finally {
    tracked.dispose();
  }
}

/**
 * The index knows the current note as last saved; with unsaved edits its line numbers may be
 * off. Blocks of the current note are looked up again in the editor text (same text, nearest
 * line), so a new id lands on the block that was picked; blocks no longer there are dropped.
 */
export function withCurrentLines(state: EditorState, ctx: EditorContext, refs: BlockRef[]): BlockRef[] {
  if (!refs.some((r) => r.path === ctx.path)) return refs;
  const fresh = parseNote(ctx.path, state.doc.toString(), ctx.workspace.config).allBlocks;
  const out: BlockRef[] = [];
  for (const ref of refs) {
    if (ref.path !== ctx.path) {
      out.push(ref);
      continue;
    }
    let best: Block | null = null;
    for (const b of fresh) {
      if (b.text !== ref.block.text || b.kind !== ref.block.kind) continue;
      if (!best || Math.abs(b.line - ref.block.line) < Math.abs(best.line - ref.block.line)) best = b;
    }
    if (best) out.push({ path: ref.path, block: best });
  }
  return out;
}

export const blockRefSource: CompletionSource = (context) => {
  const { state, pos } = context;
  const { text, lineFrom } = lineBefore(context);
  const q = blockRefQueryAt(text);
  const ctx = state.facet(editorContext);
  if (!q || !ctx || inNoCompleteContext(state, pos)) return null;
  if (q.query.trim() === '' && !context.explicit && text.length > 2 && /\S/.test(text.slice(-3, -2))) {
    // "foo((" right after a word: probably a normal parenthesis, wait for more text.
    return null;
  }
  const refs = withCurrentLines(state, ctx, ctx.workspace.index.suggestBlocks(q.query, 30));
  const options = refs.map(
    (ref, i): Completion => ({
      label: truncate(ref.block.text),
      detail: pageTitle(ref.path),
      type: 'block',
      boost: -i,
      apply: (view, _c, f, t) => void insertBlockId(view, ref, f, closingEnd(view.state, t, '))').to, (id) => `${id}))`),
    }),
  );
  return options.length ? { from: lineFrom + q.offset, to: existingTargetEnd(state, pos, 'ref'), options, filter: false } : null;
};

// ---------------------------------------------------------------------------------------------
// #tags
// ---------------------------------------------------------------------------------------------

/** Tag prefix typed after `#` (SYNTAX §3 rules for the preceding character). */
export function tagQueryAt(before: string): { query: string; offset: number } | null {
  const m = /(?:^|[\s(])#([\p{L}\p{N}\p{M}_\-/]*)$/u.exec(before);
  if (!m) return null;
  const query = m[1]!;
  if (query && tagBodyLength(query) === 0 && !/^\d+$/.test(query)) return null;
  return { query, offset: before.length - query.length };
}

export const tagSource: CompletionSource = (context) => {
  const { state, pos } = context;
  const { text, lineFrom } = lineBefore(context);
  const q = tagQueryAt(text);
  const ctx = state.facet(editorContext);
  if (!q || !ctx || inNoCompleteContext(state, pos)) return null;
  if (!q.query && !context.explicit) return null;
  const t = tFor(state);
  const fq = foldText(q.query);
  // The index may keep tags lower-cased: complete with the spelling as written (#Praca).
  const tags = ctx.workspace.index
    .tags()
    .map((x) => ({ name: x.display || x.tag, count: x.count }))
    .filter((x) => foldText(x.name).includes(fq) && x.name !== q.query)
    .sort((a, b) => Number(foldText(b.name).startsWith(fq)) - Number(foldText(a.name).startsWith(fq)) || b.count - a.count)
    .slice(0, 40);
  const options = tags.map(
    (x, i): Completion => ({
      label: x.name,
      detail: t('editor.complete.tagCount', { count: x.count }),
      type: 'tag',
      boost: -i,
      apply: /[\s[\]]/.test(x.name) ? `[[${x.name}]]` : x.name,
    }),
  );
  // Editing inside an existing tag: replace its rest too (`#re|mont` + "remont" → `#remont`).
  const rest = /^[\p{L}\p{N}\p{M}_\-/]*/u.exec(state.sliceDoc(pos, state.doc.lineAt(pos).to))![0].length;
  return options.length ? { from: lineFrom + q.offset, to: pos + rest, options, filter: false } : null;
};

// ---------------------------------------------------------------------------------------------
// Natural-language dates
// ---------------------------------------------------------------------------------------------

export interface DateOption {
  label: string;
  /** Text replacing the typed phrase (from `fromOffset` to the cursor). */
  insert: string;
  kind: 'date' | 'journal' | 'reminder';
  /** Human description ("jutro 09:00"). */
  human: string;
}

const DATE_TOKEN_TRIGGER = /(📅|⏳|🛫|⏰!?)\uFE0F?[ \t]+([^\n📅⏳🛫⏰✅❌➕🔁]*)$/u;
const AT_TRIGGER = /(?:^|[\s(])@([^\s@][^@\n]*)$/u;

/**
 * Completions for a natural-language date typed after a date token emoji (+ space) or `@`.
 * The phrase must be fully recognised (`jutro o 9`, `w piątek`, `15.10`), so prose is safe.
 */
export function dateCompletions(
  before: string,
  now: Date,
  opts: { locale?: Locale; weekStartsOn?: 0 | 1; journalTitle?: (date: LocalDate) => string } = {},
): { fromOffset: number; options: DateOption[] } | null {
  const locale = opts.locale ?? 'pl';
  const parse = (phrase: string) => {
    const trimmed = phrase.trimEnd();
    if (!trimmed || /^\d{4}-\d{2}-\d{2}/.test(trimmed)) return null;
    const r = parseNaturalDate(trimmed, now, { locale, weekStartsOn: opts.weekStartsOn ?? 1 });
    if (!r || r.index !== 0 || r.matched.length !== trimmed.length) return null;
    return r;
  };
  const token = DATE_TOKEN_TRIGGER.exec(before);
  if (token) {
    const phrase = token[2]!;
    const r = parse(phrase);
    if (!r) return null;
    const value = r.time ? `${r.date} ${r.time}` : r.date;
    const human = formatDateHuman(r.time ? `${r.date}T${r.time}` : r.date, locale, now);
    return { fromOffset: before.length - phrase.length, options: [{ label: value, insert: value, kind: 'date', human }] };
  }
  const at = AT_TRIGGER.exec(before);
  if (at) {
    const phrase = at[1]!;
    const r = parse(phrase);
    if (!r) return null;
    const value = r.time ? `${r.date} ${r.time}` : r.date;
    const human = formatDateHuman(r.time ? `${r.date}T${r.time}` : r.date, locale, now);
    const title = opts.journalTitle?.(r.date) ?? r.date;
    const options: DateOption[] = [{ label: `[[${title}]]`, insert: `[[${title}]]`, kind: 'journal', human }];
    options.push({ label: value, insert: value, kind: 'date', human });
    if (r.time) options.push({ label: `⏰ ${value}`, insert: `⏰ ${value}`, kind: 'reminder', human });
    return { fromOffset: before.length - phrase.length - 1, options };
  }
  return null;
}

export const dateSource: CompletionSource = (context): CompletionResult | null => {
  const { state, pos } = context;
  const { text, lineFrom } = lineBefore(context);
  if (!/[📅⏳🛫⏰@]/u.test(text) || inNoCompleteContext(state, pos)) return null;
  const ctx = state.facet(editorContext);
  const t = tFor(state);
  const config = ctx?.workspace.config;
  const res = dateCompletions(text, editorNow(state), {
    locale: ctx?.locale ?? 'pl',
    weekStartsOn: config?.weekStartsOn ?? 1,
    journalTitle: config ? (d) => pageTitle(journalPathForDate(d, config)) : undefined,
  });
  if (!res) return null;
  const detail = { date: 'editor.complete.date', journal: 'editor.complete.journal', reminder: 'editor.complete.reminder' } as const;
  return {
    from: lineFrom + res.fromOffset,
    filter: false,
    options: res.options.map(
      (o, i): Completion => ({ label: o.label, detail: `${o.human} · ${t(detail[o.kind])}`, type: 'date', boost: -i, apply: o.insert }),
    ),
  };
};

// ---------------------------------------------------------------------------------------------
// Extension
// ---------------------------------------------------------------------------------------------

/** Completion sources in priority order. */
export const brulionCompletionSources: readonly CompletionSource[] = [
  wikiLinkSource,
  blockRefSource,
  tagSource,
  dateSource,
  slashCompletionSource(),
];

export function brulionAutocomplete(): Extension {
  return autocompletion({
    override: [...brulionCompletionSources],
    activateOnTyping: true,
    closeOnBlur: true,
    icons: false,
    maxRenderedOptions: 60,
    tooltipClass: () => 'b-complete',
    optionClass: (c) => (c.type ? `b-complete__opt b-complete__opt--${c.type}` : 'b-complete__opt'),
  });
}

/** Blocks of a note that can be referenced (for tests and other UIs). */
export function referenceableBlocks(note: ParsedNote): Block[] {
  return note.allBlocks.filter((b) => b.kind !== 'frontmatter' && b.text.trim() !== '');
}
