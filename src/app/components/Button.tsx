import type { ComponentPropsWithRef, ReactNode } from 'react';
import { cx } from './hooks';
import { renderIcon, type IconSlot } from './Icon';
import { Spinner } from './Spinner';

export type ButtonVariant = 'primary' | 'secondary' | 'ghost' | 'danger';
export type ButtonSize = 'sm' | 'md' | 'lg';

export interface ButtonProps extends ComponentPropsWithRef<'button'> {
  variant?: ButtonVariant;
  size?: ButtonSize;
  /** Shows a spinner, sets aria-busy and blocks clicks (the label stays for a stable width). */
  loading?: boolean;
  /** Leading icon. */
  icon?: IconSlot;
  /** Trailing icon (e.g. 'chevron-down'). */
  iconEnd?: IconSlot;
  /** Full width. */
  block?: boolean;
  children?: ReactNode;
}

const ICON_SIZE: Record<ButtonSize, number> = { sm: 14, md: 16, lg: 18 };

export function Button({
  variant = 'secondary',
  size = 'md',
  loading = false,
  icon,
  iconEnd,
  block = false,
  className,
  children,
  disabled,
  type = 'button',
  onClick,
  ...rest
}: ButtonProps) {
  return (
    <button
      {...rest}
      type={type}
      className={cx('b-btn', `b-btn--${variant}`, `b-btn--${size}`, block && 'b-btn--block', loading && 'is-loading', className)}
      disabled={disabled}
      aria-disabled={loading || undefined}
      aria-busy={loading || undefined}
      onClick={loading ? (e) => e.preventDefault() : onClick}
    >
      {loading ? (
        <Spinner size={ICON_SIZE[size]} label={false} />
      ) : (
        icon && <span className="b-btn__icon">{renderIcon(icon, ICON_SIZE[size])}</span>
      )}
      {children !== undefined && children !== null && children !== false && <span className="b-btn__label">{children}</span>}
      {iconEnd && <span className="b-btn__icon b-btn__icon--end">{renderIcon(iconEnd, ICON_SIZE[size])}</span>}
    </button>
  );
}

export interface IconButtonProps extends Omit<ComponentPropsWithRef<'button'>, 'children'> {
  icon: IconSlot;
  /** Accessible name (required) — also shown as the native tooltip unless `tooltip={false}`. */
  label: string;
  variant?: ButtonVariant;
  size?: ButtonSize;
  /** Toggle buttons: sets aria-pressed. */
  pressed?: boolean;
  loading?: boolean;
  tooltip?: boolean;
  /** Optional small counter/dot rendered on the corner. */
  badge?: ReactNode;
}

export function IconButton({
  icon,
  label,
  variant = 'ghost',
  size = 'md',
  pressed,
  loading = false,
  tooltip = true,
  badge,
  className,
  type = 'button',
  onClick,
  ...rest
}: IconButtonProps) {
  return (
    <button
      title={tooltip ? label : undefined}
      {...rest}
      type={type}
      aria-label={label}
      aria-pressed={pressed}
      aria-busy={loading || undefined}
      className={cx(
        'b-btn',
        'b-iconbtn',
        `b-btn--${variant}`,
        `b-btn--${size}`,
        pressed && 'is-pressed',
        loading && 'is-loading',
        className,
      )}
      onClick={loading ? (e) => e.preventDefault() : onClick}
    >
      {loading ? <Spinner size={ICON_SIZE[size]} label={false} /> : renderIcon(icon, ICON_SIZE[size] + 2)}
      {badge !== undefined && badge !== null && badge !== false && <span className="b-iconbtn__badge">{badge}</span>}
    </button>
  );
}
