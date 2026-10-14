// @vitest-environment jsdom
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { Channel } from '@tauri-apps/api/core';
import type { ScheduledNotification } from '@/core/types';
import { onInAppNotification, type InAppNotificationListener } from './notifications-common';
import {
  activationFromAction,
  AndroidNotificationService,
  androidReminderSet,
  clampRingMinutes,
  createTauriNotificationService,
  DESKTOP_ALARM_SOUND,
  MAX_ANDROID_REMINDERS,
  TauriDesktopNotificationService,
  toAndroidReminder,
  toInt32Id,
} from './notifications-tauri';
import { installIpc, uninstallIpc, type IpcCall } from './tauri-mock.testutil';

const plugin = vi.hoisted(() => ({
  isPermissionGranted: vi.fn(async () => true),
  requestPermission: vi.fn(async (): Promise<string> => 'granted'),
  sendNotification: vi.fn(),
  onAction: vi.fn(async (_cb: (e: unknown) => void) => ({ unregister: async () => {} })),
}));
vi.mock('@tauri-apps/plugin-notification', () => plugin);

const reminder: ScheduledNotification = {
  id: 'Dziennik/2026-10-14.md:5:2026-10-14T09:00',
  nativeId: 123456,
  title: 'Odebrać paczkę',
  body: 'Paczkomat przy ul. Źródlanej',
  at: '2026-10-14T09:00',
  alarm: false,
  path: 'Dziennik/2026-10-14.md',
  line: 5,
};
const alarm: ScheduledNotification = { ...reminder, id: 'a1', nativeId: 3_000_000_000, title: 'Pobudka', at: '2026-10-15T06:30', alarm: true };
const NOW = new Date(2026, 9, 14, 8, 0).getTime();

let inApp: ReturnType<typeof vi.fn<InAppNotificationListener>>;
let offInApp: () => void;

beforeEach(() => {
  inApp = vi.fn<InAppNotificationListener>();
  offInApp = onInAppNotification(inApp);
  plugin.isPermissionGranted.mockReset().mockResolvedValue(true);
  plugin.requestPermission.mockReset().mockResolvedValue('granted');
  plugin.sendNotification.mockReset();
  plugin.onAction.mockReset().mockResolvedValue({ unregister: async () => {} });
  vi.spyOn(console, 'warn').mockImplementation(() => {});
  vi.spyOn(console, 'debug').mockImplementation(() => {});
});
afterEach(() => {
  offInApp();
  uninstallIpc();
  vi.restoreAllMocks();
});

