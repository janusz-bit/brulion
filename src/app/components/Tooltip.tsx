import { cloneElement, isValidElement, useId, type ReactElement, type ReactNode } from 'react';
import { cx } from './hooks';

export interface TooltipProps {
  content: ReactNode;
  side?: 'top' | 'bottom' | 'left' | 'right';
  /** A single focusable element (button, link…). It gets aria-describedby. */
  children: ReactElement;
  className?: string;
  /** Keep the tooltip as a description only (no visual bubble) — e.g. on touch-only UIs. */
  disabled?: boolean;
}

/**
 * CSS-only tooltip: shown on hover and keyboard focus (not on touch), positioned with CSS
 * around the wrapped element. For long or interactive content use a Popover instead.
 */
export function Tooltip({ content, side = 'top', children, className, disabled = false }: TooltipProps) {
  const id = useId();
  const child = isValidElement<{ 'aria-describedby'?: string }>(children)
    ? cloneElement(children, {
        'aria-describedby': cx(children.props['aria-describedby'], id) || undefined,
      })
    : children;
  return (
    <span className={cx('b-tooltip', `b-tooltip--${side}`, disabled && 'is-disabled', className)}>
      {child}
      <span role="tooltip" id={id} className="b-tooltip__bubble">
        {content}
      </span>
    </span>
  );
}
