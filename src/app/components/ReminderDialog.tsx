/**
 * Reminder editor: quick picks, date + time (natural language too), alarm switch and a
 * recurrence picker. Produces `{ at, alarm, recurrence? }` with canonical English rule text.
 */
import { useEffect, useId, useMemo, useState } from 'react';
import { compareLocal, isLocalDateTime, toLocalDateTime, weekdayShortName, weekdayName } from '@/core/dates';
import type { LocalDate, LocalDateTime } from '@/core/types';
import { Chip } from './Badge';
import { Button } from './Button';
import type { DayMarks } from './datepicker';
import { formatPreview } from './datepicker';
import { DatePicker } from './DatePicker';
import { Select, Switch, TextInput } from './forms';
import { cx, useNow } from './hooks';
import { Icon } from './Icon';
import { useLocale, useT } from './i18n';
import { Modal } from './Modal';
import {
  describeDraft,
  quickPicks,
  REPEAT_KINDS,
  ruleToDraft,
  toggleWeekday,
  type RepeatDraft,
  type RepeatKind,
  type RepeatUnit,
  type ReminderValue,
} from './reminder';
import { SegmentedControl } from './Tabs';

export interface ReminderDraftState {
  at: LocalDate | LocalDateTime | null;
  setAt: (v: LocalDate | LocalDateTime | null) => void;
  alarm: boolean;
  setAlarm: (v: boolean) => void;
  repeat: RepeatDraft;
  setRepeat: (v: RepeatDraft) => void;
}

/** State for <ReminderFields>; initialise from an existing reminder when editing. */
export function useReminderDraft(initial?: Partial<ReminderValue>): ReminderDraftState {
  const [at, setAt] = useState<LocalDate | LocalDateTime | null>(initial?.at ?? null);
  const [alarm, setAlarm] = useState(initial?.alarm ?? false);
  const [repeat, setRepeat] = useState<RepeatDraft>(() => ruleToDraft(initial?.recurrence));
  return { at, setAt, alarm, setAlarm, repeat, setRepeat };
}

/** Validate a draft; returns the dialog result or the reason it is not ready. */
export function reminderResult(
  s: Pick<ReminderDraftState, 'at' | 'alarm' | 'repeat'>,
): { ok: true; value: ReminderValue } | { ok: false; reason: 'missing' } {
  if (!s.at || !isLocalDateTime(s.at)) return { ok: false, reason: 'missing' };
  const desc = describeDraft(s.repeat, s.at, 'en');
  const value: ReminderValue = { at: s.at, alarm: s.alarm };
  if (desc) value.recurrence = desc.canonical;
  return { ok: true, value };
}

const UNITS: readonly RepeatUnit[] = ['day', 'week', 'month', 'year'];

export interface ReminderFieldsProps {
  state: ReminderDraftState;
  now?: Date;
  weekStartsOn?: 0 | 1;
  marks?: DayMarks;
  defaultTime?: string;
  /** Text of the block the reminder belongs to (context line). */
  context?: string;
  /** Show the "missing date" message (after a submit attempt). */
  showErrors?: boolean;
  /**
   * Ctrl/⌘+Enter inside the fields (submit shortcut). Fields may update the state in the same
   * event, so read the state after the next render (see ReminderDialog).
   */
  onSubmit?: () => void;
  className?: string;
}