describe('helpers', () => {
  it('toInt32Id keeps int32 and wraps uint32 like the Rust side', () => {
    expect(toInt32Id(0)).toBe(0);
    expect(toInt32Id(-5)).toBe(-5);
    expect(toInt32Id(2 ** 31 - 1)).toBe(2 ** 31 - 1);
    expect(toInt32Id(2 ** 31)).toBe(-(2 ** 31));
    expect(toInt32Id(2 ** 32 - 1)).toBe(-1);
    expect(toInt32Id(2 ** 32)).toBeNull();
    expect(toInt32Id(-(2 ** 31) - 1)).toBeNull();
    expect(toInt32Id(1.5)).toBeNull();
    expect(toInt32Id(Number.NaN)).toBeNull();
  });

  it('clampRingMinutes', () => {
    expect(clampRingMinutes(undefined)).toBeUndefined();
    expect(clampRingMinutes(Number.NaN)).toBeUndefined();
    expect(clampRingMinutes(0)).toBe(1);
    expect(clampRingMinutes(15.4)).toBe(15);
    expect(clampRingMinutes(600)).toBe(60);
  });

  it('toAndroidReminder converts local wall-clock time to epoch ms', () => {
    expect(toAndroidReminder(reminder)).toEqual({
      id: 123456,
      title: 'Odebrać paczkę',
      body: 'Paczkomat przy ul. Źródlanej',
      at: new Date(2026, 9, 14, 9, 0).getTime(),
      alarm: false,
      path: 'Dziennik/2026-10-14.md',
      line: 5,
    });
    expect(toAndroidReminder(alarm, 15)).toMatchObject({ id: 3_000_000_000 | 0, alarm: true, ringMinutes: 15 });
    expect(toAndroidReminder(reminder, 15)).not.toHaveProperty('ringMinutes');
    expect(toAndroidReminder({ ...reminder, title: ' ', line: -3 })).toMatchObject({ title: 'Brulion', line: 0 });
  });

  it('toAndroidReminder rejects invalid ids and dates', () => {
    expect(toAndroidReminder({ ...reminder, at: '14.10.2026' })).toBeNull();
    expect(toAndroidReminder({ ...reminder, nativeId: 2 ** 40 })).toBeNull();
  });

  it('androidReminderSet drops past items, keeps the last duplicate, sorts and caps', () => {
    const past = { ...reminder, id: 'p', nativeId: 1, at: '2026-10-14T07:59' };
    const exactlyNow = { ...reminder, id: 'n', nativeId: 2, at: '2026-10-14T08:00' };
    const later = { ...reminder, id: 'l', nativeId: 3, at: '2026-10-20T10:00' };
    const dupOld = { ...reminder, id: 'd', nativeId: 4, at: '2026-10-16T10:00', title: 'stara' };
    const dupNew = { ...dupOld, title: 'nowa wersja' };
    const set = androidReminderSet([later, past, exactlyNow, dupOld, reminder, dupNew, { ...reminder, at: 'zła data' }], NOW);
    expect(set.map((r) => r.id)).toEqual([123456, 4, 3]);
    expect(set[1]!.title).toBe('nowa wersja');

    const many = Array.from({ length: MAX_ANDROID_REMINDERS + 30 }, (_, i) => ({
      ...reminder,
      id: `r${i}`,
      nativeId: i,
      at: `2027-01-01T${String(Math.floor(i / 60) % 24).padStart(2, '0')}:${String(i % 60).padStart(2, '0')}`,
    }));
    const capped = androidReminderSet(many, NOW);
    expect(capped).toHaveLength(MAX_ANDROID_REMINDERS);
    expect(capped[0]!.id).toBe(0);
  });

  it('activationFromAction reads notification.extra', () => {
    expect(activationFromAction({ actionId: 'tap', notification: { extra: { path: 'a.md', line: 2 } } })).toEqual({ path: 'a.md', line: 2 });
    expect(activationFromAction({ actionId: 'tap', notification: null })).toBeNull();
    expect(activationFromAction(null)).toBeNull();
  });

  it('createTauriNotificationService picks the implementation', () => {
    expect(createTauriNotificationService(true)).toBeInstanceOf(AndroidNotificationService);
    expect(createTauriNotificationService(false)).toBeInstanceOf(TauriDesktopNotificationService);
  });
});

describe('TauriDesktopNotificationService', () => {
  it('relies on the in-app scheduler', async () => {
    const svc = new TauriDesktopNotificationService();
    expect(svc.firesWhenClosed).toBe(false);
    await svc.sync([reminder, alarm]);
    expect(svc.pending()).toEqual([reminder, alarm]);
    expect(plugin.sendNotification).not.toHaveBeenCalled();
  });

  it('maps permissions', async () => {
    const svc = new TauriDesktopNotificationService();
    expect(await svc.permission()).toBe('granted');
    plugin.isPermissionGranted.mockResolvedValue(false);
    expect(await svc.permission()).toBe('prompt');
    plugin.isPermissionGranted.mockRejectedValue(new Error('no plugin'));
    expect(await svc.permission()).toBe('denied');
    expect(await svc.requestPermission()).toBe('granted');
    plugin.requestPermission.mockResolvedValue('default');
    expect(await svc.requestPermission()).toBe('prompt');
    plugin.requestPermission.mockResolvedValue('denied');
    expect(await svc.requestPermission()).toBe('denied');
    plugin.requestPermission.mockRejectedValue(new Error('x'));
    expect(await svc.requestPermission()).toBe('denied');
  });

  it('shows reminders and alarms (with the alarm sound) and notifies in-app', async () => {
    const svc = new TauriDesktopNotificationService();
    await svc.showNow(reminder);
    expect(plugin.sendNotification).toHaveBeenLastCalledWith({
      id: 123456,
      title: 'Odebrać paczkę',
      body: 'Paczkomat przy ul. Źródlanej',
      extra: { path: 'Dziennik/2026-10-14.md', line: 5 },
    });
    await svc.showNow(alarm);
    expect(plugin.sendNotification).toHaveBeenLastCalledWith(expect.objectContaining({ id: -1294967296, title: 'Pobudka', sound: DESKTOP_ALARM_SOUND }));
    expect(inApp).toHaveBeenCalledWith(alarm, { systemShown: true });
  });

  it('skips the system notification without permission, never the in-app one', async () => {
    plugin.isPermissionGranted.mockResolvedValue(false);
    await new TauriDesktopNotificationService().showNow({ ...reminder, nativeId: 0.5, title: '' });
    expect(plugin.sendNotification).not.toHaveBeenCalled();
    expect(inApp).toHaveBeenCalledWith(expect.objectContaining({ id: reminder.id }), { systemShown: false });
  });

  it('survives a failing plugin', async () => {
    plugin.sendNotification.mockImplementation(() => {
      throw new Error('dbus');
    });
    await new TauriDesktopNotificationService().showNow(reminder);
    expect(inApp).toHaveBeenCalledWith(reminder, { systemShown: false });
  });

  it('omits invalid native ids and empty titles', async () => {
    await new TauriDesktopNotificationService().showNow({ ...reminder, nativeId: 2 ** 40, title: '  ' });
    const opts = plugin.sendNotification.mock.calls[0]![0] as Record<string, unknown>;
    expect(opts).not.toHaveProperty('id');
    expect(opts.title).toBe('Brulion');
  });

  it('reports plugin action taps once subscribed', async () => {
    let action: ((e: unknown) => void) | null = null;
    plugin.onAction.mockImplementation(async (cb) => {
      action = cb;
      return { unregister: async () => {} };
    });
    const svc = new TauriDesktopNotificationService();
    const cb = vi.fn();
    svc.onActivate(cb);
    svc.onActivate(vi.fn());
    await vi.waitFor(() => expect(action).not.toBeNull());
    expect(plugin.onAction).toHaveBeenCalledTimes(1);
    action!({ actionId: 'tap', notification: { extra: { path: 'Łąka.md', line: 9 } } });
    expect(cb).toHaveBeenCalledWith({ path: 'Łąka.md', line: 9 });
    svc.activate({ path: 'toast.md', line: 1 });
    expect(cb).toHaveBeenLastCalledWith({ path: 'toast.md', line: 1 });
  });

  it('copes with desktop plugins without action support', async () => {
    plugin.onAction.mockRejectedValue('not supported on desktop');
    const svc = new TauriDesktopNotificationService();
    const off = svc.onActivate(vi.fn());
    await new Promise((r) => setTimeout(r, 5));
    off();
  });
});

