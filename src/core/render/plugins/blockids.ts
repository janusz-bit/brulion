/**
 * Block ids (SYNTAX §4): ` ^abc-123` at the end of a block's first or last line is hidden and
 * becomes `data-block-id="abc-123" id="^abc-123"` on the block element (the list item for list
 * items, else the paragraph / heading). A paragraph consisting only of `^id` (Obsidian's way to
 * name a table, list or quote) is hidden and names the block right before it.
 */
import type { MarkdownIt, StateCore, Token } from 'markdown-it';
import { previousBlock, setBlockId } from './util';

const ID_AT_END_RE = /(^|[ \t])\^([A-Za-z0-9-]+)[ \t]*$/;
const STANDALONE_RE = /^[ \t]*\^([A-Za-z0-9-]+)[ \t]*$/;

function isBreak(t: Token): boolean {
  return t.type === 'softbreak' || t.type === 'hardbreak';
}

/** `^id` when the inline children are just a block id. */
export function standaloneBlockId(children: readonly Token[]): string | null {
  const meaningful = children.filter((c) => !(c.type === 'text' && c.content.trim() === '') && !isBreak(c));
  if (meaningful.length !== 1 || meaningful[0]!.type !== 'text') return null;
  const m = STANDALONE_RE.exec(meaningful[0]!.content);
  return m ? m[1]! : null;
}

/** Strip a block id from the end of text token `t`; returns the id. */
function stripIdFrom(t: Token | undefined): string | null {
  if (!t || t.type !== 'text') return null;
  const m = ID_AT_END_RE.exec(t.content);
  if (!m) return null;
  t.content = t.content.slice(0, m.index + m[1]!.length).replace(/[ \t]+$/, '');
  return m[2]!;
}

/**
 * Remove block ids at the end of the first and the last line of inline `children` (the text
 * token right before the first line break, and the last token). Returns the id (first line wins).
 */
export function stripLineEndIds(children: Token[]): string | null {
  if (children.length === 0) return null;
  const firstBreak = children.findIndex(isBreak);
  const firstLineEnd = firstBreak === -1 ? children.length - 1 : firstBreak - 1;
  const first = stripIdFrom(children[firstLineEnd]);
  const last = firstBreak === -1 ? null : stripIdFrom(children[children.length - 1]);
  return first ?? last;
}

/** The token that carries the id of the block whose opener is at `openIdx`. */
function containerOf(tokens: readonly Token[], openIdx: number): Token {
  const open = tokens[openIdx]!;
  const prev = tokens[openIdx - 1];
  return open.type === 'paragraph_open' && prev?.type === 'list_item_open' ? prev : open;
}

/** The `block_ids` core rule (after inline parsing). */
export function blockIdsRule(state: StateCore): void {
  if (state.inlineMode) return;
  const tokens = state.tokens;
  for (let i = 1; i < tokens.length; i++) {
    const tok = tokens[i]!;
    if (tok.type !== 'inline' || !tok.children || tok.children.length === 0) continue;
    const open = tokens[i - 1]!;
    if (open.type !== 'paragraph_open' && open.type !== 'heading_open') continue;
    if (open.type === 'paragraph_open' && !tok.meta?.task) {
      const sid = standaloneBlockId(tok.children);
      if (sid) {
        const target = previousBlock(tokens, i - 1);
        if (target) setBlockId(target, sid);
        // Hide the id paragraph completely (also in tight lists).
        open.hidden = true;
        const close = tokens[i + 1];
        if (close) close.hidden = true;
        tok.children = [];
        if (!target && tokens[i - 2]?.type === 'list_item_open') setBlockId(tokens[i - 2]!, sid);
        continue;
      }
    }
    const id = stripLineEndIds(tok.children);
    if (id) setBlockId(containerOf(tokens, i - 1), id);
  }
}

/** Install block id handling. */
export function blockIdsPlugin(md: MarkdownIt): void {
  md.core.ruler.before('linkify', 'block_ids', blockIdsRule);
}
