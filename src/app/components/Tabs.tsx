import { useId, useRef, type KeyboardEvent, type ReactNode } from 'react';
import { cx } from './hooks';
import { renderIcon, type IconSlot } from './Icon';

/** Index of the next enabled item for a roving-focus key, or null when the key is not ours. */
export function rovingIndex(
  key: string,
  current: number,
  disabled: readonly boolean[],
  orientation: 'horizontal' | 'vertical' | 'both' = 'horizontal',
): number | null {
  const n = disabled.length;
  if (n === 0) return null;
  const nextKeys = orientation === 'vertical' ? ['ArrowDown'] : orientation === 'both' ? ['ArrowRight', 'ArrowDown'] : ['ArrowRight'];
  const prevKeys = orientation === 'vertical' ? ['ArrowUp'] : orientation === 'both' ? ['ArrowLeft', 'ArrowUp'] : ['ArrowLeft'];
  const step = (from: number, dir: 1 | -1): number => {
    for (let k = 1; k <= n; k++) {
      const i = (((from + dir * k) % n) + n) % n;
      if (!disabled[i]) return i;
    }
    return from;
  };
  if (nextKeys.includes(key)) return step(current, 1);
  if (prevKeys.includes(key)) return step(current, -1);
  if (key === 'Home') return step(-1, 1);
  if (key === 'End') return step(n, -1);
  return null;
}

// ---------------------------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------------------------

export interface TabItem<T extends string = string> {
  id: T;
  label: ReactNode;
  icon?: IconSlot;
  /** Small count/badge after the label. */
  badge?: ReactNode;
  disabled?: boolean;
}

export interface TabsProps<T extends string = string> {
  items: readonly TabItem<T>[];
  value: T;
  onChange: (id: T) => void;
  'aria-label'?: string;
  /** Prefix for tab/panel ids; pair with <TabPanel idBase=… value=…>. */
  idBase?: string;
  variant?: 'line' | 'pill';
  /** Stretch tabs to fill the width (mobile). */
  fitted?: boolean;
  className?: string;
}

export function tabId(idBase: string, id: string): string {
  return `${idBase}-tab-${id}`;
}
export function tabPanelId(idBase: string, id: string): string {
  return `${idBase}-panel-${id}`;
}

/** Tab list with automatic activation (arrows/Home/End move and select). */
export function Tabs<T extends string = string>({
  items,
  value,
  onChange,
  idBase,
  variant = 'line',
  fitted = false,
  className,
  ...aria
}: TabsProps<T>) {
  const autoId = useId();
  const base = idBase ?? autoId;
  const refs = useRef<(HTMLButtonElement | null)[]>([]);
  const onKeyDown = (e: KeyboardEvent<HTMLDivElement>) => {
    const current = Math.max(
      0,
      items.findIndex((it) => it.id === value),
    );
    const next = rovingIndex(
      e.key,
      current,
      items.map((it) => !!it.disabled),
    );
    if (next === null) return;
    e.preventDefault();
    const item = items[next];
    if (!item) return;
    onChange(item.id);
    refs.current[next]?.focus();
  };
  return (
    <div
      role="tablist"
      aria-label={aria['aria-label']}
      className={cx('b-tabs', `b-tabs--${variant}`, fitted && 'b-tabs--fitted', className)}
      onKeyDown={onKeyDown}
    >
      {items.map((it, i) => {
        const selected = it.id === value;
        return (
          <button
            key={it.id}
            ref={(el) => {
              refs.current[i] = el;
            }}
            type="button"
            role="tab"
            id={tabId(base, it.id)}
            aria-selected={selected}
            aria-controls={idBase ? tabPanelId(base, it.id) : undefined}
            tabIndex={selected ? 0 : -1}
            disabled={it.disabled}
            className={cx('b-tab', selected && 'is-selected')}
            onClick={() => onChange(it.id)}
          >
            {it.icon && <span className="b-tab__icon">{renderIcon(it.icon, 16)}</span>}
            <span className="b-tab__label">{it.label}</span>
            {it.badge !== undefined && it.badge !== null && <span className="b-tab__badge">{it.badge}</span>}
          </button>
        );
      })}
    </div>
  );
}

export interface TabPanelProps {
  idBase: string;
  value: string;
  children: ReactNode;
  className?: string;
}

export function TabPanel({ idBase, value, children, className }: TabPanelProps) {
  return (
    <div
      role="tabpanel"
      id={tabPanelId(idBase, value)}
      aria-labelledby={tabId(idBase, value)}
      tabIndex={0}
      className={cx('b-tabpanel', className)}
    >
      {children}
    </div>
  );
}

// ---------------------------------------------------------------------------------------------
// SegmentedControl
// ---------------------------------------------------------------------------------------------

export interface SegmentOption<T extends string = string> {
  value: T;
  label?: ReactNode;
  icon?: IconSlot;
  /** Accessible name when the label is just an icon. */
  ariaLabel?: string;
  disabled?: boolean;
  title?: string;
}

export interface SegmentedControlProps<T extends string = string> {
  options: readonly SegmentOption<T>[];
  value: T;
  onChange: (value: T) => void;
  'aria-label'?: string;
  'aria-labelledby'?: string;
  size?: 'sm' | 'md';
  /** 'segments': one joined bar; 'chips': wrapping pill buttons (many options, phones). */
  variant?: 'segments' | 'chips';
  /** Stretch to the full width. */
  fitted?: boolean;
  className?: string;
}

/** Single choice from a few options (radiogroup semantics, arrow keys move the choice). */
export function SegmentedControl<T extends string = string>({
  options,
  value,
  onChange,
  size = 'md',
  variant = 'segments',
  fitted = false,
  className,
  ...aria
}: SegmentedControlProps<T>) {
  const refs = useRef<(HTMLButtonElement | null)[]>([]);
  const selectedIndex = options.findIndex((o) => o.value === value);
  const onKeyDown = (e: KeyboardEvent<HTMLDivElement>) => {
    const next = rovingIndex(
      e.key,
      Math.max(0, selectedIndex),
      options.map((o) => !!o.disabled),
      'both',
    );
    if (next === null) return;
    e.preventDefault();
    const opt = options[next];
    if (!opt) return;
    onChange(opt.value);
    refs.current[next]?.focus();
  };
  return (
    <div
      role="radiogroup"
      aria-label={aria['aria-label']}
      aria-labelledby={aria['aria-labelledby']}
      className={cx('b-segmented', `b-segmented--${variant}`, `b-segmented--${size}`, fitted && 'b-segmented--fitted', className)}
      onKeyDown={onKeyDown}
    >
      {options.map((o, i) => {
        const checked = o.value === value;
        const tabbable = checked || (selectedIndex < 0 && i === 0);
        return (
          <button
            key={o.value}
            ref={(el) => {
              refs.current[i] = el;
            }}
            type="button"
            role="radio"
            aria-checked={checked}
            aria-label={o.ariaLabel}
            title={o.title}
            tabIndex={tabbable ? 0 : -1}
            disabled={o.disabled}
            className={cx('b-segment', checked && 'is-selected')}
            onClick={() => onChange(o.value)}
          >
            {o.icon && <span className="b-segment__icon">{renderIcon(o.icon, 16)}</span>}
            {o.label !== undefined && <span className="b-segment__label">{o.label}</span>}
          </button>
        );
      })}
    </div>
  );
}