describe('AndroidNotificationService', () => {
  function android(handler: (cmd: string, args: unknown) => unknown = () => null) {
    const calls = installIpc((cmd, args) => handler(cmd, args));
    return { calls, svc: new AndroidNotificationService({ now: () => NOW }) };
  }
  const scheduleCalls = (calls: IpcCall[]) => calls.filter((c) => c.cmd === 'plugin:brulion-android|schedule_reminders');

  it('fires while closed', () => {
    expect(new AndroidNotificationService().firesWhenClosed).toBe(true);
  });

  it('hands the reminder set to the native scheduler', async () => {
    const { calls, svc } = android((cmd) => (cmd.endsWith('schedule_reminders') ? { scheduled: 2, exact: true } : null));
    expect(svc.scheduleResult).toBeNull();
    await svc.sync([alarm, reminder, { ...reminder, id: 'old', nativeId: 9, at: '2026-10-13T09:00' }]);
    const sent = scheduleCalls(calls);
    expect(sent).toHaveLength(1);
    expect(sent[0]!.args).toEqual({
      items: [
        expect.objectContaining({ id: 123456, at: new Date(2026, 9, 14, 9, 0).getTime(), alarm: false, path: 'Dziennik/2026-10-14.md', line: 5 }),
        expect.objectContaining({ id: 3_000_000_000 | 0, at: new Date(2026, 9, 15, 6, 30).getTime(), alarm: true, title: 'Pobudka' }),
      ],
    });
    expect(svc.scheduleResult).toEqual({ scheduled: 2, exact: true });
  });

  it('does not resend an identical set, but sends changes and empty sets', async () => {
    const { calls, svc } = android(() => ({ scheduled: 1, exact: false }));
    await svc.sync([reminder]);
    await svc.sync([{ ...reminder }]);
    expect(scheduleCalls(calls)).toHaveLength(1);
    await svc.sync([{ ...reminder, title: 'Odebrać paczkę — pilne' }]);
    expect(scheduleCalls(calls)).toHaveLength(2);
    await svc.sync([]);
    expect(scheduleCalls(calls).at(-1)!.args).toEqual({ items: [] });
  });

  it('passes the alarm ring time', async () => {
    const { calls, svc } = android(() => ({ scheduled: 1, exact: true }));
    svc.ringMinutes = 90;
    expect(svc.ringMinutes).toBe(60);
    await svc.sync([alarm, reminder]);
    const items = (scheduleCalls(calls)[0]!.args as { items: Record<string, unknown>[] }).items;
    expect(items.find((i) => i.alarm)?.ringMinutes).toBe(60);
    expect(items.find((i) => !i.alarm)).not.toHaveProperty('ringMinutes');
    expect(new AndroidNotificationService({ ringMinutes: 5 }).ringMinutes).toBe(5);
  });

  it('retries after a failed native call', async () => {
    let fail = true;
    const { calls, svc } = android(() => {
      if (fail) throw 'scheduleReminders failed: boom';
      return { scheduled: 1, exact: true };
    });
    await expect(svc.sync([reminder])).rejects.toBe('scheduleReminders failed: boom');
    fail = false;
    await svc.sync([reminder]);
    expect(scheduleCalls(calls)).toHaveLength(2);
    expect(svc.scheduleResult).toEqual({ scheduled: 1, exact: true });
  });

  it('tolerates odd schedule answers', async () => {
    const { svc } = android(() => null);
    await svc.sync([reminder]);
    expect(svc.scheduleResult).toEqual({ scheduled: 0, exact: false });
  });

  it('maps notification permission', async () => {
    let granted = false;
    const { calls, svc } = android((cmd) => ({ granted: cmd.endsWith('request_notification_permission') ? true : granted }));
    expect(await svc.permission()).toBe('prompt');
    granted = true;
    expect(await svc.permission()).toBe('granted');
    expect(await svc.requestPermission()).toBe('granted');
    expect(calls.map((c) => c.cmd)).toEqual([
      'plugin:brulion-android|notification_permission',
      'plugin:brulion-android|notification_permission',
      'plugin:brulion-android|request_notification_permission',
    ]);
  });

  it('reports denied when the request fails or is refused', async () => {
    const { svc } = android((cmd) => {
      if (cmd.endsWith('|notification_permission')) throw 'no plugin';
      return { granted: false };
    });
    expect(await svc.permission()).toBe('denied');
    expect(await svc.requestPermission()).toBe('denied');
  });

  it('exposes exact-alarm and full-screen permissions', async () => {
    const { calls, svc } = android((cmd) => (cmd.endsWith('can_schedule_exact') ? { granted: true } : cmd.endsWith('can_use_full_screen_intent') ? { granted: false } : null));
    expect(await svc.canScheduleExact()).toBe(true);
    expect(await svc.canUseFullScreenIntent()).toBe(false);
    await svc.requestExactAlarms();
    await svc.requestFullScreenIntent();
    expect(calls.map((c) => c.cmd.split('|')[1])).toEqual(['can_schedule_exact', 'can_use_full_screen_intent', 'request_exact_alarms', 'request_full_screen_intent']);
  });

  it('re-sends the set after the exact-alarm permission was requested', async () => {
    const { calls, svc } = android(() => ({ scheduled: 1, exact: true }));
    await svc.sync([reminder]);
    await svc.requestExactAlarms();
    await svc.sync([reminder]);
    expect(scheduleCalls(calls)).toHaveLength(2);
  });

  it('answers false when permission checks fail', async () => {
    const { svc } = android(() => {
      throw 'x';
    });
    expect(await svc.canScheduleExact()).toBe(false);
    expect(await svc.canUseFullScreenIntent()).toBe(false);
  });

  it('shows notifications on the native channels', async () => {
    const { svc } = android();
    await svc.showNow(reminder);
    expect(plugin.sendNotification).toHaveBeenLastCalledWith({
      id: 123456,
      channelId: 'reminders',
      title: 'Odebrać paczkę',
      body: 'Paczkomat przy ul. Źródlanej',
      extra: { path: 'Dziennik/2026-10-14.md', line: 5 },
      autoCancel: true,
    });
    await svc.showNow(alarm);
    expect(plugin.sendNotification).toHaveBeenLastCalledWith(expect.objectContaining({ channelId: 'alarms' }));
    expect(inApp).toHaveBeenCalledTimes(2);
    plugin.sendNotification.mockImplementation(() => {
      throw new Error('x');
    });
    await svc.showNow(reminder);
    expect(inApp).toHaveBeenLastCalledWith(reminder, { systemShown: false });
  });

  it('delivers the tap that started the app, native tap events and plugin taps', async () => {
    let channel: Channel<unknown> | null = null;
    let action: ((e: unknown) => void) | null = null;
    plugin.onAction.mockImplementation(async (cb) => {
      action = cb;
      return { unregister: async () => {} };
    });
    const { calls, svc } = android((cmd, args) => {
      if (cmd === 'plugin:brulion-android|register_listener') {
        expect((args as { event: string }).event).toBe('notification-activated');
        channel = (args as { handler: Channel<unknown> }).handler;
        return null;
      }
      if (cmd === 'plugin:brulion-android|take_activation') return { path: 'Książki/Pan Tadeusz.md', line: 12 };
      return null;
    });
    const cb = vi.fn();
    svc.onActivate(cb);
    svc.onActivate(vi.fn());
    await vi.waitFor(() => expect(action).not.toBeNull());
    expect(cb).toHaveBeenCalledWith({ path: 'Książki/Pan Tadeusz.md', line: 12 });
    expect(calls.filter((c) => c.cmd.endsWith('take_activation'))).toHaveLength(1);

    channel!.onmessage({ path: 'Dziennik/2026-10-14.md', line: 3 });
    expect(cb).toHaveBeenLastCalledWith({ path: 'Dziennik/2026-10-14.md', line: 3 });
    action!({ actionId: 'tap', notification: { extra: { path: 'Zakupy.md', line: 1 } } });
    expect(cb).toHaveBeenLastCalledWith({ path: 'Zakupy.md', line: 1 });
    channel!.onmessage({});
    expect(cb).toHaveBeenCalledTimes(3);
  });

  it('ignores an empty activation and failing listeners', async () => {
    const { svc } = android((cmd) => {
      if (cmd.endsWith('register_listener')) throw 'unsupported';
      if (cmd.endsWith('take_activation')) return {};
      return null;
    });
    plugin.onAction.mockRejectedValue('nope');
    const cb = vi.fn();
    svc.onActivate(cb);
    await new Promise((r) => setTimeout(r, 10));
    expect(cb).not.toHaveBeenCalled();
    svc.activate({ path: 'x.md', line: 0 });
    expect(cb).toHaveBeenCalledWith({ path: 'x.md', line: 0 });
  });
});

