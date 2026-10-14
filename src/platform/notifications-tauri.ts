/**
 * NotificationServices for the Tauri shell.
 *
 * **Desktop** ({@link TauriDesktopNotificationService}): the app lives in the tray, so the
 * in-app `ReminderScheduler` keeps the timers (`firesWhenClosed = false`, `sync()` only
 * stores the list) and `showNow()` shows a system notification through
 * `@tauri-apps/plugin-notification`.
 *
 * **Android** ({@link AndroidNotificationService}): reminders are handed to Brulion's native
 * plugin (`plugin:brulion-android|schedule_reminders`, docs/ARCHITECTURE.md), which arms
 * exact alarms, survives reboots and shows the alarm screen — so they fire while the app is
 * closed (`firesWhenClosed = true`). The native side owns the `reminders` / `alarms`
 * notification channels; `showNow()` posts to them through the official plugin. Taps
 * arrive through `take_activation` (cold start) and the plugin event
 * `notification-activated`.
 *
 * Both services also invoke in-app listeners from `showNow()` (see `onInAppNotification`).
 */
import type { NotificationService, ScheduledNotification } from '@/core/types';
import { parseLocal } from '@/core/dates';
import { errorMessage } from './errors';
import {
  ActivationHub,
  DEFAULT_NOTIFICATION_TITLE,
  emitInAppNotification,
  type NotificationActivation,
} from './notifications-common';
import { androidInvoke, ANDROID_PLUGIN, loadNotificationPlugin, loadTauriCore } from './tauri-ipc';

type PermissionResult = 'granted' | 'denied' | 'prompt';

/** Android notification channel ids (created by the native plugin). */
export const ANDROID_CHANNELS = { reminders: 'reminders', alarms: 'alarms' } as const;

/** Most reminders handed to the native scheduler at once (the plugin keeps the soonest 400). */
export const MAX_ANDROID_REMINDERS = 400;

/** Freedesktop sound name used for alarms on Linux. */
export const DESKTOP_ALARM_SOUND = 'alarm-clock-elapsed';

/** `AndroidReminder` of the IPC contract. */
export interface AndroidReminder {
  id: number;
  title: string;
  body: string;
  /** Epoch ms. */
  at: number;
  alarm: boolean;
  path: string;
  line: number;
  /** Alarms: minutes to ring before turning into a missed-alarm notification (native default 10, 1–60). */
  ringMinutes?: number;
}

/** Result of `schedule_reminders`. */
export interface AndroidScheduleResult {
  scheduled: number;
  exact: boolean;
}

/**
 * Integer notification id for native APIs: int32 as is, uint32 wrapped into int32 (like the
 * Rust side), anything else null.
 */
export function toInt32Id(n: number): number | null {
  if (!Number.isInteger(n) || n < -0x80000000 || n > 0xffffffff) return null;
  return n | 0;
}

/** Clamp an alarm ring time to the native range (1–60 minutes); undefined when not a number. */
export function clampRingMinutes(n: number | undefined): number | undefined {
  if (n === undefined || !Number.isFinite(n)) return undefined;
  return Math.min(60, Math.max(1, Math.round(n)));
}

/**
 * Convert a ScheduledNotification (local wall-clock `at`) for the native scheduler.
 * `ringMinutes` is attached to alarms only.
 */
export function toAndroidReminder(item: ScheduledNotification, ringMinutes?: number): AndroidReminder | null {
  const id = toInt32Id(item.nativeId);
  const at = parseLocal(item.at).getTime();
  if (id === null || !Number.isFinite(at)) return null;
  const line = Number.isFinite(item.line) && item.line >= 0 ? Math.trunc(item.line) : 0;
  const reminder: AndroidReminder = {
    id,
    title: item.title.trim() || DEFAULT_NOTIFICATION_TITLE,
    body: item.body,
    at,
    alarm: !!item.alarm,
    path: item.path,
    line,
  };
  const ring = clampRingMinutes(ringMinutes);
  if (reminder.alarm && ring !== undefined) reminder.ringMinutes = ring;
  return reminder;
}

/**
 * The set sent to `schedule_reminders`: valid future reminders, the last copy of a repeated
 * id, sorted by time (then id), at most {@link MAX_ANDROID_REMINDERS}.
 */
export function androidReminderSet(items: readonly ScheduledNotification[], nowMs: number, ringMinutes?: number): AndroidReminder[] {
  const byId = new Map<number, AndroidReminder>();
  for (const item of items) {
    const r = toAndroidReminder(item, ringMinutes);
    if (!r) continue;
    byId.delete(r.id);
    byId.set(r.id, r);
  }
  return [...byId.values()]
    .filter((r) => r.at > nowMs)
    .sort((a, b) => a.at - b.at || a.id - b.id)
    .slice(0, MAX_ANDROID_REMINDERS);
}

function grantedOf(v: unknown): boolean {
  return typeof v === 'object' && v !== null && (v as { granted?: unknown }).granted === true;
}

