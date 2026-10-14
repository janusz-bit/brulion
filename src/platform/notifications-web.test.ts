// @vitest-environment jsdom
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { ScheduledNotification } from '@/core/types';
import { onInAppNotification, type InAppNotificationListener } from './notifications-common';
import {
  ALARM_VIBRATION,
  browserNotificationEnv,
  buildNotificationOptions,
  mapPermission,
  NOTIFICATION_CLICK_MESSAGE,
  WebNotificationService,
  type NotificationCtorLike,
  type NotificationLike,
  type SwContainerLike,
  type WebNotificationEnv,
} from './notifications-web';

const reminder: ScheduledNotification = {
  id: 'Projekty/Łódź.md:4:2026-10-14T09:00',
  nativeId: 987654,
  title: 'Spotkanie w Łodzi',
  body: 'Zabrać dokumenty — ważne!',
  at: '2026-10-14T09:00',
  alarm: false,
  path: 'Projekty/Łódź.md',
  line: 4,
};
const alarm: ScheduledNotification = { ...reminder, id: 'alarm-1', title: 'Pobudka', alarm: true, at: '2026-10-15T06:30' };

interface FakeNotificationClass extends NotificationCtorLike {
  instances: (NotificationLike & { title: string; options?: NotificationOptions; closed: boolean })[];
  permission: NotificationPermission;
  nextPermission: NotificationPermission;
  throwOnConstruct: boolean;
}

function fakeNotificationClass(permission: NotificationPermission = 'granted'): FakeNotificationClass {
  class FakeNotification implements NotificationLike {
    static instances: FakeNotificationClass['instances'] = [];
    static permission: NotificationPermission = permission;
    static nextPermission: NotificationPermission = 'granted';
    static throwOnConstruct = false;
    static requestPermission = vi.fn(async () => {
      FakeNotification.permission = FakeNotification.nextPermission;
      return FakeNotification.permission;
    });
    onclick: ((ev: Event) => unknown) | null = null;
    closed = false;
    constructor(
      readonly title: string,
      readonly options?: NotificationOptions,
    ) {
      if (FakeNotification.throwOnConstruct) throw new TypeError('Illegal constructor');
      FakeNotification.instances.push(this);
    }
    close(): void {
      this.closed = true;
    }
  }
  return FakeNotification as unknown as FakeNotificationClass;
}

function fakeServiceWorker(withRegistration = true) {
  const listeners = new Set<(ev: MessageEvent) => void>();
  const showNotification = vi.fn(async (_title: string, _options?: NotificationOptions) => {});
  const container: SwContainerLike & { post(data: unknown): void; listeners: typeof listeners } = {
    listeners,
    getRegistration: vi.fn(async () => (withRegistration ? { showNotification } : undefined)),
    addEventListener: (_type, l) => listeners.add(l),
    removeEventListener: (_type, l) => listeners.delete(l),
    startMessages: vi.fn(),
    post(data: unknown) {
      for (const l of listeners) l(new MessageEvent('message', { data }));
    },
  };
  return { container, showNotification };
}

describe('mapPermission', () => {
  it('maps the Notification API states', () => {
    expect(mapPermission('granted')).toBe('granted');
    expect(mapPermission('denied')).toBe('denied');
    expect(mapPermission('default')).toBe('prompt');
    expect(mapPermission(undefined)).toBe('prompt');
  });
});

describe('buildNotificationOptions', () => {
  it('builds reminder options with tag, data and timestamp', () => {
    const o = buildNotificationOptions(reminder, 'https://app/icons/icon-192.png');
    expect(o).toMatchObject({
      body: 'Zabrać dokumenty — ważne!',
      tag: reminder.id,
      data: { path: 'Projekty/Łódź.md', line: 4, id: reminder.id },
      requireInteraction: false,
      icon: 'https://app/icons/icon-192.png',
      renotify: true,
    });
    expect(o.vibrate).toBeUndefined();
    expect(o.timestamp).toBe(new Date(2026, 9, 14, 9, 0).getTime());
  });

  it('makes alarms persistent and vibrating', () => {
    const o = buildNotificationOptions(alarm);
    expect(o.requireInteraction).toBe(true);
    expect(o.vibrate).toEqual(ALARM_VIBRATION);
    expect(o.icon).toBeUndefined();
  });

  it('skips the timestamp for invalid dates', () => {
    expect(buildNotificationOptions({ ...reminder, at: 'jutro' }).timestamp).toBeUndefined();
  });
});

