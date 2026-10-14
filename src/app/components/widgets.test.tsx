// @vitest-environment jsdom
import { useRef, useState } from 'react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { Chip } from './Badge';
import { DiffView } from './DiffView';
import { SearchInput, Switch } from './forms';
import { LocaleContext } from './i18n';
import { Menu, MenuButton, type MenuItem } from './Menu';
import { MergeView } from './MergeView';
import type { MergeHunk } from './merge';
import { Popover } from './Popover';
import { byText, cleanup, click, key, render, type } from './render.testutil';
import { Splitter } from './Splitter';
import { SegmentedControl, Tabs } from './Tabs';
import { ToastView } from './ToastView';
import { VirtualList } from './VirtualList';

afterEach(cleanup);
// jsdom + React renders are slow on the Raspberry Pi dev machine under load.
vi.setConfig({ testTimeout: 30_000 });

const ITEMS = (onSelect: (id: string) => void): MenuItem[] => [
  { type: 'label', label: 'Strona' },
  { id: 'open', label: 'Otwórz', onSelect: () => onSelect('open') },
  { id: 'rename', label: 'Zmień nazwę', onSelect: () => onSelect('rename') },
  { type: 'separator' },
  { id: 'export', label: 'Eksportuj', disabled: true },
  { id: 'delete', label: 'Usuń', danger: true, onSelect: () => onSelect('delete') },
];

describe('MenuButton / Menu', () => {
  it('opens from the keyboard, navigates, type-ahead, selects and returns focus', () => {
    const selected = vi.fn();
    const r = render(<MenuButton label="Akcje" items={ITEMS(selected)} />);
    const trigger = r.container.querySelector<HTMLButtonElement>('button[aria-haspopup="menu"]')!;
    trigger.focus();
    key(trigger, 'ArrowDown');
    expect(trigger.getAttribute('aria-expanded')).toBe('true');
    const menu = document.querySelector('[role="menu"]')!;
    expect(menu.getAttribute('aria-labelledby')).toBe(trigger.id);
    expect(document.activeElement?.textContent).toBe('Otwórz');
    key(document.activeElement, 'ArrowDown');
    expect(document.activeElement?.textContent).toBe('Zmień nazwę');
    // Disabled items are skipped.
    key(document.activeElement, 'ArrowDown');
    expect(document.activeElement?.textContent).toBe('Usuń');
    key(document.activeElement, 'ArrowDown');
    expect(document.activeElement?.textContent).toBe('Otwórz');
    key(document.activeElement, 'z');
    expect(document.activeElement?.textContent).toBe('Zmień nazwę');
    click(document.activeElement);
    expect(selected).toHaveBeenCalledWith('rename');
    expect(document.querySelector('[role="menu"]')).toBeNull();
    expect(document.activeElement).toBe(trigger);
  });

  it('ArrowUp opens at the last item; Escape closes', () => {
    const r = render(<MenuButton label="Akcje" items={ITEMS(() => {})} iconOnly />);
    const trigger = r.container.querySelector<HTMLButtonElement>('button')!;
    expect(trigger.getAttribute('aria-label')).toBe('Akcje');
    trigger.focus();
    key(trigger, 'ArrowUp');
    expect(document.activeElement?.textContent).toBe('Usuń');
    key(document.activeElement, 'Escape');
    expect(document.querySelector('[role="menu"]')).toBeNull();
    expect(document.activeElement).toBe(trigger);
  });

  it('closes on outside press but not on the trigger itself (no flicker)', () => {
    const onClose = vi.fn();
    function Harness() {
      const anchor = useRef<HTMLButtonElement | null>(null);
      return (
        <>
          <button ref={anchor} id="anchor">
            kotwica
          </button>
          <p id="outside">poza</p>
          <Menu open onClose={onClose} anchor={anchor} items={ITEMS(() => {})} />
        </>
      );
    }
    render(<Harness />);
    document.getElementById('anchor')!.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(onClose).not.toHaveBeenCalled();
    click(document.getElementById('outside'));
    expect(onClose).toHaveBeenCalled();
  });

  it('marks checkable items', () => {
    render(<Menu open inline onClose={() => {}} anchor={null} items={[{ id: 'fav', label: 'Ulubione', checked: true }]} />);
    const item = document.querySelector('[role="menuitemcheckbox"]');
    expect(item?.getAttribute('aria-checked')).toBe('true');
  });
});