/** Map `Notification.requestPermission`-style answers. */
function mapNotificationPermission(p: unknown): PermissionResult {
  if (p === 'granted') return 'granted';
  if (p === 'denied') return 'denied';
  return 'prompt';
}

/** Extract `{ path, line }` from a plugin-notification `actionPerformed` event. */
export function activationFromAction(event: unknown): unknown {
  if (typeof event !== 'object' || event === null) return null;
  const notification = (event as { notification?: unknown }).notification;
  if (typeof notification !== 'object' || notification === null) return null;
  return (notification as { extra?: unknown }).extra ?? null;
}

/** Subscribe to taps on notifications shown with plugin-notification (Android, iOS). */
async function listenToPluginActions(hub: ActivationHub): Promise<void> {
  try {
    const { onAction } = await loadNotificationPlugin();
    await onAction((event) => hub.emit(activationFromAction(event)));
  } catch (e) {
    // Desktop builds of the plugin have no action listener.
    console.debug('[platform] notification actions unavailable', errorMessage(e));
  }
}

/** Desktop NotificationService (see module docs). */
export class TauriDesktopNotificationService implements NotificationService {
  readonly firesWhenClosed = false;
  private readonly hub = new ActivationHub();
  private items: ScheduledNotification[] = [];
  private listening = false;

  async permission(): Promise<PermissionResult> {
    try {
      const { isPermissionGranted } = await loadNotificationPlugin();
      if (await isPermissionGranted()) return 'granted';
    } catch (e) {
      console.warn('[platform] notification permission check failed', e);
      return 'denied';
    }
    const g = globalThis as { Notification?: { permission?: string } };
    return g.Notification?.permission === 'denied' ? 'denied' : 'prompt';
  }

  async requestPermission(): Promise<PermissionResult> {
    try {
      const { requestPermission } = await loadNotificationPlugin();
      return mapNotificationPermission(await requestPermission());
    } catch (e) {
      console.warn('[platform] notification permission request failed', e);
      return 'denied';
    }
  }

  async sync(items: ScheduledNotification[]): Promise<void> {
    this.items = items.map((i) => ({ ...i }));
  }

  /** The last list passed to `sync()` (copies). */
  pending(): ScheduledNotification[] {
    return this.items.map((i) => ({ ...i }));
  }

  async showNow(item: ScheduledNotification): Promise<void> {
    let systemShown = false;
    try {
      const plugin = await loadNotificationPlugin();
      if (await plugin.isPermissionGranted()) {
        const id = toInt32Id(item.nativeId);
        plugin.sendNotification({
          ...(id !== null ? { id } : {}),
          title: item.title.trim() || DEFAULT_NOTIFICATION_TITLE,
          body: item.body,
          extra: { path: item.path, line: item.line },
          ...(item.alarm ? { sound: DESKTOP_ALARM_SOUND } : {}),
        });
        systemShown = true;
      }
    } catch (e) {
      console.warn('[platform] system notification failed', e);
    }
    emitInAppNotification(item, { systemShown });
  }

  onActivate(cb: (item: NotificationActivation) => void): () => void {
    if (!this.listening) {
      this.listening = true;
      void listenToPluginActions(this.hub);
    }
    return this.hub.subscribe(cb);
  }

  /** Report a tap (e.g. on an in-app toast) to `onActivate` subscribers. */
  activate(item: NotificationActivation): void {
    this.hub.emit(item);
  }
}

/** Android NotificationService backed by the `brulion-android` plugin (see module docs). */
export class AndroidNotificationService implements NotificationService {
  readonly firesWhenClosed = true;
  private readonly hub = new ActivationHub();
  private readonly now: () => number;
  private listening: Promise<void> | null = null;
  private lastSignature: string | null = null;
  private lastResult: AndroidScheduleResult | null = null;
  private ring: number | undefined;
  /** Tail of the running / queued `sync()` calls. */
  private syncQueue: Promise<void> = Promise.resolve();

  constructor(opts: { now?: () => number; ringMinutes?: number } = {}) {
    this.now = opts.now ?? Date.now;
    this.ring = clampRingMinutes(opts.ringMinutes);
  }

  /** How long alarms ring before becoming a missed-alarm notification (undefined: native default, 10 min). */
  get ringMinutes(): number | undefined {
    return this.ring;
  }

  /** Change the alarm ring time (1–60 min); applied by the next `sync()`. */
  set ringMinutes(n: number | undefined) {
    this.ring = clampRingMinutes(n);
  }

  /** `granted` or `prompt` (a request may still show the system dialog or open settings). */
  async permission(): Promise<PermissionResult> {
    try {
      return grantedOf(await androidInvoke('notification_permission')) ? 'granted' : 'prompt';
    } catch (e) {
      console.warn('[platform] notification permission check failed', e);
      return 'denied';
    }
  }

  /** Android 13+: system dialog; when the system no longer asks, the plugin opens settings. */
  async requestPermission(): Promise<PermissionResult> {
    try {
      return grantedOf(await androidInvoke('request_notification_permission')) ? 'granted' : 'denied';
    } catch (e) {
      console.warn('[platform] notification permission request failed', e);
      return 'denied';
    }
  }

