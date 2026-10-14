/**
 * Obsidian comments `%%hidden%%` are removed from the output:
 *
 * * inline: `text %%comment%% text`, also across the lines of one paragraph;
 * * block: a block starting with `%%` and ending with a line that ends in `%%`, which may span
 *   blank lines (`%%\n…\n\n…\n%%`).
 *
 * An unclosed `%%` is literal text (like the note parser, so a typo never hides the rest of a
 * note). `%%` inside code is never a comment (code is tokenised separately).
 */
import type { MarkdownIt, StateBlock, StateInline } from 'markdown-it';

/** Line index containing offset `pos`, scanning forward from `from` (or -1 past `endLine`). */
function lineOfOffset(state: StateBlock, from: number, endLine: number, pos: number): number {
  for (let line = from; line < endLine; line++) {
    if (pos <= state.eMarks[line]!) return pos >= state.bMarks[line]! ? line : -1;
  }
  return -1;
}

/** The `comment_block` block rule. */
export function commentBlockRule(state: StateBlock, startLine: number, endLine: number, silent: boolean): boolean {
  if (state.sCount[startLine]! - state.blkIndent >= 4) return false;
  const src = state.src;
  const start = state.bMarks[startLine]! + state.tShift[startLine]!;
  if (src.charCodeAt(start) !== 0x25 || src.charCodeAt(start + 1) !== 0x25) return false;
  const close = src.indexOf('%%', start + 2);
  if (close === -1) return false;
  const closeLine = lineOfOffset(state, startLine, endLine, close);
  if (closeLine === -1 || close + 2 > state.eMarks[closeLine]!) return false;
  // Every line of the comment must belong to the current container (list item, quote).
  for (let line = startLine + 1; line <= closeLine; line++) {
    if (state.sCount[line]! < state.blkIndent && !state.isEmpty(line)) return false;
  }
  const rest = src.slice(close + 2, state.eMarks[closeLine]).trim();
  // `%%c%% text` on one line is an inline comment inside a paragraph.
  if (rest && closeLine === startLine) return false;
  if (silent) return true;
  const token = state.push('comment_block', '', 0);
  token.block = true;
  token.hidden = true;
  token.map = [startLine, closeLine + 1];
  state.line = closeLine + 1;
  if (rest) {
    // Text after the closing `%%` of a multi-line comment stays visible.
    const open = state.push('paragraph_open', 'p', 1);
    open.map = [closeLine, closeLine + 1];
    const inline = state.push('inline', '', 0);
    inline.content = rest;
    inline.map = [closeLine, closeLine + 1];
    inline.children = [];
    state.push('paragraph_close', 'p', -1);
  }
  return true;
}

/** The `comment_inline` inline rule: skip `%%…%%` within the inline content. */
export function commentInlineRule(state: StateInline, _silent: boolean): boolean {
  const pos = state.pos;
  const src = state.src;
  if (src.charCodeAt(pos) !== 0x25 || src.charCodeAt(pos + 1) !== 0x25) return false;
  const close = src.indexOf('%%', pos + 2);
  if (close === -1 || close + 2 > state.posMax) return false;
  state.pos = close + 2;
  return true;
}

/** Install comment handling. */
export function commentsPlugin(md: MarkdownIt): void {
  md.block.ruler.before('lheading', 'comment_block', commentBlockRule);
  md.inline.ruler.before('link', 'comment_inline', commentInlineRule);
  md.renderer.rules.comment_block = () => '';
}
