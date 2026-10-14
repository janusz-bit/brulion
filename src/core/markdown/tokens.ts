/**
 * Metadata tokens (docs/SYNTAX.md §5-6): Obsidian Tasks emoji signifiers, Brulion reminders
 * (`⏰`, `⏰!`), obsidian-reminder `(@date)`, Logseq priorities `[#A]` and planning items
 * (`SCHEDULED: <…>`, `DEADLINE: <…>`), plus block ids (` ^abc123`).
 *
 * Token detection runs on *masked* text (code spans / comments blanked, see ./mask.ts) so
 * tokens inside inline code are ignored; ranges are valid in the original text.
 */
import { canonicalRecurrence, normalizeTime, isLocalDate, toFileDateTime } from '@/core/dates';
import type { LocalDateTime, Priority } from '../types';
import { maskInline } from './mask';
import { splitLines } from './lines';

/** Emoji signifiers written by Brulion (Obsidian Tasks compatible) — SYNTAX §5-6. */
export const TOKENS = {
  due: '📅',
  scheduled: '⏳',
  start: '🛫',
  created: '➕',
  done: '✅',
  cancelled: '❌',
  recurrence: '🔁',
  reminder: '⏰',
  alarm: '⏰!',
  /** Obsidian Tasks task id / dependency / on-completion signifiers (recognised and stripped). */
  id: '🆔',
  dependsOn: '⛔',
  onCompletion: '🏁',
  priority: {
    highest: '🔺',
    high: '⏫',
    medium: '🔼',
    low: '🔽',
    lowest: '⏬',
  },
} as const;

/** Date fields carried by emoji tokens. */
export type DateField = 'due' | 'scheduled' | 'start' | 'created' | 'done' | 'cancelled';

export type TokenKind =
  | DateField
  | 'recurrence'
  | 'priority'
  | 'reminder'
  | 'reminderCompat'
  | 'taskId'
  | 'dependsOn'
  | 'onCompletion'
  | 'logseqPriority'
  | 'planning';

/** A metadata token found in a line. Offsets are relative to the scanned string. */
export interface Token {
  kind: TokenKind;
  /** Start of the whole token (emoji / bracket). */
  from: number;
  /** End of the whole token (exclusive). */
  to: number;
  /** Date ("YYYY-MM-DD") for date, reminder and planning tokens; rule text for recurrence; id text. */
  value?: string;
  /** Normalised "HH:mm" when the date has a valid time. */
  time?: string;
  /** Range of the date(-time) value inside the token (for rewriting dates). */
  valueFrom?: number;
  valueTo?: number;
  /** `⏰!` alarm. */
  alarm?: boolean;
  priority?: Priority;
  /** Planning items: which task field (`SCHEDULED` → scheduled, `DEADLINE` → due). */
  field?: 'scheduled' | 'due';
  /** Planning items: Logseq repeater such as "+1w" or ".+1d". */
  repeater?: string;
  /** Recurrence: canonical English rule when parseable (else undefined, raw text in `value`). */
  canonical?: string;
}

const DATE_EMOJI: Readonly<Record<string, DateField>> = {
  '📅': 'due',
  '📆': 'due',
  '🗓': 'due',
  '⏳': 'scheduled',
  '⌛': 'scheduled',
  '🛫': 'start',
  '➕': 'created',
  '✅': 'done',
  '❌': 'cancelled',
};

const PRIORITY_EMOJI: Readonly<Record<string, Priority>> = {
  '🔺': 'highest',
  '⏫': 'high',
  '🔼': 'medium',
  '🔽': 'low',
  '⏬': 'lowest',
};

/** Logseq priorities `[#A]` / `[#B]` / `[#C]`. */
export const LOGSEQ_PRIORITY: Readonly<Record<string, Priority>> = { A: 'high', B: 'medium', C: 'low' };

const DATE = String.raw`(\d{4}-\d{2}-\d{2})`;
const TIME = String.raw`(?:(?:[ \t]+|T)(\d{1,2}:\d{2}))?(?![\d:])`;
const VS = '\uFE0F?';

const TOKEN_RE = new RegExp(
  [
    `(📅|📆|🗓|⏳|⌛|🛫|➕|✅|❌)${VS}[ \\t]*${DATE}${TIME}`, // 1 emoji, 2 date, 3 time
    `⏰${VS}(!)?[ \\t]*${DATE}${TIME}`, // 4 alarm, 5 date, 6 time
    String.raw`\(@(\d{4}-\d{2}-\d{2})(?:[ \t]+(\d{1,2}:\d{2}))?\)`, // 7 date, 8 time
    `(🔁)${VS}`, // 9
    `(🔺|⏫|🔼|🔽|⏬)${VS}`, // 10
    `🆔${VS}[ \\t]*([A-Za-z0-9_-]+)`, // 11
    `⛔${VS}[ \\t]*([A-Za-z0-9_-]+(?:[ \\t]*,[ \\t]*[A-Za-z0-9_-]+)*)`, // 12
    `🏁${VS}[ \\t]*([A-Za-z]+)`, // 13
    String.raw`\[#([ABC])\]`, // 14
    String.raw`(SCHEDULED|DEADLINE):[ \t]*<([^>\r\n]*)>`, // 15 keyword, 16 inner
  ].join('|'),
  'gu',
);