describe('browserNotificationEnv', () => {
  it('reads globals lazily and resolves the icon against the document', () => {
    const env = browserNotificationEnv();
    expect(env.iconUrl).toMatch(/icons\/icon-192\.png$/);
    expect(env.iconUrl!.startsWith('http')).toBe(true);
    expect(typeof env.focus).toBe('function');
  });
});

describe('WebNotificationService', () => {
  let inApp: ReturnType<typeof vi.fn<InAppNotificationListener>>;
  let offInApp: () => void;
  let warn: ReturnType<typeof vi.spyOn>;

  beforeEach(() => {
    inApp = vi.fn<InAppNotificationListener>();
    offInApp = onInAppNotification(inApp);
    warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
  });
  afterEach(() => {
    offInApp();
    warn.mockRestore();
  });

  it('cannot fire while closed', () => {
    expect(new WebNotificationService({}).firesWhenClosed).toBe(false);
  });

  describe('permission', () => {
    it('reports denied without the Notification API', async () => {
      const svc = new WebNotificationService({});
      expect(await svc.permission()).toBe('denied');
      expect(await svc.requestPermission()).toBe('denied');
    });

    it('maps default to prompt and requests it', async () => {
      const N = fakeNotificationClass('default');
      const svc = new WebNotificationService({ Notification: N });
      expect(await svc.permission()).toBe('prompt');
      N.nextPermission = 'granted';
      expect(await svc.requestPermission()).toBe('granted');
      expect(N.requestPermission).toHaveBeenCalledTimes(1);
      expect(await svc.permission()).toBe('granted');
    });

    it('does not prompt again once decided', async () => {
      const N = fakeNotificationClass('denied');
      const svc = new WebNotificationService({ Notification: N });
      expect(await svc.requestPermission()).toBe('denied');
      expect(N.requestPermission).not.toHaveBeenCalled();
    });

    it('supports the old callback-only requestPermission', async () => {
      const N = fakeNotificationClass('default');
      (N as { requestPermission: NotificationCtorLike['requestPermission'] }).requestPermission = (cb) => {
        cb?.('denied');
        return undefined;
      };
      expect(await new WebNotificationService({ Notification: N }).requestPermission()).toBe('denied');
    });

    it('survives a throwing requestPermission', async () => {
      const N = fakeNotificationClass('default');
      (N as { requestPermission: NotificationCtorLike['requestPermission'] }).requestPermission = () => Promise.reject(new Error('blocked'));
      expect(await new WebNotificationService({ Notification: N }).requestPermission()).toBe('prompt');
    });
  });

  it('sync keeps a copy of the list', async () => {
    const svc = new WebNotificationService({});
    const items = [reminder, alarm];
    await svc.sync(items);
    items.pop();
    expect(svc.pending()).toEqual([reminder, alarm]);
    await svc.sync([]);
    expect(svc.pending()).toEqual([]);
  });

  describe('showNow', () => {
    it('prefers the service worker registration', async () => {
      const N = fakeNotificationClass('granted');
      const { container, showNotification } = fakeServiceWorker();
      const svc = new WebNotificationService({ Notification: N, serviceWorker: container, iconUrl: 'https://app/icon.png' });
      await svc.showNow(alarm);
      expect(showNotification).toHaveBeenCalledWith(
        'Pobudka',
        expect.objectContaining({
          body: alarm.body,
          tag: 'alarm-1',
          data: { path: alarm.path, line: alarm.line, id: 'alarm-1' },
          requireInteraction: true,
          vibrate: ALARM_VIBRATION,
          icon: 'https://app/icon.png',
        }),
      );
      expect(N.instances).toHaveLength(0);
      expect(inApp).toHaveBeenCalledWith(alarm, { systemShown: true });
    });

    it('falls back to new Notification without a registration', async () => {
      const N = fakeNotificationClass('granted');
      const { container } = fakeServiceWorker(false);
      const svc = new WebNotificationService({ Notification: N, serviceWorker: container });
      await svc.showNow(reminder);
      expect(N.instances).toHaveLength(1);
      expect(N.instances[0]!.title).toBe('Spotkanie w Łodzi');
      expect(N.instances[0]!.options).toMatchObject({ tag: reminder.id, requireInteraction: false });
      expect(inApp).toHaveBeenCalledWith(reminder, { systemShown: true });
    });

    it('falls back when showNotification rejects', async () => {
      const N = fakeNotificationClass('granted');
      const { container, showNotification } = fakeServiceWorker();
      showNotification.mockRejectedValueOnce(new TypeError('No active registration'));
      await new WebNotificationService({ Notification: N, serviceWorker: container }).showNow(reminder);
      expect(N.instances).toHaveLength(1);
    });

    it('can prefer the page API and still fall back to the service worker', async () => {
      const N = fakeNotificationClass('granted');
      N.throwOnConstruct = true;
      const { container, showNotification } = fakeServiceWorker();
      await new WebNotificationService({ Notification: N, serviceWorker: container }, { preferServiceWorker: false }).showNow(reminder);
      expect(showNotification).toHaveBeenCalledTimes(1);
      expect(inApp).toHaveBeenCalledWith(reminder, { systemShown: true });
    });

    it('still notifies in-app when the constructor throws (Android Chrome)', async () => {
      const N = fakeNotificationClass('granted');
      N.throwOnConstruct = true;
      await new WebNotificationService({ Notification: N }).showNow(reminder);
      expect(inApp).toHaveBeenCalledWith(reminder, { systemShown: false });
    });

    it('shows nothing system-wide without permission, but always in-app', async () => {
      const N = fakeNotificationClass('denied');
      const { container, showNotification } = fakeServiceWorker();
      await new WebNotificationService({ Notification: N, serviceWorker: container }).showNow(alarm);
      expect(showNotification).not.toHaveBeenCalled();
      expect(N.instances).toHaveLength(0);
      expect(inApp).toHaveBeenCalledWith(alarm, { systemShown: false });
    });

    it('works without any notification API', async () => {
      await new WebNotificationService({}).showNow(reminder);
      expect(inApp).toHaveBeenCalledWith(reminder, { systemShown: false });
    });

    it('uses a default title for empty titles', async () => {
      const N = fakeNotificationClass('granted');
      await new WebNotificationService({ Notification: N }).showNow({ ...reminder, title: '   ' });
      expect(N.instances[0]!.title).toBe('Brulion');
    });
  });

  describe('onActivate', () => {
    it('reports clicks on page notifications and focuses the window', async () => {
      const N = fakeNotificationClass('granted');
      const focus = vi.fn();
      const svc = new WebNotificationService({ Notification: N, focus });
      const cb = vi.fn();
      svc.onActivate(cb);
      await svc.showNow(reminder);
      N.instances[0]!.onclick?.(new Event('click'));
      expect(cb).toHaveBeenCalledWith({ path: 'Projekty/Łódź.md', line: 4 });
      expect(focus).toHaveBeenCalled();
      expect(N.instances[0]!.closed).toBe(true);
    });

    it('reports service worker click messages and ignores others', () => {
      const { container } = fakeServiceWorker();
      const svc = new WebNotificationService({ serviceWorker: container });
      expect(container.startMessages).toHaveBeenCalled();
      const cb = vi.fn();
      const off = svc.onActivate(cb);
      container.post({ type: NOTIFICATION_CLICK_MESSAGE, path: 'Dziennik/2026-10-14.md', line: 7 });
      container.post({ type: 'other', path: 'x.md', line: 1 });
      container.post({ type: NOTIFICATION_CLICK_MESSAGE, line: 1 });
      container.post('string');
      container.post(null);
      expect(cb).toHaveBeenCalledExactlyOnceWith({ path: 'Dziennik/2026-10-14.md', line: 7 });
      off();
      container.post({ type: NOTIFICATION_CLICK_MESSAGE, path: 'a.md', line: 0 });
      expect(cb).toHaveBeenCalledTimes(1);
    });

    it('delivers a click that arrived before subscribing', async () => {
      const { container } = fakeServiceWorker();
      const svc = new WebNotificationService({ serviceWorker: container });
      container.post({ type: NOTIFICATION_CLICK_MESSAGE, path: 'Zakupy.md', line: 2 });
      const cb = vi.fn();
      svc.onActivate(cb);
      await Promise.resolve();
      expect(cb).toHaveBeenCalledWith({ path: 'Zakupy.md', line: 2 });
    });

    it('activate() simulates a tap; dispose() stops listening', () => {
      const { container } = fakeServiceWorker();
      const svc = new WebNotificationService({ serviceWorker: container });
      const cb = vi.fn();
      svc.onActivate(cb);
      svc.activate({ path: 'a.md', line: 1 });
      expect(cb).toHaveBeenCalledWith({ path: 'a.md', line: 1 });
      svc.dispose();
      expect(container.listeners.size).toBe(0);
    });
  });

  describe('launch URL activation (app opened by a notification tap)', () => {
    it('delivers the activation and cleans the URL', async () => {
      const replaceUrl = vi.fn();
      const svc = new WebNotificationService({
        launchUrl: 'https://app.example/brulion/?brulion-note=Dziennik%2F2026-10-14.md&brulion-line=6&x=1',
        replaceUrl,
      });
      expect(replaceUrl).toHaveBeenCalledWith('https://app.example/brulion/?x=1');
      const cb = vi.fn();
      svc.onActivate(cb);
      await Promise.resolve();
      expect(cb).toHaveBeenCalledWith({ path: 'Dziennik/2026-10-14.md', line: 6 });
    });

    it('ignores ordinary launch URLs', async () => {
      const replaceUrl = vi.fn();
      const svc = new WebNotificationService({ launchUrl: 'https://app.example/?share-target&text=x', replaceUrl });
      const cb = vi.fn();
      svc.onActivate(cb);
      await Promise.resolve();
      expect(cb).not.toHaveBeenCalled();
      expect(replaceUrl).not.toHaveBeenCalled();
    });

    it('survives a failing replaceState', () => {
      expect(
        () =>
          new WebNotificationService({
            launchUrl: 'https://app.example/?brulion-note=a.md',
            replaceUrl: () => {
              throw new Error('SecurityError');
            },
          }),
      ).not.toThrow();
    });

    it('reads window.location by default', async () => {
      window.history.replaceState(null, '', '/?brulion-note=%C5%81%C3%B3d%C5%BA.md&brulion-line=2');
      const svc = new WebNotificationService(browserNotificationEnv());
      expect(window.location.search).toBe('');
      const cb = vi.fn();
      svc.onActivate(cb);
      await Promise.resolve();
      expect(cb).toHaveBeenCalledWith({ path: 'Łódź.md', line: 2 });
    });
  });

  it('works with the jsdom globals (no Notification API)', async () => {
    const env: WebNotificationEnv = browserNotificationEnv();
    const svc = new WebNotificationService(env);
    expect(await svc.permission()).toBe(env.Notification ? 'prompt' : 'denied');
    await svc.showNow(reminder);
    expect(inApp).toHaveBeenCalled();
  });
});

