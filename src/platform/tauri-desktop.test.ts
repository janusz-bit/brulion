// @vitest-environment jsdom
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { emit } from '@tauri-apps/api/event';
import { createTauriPlatform } from './tauri';
import { QUIT_REQUESTED_EVENT, TauriDesktopShell } from './tauri-desktop';
import { installIpc, uninstallIpc, type IpcCall } from './tauri-mock.testutil';

vi.mock('@tauri-apps/plugin-notification', () => ({
  isPermissionGranted: vi.fn(async () => true),
  requestPermission: vi.fn(async () => 'granted'),
  sendNotification: vi.fn(),
  onAction: vi.fn(async () => ({ unregister: async () => {} })),
}));

const cmds = (calls: IpcCall[]) => calls.map((c) => c.cmd);
/** Wait until the quit listener is registered (the event plugin is mocked by installIpc). */
const settle = () => new Promise((r) => setTimeout(r, 10));

let calls: IpcCall[];
beforeEach(() => {
  calls = installIpc(() => null);
  vi.spyOn(console, 'warn').mockImplementation(() => {});
  vi.spyOn(console, 'error').mockImplementation(() => {});
});
afterEach(() => {
  uninstallIpc();
  vi.restoreAllMocks();
});

describe('TauriDesktopShell commands', () => {
  it('maps to the Rust tray / window commands', async () => {
    const shell = new TauriDesktopShell();
    await shell.setCloseToTray(false);
    await shell.showMainWindow();
    await shell.quit();
    expect(calls).toEqual([
      expect.objectContaining({ cmd: 'set_close_to_tray', args: { enabled: false } }),
      expect.objectContaining({ cmd: 'show_main_window' }),
      expect.objectContaining({ cmd: 'quit_app' }),
    ]);
  });
});

describe('quit handshake (brulion://quit-requested)', () => {
  it('flushes pending writes, then answers with quit_app', async () => {
    const shell = new TauriDesktopShell();
    const order: string[] = [];
    let finishWrite: () => void = () => {};
    shell.onQuitRequested(
      () =>
        new Promise<void>((resolve) => {
          order.push('flush: zapis notatki „Żółw”');
          finishWrite = () => {
            order.push('written');
            resolve();
          };
        }),
    );
    shell.onQuitRequested(() => {
      order.push('second flusher');
    });
    await settle();
    await emit(QUIT_REQUESTED_EVENT);
    await settle();
    expect(order).toEqual(['flush: zapis notatki „Żółw”', 'second flusher']);
    expect(cmds(calls)).not.toContain('quit_app');
    finishWrite();
    await vi.waitFor(() => expect(cmds(calls)).toContain('quit_app'));
    expect(order).toEqual(['flush: zapis notatki „Żółw”', 'second flusher', 'written']);
  });

  it('quits even when a flusher fails', async () => {
    const shell = new TauriDesktopShell();
    shell.onQuitRequested(() => {
      throw new Error('disk full');
    });
    shell.onQuitRequested(async () => {
      throw new Error('ENOSPC');
    });
    await settle();
    await emit(QUIT_REQUESTED_EVENT);
    await vi.waitFor(() => expect(cmds(calls)).toEqual(['quit_app']));
    expect(console.error).toHaveBeenCalled();
  });

  it('runs one flush for repeated quit events and skips unsubscribed flushers', async () => {
    const shell = new TauriDesktopShell();
    const kept = vi.fn(async () => {
      await settle();
    });
    const removed = vi.fn();
    shell.onQuitRequested(kept);
    const off = shell.onQuitRequested(removed);
    off();
    await settle();
    await emit(QUIT_REQUESTED_EVENT);
    await emit(QUIT_REQUESTED_EVENT);
    await vi.waitFor(() => expect(cmds(calls)).toContain('quit_app'));
    expect(kept).toHaveBeenCalledTimes(1);
    expect(removed).not.toHaveBeenCalled();
    expect(cmds(calls).filter((c) => c === 'quit_app')).toHaveLength(1);
  });

  it('flushAndQuit() runs the same handshake from the UI (e.g. a "Zakończ" menu item)', async () => {
    const shell = new TauriDesktopShell();
    const flush = vi.fn();
    shell.onQuitRequested(flush);
    await shell.flushAndQuit();
    expect(flush).toHaveBeenCalledTimes(1);
    expect(cmds(calls)).toEqual(['quit_app']);
  });

  it('lets a failed quit_app be retried', async () => {
    uninstallIpc();
    let fail = true;
    calls = installIpc((cmd) => {
      if (cmd === 'quit_app' && fail) throw 'EIO: cannot exit: (app)';
      return null;
    });
    const shell = new TauriDesktopShell();
    await expect(shell.flushAndQuit()).rejects.toBe('EIO: cannot exit: (app)');
    fail = false;
    await shell.flushAndQuit();
    expect(cmds(calls)).toEqual(['quit_app', 'quit_app']);
  });
});

describe('TauriPlatform.desktop', () => {
  it('exists on desktop only', async () => {
    const desktop = await createTauriPlatform({ android: false });
    expect(desktop.desktop).toBeInstanceOf(TauriDesktopShell);
    const android = await createTauriPlatform({ android: true });
    expect(android.desktop).toBeUndefined();
  });
});
