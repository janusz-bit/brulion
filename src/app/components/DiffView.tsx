/** Read-only comparison of two texts (history, sync conflicts): split or unified. */
import { Fragment, useMemo, useState, type ReactNode } from 'react';
import {
  collapseUnchanged,
  diffLines,
  diffWords,
  toSplitRows,
  type Collapsed,
  type DiffLine,
  type Segment,
  type SplitRow,
} from './diff';
import { EmptyState } from './EmptyState';
import { cx, NARROW_QUERY, useMediaQuery } from './hooks';
import { Icon } from './Icon';
import { useT } from './i18n';
import { SegmentedControl } from './Tabs';

export type DiffMode = 'split' | 'unified';

export interface DiffViewProps {
  left: string;
  right: string;
  leftLabel?: ReactNode;
  rightLabel?: ReactNode;
  mode?: DiffMode;
  /** Shows a split/unified switch in the header. */
  onModeChange?: (mode: DiffMode) => void;
  /** Unchanged lines kept around each change (default 3; Infinity = show all). */
  context?: number;
  /** Wrap long lines (default) or scroll horizontally. */
  wrap?: boolean;
  /** Use the unified layout on phone widths even in split mode (default true). */
  autoUnified?: boolean;
  maxEdits?: number;
  className?: string;
}

/** Rendered rows (items + gaps) before a "show more" button; raised by this much per click. */
export const DIFF_RENDER_STEP = 2000;
/** Word highlighting is skipped past this many changed line pairs (huge rewrites). */
const MAX_WORD_PAIRS = 2000;

const EMPTY_SET: ReadonlySet<number> = new Set();

/** Lines behind the "show more" button (a gap counts its hidden lines). */
function hiddenRows(items: readonly Collapsed<unknown>[], shown: number): number {
  let n = 0;
  for (let i = shown; i < items.length; i++) {
    const it = items[i]!;
    n += it.type === 'gap' ? it.count : 1;
  }
  return n;
}

/** Word-level segments for paired deleted/added lines. */
function wordSegments(rows: readonly SplitRow[], truncated: boolean): Map<DiffLine, Segment[]> {
  const map = new Map<DiffLine, Segment[]>();
  // A simplified (truncated) diff pairs unrelated lines: highlighting them is wasted work.
  if (truncated) return map;
  let pairs = 0;
  for (const r of rows) {
    if (r.left?.kind === 'del' && r.right?.kind === 'add') {
      if (++pairs > MAX_WORD_PAIRS) break;
      const w = diffWords(r.left.text, r.right.text);
      if (w) {
        map.set(r.left, w.left);
        map.set(r.right, w.right);
      }
    }
  }
  return map;
}

function LineText({ line, words }: { line: DiffLine; words: Map<DiffLine, Segment[]> }) {
  const segs = words.get(line);
  if (!segs) return <>{line.text === '' ? '\u200b' : line.text}</>;
  return (
    <>
      {segs.map((s, i) =>
        s.changed ? (
          <span key={i} className="b-diff__word">
            {s.text}
          </span>
        ) : (
          <Fragment key={i}>{s.text}</Fragment>
        ),
      )}
    </>
  );
}

