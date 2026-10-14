/**
 * `parseNote` — turns a note into the {@link ParsedNote} model (docs/SYNTAX.md §1-§7, §11):
 * block tree with text, properties, tasks, reminders, tags and ids, plus page-level
 * properties, aliases, tags, links and headings.
 *
 * Pipeline: ./structure.ts finds blocks and their lines; for every block the own text lines
 * are joined, inline code is masked, and links/tags (./inline.ts) and metadata tokens
 * (./tokens.ts) are read. Code (fenced or HTML) is never interpreted.
 */
import { formatRecurrence, joinDateTime, normalizeTime, repeaterToRule } from '@/core/dates';
import { pageTitle } from '@/core/paths';
import {
  DEFAULT_VAULT_CONFIG,
  type Block,
  type Heading,
  type Link,
  type LocalDateTime,
  type ParsedNote,
  type Priority,
  type PropertyValue,
  type Reminder,
  type Task,
  type VaultConfig,
  type VaultPath,
} from '../types';
import { parseYaml } from './frontmatter';
import { scanInline, segmentsForText, type Segments } from './inline';
import { journalDateFromPath } from './journal';
import { maskInline } from './mask';
import {
  emptyMap,
  frontmatterGet,
  frontmatterProperties,
  mergeMaps,
  namesFrom,
  parsePropertyValue,
  setProperty,
  tagKey,
  uniqueCaseInsensitive,
} from './properties';
import { PROPERTY_RE, parseStructure, type LineView, type Node, type OwnLine } from './structure';
import { matchTaskMarker, type TaskMarker } from './tasks';
import { blockIdAtEnd, findTokens, recurrenceOf, removeRanges, type Token } from './tokens';

const DRAWER_START_RE = /^[ \t]*:[A-Z][A-Z_-]*:[ \t]*$/;
const DRAWER_END_RE = /^[ \t]*:END:[ \t]*$/;
const PLANNING_LINE_RE = /^[ \t]*(?:(?:SCHEDULED|DEADLINE):[ \t]*<[^>\r\n]*>[ \t]*)+$/;
const CLOSING_HASHES_RE = /(?:^|[ \t]+)#+[ \t]*$/;
const TAG_PROPERTY_KEYS = new Set(['tags', 'tag']);

/** How a block line is used after classification. */
type LineClass = 'text' | 'property' | 'planning' | 'drawer' | 'code' | 'blank' | 'meta';

/** Shared per-note state while building blocks. */
interface Ctx {
  defaultTime: string;
  /** Document line texts (for `Block.raw`). */
  lineTexts: string[];
  links: Link[];
}

/** Per-line analysis of a block. */
interface LineInfo {
  own: OwnLine;
  cls: LineClass;
  /** Line text with inline code / comments masked (text-like lines only). */
  masked: string;
  tokens: Token[];
  /** Ranges (line coordinates) removed from the block text besides tokens (allocated lazily). */
  extra: [number, number][] | null;
}

/** Classify own lines: Logseq drawers (`:LOGBOOK:` … `:END:`) and roles. */
function classifyLines(node: Node): LineInfo[] {
  const own = node.own;
  const infos: LineInfo[] = [];
  const drawers = node.kind === 'listItem' || node.kind === 'paragraph';
  let inDrawer = false;
  for (let k = 0; k < own.length; k++) {
    const l = own[k]!;
    let cls: LineClass;
    if (l.role === 'fence' || l.role === 'code') cls = 'code';
    else if (l.role !== 'text') cls = l.role;
    else {
      cls = 'text';
      const m = l.view.masked;
      if (drawers && (inDrawer || (k > 0 && m.indexOf(':') !== -1))) {
        const content = m.slice(l.start);
        if (inDrawer) {
          cls = 'drawer';
          if (DRAWER_END_RE.test(content)) inDrawer = false;
        } else if (DRAWER_START_RE.test(content) && !DRAWER_END_RE.test(content)) {
          cls = 'drawer';
          inDrawer = true;
        }
      }
    }
    infos.push({ own: l, cls, masked: '', tokens: NO_TOKENS, extra: null });
  }
  return infos;
}

