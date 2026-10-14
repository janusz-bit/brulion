/**
 * Pure positioning for anchored overlays (popovers, menus): place next to an anchor rect,
 * flip to the opposite side when there is not enough room, and clamp into the viewport.
 */

export type Side = 'top' | 'bottom' | 'left' | 'right';
export type Align = 'start' | 'center' | 'end';
export type Placement = Side | `${Side}-start` | `${Side}-end`;

export interface RectLike {
  top: number;
  left: number;
  width: number;
  height: number;
}

export interface Size {
  width: number;
  height: number;
}

export interface PositionResult {
  top: number;
  left: number;
  /** The placement actually used (after flipping). */
  placement: Placement;
  /** Room on the chosen side, for max-height / max-width of scrollable content. */
  maxHeight: number;
  maxWidth: number;
}

const OPPOSITE: Record<Side, Side> = { top: 'bottom', bottom: 'top', left: 'right', right: 'left' };

export function splitPlacement(p: Placement): [Side, Align] {
  const [side, align] = p.split('-') as [Side, Align | undefined];
  return [side, align ?? 'center'];
}

function clamp(v: number, lo: number, hi: number): number {
  if (hi < lo) return lo;
  return Math.min(Math.max(v, lo), hi);
}

/**
 * Compute the top/left of a floating box of `size` around `anchor` within a viewport of
 * `viewport` (all in the same coordinate space, e.g. client coordinates for position: fixed).
 */
export function computePosition(
  anchor: RectLike,
  size: Size,
  viewport: Size,
  placement: Placement = 'bottom-start',
  offset = 6,
  margin = 8,
): PositionResult {
  const [preferred, align] = splitPlacement(placement);
  const space: Record<Side, number> = {
    top: anchor.top - offset - margin,
    bottom: viewport.height - (anchor.top + anchor.height) - offset - margin,
    left: anchor.left - offset - margin,
    right: viewport.width - (anchor.left + anchor.width) - offset - margin,
  };
  const need = preferred === 'top' || preferred === 'bottom' ? size.height : size.width;
  let side = preferred;
  if (space[preferred] < need && space[OPPOSITE[preferred]] > space[preferred]) side = OPPOSITE[preferred];

  let top: number;
  let left: number;
  const vertical = side === 'top' || side === 'bottom';
  if (vertical) {
    top = side === 'bottom' ? anchor.top + anchor.height + offset : anchor.top - offset - size.height;
    if (align === 'start') left = anchor.left;
    else if (align === 'end') left = anchor.left + anchor.width - size.width;
    else left = anchor.left + anchor.width / 2 - size.width / 2;
    left = clamp(left, margin, viewport.width - size.width - margin);
    // Not enough room on either side: keep it inside the viewport, overlapping the anchor.
    top = clamp(top, margin, viewport.height - size.height - margin);
  } else {
    left = side === 'right' ? anchor.left + anchor.width + offset : anchor.left - offset - size.width;
    if (align === 'start') top = anchor.top;
    else if (align === 'end') top = anchor.top + anchor.height - size.height;
    else top = anchor.top + anchor.height / 2 - size.height / 2;
    top = clamp(top, margin, viewport.height - size.height - margin);
    left = clamp(left, margin, viewport.width - size.width - margin);
  }
  const usedPlacement = (align === 'center' ? side : `${side}-${align}`) as Placement;
  return {
    top: Math.round(top),
    left: Math.round(left),
    placement: usedPlacement,
    maxHeight: Math.max(0, vertical ? space[side] : viewport.height - 2 * margin),
    maxWidth: Math.max(0, vertical ? viewport.width - 2 * margin : space[side]),
  };
}