export function DiffView({
  left,
  right,
  leftLabel,
  rightLabel,
  mode = 'split',
  onModeChange,
  context = 3,
  wrap = true,
  autoUnified = true,
  maxEdits,
  className,
}: DiffViewProps) {
  const t = useT();
  const narrow = useMediaQuery(NARROW_QUERY);
  const effective: DiffMode = autoUnified && narrow ? 'unified' : mode;
  const result = useMemo(() => diffLines(left, right, { maxEdits }), [left, right, maxEdits]);
  const rows = useMemo(() => toSplitRows(result.lines), [result]);
  const words = useMemo(() => wordSegments(rows, result.truncated), [rows, result.truncated]);
  const ctx = Number.isFinite(context) ? Math.max(0, context) : Number.MAX_SAFE_INTEGER;

  // Expanded gaps and the render limit belong to one diff in one layout (gap starts index
  // split rows or unified lines): a new text or a mode switch starts folded again.
  const [view, setView] = useState<{ result: unknown; mode: DiffMode; expanded: ReadonlySet<number>; limit: number }>(
    () => ({ result, mode: effective, expanded: new Set(), limit: DIFF_RENDER_STEP }),
  );
  const current = view.result === result && view.mode === effective;
  const expanded = current ? view.expanded : EMPTY_SET;
  const limit = current ? view.limit : DIFF_RENDER_STEP;
  const update = (patch: { expand?: number; more?: boolean }) =>
    setView((v) => {
      const same = v.result === result && v.mode === effective;
      const next = new Set(same ? v.expanded : EMPTY_SET);
      if (patch.expand !== undefined) next.add(patch.expand);
      const base = same ? v.limit : DIFF_RENDER_STEP;
      return { result, mode: effective, expanded: next, limit: patch.more ? base + DIFF_RENDER_STEP : base };
    });
  const expand = (start: number) => update({ expand: start });
  const unchanged = result.added === 0 && result.removed === 0;

  const header = (
    <div className="b-diff__header">
      <div className="b-diff__labels">
        {(leftLabel || rightLabel) && (
          <>
            <span className="b-diff__label b-diff__label--left">
              <span className="b-diff__swatch b-diff__swatch--del" aria-hidden="true" />
              {leftLabel}
            </span>
            <Icon name="arrow-right" size={14} className="b-diff__arrow" />
            <span className="b-diff__label b-diff__label--right">
              <span className="b-diff__swatch b-diff__swatch--add" aria-hidden="true" />
              {rightLabel}
            </span>
          </>
        )}
      </div>
      <div className="b-diff__tools">
        <span className="b-diff__stats">
          <span className="b-diff__stat--add" aria-hidden="true">
            +{result.added}
          </span>{' '}
          <span className="b-diff__stat--del" aria-hidden="true">
            −{result.removed}
          </span>
          <span className="b-sr-only">
            {result.added} {t('components.diff.added')}, {result.removed} {t('components.diff.removed')}
          </span>
        </span>
        {onModeChange && !narrow && (
          <SegmentedControl
            size="sm"
            value={mode}
            onChange={onModeChange}
            options={[
              { value: 'split', icon: 'columns', ariaLabel: t('components.diff.split'), title: t('components.diff.split') },
              { value: 'unified', icon: 'rows', ariaLabel: t('components.diff.unified'), title: t('components.diff.unified') },
            ]}
          />
        )}
      </div>
    </div>
  );

  if (unchanged) {
    return (
      <div className={cx('b-diff', className)}>
        {header}
        <EmptyState size="sm" icon="check-circle" title={t('components.diff.noChanges')} body={t('components.diff.noChangesBody')} />
      </div>
    );
  }

  const gapRow = (count: number, start: number, cols: number) => (
    <tr key={`gap-${start}`} className="b-diff__gap">
      <td colSpan={cols}>
        <button type="button" className="b-diff__expand" onClick={() => expand(start)}>
          <Icon name="chevron-down" size={14} />
          {t('components.diff.expand')} · {t('components.diff.unchanged', { count })}
        </button>
      </td>
    </tr>
  );

  const moreRow = (hidden: number, cols: number) => (
    <tr key="more" className="b-diff__gap">
      <td colSpan={cols}>
        <button type="button" className="b-diff__expand" onClick={() => update({ more: true })}>
          <Icon name="chevron-down" size={14} />
          {t('components.diff.more', { count: hidden })}
        </button>
      </td>
    </tr>
  );

  const marker = (kind: DiffLine['kind']) =>
    kind === 'equal' ? null : (
      <span className="b-sr-only">{kind === 'add' ? t('components.diff.added') : t('components.diff.removed')}: </span>
    );

  let body: ReactNode;
  if (effective === 'split') {
    const all = collapseUnchanged(rows, (r) => r.left?.kind === 'equal', ctx, expanded);
    const items = all.slice(0, limit);
    body = (
      <table className="b-diff__table b-diff__table--split">
        <colgroup>
          <col className="b-diff__col-no" />
          <col />
          <col className="b-diff__col-no" />
          <col />
        </colgroup>
        <tbody>
          {items.map((it) => {
            if (it.type === 'gap') return gapRow(it.count, it.start, 4);
            const { left: l, right: r } = it.item;
            return (
              <tr key={it.index} className="b-diff__row">
                <td className={cx('b-diff__no', l && `is-${l.kind}`, !l && 'is-empty')}>{l?.oldNo}</td>
                <td className={cx('b-diff__text', l ? `is-${l.kind}` : 'is-empty')}>
                  {l && (
                    <>
                      {marker(l.kind)}
                      <LineText line={l} words={words} />
                    </>
                  )}
                </td>
                <td className={cx('b-diff__no', r && `is-${r.kind}`, !r && 'is-empty')}>{r?.newNo}</td>
                <td className={cx('b-diff__text', r ? `is-${r.kind}` : 'is-empty')}>
                  {r && (
                    <>
                      {r.kind !== 'equal' && marker(r.kind)}
                      <LineText line={r} words={words} />
                    </>
                  )}
                </td>
              </tr>
            );
          })}
          {all.length > items.length && moreRow(hiddenRows(all, items.length), 4)}
        </tbody>
      </table>
    );
  } else {
    const all = collapseUnchanged(result.lines, (l) => l.kind === 'equal', ctx, expanded);
    const items = all.slice(0, limit);
    body = (
      <table className="b-diff__table b-diff__table--unified">
        <colgroup>
          <col className="b-diff__col-no" />
          <col className="b-diff__col-no" />
          <col className="b-diff__col-sign" />
          <col />
        </colgroup>
        <tbody>
          {items.map((it) => {
            if (it.type === 'gap') return gapRow(it.count, it.start, 4);
            const l = it.item;
            return (
              <tr key={it.index} className={cx('b-diff__row', `is-${l.kind}`)}>
                <td className={cx('b-diff__no', `is-${l.kind}`)}>{l.oldNo}</td>
                <td className={cx('b-diff__no', `is-${l.kind}`)}>{l.newNo}</td>
                <td className={cx('b-diff__sign', `is-${l.kind}`)} aria-hidden="true">
                  {l.kind === 'add' ? '+' : l.kind === 'del' ? '−' : ''}
                </td>
                <td className={cx('b-diff__text', `is-${l.kind}`)}>
                  {marker(l.kind)}
                  <LineText line={l} words={words} />
                </td>
              </tr>
            );
          })}
          {all.length > items.length && moreRow(hiddenRows(all, items.length), 4)}
        </tbody>
      </table>
    );
  }

  return (
    <div className={cx('b-diff', !wrap && 'b-diff--nowrap', className)}>
      {header}
      {result.truncated && (
        <div className="b-diff__notice" role="note">
          <Icon name="info" size={14} />
          {t('components.diff.truncated')}
        </div>
      )}
      <div className="b-diff__scroll">{body}</div>
    </div>
  );
}