const NO_TOKENS: Token[] = [];

function addExtra(info: LineInfo, from: number, to: number): void {
  (info.extra ??= []).push([from, to]);
}

/**
 * Masked text of a line for inline scanning: container syntax before the block's content
 * (indentation, list marker, `>` quote marker) is blanked, so content starts after whitespace
 * (`>#tag` is a tag, like `> #tag`).
 */
function scanText(own: OwnLine): string {
  const m = own.view.masked;
  return own.start > 0 ? ' '.repeat(own.start) + m.slice(own.start) : m;
}

/** Join the scanned lines, mask inline code across them and store each line's masked text. */
function maskScannedLines(infos: LineInfo[]): { orig: string; masked: string; seg: Segments } {
  const seg: Segments = { pos: [], offset: [], line: [] };
  let single: LineInfo | null = null;
  let count = 0;
  for (const info of infos) {
    if (info.cls !== 'text') continue;
    single = info;
    count++;
  }
  if (count === 1 && single) {
    // Common case: a one-line block — no joining needed.
    const v = single.own.view;
    seg.pos.push(0);
    seg.offset.push(v.offset);
    seg.line.push(v.line);
    single.masked = maskInline(scanText(single.own));
    return { orig: v.text, masked: single.masked, seg };
  }
  const origParts: string[] = [];
  const maskedParts: string[] = [];
  const scanned: LineInfo[] = [];
  let pos = 0;
  for (const info of infos) {
    if (info.cls !== 'text') continue;
    const v = info.own.view;
    seg.pos.push(pos);
    seg.offset.push(v.offset);
    seg.line.push(v.line);
    origParts.push(v.text);
    maskedParts.push(scanText(info.own));
    scanned.push(info);
    pos += v.text.length + 1;
  }
  const orig = origParts.join('\n');
  const masked = maskInline(maskedParts.join('\n'));
  for (let k = 0; k < scanned.length; k++) {
    const info = scanned[k]!;
    const p = seg.pos[k]!;
    info.masked = masked.slice(p, p + info.own.view.text.length);
  }
  return { orig, masked, seg };
}

/** Block id at the end of the first or last text line (Obsidian `^id`); records its removal. */
function findBlockId(infos: LineInfo[]): string | undefined {
  let first: LineInfo | undefined;
  let last: LineInfo | undefined;
  for (const info of infos) {
    if (info.cls !== 'text') continue;
    first ??= info;
    last = info;
  }
  for (const info of first === last ? [first] : [first, last]) {
    if (!info || info.masked.indexOf('^') === -1) continue;
    const hit = blockIdAtEnd(info.masked);
    if (hit && info.masked.lastIndexOf('^') >= info.own.start) {
      addExtra(info, hit.from, hit.to);
      return hit.id;
    }
  }
  return undefined;
}

/** Collected task metadata (first-line tokens and planning items). */
interface TaskMeta {
  due?: string;
  scheduled?: string;
  start?: string;
  created?: string;
  doneDate?: string;
  cancelledDate?: string;
  recurrence?: string;
  priority?: Priority;
  logseqPriority?: Priority;
  repeater?: string;
}

/** Read task fields from the first line's tokens. */
function taskMetaFromTokens(tokens: Token[], meta: TaskMeta): void {
  for (const t of tokens) {
    switch (t.kind) {
      case 'due':
      case 'scheduled':
      case 'start':
        if (meta[t.kind] === undefined && t.value) meta[t.kind] = joinDateTime(t.value, t.time);
        break;
      case 'created':
        meta.created ??= t.value;
        break;
      case 'done':
        meta.doneDate ??= t.value;
        break;
      case 'cancelled':
        meta.cancelledDate ??= t.value;
        break;
      case 'recurrence':
        meta.recurrence ??= recurrenceOf(t);
        break;
      case 'priority':
        meta.priority ??= t.priority;
        break;
      case 'logseqPriority':
        meta.logseqPriority ??= t.priority;
        break;
      default:
        break;
    }
  }
}

