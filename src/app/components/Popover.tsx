/**
 * Anchored floating panel rendered in a portal (position: fixed). Flips when there is no
 * room, follows scroll/resize, closes on outside press / Escape and gives focus back.
 */
import {
  useCallback,
  useEffect,
  useRef,
  useState,
  type CSSProperties,
  type ReactNode,
  type RefObject,
} from 'react';
import { createPortal } from 'react-dom';
import { focusQuietly, initialFocusTarget } from './focus';
import { cx, useIsoLayoutEffect, useLatest } from './hooks';
import { isTopLayer, pushLayer, type Layer } from './layers';
import { computePosition, type Placement } from './position';

/** Something with a position: an element, or a virtual anchor (caret rect, pointer). */
export interface VirtualAnchor {
  getBoundingClientRect(): { top: number; left: number; width: number; height: number };
  /** Optional: what counts as "inside" the anchor for outside-press detection. */
  contains?(node: Node): boolean;
}
export type AnchorLike = Element | VirtualAnchor;
export type AnchorProp = AnchorLike | null | undefined | RefObject<AnchorLike | null>;

export type PopoverCloseReason = 'escape' | 'outside' | 'focus-out' | 'select';

export interface PopoverProps {
  open: boolean;
  onClose: (reason: PopoverCloseReason) => void;
  anchor: AnchorProp;
  placement?: Placement;
  /** Gap between anchor and panel (px). */
  offset?: number;
  /** Make the panel at least as wide as the anchor (selects, comboboxes). */
  matchWidth?: boolean;
  /** Focus the first focusable element (or `[data-autofocus]`) when opening. Default true. */
  autoFocus?: boolean;
  /** Give focus back to the previously focused element on close. Default true. */
  restoreFocus?: boolean;
  closeOnOutside?: boolean;
  /** Close when focus moves out of the panel and the anchor. Default true. */
  closeOnFocusOut?: boolean;
  role?: 'dialog' | 'menu' | 'listbox' | 'tooltip' | 'region' | 'none';
  'aria-label'?: string;
  'aria-labelledby'?: string;
  id?: string;
  className?: string;
  style?: CSSProperties;
  /** Portal target; default: the anchor's themed subtree (gallery) or document.body. */
  container?: Element | null;
  /** Visual style: padded card (default) or bare (menus provide their own padding). */
  padded?: boolean;
  /**
   * Render in place (no portal, no positioning, no outside/Escape handling) — for embedding
   * the panel in a page, e.g. the component gallery.
   */
  inline?: boolean;
  children: ReactNode;
}

export function resolveAnchor(anchor: AnchorProp): AnchorLike | null {
  if (!anchor) return null;
  if ('current' in anchor) return anchor.current;
  return anchor;
}

function anchorContains(a: AnchorLike | null, node: Node): boolean {
  if (!a) return false;
  if (a instanceof Element) return a.contains(node);
  return a.contains?.(node) ?? false;
}

/**
 * Portal target: the open modal the anchor lives in (content outside an aria-modal dialog is
 * inert for screen readers), else a themed subtree (e.g. a gallery column), else the body.
 */
export function portalTarget(anchor: AnchorLike | null, container?: Element | null): Element | null {
  if (container) return container;
  if (typeof document === 'undefined') return null;
  if (anchor instanceof Element) {
    const modal = anchor.closest('.b-modal-root:not(.is-inline)');
    if (modal) return modal;
    const scope = anchor.closest('[data-theme-scope]');
    if (scope) return scope;
  }
  return document.body;
}

interface Pos {
  top: number;
  left: number;
  placement: Placement;
  maxHeight: number;
  minWidth?: number;
  ready: boolean;
}

export function Popover({
  open,
  onClose,
  anchor,
  placement = 'bottom-start',
  offset = 6,
  matchWidth = false,
  autoFocus = true,
  restoreFocus = true,
  closeOnOutside = true,
  closeOnFocusOut = true,
  role = 'dialog',
  id,
  className,
  style,
  container,
  padded = true,
  inline = false,
  children,
  ...aria
}: PopoverProps) {
  if (!open) return null;
  if (inline) {
    return (
      <div
        id={id}
        role={role === 'none' ? undefined : role}
        aria-label={aria['aria-label']}
        aria-labelledby={aria['aria-labelledby']}
        className={cx('b-popover', 'b-popover--inline', 'is-ready', padded && 'b-popover--padded', className)}
        style={style}
      >
        {children}
      </div>
    );
  }
  return (
    <PopoverPanel
      onClose={onClose}
      anchor={anchor}
      placement={placement}
      offset={offset}
      matchWidth={matchWidth}
      autoFocus={autoFocus}
      restoreFocus={restoreFocus}
      closeOnOutside={closeOnOutside}
      closeOnFocusOut={closeOnFocusOut}
      role={role}
      id={id}
      className={className}
      style={style}
      container={container}
      padded={padded}
      {...aria}
    >
      {children}
    </PopoverPanel>
  );
}

type PanelProps = Omit<PopoverProps, 'open' | 'inline'> & Required<Pick<PopoverProps, 'placement' | 'offset'>>;

