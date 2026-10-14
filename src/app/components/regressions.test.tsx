// @vitest-environment jsdom
/**
 * Regression tests for review fixes: state that must not leak between inputs (DiffView,
 * MergeView), rendering caps, promise handling, time-field edge cases, ResizeObserver
 * lifecycle and the ticking clock.
 */
import { act, createRef, useRef, useState } from 'react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { DatePicker } from './DatePicker';
import { DIFF_RENDER_STEP, DiffView } from './DiffView';
import { useNow } from './hooks';
import { MergeView } from './MergeView';
import type { MergeHunk } from './merge';
import { ConfirmDialog, Modal } from './Modal';
import { Popover, type PopoverCloseReason } from './Popover';
import { byText, cleanup, click, key, render, type } from './render.testutil';
import { VirtualList, type VirtualListHandle } from './VirtualList';

afterEach(() => {
  cleanup();
  vi.useRealTimers();
  vi.unstubAllGlobals();
});
// jsdom + React renders are slow on the Raspberry Pi dev machine under load.
vi.setConfig({ testTimeout: 30_000 });

const lines = (n: number, f: (i: number) => string) => Array.from({ length: n }, (_, i) => f(i)).join('\n');

describe('DiffView', () => {
  const left = lines(20, (i) => `wiersz ${i}`);
  const right = left.replace('wiersz 10', 'wiersz dziesiąty');

  it('folds again when the compared texts change', () => {
    const r = render(<DiffView left={left} right={right} mode="unified" context={1} />);
    const gaps = () => r.container.querySelectorAll('.b-diff__gap').length;
    expect(gaps()).toBe(2);
    click(r.container.querySelector('.b-diff__expand'));
    expect(gaps()).toBe(1);
    // A different pair of texts: the expanded gap (indexed by row) must not carry over.
    r.rerender(<DiffView left={left} right={right.replace('wiersz 3', 'wiersz trzeci')} mode="unified" context={1} />);
    expect(gaps()).toBe(3);
  });

  it('folds again after switching between split and unified', () => {
    const r = render(<DiffView left={left} right={right} mode="unified" context={1} />);
    click(r.container.querySelector('.b-diff__expand'));
    expect(r.container.querySelectorAll('.b-diff__gap')).toHaveLength(1);
    r.rerender(<DiffView left={left} right={right} mode="split" context={1} />);
    expect(r.container.querySelectorAll('.b-diff__gap')).toHaveLength(2);
  });

  // Thousands of jsdom rows: slow on the loaded Raspberry Pi.
  it('renders huge diffs in steps with a "show more" row', { timeout: 120_000 }, () => {
    // All lines differ: 2n rows, just over one render step.
    const n = DIFF_RENDER_STEP / 2 + 50;
    const a = lines(n, (i) => `a${i}`);
    const b = lines(n, (i) => `b${i}`);
    const r = render(<DiffView left={a} right={b} mode="unified" maxEdits={10} />);
    expect(r.container.querySelector('.b-diff__notice')).not.toBeNull();
    expect(r.container.querySelectorAll('tr.b-diff__row')).toHaveLength(DIFF_RENDER_STEP);
    const more = byText(r.container, '.b-diff__expand', /Pokaż dalej/);
    expect(more?.textContent).toContain(`${2 * n - DIFF_RENDER_STEP}`);
    click(more);
    expect(r.container.querySelectorAll('tr.b-diff__row')).toHaveLength(2 * n);
    expect(byText(r.container, '.b-diff__expand', /Pokaż dalej/)).toBeUndefined();
  });
});

describe('MergeView', () => {
  const hunks = (theirs: string): MergeHunk[] => [
    { kind: 'ok', lines: ['# Zakupy'] },
    { kind: 'conflict', base: ['- chleb'], local: ['- chleb żytni'], remote: [theirs] },
  ];

  it('drops resolutions made for previous hunks', () => {
    const onResolve = vi.fn();
    const first = hunks('- chleb razowy');
    const r = render(<MergeView hunks={first} onResolve={onResolve} />);
    click(byText(r.container, 'button', 'Ich wersja'));
    expect(r.container.querySelector('.b-merge__conflict.is-resolved')).not.toBeNull();
    // The file changed again while the view was open: new hunks start unresolved.
    r.rerender(<MergeView hunks={hunks('- bułki')} onResolve={onResolve} />);
    expect(r.container.querySelector('.b-merge__conflict.is-resolved')).toBeNull();
    const save = byText(r.container, 'button', 'Zapisz scalenie') as HTMLButtonElement;
    expect(save.disabled).toBe(true);
    click(byText(r.container, 'button', 'Ich wersja'));
    click(byText(r.container, 'button', 'Zapisz scalenie'));
    expect(onResolve).toHaveBeenCalledWith('# Zakupy\n- bułki\n');
  });
});

describe('ConfirmDialog', () => {
  it('stays open and usable when onConfirm rejects, without an unhandled rejection', async () => {
    const unhandled = vi.fn();
    process.on('unhandledRejection', unhandled);
    try {
      let reject: (e: Error) => void = () => {};
      const onConfirm = vi.fn(() => new Promise<void>((_, rej) => (reject = rej)));
      const r = render(<ConfirmDialog open title="Usunąć?" onConfirm={onConfirm} onCancel={() => {}} inline />);
      const confirm = byText(r.container, 'button', 'Potwierdź') ?? r.container.querySelector('.b-btn--primary');
      click(confirm);
      expect(confirm?.getAttribute('aria-busy')).toBe('true');
      await act(async () => {
        reject(new Error('brak uprawnień'));
        await new Promise((res) => setTimeout(res, 0));
      });
      expect(confirm?.getAttribute('aria-busy')).toBeNull();
      expect(unhandled).not.toHaveBeenCalled();
    } finally {
      process.off('unhandledRejection', unhandled);
    }
  });
});

