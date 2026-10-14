// @vitest-environment jsdom
import './testutil';
import { EditorSelection, EditorState } from '@codemirror/state';
import { EditorView } from '@codemirror/view';
import { afterEach, describe, expect, it } from 'vitest';
import { createTranslator } from '@/core/i18n';
import { contextFor, editorContext } from './context';
import { FakeWorkspace } from './fake-workspace';
import { attachmentLink, attachmentName, handlePaste, htmlToMarkdown, isRichHtml, listPaste } from './paste';
import { brulionLanguage } from './setup';
import { rangeTracker } from './tracker';

describe('HTML → Markdown', () => {
  it('converts formatting, links and headings', () => {
    const md = htmlToMarkdown('<h2>Plan</h2><p>To jest <strong>ważne</strong> i <em>pilne</em>, zob. <a href="https://example.com">stronę</a>.</p>');
    expect(md).toBe('## Plan\n\nTo jest **ważne** i *pilne*, zob. [stronę](https://example.com).');
  });
  it('converts lists with nesting and numbering', () => {
    const md = htmlToMarkdown('<ul><li>jeden<ul><li>pod</li></ul></li><li>dwa</li></ul><ol start="3"><li>trzy</li><li>cztery</li></ol>');
    expect(md).toBe('- jeden\n\t- pod\n- dwa\n\n3. trzy\n4. cztery');
  });
  it('converts GFM task lists, strikethrough and highlights', () => {
    const md = htmlToMarkdown(
      '<ul><li><input type="checkbox" checked disabled> zrobione</li><li><input type="checkbox"> do zrobienia</li></ul><p><del>stare</del> <mark>nowe</mark></p>',
    );
    expect(md).toBe('- [x] zrobione\n- [ ] do zrobienia\n\n~~stare~~ ==nowe==');
  });
  it('converts tables', () => {
    const md = htmlToMarkdown(
      '<table><thead><tr><th>Produkt</th><th>Cena</th></tr></thead><tbody><tr><td>Mleko</td><td>3,50 zł</td></tr><tr><td>a|b</td><td><b>1</b></td></tr></tbody></table>',
    );
    expect(md).toBe('| Produkt | Cena |\n| --- | --- |\n| Mleko | 3,50 zł |\n| a\\|b | **1** |');
  });
  it('drops styles and scripts', () => {
    expect(htmlToMarkdown('<style>p{}</style><p>tekst</p><script>alert(1)</script>')).toBe('tekst');
  });
  it('decides when HTML is worth converting', () => {
    expect(isRichHtml('<meta charset="utf-8"><div><span style="color:red">kod</span></div>')).toBe(false);
    expect(isRichHtml('<p>a</p>')).toBe(true);
    expect(isRichHtml('<b>x</b>')).toBe(true);
  });
});

describe('listPaste', () => {
  it('turns plain lines into sibling bullets at the item level', () => {
    expect(listPaste('pierwsza\ndruga\n\ntrzecia', '\t- ', 3)).toEqual({ from: 3, to: 3, insert: 'pierwsza\n\t- druga\n\t- trzecia' });
  });
  it('re-indents pasted list items keeping relative nesting', () => {
    expect(listPaste('a\n- b\n  - c', '- x ', 4)).toEqual({ from: 4, to: 4, insert: 'a\n- b\n  - c' });
    expect(listPaste('- a\n\t- b', '\t- ', 3)).toEqual({ from: 0, to: 3, insert: '\t- a\n\t\t- b' });
  });
  it('does nothing outside lists or for single lines', () => {
    expect(listPaste('a\nb', 'tekst', 5)).toBeNull();
    expect(listPaste('jedna linia', '- ', 2)).toBeNull();
  });
  it('a list pasted at the end of a non-empty item follows it as siblings', () => {
    expect(listPaste('- a\n- b', '- x', 3)).toEqual({ from: 3, to: 3, insert: '\n- a\n- b' });
    expect(listPaste('- a\n\t- b', '\t- zakupy ', 10)).toEqual({ from: 10, to: 10, insert: '\n\t- a\n\t\t- b' });
  });
  it('a list pasted inside an item text does not paste its first marker into the text', () => {
    expect(listPaste('- a\n- b', '- xy', 3)).toEqual({ from: 3, to: 3, insert: 'a\n- b' });
  });
  it('handles CRLF clipboard text', () => {
    expect(listPaste('a\r\nb\r\n', '- ', 2)).toEqual({ from: 2, to: 2, insert: 'a\n- b' });
  });
});

