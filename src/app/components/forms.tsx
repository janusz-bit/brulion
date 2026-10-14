/**
 * Form controls: Field (label + hint + error wrapper), TextInput, TextArea, Select,
 * SearchInput, Checkbox and Switch (alias Toggle). Native elements underneath, so forms,
 * autofill and mobile keyboards behave; `onValueChange` is offered next to the native
 * `onChange` for convenience.
 */
import {
  useCallback,
  useId,
  useRef,
  type ChangeEvent,
  type ComponentPropsWithRef,
  type KeyboardEvent,
  type ReactNode,
  type Ref,
} from 'react';
import { IconButton } from './Button';
import { cx, useIsoLayoutEffect } from './hooks';
import { Icon, renderIcon, type IconSlot } from './Icon';
import { useT } from './i18n';
import { Kbd } from './Kbd';

export type ControlSize = 'sm' | 'md' | 'lg';

// ---------------------------------------------------------------------------------------------
// Field
// ---------------------------------------------------------------------------------------------

export interface FieldProps {
  /** id of the control the label points to. */
  htmlFor?: string;
  label?: ReactNode;
  hint?: ReactNode;
  error?: ReactNode;
  /** Shows "(opcjonalnie)" after the label. */
  optional?: boolean;
  className?: string;
  /** ids for aria-describedby (computed by the controls). */
  hintId?: string;
  errorId?: string;
  children: ReactNode;
}

export function Field({ htmlFor, label, hint, error, optional, className, hintId, errorId, children }: FieldProps) {
  const t = useT();
  return (
    <div className={cx('b-field', !!error && 'has-error', className)}>
      {label && (
        <label className="b-field__label" htmlFor={htmlFor}>
          {label}
          {optional && <span className="b-field__optional"> ({t('components.optional')})</span>}
        </label>
      )}
      {children}
      {error ? (
        <div className="b-field__error" id={errorId} role="alert">
          <Icon name="alert" size={14} />
          <span>{error}</span>
        </div>
      ) : (
        hint && (
          <div className="b-field__hint" id={hintId}>
            {hint}
          </div>
        )
      )}
    </div>
  );
}

function describedBy(...ids: (string | false | undefined | null)[]): string | undefined {
  const s = ids.filter(Boolean).join(' ');
  return s || undefined;
}

// ---------------------------------------------------------------------------------------------
// TextInput
// ---------------------------------------------------------------------------------------------

export interface TextInputProps extends Omit<ComponentPropsWithRef<'input'>, 'size'> {
  label?: ReactNode;
  hint?: ReactNode;
  error?: ReactNode;
  optional?: boolean;
  /** Leading icon inside the box. */
  icon?: IconSlot;
  /** Trailing content inside the box (unit, button, Kbd...). */
  end?: ReactNode;
  controlSize?: ControlSize;
  onValueChange?: (value: string) => void;
  /** Class for the outer Field wrapper. */
  fieldClassName?: string;
}

export function TextInput({
  label,
  hint,
  error,
  optional,
  icon,
  end,
  controlSize = 'md',
  onValueChange,
  onChange,
  id,
  className,
  fieldClassName,
  type = 'text',
  ...rest
}: TextInputProps) {
  const autoId = useId();
  const inputId = id ?? autoId;
  const hintId = `${inputId}-hint`;
  const errorId = `${inputId}-error`;
  const box = (
    <div className={cx('b-input', `b-input--${controlSize}`, !!error && 'is-invalid', rest.disabled && 'is-disabled', className)}>
      {icon && <span className="b-input__icon">{renderIcon(icon, 16)}</span>}
      <input
        {...rest}
        id={inputId}
        type={type}
        className="b-input__el"
        aria-invalid={error ? true : undefined}
        aria-describedby={describedBy(rest['aria-describedby'], error ? errorId : hint ? hintId : undefined)}
        onChange={(e: ChangeEvent<HTMLInputElement>) => {
          onChange?.(e);
          onValueChange?.(e.target.value);
        }}
      />
      {end && <span className="b-input__end">{end}</span>}
    </div>
  );
  if (!label && !hint && !error) return box;
  return (
    <Field
      htmlFor={inputId}
      label={label}
      hint={hint}
      error={error}
      optional={optional}
      hintId={hintId}
      errorId={errorId}
      className={fieldClassName}
    >
      {box}
    </Field>
  );
}

