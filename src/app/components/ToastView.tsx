import type { ReactNode } from 'react';
import { IconButton } from './Button';
import { cx } from './hooks';
import { Icon, type IconName } from './Icon';
import { useT } from './i18n';

export type ToastKind = 'info' | 'success' | 'warning' | 'error' | 'reminder';

/** Structural toast shape; the shell's store Toast type is compatible with it. */
export interface ToastData {
  id?: string;
  kind?: ToastKind;
  title?: string;
  message: ReactNode;
  action?: { label: string; onClick: () => void };
}

const KIND_ICON: Record<ToastKind, IconName> = {
  info: 'info',
  success: 'check-circle',
  warning: 'warning',
  error: 'alert',
  reminder: 'bell-ring',
};

export interface ToastViewProps {
  toast: ToastData;
  onClose?: () => void;
  className?: string;
}

/**
 * One toast (presentational). Timers, stacking order and the store live in the shell; wrap
 * several in <ToastStack> for the standard bottom/right placement.
 */
export function ToastView({ toast, onClose, className }: ToastViewProps) {
  const t = useT();
  const kind = toast.kind ?? 'info';
  const urgent = kind === 'error' || kind === 'reminder';
  return (
    <div
      className={cx('b-toast', `b-toast--${kind}`, className)}
      role={urgent ? 'alert' : 'status'}
      aria-live={urgent ? 'assertive' : 'polite'}
    >
      <span className="b-toast__icon">
        <Icon name={KIND_ICON[kind]} size={18} />
      </span>
      <div className="b-toast__text">
        {toast.title && <div className="b-toast__title">{toast.title}</div>}
        <div className="b-toast__message">{toast.message}</div>
      </div>
      {toast.action && (
        <button
          type="button"
          className="b-toast__action"
          onClick={() => {
            toast.action?.onClick();
            onClose?.();
          }}
        >
          {toast.action.label}
        </button>
      )}
      {onClose && <IconButton icon="x" size="sm" label={t('components.toast.dismiss')} onClick={onClose} className="b-toast__close" />}
    </div>
  );
}

/** Fixed container for toasts (bottom-centre on phones, bottom-right on desktop). */
export function ToastStack({ children, className }: { children: ReactNode; className?: string }) {
  return <div className={cx('b-toast-stack', className)}>{children}</div>;
}