/** Logseq `SCHEDULED:` / `DEADLINE:` planning items (only fill fields not set by emoji tokens). */
function taskMetaFromPlanning(tokens: Token[], meta: TaskMeta): void {
  for (const t of tokens) {
    if (t.kind !== 'planning' || !t.value || !t.field) continue;
    if (meta[t.field] === undefined) meta[t.field] = joinDateTime(t.value, t.time);
    if (t.repeater && meta.repeater === undefined) meta.repeater = t.repeater;
  }
}

function buildTask(marker: TaskMarker, meta: TaskMeta, text: string, line: number): Task {
  const task: Task = { status: marker.status, marker: marker.marker, text, line };
  if (meta.due) task.due = meta.due;
  if (meta.scheduled) task.scheduled = meta.scheduled;
  if (meta.start) task.start = meta.start;
  if (meta.created) task.created = meta.created;
  if (meta.doneDate) task.doneDate = meta.doneDate;
  if (meta.cancelledDate) task.cancelledDate = meta.cancelledDate;
  let recurrence = meta.recurrence;
  if (!recurrence && meta.repeater) {
    const rule = repeaterToRule(meta.repeater);
    if (rule) recurrence = formatRecurrence(rule, 'en');
  }
  if (recurrence) task.recurrence = recurrence;
  const priority = meta.priority ?? meta.logseqPriority;
  if (priority) task.priority = priority;
  return task;
}

/**
 * Ranges (line coordinates, from `begin`) hidden by document-level `%%comments%%` — where the
 * comment-masked view is blank but the text is not. Null when the line has none.
 */
function commentRuns(v: LineView, begin: number): [number, number][] | null {
  const t = v.text;
  const m = v.masked;
  if (m === t) return null;
  let runs: [number, number][] | null = null;
  for (let i = begin; i < t.length; i++) {
    if (m.charCodeAt(i) !== 32 || t.charCodeAt(i) === 32) continue;
    let j = i + 1;
    while (j < t.length && m.charCodeAt(j) === 32 && t.charCodeAt(j) !== 32) j++;
    (runs ??= []).push([i, j]);
    i = j;
  }
  return runs;
}

/** Text of one text line: content after `begin` without tokens / extra ranges / comments. */
function cleanLine(info: LineInfo, begin: number): string {
  const text = info.own.view.text;
  const comments = commentRuns(info.own.view, begin);
  if (info.tokens.length === 0 && !info.extra && !comments) return text.slice(begin).trimEnd();
  const ranges: [number, number][] = [];
  for (const t of info.tokens) if (t.from >= begin) ranges.push([t.from - begin, t.to - begin]);
  if (info.extra) for (const [a, b] of info.extra) if (a >= begin) ranges.push([a - begin, b - begin]);
  if (comments) for (const [a, b] of comments) ranges.push([a - begin, b - begin]);
  return removeRanges(text.slice(begin), ranges).trimEnd();
}

/** Drop leading/trailing empty lines and trim the outer whitespace. */
function joinText(parts: string[]): string {
  let a = 0;
  let b = parts.length;
  while (a < b && parts[a]!.trim() === '') a++;
  while (b > a && parts[b - 1]!.trim() === '') b--;
  return parts.slice(a, b).join('\n').trim();
}

/** A block with the fields every kind has (text etc. filled in later). */
function blockShell(node: Node, ctx: Ctx): Block {
  const own = node.own;
  const raw =
    own.length === 1 ? (ctx.lineTexts[own[0]!.view.line] ?? '') : own.map((l) => ctx.lineTexts[l.view.line] ?? '').join('\n');
  const block: Block = {
    kind: node.kind,
    line: node.line,
    endLine: node.endLine,
    ownEndLine: node.ownEndLine,
    depth: node.depth,
    raw,
    text: '',
    properties: emptyMap<PropertyValue>(),
    reminders: [],
    tags: [],
    children: [],
  };
  if (node.headingLevel) block.headingLevel = node.headingLevel;
  if (node.lang) block.lang = node.lang;
  if (node.calloutType) {
    block.properties.calloutType = node.calloutType;
    if (node.calloutFold) block.properties.calloutFold = node.calloutFold;
  }
  return block;
}

