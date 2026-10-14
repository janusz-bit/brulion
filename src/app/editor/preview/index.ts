/**
 * Live preview for the note editor: Obsidian-style hidden Markdown syntax with Logseq-style
 * outline bullets, guides and folding, task checkboxes and chips, rendered links, block
 * references, transclusions, images, calc results, tables, query results, callouts and
 * frontmatter. `livePreview()` is spread into the editor's extensions by ../setup.ts (inside a
 * compartment, so the `livePreview` setting can switch it off).
 *
 * Parts:
 * * state.ts   — focus / vault-version fields and the workspace subscription;
 * * inline.ts  — ViewPlugin decorating the visible ranges (incremental on selection moves);
 * * blocks.ts  — StateField with line-replacing widgets (tables, frontmatter, queries) and
 *                collapsed callouts;
 * * actions.ts — pointer handling (links, bullets, folds, checkboxes, chips, tables, queries);
 * * widgets.ts, embeds.ts, tables.ts, query.ts, menu.ts — widgets and their helpers;
 * * reveal.ts, lists.ts, tokens.ts, callouts.ts, render.ts — pure helpers (unit-tested).
 * Styles: preview.css (theme variables only).
 */
import type { Extension } from '@codemirror/state';
import { previewHandlers } from './actions';
import { blockExtension } from './blocks';
import { inlinePreview } from './inline';
import { previewState } from './state';
import './preview.css';

export function livePreview(): Extension[] {
  return [previewState(), blockExtension(), inlinePreview, previewHandlers()];
}

export { applyTableAction, tableCellSpans, TableWidget } from './tables';
export { buildDecorations, inlinePreview, wikiDisplay } from './inline';
export { blockPreview, scanBlocks, setFrontmatterOpen, toggleCallout } from './blocks';
export { chipFor, tokenDateValue, tokenKind } from './tokens';
export type { ChipModel, TokenKind } from './tokens';
export { revealSelection, touches } from './reveal';
export { previewFocus, setPreviewFocus, vaultChanged } from './state';
export { runAction, toggleTaskAtLine } from './actions';
