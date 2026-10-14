// PLACEHOLDER — replaced by the views agent
/**
 * Link graph (optionally centred on route.path).
 * Contract: named export `GraphView` (= file name), props `ViewProps<'graph'>` from
 * '@/app/state/store' ({ route, pane }); App.tsx lazy-loads this module.
 */
import { EmptyState } from '@/app/components';
import { useT } from '@/app/state/hooks';
import type { ViewProps } from '@/app/state/store';

export function GraphView(_props: ViewProps<'graph'>) {
  const t = useT();
  return <EmptyState icon="sparkles" title={t('shell.nav.graph')} body={t('shell.placeholder')} />;
}
