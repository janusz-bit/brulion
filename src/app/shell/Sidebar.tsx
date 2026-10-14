/**
 * Left sidebar (desktop) / drawer content (mobile): vault switcher, navigation, favourites,
 * recent pages, the file tree and the sync status footer.
 */
import { ChevronRight, Network } from 'lucide-react';
import { useState, type ReactNode } from 'react';
import { cx, Icon, IconButton, Kbd, MenuButton, renderIcon, type IconSlot } from '@/app/components';
import { useAppStore, useFavourites, useRecent, useRoute, useT, useWorkspace } from '@/app/state/hooks';
import type { Route, ViewName } from '@/app/state/store';
import { pageTitle } from '@/core/paths';
import { showVaultPicker } from '../services/boot';
import { FileTree } from './FileTree';
import { readLocal, writeLocal } from './storage';
import { SyncIndicator } from './SyncIndicator';

interface NavEntry {
  view: ViewName;
  route: Route;
  label: string;
  icon: IconSlot;
}

export function navEntries(t: (k: string) => string): NavEntry[] {
  return [
    { view: 'journals', route: { view: 'journals' }, label: t('shell.nav.journals'), icon: 'book' },
    { view: 'calendar', route: { view: 'calendar' }, label: t('shell.nav.calendar'), icon: 'calendar' },
    { view: 'tasks', route: { view: 'tasks' }, label: t('shell.nav.tasks'), icon: 'tasks' },
    { view: 'pages', route: { view: 'pages' }, label: t('shell.nav.pages'), icon: 'file' },
    { view: 'tags', route: { view: 'tags' }, label: t('shell.nav.tags'), icon: 'hash' },
    { view: 'graph', route: { view: 'graph' }, label: t('shell.nav.graph'), icon: Network },
    { view: 'search', route: { view: 'search' }, label: t('shell.nav.search'), icon: 'search' },
  ];
}

function Section({ id, title, children, action }: { id: string; title: string; children: ReactNode; action?: ReactNode }) {
  const key = `brulion.sidebar.${id}`;
  const [open, setOpen] = useState(() => readLocal<boolean>(key, true));
  return (
    <section className={cx('b-sidebar__section', !open && 'is-collapsed')} aria-label={title}>
      <div className="b-sidebar__section-head">
        <button
          type="button"
          className="b-sidebar__section-toggle"
          aria-expanded={open}
          onClick={() => {
            setOpen(!open);
            writeLocal(key, !open);
          }}
        >
          <Icon icon={ChevronRight} size={14} className="b-sidebar__chevron" />
          <span>{title}</span>
        </button>
        {action}
      </div>
      {open && <div className="b-sidebar__section-body">{children}</div>}
    </section>
  );
}

function PageLinks({ paths, empty }: { paths: readonly string[]; empty: string }) {
  const route = useRoute();
  const navigate = useAppStore((s) => s.navigate);
  const ws = useWorkspace();
  const existing = paths.filter((p) => ws.exists(p));
  if (existing.length === 0) return <p className="b-sidebar__empty">{empty}</p>;
  return (
    <ul className="b-sidebar__list">
      {existing.map((p) => {
        const active = route.view === 'page' && route.path === p;
        return (
          <li key={p}>
            <button
              type="button"
              className={cx('b-sidebar__link', active && 'is-active')}
              aria-current={active ? 'page' : undefined}
              title={p}
              onClick={(e) => navigate({ view: 'page', path: p }, { newPane: e.ctrlKey || e.metaKey })}
            >
              <Icon name="file" size={15} />
              <span className="b-sidebar__text">{pageTitle(p)}</span>
            </button>
          </li>
        );
      })}
    </ul>
  );
}

export function Sidebar({ inDrawer = false }: { inDrawer?: boolean }) {
  const t = useT();
  const ws = useWorkspace();
  const route = useRoute();
  const navigate = useAppStore((s) => s.navigate);
  const openDialog = useAppStore((s) => s.openDialog);
  const setSwitcherOpen = useAppStore((s) => s.setSwitcherOpen);
  const setDrawerOpen = useAppStore((s) => s.setDrawerOpen);
  const favourites = useFavourites();
  const recent = useRecent();

  return (
    <nav className={cx('b-sidebar', inDrawer && 'is-drawer')} aria-label={t('shell.sidebar.label')}>
      <div className="b-sidebar__head">
        <MenuButton
          label={ws.name}
          icon={<img src={`${import.meta.env.BASE_URL}favicon.svg`} alt="" width={20} height={20} className="b-sidebar__logo" />}
          variant="ghost"
          className="b-sidebar__vault"
          items={[
            { type: 'label', label: ws.vault.fs.label },
            { id: 'switch', label: t('shell.cmd.switchVault'), icon: 'folder', onSelect: () => showVaultPicker() },
            { id: 'vault-settings', label: t('shell.settings.vault'), icon: 'settings', onSelect: () => navigate({ view: 'settings', section: 'vault' }) },
          ]}
        />
        <IconButton icon="edit" label={t('shell.cmd.newPage')} onClick={() => openDialog({ kind: 'new-page' })} />
        {inDrawer && <IconButton icon="x" label={t('common.close')} onClick={() => setDrawerOpen(false)} />}
      </div>

      <div className="b-sidebar__scroll">
        <button type="button" className="b-sidebar__find" onClick={() => setSwitcherOpen(true)}>
          <Icon name="search" size={15} />
          <span>{t('shell.sidebar.find')}</span>
          {!inDrawer && <Kbd keys="Mod+O" className="b-sidebar__find-kbd" />}
        </button>

        <ul className="b-sidebar__list b-sidebar__nav">
          {navEntries(t).map((e) => {
            const active = route.view === e.view;
            return (
              <li key={e.view}>
                <button
                  type="button"
                  className={cx('b-sidebar__link', active && 'is-active')}
                  aria-current={active ? 'page' : undefined}
                  onClick={() => navigate(e.route)}
                >
                  {renderIcon(e.icon, 16)}
                  <span className="b-sidebar__text">{e.label}</span>
                </button>
              </li>
            );
          })}
        </ul>

        <Section id="favourites" title={t('shell.sidebar.favourites')}>
          <PageLinks paths={favourites} empty={t('shell.sidebar.noFavourites')} />
        </Section>
        <Section id="recent" title={t('shell.sidebar.recent')}>
          <PageLinks paths={recent.slice(0, 8)} empty={t('shell.sidebar.noRecent')} />
        </Section>
        <Section
          id="files"
          title={t('shell.sidebar.files')}
          action={<IconButton icon="plus" size="sm" label={t('shell.cmd.newPage')} onClick={() => openDialog({ kind: 'new-page' })} />}
        >
          <FileTree />
        </Section>
      </div>

      <div className="b-sidebar__foot">
        <SyncIndicator onClick={() => navigate({ view: 'sync' })} />
        <IconButton
          icon="settings"
          label={t('shell.nav.settings')}
          pressed={route.view === 'settings'}
          onClick={() => navigate({ view: 'settings' })}
        />
      </div>
    </nav>
  );
}