describe('notification options without an id (review fix)', () => {
  it('sets renotify only together with a non-empty tag', () => {
    const o = buildNotificationOptions({ ...reminder, id: '' });
    expect(o.tag).toBeUndefined();
    expect(o.renotify).toBeUndefined();
    expect(buildNotificationOptions(reminder)).toMatchObject({ tag: reminder.id, renotify: true });
  });

  it('shows a system notification for an item without an id (Chrome rejects renotify without tag)', async () => {
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
    const showNotification = vi.fn(async (_title: string, options?: NotificationOptions & { renotify?: boolean }) => {
      if (options?.renotify && !options.tag) throw new TypeError('Notifications which set the renotify flag must specify a non-empty tag.');
    });
    const container: SwContainerLike = {
      getRegistration: async () => ({ showNotification }),
      addEventListener: () => {},
      removeEventListener: () => {},
    };
    const N = fakeNotificationClass('granted');
    N.throwOnConstruct = true;
    const inApp = vi.fn<InAppNotificationListener>();
    const off = onInAppNotification(inApp);
    try {
      await new WebNotificationService({ Notification: N, serviceWorker: container }).showNow({ ...reminder, id: '' });
      expect(showNotification).toHaveBeenCalledTimes(1);
      expect(inApp).toHaveBeenCalledWith(expect.objectContaining({ title: 'Spotkanie w Łodzi' }), { systemShown: true });
    } finally {
      off();
      warn.mockRestore();
    }
  });
});
