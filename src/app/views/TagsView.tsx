// PLACEHOLDER — replaced by the views agent
/**
 * Tags and tagged blocks (route.tag).
 * Contract: named export `TagsView` (= file name), props `ViewProps<'tags'>` from
 * '@/app/state/store' ({ route, pane }); App.tsx lazy-loads this module.
 */
import { EmptyState } from '@/app/components';
import { useT } from '@/app/state/hooks';
import type { ViewProps } from '@/app/state/store';

export function TagsView(_props: ViewProps<'tags'>) {
  const t = useT();
  return <EmptyState icon="hash" title={t('shell.nav.tags')} body={t('shell.placeholder')} />;
}
