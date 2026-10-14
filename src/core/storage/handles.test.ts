import { describe, expect, it } from 'vitest';
import { deleteDirectoryHandle, listDirectoryHandles, loadDirectoryHandle, saveDirectoryHandle } from './handles';

// Real FileSystemDirectoryHandles are structured-cloneable host objects; in tests a plain
// object stands in for one (fake-indexeddb clones it with structuredClone).
const fakeHandle = (name: string): FileSystemDirectoryHandle => ({ kind: 'directory', name }) as unknown as FileSystemDirectoryHandle;

describe('directory handle persistence', () => {
  it('saves and loads handles by id', async () => {
    await saveDirectoryHandle('vault-1', fakeHandle('Notatki'));
    const loaded = await loadDirectoryHandle('vault-1');
    expect(loaded).toMatchObject({ kind: 'directory', name: 'Notatki' });
  });

  it('returns null for unknown ids', async () => {
    expect(await loadDirectoryHandle('nope')).toBeNull();
  });

  it('overwrites, lists and deletes handles', async () => {
    await saveDirectoryHandle('h-a', fakeHandle('A'));
    await saveDirectoryHandle('h-b', fakeHandle('Bąk'));
    await saveDirectoryHandle('h-a', fakeHandle('A2'));
    expect(await loadDirectoryHandle('h-a')).toMatchObject({ name: 'A2' });
    expect(await listDirectoryHandles()).toEqual(expect.arrayContaining(['h-a', 'h-b']));
    await deleteDirectoryHandle('h-a');
    expect(await loadDirectoryHandle('h-a')).toBeNull();
    expect(await loadDirectoryHandle('h-b')).toMatchObject({ name: 'Bąk' });
  });
});
