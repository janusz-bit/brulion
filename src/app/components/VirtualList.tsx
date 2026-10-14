/**
 * Simple windowing for long lists (search results, all pages): only rows near the viewport
 * are rendered. Fixed heights (fast path), known variable heights (function) or measured
 * heights (`measure`, rows observed with ResizeObserver; `rowHeight` is the estimate).
 *
 * The list scrolls itself: give it a height (CSS / flex) — it fills its container.
 */
import {
  useCallback,
  useEffect,
  useImperativeHandle,
  useMemo,
  useRef,
  useState,
  type CSSProperties,
  type ReactNode,
  type Ref,
  type UIEvent,
} from 'react';
import { cx, useIsoLayoutEffect, useLatest } from './hooks';
import { fixedRange, OffsetIndex, scrollTargetFor, visibleRange, type ScrollAlign } from './virtual';

export interface VirtualListHandle {
  scrollToIndex(index: number, align?: ScrollAlign): void;
  scrollToOffset(offset: number): void;
  readonly element: HTMLDivElement | null;
}

export interface VirtualListProps<T> {
  items: readonly T[];
  renderRow: (item: T, index: number) => ReactNode;
  /** Row height in px, or a function of the item; with `measure` it is the first estimate. */
  rowHeight?: number | ((item: T, index: number) => number);
  /** Measure the real row heights after render (variable-height rows). */
  measure?: boolean;
  getKey?: (item: T, index: number) => string | number;
  /** Extra pixels rendered above and below the viewport. */
  overscan?: number;
  /** Viewport height assumed before the first measurement (and in tests). */
  initialHeight?: number;
  role?: string;
  'aria-label'?: string;
  /** Role of each row wrapper (e.g. 'listitem'); default none. */
  rowRole?: string;
  className?: string;
  style?: CSSProperties;
  onScroll?: (e: UIEvent<HTMLDivElement>) => void;
  /** Called with the rendered range (e.g. to prefetch). */
  onRangeChange?: (start: number, end: number) => void;
  ref?: Ref<VirtualListHandle>;
}

export function VirtualList<T>({
  items,
  renderRow,
  rowHeight = 36,
  measure = false,
  getKey,
  overscan = 240,
  initialHeight = 600,
  role,
  rowRole,
  className,
  style,
  onScroll,
  onRangeChange,
  ref,
  ...aria
}: VirtualListProps<T>) {
  const scrollerRef = useRef<HTMLDivElement | null>(null);
  const [scrollTop, setScrollTop] = useState(0);
  const [viewport, setViewport] = useState(initialHeight);
  const [, setVersion] = useState(0);
  const fixed = typeof rowHeight === 'number' && !measure ? rowHeight : null;

  // Offsets for variable/measured heights; rebuilt when the items change.
  const index = useMemo(
    () =>
      fixed !== null
        ? null
        : new OffsetIndex(items.length, (i) => (typeof rowHeight === 'function' ? rowHeight(items[i]!, i) : rowHeight)),
    // rowHeight functions are usually inline; rebuild on items/fixed change only.
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [items, fixed],
  );

  const total = fixed !== null ? items.length * fixed : (index?.total() ?? 0);
  const range =
    fixed !== null
      ? fixedRange(items.length, fixed, scrollTop, viewport, overscan)
      : visibleRange(index!, scrollTop, viewport, overscan);

  const offsetOf = useCallback((i: number) => (fixed !== null ? i * fixed : (index?.offsetOf(i) ?? 0)), [fixed, index]);
  const heightOf = useCallback((i: number) => (fixed !== null ? fixed : (index?.heightOf(i) ?? 0)), [fixed, index]);

  // Viewport size.
  useIsoLayoutEffect(() => {
    const el = scrollerRef.current;
    if (!el) return;
    const read = () => {
      if (el.clientHeight > 0) setViewport(el.clientHeight);
    };
    read();
    if (typeof ResizeObserver === 'undefined') return;
    const ro = new ResizeObserver(read);
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  // Row measurement. The observer is created lazily from the row refs: refs attach before any
  // effect of this component runs, so an observer made in an effect would miss the first rows.
  const roRef = useRef<ResizeObserver | null>(null);
  const indexRef = useLatest(index);
  const observer = useCallback((): ResizeObserver | null => {
    if (roRef.current || typeof ResizeObserver === 'undefined') return roRef.current;
    roRef.current = new ResizeObserver((entries) => {
      let changed = false;
      for (const e of entries) {
        const el = e.target as HTMLElement;
        if (!el.isConnected) continue;
        const i = Number(el.dataset.index);
        const h = e.borderBoxSize?.[0]?.blockSize ?? el.offsetHeight;
        if (indexRef.current?.setHeight(i, h)) changed = true;
      }
      if (changed) setVersion((v) => v + 1);
    });
    return roRef.current;
  }, [indexRef]);
  useEffect(
    () => () => {
      roRef.current?.disconnect();
      roRef.current = null;
    },
    [],
  );

  const measureRef = useCallback(
    (el: HTMLDivElement | null) => {
      if (!el || !measure) return;
      const ro = observer();
      ro?.observe(el);
      const i = Number(el.dataset.index);
      if (index?.setHeight(i, el.offsetHeight || index.heightOf(i))) setVersion((v) => v + 1);
      // Rows scrolled out of the window unmount: stop observing them (the observer would
      // otherwise keep every detached row alive).
      return () => ro?.unobserve(el);
    },
    [measure, index, observer],
  );

  useEffect(() => {
    onRangeChange?.(range.start, range.end);
  }, [range.start, range.end, onRangeChange]);

  useImperativeHandle(
    ref,
    () => ({
      scrollToIndex(i: number, align: ScrollAlign = 'auto') {
        const el = scrollerRef.current;
        if (!el || items.length === 0) return;
        const idx = Math.max(0, Math.min(i, items.length - 1));
        const target = scrollTargetFor(offsetOf(idx), heightOf(idx), el.scrollTop, el.clientHeight || viewport, align);
        if (target !== null) {
          el.scrollTop = target;
          setScrollTop(target);
        }
      },
      scrollToOffset(offset: number) {
        const el = scrollerRef.current;
        if (!el) return;
        el.scrollTop = offset;
        setScrollTop(offset);
      },
      get element() {
        return scrollerRef.current;
      },
    }),
    [items.length, offsetOf, heightOf, viewport],
  );

  const rows: ReactNode[] = [];
  for (let i = range.start; i < range.end; i++) {
    const item = items[i]!;
    rows.push(
      <div
        key={getKey ? getKey(item, i) : i}
        data-index={i}
        ref={measure ? measureRef : undefined}
        role={rowRole}
        className="b-vlist__row"
        style={{
          transform: `translateY(${offsetOf(i)}px)`,
          height: measure ? undefined : heightOf(i),
        }}
      >
        {renderRow(item, i)}
      </div>,
    );
  }

  return (
    <div
      ref={scrollerRef}
      className={cx('b-vlist', className)}
      style={style}
      role={role}
      aria-label={aria['aria-label']}
      onScroll={(e) => {
        setScrollTop(e.currentTarget.scrollTop);
        onScroll?.(e);
      }}
    >
      <div className="b-vlist__inner" style={{ height: total }}>
        {rows}
      </div>
    </div>
  );
}