describe('AndroidNotificationService review fixes', () => {
  const scheduleCalls = (calls: IpcCall[]) => calls.filter((c) => c.cmd === 'plugin:brulion-android|schedule_reminders');

  it('does not report a system notification when notifications are not allowed', async () => {
    installIpc((cmd) => (cmd.endsWith('|notification_permission') ? { granted: false } : null));
    const svc = new AndroidNotificationService({ now: () => NOW });
    await svc.showNow(alarm);
    expect(plugin.sendNotification).not.toHaveBeenCalled();
    expect(inApp).toHaveBeenCalledWith(alarm, { systemShown: false });
  });

  it('still tries the system notification when the permission check itself fails', async () => {
    installIpc((cmd) => {
      if (cmd.endsWith('|notification_permission')) throw 'unknown command';
      return null;
    });
    await new AndroidNotificationService({ now: () => NOW }).showNow(reminder);
    expect(plugin.sendNotification).toHaveBeenCalledTimes(1);
    expect(inApp).toHaveBeenCalledWith(reminder, { systemShown: true });
  });

  it('runs one schedule_reminders at a time; the last set wins and is not re-sent', async () => {
    const pending: ((v: unknown) => void)[] = [];
    const calls = installIpc((cmd) =>
      cmd.endsWith('schedule_reminders') ? new Promise((resolve) => pending.push(resolve)) : null,
    );
    const svc = new AndroidNotificationService({ now: () => NOW });
    const first = svc.sync([reminder]);
    const second = svc.sync([reminder, alarm]);
    await vi.waitFor(() => expect(scheduleCalls(calls)).toHaveLength(1));
    await new Promise((r) => setTimeout(r, 10));
    // The second set waits for the first native call.
    expect(scheduleCalls(calls)).toHaveLength(1);
    pending[0]!({ scheduled: 1, exact: true });
    await first;
    await vi.waitFor(() => expect(scheduleCalls(calls)).toHaveLength(2));
    pending[1]!({ scheduled: 2, exact: true });
    await second;
    expect((scheduleCalls(calls)[1]!.args as { items: unknown[] }).items).toHaveLength(2);
    expect(svc.scheduleResult).toEqual({ scheduled: 2, exact: true });
    await svc.sync([reminder, alarm]);
    expect(scheduleCalls(calls)).toHaveLength(2);
  });

  it('a failed sync does not block the next one', async () => {
    let fail = true;
    const calls = installIpc((cmd) => {
      if (!cmd.endsWith('schedule_reminders')) return null;
      if (fail) throw 'scheduleReminders failed: boom';
      return { scheduled: 1, exact: true };
    });
    const svc = new AndroidNotificationService({ now: () => NOW });
    const a = svc.sync([reminder]);
    fail = true;
    const b = svc.sync([alarm]).catch((e: unknown) => e);
    await expect(a).rejects.toBe('scheduleReminders failed: boom');
    fail = false;
    await b;
    await svc.sync([alarm]);
    expect(scheduleCalls(calls).length).toBeGreaterThanOrEqual(2);
    expect(svc.scheduleResult).toEqual({ scheduled: 1, exact: true });
  });
});
