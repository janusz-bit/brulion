/**
 * React host for dialogs opened by editor extensions (slash commands, mobile toolbar): date
 * picker, reminder, recurrence and template pickers. NoteEditor owns one `DialogController`
 * per editor, installs `controller.open` as the `editorDialogs` facet and renders
 * `<EditorDialogs>`; after a dialog closes, focus goes back to the editor.
 */
import { useMemo, useState, useSyncExternalStore, type ReactNode } from 'react';
import { canonicalRecurrence, formatRecurrence, parseRecurrence } from '@/core/dates';
import { pageTitle } from '@/core/paths';
import type { LocalDate, LocalDateTime, VaultPath } from '@/core/types';
import {
  Button,
  Chip,
  DatePicker,
  EmptyState,
  Modal,
  NARROW_QUERY,
  Popover,
  ReminderDialog,
  Switch,
  TextInput,
  useLocale,
  useMediaQuery,
  useT,
  type VirtualAnchor,
} from '@/app/components';
import type { EditorDialogRequest } from './context';

type Listener = () => void;

/** Holds the open dialog request (one at a time). */
export class DialogController {
  private request: EditorDialogRequest | null = null;
  private readonly listeners = new Set<Listener>();
  /** Where to anchor popovers (the caret); set by the editor when opening. */
  anchor: VirtualAnchor | null = null;
  /** Called after a dialog closed (submit or cancel). */
  onClosed: (() => void) | null = null;

  readonly open = (request: EditorDialogRequest): void => {
    const replaced = this.request;
    this.request = request;
    this.emit();
    if (replaced && replaced !== request) replaced.onCancel?.();
  };

  /** Close the open dialog; `submitted` = its result was taken (else `onCancel` runs). */
  close(submitted = false): void {
    const request = this.request;
    if (!request) return;
    this.request = null;
    this.emit();
    if (!submitted) request.onCancel?.();
    this.onClosed?.();
  }

  current(): EditorDialogRequest | null {
    return this.request;
  }

  subscribe = (cb: Listener): (() => void) => {
    this.listeners.add(cb);
    return () => this.listeners.delete(cb);
  };

  private emit(): void {
    for (const l of this.listeners) l();
  }
}

export interface EditorDialogsProps {
  controller: DialogController;
  now: Date;
  weekStartsOn?: 0 | 1;
  defaultTime?: string;
  templatesFolder?: string;
}

export function EditorDialogs({ controller, now, weekStartsOn = 1, defaultTime, templatesFolder }: EditorDialogsProps) {
  const request = useSyncExternalStore(controller.subscribe, () => controller.current(), () => null);
  if (!request) return null;
  const close = () => controller.close();
  const submit = <T,>(fn: (v: T) => void) => (v: T) => {
    controller.close(true);
    fn(v);
  };
  switch (request.kind) {
    case 'date':
      return (
        <DateDialog
          key="date"
          title={request.title}
          withTime={request.withTime ?? false}
          initial={request.initial}
          now={now}
          weekStartsOn={weekStartsOn}
          defaultTime={defaultTime}
          anchor={controller.anchor}
          onClose={close}
          onSubmit={submit(request.onSubmit)}
        />
      );
    case 'reminder':
      return (
        <ReminderDialog
          open
          onClose={close}
          onSubmit={submit(request.onSubmit)}
          initial={request.initial}
          context={request.context}
          now={now}
          weekStartsOn={weekStartsOn}
          defaultTime={defaultTime}
        />
      );
    case 'recurrence':
      return <RecurrenceDialog initial={request.initial} onClose={close} onSubmit={submit(request.onSubmit)} />;
    case 'template':
      return (
        <TemplateDialog
          templates={request.templates}
          folder={templatesFolder ?? 'templates'}
          onClose={close}
          onSubmit={submit(request.onSubmit)}
        />
      );
  }
}

// ---------------------------------------------------------------------------------------------
// Date: popover at the caret on wide screens, bottom sheet on phones
// ---------------------------------------------------------------------------------------------

