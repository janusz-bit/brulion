/**
 * Top-level layout (docs/UI.md): splash while booting, the welcome screen / vault picker, and
 * the shell — desktop: resizable sidebar, top bar, main pane, optional right side panel;
 * phone (< 768 px): top bar, main pane, bottom navigation, sidebar drawer, capture button.
 * The current route's view is lazily loaded (shell/views.tsx).
 */
import { useEffect, useMemo } from 'react';
import { Button, EmptyState, LocaleContext, Spinner, Splitter, useMediaQuery } from '@/app/components';
import { EditorHostProvider, type EditorHost, type EditorRoute } from '@/app/editor';
import { useAppStore, useLocale, useRoute, useSettings, useT, useWorkspace } from '@/app/state/hooks';
import type { Route } from '@/app/state/store';
import { useBootStore } from './services/boot';
import { CommandPalette } from './shell/CommandPalette';
import { ShellDialogs, Toasts } from './shell/Dialogs';
import { useGlobalShortcuts } from './shell/keyboard';
import { CaptureFab, Drawer, MobileNav } from './shell/MobileNav';
import { Onboarding } from './shell/Onboarding';
import { IndexProgress, MainPane, SidePanel } from './shell/Panes';
import { QuickCapture } from './shell/QuickCapture';
import { QuickSwitcher } from './shell/QuickSwitcher';
import { Sidebar } from './shell/Sidebar';
import { routeTitle, TopBar } from './shell/TopBar';
import { preloadViews } from './shell/views';

/** Width below which the phone layout is used (matches app.css). */
export const MOBILE_QUERY = '(max-width: 767.98px)';

function Splash() {
  const t = useT();
  const status = useAppStore((s) => s.status);
  return (
    <div className="b-splash" aria-busy="true">
      <img src={`${import.meta.env.BASE_URL}favicon.svg`} alt="" width={64} height={64} className="b-splash__logo" />
      <Spinner size={22} label={status === 'loading' ? t('shell.splash.opening') : t('common.loading')} />
    </div>
  );
}

function FatalError() {
  const t = useT();
  const error = useAppStore((s) => s.error);
  return (
    <div className="b-splash">
      <EmptyState
        icon="alert"
        title={t('shell.error.fatal')}
        body={error}
        action={
          <Button variant="primary" onClick={() => window.location.reload()}>
            {t('shell.update.reload')}
          </Button>
        }
      />
    </div>
  );
}

/** Heading of a link target ("Page#Heading") → its line in the page. */
function headingLine(ws: ReturnType<typeof useWorkspace>, path: string, heading: string): number | undefined {
  const want = heading.trim().toLowerCase();
  return ws.index.note(path)?.headings.find((h) => h.text.trim().toLowerCase() === want)?.line;
}

/** Everything editors need from the shell. */
function useEditorHostValue(): EditorHost {
  const ws = useWorkspace();
  const t = useT();
  const locale = useLocale();
  const settings = useSettings();
  return useMemo((): EditorHost => {
    const nav = (r: Route, newPane?: boolean) => useAppStore.getState().navigate(r, { newPane });
    return {
      workspace: ws,
      t,
      locale,
      now: () => new Date(),
      navigate: (r: EditorRoute, opts) => nav(r, opts?.newPane),
      openLink: (target, opts) => {
        const hash = target.indexOf('#');
        const page = hash === -1 ? target : target.slice(0, hash);
        const frag = hash === -1 ? '' : target.slice(hash + 1);
        const resolve = page.trim() ? ws.resolveOrCreate(page, opts?.from) : Promise.resolve(opts?.from ?? '');
        void resolve
          .then((path) => {
            if (!path) return;
            const r: Route = { view: 'page', path };
            if (frag.startsWith('^')) r.blockId = frag.slice(1);
            else if (frag) {
              const line = headingLine(ws, path, frag);
              if (line !== undefined) r.line = line;
            }
            nav(r, opts?.newPane);
          })
          .catch((e: unknown) => {
            useAppStore.getState().toast({ kind: 'error', message: t('shell.error.link', { error: e instanceof Error ? e.message : String(e) }) });
          });
      },
      settings: { ...settings.editor, fontSize: settings.fontSize },
    };
  }, [ws, t, locale, settings.editor, settings.fontSize]);
}

function DesktopLayout() {
  const t = useT();
  const sidebarOpen = useAppStore((s) => s.sidebarOpen);
  const sidePanelOpen = useAppStore((s) => s.sidePanelOpen);
  return (
    <div className="b-shell">
      <Splitter
        fixed="first"
        defaultSize={272}
        min={200}
        max={480}
        storageKey="sidebar"
        collapsed={!sidebarOpen}
        aria-label={t('shell.sidebar.resize')}
        className="b-shell__split"
      >
        <Sidebar />
        <div className="b-main">
          <TopBar narrow={false} />
          <IndexProgress />
          <div className="b-main__body">
            {sidePanelOpen ? (
              <Splitter fixed="second" defaultSize={340} min={260} max={720} storageKey="sidepanel" aria-label={t('shell.panel.resize')} className="b-main__split">
                <MainPane />
                <SidePanel />
              </Splitter>
            ) : (
              <MainPane />
            )}
          </div>
        </div>
      </Splitter>
    </div>
  );
}

function MobileLayout() {
  const route = useRoute();
  const showFab = route.view !== 'settings';
  return (
    <div className="b-mobile">
      <TopBar narrow />
      <IndexProgress />
      <MainPane />
      {showFab && <CaptureFab />}
      <MobileNav />
      <Drawer>
        <Sidebar inDrawer />
      </Drawer>
    </div>
  );
}

function Shell() {
  const narrow = useMediaQuery(MOBILE_QUERY);
  const host = useEditorHostValue();
  const t = useT();
  const locale = useLocale();
  const route = useRoute();
  const ws = useWorkspace();
  useGlobalShortcuts();
  useEffect(() => preloadViews(), []);
  useEffect(() => {
    document.title = `${routeTitle(route, t, locale, ws.config)} · Brulion`;
  }, [route, t, locale, ws]);
  return (
    <EditorHostProvider value={host}>
      <div className="b-app" data-layout={narrow ? 'narrow' : 'wide'}>
        <a href="#b-main" className="b-skip">
          {t('shell.skip')}
        </a>
        {narrow ? <MobileLayout /> : <DesktopLayout />}
        <CommandPalette />
        <QuickSwitcher />
        <QuickCapture />
        <ShellDialogs />
      </div>
    </EditorHostProvider>
  );
}

export function App() {
  const status = useAppStore((s) => s.status);
  const hasWorkspace = useAppStore((s) => s.workspace !== null);
  const locale = useLocale();
  const switching = useBootStore((s) => s.switching);
  const narrow = useMediaQuery(MOBILE_QUERY);
  let body;
  if (status === 'error') body = <FatalError />;
  else if (status === 'onboarding' || (switching && status === 'ready')) body = <Onboarding />;
  else if (status === 'ready' && hasWorkspace) body = <Shell />;
  else body = <Splash />;
  return (
    <LocaleContext.Provider value={locale}>
      {body}
      <Toasts aboveNav={narrow && status === 'ready' && !switching} />
    </LocaleContext.Provider>
  );
}
