/**
 * Paste and drop: files/images become attachments (`![[name]]` / `[[name]]`), a URL pasted over
 * a selection becomes `[text](url)`, HTML from browsers/office apps is converted to Markdown
 * (turndown + GFM tables, task lists, strikethrough, highlights) and multi-line text pasted
 * into a list item is re-indented to the item's level.
 */
import { EditorSelection, type Extension } from '@codemirror/state';
import { EditorView } from '@codemirror/view';
import TurndownService from 'turndown';
import { createTranslator } from '@/core/i18n';
import { basename, extname } from '@/core/paths';
import { editorContext, editorNow } from './context';
import { childColumn, inCodeContext, isBlank, leadingWhitespace, parseListLine } from './outliner';
import { trackRange } from './tracker';

// ---------------------------------------------------------------------------------------------
// HTML → Markdown
// ---------------------------------------------------------------------------------------------

let service: TurndownService | null = null;

/** Column count of a table (widest row), cached per table element during one conversion. */
const tableWidths = new WeakMap<Node, number>();

function tableWidth(table: HTMLTableElement): number {
  let width = tableWidths.get(table);
  if (width === undefined) {
    width = 1;
    for (const row of Array.from(table.rows)) width = Math.max(width, row.cells.length);
    tableWidths.set(table, width);
  }
  return width;
}

/**
 * GFM table row from its converted cells (` text |` each): short rows are padded, the first
 * row is the header and gets the delimiter row. Cells are converted once by turndown itself
 * (re-converting every cell's HTML separately made big pasted tables very slow).
 */
function tableRow(content: string, row: HTMLTableRowElement): string {
  const table = row.closest('table');
  if (!table) return content;
  const width = tableWidth(table);
  let line = `|${content}${'  |'.repeat(Math.max(0, width - row.cells.length))}`;
  if (table.rows[0] === row) line += `\n|${' --- |'.repeat(width)}`;
  return `\n${line}`;
}

/** Position of each list item in its `<ol>` (indexOf per item made long lists quadratic). */
const itemIndexes = new WeakMap<Node, Map<Node, number>>();

function indexInParent(parent: HTMLElement, node: Node): number {
  let map = itemIndexes.get(parent);
  if (!map) {
    map = new Map();
    let i = 0;
    for (const child of Array.from(parent.children)) map.set(child, i++);
    itemIndexes.set(parent, map);
  }
  return map.get(node) ?? 0;
}

/** Inline style declarations of an element (lower-cased, spaces removed). */
function styleOf(node: Node): string {
  return ((node as HTMLElement).getAttribute?.('style') ?? '').toLowerCase().replace(/\s+/g, '');
}

