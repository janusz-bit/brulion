/**
 * NotificationService for the browser (PWA). Browsers cannot schedule notifications for a
 * closed page, so `firesWhenClosed` is false: `sync()` only keeps the list and the in-app
 * `ReminderScheduler` calls `showNow()` at the right time while Brulion is open.
 *
 * `showNow()` prefers the service worker (`registration.showNotification`, the only way on
 * Android Chrome and persistent in the notification centre) and falls back to
 * `new Notification()`. In-app listeners (`onInAppNotification`) are always invoked, so the
 * UI can show a toast and ring for alarms even when permission is denied.
 *
 * Taps: page notifications report through `onclick`; service-worker notifications need the
 * `notificationclick` handler from ./sw-notifications in the service worker, which posts
 * `{ type: 'brulion-notification-click', path, line }` to an open window or opens the app
 * with `?brulion-note=…&brulion-line=…` (read here on startup, then removed from the URL).
 */
import type { NotificationService, ScheduledNotification } from '@/core/types';
import { parseLocal } from '@/core/dates';
import {
  ActivationHub,
  DEFAULT_NOTIFICATION_TITLE,
  emitInAppNotification,
  type NotificationActivation,
} from './notifications-common';
import { activationFromUrl, NOTIFICATION_CLICK_MESSAGE, stripActivationParams } from './sw-notifications';

export { NOTIFICATION_CLICK_MESSAGE };

/** App icon used for notifications (relative to the document base URL). */
export const NOTIFICATION_ICON = 'icons/icon-192.png';

/** Vibration pattern for alarms (ms on/off). */
export const ALARM_VIBRATION = [500, 200, 500, 200, 500, 200, 800];

export type PermissionResult = 'granted' | 'denied' | 'prompt';

/** The parts of a page `Notification` instance we use. */
export interface NotificationLike {
  onclick: ((ev: Event) => unknown) | null;
  close(): void;
}

/** The parts of the `Notification` constructor we use. */
export interface NotificationCtorLike {
  new (title: string, options?: NotificationOptions): NotificationLike;
  readonly permission: NotificationPermission;
  requestPermission(callback?: (p: NotificationPermission) => void): Promise<NotificationPermission> | undefined | void;
}

/** The parts of a ServiceWorkerRegistration we use. */
export interface SwRegistrationLike {
  showNotification(title: string, options?: NotificationOptions): Promise<void>;
}

/** The parts of `navigator.serviceWorker` we use. */
export interface SwContainerLike {
  getRegistration(): Promise<SwRegistrationLike | undefined>;
  addEventListener(type: 'message', listener: (ev: MessageEvent) => void): void;
  removeEventListener(type: 'message', listener: (ev: MessageEvent) => void): void;
  startMessages?(): void;
}

/** Browser APIs used by the service (injectable for tests). */
export interface WebNotificationEnv {
  Notification?: NotificationCtorLike;
  serviceWorker?: SwContainerLike;
  /** Bring the app window to the front (on notification click). */
  focus?: () => void;
  /** Absolute icon URL. */
  iconUrl?: string;
  /** URL the app was opened with (may carry a notification activation). */
  launchUrl?: string;
  /** Replace the address bar URL without navigating (removes the activation parameters). */
  replaceUrl?: (url: string) => void;
}

export interface WebNotificationOptions {
  /** Try the service worker before `new Notification()` (default true). */
  preferServiceWorker?: boolean;
}

/** NotificationOptions plus fields missing from TypeScript's DOM lib. */
interface ExtendedNotificationOptions extends NotificationOptions {
  vibrate?: number[];
  renotify?: boolean;
  timestamp?: number;
}

/** Read the browser globals (only called at runtime, never at module load). */
export function browserNotificationEnv(): WebNotificationEnv {
  const g = globalThis as typeof globalThis & { Notification?: NotificationCtorLike };
  const env: WebNotificationEnv = {};
  if (typeof g.Notification === 'function') env.Notification = g.Notification;
  if (typeof navigator !== 'undefined' && navigator.serviceWorker) env.serviceWorker = navigator.serviceWorker;
  if (typeof window !== 'undefined') {
    env.focus = () => window.focus();
    env.launchUrl = window.location.href;
    env.replaceUrl = (url) => window.history.replaceState(window.history.state, '', url);
  }
  if (typeof document !== 'undefined') {
    try {
      env.iconUrl = new URL(NOTIFICATION_ICON, document.baseURI).href;
    } catch {
      env.iconUrl = NOTIFICATION_ICON;
    }
  }
  return env;
}

/** Map the Notification API's permission to the NotificationService one. */
export function mapPermission(p: NotificationPermission | string | undefined | null): PermissionResult {
  if (p === 'granted') return 'granted';
  if (p === 'denied') return 'denied';
  return 'prompt';
}

/** Options for a reminder notification (identical for the SW and the page constructor). */
export function buildNotificationOptions(item: ScheduledNotification, iconUrl?: string): ExtendedNotificationOptions {
  const options: ExtendedNotificationOptions = {
    body: item.body,
    data: { path: item.path, line: item.line, id: item.id },
    requireInteraction: item.alarm,
  };
  if (item.id) {
    options.tag = item.id;
    // Re-alert when a notification with the same tag (e.g. a snoozed reminder) is shown
    // again. Browsers throw a TypeError for `renotify` without a non-empty tag.
    options.renotify = true;
  }
  // No `badge`: Android shows it as a monochrome mask, a colour icon becomes a white square.
  if (iconUrl) options.icon = iconUrl;
  if (item.alarm) options.vibrate = [...ALARM_VIBRATION];
  const at = parseLocal(item.at).getTime();
  if (Number.isFinite(at)) options.timestamp = at;
  return options;
}

