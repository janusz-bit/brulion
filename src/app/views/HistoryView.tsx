// PLACEHOLDER — replaced by the views agent
/**
 * Local version history of route.path.
 * Contract: named export `HistoryView` (= file name), props `ViewProps<'history'>` from
 * '@/app/state/store' ({ route, pane }); App.tsx lazy-loads this module.
 */
import { EmptyState } from '@/app/components';
import { useT } from '@/app/state/hooks';
import type { ViewProps } from '@/app/state/store';

export function HistoryView(_props: ViewProps<'history'>) {
  const t = useT();
  return <EmptyState icon="clock" title={t('shell.nav.history')} body={t('shell.placeholder')} />;
}
