/**
 * Highlights `==text==` → `<mark>` (Obsidian), implemented like markdown-it's strikethrough:
 * `==` runs become delimiters that the `balance_pairs` rule matches, so highlights nest
 * correctly with emphasis and never cross code spans or links.
 */
import type { Delimiter, MarkdownIt, StateInline } from 'markdown-it';

const EQUALS = 0x3d;

/** Tokenizer: turn a run of `=` into text tokens plus `==` delimiters. */
export function markTokenize(state: StateInline, silent: boolean): boolean {
  const start = state.pos;
  if (silent || state.src.charCodeAt(start) !== EQUALS) return false;
  const scanned = state.scanDelims(start, true);
  let len = scanned.length;
  if (len < 2) return false;
  if (len % 2) {
    const token = state.push('text', '', 0);
    token.content = '=';
    len--;
  }
  for (let i = 0; i < len; i += 2) {
    const token = state.push('text', '', 0);
    token.content = '==';
    if (!scanned.can_open && !scanned.can_close) continue;
    state.delimiters.push({
      marker: EQUALS,
      length: 0,
      token: state.tokens.length - 1,
      end: -1,
      open: scanned.can_open,
      close: scanned.can_close,
    });
  }
  state.pos += scanned.length;
  return true;
}

function postProcess(state: StateInline, delimiters: Delimiter[]): void {
  const loneMarkers: number[] = [];
  for (const startDelim of delimiters) {
    if (startDelim.marker !== EQUALS || startDelim.end === -1) continue;
    const endDelim = delimiters[startDelim.end]!;
    const open = state.tokens[startDelim.token]!;
    open.type = 'mark_open';
    open.tag = 'mark';
    open.nesting = 1;
    open.markup = '==';
    open.content = '';
    const close = state.tokens[endDelim.token]!;
    close.type = 'mark_close';
    close.tag = 'mark';
    close.nesting = -1;
    close.markup = '==';
    close.content = '';
    const before = state.tokens[endDelim.token - 1];
    if (before?.type === 'text' && before.content === '=') loneMarkers.push(endDelim.token - 1);
  }
  // A lone `=` before a closing marker belongs outside the highlight (`===a==` cases).
  while (loneMarkers.length) {
    const i = loneMarkers.pop()!;
    let j = i + 1;
    while (j < state.tokens.length && state.tokens[j]!.type === 'mark_close') j++;
    j--;
    if (i !== j) {
      const tmp = state.tokens[j]!;
      state.tokens[j] = state.tokens[i]!;
      state.tokens[i] = tmp;
    }
  }
}

/** Post-processing rule: convert matched `==` delimiters into `mark_open` / `mark_close`. */
export function markPostProcess(state: StateInline): void {
  postProcess(state, state.delimiters);
  for (const meta of state.tokens_meta) {
    if (meta?.delimiters) postProcess(state, meta.delimiters);
  }
}

/** Install `==highlight==`. */
export function highlightPlugin(md: MarkdownIt): void {
  md.inline.ruler.before('emphasis', 'mark', markTokenize);
  md.inline.ruler2.before('emphasis', 'mark', markPostProcess);
}
