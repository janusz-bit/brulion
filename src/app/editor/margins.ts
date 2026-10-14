/**
 * Space covered by overlays at the screen's bottom edge (the mobile toolbar above the
 * keyboard), so CodeMirror scrolls the cursor clear of them: `overlayMargins` (in the editor's
 * extensions) turns the registered height into a bottom scroll margin. CodeMirror applies it to
 * every scrolling ancestor, so it also works for compact editors inside a scrolling page.
 */
import { EditorView } from '@codemirror/view';

const bottoms = new WeakMap<EditorView, number>();

/** Height in px covered at the bottom while an overlay is shown (null/0 = none). */
export function setBottomOverlay(view: EditorView, px: number | null): void {
  if (px && px > 0) bottoms.set(view, px);
  else bottoms.delete(view);
}

export function bottomOverlay(view: EditorView): number {
  return bottoms.get(view) ?? 0;
}

export const overlayMargins = EditorView.scrollMargins.of((view) => {
  const bottom = bottoms.get(view);
  return bottom ? { bottom } : null;
});