/** The form body (no dialog chrome) — reusable in a popover or a settings page. */
export function ReminderFields({
  state,
  now,
  weekStartsOn = 1,
  marks,
  defaultTime = '09:00',
  context,
  showErrors = false,
  onSubmit,
  className,
}: ReminderFieldsProps) {
  const t = useT();
  const locale = useLocale();
  const clock = useNow(now);
  const picks = useMemo(() => quickPicks(clock), [clock]);
  const repeatLabelId = useId();
  const weekdaysLabelId = useId();
  const { at, setAt, alarm, setAlarm, repeat, setRepeat } = state;
  // The interval field may be empty while typing; the draft keeps the last valid number.
  const [intervalText, setIntervalText] = useState(String(repeat.interval));
  const desc = describeDraft(repeat, at, locale);
  const canonical = describeDraft(repeat, at, 'en')?.canonical;
  const past = !!at && isLocalDateTime(at) && compareLocal(at, toLocalDateTime(clock)) <= 0;
  const missing = showErrors && (!at || !isLocalDateTime(at));
  const showWeekdays = repeat.kind === 'weekly' || (repeat.kind === 'custom' && repeat.unit === 'week');
  const weekOrder = weekStartsOn === 1 ? [1, 2, 3, 4, 5, 6, 0] : [0, 1, 2, 3, 4, 5, 6];
  // Weekly without explicit days repeats on the reminder's weekday: show it as chosen.
  const effectiveDays = repeat.weekdays.length > 0 ? repeat.weekdays : (describeDraft(repeat, at, 'en')?.rule.weekdays ?? []);

  const setKind = (kind: RepeatKind) => setRepeat({ ...repeat, kind });

  return (
    <div
      className={cx('b-reminder', className)}
      onKeyDown={(e) => {
        if (e.key === 'Enter' && (e.ctrlKey || e.metaKey) && onSubmit) {
          e.preventDefault();
          onSubmit();
        }
      }}
    >
      {context && (
        <p className="b-reminder__context">
          <Icon name="file" size={14} />
          <span>{context}</span>
        </p>
      )}

      <div className="b-reminder__col">
        <section className="b-reminder__section" aria-label={t('components.reminder.quick')}>
          <div className="b-reminder__quick">
            {picks.map((p) => (
              <Chip
                key={p.id}
                size="md"
                icon="clock"
                selected={at === p.at}
                disabled={p.disabled}
                onClick={() => setAt(p.at)}
                title={formatPreview(p.at, locale, clock)}
              >
                {t(`components.reminder.quick.${p.id}`)}
              </Chip>
            ))}
          </div>
        </section>

        <section className="b-reminder__section">
          <DatePicker
            value={at}
            onChange={setAt}
            withTime
            timeRequired
            defaultTime={defaultTime}
            weekStartsOn={weekStartsOn}
            marks={marks}
            now={clock}
            aria-label={t('components.reminder.when')}
          />
        </section>
      </div>

      <div className="b-reminder__col">
        <section className="b-reminder__section">
          <Switch
            checked={alarm}
            onCheckedChange={setAlarm}
            label={t('components.reminder.alarm')}
            description={t('components.reminder.alarmHint')}
            icon="alarm"
          />
        </section>

        <section className="b-reminder__section" aria-labelledby={repeatLabelId}>
          <div id={repeatLabelId} className="b-reminder__heading">
            <Icon name="repeat" size={16} />
            {t('components.reminder.repeat')}
          </div>
          <SegmentedControl
            variant="chips"
            size="sm"
            aria-labelledby={repeatLabelId}
            value={repeat.kind}
            onChange={setKind}
            options={REPEAT_KINDS.map((k) => ({ value: k, label: t(`components.reminder.repeat.${k}`) }))}
          />

          {repeat.kind === 'custom' && (
            <div className="b-reminder__custom">
              <span className="b-reminder__every">{t('components.reminder.repeat.every')}</span>
              <TextInput
                type="number"
                min={1}
                max={999}
                inputMode="numeric"
                controlSize="sm"
                className="b-reminder__interval"
                aria-label={t('components.reminder.repeat.interval')}
                value={intervalText}
                onValueChange={(v) => {
                  setIntervalText(v);
                  const n = Math.floor(Number(v));
                  if (v.trim() && Number.isFinite(n) && n >= 1) setRepeat({ ...repeat, interval: Math.min(n, 999) });
                }}
                onBlur={() => setIntervalText(String(repeat.interval))}
              />
              <Select
                controlSize="sm"
                aria-label={t('components.reminder.repeat.unit')}
                value={repeat.unit}
                onValueChange={(v) => setRepeat({ ...repeat, unit: v as RepeatUnit })}
                options={UNITS.map((u) => ({ value: u, label: t(`components.reminder.unit.${u}`, { count: repeat.interval }) }))}
              />
            </div>
          )}

          {showWeekdays && (
            <div className="b-reminder__weekdays" role="group" aria-labelledby={weekdaysLabelId}>
              <span id={weekdaysLabelId} className="b-sr-only">
                {t('components.reminder.repeat.on')}
              </span>
              {weekOrder.map((d) => {
                const on = effectiveDays.includes(d);
                return (
                  <button
                    key={d}
                    type="button"
                    className={cx('b-weekday', on && 'is-on')}
                    aria-pressed={on}
                    aria-label={weekdayName(d, locale)}
                    onClick={() => {
                      const next = toggleWeekday(effectiveDays, d);
                      setRepeat({ ...repeat, weekdays: next });
                    }}
                  >
                    {weekdayShortName(d, locale)}
                  </button>
                );
              })}
            </div>
          )}

          {desc && (
            <div className="b-reminder__rule">
              <div className="b-reminder__rule-label">{desc.label}</div>
              {canonical && locale !== 'en' && (
                <div className="b-reminder__rule-file">{t('components.reminder.fileText', { text: canonical })}</div>
              )}
            </div>
          )}
        </section>

        <div className="b-reminder__summary" aria-live="polite">
          {at && isLocalDateTime(at) ? (
            <>
              <Icon name={alarm ? 'alarm' : 'bell'} size={16} />
              <span>
                {desc
                  ? t('components.reminder.summaryRepeat', { when: formatPreview(at, locale, clock), rule: desc.label })
                  : t('components.reminder.summary', { when: formatPreview(at, locale, clock) })}
              </span>
            </>
          ) : (
            <span className={cx('b-reminder__hint', missing && 'is-error')}>{t('components.reminder.missing')}</span>
          )}
        </div>
        {past && (
          <p className="b-reminder__warning" role="status">
            <Icon name="warning" size={14} />
            {t('components.reminder.past')}
          </p>
        )}
      </div>
    </div>
  );
}

