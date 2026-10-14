/**
 * Month-grid date picker with a natural-language input ("jutro o 9", "w piątek 18:00",
 * "15.10") and an optional 24 h time field. Keyboard: arrows (days/weeks), PageUp/PageDown
 * (months, +Shift years), Home/End (week start/end), Enter/Space (select).
 */
import { useEffect, useId, useMemo, useRef, useState, type KeyboardEvent } from 'react';
import { joinDateTime, splitLocal, toLocalDate, weekdayOf } from '@/core/dates';
import type { LocalDate, LocalDateTime } from '@/core/types';
import { Button, IconButton } from './Button';
import {
  dayLabel,
  formatPreview,
  inRange,
  marksFor,
  monthGrid,
  monthTitle,
  moveFocusDate,
  naturalPreview,
  parseTimeInput,
  sameView,
  shiftView,
  stepTime,
  viewOf,
  weekdayHeaders,
  type DayMarks,
  type MonthView,
} from './datepicker';
import { cx } from './hooks';
import { Icon } from './Icon';
import { useLocale, useT } from './i18n';

export interface DatePickerProps {
  value: LocalDate | LocalDateTime | null | undefined;
  onChange: (value: LocalDate | LocalDateTime) => void;
  /** Show the HH:mm field. */
  withTime?: boolean;
  /** With `withTime`: always produce a date-time (day clicks use the current or default time). */
  timeRequired?: boolean;
  /** Time for a newly picked day / a phrase without time (default "09:00"). */
  defaultTime?: string;
  weekStartsOn?: 0 | 1;
  /** Dots under days that have items. */
  marks?: DayMarks;
  /** Show the natural-language input above the grid (default true). */
  naturalInput?: boolean;
  /** Clock for "today" and relative phrases (tests, gallery). */
  now?: Date;
  min?: LocalDate;
  max?: LocalDate;
  /**
   * The user confirmed a value: Enter in a text field, or a day click when there is no time
   * field. Popovers use it to close.
   */
  onCommit?: (value: LocalDate | LocalDateTime) => void;
  /** What to focus on mount. */
  autoFocus?: 'input' | 'grid' | false;
  /** Initial text of the natural-language input (e.g. what the user typed in the editor). */
  initialText?: string;
  showToday?: boolean;
  className?: string;
  'aria-label'?: string;
}

