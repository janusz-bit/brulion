/**
 * Top bar: back / forward, the title (folder breadcrumb for pages), search, quick capture,
 * favourite, side panel toggle and the "more" menu.
 */
import { PanelLeft, PanelRight, PenLine } from 'lucide-react';
import { Fragment } from 'react';
import { cx, IconButton, MenuButton, type MenuItem } from '@/app/components';
import { useAppStore, useFavourites, useIndexVersion, useLocale, useRoute, useT, useWorkspace } from '@/app/state/hooks';
import { toggleFavourite } from '@/app/state/settings';
import type { Route } from '@/app/state/store';
import type { Translator } from '@/core/i18n';
import { journalDateFromPath } from '@/core/markdown';
import { dirname, pageTitle } from '@/core/paths';
import type { LocalDate, Locale, VaultConfig } from '@/core/types';
import { resolveTheme } from '../services/theme';

const VIEW_TITLE: Record<Route['view'], string> = {
  journals: 'shell.nav.journals',
  page: 'shell.nav.pages',
  calendar: 'shell.nav.calendar',
  tasks: 'shell.nav.tasks',
  graph: 'shell.nav.graph',
  search: 'shell.nav.search',
  pages: 'shell.nav.pages',
  tags: 'shell.nav.tags',
  sync: 'shell.nav.sync',
  'import-export': 'shell.nav.importExport',
  history: 'shell.nav.history',
  trash: 'shell.nav.trash',
  settings: 'shell.nav.settings',
};

/** "środa, 14 października 2026" */
export function journalTitle(date: LocalDate, locale: Locale): string {
  const [y, m, d] = date.split('-').map(Number);
  return new Intl.DateTimeFormat(locale === 'pl' ? 'pl-PL' : 'en-GB', { weekday: 'long', day: 'numeric', month: 'long', year: 'numeric' }).format(
    new Date(y!, m! - 1, d!),
  );
}

/** Title of a route for the top bar / document title. */
export function routeTitle(route: Route, t: Translator, locale: Locale, config?: VaultConfig): string {
  if (route.view === 'page') {
    const date = config ? journalDateFromPath(route.path, config) : null;
    return date ? journalTitle(date, locale) : pageTitle(route.path);
  }
  if (route.view === 'history') return `${t(VIEW_TITLE.history)}: ${pageTitle(route.path)}`;
  if (route.view === 'tags' && route.tag) return `#${route.tag}`;
  return t(VIEW_TITLE[route.view]);
}

