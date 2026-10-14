/**
 * Browser file picking (import) and saving (export). Browser-only at runtime: every DOM
 * access happens inside the functions. Also used by the Tauri platform on Android, whose
 * WebView supports `<input type=file>`.
 */
import type { PickedFile } from '@/core/types';
import { sanitizeFileName } from '@/core/paths';
import { toArrayBufferView } from './bytes';

export interface PickFilesOptions {
  /** Extensions (".md", "md") and/or MIME types ("text/markdown", "image/*"). */
  accept?: string[];
  multiple?: boolean;
  /** Pick a whole directory (`webkitdirectory`). */
  directory?: boolean;
}

/** DOM handles used by the picker (injectable for tests). */
export interface PickerDom {
  document: Document;
  window: Window;
}

/** How long after the window regains focus a picker without files counts as cancelled. */
export const PICKER_FOCUS_CANCEL_DELAY = 1000;

/** Build the `accept` attribute: ".md" stays, "md" becomes ".md", MIME types stay. */
export function acceptAttribute(accept: readonly string[] | undefined): string {
  if (!accept) return '';
  const out: string[] = [];
  for (const raw of accept) {
    const a = raw.trim();
    if (!a) continue;
    const v = a.startsWith('.') || a.includes('/') ? a : `.${a}`;
    if (!out.includes(v)) out.push(v);
  }
  return out.join(',');
}

/**
 * Relative paths of picked files (NFC, like vault paths). For directory picks
 * `webkitRelativePath` starts with the picked folder's name ("Notatki/a.md"); it is
 * stripped so paths are relative to the picked directory (as on desktop). Without
 * `webkitRelativePath` the file name is used.
 */
export function pickedPaths(files: readonly { name: string; webkitRelativePath?: string }[], directory: boolean): string[] {
  const raw = files.map((f) => (f.webkitRelativePath ? f.webkitRelativePath.replace(/\\/g, '/') : f.name).normalize('NFC'));
  if (!directory || raw.length === 0) return raw;
  const first = raw[0]!.split('/')[0] ?? '';
  const shared = first !== '' && raw.every((p) => p.startsWith(`${first}/`));
  return shared ? raw.map((p) => p.slice(first.length + 1)) : raw;
}

async function readFileBytes(file: File): Promise<Uint8Array> {
  if (typeof file.arrayBuffer === 'function') return new Uint8Array(await file.arrayBuffer());
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(new Uint8Array(reader.result as ArrayBuffer));
    reader.onerror = () => reject(reader.error ?? new Error('FileReader failed'));
    reader.readAsArrayBuffer(file);
  });
}

/** Read a FileList (or array of Files) into PickedFile[]. */
export async function readPickedFiles(files: readonly File[], directory: boolean): Promise<PickedFile[]> {
  const paths = pickedPaths(files, directory);
  return Promise.all(files.map(async (file, i) => ({ path: paths[i] ?? file.name, data: await readFileBytes(file) })));
}

function defaultDom(): PickerDom {
  return { document, window };
}

/**
 * Let the user pick files (or a directory) with a hidden `<input type=file>`. Resolves `[]`
 * when the dialog is cancelled: via the `cancel` event, or — for browsers without it — when
 * the window regains focus and no file arrives within {@link PICKER_FOCUS_CANCEL_DELAY} ms.
 * Must be called from a user gesture.
 */
export function pickFilesWithInput(opts: PickFilesOptions = {}, dom: PickerDom = defaultDom()): Promise<PickedFile[]> {
  const { document: doc, window: win } = dom;
  const directory = !!opts.directory;
  const input = doc.createElement('input');
  input.type = 'file';
  input.multiple = !!opts.multiple || directory;
  const accept = acceptAttribute(opts.accept);
  if (accept && !directory) input.accept = accept;
  if (directory) {
    input.webkitdirectory = true;
    input.setAttribute('webkitdirectory', '');
    input.setAttribute('directory', '');
  }
  input.setAttribute('aria-hidden', 'true');
  input.tabIndex = -1;
  input.style.cssText = 'position:fixed;left:-10000px;top:0;width:1px;height:1px;opacity:0;';

  return new Promise<PickedFile[]>((resolve, reject) => {
    let settled = false;
    let focusTimer: ReturnType<typeof setTimeout> | null = null;

    const cleanup = (): void => {
      input.removeEventListener('change', onChange);
      input.removeEventListener('cancel', onCancel);
      win.removeEventListener('focus', onFocus);
      if (focusTimer !== null) clearTimeout(focusTimer);
      input.remove();
    };
    const finish = (files: File[]): void => {
      if (settled) return;
      settled = true;
      cleanup();
      if (files.length === 0) {
        resolve([]);
        return;
      }
      readPickedFiles(files, directory).then(resolve, reject);
    };
    function onChange(): void {
      finish(Array.from(input.files ?? []));
    }
    function onCancel(): void {
      finish([]);
    }
    function onFocus(): void {
      if (focusTimer !== null) clearTimeout(focusTimer);
      focusTimer = setTimeout(() => {
        // A `change` may still be on its way for big directories; only give up when empty.
        if (!input.files || input.files.length === 0) finish([]);
      }, PICKER_FOCUS_CANCEL_DELAY);
    }

    input.addEventListener('change', onChange);
    input.addEventListener('cancel', onCancel);
    win.addEventListener('focus', onFocus);
    doc.body.appendChild(input);
    try {
      input.click();
    } catch (e) {
      settled = true;
      cleanup();
      reject(e);
    }
  });
}

/** How long the object URL of a download stays valid. */
export const DOWNLOAD_URL_TTL = 60_000;

/**
 * Save `data` as a download named `name` (sanitised). Browser-only. The object URL is
 * revoked after {@link DOWNLOAD_URL_TTL} ms (the download has started by then).
 */
export function downloadFile(name: string, data: Uint8Array, mime: string, doc: Document = document): void {
  const blob = new Blob([toArrayBufferView(data)], { type: mime || 'application/octet-stream' });
  const url = URL.createObjectURL(blob);
  const a = doc.createElement('a');
  a.href = url;
  a.download = sanitizeFileName(name);
  a.rel = 'noopener';
  a.style.display = 'none';
  doc.body.appendChild(a);
  try {
    a.click();
  } finally {
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), DOWNLOAD_URL_TTL);
  }
}
