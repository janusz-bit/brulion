/**
 * Menu: a keyboard-navigable list of actions in a Popover (role="menu"), and MenuButton,
 * a trigger + menu pair with the usual ARIA wiring.
 */
import { useEffect, useId, useRef, useState, type KeyboardEvent, type ReactNode } from 'react';
import { Button, IconButton, type ButtonSize, type ButtonVariant } from './Button';
import { cx } from './hooks';
import { Icon, renderIcon, type IconSlot } from './Icon';
import { Kbd } from './Kbd';
import { Popover, type AnchorProp } from './Popover';
import type { Placement } from './position';

export interface MenuAction {
  type?: 'item';
  id: string;
  label: ReactNode;
  /** Plain text for type-ahead when `label` is not a string. */
  textValue?: string;
  description?: ReactNode;
  icon?: IconSlot;
  shortcut?: string;
  danger?: boolean;
  disabled?: boolean;
  /** Shows a check mark (toggle items). */
  checked?: boolean;
  onSelect?: () => void;
}
export interface MenuSeparator {
  type: 'separator';
  id?: string;
}
export interface MenuLabel {
  type: 'label';
  id?: string;
  label: ReactNode;
}
export type MenuItem = MenuAction | MenuSeparator | MenuLabel;

function isAction(it: MenuItem): it is MenuAction {
  return it.type === undefined || it.type === 'item';
}

function itemText(it: MenuAction): string {
  if (it.textValue) return it.textValue;
  return typeof it.label === 'string' ? it.label : '';
}

/**
 * Next index for menu keyboard navigation over `actions` (only enabled ones are reachable).
 * Returns null for keys the menu does not handle.
 */
export function menuNavIndex(key: string, current: number, enabled: readonly boolean[]): number | null {
  const n = enabled.length;
  if (n === 0) return null;
  const step = (from: number, dir: 1 | -1): number => {
    for (let k = 1; k <= n; k++) {
      const i = (((from + dir * k) % n) + n) % n;
      if (enabled[i]) return i;
    }
    return current;
  };
  switch (key) {
    case 'ArrowDown':
      return step(current, 1);
    case 'ArrowUp':
      return step(current < 0 ? 0 : current, -1);
    case 'Home':
      return step(-1, 1);
    case 'End':
      return step(n, -1);
    default:
      return null;
  }
}

/** Type-ahead: first enabled item after `current` whose text starts with `query` (case/diacritics-insensitive). */
export function typeaheadIndex(query: string, texts: readonly string[], enabled: readonly boolean[], current: number): number | null {
  const fold = (s: string) =>
    s
      .normalize('NFD')
      .replace(/\p{M}/gu, '')
      .replace(/ł/g, 'l')
      .replace(/Ł/g, 'L')
      .toLowerCase();
  const q = fold(query);
  if (!q) return null;
  const n = texts.length;
  // A repeated single letter cycles; a longer query keeps matching from the current item.
  const start = q.length === 1 ? current + 1 : Math.max(current, 0);
  for (let k = 0; k < n; k++) {
    const i = (start + k) % n;
    if (enabled[i] && fold(texts[i] ?? '').startsWith(q)) return i;
  }
  return null;
}

export interface MenuProps {
  open: boolean;
  onClose: () => void;
  anchor: AnchorProp;
  items: readonly MenuItem[];
  placement?: Placement;
  'aria-label'?: string;
  'aria-labelledby'?: string;
  id?: string;
  /** Called with the item id after the item's own onSelect. */
  onSelect?: (id: string) => void;
  /** Which item gets focus on open. */
  initialFocus?: 'first' | 'last';
  /** Render in place (see Popover `inline`); no focus is moved. */
  inline?: boolean;
  className?: string;
}

export function Menu({ open, ...props }: MenuProps) {
  if (!open) return null;
  return <MenuList {...props} />;
}