export interface ReminderDialogProps {
  open: boolean;
  onClose: () => void;
  onSubmit: (value: ReminderValue) => void;
  /** Shows "Usuń przypomnienie" (editing an existing reminder). */
  onRemove?: () => void;
  initial?: Partial<ReminderValue>;
  now?: Date;
  weekStartsOn?: 0 | 1;
  marks?: DayMarks;
  defaultTime?: string;
  title?: string;
  context?: string;
  container?: Element | null;
  /** Render in place (see Modal `inline`). */
  inline?: boolean;
  /** Force the bottom-sheet look (gallery). */
  sheet?: 'auto' | 'never' | 'always';
}

export function ReminderDialog(props: ReminderDialogProps) {
  if (!props.open) return null;
  return <ReminderDialogImpl {...props} />;
}

function ReminderDialogImpl({
  onClose,
  onSubmit,
  onRemove,
  initial,
  now,
  weekStartsOn,
  marks,
  defaultTime,
  title,
  context,
  container,
  inline,
  sheet,
}: ReminderDialogProps) {
  const t = useT();
  const state = useReminderDraft(initial);
  const [tried, setTried] = useState(false);
  // Shortcut submits wait one render so state updates from the same key event are included.
  const [submitRequest, setSubmitRequest] = useState(0);
  const submit = () => {
    const r = reminderResult(state);
    if (!r.ok) {
      setTried(true);
      return;
    }
    onSubmit(r.value);
  };
  useEffect(() => {
    if (submitRequest > 0) submit();
    // Only when a shortcut submit was requested.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [submitRequest]);
  return (
    <Modal
      open
      onClose={onClose}
      size="lg"
      title={title ?? t(initial?.at ? 'components.reminder.editTitle' : 'components.reminder.title')}
      container={container}
      inline={inline}
      sheet={sheet}
      bodyClassName="b-reminder-body"
      footer={
        <>
          {onRemove && (
            <Button variant="ghost" className="b-reminder__remove" icon="trash" onClick={onRemove}>
              {t('components.reminder.remove')}
            </Button>
          )}
          <Button variant="ghost" onClick={onClose}>
            {t('components.confirm.cancel')}
          </Button>
          <Button variant="primary" icon="bell" onClick={submit}>
            {t('components.reminder.submit')}
          </Button>
        </>
      }
    >
      <ReminderFields
        state={state}
        now={now}
        weekStartsOn={weekStartsOn}
        marks={marks}
        defaultTime={defaultTime}
        context={context}
        showErrors={tried}
        onSubmit={() => setSubmitRequest((n) => n + 1)}
      />
    </Modal>
  );
}
