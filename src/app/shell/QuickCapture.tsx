/**
 * Quick capture: one line into today's journal. A date phrase becomes a reminder or a due
 * date ("Kupić mleko jutro o 18" → "- [ ] Kupić mleko ⏰ 2026-10-15 18:00"); a preview shows
 * what was understood before saving.
 */
import { useMemo, useRef, useState } from 'react';
import { Badge, Button, Modal, Switch, TextArea, useNow } from '@/app/components';
import { useAppStore, useLocale, useT, useWorkspace } from '@/app/state/hooks';
import { formatDateHuman, parseNaturalDate, stripNaturalDate } from '@/core/dates';

export function QuickCapture() {
  const open = useAppStore((s) => s.captureOpen);
  const ws = useAppStore((s) => s.workspace);
  if (!open || !ws) return null;
  return <CaptureDialog />;
}

function CaptureDialog() {
  const t = useT();
  const locale = useLocale();
  const ws = useWorkspace();
  const setOpen = useAppStore((s) => s.setCaptureOpen);
  const toast = useAppStore((s) => s.toast);
  const navigate = useAppStore((s) => s.navigate);
  const [text, setText] = useState('');
  const [task, setTask] = useState(true);
  const [busy, setBusy] = useState(false);
  const ref = useRef<HTMLTextAreaElement | null>(null);
  const now = useNow(undefined, 60_000);

  const parsed = useMemo(() => {
    const first = text.split('\n')[0]?.trim() ?? '';
    if (!first) return null;
    const r = parseNaturalDate(first, now, { locale, weekStartsOn: ws.config.weekStartsOn });
    if (!r || !stripNaturalDate(first, r)) return null;
    return r;
  }, [text, now, locale, ws]);

  const submit = async () => {
    if (!text.trim() || busy) return;
    setBusy(true);
    try {
      const at = await ws.quickCapture(text, { task });
      setOpen(false);
      toast({
        kind: 'success',
        message: t('shell.capture.saved'),
        action: { label: t('shell.capture.open'), onClick: () => navigate({ view: 'page', path: at.path, line: at.line }) },
      });
    } catch (e) {
      toast({ kind: 'error', message: t('shell.capture.failed', { error: e instanceof Error ? e.message : String(e) }) });
      setBusy(false);
    }
  };

  return (
    <Modal
      open
      onClose={() => setOpen(false)}
      title={t('shell.capture.title')}
      description={t('shell.capture.description')}
      initialFocus={ref}
      className="b-capture"
      footer={
        <>
          <Button onClick={() => setOpen(false)}>{t('common.cancel')}</Button>
          <Button variant="primary" loading={busy} disabled={!text.trim()} onClick={() => void submit()}>
            {t('shell.capture.add')}
          </Button>
        </>
      }
    >
      <form
        className="b-capture__form"
        onSubmit={(e) => {
          e.preventDefault();
          void submit();
        }}
      >
        <TextArea
          ref={ref}
          label={t('shell.capture.label')}
          placeholder={t('shell.capture.placeholder')}
          value={text}
          autoGrow
          rows={2}
          maxRows={8}
          enterKeyHint="done"
          onValueChange={setText}
          onKeyDown={(e) => {
            if (e.key === 'Enter' && !e.shiftKey && !e.nativeEvent.isComposing) {
              e.preventDefault();
              void submit();
            }
          }}
        />
        <div className="b-capture__meta">
          {parsed ? (
            <Badge variant={parsed.time ? 'reminder' : 'due'}>
              {parsed.time
                ? t('shell.capture.reminder', { when: formatDateHuman(`${parsed.date}T${parsed.time}`, locale, now) })
                : t('shell.capture.due', { when: formatDateHuman(parsed.date, locale, now) })}
            </Badge>
          ) : (
            <span className="b-capture__hint">{t('shell.capture.hint')}</span>
          )}
          <Switch checked={task} onCheckedChange={setTask} label={t('shell.capture.task')} size="sm" labelPosition="end" />
        </div>
      </form>
    </Modal>
  );
}
