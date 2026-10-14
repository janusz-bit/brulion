/**
 * Modal dialog: portal + backdrop, focus trap, Escape, focus restore, aria-modal. Below 640 px
 * it renders as a bottom sheet with a drag handle (drag down to dismiss).
 */
import {
  useEffect,
  useId,
  useRef,
  useState,
  type CSSProperties,
  type KeyboardEvent,
  type PointerEvent as ReactPointerEvent,
  type ReactNode,
  type RefObject,
} from 'react';
import { createPortal } from 'react-dom';
import { Button, IconButton } from './Button';
import { focusQuietly, initialFocusTarget, trapTabKey } from './focus';
import { cx, NARROW_QUERY, useLatest, useMediaQuery, useScrollLock } from './hooks';
import { useT } from './i18n';
import { isTopLayer, pushLayer, type Layer } from './layers';

export type ModalSize = 'sm' | 'md' | 'lg' | 'full';

export interface ModalProps {
  open: boolean;
  onClose: () => void;
  title: ReactNode;
  /** Short text under the title (also aria-describedby). */
  description?: ReactNode;
  children?: ReactNode;
  /** Action row (buttons), right-aligned; sticks to the bottom of the sheet on phones. */
  footer?: ReactNode;
  size?: ModalSize;
  /** Element to focus on open; default: `[data-autofocus]` or the first focusable control. */
  initialFocus?: RefObject<HTMLElement | null>;
  closeOnBackdrop?: boolean;
  /** Hide the × button (the dialog must offer another way out). */
  hideClose?: boolean;
  /** 'auto': bottom sheet on narrow screens; 'never': always centred; 'always': sheet. */
  sheet?: 'auto' | 'never' | 'always';
  role?: 'dialog' | 'alertdialog';
  className?: string;
  /** Extra class for the body (scrolling area). */
  bodyClassName?: string;
  /** Portal target (default document.body). */
  container?: Element | null;
  /**
   * Render the dialog panel in place: no portal, backdrop, focus trap, scroll lock or
   * Escape handling (gallery / screenshots, embedding a dialog's look in a page).
   */
  inline?: boolean;
}

export function Modal(props: ModalProps) {
  if (!props.open) return null;
  return <ModalImpl {...props} />;
}

const DISMISS_DISTANCE = 96;