/** Source of a code / HTML block (fences excluded). */
function codeText(node: Node): string {
  const parts: string[] = [];
  for (const l of node.own) if (l.role === 'code' || (node.kind === 'html' && l.role === 'text')) parts.push(l.view.text);
  return parts.join('\n');
}

/** Result of reading property / planning lines and tokens. */
interface LineScan {
  id: string | undefined;
  extraTags: string[];
}

/**
 * Read `key:: value` property lines (into `props`), mark Logseq planning lines and find the
 * metadata tokens of every text line. Returns a Logseq `id::` (when no `^id` was found) and
 * tags given by `tags::` properties.
 */
function readLines(node: Node, infos: LineInfo[], marker: TaskMarker | null, props: Record<string, PropertyValue>, id: string | undefined): LineScan {
  const extraTags: string[] = [];
  const propertyKinds = node.kind !== 'table' && node.kind !== 'heading' && node.kind !== 'callout';
  for (let k = 0; k < infos.length; k++) {
    const info = infos[k]!;
    if (info.cls !== 'text') continue;
    const m = info.masked;
    info.tokens = findTokens(m);
    const propertyCandidate = propertyKinds && !(k === 0 && marker) && m.indexOf('::') !== -1;
    const planningCandidate = k > 0 && (m.indexOf('SCHEDULED') !== -1 || m.indexOf('DEADLINE') !== -1);
    if (!propertyCandidate && !planningCandidate) continue;
    const begin = k === 0 && marker ? marker.contentStart : info.own.start;
    const content = m.slice(begin);
    const lead = content.length - content.trimStart().length;
    const pm = propertyCandidate ? PROPERTY_RE.exec(content.slice(lead)) : null;
    if (pm) {
      const key = pm[1]!.toLowerCase();
      const valueStart = begin + lead + pm[1]!.length + 2;
      let valueEnd = info.own.view.text.length;
      if (info.extra) for (const [a] of info.extra) if (a >= valueStart && a < valueEnd) valueEnd = a;
      const rawValue = info.own.view.text.slice(valueStart, valueEnd).trim();
      const value = parsePropertyValue(key, rawValue);
      setProperty(props, key, value);
      if (key === 'id' && rawValue && !id && node.kind !== 'frontmatter') id = rawValue;
      if (TAG_PROPERTY_KEYS.has(key) && Array.isArray(value)) for (const v of value) extraTags.push(v);
      info.cls = 'property';
    } else if (planningCandidate && PLANNING_LINE_RE.test(content)) info.cls = 'planning';
  }
  return { id, extraTags };
}

/** Block text from its lines; also returns the cleaned first line (task text). */
function blockText(node: Node, infos: LineInfo[], marker: TaskMarker | null): { text: string; first: string } {
  const parts: string[] = [];
  let first = '';
  for (let k = 0; k < infos.length; k++) {
    const info = infos[k]!;
    if (info.cls === 'blank') parts.push('');
    else if (info.cls === 'code') {
      if (node.kind === 'listItem') parts.push(info.own.view.text.slice(info.own.start));
    } else if (info.cls === 'text') {
      let line = cleanLine(info, k === 0 && marker ? marker.contentStart : info.own.start);
      if (node.kind === 'heading') line = line.replace(CLOSING_HASHES_RE, '');
      if (parts.length === 0) line = line.trimStart();
      if (k === 0) first = line.trim();
      parts.push(line);
    }
  }
  return { text: node.kind === 'table' ? tableText(node) : joinText(parts), first };
}

/** First `🔁` rule on any line of the block. */
function blockRecurrenceOf(infos: LineInfo[]): string | undefined {
  for (const info of infos) {
    for (const t of info.tokens) if (t.kind === 'recurrence' && t.value) return recurrenceOf(t);
  }
  return undefined;
}

