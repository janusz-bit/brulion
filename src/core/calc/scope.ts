/**
 * Page-level calc scope: variables from all calc blocks of a page plus numeric page/block
 * properties, used by inline calculations (`` `= cena * 2` ``) and table formulas.
 *
 * Fence and property detection is deliberately simple and self-contained (no dependency on the
 * markdown module): fences may be indented or start a list item (`- ```calc`, Logseq style).
 */
import type { Locale } from '../types';
import { evaluateCalcInto } from './calc';
import { withBudget } from './engine';
import { CalcScope, type ValueMeta } from './model';
import { isIdentifier, sanitizeIdentifier, toInternalName } from './names';
import { parseLocaleNumber } from './numbers';

/** A fenced calc block found in a page. */
export interface CalcBlockInfo {
  /** 0-based line of the opening fence. */
  startLine: number;
  /** 0-based line of the closing fence (last line of the document if unclosed). */
  endLine: number;
  /** 0-based line of the first content line (`startLine + 1`). */
  contentStartLine: number;
  /** Block content with the fence indentation removed. */
  source: string;
  /** False when the fence is never closed. */
  closed: boolean;
}

const FENCE_OPEN_RE = /^(\s*(?:(?:[-*+]|\d+[.)])\s+)?)(`{3,}|~{3,})(.*)$/;

interface OpenFence {
  char: string;
  length: number;
  indent: number;
  calc: boolean;
  start: number;
  lines: string[];
}

function fenceOpen(line: string): OpenFence | null {
  const m = FENCE_OPEN_RE.exec(line);
  if (!m) return null;
  const marker = m[2]!;
  const info = m[3]!.trim();
  if (marker[0] === '`' && info.includes('`')) return null;
  const lang = (info.split(/\s+/)[0] ?? '').toLowerCase();
  return { char: marker[0]!, length: marker.length, indent: m[1]!.length, calc: lang === 'calc', start: -1, lines: [] };
}

function isFenceClose(line: string, open: OpenFence): boolean {
  const m = /^\s*(`{3,}|~{3,})\s*$/.exec(line);
  return !!m && m[1]![0] === open.char && m[1]!.length >= open.length;
}

/** Remove up to `n` leading spaces (a tab counts as one). */
function dedent(line: string, n: number): string {
  let i = 0;
  while (i < n && i < line.length && (line[i] === ' ' || line[i] === '\t')) i++;
  return line.slice(i);
}

function splitLines(content: string): string[] {
  return content.split(/\r\n|\r|\n/);
}

/** Locate fenced calc blocks (```` ```calc ```` / `~~~calc`) in a page. */
export function findCalcBlocks(content: string): CalcBlockInfo[] {
  const lines = splitLines(content);
  const blocks: CalcBlockInfo[] = [];
  let open: OpenFence | null = null;
  lines.forEach((line, i) => {
    if (open) {
      if (isFenceClose(line, open)) {
        if (open.calc) {
          blocks.push({ startLine: open.start, endLine: i, contentStartLine: open.start + 1, source: open.lines.join('\n'), closed: true });
        }
        open = null;
      } else if (open.calc) {
        open.lines.push(dedent(line, open.indent));
      }
      return;
    }
    const f = fenceOpen(line);
    if (f) {
      f.start = i;
      open = f;
    }
  });
  const unclosed = open as OpenFence | null;
  if (unclosed?.calc) {
    blocks.push({
      startLine: unclosed.start,
      endLine: lines.length - 1,
      contentStartLine: unclosed.start + 1,
      source: unclosed.lines.join('\n'),
      closed: false,
    });
  }
  return blocks;
}