// ---------------------------------------------------------------------------------------------
// TextArea
// ---------------------------------------------------------------------------------------------

export interface TextAreaProps extends ComponentPropsWithRef<'textarea'> {
  label?: ReactNode;
  hint?: ReactNode;
  error?: ReactNode;
  optional?: boolean;
  /** Grow with the content (between `rows` and `maxRows`). */
  autoGrow?: boolean;
  maxRows?: number;
  /** Monospace text (code, raw Markdown). */
  mono?: boolean;
  onValueChange?: (value: string) => void;
  fieldClassName?: string;
}

export function TextArea({
  label,
  hint,
  error,
  optional,
  autoGrow = false,
  maxRows = 16,
  mono = false,
  onValueChange,
  onChange,
  id,
  className,
  fieldClassName,
  rows = 3,
  ref,
  ...rest
}: TextAreaProps) {
  const autoId = useId();
  const inputId = id ?? autoId;
  const hintId = `${inputId}-hint`;
  const errorId = `${inputId}-error`;
  const inner = useRef<HTMLTextAreaElement | null>(null);

  const setRefs = useCallback(
    (el: HTMLTextAreaElement | null) => {
      inner.current = el;
      assignRef(ref, el);
    },
    [ref],
  );

  const resize = useCallback(() => {
    const el = inner.current;
    if (!autoGrow || !el) return;
    const cs = getComputedStyle(el);
    const line = parseFloat(cs.lineHeight) || 20;
    const pad = parseFloat(cs.paddingTop) + parseFloat(cs.paddingBottom) + parseFloat(cs.borderTopWidth) + parseFloat(cs.borderBottomWidth);
    el.style.height = 'auto';
    const max = line * maxRows + pad;
    const h = Math.min(el.scrollHeight + parseFloat(cs.borderTopWidth) + parseFloat(cs.borderBottomWidth), max);
    el.style.height = `${Math.max(h, line * rows + pad)}px`;
    el.style.overflowY = el.scrollHeight > max ? 'auto' : 'hidden';
  }, [autoGrow, maxRows, rows]);

  useIsoLayoutEffect(() => {
    resize();
  }, [resize, rest.value]);

  const el = (
    <textarea
      {...rest}
      ref={setRefs}
      id={inputId}
      rows={rows}
      className={cx('b-textarea', mono && 'b-textarea--mono', !!error && 'is-invalid', className)}
      aria-invalid={error ? true : undefined}
      aria-describedby={describedBy(rest['aria-describedby'], error ? errorId : hint ? hintId : undefined)}
      onChange={(e) => {
        onChange?.(e);
        onValueChange?.(e.target.value);
        resize();
      }}
    />
  );
  if (!label && !hint && !error) return el;
  return (
    <Field
      htmlFor={inputId}
      label={label}
      hint={hint}
      error={error}
      optional={optional}
      hintId={hintId}
      errorId={errorId}
      className={fieldClassName}
    >
      {el}
    </Field>
  );
}

function assignRef<T>(ref: Ref<T> | undefined, value: T | null): void {
  if (typeof ref === 'function') ref(value);
  else if (ref && typeof ref === 'object') (ref as { current: T | null }).current = value;
}

// ---------------------------------------------------------------------------------------------
// Select
// ---------------------------------------------------------------------------------------------

export interface SelectOption {
  value: string;
  label: string;
  disabled?: boolean;
}

export interface SelectProps extends Omit<ComponentPropsWithRef<'select'>, 'size'> {
  options?: readonly SelectOption[];
  label?: ReactNode;
  hint?: ReactNode;
  error?: ReactNode;
  controlSize?: ControlSize;
  onValueChange?: (value: string) => void;
  fieldClassName?: string;
}