/** Reminders of a block (any text line, including property lines). */
function blockReminders(infos: LineInfo[], block: Block, ctx: Ctx): void {
  let recurrence: string | undefined;
  let looked = false;
  for (const info of infos) {
    for (const t of info.tokens) {
      if ((t.kind !== 'reminder' && t.kind !== 'reminderCompat') || !t.value) continue;
      if (!looked) {
        recurrence = block.task?.recurrence ?? blockRecurrenceOf(infos);
        looked = true;
      }
      const at = `${t.value}T${t.time ?? ctx.defaultTime}` as LocalDateTime;
      const r: Reminder = { at, alarm: !!t.alarm, text: block.text, line: info.own.view.line };
      if (recurrence) r.recurrence = recurrence;
      block.reminders.push(r);
    }
  }
}

/** Build one {@link Block} (without children) from a structural node. */
function buildBlock(node: Node, ctx: Ctx): Block {
  const block = blockShell(node, ctx);
  if (node.kind === 'code' || node.kind === 'html') {
    block.text = codeText(node);
    if (node.trailingId) block.id = node.trailingId;
    return block;
  }
  if (node.kind === 'frontmatter' && node.own[0]?.role === 'code') return block; // YAML: page-level only

  const infos = classifyLines(node);
  const scan = maskScannedLines(infos);
  const first = infos[0];
  const marker =
    node.kind === 'listItem' && first?.cls === 'text' ? matchTaskMarker(first.own.view.text, first.masked, first.own.start) : null;
  if (marker) addExtra(first!, marker.from, marker.contentStart);

  // Block id: `^id` on the first/last line, a standalone `^id` after the block, or `id::`.
  const caretId = (node.kind === 'frontmatter' ? undefined : findBlockId(infos)) ?? node.trailingId;
  const lines = readLines(node, infos, marker, block.properties, caretId);
  if (lines.id) block.id = lines.id;

  const inline = scanInline(scan.orig, scan.masked, scan.seg);
  for (const l of inline.links) ctx.links.push(l);
  if (inline.tags.length || lines.extraTags.length) {
    const names: string[] = [];
    for (const t of inline.tags) names.push(t.name);
    for (const t of lines.extraTags) names.push(t);
    block.tags = uniqueTagKeys(names);
  }

  const text = blockText(node, infos, marker);
  block.text = text.text;
  if (marker) {
    const meta: TaskMeta = {};
    taskMetaFromTokens(first!.tokens, meta);
    for (const info of infos) taskMetaFromPlanning(info.tokens, meta);
    block.task = buildTask(marker, meta, text.first, node.line);
  }
  blockReminders(infos, block, ctx);
  return block;
}

/** A table's text is its source rows (formulas and cells kept), without a trailing `^id` line. */
function tableText(node: Node): string {
  return node.own
    .filter((l) => l.role !== 'meta' || l.view.line !== node.trailingIdLine)
    .map((l) => l.view.text.trim())
    .join('\n');
}

function uniqueTagKeys(names: string[]): string[] {
  if (names.length === 0) return [];
  const out: string[] = [];
  const seen = new Set<string>();
  for (const n of names) {
    const k = tagKey(n);
    if (!k || seen.has(k)) continue;
    seen.add(k);
    out.push(k);
  }
  return out;
}

/** Default reminder time from the config ("HH:mm"), falling back to 09:00. */
function defaultTimeOf(config: VaultConfig): string {
  return normalizeTime(config.defaultReminderTime ?? '') ?? DEFAULT_VAULT_CONFIG.defaultReminderTime;
}

/** Wiki links / embeds written in YAML frontmatter values (`related: "[[Page]]"`). */
function frontmatterLinks(yaml: string, yamlStart: number, firstLine: number): Link[] {
  const res = scanInline(yaml, maskInline(yaml, { htmlComments: false }), segmentsForText(yaml, yamlStart, firstLine));
  return res.links.filter((l) => l.kind === 'wiki' || l.kind === 'embed');
}

/**
 * Parse a note. `path` gives the title (file base name) and, with `config`, the journal date.
 * Never throws for any input; runs in linear time.
 */
