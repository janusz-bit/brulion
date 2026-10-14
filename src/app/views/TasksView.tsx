// PLACEHOLDER — replaced by the views agent
/**
 * Tasks across the vault (filters, grouping).
 * Contract: named export `TasksView` (= file name), props `ViewProps<'tasks'>` from
 * '@/app/state/store' ({ route, pane }); App.tsx lazy-loads this module.
 */
import { EmptyState } from '@/app/components';
import { useT } from '@/app/state/hooks';
import type { ViewProps } from '@/app/state/store';

export function TasksView(_props: ViewProps<'tasks'>) {
  const t = useT();
  return <EmptyState icon="tasks" title={t('shell.nav.tasks')} body={t('shell.placeholder')} />;
}
