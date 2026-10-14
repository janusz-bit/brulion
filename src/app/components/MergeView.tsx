/**
 * Manual merge of a sync conflict: per conflict hunk the user picks "moja wersja" / "ich
 * wersja" / "obie" or edits by hand, then saves the merged text.
 */
import { useRef, useState, type ReactNode } from 'react';
import { Button, IconButton } from './Button';
import { TextArea } from './forms';
import { cx } from './hooks';
import { Icon } from './Icon';
import { useT } from './i18n';
import {
  buildMerged,
  conflictIndexes,
  manualSeed,
  resolvedLines,
  type MergeHunk,
  type MergePick,
  type Resolution,
  type Resolutions,
} from './merge';
import { ProgressBar } from './Spinner';

export interface MergeViewProps {
  hunks: readonly MergeHunk[];
  onResolve: (text: string) => void;
  onCancel?: () => void;
  localLabel?: ReactNode;
  remoteLabel?: ReactNode;
  /** Line ending for the result (default "\n"). */
  eol?: string;
  /** End the result with a newline (default true). */
  finalNewline?: boolean;
  /** Saving in progress. */
  busy?: boolean;
  /** Unchanged lines shown around conflicts before folding (default 2). */
  context?: number;
  className?: string;
}

function Lines({ lines, emptyLabel }: { lines: readonly string[]; emptyLabel?: string }) {
  if (lines.length === 0) return <div className="b-merge__lines is-empty">{emptyLabel}</div>;
  return (
    <pre className="b-merge__lines">
      {lines.map((l, i) => (
        <div key={i} className="b-merge__line">
          {l === '' ? '\u200b' : l}
        </div>
      ))}
    </pre>
  );
}

function OkHunk({ lines, context, first, last }: { lines: readonly string[]; context: number; first: boolean; last: boolean }) {
  const t = useT();
  const [open, setOpen] = useState(false);
  const before = first ? 0 : context;
  const after = last ? 0 : context;
  const hidden = lines.length - before - after;
  if (open || hidden <= 1) return <Lines lines={lines} />;
  return (
    <div className="b-merge__ok">
      {before > 0 && <Lines lines={lines.slice(0, before)} />}
      <button type="button" className="b-merge__fold" onClick={() => setOpen(true)}>
        <Icon name="chevron-down" size={14} />
        {t('components.merge.unchanged', { count: hidden })}
      </button>
      {after > 0 && <Lines lines={lines.slice(lines.length - after)} />}
    </div>
  );
}

const NO_RESOLUTIONS: Resolutions = {};

const PICKS: readonly { pick: MergePick; key: string }[] = [
  { pick: 'local', key: 'components.merge.mine' },
  { pick: 'remote', key: 'components.merge.theirs' },
  { pick: 'both', key: 'components.merge.both' },
  { pick: 'manual', key: 'components.merge.edit' },
];

