// @vitest-environment jsdom
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { acceptAttribute, downloadFile, DOWNLOAD_URL_TTL, pickedPaths, pickFilesWithInput, PICKER_FOCUS_CANCEL_DELAY, readPickedFiles } from './web-files';

const dec = new TextDecoder();

function file(name: string, content: string, relativePath?: string): File {
  const f = new File([content], name, { type: 'text/markdown' });
  if (relativePath !== undefined) Object.defineProperty(f, 'webkitRelativePath', { value: relativePath });
  return f;
}

/** Capture the hidden input when the picker clicks it. */
function captureInput(): { get: () => HTMLInputElement } {
  const click = vi.spyOn(HTMLInputElement.prototype, 'click').mockImplementation(() => {});
  return {
    get: () => {
      const input = click.mock.contexts.at(-1) as HTMLInputElement | undefined;
      if (!input) throw new Error('picker input was not clicked');
      return input;
    },
  };
}

function choose(input: HTMLInputElement, files: File[]): void {
  Object.defineProperty(input, 'files', { value: files, configurable: true });
  input.dispatchEvent(new Event('change'));
}

describe('acceptAttribute', () => {
  it('normalises extensions and keeps MIME types', () => {
    expect(acceptAttribute(['.md', 'markdown', ' txt ', 'text/html', 'image/*', '.md', ''])).toBe('.md,.markdown,.txt,text/html,image/*');
    expect(acceptAttribute(undefined)).toBe('');
    expect(acceptAttribute([])).toBe('');
  });
});

describe('pickedPaths', () => {
  it('uses file names without webkitRelativePath', () => {
    expect(pickedPaths([{ name: 'Żółw.md' }, { name: 'b.md', webkitRelativePath: '' }], false)).toEqual(['Żółw.md', 'b.md']);
  });

  it('strips the picked folder name for directory picks', () => {
    const files = [
      { name: 'a.md', webkitRelativePath: 'Mój skarbiec/a.md' },
      { name: 'b.md', webkitRelativePath: 'Mój skarbiec/Książki/b.md' },
      { name: 'app.json', webkitRelativePath: 'Mój skarbiec/.obsidian/app.json' },
    ];
    expect(pickedPaths(files, true)).toEqual(['a.md', 'Książki/b.md', '.obsidian/app.json']);
    expect(pickedPaths(files, false)).toEqual(['Mój skarbiec/a.md', 'Mój skarbiec/Książki/b.md', 'Mój skarbiec/.obsidian/app.json']);
  });

  it('keeps paths without a shared root and converts backslashes', () => {
    expect(pickedPaths([{ name: 'a', webkitRelativePath: 'x/a' }, { name: 'b', webkitRelativePath: 'y/b' }], true)).toEqual(['x/a', 'y/b']);
    expect(pickedPaths([{ name: 'a', webkitRelativePath: 'Dir\\sub\\a' }], true)).toEqual(['sub/a']);
    expect(pickedPaths([], true)).toEqual([]);
  });
});

describe('readPickedFiles', () => {
  it('reads bytes with Polish content', async () => {
    const out = await readPickedFiles([file('notatka.md', 'Zażółć gęślą jaźń')], false);
    expect(out).toHaveLength(1);
    expect(out[0]!.path).toBe('notatka.md');
    expect(dec.decode(out[0]!.data)).toBe('Zażółć gęślą jaźń');
  });
});

