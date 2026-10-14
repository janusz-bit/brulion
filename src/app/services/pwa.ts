/**
 * Service worker registration of the PWA build (vite-plugin-pwa generates `sw.js` with
 * `registerType: 'prompt'` and `injectRegister: false`): registered with workbox-window, only
 * in production browser builds (never inside Tauri). When a new version is waiting,
 * `onUpdate(apply)` is called; `apply()` activates it and reloads the page.
 */
import { isTauriRuntime } from '@/platform';

export interface PwaOptions {
  onUpdate(apply: () => void): void;
  /** Called once the app is cached for offline use (first install). */
  onOfflineReady?(): void;
}

export async function registerServiceWorker(opts: PwaOptions): Promise<void> {
  if (isTauriRuntime() || !import.meta.env.PROD) return;
  if (typeof navigator === 'undefined' || !('serviceWorker' in navigator)) return;
  try {
    const { Workbox } = await import('workbox-window');
    const base = import.meta.env.BASE_URL || '/';
    const wb = new Workbox(`${base}sw.js`, { scope: base });
    let reloading = false;
    const apply = () => {
      wb.addEventListener('controlling', () => {
        if (reloading) return;
        reloading = true;
        window.location.reload();
      });
      wb.messageSkipWaiting();
    };
    wb.addEventListener('waiting', () => opts.onUpdate(apply));
    wb.addEventListener('installed', (e) => {
      if (!e.isUpdate) opts.onOfflineReady?.();
    });
    await wb.register();
  } catch (e) {
    // Blocked service workers (private mode, e2e) must not break the app.
    console.warn('[pwa] service worker registration failed', e);
  }
}
