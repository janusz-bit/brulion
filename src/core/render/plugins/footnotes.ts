/**
 * Footnotes (simple implementation): references `[^label]`, inline footnotes `^[text]` and
 * definitions `[^label]: text` (continuation lines indented by 2+ spaces, or lazy paragraph
 * continuation). Definitions are hidden where written; all footnotes are listed at the end in
 * `<section class="footnotes">`, numbered in order of first reference (unreferenced definitions
 * follow). Footnote content is inline Markdown. References to undefined labels stay text.
 */
import type { MarkdownIt, RendererRule, StateBlock, StateCore, StateInline, Token } from 'markdown-it';
import { footnoteId, footnoteRefId } from '../anchors';
import { escapeHtml, renderAttrs } from '../escape';
import { getEnv, tr } from '../env';

const DEF_RE = /^\[\^([^\]\s]{1,100})\]:[ \t]?/;
const REF_RE = /\[\^([^\]\s]{1,100})\]/y;

/** Meta of `footnote_ref` tokens. */
export type FootnoteRefMeta = { label?: string; inline?: string; n?: number; k?: number };

/** Does line `line` start a block that ends a lazy footnote continuation? */
function interrupts(state: StateBlock, line: number, endLine: number): boolean {
  const oldParent = state.parentType;
  state.parentType = 'paragraph';
  try {
    for (const rule of state.md.block.ruler.getRules('paragraph')) {
      if (rule(state, line, endLine, true)) return true;
    }
    return false;
  } finally {
    state.parentType = oldParent;
  }
}

/** The `footnote_def` block rule. */
export function footnoteDefRule(state: StateBlock, startLine: number, endLine: number, silent: boolean): boolean {
  if (state.sCount[startLine]! - state.blkIndent >= 4) return false;
  const start = state.bMarks[startLine]! + state.tShift[startLine]!;
  const max = state.eMarks[startLine]!;
  if (state.src.charCodeAt(start) !== 0x5b || state.src.charCodeAt(start + 1) !== 0x5e) return false;
  const m = DEF_RE.exec(state.src.slice(start, max));
  if (!m) return false;
  if (silent) return true;
  const lines = [state.src.slice(start + m[0].length, max)];
  let line = startLine + 1;
  while (line < endLine) {
    if (state.isEmpty(line)) {
      let next = line + 1;
      while (next < endLine && state.isEmpty(next)) next++;
      if (next < endLine && state.sCount[next]! >= state.blkIndent + 2) {
        for (let k = line; k < next; k++) lines.push('');
        line = next;
        continue;
      }
      break;
    }
    const indented = state.sCount[line]! >= state.blkIndent + 2;
    if (!indented && (state.sCount[line]! < state.blkIndent || interrupts(state, line, endLine))) break;
    lines.push(state.src.slice(state.bMarks[line]! + state.tShift[line]!, state.eMarks[line]!));
    line++;
  }
  const label = m[1]!;
  const env = getEnv(state.env);
  if (env && !env.footnotes.has(label)) env.footnotes.set(label, { label, content: lines.join('\n').trim(), line: startLine });
  const token = state.push('footnote_def', '', 0);
  token.block = true;
  token.hidden = true;
  token.map = [startLine, line];
  token.meta = { label };
  state.line = line;
  return true;
}

/** The `footnote_ref` inline rule: `[^label]` with a known definition. */
export function footnoteRefRule(state: StateInline, silent: boolean): boolean {
  const pos = state.pos;
  const src = state.src;
  if (src.charCodeAt(pos) !== 0x5b || src.charCodeAt(pos + 1) !== 0x5e) return false;
  REF_RE.lastIndex = pos;
  const m = REF_RE.exec(src);
  if (!m || pos + m[0].length > state.posMax) return false;
  const env = getEnv(state.env);
  if (!env?.footnotes.has(m[1]!)) return false;
  if (!silent) {
    const token = state.push('footnote_ref', '', 0);
    token.meta = { label: m[1]! } satisfies FootnoteRefMeta;
  }
  state.pos = pos + m[0].length;
  return true;
}

/** The `footnote_inline` inline rule: `^[inline footnote text]`. */
export function footnoteInlineRule(state: StateInline, silent: boolean): boolean {
  const pos = state.pos;
  if (state.src.charCodeAt(pos) !== 0x5e || state.src.charCodeAt(pos + 1) !== 0x5b) return false;
  const end = state.md.helpers.parseLinkLabel(state, pos + 1);
  if (end < 0) return false;
  const content = state.src.slice(pos + 2, end);
  if (!content.trim()) return false;
  if (!silent) {
    const token = state.push('footnote_ref', '', 0);
    token.meta = { inline: content } satisfies FootnoteRefMeta;
  }
  state.pos = end + 1;
  return true;
}

type Entry = { n: number; refs: number; content: string };

function blockToken(state: StateCore, type: string, tag: string, nesting: 1 | 0 | -1): Token {
  const t = new state.Token(type, tag, nesting);
  t.block = true;
  return t;
}

