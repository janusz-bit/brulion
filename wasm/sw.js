/*
 * Brulion service worker — makes the browser version work offline.
 * Copyright the Brulion authors, AGPL-3.0-or-later.
 *
 * Cache-first app shell: on install every file of this build is put into a cache named after
 * the build's content hash; pages are then served from that cache, online or not. A new
 * deployment changes this file (new version/precache list), the browser installs the new
 * worker, which takes over at once and drops the old caches — the next page load runs the
 * new version (no update prompt).
 *
 * Only the app's own files are handled. Everything else — WebDAV/Nextcloud sync, other apps
 * on the same origin, non-GET requests — goes to the network untouched.
 *
 * scripts/wasm-build.sh fills in the placeholders below.
 */
'use strict';

const VERSION = '@BRULION_CACHE_VERSION@';
const CACHE_PREFIX = 'brulion-';
const CACHE = CACHE_PREFIX + VERSION;
// Paths relative to the worker's scope, e.g. "brulion.wasm?v=…" (the page requests them with
// the same query).
const PRECACHE = ['@BRULION_PRECACHE@'];
const SHELL = 'index.html';

const scopeUrl = (path) => new URL(path, self.registration.scope).href;
const withoutQuery = (url) => url.split(/[?#]/)[0];
const precached = new Set(PRECACHE.map((p) => withoutQuery(scopeUrl(p))));

self.addEventListener('install', (event) => {
  event.waitUntil(
    (async () => {
      const cache = await caches.open(CACHE);
      // 'no-cache': revalidate with the server (a 304 reuses the copy the page just
      // downloaded), so the cache never mixes files of two deployments.
      await cache.addAll(PRECACHE.map((p) => new Request(scopeUrl(p), { cache: 'no-cache' })));
      await self.skipWaiting();
    })(),
  );
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    (async () => {
      const keys = await caches.keys();
      await Promise.all(
        keys.filter((k) => k.startsWith(CACHE_PREFIX) && k !== CACHE).map((k) => caches.delete(k)),
      );
      await self.clients.claim();
    })(),
  );
});

self.addEventListener('fetch', (event) => {
  const request = event.request;
  if (request.method !== 'GET') return;
  const url = new URL(request.url);
  if (url.origin !== self.location.origin) return;
  if (!request.url.startsWith(self.registration.scope)) return;

  if (request.mode === 'navigate') {
    // Any page of the app (also "?selftest=…" or reminder links) gets the cached shell.
    event.respondWith(
      (async () => {
        const cache = await caches.open(CACHE);
        const shell = await cache.match(scopeUrl(SHELL), { ignoreSearch: true, ignoreVary: true });
        return shell || fetch(request);
      })(),
    );
    return;
  }

  if (!precached.has(withoutQuery(request.url))) return;
  event.respondWith(
    (async () => {
      // Exact match (with ?v=): a page of an older build never gets this build's files.
      const cache = await caches.open(CACHE);
      const hit = await cache.match(request, { ignoreVary: true });
      return hit || fetch(request);
    })(),
  );
});

// Notification taps (notifications shown through the registration, e.g. on Android; see
// src/platform/wasm/wasmnotifier.cpp): focus an open Brulion window and tell it which reminder
// was tapped, or open the app with the reminder in the URL.
self.addEventListener('notificationclick', (event) => {
  const data = event.notification.data || {};
  event.notification.close();
  if (!data.brulion) return;
  event.waitUntil(
    (async () => {
      const windows = await self.clients.matchAll({ type: 'window', includeUncontrolled: true });
      // One origin may host several apps (user.github.io/<project>/): only ours.
      const mine = windows.filter((c) => !c.url || c.url.startsWith(self.registration.scope));
      const target = mine.find((c) => c.focused) || mine.find((c) => c.visibilityState === 'visible') || mine[0];
      const message = { type: 'brulion:notificationclick', id: data.id, path: data.path, line: data.line };
      if (target) {
        try {
          await target.focus();
        } catch (e) {
          // Focusing may be refused; the message still arrives.
        }
        target.postMessage(message);
        return;
      }
      const url = new URL(self.registration.scope);
      url.searchParams.set('brulion-id', String(data.id || ''));
      url.searchParams.set('brulion-note', String(data.path || ''));
      url.searchParams.set('brulion-line', String(typeof data.line === 'number' ? data.line : -1));
      await self.clients.openWindow(url.href);
    })(),
  );
});
