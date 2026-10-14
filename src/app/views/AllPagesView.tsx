// PLACEHOLDER — replaced by the views agent
/**
 * All pages list.
 * Contract: named export `AllPagesView` (= file name), props `ViewProps<'pages'>` from
 * '@/app/state/store' ({ route, pane }); App.tsx lazy-loads this module.
 */
import { EmptyState } from '@/app/components';
import { useT } from '@/app/state/hooks';
import type { ViewProps } from '@/app/state/store';

export function AllPagesView(_props: ViewProps<'pages'>) {
  const t = useT();
  return <EmptyState icon="file" title={t('shell.nav.pages')} body={t('shell.placeholder')} />;
}