/** The `footnote_tail` core rule: number references and append the footnote list. */
export function footnoteTailRule(state: StateCore): void {
  const env = getEnv(state.env);
  if (!env || state.inlineMode) return;
  const entries: Entry[] = [];
  const byLabel = new Map<string, Entry>();
  for (const tok of state.tokens) {
    if (tok.type !== 'inline' || !tok.children) continue;
    for (const child of tok.children) {
      if (child.type !== 'footnote_ref') continue;
      const meta = (child.meta ?? {}) as FootnoteRefMeta;
      let entry: Entry | undefined;
      if (meta.label !== undefined) {
        entry = byLabel.get(meta.label);
        if (!entry) {
          entry = { n: entries.length + 1, refs: 0, content: env.footnotes.get(meta.label)?.content ?? '' };
          byLabel.set(meta.label, entry);
          entries.push(entry);
        }
      } else {
        entry = { n: entries.length + 1, refs: 0, content: meta.inline ?? '' };
        entries.push(entry);
      }
      entry.refs++;
      child.meta = { ...meta, n: entry.n, k: entry.refs } satisfies FootnoteRefMeta;
    }
  }
  for (const def of env.footnotes.values()) {
    if (byLabel.has(def.label)) continue;
    const entry = { n: entries.length + 1, refs: 0, content: def.content };
    byLabel.set(def.label, entry);
    entries.push(entry);
  }
  if (entries.length === 0) return;
  state.tokens.push(blockToken(state, 'footnotes_open', 'section', 1));
  for (const entry of entries) {
    const open = blockToken(state, 'footnote_item_open', 'li', 1);
    open.meta = { n: entry.n, refs: entry.refs };
    const inline = new state.Token('inline', '', 0);
    inline.content = entry.content;
    inline.children = [];
    state.md.inline.parse(entry.content, state.md, state.env, inline.children);
    const close = blockToken(state, 'footnote_item_close', 'li', -1);
    close.meta = open.meta;
    state.tokens.push(open, inline, close);
  }
  state.tokens.push(blockToken(state, 'footnotes_close', 'section', -1));
}

/** Install footnotes. */
export function footnotesPlugin(md: MarkdownIt): void {
  md.block.ruler.before('reference', 'footnote_def', footnoteDefRule, { alt: ['paragraph', 'reference'] });
  md.inline.ruler.before('link', 'footnote_ref', footnoteRefRule);
  md.inline.ruler.after('footnote_ref', 'footnote_inline', footnoteInlineRule);
  md.core.ruler.before('linkify', 'footnote_tail', footnoteTailRule);

  const ref: RendererRule = (tokens, idx, _options, mdEnv) => {
    const meta = (tokens[idx]!.meta ?? {}) as FootnoteRefMeta;
    const env = getEnv(mdEnv);
    if (meta.n === undefined || !env) {
      return meta.label !== undefined ? escapeHtml(`[^${meta.label}]`) : escapeHtml(`^[${meta.inline ?? ''}]`);
    }
    const attrs = renderAttrs({
      href: `#${footnoteId(env.idPrefix, meta.n)}`,
      id: footnoteRefId(env.idPrefix, meta.n, meta.k),
      'data-footnote': meta.n,
    });
    return `<sup class="footnote-ref"><a${attrs}>${meta.n}</a></sup>`;
  };
  const itemOpen: RendererRule = (tokens, idx, _options, mdEnv) => {
    const n = Number(tokens[idx]!.meta?.n ?? 0);
    const prefix = getEnv(mdEnv)?.idPrefix ?? '';
    return `<li${renderAttrs({ id: footnoteId(prefix, n), class: 'footnote-item', 'data-footnote': n })}>`;
  };
  const itemClose: RendererRule = (tokens, idx, _options, mdEnv) => {
    const n = Number(tokens[idx]!.meta?.n ?? 0);
    const refs = Number(tokens[idx]!.meta?.refs ?? 0);
    const env = getEnv(mdEnv);
    const prefix = env?.idPrefix ?? '';
    const label = env ? tr(env, 'render.footnotes.back') : undefined;
    let back = '';
    for (let k = 1; k <= refs; k++) {
      back += ` <a${renderAttrs({ href: `#${footnoteRefId(prefix, n, k)}`, class: 'footnote-backref', 'aria-label': label, title: label })}>↩︎</a>`;
    }
    return `${back}</li>\n`;
  };
  md.renderer.rules.footnote_ref = ref;
  md.renderer.rules.footnote_def = () => '';
  md.renderer.rules.footnotes_open = () => '<section class="footnotes" data-footnotes><hr class="footnotes-sep"><ol class="footnotes-list">\n';
  md.renderer.rules.footnotes_close = () => '</ol></section>\n';
  md.renderer.rules.footnote_item_open = itemOpen;
  md.renderer.rules.footnote_item_close = itemClose;
}
