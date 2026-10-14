/**
 * Route → lazily loaded view component (src/app/views/<Name>View.tsx, named export = file
 * name; the settings view lives in the shell). Each view gets `ViewProps` ({ route, pane }).
 */
import { Component, lazy, Suspense, type ComponentType, type ErrorInfo, type LazyExoticComponent, type ReactNode } from 'react';
import { Button, EmptyState, Spinner } from '@/app/components';
import { useT } from '@/app/state/hooks';
import type { Route, ViewName, ViewProps } from '@/app/state/store';

// Views are typed per route; the registry stores them under the common props type.
type AnyView = ComponentType<ViewProps<ViewName>>;

function view<M>(load: () => Promise<M>, pick: (m: M) => unknown): LazyExoticComponent<AnyView> {
  return lazy(async () => ({ default: pick(await load()) as AnyView }));
}

export const VIEWS: Record<ViewName, LazyExoticComponent<AnyView>> = {
  journals: view(() => import('@/app/views/JournalsView'), (m) => m.JournalsView),
  page: view(() => import('@/app/views/PageView'), (m) => m.PageView),
  calendar: view(() => import('@/app/views/CalendarView'), (m) => m.CalendarView),
  tasks: view(() => import('@/app/views/TasksView'), (m) => m.TasksView),
  graph: view(() => import('@/app/views/GraphView'), (m) => m.GraphView),
  search: view(() => import('@/app/views/SearchView'), (m) => m.SearchView),
  pages: view(() => import('@/app/views/AllPagesView'), (m) => m.AllPagesView),
  tags: view(() => import('@/app/views/TagsView'), (m) => m.TagsView),
  sync: view(() => import('@/app/views/SyncView'), (m) => m.SyncView),
  'import-export': view(() => import('@/app/views/ImportExportView'), (m) => m.ImportExportView),
  history: view(() => import('@/app/views/HistoryView'), (m) => m.HistoryView),
  trash: view(() => import('@/app/views/TrashView'), (m) => m.TrashView),
  settings: view(() => import('./SettingsView'), (m) => m.SettingsView),
};

/** Start loading the code of the most used views while the app is idle. */
export function preloadViews(): void {
  const load = () => {
    void import('@/app/views/JournalsView');
    void import('@/app/views/PageView');
    void import('@/app/views/CalendarView');
    void import('@/app/views/TasksView');
    void import('@/app/views/SearchView');
    void import('@/app/views/panels');
  };
  const ric = (globalThis as { requestIdleCallback?: (cb: () => void) => void }).requestIdleCallback;
  if (ric) ric(load);
  else setTimeout(load, 300);
}

interface BoundaryProps {
  resetKey: string;
  fallback: (error: Error, reset: () => void) => ReactNode;
  children: ReactNode;
}

/** Catches errors of one view so the rest of the shell keeps working. */
export class ViewBoundary extends Component<BoundaryProps, { error: Error | null; key: string }> {
  override state = { error: null as Error | null, key: this.props.resetKey };

  static getDerivedStateFromError(error: Error): { error: Error } {
    return { error };
  }

  static getDerivedStateFromProps(props: BoundaryProps, state: { error: Error | null; key: string }) {
    return props.resetKey !== state.key ? { error: null, key: props.resetKey } : null;
  }

  override componentDidCatch(error: Error, info: ErrorInfo): void {
    console.warn('[shell] view crashed', error, info.componentStack);
  }

  override render(): ReactNode {
    if (this.state.error) return this.props.fallback(this.state.error, () => this.setState({ error: null }));
    return this.props.children;
  }
}

function ViewError({ error, reset }: { error: Error; reset: () => void }) {
  const t = useT();
  const chunk = /dynamically imported module|Importing a module script failed|Failed to fetch/i.test(error.message);
  return (
    <EmptyState
      icon="alert"
      title={t(chunk ? 'shell.view.updateNeeded' : 'shell.view.crashed')}
      body={chunk ? t('shell.view.updateBody') : error.message}
      action={
        chunk ? (
          <Button variant="primary" onClick={() => window.location.reload()}>
            {t('shell.update.reload')}
          </Button>
        ) : (
          <Button onClick={reset}>{t('shell.view.retry')}</Button>
        )
      }
    />
  );
}

export function ViewLoading() {
  const t = useT();
  return (
    <div className="b-view-loading">
      <Spinner size={22} label={t('common.loading')} />
    </div>
  );
}

/** Render the view for `route` (lazy, with a loading state and an error boundary). */
export function RouteView({ route, pane, resetKey }: { route: Route; pane: 'main' | 'side'; resetKey: string }) {
  const View = VIEWS[route.view];
  return (
    <ViewBoundary resetKey={resetKey} fallback={(error, reset) => <ViewError error={error} reset={reset} />}>
      <Suspense fallback={<ViewLoading />}>
        <View route={route} pane={pane} />
      </Suspense>
    </ViewBoundary>
  );
}
