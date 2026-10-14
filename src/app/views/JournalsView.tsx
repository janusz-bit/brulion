// PLACEHOLDER — replaced by the views agent
/**
 * Journals: today's journal followed by past days (compact editors in one scrolling stream).
 * Contract: named export `JournalsView` (= file name), props `ViewProps<'journals'>` from
 * '@/app/state/store' ({ route, pane }); App.tsx lazy-loads this module.
 */
import { useEffect, useState } from 'react';
import { EmptyState } from '@/app/components';
import { NoteEditor } from '@/app/editor';
import { useIndexVersion, useLocale, useNavigate, useT, useWorkspace } from '@/app/state/hooks';
import type { ViewProps } from '@/app/state/store';
import { formatDateHuman } from '@/core/dates';
import type { VaultPath } from '@/core/types';

export function JournalsView(_props: ViewProps<'journals'>) {
  const t = useT();
  const locale = useLocale();
  const ws = useWorkspace();
  const navigate = useNavigate();
  useIndexVersion();
  const today = ws.today();
  const [todayPath, setTodayPath] = useState<VaultPath | null>(null);
  useEffect(() => {
    let alive = true;
    void ws.openJournal(today).then((p) => alive && setTodayPath(p));
    return () => {
      alive = false;
    };
  }, [ws, today]);
  const past = ws.index
    .journals()
    .filter((j) => j.date < today)
    .slice(0, 5);
  return (
    <div className="b-placeholder-stream">
      {todayPath ? (
        <section className="b-placeholder-day">
          <h2>
            <button type="button" onClick={() => navigate({ view: 'page', path: todayPath })}>
              {formatDateHuman(today, locale)}
            </button>
          </h2>
          <NoteEditor path={todayPath} compact />
        </section>
      ) : (
        <EmptyState icon="book" title={t('shell.nav.journals')} body={t('shell.placeholder')} />
      )}
      {past.map((j) => (
        <section key={j.path} className="b-placeholder-day">
          <h2>
            <button type="button" onClick={() => navigate({ view: 'page', path: j.path })}>
              {formatDateHuman(j.date, locale)}
            </button>
          </h2>
          <NoteEditor path={j.path} compact />
        </section>
      ))}
    </div>
  );
}
