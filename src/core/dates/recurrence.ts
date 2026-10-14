/**
 * Recurrence rules (docs/SYNTAX.md §7): parsing (canonical English / Obsidian Tasks and
 * Polish), formatting, Logseq repeaters and occurrence computation.
 */
import type { LocalDate, LocalDateTime, Locale } from '../types';
import { foldText } from './fold';
import {
  addDaysLocal,
  addMonthsLocal,
  dayNumber,
  daysInMonth,
  formatYmd,
  fromDayNumber,
  joinDateTime,
  localParts,
  splitLocal,
  weekdayOf,
} from './local';
import { EN_WEEKDAYS, NUMBER_WORDS, ORDINAL_WORDS, PL_WEEKDAYS_ACC, WEEKDAY_LOOKUP, alternation, own } from './names';

/**
 * A recurrence rule.
 *
 * * `weekdays` — for `freq: 'week'`: days of the week (0 = Sunday … 6 = Saturday), stored
 *   unique in Monday-first order. Ignored for other frequencies.
 * * `monthDay` — for `freq: 'month'` only: day of month (clamped to the month's length) or
 *   `'last'`. Without it the base date's day of month is used. Yearly rules repeat on the
 *   base date's month and day (Feb 29 -> Feb 28 in common years).
 * * `weekdaysOnly` — business days: with `freq: 'day'` every `interval`-th Monday–Friday
 *   ("every weekday"); with `freq: 'week'` and no `weekdays`, Monday–Friday of every
 *   `interval`-th week.
 * * `whenDone` — the next occurrence is computed from the completion date.
 *
 * Multi-week intervals count Monday-based (ISO) weeks, like Obsidian Tasks / rrule.
 */
export interface RecurrenceRule {
  freq: 'day' | 'week' | 'month' | 'year';
  interval: number;
  weekdays?: number[];
  monthDay?: number | 'last';
  weekdaysOnly?: boolean;
  whenDone?: boolean;
}

type Freq = RecurrenceRule['freq'];

const FREQS: ReadonlySet<string> = new Set<Freq>(['day', 'week', 'month', 'year']);

/**
 * Whether the rule's frequency is one of the four supported ones. Rules may come from
 * untyped sources (JSON, UI drafts); the public functions treat anything else as "no rule".
 */
function hasKnownFreq(rule: RecurrenceRule): boolean {
  return FREQS.has(rule.freq);
}

const MONDAY_FIRST = (d: number): number => (d + 6) % 7;
const WORKDAYS = [1, 2, 3, 4, 5];

/**
 * Largest accepted interval. Bigger values are rejected by the parsers and clamped by
 * {@link normalizeRule}; they could only produce dates beyond year 9999 and would not
 * round-trip through formatting (`String(1e21)` is "1e+21").
 */
export const MAX_INTERVAL = 99_999;

/**
 * Clean a rule: integer interval 1…{@link MAX_INTERVAL} (non-finite or < 1 -> 1, larger ->
 * clamped), valid unique weekdays (Monday-first), valid month day.
 */