describe('attachments', () => {
  const now = new Date(2026, 9, 14, 10, 30, 5);
  const label = (s: string) => `Wklejony obraz ${s}`;
  it('names generic clipboard images with a timestamp', () => {
    expect(attachmentName({ name: 'image.png', type: 'image/png' }, now, label)).toBe('Wklejony obraz 20261014103005.png');
    expect(attachmentName({ name: '', type: 'image/jpeg' }, now, label)).toBe('Wklejony obraz 20261014103005.jpg');
    expect(attachmentName({ name: 'faktura.pdf', type: 'application/pdf' }, now, label)).toBe('faktura.pdf');
  });
  it('links images as embeds, other files as links', () => {
    expect(attachmentLink('assets/zdjęcie.png')).toBe('![[zdjęcie.png]]');
    expect(attachmentLink('assets/umowa.pdf')).toBe('[[umowa.pdf]]');
  });
});

/** Minimal DataTransfer stand-in (jsdom has none). */
function data(items: Record<string, string>, files: File[] = []): DataTransfer {
  return { getData: (type: string) => items[type] ?? '', files } as unknown as DataTransfer;
}

describe('handlePaste', () => {
  let views: EditorView[] = [];
  afterEach(() => {
    for (const v of views) v.destroy();
    views = [];
  });
  const ws = new FakeWorkspace();
  function view(doc: string, anchor: number, head = anchor): EditorView {
    const ctx = contextFor(
      { workspace: ws, t: createTranslator('pl'), locale: 'pl', now: () => new Date(2026, 9, 14, 10, 30), navigate: () => {}, openLink: () => {} },
      'a.md',
    );
    const v = new EditorView({
      state: EditorState.create({ doc, selection: EditorSelection.range(anchor, head), extensions: [brulionLanguage(), editorContext.of(ctx), rangeTracker] }),
      parent: document.body,
    });
    views.push(v);
    return v;
  }

  it('wraps the selection in a link when a URL is pasted', () => {
    const v = view('zobacz dokumentację tutaj', 7, 19);
    expect(handlePaste(v, data({ 'text/plain': 'https://brulion.app/docs' }))).toBe(true);
    expect(v.state.doc.toString()).toBe('zobacz [dokumentację](https://brulion.app/docs) tutaj');
  });
  it('converts rich HTML', () => {
    const v = view('', 0);
    expect(handlePaste(v, data({ 'text/plain': 'Lista a b', 'text/html': '<ul><li>a</li><li>b</li></ul>' }))).toBe(true);
    expect(v.state.doc.toString()).toBe('- a\n- b');
  });
  it('leaves plain text to CodeMirror', () => {
    const v = view('x', 1);
    expect(handlePaste(v, data({ 'text/plain': 'zwykły tekst' }))).toBe(false);
  });
  it('re-indents multi-line text in list items', () => {
    const v = view('- ', 2);
    expect(handlePaste(v, data({ 'text/plain': 'jeden\ndwa' }))).toBe(true);
    expect(v.state.doc.toString()).toBe('- jeden\n- dwa');
  });
  it('pastes text, not the picture of it, from office apps', () => {
    const v = view('', 0);
    const picture = new File([new Uint8Array([1, 2, 3])], 'image.png', { type: 'image/png' });
    const html = '<table><tr><td>Mleko</td><td>3,50</td></tr><tr><td>Chleb</td><td>4,20</td></tr></table>';
    expect(handlePaste(v, data({ 'text/plain': 'Mleko\t3,50\nChleb\t4,20', 'text/html': html }, [picture]))).toBe(true);
    expect(v.state.doc.toString()).toBe('| Mleko | 3,50 |\n| --- | --- |\n| Chleb | 4,20 |');
  });
  it('converts a large HTML table quickly', () => {
    const rows = Array.from({ length: 400 }, (_, i) => `<tr><td>wiersz ${i}</td><td><b>${i}</b></td><td>zażółć</td><td>${i * 2}</td></tr>`).join('');
    const t0 = performance.now();
    const md = htmlToMarkdown(`<table><thead><tr><th>A</th><th>B</th><th>C</th><th>D</th></tr></thead><tbody>${rows}</tbody></table>`);
    const ms = performance.now() - t0;
    expect(md.split('\n')).toHaveLength(402);
    expect(md.split('\n')[2]).toBe('| wiersz 0 | **0** | zażółć | 0 |');
    expect(ms).toBeLessThan(2500);
  });
  it('saves pasted images as attachments', async () => {
    const v = view('obraz: ', 7);
    const file = new File([new Uint8Array([137, 80, 78, 71])], 'image.png', { type: 'image/png' });
    expect(handlePaste(v, data({}, [file]))).toBe(true);
    await new Promise((r) => setTimeout(r, 20));
    expect(v.state.doc.toString()).toBe('obraz: ![[Wklejony obraz 20261014103000.png]]');
    expect(await ws.fs.readBinary('assets/Wklejony obraz 20261014103000.png')).toEqual(new Uint8Array([137, 80, 78, 71]));
  });
});