describe('pickFilesWithInput', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });
  afterEach(() => {
    vi.useRealTimers();
    vi.restoreAllMocks();
  });

  it('resolves the chosen files and removes the input', async () => {
    const cap = captureInput();
    const p = pickFilesWithInput({ accept: ['md', '.txt'], multiple: true });
    const input = cap.get();
    expect(input.type).toBe('file');
    expect(input.multiple).toBe(true);
    expect(input.accept).toBe('.md,.txt');
    expect(input.isConnected).toBe(true);
    choose(input, [file('a.md', '# A'), file('Łódź.txt', 'miasto')]);
    const files = await p;
    expect(files.map((f) => f.path)).toEqual(['a.md', 'Łódź.txt']);
    expect(dec.decode(files[1]!.data)).toBe('miasto');
    expect(input.isConnected).toBe(false);
  });

  it('configures directory picks and strips the folder name', async () => {
    const cap = captureInput();
    const p = pickFilesWithInput({ directory: true, accept: ['.md'] });
    const input = cap.get();
    expect(input.webkitdirectory).toBe(true);
    expect(input.hasAttribute('webkitdirectory')).toBe(true);
    expect(input.multiple).toBe(true);
    expect(input.accept).toBe('');
    choose(input, [file('a.md', 'A', 'Logseq graf/pages/a.md'), file('config.edn', '{}', 'Logseq graf/logseq/config.edn')]);
    expect((await p).map((f) => f.path)).toEqual(['pages/a.md', 'logseq/config.edn']);
  });

  it('resolves [] on the cancel event', async () => {
    const cap = captureInput();
    const p = pickFilesWithInput({});
    const input = cap.get();
    input.dispatchEvent(new Event('cancel'));
    expect(await p).toEqual([]);
    expect(input.isConnected).toBe(false);
  });

  it('resolves [] when the window regains focus without files', async () => {
    const cap = captureInput();
    const p = pickFilesWithInput({});
    cap.get();
    window.dispatchEvent(new Event('focus'));
    await vi.advanceTimersByTimeAsync(PICKER_FOCUS_CANCEL_DELAY + 10);
    expect(await p).toEqual([]);
  });

  it('waits for a late change after focus when files are present', async () => {
    const cap = captureInput();
    const p = pickFilesWithInput({ multiple: true });
    const input = cap.get();
    Object.defineProperty(input, 'files', { value: [file('a.md', 'a')], configurable: true });
    window.dispatchEvent(new Event('focus'));
    await vi.advanceTimersByTimeAsync(PICKER_FOCUS_CANCEL_DELAY + 10);
    input.dispatchEvent(new Event('change'));
    expect((await p).map((f) => f.path)).toEqual(['a.md']);
  });

  it('resolves only once', async () => {
    const cap = captureInput();
    const p = pickFilesWithInput({});
    const input = cap.get();
    input.dispatchEvent(new Event('cancel'));
    choose(input, [file('a.md', 'a')]);
    expect(await p).toEqual([]);
  });

  it('treats an empty change as cancel', async () => {
    const cap = captureInput();
    const p = pickFilesWithInput({});
    choose(cap.get(), []);
    expect(await p).toEqual([]);
  });

  it('rejects when the input cannot be clicked', async () => {
    vi.spyOn(HTMLInputElement.prototype, 'click').mockImplementation(() => {
      throw new Error('blocked');
    });
    await expect(pickFilesWithInput({})).rejects.toThrow('blocked');
    expect(document.querySelectorAll('input[type=file]')).toHaveLength(0);
  });
});

describe('downloadFile', () => {
  afterEach(() => {
    vi.useRealTimers();
    vi.restoreAllMocks();
  });

  it('downloads through an object URL and revokes it later', async () => {
    vi.useFakeTimers();
    const create = vi.fn((_b: Blob) => 'blob:brulion/1');
    const revoke = vi.fn();
    Object.assign(URL, { createObjectURL: create, revokeObjectURL: revoke });
    const click = vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(() => {});
    downloadFile('Eksport: notatki/2026?.zip', new TextEncoder().encode('zażółć'), 'application/zip');
    expect(click).toHaveBeenCalledTimes(1);
    const a = click.mock.contexts[0] as HTMLAnchorElement;
    expect(a.download).toBe('Eksport notatki 2026 .zip');
    expect(a.href).toBe('blob:brulion/1');
    expect(a.isConnected).toBe(false);
    const blob = create.mock.calls[0]![0];
    expect(blob.type).toBe('application/zip');
    expect(blob.size).toBe(new TextEncoder().encode('zażółć').byteLength);
    expect(revoke).not.toHaveBeenCalled();
    await vi.advanceTimersByTimeAsync(DOWNLOAD_URL_TTL);
    expect(revoke).toHaveBeenCalledWith('blob:brulion/1');
  });

  it('defaults the MIME type', () => {
    const create = vi.fn((_b: Blob) => 'blob:x');
    Object.assign(URL, { createObjectURL: create, revokeObjectURL: vi.fn() });
    vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(() => {});
    downloadFile('a.bin', new Uint8Array([1]), '');
    expect(create.mock.calls[0]![0].type).toBe('application/octet-stream');
  });
});

describe('pickedPaths review fixes', () => {
  it('returns NFC paths (files copied from macOS carry NFD names)', () => {
    const nfd = (s: string) => s.normalize('NFD');
    expect(pickedPaths([{ name: nfd('Żółw.md') }], false)).toEqual(['Żółw.md']);
    expect(pickedPaths([{ name: 'x', webkitRelativePath: nfd('Książki/Pan Tadeusz.md') }, { name: 'y', webkitRelativePath: 'Książki/Lalka.md' }], true)).toEqual([
      'Pan Tadeusz.md',
      'Lalka.md',
    ]);
  });
});
