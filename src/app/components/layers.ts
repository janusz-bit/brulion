/**
 * A tiny stack of open overlay layers (modals, popovers, menus) so nested overlays behave:
 *
 * * Escape closes only the topmost layer.
 * * A pointer press outside the topmost layer closes it (if it wants that), and continues
 *   down the stack until a layer contains the press target. Modals contain their whole
 *   backdrop, so presses outside a popover that sits on a modal close just the popover.
 *
 * Browser-only (document listeners are installed lazily, on the first registered layer).
 */

export interface Layer {
  /** Elements that count as "inside" (the layer itself, its anchor/trigger, ...). */
  contains(target: Node): boolean;
  /**
   * Where the layer was opened from (a popover's anchor). Used for ordering: a layer whose
   * origin lies inside another layer stays above it even when both mount in the same commit
   * (React runs child effects before parent effects).
   */
  origin?: () => Node | null;
  onEscape?: (e: KeyboardEvent) => void;
  /** Called on a press outside; omit to make the layer ignore outside presses. */
  onOutside?: (e: PointerEvent) => void;
}

const stack: Layer[] = [];
let installed = false;

function onKeyDown(e: KeyboardEvent): void {
  if (e.key !== 'Escape' || e.defaultPrevented) return;
  const top = stack[stack.length - 1];
  if (!top?.onEscape) return;
  e.preventDefault();
  e.stopPropagation();
  top.onEscape(e);
}

function onPointerDown(e: PointerEvent): void {
  const target = e.target;
  if (!(target instanceof Node)) return;
  // Snapshot: handlers may unregister layers while we iterate.
  for (const layer of [...stack].reverse()) {
    if (layer.contains(target)) return;
    if (!layer.onOutside) return;
    layer.onOutside(e);
  }
}

function install(): void {
  if (installed || typeof document === 'undefined') return;
  installed = true;
  document.addEventListener('keydown', onKeyDown);
  document.addEventListener('pointerdown', onPointerDown, true);
}

function uninstall(): void {
  if (!installed || stack.length > 0) return;
  installed = false;
  document.removeEventListener('keydown', onKeyDown);
  document.removeEventListener('pointerdown', onPointerDown, true);
}

/** Push a layer; returns the function that removes it. */
export function pushLayer(layer: Layer): () => void {
  // Insert below the first open layer that was opened from inside this one.
  const below = stack.findIndex((l) => {
    const o = l.origin?.();
    return !!o && layer.contains(o);
  });
  if (below >= 0) stack.splice(below, 0, layer);
  else stack.push(layer);
  install();
  return () => {
    const i = stack.lastIndexOf(layer);
    if (i >= 0) stack.splice(i, 1);
    uninstall();
  };
}

/** Number of open layers (tests, debugging). */
export function layerCount(): number {
  return stack.length;
}

/** True when `layer` is the topmost open layer. */
export function isTopLayer(layer: Layer): boolean {
  return stack[stack.length - 1] === layer;
}
