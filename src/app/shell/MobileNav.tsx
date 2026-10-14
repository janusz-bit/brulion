/**
 * Phone layout pieces: the bottom navigation (Dziennik, Kalendarz, Szukaj, Zadania, Menu),
 * the sidebar drawer (swipe from the left edge, backdrop, Escape / back closes it) and the
 * floating quick-capture button.
 */
import { PenLine } from 'lucide-react';
import { useEffect, useRef, type ReactNode } from 'react';
import { cx, Icon, pushLayer, renderIcon, useScrollLock, type IconSlot } from '@/app/components';
import { useAppStore, useRoute, useT } from '@/app/state/hooks';
import type { Route, ViewName } from '@/app/state/store';

interface Tab {
  id: ViewName | 'menu';
  label: string;
  icon: IconSlot;
  route?: Route;
}

export function MobileNav() {
  const t = useT();
  const route = useRoute();
  const navigate = useAppStore((s) => s.navigate);
  const drawerOpen = useAppStore((s) => s.drawerOpen);
  const setDrawerOpen = useAppStore((s) => s.setDrawerOpen);
  const tabs: Tab[] = [
    { id: 'journals', label: t('shell.mobile.journal'), icon: 'book', route: { view: 'journals' } },
    { id: 'calendar', label: t('shell.nav.calendar'), icon: 'calendar', route: { view: 'calendar' } },
    { id: 'search', label: t('shell.mobile.search'), icon: 'search', route: { view: 'search' } },
    { id: 'tasks', label: t('shell.nav.tasks'), icon: 'tasks', route: { view: 'tasks' } },
    { id: 'menu', label: t('shell.mobile.menu'), icon: 'menu' },
  ];
  return (
    <nav className="b-mobilenav" aria-label={t('shell.mobile.nav')}>
      {tabs.map((tab) => {
        const active = tab.id === 'menu' ? drawerOpen : !drawerOpen && route.view === tab.id;
        return (
          <button
            key={tab.id}
            type="button"
            className={cx('b-mobilenav__btn', active && 'is-active')}
            aria-current={active && tab.id !== 'menu' ? 'page' : undefined}
            aria-expanded={tab.id === 'menu' ? drawerOpen : undefined}
            onClick={() => (tab.route ? navigate(tab.route) : setDrawerOpen(!drawerOpen))}
          >
            <span className="b-mobilenav__icon">{renderIcon(tab.icon, 22)}</span>
            <span className="b-mobilenav__label">{tab.label}</span>
          </button>
        );
      })}
    </nav>
  );
}

const EDGE = 24;
const SWIPE = 56;

/** Swipe right from the left screen edge opens the drawer. */
function useEdgeSwipe(onOpen: () => void, enabled: boolean): void {
  useEffect(() => {
    if (!enabled) return;
    let start: { x: number; y: number; id: number } | null = null;
    const down = (e: TouchEvent) => {
      const touch = e.touches[0];
      start = touch && e.touches.length === 1 && touch.clientX <= EDGE ? { x: touch.clientX, y: touch.clientY, id: touch.identifier } : null;
    };
    const move = (e: TouchEvent) => {
      if (!start) return;
      const touch = Array.from(e.touches).find((x) => x.identifier === start!.id);
      if (!touch) return;
      const dx = touch.clientX - start.x;
      const dy = Math.abs(touch.clientY - start.y);
      if (dy > 40) start = null;
      else if (dx > SWIPE) {
        start = null;
        onOpen();
      }
    };
    const end = () => {
      start = null;
    };
    document.addEventListener('touchstart', down, { passive: true });
    document.addEventListener('touchmove', move, { passive: true });
    document.addEventListener('touchend', end, { passive: true });
    return () => {
      document.removeEventListener('touchstart', down);
      document.removeEventListener('touchmove', move);
      document.removeEventListener('touchend', end);
    };
  }, [onOpen, enabled]);
}

export function Drawer({ children }: { children: ReactNode }) {
  const t = useT();
  const open = useAppStore((s) => s.drawerOpen);
  const setOpen = useAppStore((s) => s.setDrawerOpen);
  const panelRef = useRef<HTMLDivElement | null>(null);
  const swipe = useRef<{ x: number; y: number } | null>(null);
  useEdgeSwipe(() => setOpen(true), !open);
  useScrollLock(open);

  useEffect(() => {
    if (!open) return;
    const previous = document.activeElement instanceof HTMLElement ? document.activeElement : null;
    const off = pushLayer({ contains: (n) => !!panelRef.current?.contains(n), onEscape: () => setOpen(false) });
    requestAnimationFrame(() => panelRef.current?.querySelector<HTMLElement>('button, a')?.focus({ preventScroll: true }));
    return () => {
      off();
      if (previous?.isConnected) previous.focus({ preventScroll: true });
    };
  }, [open, setOpen]);

  return (
    <div className={cx('b-drawer', open && 'is-open')} aria-hidden={!open} inert={!open ? true : undefined}>
      <div className="b-drawer__backdrop" onClick={() => setOpen(false)} />
      <div
        ref={panelRef}
        className="b-drawer__panel"
        role="dialog"
        aria-modal="true"
        aria-label={t('shell.sidebar.label')}
        onTouchStart={(e) => {
          const touch = e.touches[0];
          swipe.current = touch ? { x: touch.clientX, y: touch.clientY } : null;
        }}
        onTouchMove={(e) => {
          const s = swipe.current;
          const touch = e.touches[0];
          if (!s || !touch) return;
          if (Math.abs(touch.clientY - s.y) > 50) swipe.current = null;
          else if (s.x - touch.clientX > SWIPE) {
            swipe.current = null;
            setOpen(false);
          }
        }}
      >
        {open && children}
      </div>
    </div>
  );
}

export function CaptureFab() {
  const t = useT();
  const setCaptureOpen = useAppStore((s) => s.setCaptureOpen);
  return (
    <button type="button" className="b-fab" aria-label={t('shell.cmd.capture')} title={t('shell.cmd.capture')} onClick={() => setCaptureOpen(true)}>
      <Icon icon={PenLine} size={24} />
    </button>
  );
}