export function parseNote(path: VaultPath, content: string, config: VaultConfig = DEFAULT_VAULT_CONFIG): ParsedNote {
  const st = parseStructure(content);
  const ctx: Ctx = { defaultTime: defaultTimeOf(config), lineTexts: st.lines.texts, links: [] };
  const frontmatter = st.frontmatter ? parseYaml(st.frontmatter.yaml) : {};
  if (st.frontmatter) ctx.links.push(...frontmatterLinks(st.frontmatter.yaml, st.frontmatter.yamlStart, 1));

  const fmProps = frontmatterProperties(frontmatter);
  const blocks: Block[] = [];
  const allBlocks: Block[] = [];
  const headings: Heading[] = [];
  const tasks: Task[] = [];
  const reminders: Reminder[] = [];
  // Prototype-free maps: `blockIds[id]` / `properties[key]` are safe for any user-written key.
  const blockIds = emptyMap<Block>();
  let pageProps = mergeMaps(fmProps);
  let logseqProps = emptyMap<PropertyValue>();

  // Iterative pre-order walk (deep outlines must not overflow the stack).
  const todo: { node: Node; parent: Block | null }[] = [];
  for (let k = st.roots.length - 1; k >= 0; k--) todo.push({ node: st.roots[k]!, parent: null });
  while (todo.length) {
    const { node, parent } = todo.pop()!;
    const block = buildBlock(node, ctx);
    (parent ? parent.children : blocks).push(block);
    allBlocks.push(block);
    for (let k = node.children.length - 1; k >= 0; k--) todo.push({ node: node.children[k]!, parent: block });
    if (node.kind === 'frontmatter') {
      if (node.own[0]?.role === 'code') block.properties = mergeMaps(fmProps);
      else {
        logseqProps = mergeMaps(logseqProps, block.properties);
        pageProps = mergeMaps(pageProps, block.properties);
      }
    }
    if (block.kind === 'heading' && !parent && block.headingLevel) {
      headings.push({ level: block.headingLevel, text: block.text, line: block.line });
    }
    if (block.task) tasks.push(block.task);
    for (const r of block.reminders) reminders.push(r);
    if (block.id && !Object.prototype.hasOwnProperty.call(blockIds, block.id)) setOwnBlock(blockIds, block.id, block);
  }

  const aliases = uniqueCaseInsensitive([
    ...namesFrom(frontmatterGet(frontmatter, 'aliases', 'alias'), false),
    ...namesFrom(logseqProps.alias ?? logseqProps.aliases ?? [], false),
  ]);
  const tags = uniqueTagKeys([
    ...namesFrom(frontmatterGet(frontmatter, 'tags', 'tag'), true),
    ...namesFrom(logseqProps.tags ?? logseqProps.tag ?? [], false),
    ...allBlocks.flatMap((b) => b.tags),
  ]);
  ctx.links.sort((a, b) => a.from - b.from);

  const note: ParsedNote = {
    path,
    title: pageTitle(path),
    frontmatter,
    properties: pageProps,
    aliases,
    tags,
    blocks,
    allBlocks,
    links: ctx.links,
    headings,
    tasks,
    reminders,
    blockIds,
  };
  const journalDate = journalDateFromPath(path, config);
  if (journalDate) note.journalDate = journalDate;
  return note;
}

function setOwnBlock(map: Record<string, Block>, id: string, block: Block): void {
  if (id === '__proto__') Object.defineProperty(map, id, { value: block, enumerable: true, writable: true, configurable: true });
  else map[id] = block;
}

/** Innermost block of a parsed note containing `line` (own lines preferred over parents). */
export function blockAtLine(note: ParsedNote, line: number): Block | null {
  let best: Block | null = null;
  let level: Block[] = note.blocks;
  for (;;) {
    const hit = level.find((b) => b.line <= line && line <= b.endLine);
    if (!hit) return best;
    best = hit;
    if (!hit.children.some((c) => c.line <= line && line <= c.endLine)) return hit;
    level = hit.children;
  }
}