describe('DatePicker time field', () => {
  function Harness({ onCommit }: { onCommit: (v: string) => void }) {
    const [v, setV] = useState<string | null>('2026-10-15T18:30');
    return <DatePicker value={v} onChange={setV} withTime timeRequired onCommit={onCommit} now={new Date(2026, 9, 14, 10, 30)} />;
  }

  it('keeps the required time when the field is emptied (Enter and blur)', () => {
    const onCommit = vi.fn();
    const r = render(<Harness onCommit={onCommit} />);
    const input = r.container.querySelector<HTMLInputElement>('.b-datepicker__time input')!;
    input.focus();
    type(input, '');
    key(input, 'Enter');
    expect(onCommit).toHaveBeenCalledWith('2026-10-15T18:30');
    expect(input.value).toBe('18:30');
    type(input, '');
    act(() => input.blur());
    expect(input.value).toBe('18:30');
  });

  it('does not commit when nothing was picked or typed', () => {
    const onCommit = vi.fn();
    const r = render(<DatePicker value={null} onChange={() => {}} withTime onCommit={onCommit} />);
    key(r.container.querySelector('.b-datepicker__time input'), 'Enter');
    expect(onCommit).not.toHaveBeenCalled();
  });
});

describe('VirtualList (measured rows)', () => {
  it('observes rendered rows and unobserves rows that scroll away', () => {
    const observed = new Set<Element>();
    class FakeRO {
      constructor(_cb: ResizeObserverCallback) {}
      observe(el: Element) {
        observed.add(el);
      }
      unobserve(el: Element) {
        observed.delete(el);
      }
      disconnect() {
        observed.clear();
      }
    }
    vi.stubGlobal('ResizeObserver', FakeRO);
    const items = Array.from({ length: 1000 }, (_, i) => `Notatka ${i}`);
    const handle = createRef<VirtualListHandle>();
    const r = render(
      <VirtualList
        ref={handle}
        items={items}
        rowHeight={30}
        measure
        initialHeight={300}
        overscan={0}
        renderRow={(it) => <div>{it}</div>}
      />,
    );
    const rows = () => r.container.querySelectorAll('.b-vlist__row');
    // First commit: rows are observed even though their refs ran before any effect.
    expect(rows().length).toBeGreaterThan(0);
    for (const row of rows()) expect(observed.has(row)).toBe(true);
    const firstRows = [...rows()];
    act(() => handle.current!.scrollToOffset(20_000));
    expect(r.container.querySelector('.b-vlist__row[data-index="0"]')).toBeNull();
    for (const row of firstRows) expect(observed.has(row)).toBe(false);
    // (The scroller itself is observed too, for the viewport height.)
    const observedRows = [...observed].filter((el) => el.classList.contains('b-vlist__row'));
    for (const el of observedRows) expect(el.isConnected).toBe(true);
    expect(observedRows).toHaveLength(rows().length);
    r.unmount();
    expect(observed.size).toBe(0);
  });
});

describe('useNow', () => {
  function Clock({ fixed }: { fixed?: Date }) {
    const now = useNow(fixed, 1000);
    return <span>{now.getTime()}</span>;
  }

  it('ticks without a fixed time and stays put with one', () => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date(2026, 9, 14, 10, 30, 0));
    const r = render(<Clock />);
    const t0 = Number(r.container.textContent);
    vi.setSystemTime(new Date(2026, 9, 14, 10, 31, 0));
    act(() => {
      vi.advanceTimersByTime(1000);
    });
    expect(Number(r.container.textContent)).toBeGreaterThan(t0);
    const fixed = new Date(2026, 0, 1);
    r.rerender(<Clock fixed={fixed} />);
    act(() => {
      vi.advanceTimersByTime(5000);
    });
    expect(Number(r.container.textContent)).toBe(fixed.getTime());
  });
});

describe('Popover inside a modal', () => {
  function Nested({ onPopClose }: { onPopClose: (r: PopoverCloseReason) => void }) {
    const anchor = useRef<HTMLButtonElement | null>(null);
    const [pop, setPop] = useState(true);
    return (
      <Modal open onClose={() => {}} title="Okno">
        <button ref={anchor}>kotwica</button>
        <button id="other">inny</button>
        <Popover
          open={pop}
          anchor={anchor}
          aria-label="Wybór daty"
          onClose={(reason) => {
            onPopClose(reason);
            setPop(false);
          }}
        >
          <button id="first">pierwszy</button>
          <button id="second">drugi</button>
        </Popover>
      </Modal>
    );
  }

  it('lives inside the aria-modal root, keeps its own Tab order and closes when focus leaves it', () => {
    const onPopClose = vi.fn();
    render(<Nested onPopClose={onPopClose} />);
    const panel = document.querySelector('[aria-label="Wybór daty"]')!;
    // Outside the modal root the popover would be inert for screen readers.
    expect(panel.closest('.b-modal-root')).not.toBeNull();
    // (Mounted together with the dialog, whose initial focus runs last; focus it like a user.)
    act(() => document.getElementById('first')!.focus());
    // Tab inside the popover is not grabbed by the dialog's focus trap.
    const ev = key(document.getElementById('first'), 'Tab');
    expect(ev.defaultPrevented).toBe(false);
    // Focus moving elsewhere in the dialog closes the popover.
    act(() => document.getElementById('other')!.focus());
    expect(onPopClose).toHaveBeenCalledWith('focus-out');
  });
});
