// @vitest-environment jsdom
import { act, useState } from 'react';
import { cleanup, click, key, render } from '@/app/components/render.testutil';
import { translatorFor } from '@/app/components';
import { resetAppStore, useAppStore } from '@/app/state/store';
import { registerBuiltinCommands } from '../services/commands';
import { applyDocumentSettings, resolveTheme } from '../services/theme';
import { normalizeSettings } from '../state/settings';
import { findShortcutCommand } from './keyboard';
import { Picker, type PickerItem, type PickModifiers } from './Picker';
import { describeSync, formatSyncTime } from './SyncIndicator';
import { journalTitle, routeTitle } from './TopBar';

afterEach(cleanup);
beforeEach(() => resetAppStore());
vi.setConfig({ testTimeout: 30_000 });

const t = translatorFor('pl');

describe('Picker', () => {
  const items: PickerItem[] = [
    { id: 'a', label: 'Dziennik', group: 'Nawigacja' },
    { id: 'b', label: 'Kalendarz', group: 'Nawigacja', shortcut: 'Mod+K' },
    { id: 'c', label: 'Zadania', hint: 'Projekty' },
  ];

  function Harness({ onPick }: { onPick: (id: string, mods: PickModifiers) => void }) {
    const [q, setQ] = useState('');
    return (
      <Picker
        open
        onClose={() => {}}
        title="Otwórz stronę"
        placeholder="Szukaj"
        query={q}
        onQueryChange={setQ}
        items={items}
        onPick={(item, mods) => onPick(item.id, mods)}
        empty="Nic"
      />
    );
  }

  it('is a combobox with an active option moved by the arrow keys', () => {
    const picks: [string, PickModifiers][] = [];
    render(<Harness onPick={(id, m) => picks.push([id, m])} />);
    const input = document.querySelector<HTMLInputElement>('[role=combobox]')!;
    expect(document.activeElement).toBe(input);
    const options = () => [...document.querySelectorAll('[role=option]')];
    expect(options().map((o) => o.getAttribute('aria-selected'))).toEqual(['true', 'false', 'false']);
    expect(document.querySelectorAll('.b-picker__group')).toHaveLength(1);
    key(input, 'ArrowDown');
    key(input, 'ArrowDown');
    expect(input.getAttribute('aria-activedescendant')).toBe(options()[2]!.id);
    key(input, 'ArrowDown');
    expect(options()[0]!.getAttribute('aria-selected')).toBe('true');
    key(input, 'ArrowUp');
    key(input, 'Enter', { ctrlKey: true });
    expect(picks).toEqual([['c', { mod: true, shift: false, alt: false }]]);
    click(options()[1]);
    expect(picks[1]![0]).toBe('b');
  });
});

describe('sync summary', () => {
  const now = new Date(2026, 9, 14, 12, 0);
  const ctx = { syncthing: false, now, t, locale: 'pl' as const };

  it('formats relative sync times in Polish', () => {
    expect(formatSyncTime(now.getTime() - 20_000, now, t, 'pl')).toBe('przed chwilą');
    expect(formatSyncTime(now.getTime() - 5 * 60_000, now, t, 'pl')).toBe('5 min temu');
    expect(formatSyncTime(new Date(2026, 9, 14, 9, 5).getTime(), now, t, 'pl')).toBe('o 09:05');
    expect(formatSyncTime(new Date(2026, 9, 12, 9, 5).getTime(), now, t, 'pl')).toMatch(/12 paź.* 09:05/);
  });

  it('describes every state', () => {
    expect(describeSync({ state: 'disabled' }, ctx)).toEqual({ tone: 'muted', label: 'Tylko na tym urządzeniu' });
    expect(describeSync({ state: 'disabled' }, { ...ctx, syncthing: true }).label).toBe('Folder synchronizowany na zewnątrz');
    expect(describeSync({ state: 'idle', lastSync: now.getTime() - 120_000 }, ctx).label).toBe('Zsynchronizowano 2 min temu');
    expect(describeSync({ state: 'syncing', progress: { phase: 'syncing', done: 3, total: 10 } }, ctx)).toEqual({
      tone: 'busy',
      label: 'Synchronizacja…',
      detail: '3 / 10',
    });
    expect(describeSync({ state: 'error', lastError: '401' }, ctx)).toEqual({ tone: 'error', label: 'Błąd synchronizacji', detail: '401' });
    expect(describeSync({ state: 'offline' }, ctx).label).toBe('Offline');
  });
});

