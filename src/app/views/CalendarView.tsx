// PLACEHOLDER — replaced by the views agent
/**
 * Calendar: month / week / agenda with journals, due dates and reminders.
 * Contract: named export `CalendarView` (= file name), props `ViewProps<'calendar'>` from
 * '@/app/state/store' ({ route, pane }); App.tsx lazy-loads this module.
 */
import { EmptyState } from '@/app/components';
import { useT } from '@/app/state/hooks';
import type { ViewProps } from '@/app/state/store';

export function CalendarView(_props: ViewProps<'calendar'>) {
  const t = useT();
  return <EmptyState icon="calendar" title={t('shell.nav.calendar')} body={t('shell.placeholder')} />;
}
