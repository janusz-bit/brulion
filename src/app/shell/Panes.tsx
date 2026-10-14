/**
 * The main pane (current route's view, scroll position remembered per route) and the desktop
 * right side panel (a second route, or the default tabs: backlinks / outline / calendar).
 */
import { Suspense, lazy, useLayoutEffect, useRef, type UIEvent } from 'react';
import { IconButton, ProgressBar, Tabs } from '@/app/components';
import { useAppStore, useLocale, useRoute, useSettings, useT, useWorkspace } from '@/app/state/hooks';
import { routeKey, type Route } from '@/app/state/store';
import { routeTitle } from './TopBar';
import { RouteView, ViewLoading } from './views';

const Panels = {
  backlinks: lazy(async () => ({ default: (await import('@/app/views/panels')).BacklinksPanel })),
  outline: lazy(async () => ({ default: (await import('@/app/views/panels')).OutlinePanel })),
  calendar: lazy(async () => ({ default: (await import('@/app/views/panels')).CalendarPanel })),
};

/** Restore a scroll offset once the (lazily loaded) content is tall enough. */
function restoreScroll(el: HTMLElement, top: number): () => void {
  el.scrollTop = top;
  if (top <= 0) return () => {};
  let frames = 0;
  let raf = requestAnimationFrame(function tick() {
    if (el.scrollHeight - el.clientHeight >= top) {
      el.scrollTop = top;
      return;
    }
    if (++frames < 90) raf = requestAnimationFrame(tick);
  });
  return () => cancelAnimationFrame(raf);
}

export function MainPane() {
  const route = useRoute();
  const key = routeKey(route);
  const ref = useRef<HTMLDivElement | null>(null);
  const positions = useRef(new Map<string, number>());
  const currentKey = useRef(key);

  useLayoutEffect(() => {
    const el = ref.current;
    if (!el) return;
    currentKey.current = key;
    const targeted = route.view === 'page' && (route.line !== undefined || !!route.blockId);
    return restoreScroll(el, targeted ? 0 : (positions.current.get(key) ?? 0));
    // Only when the destination changes (not for line targets on the same page).
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [key]);

  const onScroll = (e: UIEvent<HTMLDivElement>) => {
    positions.current.set(currentKey.current, e.currentTarget.scrollTop);
  };

  return (
    <main ref={ref} className="b-pane b-pane--main" data-view={route.view} onScroll={onScroll} id="b-main" tabIndex={-1}>
      <RouteView key={key} route={route} pane="main" resetKey={key} />
    </main>
  );
}

function DefaultPanel() {
  const t = useT();
  const route = useRoute();
  const tab = useSettings().sidePanelTab;
  const update = useAppStore((s) => s.updateSettings);
  const path = route.view === 'page' ? route.path : null;
  const Panel = Panels[tab];
  return (
    <div className="b-sidepanel__default">
      <Tabs
        items={[
          { id: 'backlinks', label: t('shell.panel.backlinks') },
          { id: 'outline', label: t('shell.panel.outline') },
          { id: 'calendar', label: t('shell.panel.calendar') },
        ]}
        value={tab}
        onChange={(id) => update({ sidePanelTab: id })}
        aria-label={t('shell.panel.label')}
        fitted
        className="b-sidepanel__tabs"
      />
      <div className="b-sidepanel__body">
        <Suspense fallback={<ViewLoading />}>
          <Panel path={path} />
        </Suspense>
      </div>
    </div>
  );
}

export function SidePanel() {
  const t = useT();
  const locale = useLocale();
  const ws = useWorkspace();
  const panel = useAppStore((s) => s.sidePanel);
  const close = useAppStore((s) => s.openSidePanel);
  const navigate = useAppStore((s) => s.navigate);
  const title = panel ? routeTitle(panel, t, locale, ws.config) : t('shell.panel.label');
  return (
    <aside className="b-sidepanel" aria-label={t('shell.panel.label')}>
      <div className="b-sidepanel__head">
        <span className="b-sidepanel__title">{title}</span>
        {panel && (
          <IconButton icon="external" size="sm" label={t('shell.panel.openMain')} onClick={() => navigate(panel as Route)} />
        )}
        <IconButton icon="x" size="sm" label={t('shell.topbar.hidePanel')} onClick={() => close(null)} />
      </div>
      {panel ? (
        <div className="b-sidepanel__route">
          <RouteView key={routeKey(panel)} route={panel} pane="side" resetKey={routeKey(panel)} />
        </div>
      ) : (
        <DefaultPanel />
      )}
    </aside>
  );
}

/** Thin bar while the vault is being indexed. */
export function IndexProgress() {
  const t = useT();
  const p = useAppStore((s) => s.indexProgress);
  if (!p || p.total < 50) return null;
  return (
    <div className="b-indexing">
      <ProgressBar value={p.indexed} max={p.total} size="sm" tone="accent" label={t('shell.indexing', { done: p.indexed, total: p.total })} />
    </div>
  );
}
