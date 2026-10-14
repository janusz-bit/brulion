import { bindBrowserHistory, MAX_HISTORY, parseRoute, resetAppStore, routeKey, sameRoute, useAppStore, type HistoryWindow, type Route } from './store';

const st = () => useAppStore.getState();
const page = (path: string, line?: number): Route => (line === undefined ? { view: 'page', path } : { view: 'page', path, line });

/** A tiny browser history: entries + index, popstate dispatched asynchronously like browsers. */
function fakeWindow() {
  const entries: unknown[] = [null];
  let index = 0;
  const listeners = new Set<(e: PopStateEvent) => void>();
  const fire = () => {
    const state = entries[index];
    queueMicrotask(() => listeners.forEach((l) => l({ state } as PopStateEvent)));
  };
  const win: HistoryWindow & { entries: unknown[]; index(): number; browserBack(): void; browserForward(): void } = {
    entries,
    index: () => index,
    history: {
      get state() {
        return entries[index];
      },
      pushState(state: unknown) {
        entries.splice(index + 1, entries.length, state);
        index = entries.length - 1;
      },
      replaceState(state: unknown) {
        entries[index] = state;
      },
      back() {
        if (index > 0) {
          index--;
          fire();
        }
      },
    } as unknown as HistoryWindow['history'],
    addEventListener: (_t, cb) => void listeners.add(cb),
    removeEventListener: (_t, cb) => void listeners.delete(cb),
    browserBack() {
      this.history.back();
    },
    browserForward() {
      if (index < entries.length - 1) {
        index++;
        fire();
      }
    },
  };
  return win;
}

const tick = () => new Promise((r) => setTimeout(r, 0));

beforeEach(() => resetAppStore());

describe('routes', () => {
  it('builds stable keys and compares routes', () => {
    expect(routeKey({ view: 'journals' })).toBe('journals');
    expect(routeKey(page('a.md', 3))).toBe('page:a.md');
    expect(routeKey({ view: 'search', q: 'żółw' })).toBe('search:żółw');
    expect(sameRoute(page('a.md', 1), page('a.md', 1))).toBe(true);
    expect(sameRoute(page('a.md', 1), page('a.md', 2))).toBe(false);
    expect(sameRoute({ view: 'tasks' }, { view: 'tasks' })).toBe(true);
  });

  it('parses stored routes defensively', () => {
    expect(parseRoute({ view: 'page', path: 'Notatki/Ćma.md', line: 4 })).toEqual({ view: 'page', path: 'Notatki/Ćma.md', line: 4 });
    expect(parseRoute({ view: 'page' })).toBeNull();
    expect(parseRoute({ view: 'page', path: 'a.md', line: -1 })).toEqual({ view: 'page', path: 'a.md' });
    expect(parseRoute({ view: 'calendar', date: '2026-10-14', mode: 'week' })).toEqual({ view: 'calendar', date: '2026-10-14', mode: 'week' });
    expect(parseRoute({ view: 'calendar', date: 'jutro' })).toEqual({ view: 'calendar' });
    expect(parseRoute({ view: 'nope' })).toBeNull();
    expect(parseRoute(null)).toBeNull();
    expect(parseRoute({ view: 'settings', section: 'vault' })).toEqual({ view: 'settings', section: 'vault' });
  });
});

describe('navigation (no browser history)', () => {
  it('pushes, goes back and forward', () => {
    st().navigate(page('a.md'));
    st().navigate(page('b.md'));
    expect(st().back).toEqual([{ view: 'journals' }, page('a.md')]);
    st().goBack();
    expect(st().route).toEqual(page('a.md'));
    expect(st().forward).toEqual([page('b.md')]);
    st().goForward();
    expect(st().route).toEqual(page('b.md'));
    st().goBack();
    st().navigate({ view: 'tasks' });
    expect(st().forward).toEqual([]);
  });

  it('ignores navigating to the same route, replaces when asked and closes overlays', () => {
    st().setPaletteOpen(true);
    st().navigate({ view: 'journals' });
    expect(st().back).toHaveLength(0);
    expect(st().paletteOpen).toBe(false);
    st().navigate(page('a.md'));
    st().navigate(page('c.md'), { replace: true });
    expect(st().route).toEqual(page('c.md'));
    expect(st().back).toEqual([{ view: 'journals' }]);
  });

  it('opens a new pane in the side panel instead', () => {
    st().navigate(page('a.md'), { newPane: true });
    expect(st().route).toEqual({ view: 'journals' });
    expect(st().sidePanel).toEqual(page('a.md'));
    expect(st().sidePanelOpen).toBe(true);
    st().toggleSidePanel();
    expect(st().sidePanelOpen).toBe(false);
    st().toggleSidePanel();
    expect(st().sidePanelOpen).toBe(true);
    expect(st().sidePanel).toBeNull();
    st().openSidePanel(null);
    expect(st().sidePanelOpen).toBe(false);
  });

  it('bounds the history', () => {
    for (let i = 0; i < MAX_HISTORY + 20; i++) st().navigate(page(`${i}.md`));
    expect(st().back).toHaveLength(MAX_HISTORY);
  });

  it('switches between palette, switcher and capture', () => {
    st().setPaletteOpen(true);
    st().setSwitcherOpen(true);
    expect(st().paletteOpen).toBe(false);
    expect(st().switcherOpen).toBe(true);
    st().openDialog({ kind: 'rename', path: 'a.md' });
    expect(st().switcherOpen).toBe(false);
    expect(st().closeOverlays()).toBe(true);
    expect(st().dialog).toBeNull();
    expect(st().closeOverlays()).toBe(false);
  });
});

