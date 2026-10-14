/**
 * Replacement of markdown-it's `text` inline rule. markdown-it skips runs of "ordinary"
 * characters in one step; its terminator set lacks `(`, so `((block refs))` in the middle of
 * text would never reach the block reference rule. This rule is identical except that `(` also
 * ends a text run.
 */
import type { MarkdownIt, StateInline } from 'markdown-it';

/** Characters at which other inline rules may start (markdown-it's list + `(`). */
function isTerminatorChar(code: number): boolean {
  switch (code) {
    case 0x0a /* \n */:
    case 0x21 /* ! */:
    case 0x23 /* # */:
    case 0x24 /* $ */:
    case 0x25 /* % */:
    case 0x26 /* & */:
    case 0x28 /* ( */:
    case 0x2a /* * */:
    case 0x2b /* + */:
    case 0x2d /* - */:
    case 0x3a /* : */:
    case 0x3c /* < */:
    case 0x3d /* = */:
    case 0x3e /* > */:
    case 0x40 /* @ */:
    case 0x5b /* [ */:
    case 0x5c /* \ */:
    case 0x5d /* ] */:
    case 0x5e /* ^ */:
    case 0x5f /* _ */:
    case 0x60 /* ` */:
    case 0x7b /* { */:
    case 0x7d /* } */:
    case 0x7e /* ~ */:
      return true;
    default:
      return false;
  }
}

/** The `text` inline rule: consume a run of non-terminator characters into pending text. */
export function textRule(state: StateInline, silent: boolean): boolean {
  let pos = state.pos;
  while (pos < state.posMax && !isTerminatorChar(state.src.charCodeAt(pos))) pos++;
  if (pos === state.pos) return false;
  if (!silent) state.pending += state.src.slice(state.pos, pos);
  state.pos = pos;
  return true;
}

/** Install the text rule. */
export function textPlugin(md: MarkdownIt): void {
  md.inline.ruler.at('text', textRule);
}