function turndown(): TurndownService {
  if (service) return service;
  const s = new TurndownService({
    headingStyle: 'atx',
    bulletListMarker: '-',
    codeBlockStyle: 'fenced',
    emDelimiter: '*',
    strongDelimiter: '**',
    hr: '---',
  });
  s.remove(['style', 'script', 'meta', 'title', 'head', 'noscript']);
  // Google Docs wraps the whole clipboard in `<b style="font-weight:normal" id="docs-internal-
  // guid-…">` (not bold) and marks bold/italic text with styled spans instead of <b>/<i>.
  s.addRule('docsWrapper', {
    filter: (node) =>
      (node.nodeName === 'B' || node.nodeName === 'STRONG') &&
      (/^docs-internal-guid/.test((node as HTMLElement).id) || /font-weight:(normal|[1-4]00)(;|$)/.test(styleOf(node))),
    replacement: (content) => content,
  });
  s.addRule('styledSpan', {
    filter: (node) => node.nodeName === 'SPAN' && /font-weight:(bold|[6-9]00)(;|$)|font-style:italic/.test(styleOf(node)),
    replacement: (content, node) => {
      if (!content.trim()) return content;
      const style = styleOf(node);
      let out = content;
      if (/font-style:italic/.test(style)) out = `*${out}*`;
      if (/font-weight:(bold|[6-9]00)(;|$)/.test(style)) out = `**${out}**`;
      return out;
    },
  });
  s.addRule('strikethrough', {
    filter: ['del', 's', 'strike' as keyof HTMLElementTagNameMap],
    replacement: (content) => `~~${content}~~`,
  });
  s.addRule('highlight', { filter: ['mark'], replacement: (content) => `==${content}==` });
  s.addRule('taskCheckbox', {
    filter: (node) => node.nodeName === 'INPUT' && (node as HTMLInputElement).type === 'checkbox' && !!node.closest('li'),
    replacement: (_content, node) => ((node as HTMLInputElement).checked ? '[x] ' : '[ ] '),
  });
  s.addRule('listItem', {
    filter: 'li',
    replacement: (content, node) => {
      const body = content.replace(/^\n+/, '').replace(/\n+$/, '\n').replace(/\n(?!$)/gm, '\n\t');
      const parent = node.parentNode as HTMLElement | null;
      let prefix = '- ';
      if (parent?.nodeName === 'OL') {
        const start = Number(parent.getAttribute('start') ?? '1') || 1;
        prefix = `${start + indexInParent(parent, node)}. `;
      }
      return prefix + body + (node.nextSibling && !body.endsWith('\n') ? '\n' : '');
    },
  });
  s.addRule('tableCell', {
    filter: ['th', 'td'],
    replacement: (content) => ` ${content.replace(/\n+/g, ' ').replace(/\|/g, '\\|').trim()} |`,
  });
  s.addRule('tableRow', { filter: 'tr', replacement: (content, node) => tableRow(content, node as HTMLTableRowElement) });
  s.addRule('tableSection', { filter: ['thead', 'tbody', 'tfoot'], replacement: (content) => content });
  s.addRule('table', {
    filter: 'table',
    replacement: (content) => `\n\n${content.replace(/^\n+/, '')}\n\n`,
  });
  service = s;
  return s;
}

/** HTML worth converting (formatting, lists, tables, links) rather than taking text/plain. */
export function isRichHtml(html: string): boolean {
  return /<(p|li|ul|ol|table|h[1-6]|a\s|strong|b|em|i|pre|blockquote|img|del|s|mark|code)[\s>]/i.test(html);
}

/** Convert pasted HTML to Brulion Markdown (browser-only: needs a DOM). */
export function htmlToMarkdown(html: string): string {
  const body = html.replace(/<!--(?:StartFragment|EndFragment)-->/g, '');
  return turndown()
    .turndown(body)
    .replace(/^([ \t]*(?:[-*+]|\d+[.)]) \[[ xX]\]) {2,}/gm, '$1 ')
    .replace(/\n{3,}/g, '\n\n')
    .trim();
}

// ---------------------------------------------------------------------------------------------
// Plain multi-line text into list items
// ---------------------------------------------------------------------------------------------

/**
 * Re-indent multi-line `text` pasted at column `col` of the list item line `line`: following
 * lines get the item's indentation (pasted list items keep their relative nesting; plain lines
 * become sibling bullets). Pasting a list into an empty item replaces that item. Returns the
 * replacement for [from, to) of the line, or null when nothing special applies.
 */
