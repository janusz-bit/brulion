/**
 * Test helper: a case-insensitive, case-preserving view of a MemoryFs, like Android shared
 * storage, FAT, Windows or macOS volumes. Every incoming path is resolved to an existing entry
 * whose name differs only in letter case.
 */
import type { MemoryFs } from '@/core/storage';
import type { FileStat, FileSystemAdapter } from '../types';

export class CaseInsensitiveFs implements FileSystemAdapter {
  readonly kind = 'memory' as const;
  readonly label = 'case-insensitive memory';

  constructor(readonly inner: MemoryFs) {}

  private async resolve(p: string): Promise<string> {
    const lower = p.toLowerCase();
    return (await this.inner.list()).find((s) => s.path.toLowerCase() === lower)?.path ?? p;
  }

  list(): Promise<FileStat[]> {
    return this.inner.list();
  }

  async stat(p: string): Promise<FileStat | null> {
    return this.inner.stat(await this.resolve(p));
  }

  async readText(p: string): Promise<string> {
    return this.inner.readText(await this.resolve(p));
  }

  async readBinary(p: string): Promise<Uint8Array> {
    return this.inner.readBinary(await this.resolve(p));
  }

  async writeText(p: string, content: string): Promise<FileStat> {
    return this.inner.writeText(await this.resolve(p), content);
  }

  async writeBinary(p: string, data: Uint8Array): Promise<FileStat> {
    return this.inner.writeBinary(await this.resolve(p), data);
  }

  async remove(p: string): Promise<void> {
    return this.inner.remove(await this.resolve(p));
  }

  async rename(from: string, to: string): Promise<void> {
    return this.inner.rename(await this.resolve(from), to);
  }

  async mkdir(p: string): Promise<void> {
    return this.inner.mkdir(await this.resolve(p));
  }
}