function MenuList({
  onClose,
  anchor,
  items,
  placement = 'bottom-start',
  id,
  onSelect,
  initialFocus = 'first',
  inline = false,
  className,
  ...aria
}: Omit<MenuProps, 'open'>) {
  const actions = items.filter(isAction);
  const enabled = actions.map((a) => !a.disabled);
  const [active, setActive] = useState(() =>
    initialFocus === 'last' ? (menuNavIndex('End', -1, enabled) ?? -1) : (menuNavIndex('Home', -1, enabled) ?? -1),
  );
  const refs = useRef<(HTMLButtonElement | null)[]>([]);
  const typed = useRef({ text: '', at: 0 });

  const moved = useRef(false);
  useEffect(() => {
    // Inline menus only take focus after the user starts navigating them.
    if (inline && !moved.current) return;
    if (active >= 0) refs.current[active]?.focus();
  }, [active, inline]);

  const choose = (it: MenuAction) => {
    if (it.disabled) return;
    onClose();
    it.onSelect?.();
    onSelect?.(it.id);
  };

  const onKeyDown = (e: KeyboardEvent<HTMLDivElement>) => {
    const next = menuNavIndex(e.key, active, enabled);
    if (next !== null) {
      e.preventDefault();
      moved.current = true;
      setActive(next);
      return;
    }
    if (e.key === 'Tab') {
      onClose();
      return;
    }
    if (e.key.length === 1 && !e.ctrlKey && !e.metaKey && !e.altKey && e.key !== ' ') {
      const now = Date.now();
      typed.current.text = now - typed.current.at > 700 ? e.key : typed.current.text + e.key;
      typed.current.at = now;
      const i = typeaheadIndex(typed.current.text, actions.map(itemText), enabled, active);
      if (i !== null) setActive(i);
    }
  };

  let actionIndex = -1;
  return (
    <Popover
      open
      onClose={() => onClose()}
      anchor={anchor}
      placement={placement}
      role="none"
      autoFocus={false}
      padded={false}
      inline={inline}
      className={cx('b-menu-popover', className)}
    >
      <div
        role="menu"
        id={id}
        aria-label={aria['aria-label']}
        aria-labelledby={aria['aria-labelledby']}
        className="b-menu"
        onKeyDown={onKeyDown}
      >
        {items.map((it, i) => {
          if (it.type === 'separator') return <div key={it.id ?? `sep-${i}`} role="separator" className="b-menu__sep" />;
          if (it.type === 'label')
            return (
              <div key={it.id ?? `label-${i}`} role="presentation" className="b-menu__label">
                {it.label}
              </div>
            );
          actionIndex++;
          const idx = actionIndex;
          const checkable = it.checked !== undefined;
          return (
            <button
              key={it.id}
              ref={(el) => {
                refs.current[idx] = el;
              }}
              type="button"
              role={checkable ? 'menuitemcheckbox' : 'menuitem'}
              aria-checked={checkable ? it.checked : undefined}
              aria-disabled={it.disabled || undefined}
              tabIndex={idx === active ? 0 : -1}
              className={cx('b-menu__item', it.danger && 'is-danger', it.disabled && 'is-disabled', idx === active && 'is-active')}
              onClick={() => choose(it)}
              onPointerMove={() => {
                if (!it.disabled && idx !== active) setActive(idx);
              }}
            >
              <span className="b-menu__icon">
                {checkable ? it.checked ? <Icon name="check" size={16} /> : null : renderIcon(it.icon, 16)}
              </span>
              <span className="b-menu__text">
                <span className="b-menu__item-label">{it.label}</span>
                {it.description && <span className="b-menu__desc">{it.description}</span>}
              </span>
              {it.shortcut && <Kbd keys={it.shortcut} className="b-menu__kbd" />}
            </button>
          );
        })}
      </div>
    </Popover>
  );
}

export interface MenuButtonProps {
  items: readonly MenuItem[];
  /** Visible label; with `iconOnly` it becomes the aria-label. */
  label: string;
  icon?: IconSlot;
  iconOnly?: boolean;
  variant?: ButtonVariant;
  size?: ButtonSize;
  placement?: Placement;
  disabled?: boolean;
  className?: string;
  onSelect?: (id: string) => void;
}

/** A button that opens a Menu. ArrowDown/ArrowUp on the trigger open it focused at the first/last item. */
export function MenuButton({
  items,
  label,
  icon,
  iconOnly = false,
  variant = 'ghost',
  size = 'md',
  placement = 'bottom-start',
  disabled,
  className,
  onSelect,
}: MenuButtonProps) {
  const [open, setOpen] = useState<false | 'first' | 'last'>(false);
  const triggerRef = useRef<HTMLButtonElement | null>(null);
  const menuId = useId();
  const triggerId = useId();
  const common = {
    ref: triggerRef,
    id: triggerId,
    disabled,
    className,
    'aria-haspopup': 'menu' as const,
    'aria-expanded': !!open,
    'aria-controls': open ? menuId : undefined,
    onClick: () => setOpen((o) => (o ? false : 'first')),
    onKeyDown: (e: KeyboardEvent<HTMLButtonElement>) => {
      if (e.key === 'ArrowDown') {
        e.preventDefault();
        setOpen('first');
      } else if (e.key === 'ArrowUp') {
        e.preventDefault();
        setOpen('last');
      }
    },
  };
  return (
    <>
      {iconOnly ? (
        <IconButton {...common} icon={icon ?? 'more'} label={label} variant={variant} size={size} />
      ) : (
        <Button {...common} icon={icon} iconEnd="chevron-down" variant={variant} size={size}>
          {label}
        </Button>
      )}
      <Menu
        open={!!open}
        onClose={() => setOpen(false)}
        anchor={triggerRef}
        items={items}
        placement={placement}
        id={menuId}
        aria-labelledby={iconOnly ? undefined : triggerId}
        aria-label={iconOnly ? label : undefined}
        initialFocus={open === 'last' ? 'last' : 'first'}
        onSelect={onSelect}
      />
    </>
  );
}