describe('titles', () => {
  it('names routes, journal pages get a full Polish date', () => {
    expect(journalTitle('2026-10-14', 'pl')).toBe('środa, 14 października 2026');
    expect(journalTitle('2026-10-14', 'en')).toMatch(/Wednesday/);
    const config = { version: 1, journalsFolder: 'journals', journalFileFormat: 'yyyy-MM-dd', newPagesFolder: '', attachmentsFolder: 'assets', templatesFolder: 'templates', journalTemplate: '', defaultReminderTime: '09:00', weekStartsOn: 1 } as const;
    expect(routeTitle({ view: 'page', path: 'journals/2026-10-14.md' }, t, 'pl', config)).toBe('środa, 14 października 2026');
    expect(routeTitle({ view: 'page', path: 'Projekty/Remont mieszkania.md' }, t, 'pl', config)).toBe('Remont mieszkania');
    expect(routeTitle({ view: 'tags', tag: 'dom' }, t, 'pl')).toBe('#dom');
    expect(routeTitle({ view: 'journals' }, t, 'pl')).toBe('Dziennik');
    expect(routeTitle({ view: 'history', path: 'a.md' }, t, 'pl')).toBe('Historia zmian: a');
  });
});

describe('global shortcuts', () => {
  it('finds the command bound to a key combination', () => {
    registerBuiltinCommands();
    const cache = new Map();
    const ev = (k: string, o: Partial<KeyboardEvent> = {}) => ({ key: k, code: '', ctrlKey: false, metaKey: false, altKey: false, shiftKey: false, ...o });
    expect(findShortcutCommand(ev('k', { ctrlKey: true }), false, cache)).toBe('app.palette');
    expect(findShortcutCommand(ev('o', { ctrlKey: true }), false, cache)).toBe('app.switcher');
    expect(findShortcutCommand(ev('F', { ctrlKey: true, shiftKey: true }), false, cache)).toBe('nav.search');
    expect(findShortcutCommand(ev('ArrowLeft', { altKey: true }), false, cache)).toBe('nav.back');
    expect(findShortcutCommand(ev('j', { metaKey: true }), true, cache)).toBe('journal.today');
    expect(findShortcutCommand(ev(',', { ctrlKey: true }), false, cache)).toBe('nav.settings');
    // Editor-only shortcuts are not global.
    expect(findShortcutCommand(ev('Enter', { ctrlKey: true }), false, cache)).toBeNull();
    expect(findShortcutCommand(ev('x', { ctrlKey: true }), false, cache)).toBeNull();
  });
});

describe('document settings', () => {
  it('applies theme, language and editor font to <html>', () => {
    const meta = document.createElement('meta');
    meta.name = 'theme-color';
    document.head.appendChild(meta);
    applyDocumentSettings(normalizeSettings({ theme: 'dark', locale: 'en', editorFont: 'sans', fontSize: 20 }));
    const root = document.documentElement;
    expect(root.dataset.theme).toBe('dark');
    expect(root.lang).toBe('en');
    expect(root.dataset.editorFont).toBe('sans');
    expect(root.style.getPropertyValue('--editor-font-size')).toBe('1.25rem');
    expect(meta.content).toBe('#1c2130');
    expect(resolveTheme('light')).toBe('light');
    act(() => useAppStore.getState().setTheme('light'));
    expect(useAppStore.getState().theme).toBe('light');
  });
});
