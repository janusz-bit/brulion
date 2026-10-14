/**
 * In-memory fake of the File System Access API subset used by FsAccessFs (tests only).
 * Mimics the browser: NotFoundError / TypeMismatchError / InvalidModificationError
 * DOMExceptions, writables that commit atomically on close(), handles that go stale when
 * their entry is removed, optional `move()` support and permission methods.
 */
import type { FsaDirectoryHandle, FsaFile, FsaFileHandle, FsaPermissionDescriptor, FsaPermissionState, FsaWritable } from './fsAccessFs';

export interface FakeFsOptions {
  now?: () => number;
  /** Which handles support move(): none, files only, or files and directories. */
  move?: 'none' | 'files' | 'all';
  /** Permission state reported by queryPermission (undefined = no permission API). */
  permission?: FsaPermissionState;
  /** State after requestPermission. */
  permissionAfterRequest?: FsaPermissionState;
  /**
   * Case-insensitive, case-preserving names (Windows, macOS, Android shared storage): a lookup
   * of "note.md" finds "Note.md". Default false (Linux).
   */
  caseInsensitive?: boolean;
}

interface FakeContext {
  now: () => number;
  move: 'none' | 'files' | 'all';
  /** Number of createWritable streams committed (for assertions). */
  commits: number;
  moves: number;
  caseInsensitive: boolean;
}

const encoder = new TextEncoder();
const decoder = new TextDecoder();

function domError(name: string, message = name): DOMException {
  return new DOMException(message, name);
}

class FakeFile implements FsaFile {
  constructor(
    private readonly bytes: Uint8Array,
    readonly lastModified: number,
  ) {}

  get size(): number {
    return this.bytes.byteLength;
  }

  async text(): Promise<string> {
    return decoder.decode(this.bytes);
  }

  async arrayBuffer(): Promise<ArrayBuffer> {
    return this.bytes.slice().buffer;
  }
}

abstract class FakeHandleBase {
  parent: FakeDirectoryHandle | null;
  name: string;
  removed = false;

  constructor(
    name: string,
    parent: FakeDirectoryHandle | null,
    protected readonly ctx: FakeContext,
  ) {
    this.name = name;
    this.parent = parent;
  }

  protected assertAlive(): void {
    if (this.removed) throw domError('NotFoundError', `${this.name} was removed`);
  }

  markRemoved(): void {
    this.removed = true;
  }
}

export class FakeFileHandle extends FakeHandleBase implements FsaFileHandle {
  readonly kind = 'file' as const;
  data: Uint8Array = new Uint8Array();
  mtime: number;
  declare move?: (destination: FsaDirectoryHandle, newName: string) => Promise<void>;

  constructor(name: string, parent: FakeDirectoryHandle, ctx: FakeContext) {
    super(name, parent, ctx);
    this.mtime = ctx.now();
    if (ctx.move !== 'none') {
      this.move = async (destination, newName) => {
        this.assertAlive();
        const dest = destination as FakeDirectoryHandle;
        const found = dest.find(newName);
        const existing = found?.[1] === this ? undefined : found?.[1];
        if (existing && existing.kind === 'directory') throw domError('InvalidModificationError');
        this.parent?.children.delete(this.name);
        if (existing) {
          dest.children.delete(found![0]);
          existing.markRemoved();
        }
        dest.children.set(newName, this);
        this.parent = dest;
        this.name = newName;
        ctx.moves++;
      };
    }
  }

  async getFile(): Promise<FsaFile> {
    this.assertAlive();
    return new FakeFile(this.data.slice(), this.mtime);
  }

  async createWritable(): Promise<FsaWritable> {
    this.assertAlive();
    const chunks: Uint8Array[] = [];
    let closed = false;
    return {
      write: async (data: string | Uint8Array) => {
        if (closed) throw domError('InvalidStateError');
        chunks.push(typeof data === 'string' ? encoder.encode(data) : data.slice());
      },
      close: async () => {
        if (closed) throw domError('InvalidStateError');
        closed = true;
        const total = chunks.reduce((n, c) => n + c.byteLength, 0);
        const out = new Uint8Array(total);
        let offset = 0;
        for (const c of chunks) {
          out.set(c, offset);
          offset += c.byteLength;
        }
        this.assertAlive();
        // Atomic commit: the visible content changes only now.
        this.data = out;
        this.mtime = this.ctx.now();
        this.ctx.commits++;
      },
      abort: async () => {
        closed = true;
      },
    };
  }
}

export class FakeDirectoryHandle extends FakeHandleBase implements FsaDirectoryHandle {
  readonly kind = 'directory' as const;
  readonly children = new Map<string, FakeFileHandle | FakeDirectoryHandle>();
  declare move?: (destination: FsaDirectoryHandle, newName: string) => Promise<void>;
  permission: FsaPermissionState | undefined;
  permissionAfterRequest: FsaPermissionState | undefined;
  declare queryPermission?: (desc?: FsaPermissionDescriptor) => Promise<FsaPermissionState>;
  declare requestPermission?: (desc?: FsaPermissionDescriptor) => Promise<FsaPermissionState>;

