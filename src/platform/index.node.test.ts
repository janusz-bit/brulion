import { describe, expect, it } from 'vitest';

// Default (node) environment: no window, document or navigator.serviceWorker.
describe('platform module without a DOM', () => {
  it('imports without touching browser globals and still creates a web platform', async () => {
    const mod = await import('./index');
    mod.resetPlatformCache();
    expect(mod.isTauriRuntime()).toBe(false);
    const p = await mod.getPlatform();
    expect(p.kind).toBe('web');
    expect(p.vaultKinds).toEqual(['idb']);
    expect(await p.notifications.permission()).toBe('denied');
    mod.resetPlatformCache();
  });
});
