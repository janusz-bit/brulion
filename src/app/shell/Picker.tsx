/**
 * Search field + result list in a dialog (command palette, quick switcher): a combobox with
 * an active option (arrow keys, Home/End, PageUp/PageDown), Enter picks it (modifier keys are
 * passed on), Escape closes (Modal).
 */
import { useEffect, useId, useRef, useState, type KeyboardEvent, type ReactNode } from 'react';
import { cx, Kbd, Modal, renderIcon, type IconSlot } from '@/app/components';
import { highlightSegments } from './fuzzy';

export interface PickerItem {
  id: string;
  label: string;
  /** Matched character indices in `label` (highlighted). */
  indices?: readonly number[];
  /** Secondary text (folder, description). */
  hint?: ReactNode;
  icon?: IconSlot;
  shortcut?: string;
  /** Group heading shown above the first item of a group. */
  group?: string;
}

export interface PickModifiers {
  mod: boolean;
  shift: boolean;
  alt: boolean;
}

export interface PickerProps {
  open: boolean;
  onClose(): void;
  title: string;
  placeholder: string;
  query: string;
  onQueryChange(q: string): void;
  items: readonly PickerItem[];
  onPick(item: PickerItem, mods: PickModifiers): void;
  empty?: ReactNode;
  /** Hints under the list (keyboard help). */
  footer?: ReactNode;
  className?: string;
}

function HighlightedLabel({ text, indices }: { text: string; indices?: readonly number[] }) {
  if (!indices || indices.length === 0) return <>{text}</>;
  return (
    <>
      {highlightSegments(text, indices).map((s, i) =>
        s.hit ? (
          <mark key={i} className="b-picker__hit">
            {s.text}
          </mark>
        ) : (
          <span key={i}>{s.text}</span>
        ),
      )}
    </>
  );
}

export function Picker({ open, onClose, title, placeholder, query, onQueryChange, items, onPick, empty, footer, className }: PickerProps) {
  const [active, setActive] = useState(0);
  const listId = useId();
  const inputRef = useRef<HTMLInputElement | null>(null);
  const listRef = useRef<HTMLUListElement | null>(null);

  useEffect(() => setActive(0), [query, open]);
  useEffect(() => {
    if (active >= items.length) setActive(Math.max(0, items.length - 1));
  }, [items.length, active]);
  useEffect(() => {
    const el = listRef.current?.querySelector<HTMLElement>(`[data-index="${active}"]`);
    el?.scrollIntoView?.({ block: 'nearest' });
  }, [active]);

  const pick = (index: number, mods: PickModifiers) => {
    const item = items[index];
    if (item) onPick(item, mods);
  };

  const onKeyDown = (e: KeyboardEvent<HTMLInputElement>) => {
    const n = items.length;
    const page = 8;
    let next: number | null = null;
    switch (e.key) {
      case 'ArrowDown':
        next = n === 0 ? 0 : (active + 1) % n;
        break;
      case 'ArrowUp':
        next = n === 0 ? 0 : (active - 1 + n) % n;
        break;
      case 'PageDown':
        next = Math.min(n - 1, active + page);
        break;
      case 'PageUp':
        next = Math.max(0, active - page);
        break;
      case 'Enter':
        if (e.nativeEvent.isComposing) return;
        e.preventDefault();
        pick(active, { mod: e.ctrlKey || e.metaKey, shift: e.shiftKey, alt: e.altKey });
        return;
      default:
        return;
    }
    e.preventDefault();
    setActive(Math.max(0, next));
  };

  const activeId = items[active] ? `${listId}-${active}` : undefined;
  let lastGroup: string | undefined;

  return (
    <Modal open={open} onClose={onClose} title={title} sheet="never" className={cx('b-picker', className)} initialFocus={inputRef}>
      <div className="b-picker__search">
        <span className="b-picker__search-icon">{renderIcon('search', 18)}</span>
        <input
          ref={inputRef}
          className="b-picker__input"
          type="text"
          role="combobox"
          aria-expanded="true"
          aria-controls={listId}
          aria-activedescendant={activeId}
          aria-autocomplete="list"
          aria-label={title}
          placeholder={placeholder}
          value={query}
          autoComplete="off"
          autoCorrect="off"
          spellCheck={false}
          enterKeyHint="go"
          onChange={(e) => onQueryChange(e.target.value)}
          onKeyDown={onKeyDown}
        />
      </div>
      <ul ref={listRef} id={listId} role="listbox" aria-label={title} className="b-picker__list">
        {items.map((item, i) => {
          const heading = item.group && item.group !== lastGroup ? item.group : null;
          lastGroup = item.group;
          return (
            <li key={item.id} role="none">
              {heading && (
                <div className="b-picker__group" role="presentation">
                  {heading}
                </div>
              )}
              <div
                id={`${listId}-${i}`}
                role="option"
                aria-selected={i === active}
                data-index={i}
                className={cx('b-picker__item', i === active && 'is-active')}
                onPointerMove={() => i !== active && setActive(i)}
                onClick={(e) => pick(i, { mod: e.ctrlKey || e.metaKey, shift: e.shiftKey, alt: e.altKey })}
              >
                {item.icon !== undefined && <span className="b-picker__icon">{renderIcon(item.icon, 16)}</span>}
                <span className="b-picker__text">
                  <span className="b-picker__label">
                    <HighlightedLabel text={item.label} indices={item.indices} />
                  </span>
                  {item.hint && <span className="b-picker__hint">{item.hint}</span>}
                </span>
                {item.shortcut && <Kbd keys={item.shortcut} className="b-picker__kbd" />}
              </div>
            </li>
          );
        })}
      </ul>
      {items.length === 0 && empty && <div className="b-picker__empty">{empty}</div>}
      {footer && <div className="b-picker__footer">{footer}</div>}
    </Modal>
  );
}
