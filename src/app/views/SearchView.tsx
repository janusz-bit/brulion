// PLACEHOLDER — replaced by the views agent
/**
 * Full-text search (route.q).
 * Contract: named export `SearchView` (= file name), props `ViewProps<'search'>` from
 * '@/app/state/store' ({ route, pane }); App.tsx lazy-loads this module.
 */
import { EmptyState } from '@/app/components';
import { useT } from '@/app/state/hooks';
import type { ViewProps } from '@/app/state/store';

export function SearchView(_props: ViewProps<'search'>) {
  const t = useT();
  return <EmptyState icon="search" title={t('shell.nav.search')} body={t('shell.placeholder')} />;
}
