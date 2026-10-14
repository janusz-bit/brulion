import { describe, expect, it, vi } from 'vitest';
import {
  activationFromUrl,
  activationUrl,
  handleNotificationClick,
  NOTIFICATION_CLICK_MESSAGE,
  stripActivationParams,
  type SwClientsLike,
  type SwWindowClientLike,
} from './sw-notifications';

const APP = 'https://janusz-bit.github.io/brulion/';

function client(extra: Partial<SwWindowClientLike> = {}) {
  return { postMessage: vi.fn(), focus: vi.fn(async () => undefined), ...extra };
}

function clients(open: SwWindowClientLike[]) {
  return {
    matchAll: vi.fn(async () => open),
    openWindow: vi.fn(async (_url: string) => null),
  } satisfies SwClientsLike;
}

describe('activation URLs', () => {
  it('round-trips Polish paths', () => {
    const url = activationUrl(APP, { path: 'Książki/Pan Tadeusz.md', line: 12 });
    expect(url).toContain('brulion-note=');
    expect(activationFromUrl(url)).toEqual({ path: 'Książki/Pan Tadeusz.md', line: 12 });
  });

  it('replaces existing parameters and strips them', () => {
    const url = activationUrl(`${APP}?brulion-note=old.md&brulion-line=1&keep=1`, { path: 'new.md', line: 0 });
    expect(activationFromUrl(url)).toEqual({ path: 'new.md', line: 0 });
    expect(new URL(url).searchParams.getAll('brulion-note')).toEqual(['new.md']);
    expect(stripActivationParams(url)).toBe(`${APP}?keep=1`);
    expect(activationUrl(APP, null)).toBe(APP);
  });

  it('handles missing or malformed input', () => {
    expect(activationFromUrl(APP)).toBeNull();
    expect(activationFromUrl(`${APP}?brulion-note=`)).toBeNull();
    expect(activationFromUrl(`${APP}?brulion-note=a.md&brulion-line=abc`)).toEqual({ path: 'a.md', line: 0 });
    expect(activationFromUrl('not a url')).toBeNull();
    expect(stripActivationParams('not a url')).toBe('not a url');
  });
});

describe('handleNotificationClick', () => {
  const notification = (data: unknown) => ({ data, close: vi.fn() });

  it('focuses an open window and posts the activation', async () => {
    const background = client({ visibilityState: 'hidden' });
    const visible = client({ visibilityState: 'visible' });
    const c = clients([background, visible]);
    const n = notification({ path: 'Dziennik/2026-10-14.md', line: 3, id: 'x' });
    await handleNotificationClick(n, c, APP);
    expect(n.close).toHaveBeenCalled();
    expect(c.matchAll).toHaveBeenCalledWith({ type: 'window', includeUncontrolled: true });
    expect(visible.focus).toHaveBeenCalled();
    expect(visible.postMessage).toHaveBeenCalledWith({ type: NOTIFICATION_CLICK_MESSAGE, path: 'Dziennik/2026-10-14.md', line: 3 });
    expect(background.postMessage).not.toHaveBeenCalled();
    expect(c.openWindow).not.toHaveBeenCalled();
  });

  it('prefers the focused window and tolerates focus errors', async () => {
    const focused = client({
      focused: true,
      focus: vi.fn(async () => {
        throw new Error('InvalidAccessError');
      }),
    });
    await handleNotificationClick(notification({ path: 'a.md', line: 1 }), clients([client(), focused]), APP);
    expect(focused.postMessage).toHaveBeenCalled();
  });

  it('opens the app with the activation in the URL when no window is open', async () => {
    const c = clients([]);
    await handleNotificationClick(notification({ path: 'Zakupy/Żywność.md', line: 4 }), c, APP);
    const url = c.openWindow.mock.calls[0]![0];
    expect(activationFromUrl(url)).toEqual({ path: 'Zakupy/Żywność.md', line: 4 });
  });

  it('just focuses / opens the app for notifications without a note', async () => {
    const open = client();
    await handleNotificationClick(notification(undefined), clients([open]), APP);
    expect(open.focus).toHaveBeenCalled();
    expect(open.postMessage).not.toHaveBeenCalled();
    const c = clients([]);
    await handleNotificationClick(notification(null), c, APP);
    expect(c.openWindow).toHaveBeenCalledWith(APP);
  });
});

describe('handleNotificationClick and other apps on the same origin (review fix)', () => {
  const notification = (data: unknown) => ({ data, close: vi.fn() });

  it('ignores windows outside the app scope (GitHub Pages hosts many projects per origin)', async () => {
    const otherApp = client({ focused: true, url: 'https://janusz-bit.github.io/inny-projekt/' });
    const brulion = client({ visibilityState: 'hidden', url: `${APP}?brulion-note=x.md` });
    await handleNotificationClick(notification({ path: 'Łąka.md', line: 2 }), clients([otherApp, brulion]), APP);
    expect(otherApp.postMessage).not.toHaveBeenCalled();
    expect(otherApp.focus).not.toHaveBeenCalled();
    expect(brulion.focus).toHaveBeenCalled();
    expect(brulion.postMessage).toHaveBeenCalledWith({ type: NOTIFICATION_CLICK_MESSAGE, path: 'Łąka.md', line: 2 });
  });

  it('opens Brulion when only other apps of the origin are open', async () => {
    const otherApp = client({ focused: true, url: 'https://janusz-bit.github.io/' });
    const c = clients([otherApp]);
    await handleNotificationClick(notification({ path: 'a.md', line: 1 }), c, APP);
    expect(otherApp.postMessage).not.toHaveBeenCalled();
    expect(activationFromUrl(c.openWindow.mock.calls[0]![0])).toEqual({ path: 'a.md', line: 1 });
  });
});
