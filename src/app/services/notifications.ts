/**
 * Notification glue: reminders fired while the app is open become toasts (with a sound for
 * alarms, also when system notifications are denied), and taps on notifications — from the
 * system, the service worker, Android's alarm screen (`take_activation` at startup) — open
 * the note at the reminder's line.
 */
import { translatorFor } from '@/app/components';
import { desktopShell, onInAppNotification } from '@/platform';
import type { Platform, ScheduledNotification, VaultPath } from '@/core/types';
import { useAppStore } from '../state/store';

type Activation = { path: VaultPath; line: number };

let pending: Activation | null = null;

/** Open the note of a tapped notification (now, or once a vault is open). */
export function openActivation(item: Activation): void {
  const s = useAppStore.getState();
  if (s.status !== 'ready' || !s.workspace) {
    pending = item;
    return;
  }
  s.navigate({ view: 'page', path: item.path, line: item.line });
}

/** Deliver an activation that arrived before the vault was open. */
export function flushPendingActivation(): void {
  const item = pending;
  pending = null;
  if (item) openActivation(item);
}

/** A short beep pattern for alarms (WebAudio); silently does nothing where audio is blocked. */
export function playAlarmSound(): void {
  try {
    const Ctx = (globalThis as { AudioContext?: typeof AudioContext; webkitAudioContext?: typeof AudioContext }).AudioContext ??
      (globalThis as { webkitAudioContext?: typeof AudioContext }).webkitAudioContext;
    if (!Ctx) return;
    const ctx = new Ctx();
    const start = ctx.currentTime + 0.05;
    for (let i = 0; i < 6; i++) {
      const osc = ctx.createOscillator();
      const gain = ctx.createGain();
      osc.type = 'sine';
      osc.frequency.value = i % 2 === 0 ? 880 : 660;
      const t0 = start + i * 0.32;
      gain.gain.setValueAtTime(0.0001, t0);
      gain.gain.exponentialRampToValueAtTime(0.25, t0 + 0.02);
      gain.gain.exponentialRampToValueAtTime(0.0001, t0 + 0.26);
      osc.connect(gain).connect(ctx.destination);
      osc.start(t0);
      osc.stop(t0 + 0.28);
    }
    setTimeout(() => void ctx.close().catch(() => undefined), 3000);
  } catch {
    // Autoplay policies: no sound, the toast is still shown.
  }
}

function showReminderToast(item: ScheduledNotification): void {
  const s = useAppStore.getState();
  const t = translatorFor(s.locale);
  s.toast({
    id: `reminder:${item.id}`,
    kind: 'reminder',
    title: item.alarm ? t('shell.reminder.alarm') : t('shell.reminder.title'),
    message: item.title || item.body,
    action: { label: t('shell.reminder.open'), onClick: () => openActivation({ path: item.path, line: item.line }) },
    timeout: item.alarm ? 0 : 30_000,
  });
  if (item.alarm && s.settings.reminders.sound) playAlarmSound();
}

/** Wire in-app reminders and notification taps for a platform. Returns an unsubscribe function. */
export function wireNotifications(platform: Platform): () => void {
  const offInApp = onInAppNotification((item) => {
    showReminderToast(item);
    // Desktop: the window may be hidden in the tray when an alarm fires.
    if (item.alarm) void desktopShell(platform)?.showMainWindow().catch(() => undefined);
  });
  const offActivate = platform.notifications.onActivate((item) => {
    void desktopShell(platform)?.showMainWindow().catch(() => undefined);
    openActivation(item);
  });
  return () => {
    offInApp();
    offActivate();
  };
}