/** A numeric variable from a property value (`12,50 zł`, `23%`, `"7"`), or null. */
function propertyVariable(key: string, raw: string, locale: Locale): { name: string; value: number; meta: ValueMeta } | null {
  const name = isIdentifier(key) ? key.normalize('NFC') : sanitizeIdentifier(key);
  if (!name) return null;
  const parsed = parseLocaleNumber(raw.trim().replace(/^(["'])(.*)\1$/, '$2'), locale);
  if (!parsed) return null;
  const meta: ValueMeta = {
    ...(parsed.percent ? { percent: true } : {}),
    ...(parsed.currency ? { currency: parsed.currency } : {}),
  };
  return { name, value: parsed.value, meta };
}

/** Numeric `key: value` lines of a leading YAML frontmatter; returns the first body line. */
function readFrontmatter(lines: string[], scope: CalcScope, locale: Locale): number {
  if (lines[0]?.trim() !== '---') return 0;
  const end = lines.findIndex((line, i) => i > 0 && /^(---|\.\.\.)\s*$/.test(line));
  if (end < 0) return 0; // unterminated: not a frontmatter
  for (const line of lines.slice(1, end)) {
    const m = /^([^\s:#][^:]*):\s+(.+)$/u.exec(line);
    const v = m ? propertyVariable(m[1]!.trim(), m[2]!, locale) : null;
    if (v) scope.set(toInternalName(v.name), v.value, v.meta);
  }
  return end + 1;
}

const PROPERTY_RE = /^\s*(?:(?:[-*+]|\d+[.)])\s+)?([\p{L}\p{N}_-]+)::\s*(.+?)\s*$/u;

/**
 * Set a `key:: value` property. Property keys are lower-cased by the Markdown dialect (SYNTAX
 * §4), so `Cena:: 5` defines `Cena` and, unless something else defines it, `cena` too.
 * `aliases` remembers the values set through such lower-case aliases. Returns the internal
 * names that were set.
 */
function setProperty(scope: CalcScope, aliases: Map<string, unknown>, key: string, raw: string, locale: Locale): string[] {
  const v = propertyVariable(key, raw, locale);
  if (!v) return [];
  const internal = toInternalName(v.name);
  scope.set(internal, v.value, v.meta);
  aliases.delete(internal);
  const lower = v.name.toLowerCase();
  if (lower === v.name) return [internal];
  const alias = toInternalName(lower);
  if (!scope.has(alias) || (aliases.has(alias) && scope.values.get(alias) === aliases.get(alias))) {
    scope.set(alias, v.value, v.meta);
    aliases.set(alias, v.value);
    return [internal, alias];
  }
  return [internal];
}

/** What changed in the page scope at one line (a property, the end of a calc block). */
interface ScopeEvent {
  /** Line of the change: -1 for frontmatter, Infinity for an unclosed block at the end. */
  line: number;
  changes: [internal: string, value: unknown, meta: ValueMeta | undefined][];
}

/**
 * Variables in scope at `uptoLine` (0-based; default: the whole page): numeric frontmatter
 * values and `key:: value` properties, then every calc block that ends before `uptoLine`, in
 * document order. Property keys become identifiers (`due-date` → `due_date`, `Cena` → also
 * `cena`); values accept decimal commas, currencies (`cena:: 12,50 zł`) and percentages
 * (`vat:: 23%`). Never throws.
 *
 * A page is evaluated once (per content and locale, memoised) into a timeline of changes; the
 * scope for any `uptoLine` is replayed from it, so many inline calculations on one page stay cheap.
 */
export function pageScope(content: string, uptoLine?: number, locale: Locale = 'pl'): Record<string, unknown> {
  const key = `${locale}\u0000${content}`;
  let timeline = timelineCache.get(key);
  if (timeline) {
    timelineCache.delete(key);
  } else {
    timeline = withBudget(() => computeTimeline(content, locale));
    if (timelineCache.size >= TIMELINE_CACHE_SIZE) timelineCache.delete(timelineCache.keys().next().value!);
  }
  timelineCache.set(key, timeline);
  return replay(timeline, uptoLine);
}

/** Recently evaluated pages (pages are re-rendered often; evaluation is not free). */
const timelineCache = new Map<string, ScopeEvent[]>();
const TIMELINE_CACHE_SIZE = 16;

/** A fresh scope record with every change before `uptoLine` applied. */
function replay(events: readonly ScopeEvent[], uptoLine: number | undefined): Record<string, unknown> {
  const scope = new CalcScope();
  for (const event of events) {
    if (uptoLine !== undefined && !(event.line < uptoLine)) break;
    for (const [internal, value, meta] of event.changes) scope.set(internal, value, meta);
  }
  return scope.toRecord();
}

function computeTimeline(content: string, locale: Locale): ScopeEvent[] {
  const lines = splitLines(content);
  const scope = new CalcScope();
  const events: ScopeEvent[] = [];
  const recorded = new Map<string, [unknown, ValueMeta | undefined]>();
  /** Record what changed at `line`: only `keys` when given (properties), else a full scan. */
  const record = (line: number, keys?: Iterable<string>) => {
    const changes: ScopeEvent['changes'] = [];
    for (const internal of keys ?? scope.values.keys()) {
      if (!scope.has(internal)) continue;
      const value = scope.values.get(internal);
      const meta = scope.metaOf(internal);
      const prev = recorded.get(internal);
      if (prev && prev[0] === value && prev[1] === meta) continue;
      recorded.set(internal, [value, meta]);
      changes.push([internal, value, meta]);
    }
    if (changes.length) events.push({ line, changes });
  };
  const aliases = new Map<string, unknown>();
  const bodyStart = readFrontmatter(lines, scope, locale);
  record(-1);
  let open: OpenFence | null = null;
  for (let i = bodyStart; i < lines.length; i++) {
    const line = lines[i]!;
    if (open) {
      if (isFenceClose(line, open)) {
        if (open.calc) {
          evaluateCalcInto(open.lines.join('\n'), scope, locale);
          record(i);
        }
        open = null;
      } else if (open.calc) {
        open.lines.push(dedent(line, open.indent));
      }
      continue;
    }
    const f = fenceOpen(line);
    if (f) {
      f.start = i;
      open = f;
      continue;
    }
    const prop = PROPERTY_RE.exec(line);
    if (prop) record(i, setProperty(scope, aliases, prop[1]!, prop[2]!, locale));
  }
  const unclosed = open as OpenFence | null;
  if (unclosed?.calc) {
    evaluateCalcInto(unclosed.lines.join('\n'), scope, locale);
    record(Infinity);
  }
  return events;
}
