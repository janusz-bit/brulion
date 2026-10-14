import { cx } from './hooks';
import { useT } from './i18n';

export interface SpinnerProps {
  /** Pixel size (default 16). */
  size?: number;
  /** Screen-reader label; `false` makes the spinner decorative (e.g. inside a busy button). */
  label?: string | false;
  className?: string;
}

export function Spinner({ size = 16, label, className }: SpinnerProps) {
  const t = useT();
  const text = label === false ? undefined : (label ?? t('components.loading'));
  return (
    <span
      className={cx('b-spinner', className)}
      style={{ width: size, height: size }}
      role={text ? 'status' : undefined}
      aria-label={text}
      aria-hidden={text ? undefined : true}
    />
  );
}

export interface ProgressBarProps {
  /** Current value; omit for an indeterminate bar. */
  value?: number;
  max?: number;
  /** Accessible label (and visible caption when `showLabel`). */
  label?: string;
  showLabel?: boolean;
  /** Caption on the right, e.g. "120 / 340". Defaults to a percentage. */
  valueText?: string;
  tone?: 'neutral' | 'accent' | 'success' | 'reminder';
  size?: 'sm' | 'md';
  className?: string;
}

export function ProgressBar({
  value,
  max = 1,
  label,
  showLabel = false,
  valueText,
  tone = 'neutral',
  size = 'md',
  className,
}: ProgressBarProps) {
  const determinate = typeof value === 'number' && Number.isFinite(value) && max > 0;
  const ratio = determinate ? Math.min(1, Math.max(0, value / max)) : 0;
  const pct = Math.round(ratio * 100);
  const caption = valueText ?? (determinate ? `${pct}%` : undefined);
  return (
    <div className={cx('b-progress', `b-progress--${tone}`, `b-progress--${size}`, className)}>
      {showLabel && (label || caption) && (
        <div className="b-progress__caption">
          <span>{label}</span>
          {caption && <span className="b-progress__value">{caption}</span>}
        </div>
      )}
      <div
        className={cx('b-progress__track', !determinate && 'is-indeterminate')}
        role="progressbar"
        aria-label={label}
        aria-valuemin={determinate ? 0 : undefined}
        aria-valuemax={determinate ? max : undefined}
        aria-valuenow={determinate ? value : undefined}
        aria-valuetext={determinate ? caption : undefined}
      >
        <div className="b-progress__bar" style={determinate ? { width: `${ratio * 100}%` } : undefined} />
      </div>
    </div>
  );
}