describe('browser history binding', () => {
  it('pushes an entry per navigation and follows browser back/forward', async () => {
    const win = fakeWindow();
    const unbind = bindBrowserHistory(win);
    expect(win.entries).toEqual([{ brulion: 0 }]);
    st().navigate(page('a.md'));
    st().navigate(page('b.md'));
    expect(win.entries).toEqual([{ brulion: 0 }, { brulion: 1 }, { brulion: 2 }]);

    win.browserBack(); // Android back gesture
    await tick();
    expect(st().route).toEqual(page('a.md'));
    win.browserForward();
    await tick();
    expect(st().route).toEqual(page('b.md'));

    // The UI back button goes through history.back().
    st().goBack();
    expect(st().route).toEqual(page('b.md'));
    await tick();
    expect(st().route).toEqual(page('a.md'));
    expect(win.index()).toBe(1);

    // UI forward pushes a fresh entry.
    st().goForward();
    expect(st().route).toEqual(page('b.md'));
    expect(win.history.state).toEqual({ brulion: 2 });

    st().navigate(page('c.md'), { replace: true });
    expect(win.history.state).toEqual({ brulion: 2 });
    unbind();
  });

  it('closes an open overlay on back instead of navigating', async () => {
    const win = fakeWindow();
    const unbind = bindBrowserHistory(win);
    st().navigate(page('a.md'));
    st().setDrawerOpen(true);
    win.browserBack();
    await tick();
    expect(st().drawerOpen).toBe(false);
    expect(st().route).toEqual(page('a.md'));
    expect(win.history.state).toEqual({ brulion: 1 });
    win.browserBack();
    await tick();
    expect(st().route).toEqual({ view: 'journals' });
    unbind();
  });

  it('ignores entries that are not ours', async () => {
    const win = fakeWindow();
    win.history.pushState({ other: true }, '');
    const unbind = bindBrowserHistory(win);
    st().navigate(page('a.md'));
    win.browserBack();
    await tick();
    win.browserBack();
    await tick();
    expect(st().route).toEqual({ view: 'journals' });
    unbind();
  });
});

describe('toasts', () => {
  afterEach(() => vi.useRealTimers());

  it('adds, replaces by id and auto-dismisses', () => {
    vi.useFakeTimers();
    const id = st().toast({ message: 'Zapisano' });
    expect(st().toasts).toHaveLength(1);
    st().toast({ id, message: 'Zapisano ponownie', kind: 'success' });
    expect(st().toasts).toEqual([expect.objectContaining({ id, message: 'Zapisano ponownie' })]);
    vi.advanceTimersByTime(6000);
    expect(st().toasts).toHaveLength(0);
  });

  it('keeps reminders until closed and limits the stack', () => {
    vi.useFakeTimers();
    const id = st().toast({ kind: 'reminder', message: 'Kupić mleko' });
    for (let i = 0; i < 8; i++) st().toast({ message: `t${i}`, timeout: 0 });
    expect(st().toasts).toHaveLength(5);
    vi.advanceTimersByTime(60_000);
    expect(st().toasts).toHaveLength(5);
    st().dismissToast(st().toasts[0]!.id);
    expect(st().toasts).toHaveLength(4);
    void id;
  });
});

describe('settings in the store', () => {
  it('updates theme / locale with the settings', () => {
    st().setTheme('dark');
    expect(st().theme).toBe('dark');
    expect(st().settings.theme).toBe('dark');
    st().setLocale('en');
    expect(st().locale).toBe('en');
    st().updateSettings((s) => ({ ...s, fontSize: 20 }));
    expect(st().settings.fontSize).toBe(20);
    st().setSidebarOpen(false);
    expect(st().settings.sidebarVisible).toBe(false);
  });
});