export function MergeView({
  hunks,
  onResolve,
  onCancel,
  localLabel,
  remoteLabel,
  eol = '\n',
  finalNewline = true,
  busy = false,
  context = 2,
  className,
}: MergeViewProps) {
  const t = useT();
  // Resolutions are keyed by hunk index, so they only apply to the hunks they were made for:
  // new hunks (the file changed again while the view was open) start unresolved.
  const [state, setState] = useState<{ hunks: readonly MergeHunk[]; resolutions: Resolutions }>(() => ({
    hunks,
    resolutions: {},
  }));
  const resolutions: Resolutions = state.hunks === hunks ? state.resolutions : NO_RESOLUTIONS;
  const conflicts = conflictIndexes(hunks);
  const total = conflicts.length;
  const remaining = conflicts.filter((i) => !resolutions[i]).length;
  const merged = buildMerged(hunks, resolutions, { eol, finalNewline });
  const cardRefs = useRef(new Map<number, HTMLElement>());

  const setRes = (i: number, r: Resolution | undefined) =>
    setState((prev) => ({ hunks, resolutions: { ...(prev.hunks === hunks ? prev.resolutions : NO_RESOLUTIONS), [i]: r } }));

  const choose = (i: number, pick: MergePick) => {
    const h = hunks[i];
    if (h?.kind !== 'conflict') return;
    if (pick === 'manual') setRes(i, { pick: 'manual', text: manualSeed(h, resolutions[i], eol) });
    else setRes(i, { pick });
    // Bring the next unresolved conflict into view.
    if (pick !== 'manual') {
      const next = conflicts.find((c) => c > i && !resolutions[c]);
      if (next !== undefined) cardRefs.current.get(next)?.scrollIntoView?.({ block: 'nearest', behavior: 'smooth' });
    }
  };

  const local = localLabel ?? t('components.merge.localLabel');
  const remote = remoteLabel ?? t('components.merge.remoteLabel');

  return (
    <div className={cx('b-merge', className)}>
      <div className="b-merge__status" aria-live="polite">
        {total === 0 ? (
          <span>{t('components.merge.noConflicts')}</span>
        ) : remaining === 0 ? (
          <span className="b-merge__done">
            <Icon name="check-circle" size={16} />
            {t('components.merge.allResolved')}
          </span>
        ) : (
          <span>{t('components.merge.remaining', { count: remaining })}</span>
        )}
        {total > 0 && <ProgressBar value={total - remaining} max={total} size="sm" tone="success" label={t('components.merge.result')} />}
      </div>

      <div className="b-merge__hunks">
        {hunks.map((h, i) => {
          if (h.kind === 'ok')
            return <OkHunk key={i} lines={h.lines} context={context} first={i === 0} last={i === hunks.length - 1} />;
          const n = conflicts.indexOf(i) + 1;
          const res = resolutions[i];
          return (
            <section
              key={i}
              ref={(el) => {
                if (el) cardRefs.current.set(i, el);
                else cardRefs.current.delete(i);
              }}
              className={cx('b-merge__conflict', res && 'is-resolved')}
              aria-label={t('components.merge.conflict', { n, total })}
            >
              <header className="b-merge__conflict-head">
                <span className="b-merge__conflict-title">
                  <Icon name={res ? 'check-circle' : 'merge'} size={16} />
                  {t('components.merge.conflict', { n, total })}
                </span>
                {res && (
                  <IconButton icon="undo" size="sm" label={t('components.merge.undo')} onClick={() => setRes(i, undefined)} />
                )}
              </header>

              {!res && (
                <div className="b-merge__sides">
                  <div className="b-merge__side b-merge__side--local">
                    <div className="b-merge__side-label">{local}</div>
                    <Lines lines={h.local} emptyLabel={t('components.merge.emptySide')} />
                  </div>
                  <div className="b-merge__side b-merge__side--remote">
                    <div className="b-merge__side-label">{remote}</div>
                    <Lines lines={h.remote} emptyLabel={t('components.merge.emptySide')} />
                  </div>
                </div>
              )}

              {!res && h.base.length > 0 && (
                <details className="b-merge__base">
                  <summary>{t('components.merge.base')}</summary>
                  <Lines lines={h.base} />
                </details>
              )}

              <div className="b-merge__picks" role="group" aria-label={t('components.merge.conflict', { n, total })}>
                {PICKS.map((p) => (
                  <Button
                    key={p.pick}
                    size="sm"
                    variant={res?.pick === p.pick ? 'primary' : 'secondary'}
                    aria-pressed={res?.pick === p.pick}
                    icon={p.pick === 'manual' ? 'pencil' : undefined}
                    onClick={() => choose(i, p.pick)}
                  >
                    {t(p.key)}
                  </Button>
                ))}
              </div>

              {res?.pick === 'manual' ? (
                <TextArea
                  mono
                  autoGrow
                  rows={Math.min(12, Math.max(3, h.local.length + h.remote.length))}
                  label={t('components.merge.manualLabel')}
                  value={res.text}
                  onValueChange={(text) => setRes(i, { pick: 'manual', text })}
                  spellCheck={false}
                />
              ) : (
                res && (
                  <div className="b-merge__result">
                    <div className="b-merge__side-label">{t('components.merge.result')}</div>
                    <Lines lines={resolvedLines(h, res)} emptyLabel={t('components.merge.emptySide')} />
                  </div>
                )
              )}
            </section>
          );
        })}
      </div>

      <div className="b-merge__footer">
        {onCancel && (
          <Button variant="ghost" onClick={onCancel} disabled={busy}>
            {t('components.confirm.cancel')}
          </Button>
        )}
        <Button
          variant="primary"
          icon="check"
          disabled={merged === null}
          loading={busy}
          onClick={() => {
            if (merged !== null) onResolve(merged);
          }}
        >
          {t('components.merge.save')}
        </Button>
      </div>
    </div>
  );
}
