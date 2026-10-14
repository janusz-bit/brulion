// @vitest-environment jsdom
import { useState } from 'react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { LocalDate, LocalDateTime } from '@/core/types';
import { DatePicker, type DatePickerProps } from './DatePicker';
import { LocaleContext } from './i18n';
import { cleanup, click, key, render, type } from './render.testutil';

afterEach(cleanup);
// jsdom + React renders are slow on the Raspberry Pi dev machine under load.
vi.setConfig({ testTimeout: 30_000 });

const NOW = new Date(2026, 9, 14, 10, 30);

function Controlled(props: Partial<DatePickerProps> & { initial?: LocalDate | LocalDateTime | null; spy?: (v: string) => void }) {
  const { initial = null, spy, ...rest } = props;
  const [value, setValue] = useState<LocalDate | LocalDateTime | null>(initial);
  return (
    <>
      <output id="value">{value ?? ''}</output>
      <DatePicker
        now={NOW}
        {...rest}
        value={value}
        onChange={(v) => {
          spy?.(v);
          setValue(v);
        }}
      />
    </>
  );
}

const valueOf = () => document.getElementById('value')?.textContent;
const title = () => document.querySelector('.b-datepicker__title')?.textContent;
const day = (d: string) => document.querySelector<HTMLButtonElement>(`[data-date="${d}"]`);

