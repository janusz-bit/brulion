/**
 * Pieces shared by the web and Tauri notification services: in-app notification listeners
 * (toast + sound in the UI, also when system notifications are denied) and the dispatch of
 * notification taps ("activations") to `onActivate` subscribers.
 */
import type { ScheduledNotification } from '@/core/types';

/** What a tapped notification points at. */
export type NotificationActivation = Pick<ScheduledNotification, 'path' | 'line'>;

/** Extra information passed to in-app notification listeners. */
export interface InAppNotificationInfo {
  /** Whether a system notification was shown as well (false: permission denied / API missing). */
  systemShown: boolean;
}

/** Listener for notifications fired while the app is open. */
export type InAppNotificationListener = (item: ScheduledNotification, info: InAppNotificationInfo) => void;

const inAppListeners = new Set<InAppNotificationListener>();

/**
 * Subscribe to every notification shown through `NotificationService.showNow` on any
 * platform. The UI uses it to show a toast and play a sound for alarms, so reminders work
 * even when system notifications are denied. Returns an unsubscribe function.
 */
export function onInAppNotification(cb: InAppNotificationListener): () => void {
  inAppListeners.add(cb);
  return () => {
    inAppListeners.delete(cb);
  };
}

/** Invoke all in-app listeners (errors in one listener do not affect the others). */
export function emitInAppNotification(item: ScheduledNotification, info: InAppNotificationInfo): void {
  for (const cb of Array.from(inAppListeners)) {
    try {
      cb(item, info);
    } catch (e) {
      console.error('[platform] in-app notification listener failed', e);
    }
  }
}

/** Number of registered in-app listeners (tests / diagnostics). */
export function inAppListenerCount(): number {
  return inAppListeners.size;
}

/**
 * Validate an activation payload coming from native code, a service worker message or a
 * notification's `data`/`extra`. Returns null for anything without a non-empty string path.
 * `line` accepts numbers and numeric strings (Android extras); invalid lines become 0.
 */
export function parseActivation(raw: unknown): NotificationActivation | null {
  if (typeof raw !== 'object' || raw === null) return null;
  const { path, line } = raw as { path?: unknown; line?: unknown };
  if (typeof path !== 'string' || path === '') return null;
  const n = typeof line === 'number' ? line : typeof line === 'string' && line.trim() !== '' ? Number(line) : 0;
  return { path, line: Number.isFinite(n) && n >= 0 ? Math.trunc(n) : 0 };
}

type ActivationListener = (item: NotificationActivation) => void;

/**
 * Fan-out of notification taps to `onActivate` subscribers. A tap that arrives while nobody
 * is subscribed (cold start: the native side reports it before the UI is ready) is kept and
 * delivered to the next subscriber.
 */
export class ActivationHub {
  private readonly listeners = new Set<ActivationListener>();
  private pending: NotificationActivation | null = null;

  /** Subscribe; a pending activation is delivered asynchronously (next microtask). */
  subscribe(cb: ActivationListener): () => void {
    this.listeners.add(cb);
    const pending = this.pending;
    if (pending) {
      this.pending = null;
      queueMicrotask(() => {
        if (this.listeners.has(cb)) this.call(cb, pending);
        else this.pending ??= pending;
      });
    }
    return () => {
      this.listeners.delete(cb);
    };
  }

  /** Deliver a (raw, validated here) activation to all subscribers, or keep it for later. */
  emit(raw: unknown): void {
    const item = parseActivation(raw);
    if (!item) return;
    if (this.listeners.size === 0) {
      this.pending = item;
      return;
    }
    for (const cb of Array.from(this.listeners)) this.call(cb, item);
  }

  /** Number of subscribers. */
  get size(): number {
    return this.listeners.size;
  }

  private call(cb: ActivationListener, item: NotificationActivation): void {
    try {
      cb({ ...item });
    } catch (e) {
      console.error('[platform] notification activation listener failed', e);
    }
  }
}

/** Fallback title when a reminder has none. */
export const DEFAULT_NOTIFICATION_TITLE = 'Brulion';