  /**
   * Replace the native set. Identical consecutive sets are not re-sent. Calls run one at a
   * time in call order (the native side replaces the whole set, so the last call must also
   * be the last to arrive). Rejects when the native call fails (the next sync retries).
   */
  sync(items: ScheduledNotification[]): Promise<void> {
    const copy = items.slice();
    const run = this.syncQueue.then(() => this.syncNow(copy));
    // Keep the queue alive after a failure; the caller still sees the rejection.
    this.syncQueue = run.catch(() => undefined);
    return run;
  }

  private async syncNow(items: ScheduledNotification[]): Promise<void> {
    const set = androidReminderSet(items, this.now(), this.ring);
    const signature = JSON.stringify(set);
    if (signature === this.lastSignature) return;
    const result = await androidInvoke<unknown>('schedule_reminders', { items: set });
    this.lastSignature = signature;
    const r = (typeof result === 'object' && result !== null ? result : {}) as Partial<AndroidScheduleResult>;
    this.lastResult = { scheduled: Number(r.scheduled) || 0, exact: r.exact === true };
  }

  /** Answer of the last successful `schedule_reminders` (null before the first sync). */
  get scheduleResult(): AndroidScheduleResult | null {
    return this.lastResult ? { ...this.lastResult } : null;
  }

  /** Whether exact alarms are allowed (Android 12: "Alarms & reminders" special access). */
  async canScheduleExact(): Promise<boolean> {
    try {
      return grantedOf(await androidInvoke('can_schedule_exact'));
    } catch {
      return false;
    }
  }

  /** Open the exact-alarm settings (Android 12+; no-op when granted). */
  async requestExactAlarms(): Promise<void> {
    await androidInvoke<null>('request_exact_alarms');
    // A newly granted permission changes how alarms are armed: re-send on the next sync.
    this.lastSignature = null;
  }

  /** Whether alarms may open the alarm screen over the lock screen (Android 14+ special access). */
  async canUseFullScreenIntent(): Promise<boolean> {
    try {
      return grantedOf(await androidInvoke('can_use_full_screen_intent'));
    } catch {
      return false;
    }
  }

  /** Open the "Full-screen notifications" settings (Android 14+; no-op when granted). */
  async requestFullScreenIntent(): Promise<void> {
    await androidInvoke<null>('request_full_screen_intent');
  }

  async showNow(item: ScheduledNotification): Promise<void> {
    let systemShown = false;
    if (await this.mayPostNotifications()) {
      try {
        const plugin = await loadNotificationPlugin();
        const id = toInt32Id(item.nativeId);
        plugin.sendNotification({
          ...(id !== null ? { id } : {}),
          channelId: item.alarm ? ANDROID_CHANNELS.alarms : ANDROID_CHANNELS.reminders,
          title: item.title.trim() || DEFAULT_NOTIFICATION_TITLE,
          body: item.body,
          extra: { path: item.path, line: item.line },
          autoCancel: true,
        });
        systemShown = true;
      } catch (e) {
        console.warn('[platform] system notification failed', e);
      }
    }
    emitInAppNotification(item, { systemShown });
  }

  /**
   * False only when the system answered that notifications are off (Android 13+ without
   * POST_NOTIFICATIONS, or disabled in settings): posting would silently do nothing. When
   * the check itself fails the notification is tried anyway.
   */
  private async mayPostNotifications(): Promise<boolean> {
    try {
      const answer = await androidInvoke<unknown>('notification_permission');
      return !(typeof answer === 'object' && answer !== null && (answer as { granted?: unknown }).granted === false);
    } catch {
      return true;
    }
  }

  onActivate(cb: (item: NotificationActivation) => void): () => void {
    const unsubscribe = this.hub.subscribe(cb);
    this.listening ??= this.startListening();
    return unsubscribe;
  }

  /** Report a tap (e.g. on an in-app toast) to `onActivate` subscribers. */
  activate(item: NotificationActivation): void {
    this.hub.emit(item);
  }

  /** Native tap events, the tap that started the app, and plugin-notification taps. */
  private async startListening(): Promise<void> {
    try {
      const { addPluginListener } = await loadTauriCore();
      await addPluginListener(ANDROID_PLUGIN, 'notification-activated', (payload: unknown) => this.hub.emit(payload));
    } catch (e) {
      console.warn('[platform] cannot listen to notification taps', e);
    }
    try {
      this.hub.emit(await androidInvoke<unknown>('take_activation'));
    } catch (e) {
      console.warn('[platform] take_activation failed', e);
    }
    await listenToPluginActions(this.hub);
  }
}

/** The NotificationService for a Tauri build. */
export function createTauriNotificationService(android: boolean): TauriDesktopNotificationService | AndroidNotificationService {
  return android ? new AndroidNotificationService() : new TauriDesktopNotificationService();
}