export function Select({
  options,
  label,
  hint,
  error,
  controlSize = 'md',
  onValueChange,
  onChange,
  id,
  className,
  fieldClassName,
  children,
  ...rest
}: SelectProps) {
  const autoId = useId();
  const selectId = id ?? autoId;
  const hintId = `${selectId}-hint`;
  const errorId = `${selectId}-error`;
  const box = (
    <div className={cx('b-select', `b-select--${controlSize}`, !!error && 'is-invalid', className)}>
      <select
        {...rest}
        id={selectId}
        className="b-select__el"
        aria-invalid={error ? true : undefined}
        aria-describedby={describedBy(rest['aria-describedby'], error ? errorId : hint ? hintId : undefined)}
        onChange={(e) => {
          onChange?.(e);
          onValueChange?.(e.target.value);
        }}
      >
        {options?.map((o) => (
          <option key={o.value} value={o.value} disabled={o.disabled}>
            {o.label}
          </option>
        ))}
        {children}
      </select>
      <Icon name="chevron-down" size={16} className="b-select__chevron" />
    </div>
  );
  if (!label && !hint && !error) return box;
  return (
    <Field htmlFor={selectId} label={label} hint={hint} error={error} hintId={hintId} errorId={errorId} className={fieldClassName}>
      {box}
    </Field>
  );
}

// ---------------------------------------------------------------------------------------------
// SearchInput
// ---------------------------------------------------------------------------------------------

export interface SearchInputProps
  extends Omit<ComponentPropsWithRef<'input'>, 'size' | 'value' | 'onChange' | 'onSubmit' | 'type'> {
  value: string;
  onValueChange: (value: string) => void;
  /** Enter pressed. */
  onSubmit?: (value: string) => void;
  /** Shortcut hint shown while empty, e.g. "Mod+K". */
  shortcut?: string;
  controlSize?: ControlSize;
  /** Accessible label (defaults to the placeholder). */
  label?: string;
  /** Show a spinner instead of the clear button. */
  busy?: boolean;
}

export function SearchInput({
  value,
  onValueChange,
  onSubmit,
  shortcut,
  controlSize = 'md',
  label,
  placeholder,
  busy = false,
  className,
  onKeyDown,
  ref,
  ...rest
}: SearchInputProps) {
  const t = useT();
  const inner = useRef<HTMLInputElement | null>(null);
  const ph = placeholder ?? t('components.search.placeholder');
  const setRefs = useCallback(
    (el: HTMLInputElement | null) => {
      inner.current = el;
      assignRef(ref, el);
    },
    [ref],
  );
  return (
    <div className={cx('b-input', 'b-search', `b-input--${controlSize}`, className)} role="search">
      <span className="b-input__icon">
        <Icon name="search" size={16} />
      </span>
      <input
        {...rest}
        ref={setRefs}
        type="search"
        className="b-input__el"
        value={value}
        placeholder={ph}
        aria-label={label ?? ph}
        autoComplete="off"
        spellCheck={false}
        enterKeyHint="search"
        onChange={(e) => onValueChange(e.target.value)}
        onKeyDown={(e: KeyboardEvent<HTMLInputElement>) => {
          onKeyDown?.(e);
          if (e.defaultPrevented) return;
          if (e.key === 'Enter' && onSubmit) {
            e.preventDefault();
            onSubmit(value);
          } else if (e.key === 'Escape' && value) {
            // First Escape clears; a second one bubbles (closes the palette/dialog).
            e.preventDefault();
            e.stopPropagation();
            e.nativeEvent.stopImmediatePropagation();
            onValueChange('');
          }
        }}
      />
      {busy ? (
        <span className="b-input__end">
          <span className="b-spinner" style={{ width: 14, height: 14 }} aria-hidden="true" />
        </span>
      ) : value ? (
        <span className="b-input__end">
          <IconButton
            icon="x"
            size="sm"
            label={t('components.search.clear')}
            className="b-search__clear"
            onClick={() => {
              onValueChange('');
              inner.current?.focus();
            }}
          />
        </span>
      ) : (
        shortcut && (
          <span className="b-input__end b-search__kbd">
            <Kbd keys={shortcut} />
          </span>
        )
      )}
    </div>
  );
}

