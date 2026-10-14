/**
 * Follow links from the Markdown source: Ctrl/⌘-click (keyboard: Alt-Enter at the cursor) on a
 * wiki link, embed, block reference, tag or URL opens it through the EditorContext; Shift
 * adds `newPane` (side panel). Plain clicks keep placing the cursor, Ctrl/⌘-click elsewhere
 * still adds a cursor. The live preview may add its own click handling on widgets.
 */
import { syntaxTree } from '@codemirror/language';
import type { EditorState } from '@codemirror/state';
import { EditorView, type Command } from '@codemirror/view';
import type { SyntaxNode } from '@lezer/common';
import { safeDecode } from '@/core/markdown';
import { getEditorContext } from './context';

export type LinkTarget =
  | { kind: 'page'; target: string }
  | { kind: 'block'; id: string }
  | { kind: 'tag'; tag: string }
  | { kind: 'url'; url: string };

const LINK_NODES = new Set(['WikiLink', 'Embed', 'BlockRef', 'BlockEmbed', 'Tag', 'URL', 'Link', 'Image', 'Autolink']);

/** The link-like syntax node at `pos` (looking at both sides), if any. */
export function linkAt(state: EditorState, pos: number): LinkTarget | null {
  for (const side of [1, -1] as const) {
    for (let n: SyntaxNode | null = syntaxTree(state).resolveInner(pos, side); n; n = n.parent) {
      if (!LINK_NODES.has(n.name)) continue;
      const text = state.sliceDoc(n.from, n.to);
      switch (n.name) {
        case 'WikiLink':
        case 'Embed': {
          const target = n.getChild('WikiTarget');
          return { kind: 'page', target: target ? state.sliceDoc(target.from, target.to) : '' };
        }
        case 'BlockRef':
        case 'BlockEmbed': {
          const id = n.getChild('BlockRefId');
          return id ? { kind: 'block', id: state.sliceDoc(id.from, id.to) } : null;
        }
        case 'Tag': {
          const name = n.getChild('TagName');
          return name ? { kind: 'tag', tag: state.sliceDoc(name.from, name.to) } : null;
        }
        case 'URL':
          // The destination of `[text](dest)` / `![alt](dest)`: handled with its link below.
          if (n.parent && (n.parent.name === 'Link' || n.parent.name === 'Image')) continue;
          return { kind: 'url', url: text.replace(/^<|>$/g, '') };
        case 'Link':
        case 'Image':
        case 'Autolink': {
          const url = n.getChild('URL');
          if (!url) return null;
          const dest = state.sliceDoc(url.from, url.to).replace(/^<|>$/g, '');
          return /^[a-z][a-z0-9+.-]*:/i.test(dest) ? { kind: 'url', url: dest } : { kind: 'page', target: safeDecode(dest) };
        }
      }
    }
  }
  return null;
}

/** Open a link target through the editor context. Returns false when nothing was opened. */
export function openTarget(view: EditorView, link: LinkTarget, newPane = false): boolean {
  const ctx = getEditorContext(view.state);
  if (!ctx) return false;
  switch (link.kind) {
    case 'page':
      ctx.openLink(link.target || ctx.path, { newPane });
      return true;
    case 'block': {
      const ref = ctx.workspace.index.blockById(link.id);
      if (!ref) return false;
      ctx.navigate({ view: 'page', path: ref.path, blockId: link.id, line: ref.block.line }, { newPane });
      return true;
    }
    case 'tag':
      ctx.navigate({ view: 'tags', tag: link.tag }, { newPane });
      return true;
    case 'url':
      void ctx.workspace.platform.openExternal(link.url);
      return true;
  }
}

/** Keyboard: open the link at the cursor (Alt-Enter). */
export const openLinkAtCursor: Command = (view) => {
  const link = linkAt(view.state, view.state.selection.main.head);
  return link ? openTarget(view, link) : false;
};

/** Ctrl/⌘-click opens links in the source (Shift: side panel). */
export const linkClicks = EditorView.domEventHandlers({
  mousedown(event, view) {
    if (event.button !== 0 || !(event.ctrlKey || event.metaKey)) return false;
    const pos = view.posAtCoords({ x: event.clientX, y: event.clientY });
    if (pos === null) return false;
    const link = linkAt(view.state, pos);
    if (!link) return false;
    event.preventDefault();
    return openTarget(view, link, event.shiftKey);
  },
});
