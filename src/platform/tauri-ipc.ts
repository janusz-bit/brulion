/**
 * Lazy loaders for `@tauri-apps/*` modules. Every Tauri module is imported dynamically so
 * the web bundle never evaluates them (they assume `window.__TAURI_INTERNALS__`). Loaded
 * modules are cached; a failed import is retried on the next call.
 */
import type { InvokeArgs, InvokeOptions } from '@tauri-apps/api/core';

type CoreModule = typeof import('@tauri-apps/api/core');
type EventModule = typeof import('@tauri-apps/api/event');
type NotificationModule = typeof import('@tauri-apps/plugin-notification');
type DialogModule = typeof import('@tauri-apps/plugin-dialog');
type HttpModule = typeof import('@tauri-apps/plugin-http');
type OpenerModule = typeof import('@tauri-apps/plugin-opener');
type OsModule = typeof import('@tauri-apps/plugin-os');

function cached<T>(load: () => Promise<T>): () => Promise<T> {
  let promise: Promise<T> | null = null;
  return () => {
    promise ??= load().catch((e: unknown) => {
      promise = null;
      throw e;
    });
    return promise;
  };
}

/** `@tauri-apps/api/core` (invoke, addPluginListener). */
export const loadTauriCore = cached<CoreModule>(() => import('@tauri-apps/api/core'));
/** `@tauri-apps/api/event` (listen). */
export const loadTauriEvent = cached<EventModule>(() => import('@tauri-apps/api/event'));
/** `@tauri-apps/plugin-notification`. */
export const loadNotificationPlugin = cached<NotificationModule>(() => import('@tauri-apps/plugin-notification'));
/** `@tauri-apps/plugin-dialog`. */
export const loadDialogPlugin = cached<DialogModule>(() => import('@tauri-apps/plugin-dialog'));
/** `@tauri-apps/plugin-http`. */
export const loadHttpPlugin = cached<HttpModule>(() => import('@tauri-apps/plugin-http'));
/** `@tauri-apps/plugin-opener`. */
export const loadOpenerPlugin = cached<OpenerModule>(() => import('@tauri-apps/plugin-opener'));
/** `@tauri-apps/plugin-os`. */
export const loadOsPlugin = cached<OsModule>(() => import('@tauri-apps/plugin-os'));

/** `invoke` through the lazily loaded core module. */
export async function tauriInvoke<T>(cmd: string, args?: InvokeArgs, options?: InvokeOptions): Promise<T> {
  const { invoke } = await loadTauriCore();
  return invoke<T>(cmd, args, options);
}

/** Name prefix of Brulion's Android plugin commands. */
export const ANDROID_PLUGIN = 'brulion-android';

/** `invoke('plugin:brulion-android|<cmd>')`. */
export function androidInvoke<T>(cmd: string, args?: InvokeArgs): Promise<T> {
  return tauriInvoke<T>(`plugin:${ANDROID_PLUGIN}|${cmd}`, args);
}