export function DatePicker({
  value,
  onChange,
  withTime = false,
  timeRequired = false,
  defaultTime = '09:00',
  weekStartsOn = 1,
  marks,
  naturalInput = true,
  now,
  min,
  max,
  onCommit,
  autoFocus = false,
  initialText = '',
  showToday = true,
  className,
  ...aria
}: DatePickerProps) {
  const t = useT();
  const locale = useLocale();
  const nowDate = useMemo(() => now ?? new Date(), [now]);
  const today = toLocalDate(nowDate);
  const sp = value ? splitLocal(value) : null;
  const selDate = sp?.date;
  const selTime = sp?.time;

  const [view, setView] = useState<MonthView>(() => viewOf(selDate ?? today));
  const [focusDate, setFocusDate] = useState<LocalDate>(selDate ?? today);
  const [text, setText] = useState(initialText);
  const [timeText, setTimeText] = useState(selTime ?? '');
  const [timeError, setTimeError] = useState(false);
  const timeFocused = useRef(false);
  const focusGrid = useRef(false);
  const gridRef = useRef<HTMLTableElement | null>(null);
  const inputRef = useRef<HTMLInputElement | null>(null);
  const titleId = useId();
  const previewId = useId();
  const timeId = useId();
  const hintId = useId();

  // Follow external value changes.
  useEffect(() => {
    if (!selDate) return;
    setView(viewOf(selDate));
    setFocusDate(selDate);
  }, [selDate]);
  useEffect(() => {
    if (!timeFocused.current) setTimeText(selTime ?? '');
  }, [selTime]);

  const preview = useMemo(
    () =>
      naturalPreview(text, nowDate, {
        locale,
        weekStartsOn,
        withTime,
        defaultTime: withTime ? (selTime ?? (timeRequired ? defaultTime : undefined)) : undefined,
      }),
    [text, nowDate, locale, weekStartsOn, withTime, selTime, timeRequired, defaultTime],
  );
  const previewDate = preview?.date;
  const previewOk = !!preview && inRange(preview.date, min, max);

  // Show the interpreted day in the grid while typing.
  useEffect(() => {
    if (previewDate) setView(viewOf(previewDate));
  }, [previewDate]);

  // Move DOM focus after keyboard navigation in the grid.
  useEffect(() => {
    if (!focusGrid.current) return;
    focusGrid.current = false;
    gridRef.current?.querySelector<HTMLButtonElement>(`[data-date="${focusDate}"]`)?.focus();
  }, [focusDate, view]);

  useEffect(() => {
    if (autoFocus === 'input') inputRef.current?.focus();
    else if (autoFocus === 'grid') gridRef.current?.querySelector<HTMLButtonElement>('button[tabindex="0"]')?.focus();
    // Mount only.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const weeks = useMemo(() => monthGrid(view, weekStartsOn), [view, weekStartsOn]);
  const headers = useMemo(() => weekdayHeaders(locale, weekStartsOn), [locale, weekStartsOn]);
  const inView = (d: LocalDate) => sameView(viewOf(d), view);
  const firstOfView = `${String(view.year).padStart(4, '0')}-${String(view.month).padStart(2, '0')}-01`;
  const tabDate = inView(focusDate)
    ? focusDate
    : selDate && inView(selDate)
      ? selDate
      : inView(today)
        ? today
        : firstOfView;

  const timeFor = (): string | undefined => (withTime ? (selTime ?? (timeRequired ? defaultTime : undefined)) : undefined);

  const selectDay = (d: LocalDate) => {
    if (!inRange(d, min, max)) return;
    const v = joinDateTime(d, timeFor());
    setFocusDate(d);
    setView(viewOf(d));
    onChange(v);
    if (!withTime) onCommit?.(v);
  };

  const applyPreview = () => {
    if (!preview || !previewOk) return;
    onChange(preview.value);
    setText('');
    setTimeText(splitLocal(preview.value)?.time ?? '');
    setTimeError(false);
    onCommit?.(preview.value);
  };

  const onGridKeyDown = (e: KeyboardEvent<HTMLTableElement>) => {
    const active = document.activeElement;
    const base = active instanceof HTMLElement && active.dataset.date ? active.dataset.date : tabDate;
    const next = moveFocusDate(base, e.key, { weekStartsOn, shiftKey: e.shiftKey, min, max });
    if (!next) return;
    e.preventDefault();
    focusGrid.current = true;
    setFocusDate(next);
    setView(viewOf(next));
  };

  const commitTime = (raw: string, commit: boolean) => {
    const day = selDate ?? today;
    if (!raw.trim()) {
      setTimeError(false);
      // A required time can't be cleared: show the kept time again when the edit ends.
      if (timeRequired) {
        if (commit) setTimeText(selTime ?? '');
        return;
      }
      if (selDate && selTime) onChange(selDate);
      return;
    }
    const time = parseTimeInput(raw);
    if (!time) {
      if (commit) setTimeError(true);
      return;
    }
    setTimeError(false);
    const v = joinDateTime(day, time);
    if (v !== value) onChange(v);
    if (commit) setTimeText(time);
  };

  const navMonth = (n: number) => setView((v) => shiftView(v, n));

  return (
    <div className={cx('b-datepicker', className)} role="group" aria-label={aria['aria-label'] ?? t('components.date.calendar')}>
      {naturalInput && (
        <div className="b-datepicker__natural">
          <div className={cx('b-input', 'b-input--md', text && !preview && 'is-invalid')}>
            <span className="b-input__icon">
              <Icon name="sparkles" size={16} />
            </span>
            <input
              ref={inputRef}
              className="b-input__el"
              type="text"
              value={text}
              placeholder={t('components.date.naturalPlaceholder')}
              aria-label={t('components.date.natural')}
              aria-describedby={previewId}
              autoComplete="off"
              spellCheck={false}
              enterKeyHint="done"
              onChange={(e) => setText(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
                  // Apply silently and let the shortcut bubble (e.g. "submit the dialog").
                  if (preview && previewOk) {
                    onChange(preview.value);
                    setText('');
                  }
                } else if (e.key === 'Enter') {
                  e.preventDefault();
                  applyPreview();
                } else if (e.key === 'Escape' && text) {
                  e.preventDefault();
                  e.stopPropagation();
                  setText('');
                }
              }}
            />
          </div>
          <div id={previewId} className="b-datepicker__preview" aria-live="polite">
            {text.trim() &&
              (preview ? (
                <button
                  type="button"
                  className={cx('b-datepicker__preview-btn', !previewOk && 'is-disabled')}
                  disabled={!previewOk}
                  onClick={applyPreview}
                  title={t('components.date.naturalApply', { when: preview.label })}
                >
                  <span aria-hidden="true">→ </span>
                  {preview.label}
                  {preview.partial && <span className="b-datepicker__matched"> („{preview.matched}”)</span>}
                </button>
              ) : (
                <span className="b-datepicker__none">{t('components.date.naturalNone')}</span>
              ))}
          </div>
        </div>
      )}

      <div className="b-datepicker__header">
        <IconButton icon="chevron-left" label={t('components.date.prevMonth')} size="sm" onClick={() => navMonth(-1)} />
        <div id={titleId} className="b-datepicker__title" aria-live="polite">
          {monthTitle(view, locale)}
        </div>
        <IconButton icon="chevron-right" label={t('components.date.nextMonth')} size="sm" onClick={() => navMonth(1)} />
        {showToday && (
          <Button
            size="sm"
            variant="ghost"
            className="b-datepicker__today"
            onClick={() => selectDay(today)}
            disabled={!inRange(today, min, max)}
          >
            {t('components.date.today')}
          </Button>
        )}
      </div>

      <span id={hintId} className="b-sr-only">
        {t('components.date.keyboardHint')}
      </span>
      <table
        ref={gridRef}
        role="grid"
        aria-labelledby={titleId}
        aria-describedby={hintId}
        className="b-datepicker__grid"
        onKeyDown={onGridKeyDown}
      >
        <thead>
          <tr>
            {headers.map((h) => (
              <th key={h.day} scope="col" abbr={h.long} className={cx((h.day === 0 || h.day === 6) && 'is-weekend')}>
                {h.short}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {weeks.map((row) => (
            <tr key={row[0]}>
              {row.map((d) => {
                const selected = d === selDate;
                const dayMarks = marksFor(marks, d);
                const wd = weekdayOf(d);
                const enabled = inRange(d, min, max);
                const label =
                  dayLabel(d, locale) +
                  (dayMarks.length ? `, ${dayMarks.map((m) => t(`components.date.mark.${m}`)).join(', ')}` : '');
                return (
                  <td key={d} role="gridcell" aria-selected={selected}>
                    <button
                      type="button"
                      data-date={d}
                      tabIndex={d === tabDate ? 0 : -1}
                      disabled={!enabled}
                      aria-label={label}
                      aria-current={d === today ? 'date' : undefined}
                      className={cx(
                        'b-day',
                        !inView(d) && 'is-outside',
                        d === today && 'is-today',
                        selected && 'is-selected',
                        d === previewDate && !selected && 'is-preview',
                        (wd === 0 || wd === 6) && 'is-weekend',
                      )}
                      onClick={() => selectDay(d)}
                    >
                      <span className="b-day__num">{Number(d.slice(8, 10))}</span>
                      {dayMarks.length > 0 && (
                        <span className="b-day__marks" aria-hidden="true">
                          {dayMarks.slice(0, 3).map((m, i) => (
                            <span key={i} className={`b-day__mark b-day__mark--${m}`} />
                          ))}
                        </span>
                      )}
                    </button>
                  </td>
                );
              })}
            </tr>
          ))}
        </tbody>
      </table>

      {withTime && (
        <div className={cx('b-datepicker__time', timeError && 'has-error')}>
          <label htmlFor={timeId} className="b-datepicker__time-label">
            <Icon name="clock" size={16} />
            {t('components.date.time')}
          </label>
          <div className={cx('b-input', 'b-input--sm', 'b-datepicker__time-input', timeError && 'is-invalid')}>
            <input
              id={timeId}
              className="b-input__el"
              type="text"
              inputMode="decimal"
              autoComplete="off"
              placeholder={t('components.date.timePlaceholder')}
              value={timeText}
              aria-invalid={timeError || undefined}
              aria-describedby={timeError ? `${timeId}-err` : undefined}
              onFocus={() => {
                timeFocused.current = true;
              }}
              onBlur={(e) => {
                timeFocused.current = false;
                commitTime(e.target.value, true);
              }}
              onChange={(e) => {
                setTimeText(e.target.value);
                if (timeError) setTimeError(false);
                commitTime(e.target.value, false);
              }}
              onKeyDown={(e) => {
                if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
                  e.preventDefault();
                  const step = (e.shiftKey ? 60 : 15) * (e.key === 'ArrowUp' ? 1 : -1);
                  const next = stepTime(parseTimeInput(timeText) ?? selTime, step, defaultTime);
                  setTimeText(next);
                  setTimeError(false);
                  onChange(joinDateTime(selDate ?? today, next));
                } else if (e.key === 'Enter' && !e.ctrlKey && !e.metaKey) {
                  e.preventDefault();
                  const time = parseTimeInput(timeText);
                  if (!time && timeText.trim()) {
                    setTimeError(true);
                    return;
                  }
                  commitTime(timeText, true);
                  // Nothing picked and nothing typed: there is nothing to confirm.
                  if (!time && !selDate) return;
                  const keep = timeRequired ? (selTime ?? defaultTime) : undefined;
                  onCommit?.(joinDateTime(selDate ?? today, time ?? keep));
                }
              }}
            />
            {!timeRequired && selTime && (
              <span className="b-input__end">
                <IconButton
                  icon="x"
                  size="sm"
                  label={t('components.date.noTime')}
                  onClick={() => {
                    setTimeText('');
                    if (selDate) onChange(selDate);
                  }}
                />
              </span>
            )}
          </div>
          {selDate && (
            <span className="b-datepicker__summary" aria-hidden="true">
              {formatPreview(joinDateTime(selDate, selTime), locale, nowDate)}
            </span>
          )}
          {timeError && (
            <div id={`${timeId}-err`} className="b-datepicker__time-error" role="alert">
              {t('components.date.timeInvalid')}
            </div>
          )}
        </div>
      )}
    </div>
  );
}
