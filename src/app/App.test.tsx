// @vitest-environment jsdom
/**
 * The whole app in jsdom on the real web platform (IndexedDB = fake-indexeddb): first run →
 * welcome screen → demo vault → quick switcher → command palette → settings → back →
 * quick capture. Needs @/core/index and @/core/reminders (the Workspace); skipped until then.
 */
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { act } from 'react';
import { cleanup, click, render } from '@/app/components/render.testutil';
import { useAppStore } from '@/app/state/store';

const ready =
  existsSync(fileURLToPath(new URL('../core/index/index.ts', import.meta.url))) &&
  existsSync(fileURLToPath(new URL('../core/reminders/index.ts', import.meta.url)));

vi.setConfig({ testTimeout: 120_000 });
afterAll(cleanup);

async function waitFor<T>(fn: () => T | null | undefined | false, what: string, timeout = 30_000): Promise<T> {
  const start = Date.now();
  for (;;) {
    let v: T | null | undefined | false = null;
    try {
      v = fn();
    } catch {
      v = null;
    }
    if (v) return v;
    if (Date.now() - start > timeout) throw new Error(`timed out waiting for ${what}`);
    await act(async () => {
      await new Promise((r) => setTimeout(r, 25));
    });
  }
}

function byText(selector: string, text: string | RegExp): HTMLElement | null {
  for (const el of document.querySelectorAll<HTMLElement>(selector)) {
    const t = el.textContent ?? '';
    if (typeof text === 'string' ? t.includes(text) : text.test(t)) return el;
  }
  return null;
}

function typeInto(el: HTMLInputElement | HTMLTextAreaElement, value: string): void {
  const proto = el instanceof HTMLTextAreaElement ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
  act(() => {
    Object.getOwnPropertyDescriptor(proto, 'value')!.set!.call(el, value);
    el.dispatchEvent(new Event('input', { bubbles: true }));
  });
}

function press(target: EventTarget, key: string, opts: KeyboardEventInit = {}): void {
  act(() => {
    target.dispatchEvent(new KeyboardEvent('keydown', { key, bubbles: true, cancelable: true, ...opts }));
  });
}

describe.skipIf(!ready)('App (needs @/core/index and @/core/reminders)', () => {
  it('runs the first-run flow on the demo vault', async () => {
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
    const { App } = await import('./App');
    const { bootApp } = await import('./services/boot');
    render(<App />);
    await act(async () => {
      await bootApp();
    });

    // Welcome screen.
    await waitFor(() => byText('h1', 'Witaj w Brulionie'), 'welcome screen');
    expect(byText('button', 'Zacznij pisać')).not.toBeNull();
    click(await waitFor(() => byText('button', 'Otwórz przykładowy brulion'), 'demo button'));

    // The shell with the demo vault.
    await waitFor(() => useAppStore.getState().status === 'ready', 'vault ready');
    const ws = useAppStore.getState().workspace!;
    await act(async () => {
      await ws.ready;
    });
    expect(ws.exists('Witaj w Brulionie.md')).toBe(true);
    expect(ws.index.journals()[0]?.date).toBe(ws.today());
    await waitFor(() => document.querySelector('#b-main'), 'main pane');
    expect(byText('.b-topbar__name', 'Dziennik')).not.toBeNull();
    expect(document.documentElement.lang).toBe('pl');

    // Quick switcher (Ctrl+O) → page.
    press(window, 'o', { ctrlKey: true });
    const switcher = await waitFor(() => document.querySelector<HTMLInputElement>('input[role=combobox]'), 'switcher');
    typeInto(switcher, 'remont');
    await waitFor(() => byText('[role=option]', 'Remont mieszkania'), 'switcher option');
    press(switcher, 'Enter');
    await waitFor(() => {
      const r = useAppStore.getState().route;
      return r.view === 'page' && r.path === 'Projekty/Remont mieszkania.md';
    }, 'page route');
    await waitFor(() => byText('.b-topbar__name', 'Remont mieszkania'), 'page title');
    expect(useAppStore.getState().settings.recent[ws.key]?.[0]).toBe('Projekty/Remont mieszkania.md');

    // Command palette (Ctrl+K) → settings.
    press(window, 'k', { ctrlKey: true });
    const palette = await waitFor(() => document.querySelector<HTMLInputElement>('input[aria-label="Paleta poleceń"]'), 'palette');
    typeInto(palette, 'ustawienia');
    await waitFor(() => byText('[role=option][aria-selected=true]', 'Ustawienia'), 'settings command');
    press(palette, 'Enter');
    await waitFor(() => useAppStore.getState().route.view === 'settings', 'settings route');
    await waitFor(() => byText('h1', 'Ustawienia'), 'settings view');
    const dark = await waitFor(() => byText('[role=radio]', 'Ciemny'), 'theme control');
    click(dark);
    expect(document.documentElement.dataset.theme).toBe('dark');
    expect(useAppStore.getState().settings.theme).toBe('dark');

    // Back.
    act(() => useAppStore.getState().goBack());
    await waitFor(() => useAppStore.getState().route.view === 'page', 'back to the page');

    // Quick capture into today's journal.
    act(() => useAppStore.getState().setCaptureOpen(true));
    const area = await waitFor(() => document.querySelector<HTMLTextAreaElement>('.b-capture textarea'), 'capture dialog');
    typeInto(area, 'Kupić mleko jutro o 18');
    await waitFor(() => byText('.b-capture', 'Przypomnienie: jutro'), 'reminder preview');
    press(area, 'Enter');
    await waitFor(() => !useAppStore.getState().captureOpen, 'capture closed');
    const journal = await ws.read(ws.journalPath(ws.today()));
    expect(journal).toMatch(/- \[ \] Kupić mleko ⏰ \d{4}-\d{2}-\d{2} 18:00\n$/);
    warn.mockRestore();
  });
});