export function listPaste(text: string, line: string, col: number): { from: number; to: number; insert: string } | null {
  const item = parseListLine(line);
  const lines = text.replace(/\r\n?/g, '\n').replace(/\n+$/, '').split('\n');
  if (!item || lines.length < 2) return null;
  const isList = lines.map((l) => parseListLine(l) !== null);
  const anyList = isList.some(Boolean);
  const nonBlank = lines.filter((l) => !isBlank(l));
  const minIndent = Math.min(...nonBlank.map((l) => leadingWhitespace(l).length));
  const dedent = (l: string) => (isBlank(l) ? '' : l.slice(Math.min(minIndent, leadingWhitespace(l).length)));
  const itemEmpty = isBlank(line.slice(item.contentFrom)) && col >= item.contentFrom;

  // Code and tables must keep their lines (and indentation): they go into the item as
  // continuation lines at its text column, never one bullet per line.
  const nonBlankLines = lines.filter((l) => !isBlank(l));
  if (lines.some((l) => /^[ \t]*(```|~~~)/.test(l)) || (nonBlankLines.length > 1 && nonBlankLines.every((l) => /^[ \t]*\|/.test(l)))) {
    const pad = item.indent + ' '.repeat(childColumn(line, item) - item.indentCols);
    const block = lines.map((l) => (isBlank(l) ? '' : pad + dedent(l)));
    if (itemEmpty) return { from: item.contentFrom, to: line.length, insert: [dedent(lines[0]!), ...block.slice(1)].join('\n') };
    const tail = line.slice(col);
    return { from: col, to: col, insert: `\n${block.join('\n')}${isBlank(tail) ? '' : `\n${pad}`}` };
  }

  if (anyList && isList[0] && itemEmpty) {
    const out = lines.filter((l) => !isBlank(l)).map((l) => item.indent + dedent(l));
    return { from: 0, to: line.length, insert: out.join('\n') };
  }
  if (anyList && isList[0] && isBlank(line.slice(col))) {
    // A list pasted at the end of a non-empty item: its items follow as siblings.
    const out = lines.filter((l) => !isBlank(l)).map((l) => item.indent + dedent(l));
    return { from: line.length, to: line.length, insert: `\n${out.join('\n')}` };
  }
  if (anyList && isList[0]) {
    // In the middle of an item: the first pasted item's text joins it (no marker in the text).
    const firstItem = parseListLine(lines[0]!)!;
    lines[0] = lines[0]!.slice(firstItem.contentFrom);
  }
  const [first, ...rest] = lines;
  const restOut = anyList
    ? rest.filter((l) => !isBlank(l)).map((l) => item.indent + dedent(l))
    : rest.filter((l) => !isBlank(l)).map((l) => `${item.indent}- ${l.trim()}`);
  return { from: col, to: col, insert: [first!.trim() === '' ? '' : first!, ...restOut].join('\n') };
}

// ---------------------------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------------------------

const IMAGE_EXT = new Set(['.png', '.jpg', '.jpeg', '.gif', '.webp', '.svg', '.bmp', '.avif']);

export function isImageName(name: string): boolean {
  return IMAGE_EXT.has(extname(name).toLowerCase());
}

function stamp(d: Date): string {
  const p = (n: number) => String(n).padStart(2, '0');
  return `${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}${p(d.getHours())}${p(d.getMinutes())}${p(d.getSeconds())}`;
}

const MIME_EXT: Record<string, string> = {
  'image/png': '.png',
  'image/jpeg': '.jpg',
  'image/gif': '.gif',
  'image/webp': '.webp',
  'image/svg+xml': '.svg',
  'application/pdf': '.pdf',
};

/** File name for a pasted/dropped file: generic clipboard names get a timestamped name. */
export function attachmentName(file: { name: string; type: string }, now: Date, label: (stamp: string) => string): string {
  const generic = !file.name || /^image\.(png|jpe?g|gif|webp)$/i.test(file.name) || /^blob$/i.test(file.name);
  if (!generic) return file.name;
  const ext = extname(file.name) || MIME_EXT[file.type] || '.bin';
  return `${label(stamp(now))}${ext}`;
}

/** Wiki embed/link for a saved attachment. */
export function attachmentLink(path: string): string {
  const name = basename(path);
  return isImageName(name) ? `![[${name}]]` : `[[${name}]]`;
}

async function insertFiles(view: EditorView, files: File[], pos: number): Promise<void> {
  const ctx = view.state.facet(editorContext);
  if (!ctx) return;
  const t = createTranslator(ctx.locale);
  const tracked = trackRange(view, pos);
  const links: string[] = [];
  for (const file of files) {
    try {
      const bytes = new Uint8Array(await file.arrayBuffer());
      const name = attachmentName(file, editorNow(view.state), (s) => t('editor.paste.imageName', { stamp: s }));
      links.push(attachmentLink(await ctx.workspace.saveAttachment(name, bytes)));
    } catch (e) {
      console.warn(t('editor.paste.error', { error: String(e) }));
    }
  }
  const { from } = tracked.range();
  tracked.dispose();
  if (links.length === 0) return;
  const insert = links.join(' ');
  view.dispatch({
    changes: { from, insert },
    selection: EditorSelection.cursor(from + insert.length),
    userEvent: 'input.paste',
    scrollIntoView: true,
  });
}

// ---------------------------------------------------------------------------------------------
// Event handling
// ---------------------------------------------------------------------------------------------

const URL_RE = /^(?:https?:\/\/|mailto:|obsidian:\/\/|file:\/\/)\S+$/i;

/** Handle a paste; true when handled (default paste suppressed). */
export function handlePaste(view: EditorView, data: DataTransfer | null): boolean {
  if (!data || view.state.readOnly) return false;
  const files = Array.from(data.files ?? []);
  // Office apps and spreadsheets put a picture of the copied cells/text next to the HTML: the
  // text is what was meant. A copied image comes with `<img>` HTML (or none) and stays a file.
  const richText = files.length > 0 && isRichHtml(data.getData('text/html')) && !/<img[\s>]/i.test(data.getData('text/html'));
  if (files.length > 0 && !richText) {
    void insertFiles(view, files, view.state.selection.main.from);
    return true;
  }
  const sel = view.state.selection.main;
  if (inCodeContext(view.state, sel.head)) return false;
  const plain = data.getData('text/plain');
  const url = plain.trim();
  if (!sel.empty && URL_RE.test(url) && !view.state.sliceDoc(sel.from, sel.to).includes('\n')) {
    const text = view.state.sliceDoc(sel.from, sel.to);
    if (!URL_RE.test(text.trim())) {
      // Brackets in the text would end the link text (or make `[[x]]` a wiki link).
      const insert = `[${text.replace(/[[\]\\]/g, '\\$&')}](${url})`;
      view.dispatch({
        changes: { from: sel.from, to: sel.to, insert },
        selection: EditorSelection.cursor(sel.from + insert.length),
        userEvent: 'input.paste',
      });
      return true;
    }
  }
  const html = data.getData('text/html');
  let text = plain;
  let converted = false;
  if (html && isRichHtml(html)) {
    try {
      text = htmlToMarkdown(html);
      converted = true;
    } catch {
      text = plain;
    }
  }
  if (!text) return false;
  if (view.state.selection.ranges.length === 1 && sel.empty) {
    const line = view.state.doc.lineAt(sel.head);
    const lp = listPaste(text, line.text, sel.head - line.from);
    if (lp) {
      view.dispatch({
        changes: { from: line.from + lp.from, to: line.from + lp.to, insert: lp.insert },
        selection: EditorSelection.cursor(line.from + lp.from + lp.insert.length),
        userEvent: 'input.paste',
        scrollIntoView: true,
      });
      return true;
    }
  }
  if (!converted) return false;
  view.dispatch(view.state.replaceSelection(text), { userEvent: 'input.paste', scrollIntoView: true });
  return true;
}

export function pasteHandling(): Extension {
  return EditorView.domEventHandlers({
    paste(event, view) {
      const handled = handlePaste(view, event.clipboardData);
      if (handled) event.preventDefault();
      return handled;
    },
    drop(event, view) {
      const files = Array.from(event.dataTransfer?.files ?? []);
      if (files.length === 0 || view.state.readOnly) return false;
      event.preventDefault();
      const pos = view.posAtCoords({ x: event.clientX, y: event.clientY }) ?? view.state.selection.main.head;
      void insertFiles(view, files, pos);
      return true;
    },
  });
}