  constructor(name: string, parent: FakeDirectoryHandle | null, ctx: FakeContext) {
    super(name, parent, ctx);
    if (ctx.move === 'all') {
      this.move = async (destination, newName) => {
        this.assertAlive();
        const dest = destination as FakeDirectoryHandle;
        const found = dest.find(newName);
        if (found && found[1] !== this) throw domError('InvalidModificationError');
        this.parent?.children.delete(this.name);
        dest.children.set(newName, this);
        this.parent = dest;
        this.name = newName;
        ctx.moves++;
      };
    } else if (ctx.move === 'files') {
      this.move = async () => {
        throw domError('NotSupportedError', 'Directory moves are not supported');
      };
    }
  }

  /** Context shared by all handles of this fake tree. */
  get context(): FakeContext {
    return this.ctx;
  }

  /** Child entry by name (case-insensitively when the fake is configured so): [stored name, entry]. */
  find(name: string): [string, FakeFileHandle | FakeDirectoryHandle] | undefined {
    const exact = this.children.get(name);
    if (exact) return [name, exact];
    if (!this.ctx.caseInsensitive) return undefined;
    const lower = name.toLowerCase();
    for (const entry of this.children) if (entry[0].toLowerCase() === lower) return entry;
    return undefined;
  }

  async getDirectoryHandle(name: string, options?: { create?: boolean }): Promise<FakeDirectoryHandle> {
    this.assertAlive();
    validName(name);
    const existing = this.find(name)?.[1];
    if (existing) {
      if (existing.kind !== 'directory') throw domError('TypeMismatchError');
      return existing;
    }
    if (!options?.create) throw domError('NotFoundError', `${name} not found`);
    const dir = new FakeDirectoryHandle(name, this, this.ctx);
    this.children.set(name, dir);
    return dir;
  }

  async getFileHandle(name: string, options?: { create?: boolean }): Promise<FakeFileHandle> {
    this.assertAlive();
    validName(name);
    const existing = this.find(name)?.[1];
    if (existing) {
      if (existing.kind !== 'file') throw domError('TypeMismatchError');
      return existing;
    }
    if (!options?.create) throw domError('NotFoundError', `${name} not found`);
    const file = new FakeFileHandle(name, this, this.ctx);
    this.children.set(name, file);
    return file;
  }

  async removeEntry(name: string, options?: { recursive?: boolean }): Promise<void> {
    this.assertAlive();
    const found = this.find(name);
    if (!found) throw domError('NotFoundError', `${name} not found`);
    const [stored, entry] = found;
    if (entry.kind === 'directory' && entry.children.size > 0 && !options?.recursive) throw domError('InvalidModificationError');
    this.children.delete(stored);
    entry.markRemoved();
  }

  async *entries(): AsyncIterableIterator<[string, FakeFileHandle | FakeDirectoryHandle]> {
    this.assertAlive();
    // Snapshot first: entries added/removed while iterating are not visited.
    for (const entry of Array.from(this.children.entries())) yield entry;
  }

  override markRemoved(): void {
    this.removed = true;
    for (const c of this.children.values()) c.markRemoved();
  }
}

function validName(name: string): void {
  if (name === '' || name === '.' || name === '..' || name.includes('/') || name.includes('\\')) throw new TypeError(`Invalid name: ${name}`);
}

/** Create a fake root directory handle. */
export function createFakeRoot(name = 'Notatki', opts: FakeFsOptions = {}): FakeDirectoryHandle {
  const ctx: FakeContext = {
    now: opts.now ?? Date.now,
    move: opts.move ?? 'files',
    commits: 0,
    moves: 0,
    caseInsensitive: opts.caseInsensitive ?? false,
  };
  const root = new FakeDirectoryHandle(name, null, ctx);
  if (opts.permission !== undefined) {
    root.permission = opts.permission;
    root.permissionAfterRequest = opts.permissionAfterRequest ?? opts.permission;
    root.queryPermission = async () => root.permission!;
    root.requestPermission = async () => {
      root.permission = root.permissionAfterRequest;
      return root.permission!;
    };
  }
  return root;
}

/** Write a file directly into the fake tree (simulates another program). */
export async function fakeWrite(root: FakeDirectoryHandle, path: string, content: string | Uint8Array): Promise<void> {
  const parts = path.split('/');
  const name = parts.pop()!;
  let dir = root;
  for (const p of parts) dir = await dir.getDirectoryHandle(p, { create: true });
  const fh = await dir.getFileHandle(name, { create: true });
  const w = await fh.createWritable();
  await w.write(content);
  await w.close();
}
