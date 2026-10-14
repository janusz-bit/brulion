// PLACEHOLDER — replaced by the views agent
/**
 * Trash: restore / delete permanently.
 * Contract: named export `TrashView` (= file name), props `ViewProps<'trash'>` from
 * '@/app/state/store' ({ route, pane }); App.tsx lazy-loads this module.
 */
import { EmptyState } from '@/app/components';
import { useT } from '@/app/state/hooks';
import type { ViewProps } from '@/app/state/store';

export function TrashView(_props: ViewProps<'trash'>) {
  const t = useT();
  return <EmptyState icon="trash" title={t('shell.nav.trash')} body={t('shell.placeholder')} />;
}
