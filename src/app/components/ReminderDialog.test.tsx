// @vitest-environment jsdom
import { afterEach, describe, expect, it, vi } from 'vitest';
import { reminderResult, ReminderDialog } from './ReminderDialog';
import { DEFAULT_REPEAT } from './reminder';
import { byText, cleanup, click, key, render, type } from './render.testutil';

afterEach(cleanup);
// jsdom + React renders are slow on the Raspberry Pi dev machine under load.
vi.setConfig({ testTimeout: 30_000 });

const NOW = new Date(2026, 9, 14, 10, 30);

const dialog = () => document.querySelector<HTMLElement>('[role="dialog"]')!;
const button = (text: string | RegExp) => byText(dialog(), 'button', text);
const radio = (text: string) => byText(dialog(), '[role="radio"]', text);

describe('ReminderDialog', () => {
  it('sets a weekly alarm via quick pick, recurrence chips and the alarm switch', () => {
    const onSubmit = vi.fn();
    render(<ReminderDialog open onClose={() => {}} onSubmit={onSubmit} now={NOW} />);
    expect(dialog().getAttribute('aria-labelledby')).toBeTruthy();
    click(button('jutro rano 8:00'));
    expect(button('jutro rano 8:00')?.getAttribute('aria-pressed')).toBe('true');
    click(radio('co tydzień'));
    expect(radio('co tydzień')?.getAttribute('aria-checked')).toBe('true');
    // Weekly defaults to the reminder's weekday (Thursday) and shows the Polish label + file text.
    expect(dialog().querySelector('.b-reminder__rule-label')?.textContent).toBe('co tydzień w czwartek');
    expect(dialog().querySelector('.b-reminder__rule-file')?.textContent).toBe('W pliku: every week on Thursday');
    // Add Monday.
    click(dialog().querySelector('.b-weekday[aria-label="poniedziałek"]'));
    expect(dialog().querySelector('.b-reminder__rule-label')?.textContent).toBe('co tydzień w poniedziałek, czwartek');
    click(dialog().querySelector('[role="switch"]'));
    expect(dialog().querySelector('[role="switch"]')?.getAttribute('aria-checked')).toBe('true');
    expect(dialog().querySelector('.b-reminder__summary')?.textContent).toBe('czw, 15 paź 08:00, co tydzień w poniedziałek, czwartek');
    click(button('Ustaw przypomnienie'));
    expect(onSubmit).toHaveBeenCalledWith({
      at: '2026-10-15T08:00',
      alarm: true,
      recurrence: 'every week on Monday, Thursday',
    });
  });

  it('builds a custom interval rule', () => {
    const onSubmit = vi.fn();
    render(<ReminderDialog open onClose={() => {}} onSubmit={onSubmit} now={NOW} initial={{ at: '2026-10-20T07:00' }} />);
    click(radio('niestandardowo…'));
    const interval = dialog().querySelector<HTMLInputElement>('input[type="number"]')!;
    type(interval, '3');
    const unit = dialog().querySelector<HTMLSelectElement>('.b-reminder__custom select')!;
    const { act } = require('react') as typeof import('react');
    act(() => {
      unit.value = 'month';
      unit.dispatchEvent(new Event('change', { bubbles: true }));
    });
    expect(dialog().querySelector('.b-reminder__rule-label')?.textContent).toBe('co 3 miesiące');
    expect([...unit.options].map((o) => o.textContent)).toEqual(['dni', 'tygodnie', 'miesiące', 'lata']);
    click(button('Ustaw przypomnienie'));
    expect(onSubmit).toHaveBeenCalledWith({ at: '2026-10-20T07:00', alarm: false, recurrence: 'every 3 months' });
  });

  it('accepts a natural-language date and edits an existing reminder', () => {
    const onSubmit = vi.fn();
    const onRemove = vi.fn();
    render(
      <ReminderDialog
        open
        onClose={() => {}}
        onSubmit={onSubmit}
        onRemove={onRemove}
        now={NOW}
        initial={{ at: '2026-10-15T09:00', alarm: true, recurrence: 'co tydzień w czwartek' }}
      />,
    );
    expect(document.querySelector('.b-modal__title')?.textContent).toBe('Zmień przypomnienie');
    expect(radio('co tydzień')?.getAttribute('aria-checked')).toBe('true');
    const input = dialog().querySelector<HTMLInputElement>('input[aria-label="Data słownie"]')!;
    type(input, 'w piątek 18:00');
    key(input, 'Enter');
    // Enter applies the phrase; the dialog stays open (no submit yet).
    expect(onSubmit).not.toHaveBeenCalled();
    key(input, 'Enter', { ctrlKey: true });
    expect(onSubmit).toHaveBeenCalledWith({ at: '2026-10-16T18:00', alarm: true, recurrence: 'every week on Thursday' });
    // Ctrl+Enter straight after typing applies the phrase and submits in one go.
    type(input, 'pojutrze o 7');
    key(input, 'Enter', { ctrlKey: true });
    expect(onSubmit).toHaveBeenLastCalledWith({ at: '2026-10-16T07:00', alarm: true, recurrence: 'every week on Thursday' });
    click(button('Usuń przypomnienie'));
    expect(onRemove).toHaveBeenCalled();
  });

  it('refuses to submit without a date and warns about past times', () => {
    const onSubmit = vi.fn();
    render(<ReminderDialog open onClose={() => {}} onSubmit={onSubmit} now={NOW} />);
    click(button('Ustaw przypomnienie'));
    expect(onSubmit).not.toHaveBeenCalled();
    expect(dialog().querySelector('.b-reminder__hint.is-error')?.textContent).toBe('Wybierz datę i godzinę.');
    // "tonight" is still ahead at 10:30; a past day shows the warning.
    click(dialog().querySelector('[data-date="2026-10-13"]'));
    expect(dialog().querySelector('.b-reminder__warning')?.textContent).toBe('Ten termin już minął.');
  });

  it('closes on Cancel and Escape', () => {
    const onClose = vi.fn();
    render(<ReminderDialog open onClose={onClose} onSubmit={() => {}} now={NOW} />);
    click(button('Anuluj'));
    key(document.activeElement, 'Escape');
    expect(onClose).toHaveBeenCalledTimes(2);
  });
});

describe('reminderResult', () => {
  it('requires a date-time and adds canonical recurrence text', () => {
    expect(reminderResult({ at: null, alarm: false, repeat: DEFAULT_REPEAT })).toEqual({ ok: false, reason: 'missing' });
    expect(reminderResult({ at: '2026-10-15', alarm: false, repeat: DEFAULT_REPEAT })).toEqual({ ok: false, reason: 'missing' });
    expect(reminderResult({ at: '2026-10-15T09:00', alarm: false, repeat: DEFAULT_REPEAT })).toEqual({
      ok: true,
      value: { at: '2026-10-15T09:00', alarm: false },
    });
    expect(
      reminderResult({ at: '2026-10-15T09:00', alarm: true, repeat: { ...DEFAULT_REPEAT, kind: 'weekdays' } }),
    ).toEqual({ ok: true, value: { at: '2026-10-15T09:00', alarm: true, recurrence: 'every weekday' } });
  });
});