describe('Popover', () => {
  it('focuses its content, closes when focus leaves and restores focus', () => {
    const onClose = vi.fn();
    function Harness() {
      const anchor = useRef<HTMLButtonElement | null>(null);
      const [open, setOpen] = useState(true);
      return (
        <>
          <button ref={anchor} id="anchor">
            kotwica
          </button>
          <button id="elsewhere">gdzie indziej</button>
          <Popover
            open={open}
            anchor={anchor}
            onClose={(r) => {
              onClose(r);
              setOpen(false);
            }}
            aria-label="Panel"
          >
            <input id="first" />
          </Popover>
        </>
      );
    }
    document.body.focus();
    render(<Harness />);
    const dialog = document.querySelector('.b-popover')!;
    expect(dialog.getAttribute('role')).toBe('dialog');
    expect(document.activeElement?.id).toBe('first');
    const { act } = require('react') as typeof import('react');
    act(() => document.getElementById('elsewhere')!.focus());
    expect(onClose).toHaveBeenCalledWith('focus-out');
  });
});

describe('Tabs / SegmentedControl', () => {
  it('moves the selection with arrow keys and keeps one tab stop', () => {
    function Harness() {
      const [v, setV] = useState('a');
      return (
        <Tabs
          aria-label="Widok"
          value={v}
          onChange={setV}
          items={[
            { id: 'a', label: 'A' },
            { id: 'b', label: 'B', disabled: true },
            { id: 'c', label: 'C' },
          ]}
        />
      );
    }
    render(<Harness />);
    const tabs = [...document.querySelectorAll<HTMLButtonElement>('[role="tab"]')];
    expect(tabs.map((t) => t.tabIndex)).toEqual([0, -1, -1]);
    tabs[0]!.focus();
    key(tabs[0], 'ArrowRight');
    expect(tabs[2]!.getAttribute('aria-selected')).toBe('true');
    expect(document.activeElement).toBe(tabs[2]);
    key(tabs[2], 'Home');
    expect(tabs[0]!.getAttribute('aria-selected')).toBe('true');
  });

  it('segmented control is a radiogroup', () => {
    const onChange = vi.fn();
    render(
      <SegmentedControl
        aria-label="Zakres"
        value="week"
        onChange={onChange}
        options={[
          { value: 'month', label: 'Miesiąc' },
          { value: 'week', label: 'Tydzień' },
        ]}
      />,
    );
    const radios = [...document.querySelectorAll<HTMLButtonElement>('[role="radio"]')];
    expect(radios.map((r) => r.getAttribute('aria-checked'))).toEqual(['false', 'true']);
    key(radios[1], 'ArrowLeft');
    expect(onChange).toHaveBeenCalledWith('month');
    click(radios[1]);
    expect(onChange).toHaveBeenLastCalledWith('week');
  });
});