/** Browser NotificationService (see module docs). */
export class WebNotificationService implements NotificationService {
  readonly firesWhenClosed = false;
  private readonly env: WebNotificationEnv;
  private readonly preferServiceWorker: boolean;
  private readonly hub = new ActivationHub();
  private items: ScheduledNotification[] = [];
  private readonly onSwMessage = (ev: MessageEvent): void => {
    const data: unknown = ev.data;
    if (typeof data === 'object' && data !== null && (data as { type?: unknown }).type === NOTIFICATION_CLICK_MESSAGE) {
      this.hub.emit(data);
    }
  };

  constructor(env: WebNotificationEnv = browserNotificationEnv(), opts: WebNotificationOptions = {}) {
    this.env = env;
    this.preferServiceWorker = opts.preferServiceWorker ?? true;
    // Listen from the start: a click that opened/focused the app may be reported before
    // the UI subscribes; the hub keeps it until then.
    if (env.serviceWorker) {
      env.serviceWorker.addEventListener('message', this.onSwMessage);
      env.serviceWorker.startMessages?.();
    }
    this.readLaunchActivation();
  }

  async permission(): Promise<PermissionResult> {
    const ctor = this.env.Notification;
    return ctor ? mapPermission(ctor.permission) : 'denied';
  }

  async requestPermission(): Promise<PermissionResult> {
    const ctor = this.env.Notification;
    if (!ctor) return 'denied';
    if (ctor.permission !== 'default') return mapPermission(ctor.permission);
    try {
      const result = await new Promise<NotificationPermission>((resolve, reject) => {
        // Old Safari only supports the callback form and returns undefined.
        const ret = ctor.requestPermission(resolve);
        if (ret && typeof ret.then === 'function') ret.then(resolve, reject);
      });
      return mapPermission(result);
    } catch (e) {
      console.warn('[platform] notification permission request failed', e);
      return mapPermission(ctor.permission);
    }
  }

  /** Keep the list; timing is done by the in-app ReminderScheduler. */
  async sync(items: ScheduledNotification[]): Promise<void> {
    this.items = items.map((i) => ({ ...i }));
  }

  /** The last list passed to `sync()` (copies). */
  pending(): ScheduledNotification[] {
    return this.items.map((i) => ({ ...i }));
  }

  async showNow(item: ScheduledNotification): Promise<void> {
    let systemShown = false;
    if (this.env.Notification?.permission === 'granted') systemShown = await this.showSystem(item);
    emitInAppNotification(item, { systemShown });
  }

  onActivate(cb: (item: NotificationActivation) => void): () => void {
    return this.hub.subscribe(cb);
  }

  /** Simulate a notification tap (used by the UI for in-app toasts and by tests). */
  activate(item: NotificationActivation): void {
    this.hub.emit(item);
  }

  /** A tap that opened the app (URL parameters set by the service worker). */
  private readLaunchActivation(): void {
    const { launchUrl, replaceUrl } = this.env;
    if (!launchUrl) return;
    const activation = activationFromUrl(launchUrl);
    if (!activation) return;
    this.hub.emit(activation);
    try {
      replaceUrl?.(stripActivationParams(launchUrl));
    } catch (e) {
      console.warn('[platform] cannot clean the launch URL', e);
    }
  }

  /** Stop listening to service worker messages. */
  dispose(): void {
    this.env.serviceWorker?.removeEventListener('message', this.onSwMessage);
  }

  private async showSystem(item: ScheduledNotification): Promise<boolean> {
    const title = item.title.trim() || DEFAULT_NOTIFICATION_TITLE;
    const options = buildNotificationOptions(item, this.env.iconUrl);
    const strategies = this.preferServiceWorker
      ? [() => this.showViaServiceWorker(title, options), () => this.showViaPage(item, title, options)]
      : [() => this.showViaPage(item, title, options), () => this.showViaServiceWorker(title, options)];
    for (const show of strategies) {
      if (await show()) return true;
    }
    return false;
  }

  private async showViaServiceWorker(title: string, options: ExtendedNotificationOptions): Promise<boolean> {
    if (!this.env.serviceWorker) return false;
    try {
      const reg = await this.env.serviceWorker.getRegistration();
      if (!reg) return false;
      await reg.showNotification(title, options);
      return true;
    } catch (e) {
      // No active worker yet, or the browser refused.
      console.warn('[platform] service worker notification failed', e);
      return false;
    }
  }

  private async showViaPage(item: ScheduledNotification, title: string, options: ExtendedNotificationOptions): Promise<boolean> {
    const Ctor = this.env.Notification;
    if (!Ctor) return false;
    try {
      const n = new Ctor(title, options);
      n.onclick = () => {
        this.env.focus?.();
        n.close();
        this.hub.emit({ path: item.path, line: item.line });
      };
      return true;
    } catch (e) {
      // Android Chrome: "Illegal constructor. Use ServiceWorkerRegistration.showNotification()".
      console.warn('[platform] Notification constructor failed', e);
      return false;
    }
  }
}