export function TopBar({ narrow }: { narrow: boolean }) {
  const t = useT();
  const locale = useLocale();
  const ws = useWorkspace();
  useIndexVersion();
  const route = useRoute();
  const backCount = useAppStore((x) => x.back.length);
  const forwardCount = useAppStore((x) => x.forward.length);
  const sidebarOpen = useAppStore((x) => x.sidebarOpen);
  const sidePanelOpen = useAppStore((x) => x.sidePanelOpen);
  const theme = useAppStore((x) => x.theme);
  // Actions are stable: read them once.
  const s = useAppStore.getState();
  const favourites = useFavourites();
  const pagePath = route.view === 'page' ? route.path : null;
  const isFav = pagePath ? favourites.includes(pagePath) : false;
  const title = routeTitle(route, t, locale, ws.config);
  const folder = pagePath ? dirname(pagePath) : '';
  const crumbs = folder && folder !== '.' && !journalDateFromPath(pagePath!, ws.config) ? folder.split('/') : [];
  const dark = resolveTheme(theme) === 'dark';

  const pageItems: MenuItem[] = pagePath
    ? [
        ...(!narrow ? [{ id: 'side', label: t('shell.cmd.openSide'), icon: 'columns' as const, onSelect: () => s.openSidePanel({ view: 'page', path: pagePath }) }] : []),
        {
          id: 'fav',
          label: isFav ? t('shell.cmd.unfavourite') : t('shell.cmd.favourite'),
          icon: 'star',
          onSelect: () => s.updateSettings((x) => toggleFavourite(x, ws.key, pagePath)),
        },
        { id: 'rename', label: t('shell.cmd.rename'), icon: 'pencil', onSelect: () => s.openDialog({ kind: 'rename', path: pagePath }) },
        { id: 'history', label: t('shell.cmd.history'), icon: 'clock', onSelect: () => s.navigate({ view: 'history', path: pagePath }) },
        { id: 'graph', label: t('shell.cmd.pageGraph'), icon: 'sparkles', onSelect: () => s.navigate({ view: 'graph', path: pagePath }) },
        { id: 'delete', label: t('shell.cmd.delete'), icon: 'trash', danger: true, onSelect: () => s.openDialog({ kind: 'delete', path: pagePath }) },
        { type: 'separator' },
      ]
    : [];
  const moreItems: MenuItem[] = [
    ...pageItems,
    { id: 'new', label: t('shell.cmd.newPage'), icon: 'edit', onSelect: () => s.openDialog({ kind: 'new-page' }) },
    { id: 'palette', label: t('shell.cmd.palette'), icon: 'keyboard', shortcut: narrow ? undefined : 'Mod+K', onSelect: () => s.setPaletteOpen(true) },
    { id: 'theme', label: dark ? t('shell.cmd.lightTheme') : t('shell.cmd.darkTheme'), icon: dark ? 'sun' : 'moon', onSelect: () => s.setTheme(dark ? 'light' : 'dark') },
    { type: 'separator' },
    { id: 'import', label: t('shell.nav.importExport'), icon: 'external', onSelect: () => s.navigate({ view: 'import-export' }) },
    { id: 'trash', label: t('shell.nav.trash'), icon: 'trash', onSelect: () => s.navigate({ view: 'trash' }) },
    { id: 'settings', label: t('shell.nav.settings'), icon: 'settings', shortcut: narrow ? undefined : 'Mod+,', onSelect: () => s.navigate({ view: 'settings' }) },
  ];

  return (
    <header className={cx('b-topbar', narrow && 'is-narrow')}>
      <div className="b-topbar__start">
        {!narrow && (
          <IconButton
            icon={PanelLeft}
            label={sidebarOpen ? t('shell.topbar.hideSidebar') : t('shell.topbar.showSidebar')}
            pressed={sidebarOpen}
            onClick={() => s.toggleSidebar()}
          />
        )}
        {(!narrow || backCount > 0) && (
          <IconButton icon="arrow-left" label={t('shell.cmd.back')} disabled={backCount === 0} onClick={() => s.goBack()} />
        )}
        {!narrow && <IconButton icon="arrow-right" label={t('shell.cmd.forward')} disabled={forwardCount === 0} onClick={() => s.goForward()} />}
      </div>

      <div className="b-topbar__title">
        {crumbs.length > 0 && (
          <span className="b-topbar__crumbs" aria-hidden={narrow ? true : undefined}>
            {crumbs.map((c, i) => (
              <Fragment key={i}>
                <span className="b-topbar__crumb">{c}</span>
                <span className="b-topbar__sep">/</span>
              </Fragment>
            ))}
          </span>
        )}
        <span className="b-topbar__name" title={pagePath ?? title}>
          {title}
        </span>
      </div>

      <div className="b-topbar__end">
        {!narrow && <IconButton icon="search" label={t('shell.nav.search')} onClick={() => s.navigate({ view: 'search' })} />}
        {!narrow && <IconButton icon={PenLine} label={t('shell.cmd.capture')} onClick={() => s.setCaptureOpen(true)} className="b-topbar__capture" />}
        {pagePath && !narrow && (
          <IconButton
            icon="star"
            label={isFav ? t('shell.cmd.unfavourite') : t('shell.cmd.favourite')}
            pressed={isFav}
            className={cx('b-topbar__fav', isFav && 'is-on')}
            onClick={() => s.updateSettings((x) => toggleFavourite(x, ws.key, pagePath))}
          />
        )}
        {!narrow && (
          <IconButton
            icon={PanelRight}
            label={sidePanelOpen ? t('shell.topbar.hidePanel') : t('shell.topbar.showPanel')}
            pressed={sidePanelOpen}
            onClick={() => s.toggleSidePanel()}
          />
        )}
        <MenuButton label={t('shell.topbar.more')} icon="more" iconOnly variant="ghost" placement="bottom-end" items={moreItems} />
      </div>
    </header>
  );
}