export function normalizeRule(rule: RecurrenceRule): RecurrenceRule {
  const interval =
    Number.isFinite(rule.interval) && rule.interval >= 1 ? Math.min(Math.floor(rule.interval), MAX_INTERVAL) : 1;
  const out: RecurrenceRule = { freq: rule.freq, interval };
  if (rule.freq === 'week' && rule.weekdays && rule.weekdays.length > 0) {
    const days = [...new Set(rule.weekdays.filter((d) => Number.isInteger(d) && d >= 0 && d <= 6))];
    if (days.length > 0) out.weekdays = days.sort((a, b) => MONDAY_FIRST(a) - MONDAY_FIRST(b));
  }
  if (rule.freq === 'month' && rule.monthDay !== undefined) {
    if (rule.monthDay === 'last') out.monthDay = 'last';
    else if (Number.isInteger(rule.monthDay) && rule.monthDay >= 1 && rule.monthDay <= 31) out.monthDay = rule.monthDay;
  }
  if (rule.weekdaysOnly && (rule.freq === 'day' || (rule.freq === 'week' && !out.weekdays))) out.weekdaysOnly = true;
  if (rule.whenDone) out.whenDone = true;
  return out;
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

/**
 * "when done" suffix (folded). Deliberately without a leading `[\s,;]+` group: that made the
 * regex backtrack quadratically on long runs of separators; the separator before the suffix
 * is checked by {@link splitWhenDone} instead.
 */
const WHEN_DONE_RE = /(?:when done|po wykonaniu|po ukonczeniu|od wykonania|od ukonczenia)$/;

/** Separators that may trail a rule or precede the "when done" suffix. */
const RULE_SEPARATORS = new Set([' ', ',', ';']);

/** Remove trailing characters contained in `chars` (linear; regexes like /[,;\s]+$/ backtrack). */
function trimEndChars(s: string, chars: ReadonlySet<string>): string {
  let end = s.length;
  while (end > 0 && chars.has(s.charAt(end - 1))) end--;
  return s.slice(0, end);
}

/** Split "<body> when done" into the body and the flag. Input is folded and whitespace-collapsed. */
function splitWhenDone(s: string): { body: string; whenDone: boolean } {
  const m = WHEN_DONE_RE.exec(s);
  if (!m) return { body: s, whenDone: false };
  const before = s.charAt(m.index - 1);
  if (m.index > 0 && !RULE_SEPARATORS.has(before)) return { body: s, whenDone: false };
  return { body: trimEndChars(s.slice(0, m.index), RULE_SEPARATORS), whenDone: true };
}

const UNIT_WORDS: Record<string, Freq> = {
  day: 'day', days: 'day', dzien: 'day', dni: 'day', dnia: 'day',
  week: 'week', weeks: 'week', tydzien: 'week', tygodnie: 'week', tygodni: 'week', tygodnia: 'week',
  month: 'month', months: 'month', miesiac: 'month', miesiace: 'month', miesiecy: 'month', miesiaca: 'month',
  year: 'year', years: 'year', rok: 'year', roku: 'year', lata: 'year', lat: 'year',
};

/** Single-word rules: [frequency, interval]. */
const ADVERBS: Record<string, readonly [Freq, number]> = {
  daily: ['day', 1], codziennie: ['day', 1], dziennie: ['day', 1],
  weekly: ['week', 1], cotygodniowo: ['week', 1], tygodniowo: ['week', 1],
  fortnightly: ['week', 2], biweekly: ['week', 2],
  monthly: ['month', 1], comiesiecznie: ['month', 1], miesiecznie: ['month', 1],
  quarterly: ['month', 3], kwartalnie: ['month', 3], cokwartalnie: ['month', 3],
  semiannually: ['month', 6], polrocznie: ['month', 6], copolrocznie: ['month', 6],
  yearly: ['year', 1], annually: ['year', 1], corocznie: ['year', 1], rocznie: ['year', 1],
};

/**
 * Parse an interval word: digits (optionally with an English ordinal suffix, "2nd"), number
 * words ("dwa", "two") or ordinals ("drugi", "second", "other"). Null when unknown or above
 * {@link MAX_INTERVAL}.
 */
function parseCount(word: string | undefined): number | null {
  if (word === undefined) return 1;
  const digits = /^(\d+)(?:st|nd|rd|th)?$/.exec(word);
  const n = digits ? Number(digits[1]) : (NUMBER_WORDS.get(word) ?? ORDINAL_WORDS.get(word) ?? null);
  return n !== null && n <= MAX_INTERVAL ? n : null;
}

/**
 * Parse a list of weekday words: "monday, friday", "poniedzialek i piatek", "pn, wt",
 * "w srode", "we wtorek oraz czwartek", "mondays and fridays". Input must be folded.
 */
export function parseWeekdayList(folded: string): number[] | null {
  // Collapse whitespace first: the split regex below backtracks on long whitespace runs.
  const s = folded.replace(/\s+/g, ' ').trim().replace(/^(?:on|w|we) /, '');
  if (!s) return null;
  const items = s.split(/\s*(?:,|&|\+|\/)\s*|\s+(?:i|oraz|and|lub|or)\s+/);
  const days: number[] = [];
  for (const raw of items) {
    const item = raw.trim().replace(/^(?:on|w|we|the)\s+/, '').replace(/\.$/, '');
    if (!item) return null;
    const hit = WEEKDAY_LOOKUP.get(item);
    if (!hit) return null;
    if (!days.includes(hit.day)) days.push(hit.day);
  }
  return days.length > 0 ? days.sort((a, b) => MONDAY_FIRST(a) - MONDAY_FIRST(b)) : null;
}

/** Parse the "on the 15th" / "15." / "ostatniego" tail of a monthly rule. */
function parseMonthDayTail(tail: string): number | 'last' | null {
  const t = tail.trim();
  if (/^(?:(?:on|w)\s+)?(?:the\s+)?(?:ostatni(?:ego|m)?|last)(?:\s+(?:day|dnia|dzien|dniu))?(?:\s+(?:of the month|miesiaca))?$/.test(t)) {
    return 'last';
  }
  const m =
    /^(?:on\s+)?(?:the\s+)?(?:(?:dnia|w dniu)\s+)?(\d{1,2})(?:st|nd|rd|th|\.|-go|-ego|-tego)?(?:\s+(?:dnia|day))?(?:\s+(?:of the month|miesiaca))?$/.exec(t);
  if (!m) return null;
  const n = Number(m[1]);
  return n >= 1 && n <= 31 ? n : null;
}

/** Build a rule from a base frequency and an optional tail (weekday list / month day). */
function withTail(freq: Freq, interval: number, tail: string): RecurrenceRule | null {
  const t = tail.trim().replace(/^,\s*/, '');
  if (interval < 1) return null;
  if (!t) return { freq, interval };
  if (freq === 'week') {
    const days = parseWeekdayList(t);
    return days ? { freq, interval, weekdays: days } : null;
  }
  if (freq === 'month') {
    const md = parseMonthDayTail(t);
    return md === null ? null : { freq, interval, monthDay: md };
  }
  return null;
}

/** Interval: digits ("2", "2nd") or a known number/ordinal word (never a unit word like "miesiac"). */
const COUNT = `(\\d+(?:st|nd|rd|th)?|${alternation([...NUMBER_WORDS.keys(), ...ORDINAL_WORDS.keys()])})`;
const UNIT = '(days?|weeks?|months?|years?|dzien|dni|dnia|tydzien|tygodnie|tygodni|tygodnia|miesiac|miesiace|miesiecy|miesiaca|rok|roku|lata|lat)';
/** "every 2 weeks on …", "co 3 dni", "co drugi tydzien w piatek", "kazdy miesiac 15." */
const UNIT_RULE_RE = new RegExp(`^(?:every|co|kazd(?:y|a|e|ego)|each)(?: ${COUNT})? ${UNIT}(?![a-z])(.*)$`);
/** "every quarter", "co 2 kwartaly", "co kwartal 15." -> months x 3. */
const QUARTER_RULE_RE = new RegExp(
  `^(?:every|co|kazd(?:y|ego)|each)(?: ${COUNT})? (?:quarters?|kwartal(?:y|ow|u)?)(?![a-z])(.*)$`,
);
/** "every half year", "co pol roku" (+ month-day tail) -> every 6 months. */
const HALF_YEAR_RULE_RE = /^(?:every|co|each) (?:half(?: a)?[ -]year|pol roku)(?![a-z])(.*)$/;
/** "every 2 weekdays", "co 3 dni robocze". */
const WORKDAY_INTERVAL_RE = new RegExp(`^(?:every|co) ${COUNT} (?:weekdays|business days|working days|dni robocze|dni roboczych)$`);

/** Workday phrasings: [regex, interval group index or 0]. */
function parseWorkdays(s: string): RecurrenceRule | null {
  const fixed = [
    /^every (?:weekday|business day|working day|workday)$/,
    /^(?:on )?weekdays$/,
    /^(?:w )?dni (?:robocze|powszednie)$/,
    /^(?:co|kazdego) dni(?:a|e)? (?:robocz(?:y|ego|e)|powszedni(?:ego|e)?)$/,
    /^co dzien (?:roboczy|powszedni)$/,
    /^(?:od )?poniedzialku do piatku$/,
    /^(?:from )?monday (?:to|through|-) friday$/,
  ];
  if (fixed.some((re) => re.test(s))) return { freq: 'day', interval: 1, weekdaysOnly: true };
  const m = WORKDAY_INTERVAL_RE.exec(s);
  if (m) {
    const n = parseCount(m[1]);
    if (n !== null && n >= 1) return { freq: 'day', interval: n, weekdaysOnly: true };
  }
  return null;
}

/** Parse the rule body (folded, whitespace-collapsed, without the "when done" suffix). */
function parseBody(s: string): RecurrenceRule | null {
  const workdays = parseWorkdays(s);
  if (workdays) return workdays;

  // "every 2 weeks on monday", "every other day", "co 3 dni", "co drugi tydzien w piatek"
  let m = UNIT_RULE_RE.exec(s);
  if (m) {
    const freq = own(UNIT_WORDS, m[2] ?? '');
    const n = parseCount(m[1]);
    if (freq && n !== null) return withTail(freq, n, m[3] ?? '');
  }

  // Quarters and half-years are expressed in months ("co kwartał" -> "every 3 months").
  m = QUARTER_RULE_RE.exec(s);
  if (m) {
    const n = parseCount(m[1]);
    if (n !== null && 3 * n <= MAX_INTERVAL) return withTail('month', 3 * n, m[2] ?? '');
  }
  m = HALF_YEAR_RULE_RE.exec(s);
  if (m) return withTail('month', 6, m[1] ?? '');

  // "kazdego dnia/tygodnia/miesiaca/roku" (+ tail)
  m = /^kazdego (dnia|tygodnia|miesiaca|roku)(?![a-z])(.*)$/.exec(s);
  if (m) {
    const freq = own(UNIT_WORDS, m[1] ?? '');
    if (freq) return withTail(freq, 1, m[2] ?? '');
  }

  // Adverbs: "daily", "codziennie", "co tydzien" handled above; "weekly on monday"
  m = /^([a-z]+)(?![a-z])(.*)$/.exec(s);
  if (m) {
    const adverb = own(ADVERBS, m[1] ?? '');
    if (adverb) return withTail(adverb[0], adverb[1], m[2] ?? '');
  }

  // Weekday shorthands: "every monday and friday", "co poniedzialek", "w kazdy piatek",
  // "w poniedzialki i czwartki", "mondays". After 🔁 a bare weekday list ("pn, pt") is
  // read as weekly on those days too.
  m = /^(?:(?:every|each|co|(?:w |we )?kazd(?:y|a|e|ego)|w|we|on)\s+)?(.+)$/.exec(s);
  if (m) {
    const days = parseWeekdayList(m[1] ?? '');
    if (days) return { freq: 'week', interval: 1, weekdays: days };
  }
  return null;
}

/**
 * Parse a recurrence rule written in canonical English (Obsidian Tasks: "every 2 weeks on
 * Monday, Friday", "every month on the last", "every weekday when done") or Polish
 * ("co tydzień w poniedziałek, piątek", "co miesiąc 15.", "w dni robocze po wykonaniu").
 * Case-insensitive, diacritics optional, an optional leading 🔁 is ignored.
 * Returns null for unsupported or malformed text (e.g. "every month on the 2nd Tuesday").
 */
export function parseRecurrence(text: string): RecurrenceRule | null {
  // Folding already drops the emoji variation selector (U+FE0F is a combining mark).
  const folded = foldText(text)
    .replace(/^[\s\u{1F501}\u{1F502}]+/u, '')
    .replace(/\s+/g, ' ');
  // Trailing separators are tolerated ("every week on Monday, Friday,").
  const { body: s, whenDone } = splitWhenDone(trimEndChars(folded, RULE_SEPARATORS).trim());
  if (!s) return null;
  const rule = parseBody(s);
  if (!rule) return null;
  if (whenDone) rule.whenDone = true;
  return normalizeRule(rule);
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

/** English ordinal suffix: 1st 2nd 3rd 4th 11th 12th 13th 21st 22nd 23rd 31st. */
export function ordinalEn(n: number): string {
  const mod100 = n % 100;
  if (mod100 >= 11 && mod100 <= 13) return `${n}th`;
  const mod10 = n % 10;
  return `${n}${mod10 === 1 ? 'st' : mod10 === 2 ? 'nd' : mod10 === 3 ? 'rd' : 'th'}`;
}

/** Polish plural form: one (1), few (2-4, 22-24 …), many (rest). */
export function plPlural(n: number, one: string, few: string, many: string): string {
  if (n === 1) return one;
  const mod10 = n % 10;
  const mod100 = n % 100;
  return mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14) ? few : many;
}

function formatEn(rule: RecurrenceRule): string {
  const n = rule.interval;
  switch (rule.freq) {
    case 'day':
      if (rule.weekdaysOnly) return n === 1 ? 'every weekday' : `every ${n} weekdays`;
      return n === 1 ? 'every day' : `every ${n} days`;
    case 'week': {
      const base = n === 1 ? 'every week' : `every ${n} weeks`;
      const days = rule.weekdays ?? (rule.weekdaysOnly ? WORKDAYS : undefined);
      return days ? `${base} on ${days.map((d) => EN_WEEKDAYS[d]).join(', ')}` : base;
    }
    case 'month': {
      const base = n === 1 ? 'every month' : `every ${n} months`;
      if (rule.monthDay === 'last') return `${base} on the last`;
      if (typeof rule.monthDay === 'number') return `${base} on the ${ordinalEn(rule.monthDay)}`;
      return base;
    }
    case 'year':
      return n === 1 ? 'every year' : `every ${n} years`;
  }
}

function formatPl(rule: RecurrenceRule): string {
  const n = rule.interval;
  switch (rule.freq) {
    case 'day':
      if (rule.weekdaysOnly) return n === 1 ? 'w dni robocze' : `co ${n} dni robocze`;
      return n === 1 ? 'codziennie' : `co ${n} dni`;
    case 'week': {
      const base = n === 1 ? 'co tydzień' : `co ${n} ${plPlural(n, 'tydzień', 'tygodnie', 'tygodni')}`;
      const days = rule.weekdays ?? (rule.weekdaysOnly ? WORKDAYS : undefined);
      if (!days) return base;
      const first = days[0];
      const prep = first === 2 ? 'we' : 'w';
      return `${base} ${prep} ${days.map((d) => PL_WEEKDAYS_ACC[d]).join(', ')}`;
    }
    case 'month': {
      const base = n === 1 ? 'co miesiąc' : `co ${n} ${plPlural(n, 'miesiąc', 'miesiące', 'miesięcy')}`;
      if (rule.monthDay === 'last') return `${base} ostatniego`;
      if (typeof rule.monthDay === 'number') return `${base} ${rule.monthDay}.`;
      return base;
    }
    case 'year':
      return n === 1 ? 'co rok' : `co ${n} ${plPlural(n, 'rok', 'lata', 'lat')}`;
  }
}

/**
 * Format a rule. `'en'` produces the canonical Obsidian-Tasks-compatible text written into
 * files ("every week on Monday, Friday when done"); `'pl'` produces the Polish form for the UI
 * ("co tydzień w poniedziałek, piątek po wykonaniu"). Both round-trip through
 * {@link parseRecurrence}. A rule with an unknown `freq` formats as ''.
 */
export function formatRecurrence(rule: RecurrenceRule, locale: Locale): string {
  if (!hasKnownFreq(rule)) return '';
  const r = normalizeRule(rule);
  const body = locale === 'pl' ? formatPl(r) : formatEn(r);
  if (!r.whenDone) return body;
  return `${body} ${locale === 'pl' ? 'po wykonaniu' : 'when done'}`;
}

/** Parse any supported phrasing and return the canonical English text, or null. */
export function canonicalRecurrence(text: string): string | null {
  const rule = parseRecurrence(text);
  return rule ? formatRecurrence(rule, 'en') : null;
}

// ---------------------------------------------------------------------------
// Logseq repeaters
// ---------------------------------------------------------------------------

const REPEATER_UNITS: Record<string, Freq> = { d: 'day', w: 'week', m: 'month', y: 'year' };

/**
 * Convert a Logseq/org-mode repeater to a rule: `+1w` (shift from the original date),
 * `++2d` (catch-up: next future date in the series — treated like `+`), `.+1m` (from the
 * completion date → `whenDone`). Hour repeaters (`+3h`) and malformed input return null.
 */
export function repeaterToRule(repeater: string): RecurrenceRule | null {
  const m = /^(\.\+|\+\+|\+)(\d+)([a-z])$/i.exec(repeater.trim());
  if (!m) return null;
  const freq = own(REPEATER_UNITS, (m[3] ?? '').toLowerCase());
  const interval = Number(m[2]);
  if (!freq || interval < 1 || interval > MAX_INTERVAL) return null;
  const rule: RecurrenceRule = { freq, interval };
  if (m[1] === '.+') rule.whenDone = true;
  return rule;
}

/**
 * Inverse of {@link repeaterToRule} for rules Logseq can express (no weekdays, month day or
 * business-day restriction): `{freq:'week', interval:1}` -> "+1w", with `whenDone` -> ".+1w".
 */
export function ruleToRepeater(rule: RecurrenceRule): string | null {
  if (!hasKnownFreq(rule)) return null;
  const r = normalizeRule(rule);
  if (r.weekdays || r.monthDay !== undefined || r.weekdaysOnly) return null;
  const unit = r.freq === 'day' ? 'd' : r.freq === 'week' ? 'w' : r.freq === 'month' ? 'm' : 'y';
  return `${r.whenDone ? '.+' : '+'}${r.interval}${unit}`;
}

// ---------------------------------------------------------------------------
// Occurrences
// ---------------------------------------------------------------------------

/** Day-of-month / month context taken from the series' first date. */
interface SeriesContext {
  /** Day of month to aim for when the rule has no explicit `monthDay`. */
  day: number;
  /** Month (1-12) for yearly rules. */
  month: number;
}

function contextOf(date: LocalDate): SeriesContext {
  return { day: Number(date.slice(8, 10)), month: Number(date.slice(5, 7)) };
}

/** Concrete day for a month-day spec in a month (clamped; 'last' = last day). Throws RangeError beyond 0000-9999. */
function dayIn(year: number, month: number, md: number | 'last'): LocalDate {
  if (!Number.isInteger(year) || year < 0 || year > 9999) throw new RangeError(`Date out of range (year ${year})`);
  const dim = daysInMonth(year, month);
  return formatYmd(year, month, md === 'last' ? dim : Math.min(md, dim));
}

/** Move `n` business days (Mon-Fri) forward from `date`. */
function addBusinessDays(date: LocalDate, n: number): LocalDate {
  const start = dayNumber(Number(date.slice(0, 4)), Number(date.slice(5, 7)), Number(date.slice(8, 10)));
  const startDow = weekdayOf(date);
  // Whole weeks first (5 business days = 7 calendar days), then the remainder day by day.
  const weeks = Math.floor((n - 1) / 5);
  let day = start + weeks * 7;
  let dow = startDow;
  let left = n - weeks * 5;
  while (left > 0) {
    day += 1;
    dow = (dow + 1) % 7;
    if (dow !== 0 && dow !== 6) left -= 1;
  }
  return fromDayNumber(day);
}

/** The next date of the series strictly after `date` (a date already in the series or a base date). */
function step(rule: RecurrenceRule, date: LocalDate, ctx: SeriesContext): LocalDate {
  const n = rule.interval;
  switch (rule.freq) {
    case 'day':
      return rule.weekdaysOnly ? addBusinessDays(date, n) : addDaysLocal(date, n);
    case 'week': {
      const days = rule.weekdays ?? (rule.weekdaysOnly ? WORKDAYS : undefined);
      if (!days) return addDaysLocal(date, 7 * n);
      const pos = MONDAY_FIRST(weekdayOf(date));
      const later = days.map(MONDAY_FIRST).filter((p) => p > pos);
      if (later.length > 0) return addDaysLocal(date, Math.min(...later) - pos);
      const nextWeekMonday = addDaysLocal(date, -pos + 7 * n);
      return addDaysLocal(nextWeekMonday, Math.min(...days.map(MONDAY_FIRST)));
    }
    case 'month': {
      const md = rule.monthDay ?? ctx.day;
      const y = Number(date.slice(0, 4));
      const mo = Number(date.slice(5, 7));
      const here = dayIn(y, mo, md);
      if (here > date) return here;
      const target = addMonthsLocal(formatYmd(y, mo, 1), n);
      return dayIn(Number(target.slice(0, 4)), Number(target.slice(5, 7)), md);
    }
    case 'year': {
      const y = Number(date.slice(0, 4));
      const here = dayIn(y, ctx.month, ctx.day);
      if (here > date) return here;
      return dayIn(y + n, ctx.month, ctx.day);
    }
  }
}

/**
 * Next occurrence of `rule` strictly after `from` — or, for `whenDone` rules with a
 * `completedOn` date, strictly after the completion date. A date-time keeps its time
 * ("2026-10-14T09:00" every day -> "2026-10-15T09:00"); results are canonical LocalDate /
 * LocalDateTime ("T" separator, also for a "2026-10-14 09:00" input — use `toFileDateTime`
 * when writing a note).
 *
 * Monthly rules clamp to the month's length (Jan 31 + 1 month -> Feb 28/29); without an
 * explicit `monthDay` the base date's day is used, so chaining from a clamped date continues
 * from that day (use {@link occurrencesBetween} for series that remember the anchor's day).
 * Malformed `from` is returned unchanged, and so is `from` when the next date would lie beyond
 * 9999-12-31 or the rule has an unknown `freq` — callers can detect "no next occurrence" with
 * `compareLocal(next, from) <= 0`.
 */
export function nextOccurrence(
  rule: RecurrenceRule,
  from: LocalDate | LocalDateTime,
  completedOn?: LocalDate,
): LocalDate | LocalDateTime {
  const sp = splitLocal(from);
  if (!sp || !hasKnownFreq(rule)) return from;
  const r = normalizeRule(rule);
  const done = r.whenDone && completedOn ? splitLocal(completedOn)?.date : undefined;
  const base = done ?? sp.date;
  const next = safeStep(r, base, contextOf(base));
  return next === null ? from : joinDateTime(next, sp.time);
}

/** {@link step}, or null when the next date is outside the supported range (year > 9999). */
function safeStep(rule: RecurrenceRule, date: LocalDate, ctx: SeriesContext): LocalDate | null {
  try {
    return step(rule, date, ctx);
  } catch (e) {
    if (e instanceof RangeError) return null;
    throw e;
  }
}

/**
 * Jump close to `from` for long series, so old anchors don't iterate for years. Returns a
 * series position (not necessarily an occurrence) strictly before `from`, or null to start
 * at the anchor. Only used where shifting by whole periods keeps the series identical.
 */
function fastForward(rule: RecurrenceRule, anchor: LocalDate, from: LocalDate, ctx: SeriesContext): LocalDate | null {
  const a = localParts(anchor);
  const f = localParts(from);
  if (!a || !f) return null;
  if (rule.freq === 'day' && rule.weekdaysOnly) return fastForwardBusinessDays(rule, anchor, from, ctx);
  if (rule.freq === 'day' || rule.freq === 'week') {
    const period = rule.freq === 'day' ? rule.interval : 7 * rule.interval;
    const diff = dayNumber(f.year, f.month, f.day) - dayNumber(a.year, a.month, a.day);
    const k = Math.floor(diff / period) - 1;
    return k >= 1 ? addDaysLocal(anchor, k * period) : null;
  }
  const periodMonths = rule.freq === 'month' ? rule.interval : 12 * rule.interval;
  const diffMonths = (f.year - a.year) * 12 + (f.month - a.month);
  const k = Math.floor(diffMonths / periodMonths) - 1;
  if (k < 1) return null;
  if (rule.freq === 'month') {
    const target = addMonthsLocal(formatYmd(a.year, a.month, 1), k * periodMonths);
    return dayIn(Number(target.slice(0, 4)), Number(target.slice(5, 7)), rule.monthDay ?? ctx.day);
  }
  return dayIn(a.year + k * rule.interval, ctx.month, ctx.day);
}

/**
 * Business-day series: after the first step every occurrence is a weekday, and 5 steps of
 * `n` business days are exactly `n` calendar weeks, so whole blocks of 7·n days can be skipped.
 */
function fastForwardBusinessDays(rule: RecurrenceRule, anchor: LocalDate, from: LocalDate, ctx: SeriesContext): LocalDate | null {
  const first = safeStep(rule, anchor, ctx);
  const f = localParts(from);
  const p = first === null ? null : localParts(first);
  if (first === null || !f || !p || first >= from) return null;
  const period = 7 * rule.interval;
  const diff = dayNumber(f.year, f.month, f.day) - dayNumber(p.year, p.month, p.day);
  const k = Math.floor(diff / period) - 1;
  return k >= 1 ? addDaysLocal(first, k * period) : null;
}

/** Safety cap on loop iterations (the result count is capped separately by `max`). */
const MAX_STEPS = 200_000;

/** Default result cap of {@link occurrencesBetween}. */
const DEFAULT_MAX_OCCURRENCES = 500;

/**
 * All occurrences of a series starting at `anchor` that fall within [`from`, `to`]
 * (inclusive, compared by date). The anchor itself is the first occurrence (it counts even
 * if it doesn't match the rule's weekdays, like a task's original due date). Later
 * occurrences remember the anchor's day of month (Jan 31 monthly -> Feb 28, Mar 31, …).
 * Date-time anchors yield date-times with the anchor's time. `whenDone` is ignored here
 * (future completion dates are unknown). At most `max` results (fractions are floored, NaN
 * means the default 500). A rule with an unknown `freq` has no occurrences.
 */
export function occurrencesBetween(
  rule: RecurrenceRule,
  anchor: LocalDate | LocalDateTime,
  from: LocalDate,
  to: LocalDate,
  max = DEFAULT_MAX_OCCURRENCES,
): (LocalDate | LocalDateTime)[] {
  const limit = Number.isNaN(max) ? DEFAULT_MAX_OCCURRENCES : Math.floor(max);
  const a = splitLocal(anchor);
  const f = splitLocal(from)?.date;
  const t = splitLocal(to)?.date;
  if (!a || !f || !t || f > t || a.date > t || limit <= 0 || !hasKnownFreq(rule)) return [];
  const r = normalizeRule(rule);
  const ctx = contextOf(a.date);
  const out: (LocalDate | LocalDateTime)[] = [];
  if (a.date >= f) out.push(joinDateTime(a.date, a.time));
  let cur = (a.date < f ? fastForward(r, a.date, f, ctx) : null) ?? a.date;
  for (let i = 0; i < MAX_STEPS && out.length < limit; i++) {
    const next = safeStep(r, cur, ctx);
    if (next === null || next > t || next <= cur) break;
    if (next >= f) out.push(joinDateTime(next, a.time));
    cur = next;
  }
  return out;
}
