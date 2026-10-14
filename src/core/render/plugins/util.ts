/**
 * Small token helpers shared by several plugins.
 */
import type { Token } from 'markdown-it';
import { blockAnchor } from '../anchors';

/** Mark `token` (a block opener) as the block with id `id`: `data-block-id` and `id="^id"`. */
export function setBlockId(token: Token, id: string): void {
  token.attrSet('data-block-id', id);
  if (token.attrIndex('id') < 0) token.attrSet('id', blockAnchor(id));
}

/**
 * The block that ends right before `tokens[idx]`: the opener matching a closing token, or a
 * self-contained block token (fence, hr, embed...). Null when there is none at this level.
 */
export function previousBlock(tokens: readonly Token[], idx: number): Token | null {
  const prev = tokens[idx - 1];
  if (!prev || !prev.block || prev.type === 'inline') return null;
  if (prev.nesting === 0) return prev.hidden ? null : prev;
  if (prev.nesting === 1) return null;
  let depth = 0;
  for (let k = idx - 1; k >= 0; k--) {
    const t = tokens[k]!;
    if (t.nesting === -1) depth++;
    else if (t.nesting === 1) {
      depth--;
      if (depth === 0) return t;
    }
  }
  return null;
}

/** Plain text of inline children (text, code, links' text), for anchors and titles. */
export function inlineText(children: readonly Token[] | null): string {
  let out = '';
  for (const c of children ?? []) {
    if (c.type === 'text' || c.type === 'code_inline') out += c.content;
    else if (c.type === 'softbreak' || c.type === 'hardbreak') out += ' ';
    else if (c.type === 'tag') out += `#${c.content}`;
    else if (c.type === 'wikilink') {
      const meta = c.meta as { display?: string; target?: string; heading?: string } | null;
      out += meta?.display ?? meta?.heading ?? meta?.target ?? c.content;
    } else if (c.type === 'image') out += inlineText(c.children);
  }
  return out;
}
