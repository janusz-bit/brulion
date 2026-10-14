/** Pure windowing maths for <VirtualList>: row offsets (fixed or measured) and visible ranges. */

/**
 * Prefix sums of row heights with lazy recomputation: `setHeight` marks the index dirty,
 * offsets are rebuilt from the first dirty index on the next read (O(n) per batch).
 */
export class OffsetIndex {
  readonly count: number;
  private heights: Float64Array;
  private offsets: Float64Array;
  private dirtyFrom = 0;

  constructor(count: number, estimate: (index: number) => number) {
    this.count = count;
    this.heights = new Float64Array(count);
    this.offsets = new Float64Array(count + 1);
    for (let i = 0; i < count; i++) this.heights[i] = Math.max(0, estimate(i));
  }

  /** Update a measured height; returns true when it changed. */
  setHeight(index: number, height: number): boolean {
    if (index < 0 || index >= this.count) return false;
    const h = Math.max(0, height);
    if (Math.abs(this.heights[index]! - h) < 0.5) return false;
    this.heights[index] = h;
    if (index < this.dirtyFrom) this.dirtyFrom = index;
    return true;
  }

  heightOf(index: number): number {
    return this.heights[index] ?? 0;
  }

  private ensure(): void {
    if (this.dirtyFrom > this.count) return;
    for (let i = this.dirtyFrom; i < this.count; i++) this.offsets[i + 1] = this.offsets[i]! + this.heights[i]!;
    this.dirtyFrom = this.count + 1;
  }

  /** Top of row `index` (index === count gives the total height). */
  offsetOf(index: number): number {
    this.ensure();
    return this.offsets[Math.max(0, Math.min(index, this.count))]!;
  }

  total(): number {
    return this.offsetOf(this.count);
  }

  /** Index of the row containing `offset` (clamped to [0, count - 1]; 0 when empty). */
  indexAt(offset: number): number {
    this.ensure();
    if (this.count === 0) return 0;
    let lo = 0;
    let hi = this.count - 1;
    while (lo < hi) {
      const mid = (lo + hi + 1) >> 1;
      if (this.offsets[mid]! <= offset) lo = mid;
      else hi = mid - 1;
    }
    return lo;
  }
}

export interface Range {
  /** First rendered index (inclusive). */
  start: number;
  /** Last rendered index (exclusive). */
  end: number;
}

/** Rows to render for a scroll position, with `overscan` pixels above and below. */
export function visibleRange(index: OffsetIndex, scrollTop: number, viewport: number, overscan = 240): Range {
  if (index.count === 0) return { start: 0, end: 0 };
  const top = Math.max(0, scrollTop - overscan);
  const bottom = scrollTop + Math.max(0, viewport) + overscan;
  const start = index.indexAt(top);
  let end = index.indexAt(bottom) + 1;
  end = Math.min(index.count, Math.max(end, start + 1));
  return { start, end };
}

/** Fixed-height shortcut (no index needed). */
export function fixedRange(count: number, rowHeight: number, scrollTop: number, viewport: number, overscan = 240): Range {
  if (count === 0 || rowHeight <= 0) return { start: 0, end: Math.min(count, 1) };
  const start = Math.max(0, Math.floor((scrollTop - overscan) / rowHeight));
  const end = Math.min(count, Math.ceil((scrollTop + viewport + overscan) / rowHeight));
  return { start: Math.min(start, count), end: Math.max(end, Math.min(count, start + 1)) };
}

export type ScrollAlign = 'start' | 'center' | 'end' | 'auto';

/**
 * scrollTop that brings a row (top `offset`, height `size`) into a viewport; null when
 * `align` is 'auto' and the row is already fully visible.
 */
export function scrollTargetFor(
  offset: number,
  size: number,
  scrollTop: number,
  viewport: number,
  align: ScrollAlign = 'auto',
): number | null {
  switch (align) {
    case 'start':
      return offset;
    case 'end':
      return Math.max(0, offset + size - viewport);
    case 'center':
      return Math.max(0, offset + size / 2 - viewport / 2);
    case 'auto':
      if (offset < scrollTop) return offset;
      if (offset + size > scrollTop + viewport) return Math.max(0, offset + size - viewport);
      return null;
  }
}
