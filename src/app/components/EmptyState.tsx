import type { ReactNode } from 'react';
import { cx } from './hooks';
import { renderIcon, type IconSlot } from './Icon';

export interface EmptyStateProps {
  icon?: IconSlot;
  title: ReactNode;
  body?: ReactNode;
  /** Usually a <Button>. */
  action?: ReactNode;
  size?: 'sm' | 'md';
  className?: string;
}

/** Friendly placeholder for empty lists and views. */
export function EmptyState({ icon, title, body, action, size = 'md', className }: EmptyStateProps) {
  return (
    <div className={cx('b-empty', `b-empty--${size}`, className)}>
      {icon && <div className="b-empty__icon">{renderIcon(icon, size === 'sm' ? 22 : 28)}</div>}
      <div className="b-empty__title">{title}</div>
      {body && <div className="b-empty__body">{body}</div>}
      {action && <div className="b-empty__action">{action}</div>}
    </div>
  );
}