function PopoverPanel({
  onClose,
  anchor,
  placement,
  offset,
  matchWidth,
  autoFocus,
  restoreFocus,
  closeOnOutside,
  closeOnFocusOut,
  role,
  id,
  className,
  style,
  container,
  padded,
  children,
  ...aria
}: PanelProps) {
  const ref = useRef<HTMLDivElement | null>(null);
  const [pos, setPos] = useState<Pos>({ top: 0, left: 0, placement, maxHeight: 0, ready: false });
  const onCloseRef = useLatest(onClose);
  const anchorRef = useLatest(anchor);
  const layerRef = useRef<Layer | null>(null);

  const update = useCallback(() => {
    const el = ref.current;
    const a = resolveAnchor(anchorRef.current);
    if (!el || !a) return;
    const r = a.getBoundingClientRect();
    const vw = document.documentElement.clientWidth || window.innerWidth;
    const vh = window.innerHeight || document.documentElement.clientHeight;
    const size = { width: el.offsetWidth, height: el.offsetHeight };
    const p = computePosition(r, size, { width: vw, height: vh }, placement, offset);
    setPos((prev) => {
      const next: Pos = {
        top: p.top,
        left: p.left,
        placement: p.placement,
        maxHeight: p.maxHeight,
        minWidth: matchWidth ? r.width : undefined,
        ready: true,
      };
      return prev.top === next.top &&
        prev.left === next.left &&
        prev.placement === next.placement &&
        prev.maxHeight === next.maxHeight &&
        prev.minWidth === next.minWidth &&
        prev.ready
        ? prev
        : next;
    });
  }, [anchorRef, placement, offset, matchWidth]);

  // Measure before paint, then follow layout changes.
  useIsoLayoutEffect(() => {
    update();
    let frame = 0;
    const schedule = () => {
      cancelAnimationFrame(frame);
      frame = requestAnimationFrame(update);
    };
    window.addEventListener('resize', schedule);
    window.addEventListener('scroll', schedule, true);
    let ro: ResizeObserver | undefined;
    if (typeof ResizeObserver !== 'undefined') {
      ro = new ResizeObserver(schedule);
      if (ref.current) ro.observe(ref.current);
      const a = resolveAnchor(anchorRef.current);
      if (a instanceof Element) ro.observe(a);
    }
    return () => {
      cancelAnimationFrame(frame);
      window.removeEventListener('resize', schedule);
      window.removeEventListener('scroll', schedule, true);
      ro?.disconnect();
    };
  }, [update, anchorRef]);

  // Layer: Escape + outside presses.
  useEffect(() => {
    const layer: Layer = {
      contains: (node) => !!ref.current?.contains(node) || anchorContains(resolveAnchor(anchorRef.current), node),
      origin: () => {
        const a = resolveAnchor(anchorRef.current);
        return a instanceof Element ? a : null;
      },
      onEscape: () => onCloseRef.current('escape'),
      onOutside: closeOnOutside ? () => onCloseRef.current('outside') : undefined,
    };
    layerRef.current = layer;
    const remove = pushLayer(layer);
    return () => {
      remove();
      layerRef.current = null;
    };
  }, [anchorRef, closeOnOutside, onCloseRef]);

  // Focus in, and back out on unmount.
  useEffect(() => {
    const previous = document.activeElement instanceof HTMLElement ? document.activeElement : null;
    if (autoFocus && ref.current) focusQuietly(initialFocusTarget(ref.current));
    return () => {
      if (!restoreFocus) return;
      const active = document.activeElement;
      const lost = !active || active === document.body || !active.isConnected || !!ref.current?.contains(active);
      if (!lost) return;
      const a = resolveAnchor(anchorRef.current);
      const target = previous?.isConnected ? previous : a instanceof HTMLElement ? a : null;
      focusQuietly(target);
    };
    // Mount/unmount only.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Close when focus leaves both the panel and the anchor (Tab out of a non-modal popover).
  useEffect(() => {
    if (!closeOnFocusOut) return;
    const onFocusIn = (e: FocusEvent) => {
      const t = e.target;
      if (!(t instanceof Node)) return;
      if (ref.current?.contains(t) || anchorContains(resolveAnchor(anchorRef.current), t)) return;
      // An overlay opened above this one (nested popover, dialog) owns focus now: leave it to
      // that layer. Focus moving anywhere else — including elsewhere in a dialog this popover
      // was opened from — closes it.
      if (layerRef.current && !isTopLayer(layerRef.current)) return;
      onCloseRef.current('focus-out');
    };
    document.addEventListener('focusin', onFocusIn);
    return () => document.removeEventListener('focusin', onFocusIn);
  }, [closeOnFocusOut, anchorRef, onCloseRef]);

  const target = portalTarget(resolveAnchor(anchor), container);
  if (!target) return null;
  const panel = (
    <div
      ref={ref}
      id={id}
      role={role === 'none' ? undefined : role}
      aria-label={aria['aria-label']}
      aria-labelledby={aria['aria-labelledby']}
      tabIndex={-1}
      data-placement={pos.placement}
      className={cx('b-popover', padded && 'b-popover--padded', pos.ready && 'is-ready', className)}
      style={{
        ...style,
        top: pos.top,
        left: pos.left,
        minWidth: pos.minWidth,
        ['--b-avail-h' as string]: pos.ready ? `${Math.max(120, pos.maxHeight)}px` : undefined,
      }}
    >
      {children}
    </div>
  );
  return createPortal(panel, target);
}
