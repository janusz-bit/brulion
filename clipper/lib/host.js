// Brulion's native messaging host (docs/CLIPPER.md "The host"): brulion-cli, which the browser starts
// when the clipper sends it a message (the host manifest that `brulion-cli clip install` writes tells
// the browser where it is). Short requests (hello, folders, pages, headings, preview, open) are one
// message and one answer; a clip goes over a port, so the host can tell how its pictures download.

import { api } from './api.js';

export const HOST_NAME = 'io.github.januszbit.brulion';
/** The messages' version this extension speaks (the host's `hello` says its own). */
export const PROTOCOL = 1;

/** A failure the host reported (`code` from it), or `not_connected` when there is no host. */
export class HostError extends Error {
  constructor(code, message, details) {
    super(message || code);
    this.name = 'HostError';
    this.code = code;
    this.details = details ?? {};
  }
}

let nextId = 1;
function newId() {
  return `${Date.now().toString(36)}-${(nextId++).toString(36)}`;
}

/** The browser's text for a host that cannot be started: no manifest, or no program. */
function connectionError(error) {
  const text = String(error?.message ?? error ?? '');
  return new HostError('not_connected', text);
}

/** One request, one answer. Rejects with HostError. */
export async function request(type, fields = {}) {
  const message = { ...fields, type, id: newId() };
  let answer;
  try {
    answer = await api.runtime.sendNativeMessage(HOST_NAME, message);
  } catch (error) {
    throw connectionError(error);
  }
  if (!answer || typeof answer !== 'object') throw new HostError('no_answer', 'The host gave no answer');
  if (answer.type === 'error') throw new HostError(answer.code, answer.message, answer.details);
  return answer;
}

/**
 * Save a clip: `{vault, clip}` over a port; `onProgress(done, total)` while pictures download.
 * Resolves with the host's result ({path, title, …}).
 */
export function clip(message, onProgress) {
  return new Promise((resolve, reject) => {
    let port;
    try {
      port = api.runtime.connectNative(HOST_NAME);
    } catch (error) {
      reject(connectionError(error));
      return;
    }
    const id = newId();
    let settled = false;
    port.onMessage.addListener((answer) => {
      if (!answer || answer.id !== id) return;
      if (answer.type === 'progress') {
        onProgress?.(answer.done, answer.total);
        return;
      }
      settled = true;
      port.disconnect();
      if (answer.type === 'error') reject(new HostError(answer.code, answer.message, answer.details));
      else resolve({ ...answer.result, warnings: answer.warnings ?? [] });
    });
    port.onDisconnect.addListener((p) => {
      if (settled) return;
      settled = true;
      reject(connectionError(p?.error ?? api.runtime.lastError ?? 'The host ended'));
    });
    port.postMessage({ ...message, type: 'clip', id, progress: true });
  });
}
