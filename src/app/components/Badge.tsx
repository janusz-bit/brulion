import type { ComponentPropsWithRef, ReactNode } from 'react';
import { cx } from './hooks';
import { Icon, renderIcon, type IconName, type IconSlot } from './Icon';
import { useT } from './i18n';

export type BadgeVariant =
  | 'neutral'
  | 'accent'
  | 'info'
  | 'success'
  | 'warning'
  | 'danger'
  | 'reminder'
  | 'alarm'
  | 'due'
  | 'overdue'
  | 'tag'
  | 'outline';

/** Default icons per variant (pass `icon={null}` to drop). */
const DEFAULT_ICON: Partial<Record<BadgeVariant, IconName>> = {
  reminder: 'bell',
  alarm: 'alarm',
  due: 'calendar',
  overdue: 'alert',
};

export interface BadgeProps {
  variant?: BadgeVariant;
  size?: 'sm' | 'md';
  icon?: IconSlot;
  children?: ReactNode;
  className?: string;
  title?: string;
}

/** Small static label: counts, statuses, dates ("jutro 9:00"), tags. */
export function Badge({ variant = 'neutral', size = 'md', icon, children, className, title }: BadgeProps) {
  const ic = icon === undefined ? DEFAULT_ICON[variant] : icon;
  return (
    <span className={cx('b-badge', `b-badge--${variant}`, `b-badge--${size}`, className)} title={title}>
      {ic ? <span className="b-badge__icon">{renderIcon(ic, size === 'sm' ? 12 : 13)}</span> : null}
      {variant === 'tag' && typeof children === 'string' && !children.startsWith('#') ? `#${children}` : children}
    </span>
  );
}

export interface ChipProps extends Omit<ComponentPropsWithRef<'button'>, 'children'> {
  variant?: BadgeVariant;
  icon?: IconSlot;
  children: ReactNode;
  /** Toggle chips (filters): sets aria-pressed. */
  selected?: boolean;
  /** Renders a remove button; the chip body stays clickable when `onClick` is given. */
  onRemove?: () => void;
  removeLabel?: string;
  size?: 'sm' | 'md';
}

/** Interactive badge: filter toggles, removable tags, quick picks. */
export function Chip({
  variant = 'neutral',
  icon,
  children,
  selected,
  onRemove,
  removeLabel,
  size = 'md',
  className,
  onClick,
  type = 'button',
  disabled,
  ...rest
}: ChipProps) {
  const t = useT();
  const ic = icon === undefined ? DEFAULT_ICON[variant] : icon;
  const label = variant === 'tag' && typeof children === 'string' && !children.startsWith('#') ? `#${children}` : children;
  const body = (
    <>
      {ic ? <span className="b-badge__icon">{renderIcon(ic, size === 'sm' ? 12 : 14)}</span> : null}
      <span className="b-chip__label">{label}</span>
    </>
  );
  const cls = cx('b-chip', `b-badge--${variant}`, `b-chip--${size}`, selected && 'is-selected', disabled && 'is-disabled', className);
  if (!onRemove) {
    return (
      <button {...rest} type={type} className={cls} aria-pressed={selected} onClick={onClick} disabled={disabled}>
        {body}
      </button>
    );
  }
  return (
    <span className={cx(cls, 'b-chip--removable')}>
      {onClick ? (
        <button {...rest} type={type} className="b-chip__main" aria-pressed={selected} onClick={onClick} disabled={disabled}>
          {body}
        </button>
      ) : (
        <span className="b-chip__main">{body}</span>
      )}
      <button
        type="button"
        className="b-chip__remove"
        aria-label={removeLabel ?? `${t('components.remove')}: ${typeof label === 'string' ? label : ''}`.trim()}
        onClick={onRemove}
        disabled={disabled}
      >
        <Icon name="x" size={12} />
      </button>
    </span>
  );
}