function ModalImpl({
  onClose,
  title,
  description,
  children,
  footer,
  size = 'md',
  initialFocus,
  closeOnBackdrop = true,
  hideClose = false,
  sheet = 'auto',
  role = 'dialog',
  className,
  bodyClassName,
  container,
  inline = false,
}: ModalProps) {
  const t = useT();
  const rootRef = useRef<HTMLDivElement | null>(null);
  const dialogRef = useRef<HTMLDivElement | null>(null);
  const titleId = useId();
  const descId = useId();
  const narrow = useMediaQuery(NARROW_QUERY);
  const asSheet = sheet === 'always' || (sheet === 'auto' && narrow);
  const onCloseRef = useLatest(onClose);
  const layerRef = useRef<Layer | null>(null);
  const [drag, setDrag] = useState<{ startY: number; dy: number; pointerId: number } | null>(null);

  useScrollLock(!inline);

  // Escape (only when topmost); outside presses never reach below a modal.
  useEffect(() => {
    if (inline) return;
    const layer: Layer = {
      contains: (node) => !!rootRef.current?.contains(node),
      onEscape: () => onCloseRef.current(),
    };
    layerRef.current = layer;
    return pushLayer(layer);
  }, [onCloseRef, inline]);

  // Initial focus + restore.
  useEffect(() => {
    if (inline) return;
    const previous = document.activeElement instanceof HTMLElement ? document.activeElement : null;
    const dialog = dialogRef.current;
    if (dialog) focusQuietly(initialFocus?.current ?? initialFocusTarget(dialog));
    return () => {
      if (previous?.isConnected) focusQuietly(previous);
    };
    // Mount/unmount only.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Keep focus inside while this is the top layer (e.g. after a click on the backdrop).
  useEffect(() => {
    if (inline) return;
    const onFocusIn = (e: FocusEvent) => {
      const dialog = dialogRef.current;
      const target = e.target;
      if (!dialog || !(target instanceof Node) || dialog.contains(target)) return;
      if (!layerRef.current || !isTopLayer(layerRef.current)) return;
      focusQuietly(initialFocusTarget(dialog));
    };
    document.addEventListener('focusin', onFocusIn);
    return () => document.removeEventListener('focusin', onFocusIn);
  }, [inline]);

  const onKeyDown = (e: KeyboardEvent<HTMLDivElement>) => {
    const dialog = dialogRef.current;
    if (inline || !dialog) return;
    // React bubbles events from portals (a popover opened inside the dialog) through the
    // component tree; Tab in such a popover is the popover's business, not the trap's.
    if (!(e.target instanceof Node) || !dialog.contains(e.target)) return;
    trapTabKey(e, dialog);
  };

  // Bottom-sheet drag-to-dismiss (handle and header).
  const onPointerDown = (e: ReactPointerEvent<HTMLDivElement>) => {
    if (!asSheet || (e.pointerType === 'mouse' && e.button !== 0)) return;
    if (e.target instanceof Element && e.target.closest('button, a, input, select, textarea')) return;
    e.currentTarget.setPointerCapture?.(e.pointerId);
    setDrag({ startY: e.clientY, dy: 0, pointerId: e.pointerId });
  };
  const onPointerMove = (e: ReactPointerEvent<HTMLDivElement>) => {
    if (!drag || e.pointerId !== drag.pointerId) return;
    setDrag({ ...drag, dy: Math.max(0, e.clientY - drag.startY) });
  };
  const onPointerUp = (e: ReactPointerEvent<HTMLDivElement>) => {
    if (!drag || e.pointerId !== drag.pointerId) return;
    const dismiss = drag.dy > DISMISS_DISTANCE;
    setDrag(null);
    if (dismiss) onCloseRef.current();
  };

  const target = container ?? (typeof document !== 'undefined' ? document.body : null);
  if (!target && !inline) return null;

  const dialogStyle: CSSProperties | undefined =
    drag && drag.dy > 0 ? { transform: `translateY(${drag.dy}px)`, transition: 'none' } : undefined;

  const tree = (
    <div ref={rootRef} className={cx('b-modal-root', asSheet && 'is-sheet', inline && 'is-inline')}>
      {!inline && (
        <div
          className="b-modal__backdrop"
          aria-hidden="true"
          style={drag ? { opacity: Math.max(0.2, 1 - drag.dy / 400) } : undefined}
          onClick={closeOnBackdrop ? () => onCloseRef.current() : undefined}
        />
      )}
      <div
        ref={dialogRef}
        role={role}
        aria-modal={inline ? undefined : 'true'}
        aria-labelledby={titleId}
        aria-describedby={description ? descId : undefined}
        tabIndex={-1}
        className={cx('b-modal', `b-modal--${size}`, className)}
        style={dialogStyle}
        onKeyDown={onKeyDown}
      >
        <div
          className="b-modal__header"
          onPointerDown={onPointerDown}
          onPointerMove={onPointerMove}
          onPointerUp={onPointerUp}
          onPointerCancel={() => setDrag(null)}
        >
          {asSheet && <div className="b-modal__handle" aria-hidden="true" title={t('components.dialog.dragHint')} />}
          <div className="b-modal__titles">
            <h2 id={titleId} className="b-modal__title">
              {title}
            </h2>
            {description && (
              <p id={descId} className="b-modal__desc">
                {description}
              </p>
            )}
          </div>
          {!hideClose && (
            <IconButton
              icon="x"
              label={t('components.dialog.close')}
              className="b-modal__close"
              data-dialog-close=""
              onClick={() => onCloseRef.current()}
            />
          )}
        </div>
        <div className={cx('b-modal__body', bodyClassName)}>{children}</div>
        {footer && <div className="b-modal__footer">{footer}</div>}
      </div>
    </div>
  );
  return inline || !target ? tree : createPortal(tree, target);
}

// ---------------------------------------------------------------------------------------------
// ConfirmDialog
// ---------------------------------------------------------------------------------------------

export interface ConfirmDialogProps {
  open: boolean;
  title: ReactNode;
  body?: ReactNode;
  confirmLabel?: string;
  cancelLabel?: string;
  /** Destructive action: red confirm button, initial focus on Cancel. */
  danger?: boolean;
  /**
   * May return a promise: the button shows a spinner until it settles. A rejection keeps the
   * dialog open (the caller reports the error, e.g. with a toast); it is not rethrown.
   */
  onConfirm: () => void | Promise<unknown>;
  onCancel: () => void;
  container?: Element | null;
  inline?: boolean;
}

export function ConfirmDialog({
  open,
  title,
  body,
  confirmLabel,
  cancelLabel,
  danger = false,
  onConfirm,
  onCancel,
  container,
  inline,
}: ConfirmDialogProps) {
  const t = useT();
  const [busy, setBusy] = useState(false);
  const cancelRef = useRef<HTMLButtonElement | null>(null);
  const confirmRef = useRef<HTMLButtonElement | null>(null);
  const mounted = useRef(true);
  useEffect(() => {
    mounted.current = true;
    return () => {
      mounted.current = false;
    };
  }, []);

  const confirm = async () => {
    const r = onConfirm();
    if (r && typeof (r as Promise<unknown>).then === 'function') {
      setBusy(true);
      try {
        await r;
      } catch {
        // The caller owns the promise and its error handling; don't surface it as unhandled.
      } finally {
        if (mounted.current) setBusy(false);
      }
    }
  };

  return (
    <Modal
      open={open}
      onClose={() => {
        if (!busy) onCancel();
      }}
      title={title}
      size="sm"
      role="alertdialog"
      initialFocus={danger ? cancelRef : confirmRef}
      container={container}
      inline={inline}
      footer={
        <>
          <Button ref={cancelRef} variant="ghost" onClick={onCancel} disabled={busy}>
            {cancelLabel ?? t('components.confirm.cancel')}
          </Button>
          <Button ref={confirmRef} variant={danger ? 'danger' : 'primary'} loading={busy} onClick={() => void confirm()}>
            {confirmLabel ?? t('components.confirm.confirm')}
          </Button>
        </>
      }
    >
      {body && <div className="b-confirm__body">{body}</div>}
    </Modal>
  );
}