describe('DatePicker', () => {
  it('shows the month of the value with Polish names, today and selection', () => {
    render(<Controlled initial="2026-10-20" />);
    expect(title()).toBe('Październik 2026');
    const headers = [...document.querySelectorAll('th')].map((th) => th.textContent);
    expect(headers).toEqual(['pn', 'wt', 'śr', 'czw', 'pt', 'sob', 'nd']);
    expect(day('2026-10-14')?.getAttribute('aria-current')).toBe('date');
    expect(day('2026-10-20')?.closest('td')?.getAttribute('aria-selected')).toBe('true');
    expect(day('2026-10-20')?.tabIndex).toBe(0);
    expect(day('2026-10-21')?.tabIndex).toBe(-1);
    expect(day('2026-10-20')?.getAttribute('aria-label')).toBe('wtorek, 20 października 2026');
  });

  it('speaks English when the locale context says so', () => {
    render(
      <LocaleContext.Provider value="en">
        <Controlled initial="2026-10-20" weekStartsOn={0} />
      </LocaleContext.Provider>,
    );
    expect(title()).toBe('October 2026');
    expect(document.querySelector('th')?.textContent).toBe('Sun');
  });

  it('selects a day on click and commits when there is no time field', () => {
    const commit = vi.fn();
    render(<Controlled initial="2026-10-20" onCommit={commit} />);
    click(day('2026-10-22'));
    expect(valueOf()).toBe('2026-10-22');
    expect(commit).toHaveBeenCalledWith('2026-10-22');
  });

  it('navigates with the keyboard (arrows, PageUp/PageDown, Home/End) and selects with Enter', () => {
    render(<Controlled initial="2026-10-20" />);
    day('2026-10-20')!.focus();
    key(document.activeElement, 'ArrowRight');
    expect((document.activeElement as HTMLElement).dataset.date).toBe('2026-10-21');
    key(document.activeElement, 'ArrowDown');
    expect((document.activeElement as HTMLElement).dataset.date).toBe('2026-10-28');
    key(document.activeElement, 'End');
    expect((document.activeElement as HTMLElement).dataset.date).toBe('2026-11-01');
    expect(title()).toBe('Listopad 2026');
    key(document.activeElement, 'PageUp');
    expect((document.activeElement as HTMLElement).dataset.date).toBe('2026-10-01');
    key(document.activeElement, 'Home');
    expect((document.activeElement as HTMLElement).dataset.date).toBe('2026-09-28');
    key(document.activeElement, 'PageDown', { shiftKey: true });
    expect((document.activeElement as HTMLElement).dataset.date).toBe('2027-09-28');
    expect(title()).toBe('Wrzesień 2027');
    // Enter activates the focused day button (browsers turn Enter into a click).
    click(document.activeElement);
    expect(valueOf()).toBe('2027-09-28');
  });

  it('month buttons change the view without changing the value', () => {
    render(<Controlled initial="2026-10-20" />);
    click(document.querySelector('[aria-label="Następny miesiąc"]'));
    expect(title()).toBe('Listopad 2026');
    click(document.querySelector('[aria-label="Poprzedni miesiąc"]'));
    click(document.querySelector('[aria-label="Poprzedni miesiąc"]'));
    expect(title()).toBe('Wrzesień 2026');
    expect(valueOf()).toBe('2026-10-20');
  });

  it('previews a natural-language phrase and applies it on Enter', () => {
    const commit = vi.fn();
    render(<Controlled withTime timeRequired onCommit={commit} />);
    const input = document.querySelector<HTMLInputElement>('input[aria-label="Data słownie"]')!;
    type(input, 'jutro o 9');
    const preview = document.querySelector('.b-datepicker__preview')!;
    expect(preview.textContent).toBe('→ czw, 15 paź 09:00');
    expect(day('2026-10-15')?.className).toContain('is-preview');
    key(input, 'Enter');
    expect(valueOf()).toBe('2026-10-15T09:00');
    expect(commit).toHaveBeenCalledWith('2026-10-15T09:00');
    expect(input.value).toBe('');
    expect(document.querySelector<HTMLInputElement>('.b-datepicker__time input')!.value).toBe('09:00');
  });

  it('jumps the grid to a typed date in another month and reports unknown phrases', () => {
    render(<Controlled />);
    const input = document.querySelector<HTMLInputElement>('input[aria-label="Data słownie"]')!;
    type(input, '3.12');
    expect(title()).toBe('Grudzień 2026');
    expect(document.querySelector('.b-datepicker__preview')!.textContent).toBe('→ czw, 3 gru');
    type(input, 'pojutrze?? nie, kiedyś');
    type(input, 'zupełnie nic');
    expect(document.querySelector('.b-datepicker__preview')!.textContent).toBe('Nie rozpoznano daty');
    key(input, 'Enter');
    expect(valueOf()).toBe('');
  });

  it('Escape in the phrase input clears it without bubbling', () => {
    const outer = vi.fn();
    render(
      <div onKeyDown={(e) => outer(e.key)}>
        <Controlled />
      </div>,
    );
    const input = document.querySelector<HTMLInputElement>('input[aria-label="Data słownie"]')!;
    type(input, 'jutro');
    key(input, 'Escape');
    expect(input.value).toBe('');
    expect(outer).not.toHaveBeenCalled();
    key(input, 'Escape');
    expect(outer).toHaveBeenCalledWith('Escape');
  });

  it('edits the time (typing, arrow keys) and keeps it when another day is picked', () => {
    render(<Controlled withTime timeRequired initial="2026-10-15T09:00" />);
    const time = document.querySelector<HTMLInputElement>('.b-datepicker__time input')!;
    time.focus();
    type(time, '1830');
    expect(valueOf()).toBe('2026-10-15T18:30');
    key(time, 'ArrowUp');
    expect(valueOf()).toBe('2026-10-15T18:45');
    // Shift steps by an hour on the hour grid.
    key(time, 'ArrowDown', { shiftKey: true });
    expect(valueOf()).toBe('2026-10-15T18:00');
    key(time, 'ArrowDown', { shiftKey: true });
    expect(valueOf()).toBe('2026-10-15T17:00');
    click(day('2026-10-22'));
    expect(valueOf()).toBe('2026-10-22T17:00');
  });

  it('flags an invalid time on blur and keeps the last valid value', () => {
    render(<Controlled withTime timeRequired initial="2026-10-15T09:00" />);
    const time = document.querySelector<HTMLInputElement>('.b-datepicker__time input')!;
    time.focus();
    type(time, '25:99');
    const { act } = require('react') as typeof import('react');
    act(() => time.blur());
    expect(document.querySelector('.b-datepicker__time-error')?.textContent).toBe('Wpisz godzinę jako GG:MM, np. 9:30');
    expect(valueOf()).toBe('2026-10-15T09:00');
  });

  it('uses the default time for a picked day when time is required', () => {
    render(<Controlled withTime timeRequired defaultTime="08:15" />);
    click(day('2026-10-16'));
    expect(valueOf()).toBe('2026-10-16T08:15');
  });

  it('disables days outside min/max', () => {
    const spy = vi.fn();
    render(<Controlled initial="2026-10-15" min="2026-10-10" max="2026-10-20" spy={spy} />);
    expect(day('2026-10-09')?.disabled).toBe(true);
    expect(day('2026-10-21')?.disabled).toBe(true);
    click(day('2026-10-21'));
    expect(spy).not.toHaveBeenCalled();
  });

  it('renders marks and names them for screen readers', () => {
    render(<Controlled initial="2026-10-15" marks={{ '2026-10-16': ['task', 'reminder'] }} />);
    expect(day('2026-10-16')?.querySelectorAll('.b-day__mark')).toHaveLength(2);
    expect(day('2026-10-16')?.getAttribute('aria-label')).toBe('piątek, 16 października 2026, zadania, przypomnienie');
  });
});
