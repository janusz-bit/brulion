// PLACEHOLDER — replaced by the views agent
/**
 * Import and export.
 * Contract: named export `ImportExportView` (= file name), props `ViewProps<'import-export'>` from
 * '@/app/state/store' ({ route, pane }); App.tsx lazy-loads this module.
 */
import { EmptyState } from '@/app/components';
import { useT } from '@/app/state/hooks';
import type { ViewProps } from '@/app/state/store';

export function ImportExportView(_props: ViewProps<'import-export'>) {
  const t = useT();
  return <EmptyState icon="external" title={t('shell.nav.importExport')} body={t('shell.placeholder')} />;
}
