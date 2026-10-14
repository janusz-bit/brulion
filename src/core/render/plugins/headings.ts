/**
 * Heading anchors: every heading gets `id="h-<slug>"` (see `headingAnchor`; duplicates get
 * `-2`, `-3`, ...) so `[[Page#Heading]]` links and exported HTML can jump to it. Headings that
 * already carry a block id keep it.
 */
import type { MarkdownIt, StateCore } from 'markdown-it';
import { headingAnchor } from '../anchors';
import { getEnv } from '../env';
import { inlineText } from './util';

/** The `heading_ids` core rule (after inline parsing). */
export function headingIdsRule(state: StateCore): void {
  const env = getEnv(state.env);
  if (!env || state.inlineMode) return;
  const tokens = state.tokens;
  for (let i = 0; i < tokens.length; i++) {
    const tok = tokens[i]!;
    if (tok.type !== 'heading_open' || tok.attrIndex('id') >= 0) continue;
    const text = inlineText(tokens[i + 1]?.children ?? null);
    const base = `${env.idPrefix}${headingAnchor(text)}`;
    const seen = env.headingAnchors.get(base) ?? 0;
    env.headingAnchors.set(base, seen + 1);
    tok.attrSet('id', seen === 0 ? base : `${base}-${seen + 1}`);
  }
}

/** Install heading anchors. */
export function headingsPlugin(md: MarkdownIt): void {
  md.core.ruler.before('linkify', 'heading_ids', headingIdsRule);
}