describe('form controls', () => {
  it('SearchInput clears on the first Escape and lets the second one bubble', () => {
    const outer = vi.fn();
    function Harness() {
      const [q, setQ] = useState('');
      return (
        <div onKeyDown={(e) => outer(e.key)}>
          <SearchInput value={q} onValueChange={setQ} shortcut="Mod+K" />
        </div>
      );
    }
    render(<Harness />);
    const input = document.querySelector<HTMLInputElement>('input[type="search"]')!;
    expect(input.getAttribute('aria-label')).toBe('Szukaj…');
    type(input, 'zażółć');
    expect(document.querySelector('[aria-label="Wyczyść"]')).toBeTruthy();
    key(input, 'Escape');
    expect(input.value).toBe('');
    expect(outer).not.toHaveBeenCalled();
    key(input, 'Escape');
    expect(outer).toHaveBeenCalledWith('Escape');
  });

  it('Switch toggles from the button and the label', () => {
    function Harness() {
      const [on, setOn] = useState(false);
      return <Switch checked={on} onCheckedChange={setOn} label="Alarm" description="Głośny" />;
    }
    render(<Harness />);
    const sw = document.querySelector<HTMLButtonElement>('[role="switch"]')!;
    expect(sw.getAttribute('aria-checked')).toBe('false');
    expect(document.getElementById(sw.getAttribute('aria-labelledby')!)?.textContent).toBe('Alarm');
    click(sw);
    expect(sw.getAttribute('aria-checked')).toBe('true');
    click(document.querySelector('label'));
    expect(sw.getAttribute('aria-checked')).toBe('false');
  });

  it('Chip with remove button labels it', () => {
    const onRemove = vi.fn();
    render(
      <Chip variant="tag" onRemove={onRemove}>
        praca
      </Chip>,
    );
    expect(document.querySelector('.b-chip__label')?.textContent).toBe('#praca');
    click(document.querySelector('[aria-label="Usuń: #praca"]'));
    expect(onRemove).toHaveBeenCalled();
  });

  it('ToastView uses alert semantics for errors and runs its action', () => {
    const action = vi.fn();
    const onClose = vi.fn();
    render(
      <LocaleContext.Provider value="en">
        <ToastView toast={{ kind: 'error', message: 'Nope', action: { label: 'Retry', onClick: action } }} onClose={onClose} />
      </LocaleContext.Provider>,
    );
    expect(document.querySelector('.b-toast')?.getAttribute('role')).toBe('alert');
    click(byText(document.body, 'button', 'Retry'));
    expect(action).toHaveBeenCalled();
    expect(onClose).toHaveBeenCalled();
    expect(document.querySelector('[aria-label="Dismiss notification"]')).toBeTruthy();
  });
});

describe('VirtualList', () => {
  it('renders only the visible window and scrolls to an index', () => {
    const items = Array.from({ length: 10_000 }, (_, i) => `Wiersz ${i}`);
    const ref: { current: import('./VirtualList').VirtualListHandle | null } = { current: null };
    render(<VirtualList ref={ref} items={items} rowHeight={30} initialHeight={300} overscan={60} renderRow={(it) => <span>{it}</span>} />);
    const rows = () => [...document.querySelectorAll<HTMLElement>('.b-vlist__row')];
    expect(rows().length).toBeLessThan(20);
    expect(rows()[0]!.textContent).toBe('Wiersz 0');
    expect(document.querySelector<HTMLElement>('.b-vlist__inner')!.style.height).toBe('300000px');
    const { act } = require('react') as typeof import('react');
    act(() => ref.current!.scrollToIndex(5000, 'start'));
    expect(rows().some((r) => r.textContent === 'Wiersz 5000')).toBe(true);
    expect(rows().some((r) => r.textContent === 'Wiersz 0')).toBe(false);
  });
});

describe('DiffView', () => {
  it('shows stats, folds unchanged lines and expands them', () => {
    const left = Array.from({ length: 30 }, (_, i) => `linia ${i}`).join('\n');
    const right = left.replace('linia 15', 'linia 15 zmieniona');
    render(<DiffView left={left} right={right} leftLabel="Przed" rightLabel="Po" mode="unified" context={2} />);
    expect(document.querySelector('.b-diff__stat--add')?.textContent).toBe('+1');
    expect(document.querySelector('.b-diff__stat--del')?.textContent).toBe('−1');
    const gaps = () => [...document.querySelectorAll('.b-diff__expand')];
    expect(gaps().map((g) => g.textContent)).toEqual(['Pokaż · 13 niezmienionych wierszy', 'Pokaż · 12 niezmienionych wierszy']);
    click(gaps()[0]);
    expect(gaps()).toHaveLength(1);
    // Word-level highlight of the changed part.
    expect(document.querySelector('.b-diff__text.is-add .b-diff__word')?.textContent).toBe(' zmieniona');
  });

  it('says when there are no differences', () => {
    render(<DiffView left="a" right="a" mode="split" />);
    expect(document.querySelector('.b-empty__title')?.textContent).toBe('Brak różnic');
  });
});

