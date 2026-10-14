import { describe, expect, it } from 'vitest';
import { isConflictCopy } from '@/core/paths';
import type { FileStat } from '../types';
import { conflictCopyPath, conflictStamp, findSyncthingConflicts, parseBrulionConflict, sanitizeDeviceName } from './conflicts';

const T = new Date(2026, 9, 14, 9, 5, 7).getTime();
const file = (path: string): FileStat => ({ path, kind: 'file', size: 1, mtime: 0 });

describe('conflict copy names', () => {
  it('formats a local yyyyMMdd-HHmmss stamp', () => {
    expect(conflictStamp(T)).toBe('20261014-090507');
  });

  it('builds "Name.conflict-<device>-<stamp>.ext" next to the original', () => {
    expect(conflictCopyPath('Notatka.md', 'laptop', T)).toBe('Notatka.conflict-laptop-20261014-090507.md');
    expect(conflictCopyPath('journals/2026-10-14.md', 'Telefon', T)).toBe('journals/2026-10-14.conflict-Telefon-20261014-090507.md');
    expect(conflictCopyPath('assets/zdjęcie.png', 'pc', T)).toBe('assets/zdjęcie.conflict-pc-20261014-090507.png');
    expect(conflictCopyPath('README', 'pc', T)).toBe('README.conflict-pc-20261014-090507');
    expect(conflictCopyPath('a.b/.hidden', 'pc', T)).toBe('a.b/.hidden.conflict-pc-20261014-090507');
  });

  it('the produced Markdown names are recognised by paths.isConflictCopy', () => {
    expect(isConflictCopy(conflictCopyPath('Strona.md', 'Mój laptop', T))).toBe(true);
  });

  it('sanitises device names (keeps Polish letters)', () => {
    expect(sanitizeDeviceName('Mój laptop')).toBe('Mój-laptop');
    expect(sanitizeDeviceName('  Pixel 8 / Łukasz  ')).toBe('Pixel-8-Łukasz');
    expect(sanitizeDeviceName('a.b:c')).toBe('a-b-c');
    expect(sanitizeDeviceName('///')).toBe('urzadzenie');
    expect(sanitizeDeviceName('x'.repeat(100))).toHaveLength(40);
  });
});

describe('parseBrulionConflict', () => {
  it('parses original, device (with dashes) and time', () => {
    expect(parseBrulionConflict('dir/Zażółć.conflict-Mój-laptop-20261014-090507.md')).toEqual({
      original: 'dir/Zażółć.md',
      device: 'Mój-laptop',
      time: '2026-10-14T09:05:07',
    });
  });

  it('accepts a uniqueness suffix and binary extensions', () => {
    expect(parseBrulionConflict('a.conflict-pc-20261014-090507 2.md')?.original).toBe('a.md');
    expect(parseBrulionConflict('img.conflict-pc-20261014-090507.png')?.original).toBe('img.png');
  });

  it('round-trips conflictCopyPath', () => {
    const copy = conflictCopyPath('Projekty/Plan.md', 'Telefon Ani', T);
    expect(parseBrulionConflict(copy)).toEqual({ original: 'Projekty/Plan.md', device: 'Telefon-Ani', time: '2026-10-14T09:05:07' });
  });

  it('rejects other names', () => {
    expect(parseBrulionConflict('Notatka.md')).toBeNull();
    expect(parseBrulionConflict('Notatka.conflict-.md')).toBeNull();
    expect(parseBrulionConflict('.conflict-pc-20261014-090507.md')).toBeNull();
    expect(parseBrulionConflict('Notatka.conflict-pc-2026101-090507.md')).toBeNull();
  });
});

describe('findSyncthingConflicts', () => {
  it('finds Syncthing and Brulion conflict copies, sorted', () => {
    const files = [
      file('b.md'),
      file('b.conflict-Telefon-20261015-080000.md'),
      file('a.sync-conflict-20261014-101010-ABCDEFG.md'),
      file('a.md'),
      file('a.conflict-pc-20261013-120000.md'),
      file('zdjęcia/kot.sync-conflict-20261014-101010-XYZ1234.jpg'),
      { path: 'x.sync-conflict-20261014-101010-ABCDEFG', kind: 'dir', size: 0, mtime: 0 } as FileStat,
    ];
    expect(findSyncthingConflicts(files)).toEqual([
      { original: 'a.md', copy: 'a.conflict-pc-20261013-120000.md', time: '2026-10-13T12:00:00', device: 'pc', source: 'brulion' },
      { original: 'a.md', copy: 'a.sync-conflict-20261014-101010-ABCDEFG.md', time: '2026-10-14T10:10:10', device: 'ABCDEFG', source: 'syncthing' },
      { original: 'b.md', copy: 'b.conflict-Telefon-20261015-080000.md', time: '2026-10-15T08:00:00', device: 'Telefon', source: 'brulion' },
      { original: 'zdjęcia/kot.jpg', copy: 'zdjęcia/kot.sync-conflict-20261014-101010-XYZ1234.jpg', time: '2026-10-14T10:10:10', device: 'XYZ1234', source: 'syncthing' },
    ]);
  });

  it('returns [] when there are none', () => {
    expect(findSyncthingConflicts([file('a.md'), file('b/c.md')])).toEqual([]);
    expect(findSyncthingConflicts([])).toEqual([]);
  });
});