// ---------------------------------------------------------------------------------------------
// Checkbox
// ---------------------------------------------------------------------------------------------

export interface CheckboxProps extends Omit<ComponentPropsWithRef<'input'>, 'type' | 'size'> {
  label?: ReactNode;
  description?: ReactNode;
  indeterminate?: boolean;
  onCheckedChange?: (checked: boolean) => void;
}

export function Checkbox({
  label,
  description,
  indeterminate = false,
  onCheckedChange,
  onChange,
  className,
  id,
  ref,
  ...rest
}: CheckboxProps) {
  const autoId = useId();
  const inputId = id ?? autoId;
  const inner = useRef<HTMLInputElement | null>(null);
  const setRefs = useCallback(
    (el: HTMLInputElement | null) => {
      inner.current = el;
      assignRef(ref, el);
    },
    [ref],
  );
  useIsoLayoutEffect(() => {
    if (inner.current) inner.current.indeterminate = indeterminate;
  }, [indeterminate]);
  return (
    <div className={cx('b-check', rest.disabled && 'is-disabled', className)}>
      <input
        {...rest}
        ref={setRefs}
        id={inputId}
        type="checkbox"
        className="b-check__box"
        aria-describedby={description ? `${inputId}-desc` : undefined}
        onChange={(e) => {
          onChange?.(e);
          onCheckedChange?.(e.target.checked);
        }}
      />
      {(label || description) && (
        <div className="b-check__text">
          {label && (
            <label htmlFor={inputId} className="b-check__label">
              {label}
            </label>
          )}
          {description && (
            <div id={`${inputId}-desc`} className="b-check__desc">
              {description}
            </div>
          )}
        </div>
      )}
    </div>
  );
}

// ---------------------------------------------------------------------------------------------
// Switch / Toggle
// ---------------------------------------------------------------------------------------------

export interface SwitchProps extends Omit<ComponentPropsWithRef<'button'>, 'onChange' | 'children'> {
  checked: boolean;
  onCheckedChange: (checked: boolean) => void;
  label?: ReactNode;
  description?: ReactNode;
  /** Label left of the switch (settings rows) or right of it. */
  labelPosition?: 'start' | 'end';
  /** Icon in front of the label. */
  icon?: IconSlot;
  size?: 'sm' | 'md';
}

export function Switch({
  checked,
  onCheckedChange,
  label,
  description,
  labelPosition = 'start',
  icon,
  size = 'md',
  className,
  id,
  disabled,
  ...rest
}: SwitchProps) {
  const autoId = useId();
  const btnId = id ?? autoId;
  const button = (
    <button
      {...rest}
      id={btnId}
      type="button"
      role="switch"
      aria-checked={checked}
      aria-labelledby={label ? `${btnId}-label` : rest['aria-labelledby']}
      aria-describedby={description ? `${btnId}-desc` : undefined}
      disabled={disabled}
      className={cx('b-switch', `b-switch--${size}`, checked && 'is-on')}
      onClick={() => onCheckedChange(!checked)}
    >
      <span className="b-switch__thumb" />
    </button>
  );
  if (!label && !description) return button;
  return (
    <div className={cx('b-switch-row', labelPosition === 'end' && 'b-switch-row--end', disabled && 'is-disabled', className)}>
      <div className="b-switch-row__text">
        {label && (
          // Clicking the label toggles, like a native <label>.
          <label
            id={`${btnId}-label`}
            htmlFor={btnId}
            className="b-switch-row__label"
            onClick={(e) => {
              e.preventDefault();
              if (!disabled) onCheckedChange(!checked);
            }}
          >
            {icon && <span className="b-switch-row__icon">{renderIcon(icon, 16)}</span>}
            {label}
          </label>
        )}
        {description && (
          <div id={`${btnId}-desc`} className="b-switch-row__desc">
            {description}
          </div>
        )}
      </div>
      {button}
    </div>
  );
}

/** Alias: some call sites read better as "Toggle". */
export const Toggle = Switch;
export type ToggleProps = SwitchProps;
