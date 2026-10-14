/**
 * Two resizable panes with a draggable / keyboard-operable separator. One pane has a size in
 * px (persisted under `storageKey`), the other takes the rest.
 */
import { useRef, useState, type KeyboardEvent, type PointerEvent as ReactPointerEvent, type ReactNode } from 'react';
import { cx, readStored, writeStored } from './hooks';
import { useT } from './i18n';

export interface SplitterProps {
  children: [ReactNode, ReactNode];
  /** 'horizontal': panes side by side (vertical bar); 'vertical': stacked. */
  direction?: 'horizontal' | 'vertical';
  /** Which pane has the explicit size. */
  fixed?: 'first' | 'second';
  defaultSize: number;
  min?: number;
  max?: number;
  /** Persist the size in localStorage ("brulion.split.<key>"). */
  storageKey?: string;
  /** Controlled size (px). */
  size?: number;
  onSizeChange?: (size: number) => void;
  /** Hide the fixed pane (and the bar). */
  collapsed?: boolean;
  /** Keyboard step in px (Shift: ×4). */
  step?: number;
  'aria-label'?: string;
  className?: string;
}

export function clampSize(v: number, min: number, max: number): number {
  return Math.round(Math.min(Math.max(v, min), Math.max(min, max)));
}

/**
 * New size after a key press on the separator; null for keys it ignores. Arrow keys move
 * the bar (so for a fixed second pane, ArrowLeft grows it).
 */
export function keyResize(
  size: number,
  key: string,
  opts: { min: number; max: number; step: number; direction: 'horizontal' | 'vertical'; fixed: 'first' | 'second'; defaultSize: number },
): number | null {
  const fwd = opts.direction === 'horizontal' ? 'ArrowRight' : 'ArrowDown';
  const back = opts.direction === 'horizontal' ? 'ArrowLeft' : 'ArrowUp';
  const sign = opts.fixed === 'first' ? 1 : -1;
  switch (key) {
    case fwd:
      return clampSize(size + sign * opts.step, opts.min, opts.max);
    case back:
      return clampSize(size - sign * opts.step, opts.min, opts.max);
    case 'Home':
      return opts.min;
    case 'End':
      return opts.max;
    case 'Enter':
      return clampSize(opts.defaultSize, opts.min, opts.max);
    default:
      return null;
  }
}

const KEY_PREFIX = 'brulion.split.';

export function Splitter({
  children,
  direction = 'horizontal',
  fixed = 'first',
  defaultSize,
  min = 120,
  max = 900,
  storageKey,
  size: controlled,
  onSizeChange,
  collapsed = false,
  step = 16,
  className,
  ...aria
}: SplitterProps) {
  const t = useT();
  const [inner, setInner] = useState(() =>
    clampSize(storageKey ? readStored<number>(KEY_PREFIX + storageKey, defaultSize) : defaultSize, min, max),
  );
  const size = controlled ?? inner;
  const rootRef = useRef<HTMLDivElement | null>(null);
  const drag = useRef<{ start: number; startSize: number; id: number } | null>(null);
  const [dragging, setDragging] = useState(false);
  const horizontal = direction === 'horizontal';

  const effectiveMax = () => {
    const el = rootRef.current;
    if (!el) return max;
    const room = (horizontal ? el.clientWidth : el.clientHeight) - min;
    return room > min ? Math.min(max, room) : max;
  };

  const commit = (v: number, persist: boolean) => {
    if (controlled === undefined) setInner(v);
    onSizeChange?.(v);
    if (persist && storageKey) writeStored(KEY_PREFIX + storageKey, v);
  };

  const onPointerDown = (e: ReactPointerEvent<HTMLDivElement>) => {
    if (e.pointerType === 'mouse' && e.button !== 0) return;
    e.preventDefault();
    e.currentTarget.setPointerCapture?.(e.pointerId);
    drag.current = { start: horizontal ? e.clientX : e.clientY, startSize: size, id: e.pointerId };
    setDragging(true);
  };
  const onPointerMove = (e: ReactPointerEvent<HTMLDivElement>) => {
    const d = drag.current;
    if (!d || d.id !== e.pointerId) return;
    const delta = (horizontal ? e.clientX : e.clientY) - d.start;
    commit(clampSize(d.startSize + (fixed === 'first' ? delta : -delta), min, effectiveMax()), false);
  };
  const endDrag = (e: ReactPointerEvent<HTMLDivElement>) => {
    const d = drag.current;
    if (!d || d.id !== e.pointerId) return;
    drag.current = null;
    setDragging(false);
    if (storageKey) writeStored(KEY_PREFIX + storageKey, size);
  };
  const onKeyDown = (e: KeyboardEvent<HTMLDivElement>) => {
    const next = keyResize(size, e.key, {
      min,
      max: effectiveMax(),
      step: e.shiftKey ? step * 4 : step,
      direction,
      fixed,
      defaultSize,
    });
    if (next === null) return;
    e.preventDefault();
    commit(next, true);
  };

  const fixedStyle = { flex: `0 0 ${size}px`, [horizontal ? 'width' : 'height']: size };
  const [first, second] = children;

  return (
    <div
      ref={rootRef}
      className={cx('b-split', horizontal ? 'b-split--h' : 'b-split--v', dragging && 'is-dragging', collapsed && 'is-collapsed', className)}
    >
      {!(collapsed && fixed === 'first') && (
        <div className={cx('b-split__pane', fixed === 'first' ? 'is-fixed' : 'is-flex')} style={fixed === 'first' ? fixedStyle : undefined}>
          {first}
        </div>
      )}
      {!collapsed && (
        <div
          className="b-split__bar"
          role="separator"
          tabIndex={0}
          aria-orientation={horizontal ? 'vertical' : 'horizontal'}
          aria-valuenow={size}
          aria-valuemin={min}
          aria-valuemax={max}
          aria-label={aria['aria-label'] ?? t('components.splitter.resize')}
          onPointerDown={onPointerDown}
          onPointerMove={onPointerMove}
          onPointerUp={endDrag}
          onPointerCancel={endDrag}
          onDoubleClick={() => commit(clampSize(defaultSize, min, max), true)}
          onKeyDown={onKeyDown}
        />
      )}
      {!(collapsed && fixed === 'second') && (
        <div className={cx('b-split__pane', fixed === 'second' ? 'is-fixed' : 'is-flex')} style={fixed === 'second' ? fixedStyle : undefined}>
          {second}
        </div>
      )}
    </div>
  );
}
