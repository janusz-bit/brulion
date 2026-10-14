/**
 * Test helpers for the Tauri implementations (tests only, jsdom environment).
 *
 * `installIpc` uses `mockIPC` from `@tauri-apps/api/mocks` (events mocked, so `listen` /
 * `emit` work) and wraps its `invoke` so handlers also see the invoke options — `mockIPC`
 * drops them, but `fs_write_binary` / `write_export` pass their arguments as headers.
 *
 * `fakeVaultBackend` emulates the Rust `fs_*` commands on top of MemoryFs (one per root),
 * including the `"<CODE>: <message>: <path>"` error strings and raw / base64 bodies.
 */
import { clearMocks, mockIPC } from '@tauri-apps/api/mocks';
import type { InvokeArgs, InvokeOptions } from '@tauri-apps/api/core';
import { MemoryFs } from '@/core/storage';
import { base64ToBytes, toBytes } from './bytes';

export interface IpcCall {
  cmd: string;
  args: InvokeArgs | undefined;
  options: InvokeOptions | undefined;
}

export type IpcHandler = (cmd: string, args: InvokeArgs | undefined, options: InvokeOptions | undefined) => unknown;

interface TauriInternals {
  invoke: (cmd: string, args?: InvokeArgs, options?: InvokeOptions) => Promise<unknown>;
}

/** Install a mock IPC; returns the list of (non-event) calls. */
export function installIpc(handler: IpcHandler): IpcCall[] {
  const calls: IpcCall[] = [];
  mockIPC(() => undefined, { shouldMockEvents: true });
  const internals = (window as unknown as { __TAURI_INTERNALS__: TauriInternals }).__TAURI_INTERNALS__;
  const eventInvoke = internals.invoke;
  internals.invoke = async (cmd, args, options) => {
    if (cmd.startsWith('plugin:event|')) return eventInvoke(cmd, args, options);
    calls.push({ cmd, args, options });
    return handler(cmd, args, options);
  };
  return calls;
}

/** Remove every trace of the Tauri mocks (so later code does not think it runs in Tauri). */
export function uninstallIpc(): void {
  clearMocks();
  const w = window as unknown as Record<string, unknown>;
  delete w.__TAURI_INTERNALS__;
  delete w.__TAURI_EVENT_PLUGIN_INTERNALS__;
  delete w.__TAURI_OS_PLUGIN_INTERNALS__;
}

function rec(args: InvokeArgs | undefined): Record<string, unknown> {
  return (args ?? {}) as Record<string, unknown>;
}

function str(v: unknown): string {
  return typeof v === 'string' ? v : '';
}

function header(options: InvokeOptions | undefined, name: string): string {
  const headers = options?.headers;
  const value = headers instanceof Headers ? headers.get(name) : (headers as Record<string, string> | undefined)?.[name];
  if (value === undefined || value === null) throw `EINVAL: missing header: ${name}`;
  return decodeURIComponent(value);
}

/** Bytes of a raw (`Uint8Array` / number[]) or `{ base64 }` body, copied. */
export function bodyBytes(args: InvokeArgs | undefined): Uint8Array {
  if (args && typeof args === 'object' && !Array.isArray(args) && !ArrayBuffer.isView(args) && !(args instanceof ArrayBuffer)) {
    const b64 = (args as Record<string, unknown>).base64;
    if (typeof b64 === 'string') return base64ToBytes(b64);
  }
  const bytes = toBytes(args);
  if (!bytes) throw 'EINVAL: expected a raw byte body (Uint8Array/ArrayBuffer) or { base64 }: (body)';
  return Uint8Array.from(bytes);
}

export interface FakeVaultBackend {
  /** MemoryFs per root (created on first use). */
  vault(root: string): MemoryFs;
  /** Absolute target -> bytes written by `write_export`. */
  exports: Map<string, Uint8Array>;
  handle: IpcHandler;
}

/** Emulation of the Rust vault commands. Unknown commands return `extra(cmd, args)`. */
export function fakeVaultBackend(opts: { now?: () => number; defaultVaultDir?: string; extra?: IpcHandler } = {}): FakeVaultBackend {
  const vaults = new Map<string, MemoryFs>();
  const exports = new Map<string, Uint8Array>();
  const vault = (root: string): MemoryFs => {
    if (!root.startsWith('/')) throw `EINVAL: vault root must be an absolute path: ${root}`;
    const key = root.replace(/\/+$/, '') || '/';
    let fs = vaults.get(key);
    if (!fs) {
      fs = new MemoryFs({ now: opts.now ?? Date.now });
      vaults.set(key, fs);
    }
    return fs;
  };
  const run = async (cmd: string, args: InvokeArgs | undefined, options: InvokeOptions | undefined): Promise<unknown> => {
    const a = rec(args);
    switch (cmd) {
      case 'fs_list':
        return vault(str(a.root)).list();
      case 'fs_stat':
        return vault(str(a.root)).stat(str(a.path));
      case 'fs_read_text':
        return vault(str(a.root)).readText(str(a.path));
      case 'fs_read_binary': {
        const bytes = await vault(str(a.root)).readBinary(str(a.path));
        return bytes.slice().buffer;
      }
      case 'fs_write_text':
        return vault(str(a.root)).writeText(str(a.path), str(a.content));
      case 'fs_write_binary':
        return vault(header(options, 'x-root')).writeBinary(header(options, 'x-path'), bodyBytes(args));
      case 'fs_remove':
        await vault(str(a.root)).remove(str(a.path));
        return null;
      case 'fs_rename':
        await vault(str(a.root)).rename(str(a.from), str(a.to));
        return null;
      case 'fs_mkdir':
        await vault(str(a.root)).mkdir(str(a.path));
        return null;
      case 'watch_start':
      case 'watch_stop':
      case 'allow_vault_assets':
        return null;
      case 'default_vault_dir':
        return opts.defaultVaultDir ?? '/home/jan/Brulion';
      case 'write_export':
        exports.set(header(options, 'x-path'), bodyBytes(args));
        return null;
      default:
        if (opts.extra) return opts.extra(cmd, args, options);
        throw `unknown command ${cmd}`;
    }
  };
  return {
    vault,
    exports,
    // Rust rejects with plain strings "<CODE>: <message>: <path>".
    handle: (cmd, args, options) =>
      run(cmd, args, options).catch((e: unknown) => {
        throw e instanceof Error ? e.message : e;
      }),
  };
}