describe('MergeView', () => {
  const hunks: MergeHunk[] = [
    { kind: 'ok', lines: ['# Plan'] },
    { kind: 'conflict', base: ['- a'], local: ['- a (moje)'], remote: ['- a (ich)'] },
    { kind: 'conflict', base: [], local: ['- b'], remote: ['- c'] },
  ];

  it('resolves each conflict and saves the merged text', () => {
    const onResolve = vi.fn();
    render(<MergeView hunks={hunks} onResolve={onResolve} />);
    const save = () => byText(document.body, 'button', 'Zapisz scalenie') as HTMLButtonElement;
    expect(save().disabled).toBe(true);
    expect(document.querySelector('.b-merge__status')?.textContent).toBe('Pozostały 2 konflikty');
    const sections = () => [...document.querySelectorAll<HTMLElement>('.b-merge__conflict')];
    click(byText(sections()[0]!, 'button', 'Moja wersja'));
    expect(document.querySelector('.b-merge__status')?.textContent).toBe('Pozostał 1 konflikt');
    click(byText(sections()[1]!, 'button', 'Edytuj ręcznie'));
    const ta = sections()[1]!.querySelector('textarea')!;
    expect(ta.value).toBe('- b\n- c');
    type(ta, '- b i c');
    expect(save().disabled).toBe(false);
    click(save());
    expect(onResolve).toHaveBeenCalledWith('# Plan\n- a (moje)\n- b i c\n');
  });

  it('can undo a choice', () => {
    render(<MergeView hunks={hunks} onResolve={() => {}} />);
    const first = document.querySelector<HTMLElement>('.b-merge__conflict')!;
    click(byText(first, 'button', 'Obie'));
    expect(first.querySelector('.b-merge__result')?.textContent).toContain('- a (moje)- a (ich)');
    click(first.querySelector('[aria-label="Zmień wybór"]'));
    expect(first.querySelector('.b-merge__result')).toBeNull();
  });
});

describe('Splitter', () => {
  it('resizes from the keyboard and persists the size', () => {
    localStorage.removeItem('brulion.split.test');
    const r = render(
      <Splitter defaultSize={200} min={100} max={400} storageKey="test">
        <div>lewy</div>
        <div>prawy</div>
      </Splitter>,
    );
    const bar = document.querySelector<HTMLElement>('[role="separator"]')!;
    expect(bar.getAttribute('aria-valuenow')).toBe('200');
    expect(bar.getAttribute('aria-label')).toBe('Zmień rozmiar paneli');
    key(bar, 'ArrowRight');
    expect(bar.getAttribute('aria-valuenow')).toBe('216');
    key(bar, 'ArrowRight', { shiftKey: true });
    expect(bar.getAttribute('aria-valuenow')).toBe('280');
    expect(localStorage.getItem('brulion.split.test')).toBe('280');
    r.unmount();
    render(
      <Splitter defaultSize={200} min={100} max={400} storageKey="test">
        <div>lewy</div>
        <div>prawy</div>
      </Splitter>,
    );
    expect(document.querySelector('[role="separator"]')?.getAttribute('aria-valuenow')).toBe('280');
    expect(document.querySelector<HTMLElement>('.b-split__pane.is-fixed')?.style.width).toBe('280px');
  });
});
