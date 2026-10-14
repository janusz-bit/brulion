/**
 * Source lines: every rendered block element carries `data-line="<0-based source line>"`
 * (markdown-it `token.map`, plus `ctx.lineOffset`) for scroll sync and click-to-edit.
 */
import type { MarkdownIt, StateCore } from 'markdown-it';
import { dataLine, getEnv } from '../env';

/** The `source_lines` core rule (runs last). */
export function sourceLinesRule(state: StateCore): void {
  const env = getEnv(state.env);
  if (!env || state.inlineMode) return;
  for (const tok of state.tokens) {
    if (!tok.block || !tok.map || tok.nesting === -1 || tok.type === 'inline' || tok.hidden) continue;
    if (tok.attrIndex('data-line') >= 0) continue;
    tok.attrSet('data-line', dataLine(env, tok.map[0]));
  }
}

/** Install source line attributes. */
export function sourcemapPlugin(md: MarkdownIt): void {
  md.core.ruler.push('source_lines', sourceLinesRule);
}