function DateDialog({
  title,
  withTime,
  initial,
  now,
  weekStartsOn,
  defaultTime,
  anchor,
  onClose,
  onSubmit,
}: {
  title: string;
  withTime: boolean;
  initial?: LocalDate | LocalDateTime;
  now: Date;
  weekStartsOn: 0 | 1;
  defaultTime?: string;
  anchor: VirtualAnchor | null;
  onClose: () => void;
  onSubmit: (v: LocalDate | LocalDateTime) => void;
}) {
  const t = useT();
  const narrow = useMediaQuery(NARROW_QUERY);
  const [value, setValue] = useState<LocalDate | LocalDateTime | null>(initial ?? null);
  const picker = (
    <DatePicker
      value={value}
      onChange={setValue}
      onCommit={onSubmit}
      withTime={withTime}
      defaultTime={defaultTime}
      weekStartsOn={weekStartsOn}
      now={now}
      autoFocus="input"
      aria-label={title}
    />
  );
  const actions = (
    <>
      <Button variant="ghost" onClick={onClose}>
        {t('editor.dialog.cancel')}
      </Button>
      <Button variant="primary" disabled={!value} onClick={() => value && onSubmit(value)}>
        {t('editor.dialog.insert')}
      </Button>
    </>
  );
  if (narrow || !anchor) {
    return (
      <Modal open onClose={onClose} title={title} size="sm" footer={actions}>
        {picker}
      </Modal>
    );
  }
  return (
    <Popover open onClose={onClose} anchor={anchor} placement="bottom-start" aria-label={title} className="b-editor-datepop">
      <div className="b-editor-datepop__title">{title}</div>
      {picker}
      {withTime && <div className="b-editor-datepop__actions">{actions}</div>}
    </Popover>
  );
}

// ---------------------------------------------------------------------------------------------
// Recurrence picker
// ---------------------------------------------------------------------------------------------

const PRESETS = ['every day', 'every weekday', 'every week', 'every 2 weeks', 'every month', 'every year'] as const;

function RecurrenceDialog({
  initial,
  onClose,
  onSubmit,
}: {
  initial?: string;
  onClose: () => void;
  onSubmit: (v: { rule: string }) => void;
}) {
  const t = useT();
  const locale = useLocale();
  const [text, setText] = useState(initial ?? '');
  const [whenDone, setWhenDone] = useState(/when done|po wykonaniu/i.test(initial ?? ''));
  const canonical = useMemo(() => {
    const base = text.replace(/\s*(when done|po wykonaniu)\s*$/i, '').trim();
    if (!base) return null;
    const rule = parseRecurrence(base);
    if (!rule) return null;
    return canonicalRecurrence(formatRecurrence({ ...rule, whenDone }, 'en'));
  }, [text, whenDone]);
  const submit = () => canonical && onSubmit({ rule: canonical });
  return (
    <Modal
      open
      onClose={onClose}
      title={t('editor.dialog.recurrence')}
      size="sm"
      footer={
        <>
          <Button variant="ghost" onClick={onClose}>
            {t('editor.dialog.cancel')}
          </Button>
          <Button variant="primary" disabled={!canonical} onClick={submit}>
            {t('editor.dialog.insert')}
          </Button>
        </>
      }
    >
      <div className="b-editor-recur">
        <div className="b-editor-recur__presets" role="group" aria-label={t('editor.dialog.recurrence')}>
          {PRESETS.map((p) => {
            const rule = parseRecurrence(p)!;
            return (
              <Chip key={p} selected={canonical === canonicalRecurrence(formatRecurrence({ ...rule, whenDone }, 'en'))} onClick={() => setText(p)}>
                {formatRecurrence(rule, locale)}
              </Chip>
            );
          })}
        </div>
        <TextInput
          label={t('editor.dialog.recurrence.custom')}
          value={text}
          placeholder={t('editor.dialog.recurrence.placeholder')}
          onValueChange={setText}
          onKeyDown={(e) => {
            if (e.key === 'Enter') {
              e.preventDefault();
              submit();
            }
          }}
          data-autofocus
          hint={canonical ? t('editor.dialog.recurrence.preview', { rule: canonical }) : undefined}
          error={text.trim() !== '' && !canonical ? t('editor.dialog.recurrence.invalid') : undefined}
        />
        <Switch checked={whenDone} onCheckedChange={setWhenDone} label={t('editor.dialog.recurrence.whenDone')} />
      </div>
    </Modal>
  );
}

// ---------------------------------------------------------------------------------------------
// Template picker
// ---------------------------------------------------------------------------------------------

function TemplateDialog({
  templates,
  folder,
  onClose,
  onSubmit,
}: {
  templates: VaultPath[];
  folder: string;
  onClose: () => void;
  onSubmit: (path: VaultPath) => void;
}) {
  const t = useT();
  let body: ReactNode;
  if (templates.length === 0) body = <EmptyState icon="file" title={t('editor.dialog.template.empty', { folder })} />;
  else
    body = (
      <ul className="b-editor-templates" role="list">
        {templates.map((p, i) => (
          <li key={p}>
            <button type="button" className="b-menu__item" onClick={() => onSubmit(p)} data-autofocus={i === 0 ? true : undefined}>
              <span className="b-editor-templates__name">{pageTitle(p)}</span>
              <span className="b-editor-templates__path">{p}</span>
            </button>
          </li>
        ))}
      </ul>
    );
  return (
    <Modal open onClose={onClose} title={t('editor.dialog.template')} size="sm">
      {body}
    </Modal>
  );
}
