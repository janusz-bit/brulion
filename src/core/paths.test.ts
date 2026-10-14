import { describe, expect, it } from 'vitest';
import { isIgnoredPath, normalizePath, parseSyncthingConflict, uniquePath } from './paths';

describe('paths', () => {
  it('normalizes', () => {
    expect(normalizePath('/a//b/./c/../d.md')).toBe('a/b/d.md');
    expect(normalizePath('a\\b')).toBe('a/b');
  });
  it('ignores tool folders', () => {
    expect(isIgnoredPath('.obsidian/app.json')).toBe(true);
    expect(isIgnoredPath('notes/.stversions/x.md')).toBe(true);
    expect(isIgnoredPath('notes/~syncthing~x.md.tmp')).toBe(true);
    expect(isIgnoredPath('journals/2026-10-14.md')).toBe(false);
  });
  it('parses syncthing conflicts', () => {
    expect(parseSyncthingConflict('a/Note.sync-conflict-20261014-101112-ABCDEFG.md')).toEqual({
      original: 'a/Note.md',
      time: '2026-10-14T10:11:12',
      device: 'ABCDEFG',
    });
    expect(parseSyncthingConflict('a/Note.md')).toBeNull();
  });
  it('makes unique paths', () => {
    const taken = new Set(['a.md', 'a 2.md']);
    expect(uniquePath('a.md', (p) => taken.has(p))).toBe('a 3.md');
  });
});
