/**
 * Pure content helpers of the Workspace service (content in → content out, line endings kept):
 * quick capture lines, appending to journals, import sections, adding reminder tokens, and
 * turning page titles into vault paths.
 */
import { parseNaturalDate, stripNaturalDate } from '@/core/dates';
import { blockAtLine, blockIdAtEnd, formatReminderToken, parseNote, TOKENS } from '@/core/markdown';
import { extname, isMarkdown, joinPath, normalizePath, sanitizeFileName } from '@/core/paths';
import type { LocalDate, LocalDateTime, Locale, VaultPath } from '@/core/types';

/** First line terminator used in `content` ('\n' when none). */
export function eolOf(content: string): '\n' | '\r\n' {
  const i = content.indexOf('\n');
  return i > 0 && content[i - 1] === '\r' ? '\r\n' : '\n';
}

export interface CaptureOptions {
  /** Reminder time (overrides a date phrase in the text). */
  reminderAt?: LocalDateTime;
  /** Alarm instead of a reminder (with `reminderAt`). */
  alarm?: boolean;
  /** Write a task (`- [ ] `, default) or a plain list item (`- `). */
  task?: boolean;
  /** Recognise date phrases in the text ("jutro o 18"); default true. */
  parseDates?: boolean;
  locale?: Locale;
  weekStartsOn?: 0 | 1;
}

/**
 * The Markdown for a quick capture: a list item (task by default) with a date phrase turned
 * into a token — with a time it becomes a reminder (`⏰ 2026-10-15 18:00`), a bare date a due
 * date (`📅 2026-10-15`). Further lines become child items.
 *
 * "Kupić mleko jutro o 18" → "- [ ] Kupić mleko ⏰ 2026-10-15 18:00"
 */
export function buildCaptureBlock(text: string, now: Date, opts: CaptureOptions = {}): string {
  const lines = text
    .split(/\r?\n/)
    .map((l) => l.trim())
    .filter((l) => l !== '');
  let first = lines.shift() ?? '';
  let token = '';
  if (opts.reminderAt) {
    token = formatReminderToken(opts.reminderAt, !!opts.alarm);
  } else if (opts.parseDates !== false && first) {
    const found = parseNaturalDate(first, now, { locale: opts.locale, weekStartsOn: opts.weekStartsOn });
    if (found) {
      const rest = stripNaturalDate(first, found);
      if (rest) {
        first = rest;
        token = found.time ? formatReminderToken(`${found.date}T${found.time}`, false) : `${TOKENS.due} ${found.date}`;
      }
    }
  }
  const marker = opts.task === false ? '- ' : '- [ ] ';
  const head = `${marker}${first}${token ? `${first ? ' ' : ''}${token}` : ''}`.trimEnd();
  return [head, ...lines.map((l) => `\t- ${l}`)].join('\n');
}

const EMPTY_ITEM = /^\s*(?:[-*+]|\d+[.)])(?:\s+\[ \])?\s*$/;

/**
 * Append a block (lines joined with '\n') at the end of `content`. Trailing blank lines and a
 * trailing empty list item (what a journal template leaves for the cursor) are replaced.
 * Returns the new content and the 0-based line where the block starts.
 */
export function appendBlock(content: string, block: string): { content: string; line: number } {
  const eol = eolOf(content);
  const lines = content === '' ? [] : content.split(/\r?\n/);
  while (lines.length > 0 && lines[lines.length - 1]!.trim() === '') lines.pop();
  if (lines.length > 0 && EMPTY_ITEM.test(lines[lines.length - 1]!)) lines.pop();
  const line = lines.length;
  lines.push(...block.split('\n'));
  return { content: lines.join(eol) + eol, line };
}

/** Append imported content to an existing note under "## Import (<source>)". */
export function appendImportSection(content: string, imported: string, source: string): string {
  const eol = eolOf(content);
  const body = imported.replace(/\r\n/g, '\n').trim();
  const head = content.replace(/\s+$/, '');
  const section = [`## Import (${source})`, '', ...body.split('\n')].join(eol);
  return head ? `${head}${eol}${eol}${section}${eol}` : `${section}${eol}`;
}

/**
 * Add a metadata token (e.g. `⏰ 2026-10-15 09:00`) to the first line of the block at `line`,
 * before a trailing block id (` ^abc123`). Lines in code blocks / frontmatter and out-of-range
 * lines leave the content unchanged; a blank line becomes a new list item with the token.
 */
export function addTokenToBlock(content: string, line: number, token: string): string {
  const eol = eolOf(content);
  const lines = content.split(/\r?\n/);
  if (line < 0 || line >= lines.length) return content;
  if (lines[line]!.trim() === '') {
    lines[line] = `- ${token}`;
    return lines.join(eol);
  }
  const note = parseNote('x.md', content);
  const block = blockAtLine(note, line);
  if (!block || block.kind === 'code' || block.kind === 'frontmatter' || block.kind === 'table' || block.kind === 'hr') {
    return content;
  }
  const target = block.line;
  const text = lines[target]!;
  const id = blockIdAtEnd(text);
  const before = (id ? text.slice(0, id.from) : text).replace(/\s+$/, '');
  lines[target] = `${before} ${token}${id ? text.slice(id.from) : ''}`;
  return lines.join(eol);
}

/** `⏰ …` / `⏰! …` token for a reminder at `at`. */
export function reminderToken(at: LocalDateTime | LocalDate, alarm: boolean): string {
  return formatReminderToken(at, alarm);
}

/**
 * Vault path for a new page titled `title` in `folder`. A title with `/` creates sub-folders
 * ("Projekty/Dom" → "Projekty/Dom.md"); every segment is sanitised. An explicit `.md`
 * extension is kept.
 */
export function pathForTitle(title: string, folder = ''): VaultPath {
  const segments = title
    .split('/')
    .map((s) => s.trim())
    .filter((s) => s !== '' && s !== '.' && s !== '..')
    .map((s) => sanitizeFileName(s));
  if (segments.length === 0) segments.push(sanitizeFileName('Bez tytułu'));
  const last = segments.pop()!;
  const file = isMarkdown(last) ? last : `${last}.md`;
  return normalizePath(joinPath(folder, ...segments, file));
}

const MIME: Record<string, string> = {
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.gif': 'image/gif',
  '.webp': 'image/webp',
  '.avif': 'image/avif',
  '.bmp': 'image/bmp',
  '.ico': 'image/x-icon',
  '.svg': 'image/svg+xml',
  '.pdf': 'application/pdf',
  '.mp3': 'audio/mpeg',
  '.m4a': 'audio/mp4',
  '.ogg': 'audio/ogg',
  '.oga': 'audio/ogg',
  '.wav': 'audio/wav',
  '.flac': 'audio/flac',
  '.mp4': 'video/mp4',
  '.webm': 'video/webm',
  '.mov': 'video/quicktime',
  '.txt': 'text/plain',
  '.md': 'text/markdown',
  '.json': 'application/json',
  '.csv': 'text/csv',
};

/** MIME type for an attachment (by extension). */
export function mimeForPath(path: VaultPath): string {
  return MIME[extname(path).toLowerCase()] ?? 'application/octet-stream';
}
