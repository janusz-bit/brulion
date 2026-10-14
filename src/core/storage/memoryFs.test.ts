import { describe, expect, it, vi } from 'vitest';
import type { FsEvent } from '../types';
import { describeAdapterContract, makeClock } from './adapterContract.testutil';
import { isFsError } from './errors';
import { MemoryFs } from './memoryFs';

describeAdapterContract('MemoryFs', (clock) => new MemoryFs({ now: clock.now }));

describe('MemoryFs specifics', () => {
  it('accepts initial files and a label', async () => {
    const fs = new MemoryFs({ label: 'Demo', files: { 'a/b.md': 'B', 'c.bin': new Uint8Array([1]) } });
    expect(fs.label).toBe('Demo');
    expect(fs.kind).toBe('memory');
    expect(await fs.readText('a/b.md')).toBe('B');
    expect(fs.dump()).toEqual({ 'a/b.md': 'B', 'c.bin': new Uint8Array([1]) });
    expect((await fs.list()).map((s) => s.path)).toEqual(['a', 'a/b.md', 'c.bin']);
  });

  it('uses the injected clock for mtimes', async () => {
    const clock = makeClock(1000);
    const fs = new MemoryFs({ now: clock.now });
    expect((await fs.writeText('x.md', 'x')).mtime).toBe(1000);
    clock.advance(10);
    expect((await fs.writeText('x.md', 'y')).mtime).toBe(1010);
  });

  it('preserves mtime on rename', async () => {
    const clock = makeClock(500);
    const fs = new MemoryFs({ now: clock.now });
    await fs.writeText('a.md', 'a');
    clock.advance(100);
    await fs.rename('a.md', 'b.md');
    expect((await fs.stat('b.md'))?.mtime).toBe(500);
  });

  it('allows renaming a directory onto an empty directory', async () => {
    const fs = new MemoryFs({ files: { 'src/a.md': 'a' } });
    await fs.mkdir('dest');
    await fs.rename('src', 'dest');
    expect(await fs.readText('dest/a.md')).toBe('a');
  });

  it('refuses renaming a directory onto a non-empty directory or a file', async () => {
    const fs = new MemoryFs({ files: { 'src/a.md': 'a', 'dest/b.md': 'b', 'f.md': 'f' } });
    expect(isFsError(await fs.rename('src', 'dest').catch((e: unknown) => e), 'EEXIST')).toBe(true);
    expect(isFsError(await fs.rename('src', 'f.md').catch((e: unknown) => e), 'EEXIST')).toBe(true);
  });

  it('reports external changes to watchers in one batch', async () => {
    const fs = new MemoryFs();
    const batches: FsEvent[][] = [];
    const stop = fs.watch((e) => batches.push(e));
    fs.externalWrite('a.md', 'A');
    fs.externalWrite('a.md', 'A2');
    fs.externalMkdir('folder');
    fs.externalRename('a.md', 'folder/a.md');
    fs.externalRemove('folder');
    await fs.flushEvents();
    expect(batches).toHaveLength(1);
    expect(batches[0]).toEqual([
      { type: 'create', path: 'a.md' },
      { type: 'modify', path: 'a.md' },
      { type: 'create', path: 'folder' },
      { type: 'rename', path: 'folder/a.md', oldPath: 'a.md' },
      { type: 'delete', path: 'folder' },
      { type: 'delete', path: 'folder/a.md' },
    ]);
    stop();
    fs.externalWrite('b.md', 'B');
    await fs.flushEvents();
    expect(batches).toHaveLength(1);
  });

  it('silent external changes are not reported', async () => {
    const fs = new MemoryFs();
    const cb = vi.fn();
    fs.watch(cb);
    fs.externalWrite('a.md', 'A', { silent: true });
    fs.externalRemove('a.md', { silent: true });
    await fs.flushEvents();
    expect(cb).not.toHaveBeenCalled();
  });

  it('does not report own writes unless asked to', async () => {
    const quiet = new MemoryFs();
    const cb = vi.fn();
    quiet.watch(cb);
    await quiet.writeText('a.md', 'x');
    await quiet.flushEvents();
    expect(cb).not.toHaveBeenCalled();

    const loud = new MemoryFs({ reportOwnWrites: true });
    const events: FsEvent[] = [];
    loud.watch((e) => events.push(...e));
    await loud.writeText('a.md', 'x');
    await loud.writeText('a.md', 'y');
    await loud.writeBinary('b.bin', new Uint8Array([1]));
    await loud.mkdir('d');
    await loud.rename('a.md', 'd/a.md');
    await loud.remove('b.bin');
    await loud.flushEvents();
    expect(events.map((e) => `${e.type}:${e.path}`)).toEqual([
      'create:a.md',
      'modify:a.md',
      'create:b.bin',
      'create:d',
      'rename:d/a.md',
      'delete:b.bin',
    ]);
  });

  it('a throwing watcher does not break other watchers', async () => {
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
    const fs = new MemoryFs();
    const good = vi.fn();
    fs.watch(() => {
      throw new Error('boom');
    });
    fs.watch(good);
    fs.externalWrite('x.md', 'x');
    await fs.flushEvents();
    expect(good).toHaveBeenCalledTimes(1);
    warn.mockRestore();
  });

  it('external writes copy byte arrays', async () => {
    const fs = new MemoryFs();
    const bytes = new Uint8Array([5]);
    fs.externalWrite('x.bin', bytes);
    bytes[0] = 6;
    expect([...(await fs.readBinary('x.bin'))]).toEqual([5]);
  });
});
