// PLACEHOLDER — replaced by the views agent
/**
 * Sync center: Nextcloud settings/status, Syncthing status, conflicts + merge.
 * Contract: named export `SyncView` (= file name), props `ViewProps<'sync'>` from
 * '@/app/state/store' ({ route, pane }); App.tsx lazy-loads this module.
 */
import { EmptyState } from '@/app/components';
import { useT } from '@/app/state/hooks';
import type { ViewProps } from '@/app/state/store';

export function SyncView(_props: ViewProps<'sync'>) {
  const t = useT();
  return <EmptyState icon="repeat" title={t('shell.nav.sync')} body={t('shell.placeholder')} />;
}
