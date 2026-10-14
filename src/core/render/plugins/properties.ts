/**
 * Block properties (SYNTAX §4, Logseq / Dataview style): `key:: value` lines inside a paragraph
 * (or a list item's text) render as
 * `<div class="block-props"><div class="block-prop" data-key><span class="prop-key">key</span>
 * <span class="prop-value">value</span></div></div>`; the value is inline Markdown. The other
 * lines of the paragraph stay paragraphs, in source order.
 *
 * Logseq's `id:: <uuid>` is the block id (→ `data-block-id` on the block, not shown);
 * `collapsed::` is UI state and hidden.
 */
import type { MarkdownIt, RendererRule, StateCore, Token } from 'markdown-it';
import { escapeHtml, renderAttrs } from '../escape';
import { setBlockId } from './util';

const PROP_RE = /^[ \t]*([A-Za-z0-9_-]+)::(?:[ \t]+(.*?))?[ \t]*$/;
/** Properties that are not displayed. */
const HIDDEN_PROPS = new Set(['collapsed']);

/** A `key:: value` line, or null. */
export function parsePropertyLine(line: string): { key: string; value: string } | null {
  const m = PROP_RE.exec(line);
  return m ? { key: m[1]!, value: m[2] ?? '' } : null;
}

type Segment = { kind: 'text'; lines: string[]; start: number } | { kind: 'props'; props: { key: string; value: string; line: number }[]; start: number };

function segmentsOf(lines: readonly string[], base: number): Segment[] {
  const segs: Segment[] = [];
  lines.forEach((text, k) => {
    const prop = parsePropertyLine(text);
    const last = segs[segs.length - 1];
    if (prop) {
      if (last?.kind === 'props') last.props.push({ ...prop, line: base + k });
      else segs.push({ kind: 'props', props: [{ ...prop, line: base + k }], start: base + k });
    } else if (last?.kind === 'text') {
      last.lines.push(text);
    } else {
      segs.push({ kind: 'text', lines: [text], start: base + k });
    }
  });
  return segs;
}

function blockToken(state: StateCore, type: string, tag: string, nesting: 1 | 0 | -1, level: number, map: [number, number] | null): Token {
  const t = new state.Token(type, tag, nesting);
  t.block = true;
  t.level = level;
  t.map = map;
  return t;
}

/** Replace one paragraph with its text / property segments. */
function splitParagraph(state: StateCore, open: Token, inline: Token, close: Token, container: Token | null): Token[] {
  const base = inline.map?.[0] ?? open.map?.[0] ?? 0;
  const segs = segmentsOf(inline.content.split('\n'), base);
  const task = inline.meta?.task;
  // A task needs a text paragraph for its checkbox.
  if (task !== undefined && segs[0]?.kind !== 'text') segs.unshift({ kind: 'text', lines: [], start: base });
  const out: Token[] = [];
  let blockId: string | null = null;
  let firstText: Token | null = null;
  for (const seg of segs) {
    if (seg.kind === 'text') {
      const end = seg.start + Math.max(1, seg.lines.length);
      const p = blockToken(state, 'paragraph_open', 'p', 1, open.level, [seg.start, end]);
      p.hidden = open.hidden;
      if (!firstText && open.attrs) p.attrs = open.attrs.map(([n, v]) => [n, v]);
      const inl = new state.Token('inline', '', 0);
      inl.level = inline.level;
      inl.map = [seg.start, end];
      inl.content = seg.lines.join('\n');
      inl.children = [];
      if (!firstText && inline.meta) inl.meta = { ...inline.meta };
      const pc = blockToken(state, 'paragraph_close', 'p', -1, close.level, null);
      pc.hidden = close.hidden;
      firstText ??= p;
      out.push(p, inl, pc);
      continue;
    }
    const visible = seg.props.filter((prop) => {
      const key = prop.key.toLowerCase();
      if (key === 'id') {
        if (prop.value.trim()) blockId ??= prop.value.trim();
        return false;
      }
      return !HIDDEN_PROPS.has(key);
    });
    if (visible.length === 0) continue;
    const last = visible[visible.length - 1]!;
    const wrapOpen = blockToken(state, 'block_props_open', 'div', 1, open.level, [seg.start, last.line + 1]);
    wrapOpen.attrSet('class', 'block-props');
    out.push(wrapOpen);
    for (const prop of visible) {
      const po = blockToken(state, 'block_prop_open', 'div', 1, open.level + 1, [prop.line, prop.line + 1]);
      po.meta = { key: prop.key };
      const inl = new state.Token('inline', '', 0);
      inl.level = open.level + 2;
      inl.map = [prop.line, prop.line + 1];
      inl.content = prop.value;
      inl.children = [];
      out.push(po, inl, blockToken(state, 'block_prop_close', 'div', -1, open.level + 1, null));
    }
    out.push(blockToken(state, 'block_props_close', 'div', -1, open.level, null));
  }
  if (blockId !== null) {
    const target = container ?? firstText ?? out.find((t) => t.type === 'block_props_open') ?? null;
    if (target) setBlockId(target, blockId);
  }
  return out;
}

/** The `block_properties` core rule (before inline parsing). */
export function propertiesRule(state: StateCore): void {
  if (state.inlineMode) return;
  const tokens = state.tokens;
  let out: Token[] | null = null;
  for (let i = 0; i < tokens.length; i++) {
    const tok = tokens[i]!;
    const inline = tokens[i + 1];
    const close = tokens[i + 2];
    if (
      tok.type === 'paragraph_open' &&
      inline?.type === 'inline' &&
      close?.type === 'paragraph_close' &&
      inline.content.includes('::') &&
      inline.content.split('\n').some((l) => PROP_RE.test(l))
    ) {
      out ??= tokens.slice(0, i);
      const prev = tokens[i - 1];
      const container = prev?.type === 'list_item_open' ? prev : null;
      for (const t of splitParagraph(state, tok, inline, close, container)) out.push(t);
      i += 2;
      continue;
    }
    out?.push(tok);
  }
  if (out) state.tokens = out;
}

/** Install block properties. */
export function propertiesPlugin(md: MarkdownIt): void {
  md.core.ruler.before('inline', 'block_properties', propertiesRule);
  const propOpen: RendererRule = (tokens, idx) => {
    const key = String(tokens[idx]!.meta?.key ?? '');
    return `<div${renderAttrs({ class: 'block-prop', 'data-key': key })}><span class="prop-key">${escapeHtml(key)}</span> <span class="prop-value">`;
  };
  md.renderer.rules.block_prop_open = propOpen;
  md.renderer.rules.block_prop_close = () => '</span></div>\n';
}
