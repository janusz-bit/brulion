/**
 * `@/platform` — the {@link Platform} the app runs on: the browser (PWA) or the Tauri shell
 * (desktop / Android). Call {@link getPlatform} once at startup; the result is cached.
 *
 * Importing this module never loads `@tauri-apps/*`: the Tauri implementation (./tauri) is
 * imported dynamically, and only when `__TAURI_INTERNALS__` exists.
 */
import type { Platform } from '@/core/types';
import { AndroidNotificationService } from './notifications-tauri';
import { TauriDesktopShell } from './tauri-desktop';
import { createWebPlatform } from './web';

/** True inside the Tauri WebView (Tauri injects `__TAURI_INTERNALS__` before any script). */
export function isTauriRuntime(): boolean {
  if ('__TAURI_INTERNALS__' in globalThis) return true;
  return typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window;
}

async function createPlatform(): Promise<Platform> {
  if (isTauriRuntime()) {
    const { createTauriPlatform } = await import('./tauri');
    return createTauriPlatform();
  }
  return createWebPlatform();
}

let cached: Promise<Platform> | null = null;

/**
 * The current platform (cached). Tauri: `tauri-android` when plugin-os reports `android`,
 * else `tauri-desktop`. Otherwise `web`. A failed creation is retried on the next call.
 */
export function getPlatform(): Promise<Platform> {
  cached ??= createPlatform().catch((e: unknown) => {
    cached = null;
    throw e;
  });
  return cached;
}

/** Forget the cached platform (tests). */
export function resetPlatformCache(): void {
  cached = null;
}

/**
 * Tray / window controls and the quit handshake of the desktop app (register the flush of
 * pending writes with `onQuitRequested`); undefined on Android and in the browser.
 */
export function desktopShell(platform: Platform): TauriDesktopShell | undefined {
  const shell = (platform as { desktop?: unknown }).desktop;
  return shell instanceof TauriDesktopShell ? shell : undefined;
}

/**
 * The Android notification service with its extras (exact alarms, full-screen alarm screen,
 * alarm ring time, last schedule result); undefined on other platforms.
 */
export function androidNotifications(platform: Platform): AndroidNotificationService | undefined {
  return platform.notifications instanceof AndroidNotificationService ? platform.notifications : undefined;
}

export { createWebPlatform, WebPlatform, openBrowserVault, newHandleId, DEFAULT_WEB_VAULT, WEB_PLATFORM_NAME } from './web';
export type { WebPlatformDeps, ShowDirectoryPicker } from './web';
export type { TauriPlatform, TauriPlatformOptions } from './tauri';
export { TauriDesktopShell, QUIT_REQUESTED_EVENT, type QuitFlush } from './tauri-desktop';
export {
  AndroidNotificationService,
  TauriDesktopNotificationService,
  type AndroidReminder,
  type AndroidScheduleResult,
} from './notifications-tauri';
export {
  WebNotificationService,
  browserNotificationEnv,
  buildNotificationOptions,
  mapPermission,
  NOTIFICATION_CLICK_MESSAGE,
  NOTIFICATION_ICON,
  ALARM_VIBRATION,
} from './notifications-web';
export type { WebNotificationEnv, WebNotificationOptions, NotificationCtorLike, SwContainerLike, SwRegistrationLike } from './notifications-web';
export {
  onInAppNotification,
  parseActivation,
  ActivationHub,
  type InAppNotificationListener,
  type InAppNotificationInfo,
  type NotificationActivation,
} from './notifications-common';
export {
  handleNotificationClick,
  isAppClient,
  activationUrl,
  activationFromUrl,
  stripActivationParams,
  ACTIVATION_PATH_PARAM,
  ACTIVATION_LINE_PARAM,
  type SwClientsLike,
  type SwWindowClientLike,
} from './sw-notifications';
export { createFetchTransport, createBrowserHttpTransport, createNetworkError, isNetworkError, NETWORK_ERROR, type FetchLike, type FetchTransportOptions } from './http';
export { pickFilesWithInput, downloadFile, acceptAttribute, pickedPaths, type PickFilesOptions } from './web-files';
export { TauriFs, TauriFsError, toFsError, FS_EVENTS, type TauriFsOptions, type FileStatDto, type FsEventsPayload } from './tauri-fs';
export { PlatformError, isPlatformError, type PlatformErrorCode } from './errors';
export { checkExternalUrl, redactUrl } from './url';
export { bytesToBase64, base64ToBytes } from './bytes';
