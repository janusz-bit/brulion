// The app's storage in the browser, written and read directly: Emscripten's IDBFS keeps every
// mount in an IndexedDB database named by its path (version 21, object store FILE_DATA keyed by
// the absolute path, index "timestamp"; files {timestamp, mode: 33188, contents: Uint8Array},
// folders {timestamp, mode: 16877}) — the same format as scripts/wasm-smoke.mjs (seedVaultScript)
// and docs/WEB.md "Dane w przeglądarce". Written from a page of the site's origin before the app
// starts there (src/platform/wasm/wasmstorage.h: the notebook and the app's data are mounts).

export const VAULT = '/home/web_user/Brulion';
export const APP_DATA = '/home/web_user/.local/share/Brulion/Brulion';

/** The settings that open the seeded notebook (plus `extra` INI text, e.g. "[editor]\n…"). */
export function settingsIni(extra = '') {
  return `[vaults]\ncurrent=${VAULT}\n${extra}`;
}

/**
 * Writes `files` ({vault path: string | Uint8Array | Buffer}) into the notebook's mount and
 * `settings` (INI text) as the app's settings.ini, from `${origin}/__blank.html`.
 */
export async function seed(page, origin, { files = {}, settings = settingsIni() }) {
  await page.goto(`${origin}/__blank.html`);
  const entries = Object.entries(files).map(([path, content]) => [
    path,
    Buffer.from(typeof content === 'string' ? Buffer.from(content, 'utf8') : content).toString('base64'),
  ]);
  return page.evaluate(async ({ entries, settings, vault, appData }) => {
    const bytes = (b64) => Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    const timestamp = new Date();
    const rows = [];
    const dirs = new Set();
    for (const [path, b64] of entries) {
      const parts = path.split('/');
      for (let i = 1; i < parts.length; i++) dirs.add(vault + '/' + parts.slice(0, i).join('/'));
      rows.push([vault + '/' + path, { timestamp, mode: 33188, contents: bytes(b64) }]);
    }
    for (const d of dirs) rows.push([d, { timestamp, mode: 16877 }]);
    const put = (dbName, list) => new Promise((resolve, reject) => {
      const req = indexedDB.open(dbName, 21);
      req.onupgradeneeded = () => {
        const store = req.result.createObjectStore('FILE_DATA');
        store.createIndex('timestamp', 'timestamp', { unique: false });
      };
      req.onerror = () => reject(req.error);
      req.onsuccess = () => {
        const tx = req.result.transaction(['FILE_DATA'], 'readwrite');
        for (const [key, value] of list) tx.objectStore('FILE_DATA').put(value, key);
        tx.oncomplete = () => {
          req.result.close();
          resolve(list.length);
        };
        tx.onerror = () => reject(tx.error);
      };
    });
    const written = await put(vault, rows);
    await put(appData, [[appData + '/settings.ini',
      { timestamp, mode: 33188, contents: new TextEncoder().encode(settings) }]]);
    return written;
  }, { entries, settings, vault: VAULT, appData: APP_DATA });
}

/** The notebook's file `path` as the app last wrote it back (a Buffer), or null. */
export async function readFile(page, path) {
  const b64 = await page.evaluate(async ({ dbName, key }) => {
    const db = await new Promise((resolve, reject) => {
      const req = indexedDB.open(dbName);
      req.onerror = () => reject(req.error);
      req.onsuccess = () => resolve(req.result);
    });
    try {
      if (!db.objectStoreNames.contains('FILE_DATA')) return null;
      const value = await new Promise((resolve, reject) => {
        const get = db.transaction(['FILE_DATA'], 'readonly').objectStore('FILE_DATA').get(key);
        get.onerror = () => reject(get.error);
        get.onsuccess = () => resolve(get.result);
      });
      if (!value || !value.contents) return null;
      const data = value.contents instanceof Uint8Array ? value.contents : new Uint8Array(value.contents);
      let s = '';
      for (let i = 0; i < data.length; i += 0x8000) s += String.fromCharCode.apply(null, data.subarray(i, i + 0x8000));
      return btoa(s);
    } finally {
      db.close();
    }
  }, { dbName: VAULT, key: `${VAULT}/${path}` });
  return b64 === null ? null : Buffer.from(b64, 'base64');
}
