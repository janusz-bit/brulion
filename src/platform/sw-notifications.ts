/**
 * Notification taps in the PWA. Service-worker notifications report clicks to the service
 * worker, not to the page; {@link handleNotificationClick} is the `notificationclick`
 * handler a service worker should run:
 *
 * ```ts
 * self.addEventListener('notificationclick', (e) =>
 *   e.waitUntil(handleNotificationClick(e.notification, self.clients, self.registration.scope)));
 * ```
 *
 * An open Brulion window is focused and receives `{ type: 'brulion-notification-click',
 * path, line }` (handled by WebNotificationService). Without an open window a new one is
 * opened at the app URL with `?brulion-note=<path>&brulion-line=<n>`, which
 * WebNotificationService reads on startup ({@link activationFromUrl}).
 *
 * Plain functions over structural types: usable from a service worker bundle and testable.
 */
import { parseActivation, type NotificationActivation } from './notifications-common';

/** `type` of the message posted to the page when a notification is tapped. */
export const NOTIFICATION_CLICK_MESSAGE = 'brulion-notification-click';
/** URL parameter carrying the note path of a tapped notification. */
export const ACTIVATION_PATH_PARAM = 'brulion-note';
/** URL parameter carrying the line of a tapped notification. */
export const ACTIVATION_LINE_PARAM = 'brulion-line';

/** The parts of a service worker `Client` used here. */
export interface SwWindowClientLike {
  postMessage(message: unknown): void;
  focus?(): Promise<unknown>;
  readonly focused?: boolean;
  readonly visibilityState?: string;
  /** The window's URL; windows outside the app URL are other apps on the same origin. */
  readonly url?: string;
}

/** The parts of `self.clients` used here. */
export interface SwClientsLike {
  matchAll(options: { type: 'window'; includeUncontrolled: boolean }): Promise<readonly SwWindowClientLike[]>;
  openWindow(url: string): Promise<SwWindowClientLike | null>;
}

/** `base` with the activation parameters set (or removed when `activation` is null). */
export function activationUrl(base: string, activation: NotificationActivation | null): string {
  const url = new URL(base);
  url.searchParams.delete(ACTIVATION_PATH_PARAM);
  url.searchParams.delete(ACTIVATION_LINE_PARAM);
  if (activation) {
    url.searchParams.set(ACTIVATION_PATH_PARAM, activation.path);
    url.searchParams.set(ACTIVATION_LINE_PARAM, String(activation.line));
  }
  return url.href;
}

/** The activation encoded in a launch URL, or null. */
export function activationFromUrl(href: string): NotificationActivation | null {
  let url: URL;
  try {
    url = new URL(href);
  } catch {
    return null;
  }
  return parseActivation({ path: url.searchParams.get(ACTIVATION_PATH_PARAM), line: url.searchParams.get(ACTIVATION_LINE_PARAM) ?? 0 });
}

/** `href` without the activation parameters (to clean the address bar after reading them). */
export function stripActivationParams(href: string): string {
  try {
    return activationUrl(href, null);
  } catch {
    return href;
  }
}

/**
 * True when `client` shows this app: its URL starts with `appUrl` (the service worker scope,
 * prefix-matched like the browser does). `matchAll({ includeUncontrolled: true })` returns
 * every window of the origin, and one origin may host several apps (GitHub Pages:
 * `user.github.io/<project>/`). Clients without a URL are accepted.
 */
export function isAppClient(client: SwWindowClientLike, appUrl: string): boolean {
  if (typeof client.url !== 'string') return true;
  let prefix: string;
  try {
    const u = new URL(appUrl);
    prefix = `${u.origin}${u.pathname}`;
  } catch {
    return true;
  }
  return client.url.startsWith(prefix);
}

function pickClient(all: readonly SwWindowClientLike[], appUrl: string): SwWindowClientLike | undefined {
  const clients = all.filter((c) => isAppClient(c, appUrl));
  return clients.find((c) => c.focused) ?? clients.find((c) => c.visibilityState === 'visible') ?? clients[0];
}

/**
 * `notificationclick` handler: close the notification, then focus an open Brulion window (one
 * whose URL starts with `appUrl`) and post the activation to it, or open a new window whose
 * URL carries the activation.
 */
export async function handleNotificationClick(
  notification: { data?: unknown; close(): void },
  clients: SwClientsLike,
  appUrl: string,
): Promise<void> {
  notification.close();
  const activation = parseActivation(notification.data);
  const client = pickClient(await clients.matchAll({ type: 'window', includeUncontrolled: true }), appUrl);
  if (client) {
    try {
      await client.focus?.();
    } catch {
      // Focusing may be refused (no user activation); still deliver the message.
    }
    if (activation) client.postMessage({ type: NOTIFICATION_CLICK_MESSAGE, ...activation });
    return;
  }
  await clients.openWindow(activationUrl(appUrl, activation));
}