/** Where a recurrence rule text ends: the next token, a tag, a trailing block id, a line break. */
const RECURRENCE_STOP_RE =
  /📅|📆|🗓|⏳|⌛|🛫|➕|✅|❌|🔁|⏰|🔺|⏫|🔼|🔽|⏬|🆔|⛔|🏁|\(@\d{4}-|\[#[ABC]\]|[ \t]#(?=[\p{L}\p{N}_[])|[ \t]\^[A-Za-z0-9-]+[ \t]*(?=$|[\r\n])|%%|SCHEDULED:|DEADLINE:|[\r\n]/gu;

/** Cheap pre-check: can `s` contain any token at all? */
const MAYBE_TOKEN_RE = /[⌛⏫⏬⏰⏳⛔✅❌➕\uD83C\uD83D]|\(@|\[#|SCHEDULED|DEADLINE/;

/** Parse the inside of a Logseq timestamp `<2026-10-15 Thu 10:00 .+1w>`. */
export function parseLogseqTimestamp(inner: string): { date: string; time?: string; repeater?: string } | null {
  const dm = /^\s*(\d{4}-\d{2}-\d{2})/.exec(inner);
  if (!dm || !isLocalDate(dm[1]!)) return null;
  const rest = inner.slice(dm[0].length);
  const out: { date: string; time?: string; repeater?: string } = { date: dm[1]! };
  const tm = /(?:^|\s)(\d{1,2}:\d{2})(?:-\d{1,2}:\d{2})?(?=\s|$)/.exec(rest);
  const time = tm ? normalizeTime(tm[1]!) : null;
  if (time) out.time = time;
  const rm = /(?:^|\s)(\.\+|\+\+|\+)(\d+[hdwmy])(?=\s|$)/i.exec(rest);
  if (rm) out.repeater = rm[1]! + rm[2]!;
  return out;
}

/** Fill date/time fields of a date-like token, validating the date and time. */
function dateToken(
  kind: TokenKind,
  from: number,
  fullEnd: number,
  date: string,
  dateFrom: number,
  rawTime: string | undefined,
): Token | null {
  if (!isLocalDate(date)) return null;
  const tok: Token = { kind, from, to: fullEnd, value: date, valueFrom: dateFrom, valueTo: fullEnd };
  const time = rawTime === undefined ? null : normalizeTime(rawTime);
  if (time) tok.time = time;
  else if (rawTime !== undefined) {
    // Invalid time ("25:00"): the token ends after the date, the rest stays text.
    tok.to = dateFrom + date.length;
    tok.valueTo = tok.to;
  }
  return tok;
}

/**
 * Find all metadata tokens in `masked` (a line or a block's text; code spans and comments
 * should already be masked). Tokens are returned in order and never overlap. Malformed dates
 * (e.g. `📅 2026-13-45`) are not tokens.
 */
export function findTokens(masked: string): Token[] {
  if (!MAYBE_TOKEN_RE.test(masked)) return [];
  const out: Token[] = [];
  const re = TOKEN_RE;
  re.lastIndex = 0;
  let m: RegExpExecArray | null;
  while ((m = re.exec(masked)) !== null) {
    const from = m.index;
    const end = from + m[0].length;
    let tok: Token | null = null;
    if (m[1] !== undefined) {
      const date = m[2]!;
      tok = dateToken(DATE_EMOJI[m[1]] ?? 'due', from, end, date, masked.indexOf(date, from), m[3]);
    } else if (m[5] !== undefined) {
      tok = dateToken('reminder', from, end, m[5], masked.indexOf(m[5], from), m[6]);
      if (tok) tok.alarm = m[4] === '!';
    } else if (m[7] !== undefined) {
      tok = dateToken('reminderCompat', from, end, m[7], from + 2, m[8]);
      if (tok) {
        // The closing parenthesis always belongs to the token.
        tok.to = end;
        tok.valueTo = end - 1;
        if (!tok.time && m[8] !== undefined) tok = null;
      }
    } else if (m[9] !== undefined) {
      tok = recurrenceToken(masked, from, end);
      re.lastIndex = tok.to;
    } else if (m[10] !== undefined) {
      tok = { kind: 'priority', from, to: end, priority: PRIORITY_EMOJI[m[10]] ?? 'medium' };
    } else if (m[11] !== undefined) {
      tok = { kind: 'taskId', from, to: end, value: m[11] };
    } else if (m[12] !== undefined) {
      tok = { kind: 'dependsOn', from, to: end, value: m[12] };
    } else if (m[13] !== undefined) {
      tok = { kind: 'onCompletion', from, to: end, value: m[13] };
    } else if (m[14] !== undefined) {
      tok = { kind: 'logseqPriority', from, to: end, priority: LOGSEQ_PRIORITY[m[14]] ?? 'medium' };
    } else if (m[15] !== undefined) {
      const ts = parseLogseqTimestamp(m[16] ?? '');
      if (ts) {
        const innerFrom = masked.indexOf('<', from) + 1;
        tok = {
          kind: 'planning',
          from,
          to: end,
          value: ts.date,
          field: m[15] === 'DEADLINE' ? 'due' : 'scheduled',
          valueFrom: innerFrom,
          valueTo: end - 1,
        };
        if (ts.time) tok.time = ts.time;
        if (ts.repeater) tok.repeater = ts.repeater;
      }
    }
    if (tok) out.push(tok);
  }
  return out;
}

/** Recurrence token: `🔁` followed by the rule text up to the next token / tag / line end. */
function recurrenceToken(s: string, from: number, emojiEnd: number): Token {
  const stop = RECURRENCE_STOP_RE;
  stop.lastIndex = emojiEnd;
  const sm = stop.exec(s);
  const end = sm ? sm.index : s.length;
  const raw = s.slice(emojiEnd, end);
  const value = raw.trim();
  const lead = raw.length - raw.trimStart().length;
  const tok: Token = {
    kind: 'recurrence',
    from,
    to: emojiEnd + lead + value.length,
    value,
    valueFrom: emojiEnd + lead,
    valueTo: emojiEnd + lead + value.length,
  };
  const canonical = value ? canonicalRecurrence(value) : null;
  if (canonical) tok.canonical = canonical;
  return tok;
}

/** Recurrence rule of a token: canonical form when parseable, otherwise the raw text (or undefined). */
export function recurrenceOf(tok: Token): string | undefined {
  return tok.canonical ?? (tok.value ? tok.value : undefined);
}

const BLOCK_ID_END_RE = /(^|[ \t])\^([A-Za-z0-9-]+)[ \t]*$/;

/** Block id (` ^abc-123`) at the end of a single line: id and the range to remove. */
export function blockIdAtEnd(line: string): { id: string; from: number; to: number } | null {
  if (line.indexOf('^') === -1) return null;
  const m = BLOCK_ID_END_RE.exec(line);
  if (!m) return null;
  return { id: m[2]!, from: m.index, to: line.length };
}

/** True when the whole line is just a block id (`^abc123`). */
export function isStandaloneBlockId(line: string): string | null {
  const m = /^[ \t]*\^([A-Za-z0-9-]+)[ \t]*$/.exec(line);
  return m ? m[1]! : null;
}

/**
 * Remove [from, to) ranges from `text`, also dropping the whitespace that separated each
 * removed range from the preceding text (or, at a line start, the whitespace after it), so
 * "a 📅 2026-10-15 b" becomes "a b". Ranges may be unsorted; overlapping ranges are merged.
 */
export function removeRanges(text: string, ranges: readonly (readonly [number, number])[]): string {
  if (ranges.length === 0) return text;
  const sorted = [...ranges].sort((a, b) => a[0] - b[0]);
  let out = '';
  let last = 0;
  for (const [rf, rt] of sorted) {
    let from = Math.max(rf, last);
    let to = Math.max(rt, from);
    if (to <= last) continue;
    while (from > last && (text[from - 1] === ' ' || text[from - 1] === '\t')) from--;
    const atLineStart = from === 0 || text[from - 1] === '\n' || text[from - 1] === '\r';
    if (atLineStart) while (to < text.length && (text[to] === ' ' || text[to] === '\t')) to++;
    out += text.slice(last, from);
    last = to;
  }
  return out + text.slice(last);
}

/**
 * Remove all metadata tokens (task emoji signifiers with their values, reminders, Logseq
 * priorities and planning items, block ids at line ends) from `text`. Tokens inside inline
 * code are kept. Trailing whitespace of each line and of the whole text is trimmed.
 */
export function stripTokens(text: string): string {
  const masked = maskInline(text);
  const ranges: [number, number][] = findTokens(masked).map((t): [number, number] => [t.from, t.to]);
  if (masked.indexOf('^') !== -1) {
    const lines = splitLines(masked);
    for (let i = 0; i < lines.texts.length; i++) {
      const id = blockIdAtEnd(lines.texts[i]!);
      if (id) ranges.push([lines.starts[i]! + id.from, lines.starts[i]! + id.to]);
    }
  }
  const stripped = removeRanges(text, ranges);
  return stripped
    .split(/(\r\n|\r|\n)/)
    .map((part, i) => (i % 2 === 0 ? part.trimEnd() : part))
    .join('')
    .trim();
}

/** `⏰ 2026-10-15 09:00` / `⏰! 2026-10-15 06:30` (a bare date stays a bare date). */
export function formatReminderToken(at: LocalDateTime, alarm = false): string {
  return `${alarm ? TOKENS.alarm : TOKENS.reminder} ${toFileDateTime(at)}`;
}

/** Emoji for a date field. */
export function dateFieldEmoji(field: DateField): string {
  return TOKENS[field];
}
