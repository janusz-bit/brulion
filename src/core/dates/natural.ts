/**
 * Natural-language date/time recognition (Polish + English) for editor input, quick capture
 * and the date picker (docs/SYNTAX.md §8).
 *
 * The parser scans the whole input for date and time phrases, so a phrase can be found
 * anywhere in a sentence and stripped by the caller:
 * "Zadzwoń do mamy jutro o 18" -> { date: tomorrow, time: "18:00", matched: "jutro o 18" }.
 *
 * How it works: the input is folded (lower-case, diacritics removed — users may type
 * "srode" for "środę") while keeping an index map back to the original. A set of
 * matchers produce candidate tokens (a date, a time, a time-of-day word or an exact
 * relative date-time). Overlaps are resolved longest-first, then adjacent compatible tokens
 * ("jutro" + "o 18", "15.10.2026" + "18:30", "Oct 15" + "6pm") are merged into groups and
 * the first valid group wins.
 *
 * Semantics worth knowing:
 * * A weekday alone ("piątek", "w piątek", "friday", "on friday") is the next such day
 *   **strictly after** today (on a Wednesday "w środę" means next week's Wednesday).
 * * "w przyszły/następny piątek", "next friday": the Friday of next week (per weekStartsOn).
 * * "ten piątek", "this friday": the next Friday on or after today.
 * * "zeszły/ubiegły/poprzedni piątek", "last friday": the most recent Friday before today.
 * * A time without a date ("o 18", "6pm", "wieczorem") is today if still ahead, else tomorrow.
 *   A bare hour before a time of day keeps the hour: "7 rano" = 07:00, "8 wieczorem" = 20:00.
 * * A date and a time may be separated by whitespace, one comma, colon or dash ("jutro: 18:00").
 * * Dates without a year that already passed this year are moved to the next year.
 * * "za 3 godziny", "in 30 minutes" yield an exact date + time relative to `now`.
 * * "w przyszłym tygodniu/miesiącu/roku", "next week/month/year": first day of that period.
 * * Midnight words and small hours at night after a named day mean the following night:
 *   "dziś o północy" / "tonight at midnight" = tomorrow 00:00, "jutro o 2 w nocy" = the day
 *   after tomorrow 02:00 (a numeric "jutro o 0:00" stays tomorrow 00:00). "12" with an
 *   evening word is midnight too ("tonight at 12", "wieczorem o 12").
 * * A unit without a number must be in the case its preposition governs: "za tydzień",
 *   "tydzień temu", "w ciągu tygodnia" (so the idiom "za dnia" is no date).
 *
 * To avoid false positives in prose: weekday abbreviations ("pt", "sat") are used only with
 * a preposition/modifier, next to another date/time token, or as the whole input; a bare
 * "o 9" / "at 9" only counts when followed by the end of the text, punctuation, a
 * preposition/conjunction or a few common words ("o 15 się spotkamy"), a Polish infinitive
 * ("o 18 zadzwonić") or another date/time token (so "podwyżka o 5 zł" is not a time);
 * "15 się" / "5 mają" are never dates (folded "sie" = August, "maja" = May), nor is
 * "o 15 sie <word>" (typed without diacritics after an hour); numbers followed by units
 * ("12.05 zł") are not dates; numeric dates without a year need a two-digit month
 * ("15.10", not "12.5"), and slash dates two-digit parts ("15/10", not "1/2" or "8/10").
 * Day-of-month ordinals ("the 15th", "15-go") are dates only when no noun follows them
 * ("the 2nd bug", "2-go stopnia" are not).
 *
 * Nothing is recognised inside URLs, e-mail addresses, wiki/Markdown links, inline code, tags
 * or canonical date tokens ("📅 2026-10-15") — see protect.ts — so stripping a phrase can
 * never corrupt them.
 */
import type { LocalDate, Locale } from '../types';
import { type FoldedText, foldWithMap, unfoldRange } from './fold';
import {
  addDaysLocal,
  addMonthsLocal,
  addYearsLocal,
  daysInMonth,
  endOfMonthLocal,
  endOfWeekLocal,
  formatHm,
  formatYmd,
  isLocalDate,
  isValidYmd,
  normalizeWeekStart,
  splitLocal,
  startOfMonthLocal,
  startOfWeekLocal,
  toLocalDate,
  toLocalDateTime,
  weekdayOf,
} from './local';
import { MONTH_LOOKUP, NUMBER_WORDS, WEEKDAY_LOOKUP, alternation, own } from './names';
import { protectedMask } from './protect';

/** A recognised date phrase. */
export interface NaturalDateResult {
  date: LocalDate;
  /** "HH:mm" when the phrase contains a time. */
  time?: string;
  /** The exact substring of the input that was recognised (for stripping). */
  matched: string;
  /** Offset of `matched` in the input. */
  index: number;
}

/** Options for {@link parseNaturalDate}. */
export interface NaturalDateOptions {
  /** Resolves ambiguous numeric dates: "10/11" is 10 Nov for 'pl' (default), Oct 11 for 'en'. */
  locale?: Locale;
  /** First day of the week for "next friday" / "next week" (default 1 = Monday). */
  weekStartsOn?: 0 | 1;
}

type Daypart = 'morning' | 'forenoon' | 'afternoon' | 'evening' | 'night';

interface TimeSpec {
  hour?: number;
  minute?: number;
  meridiem?: 'am' | 'pm';
  daypart?: Daypart;
  /** A "midnight" word: with an explicit day it means the end of that day. */
  midnight?: boolean;
}

/**
 * Why a token is ambiguous: 'alone' = only as the whole input; 'hour' = needs time-ish
 * context; 'ordinal' = a day-of-month ordinal ("the 2nd", "2-go") that is no date when it
 * modifies a following noun ("the 2nd bug", "2-go stopnia").
 */
type Weakness = 'alone' | 'hour' | 'ordinal';

interface TokenValue {
  date?: LocalDate;
  time?: TimeSpec;
  /** Exact date-time from a relative offset ("za 3 godziny"); never merged with other tokens. */
  exact?: { date: LocalDate; time: string };
  weak?: Weakness;
}

interface Token extends TokenValue {
  start: number;
  end: number;
  prio: number;
}

interface Ctx {
  /** Folded input. */
  text: string;
  folded: FoldedText;
  original: string;
  now: Date;
  today: LocalDate;
  /** Current wall-clock time "HH:mm". */
  nowHm: string;
  weekStartsOn: 0 | 1;
  locale: Locale;
  /** Index of the first folded unit that is not whitespace/punctuation (text.length if none). */
  firstContent: number;
  /** Index just after the last folded unit that is not whitespace/punctuation (0 if none). */
  contentEnd: number;
}

interface Matcher {
  re: RegExp;
  build(m: RegExpExecArray, ctx: Ctx): TokenValue | null;
}

// ---------------------------------------------------------------------------
// Regex building blocks (patterns run on folded, lower-case text)
// ---------------------------------------------------------------------------

/** Start of a word (Unicode-aware; `\b` does not understand Polish letters). */
const L = String.raw`(?<![\p{L}\p{N}])`;
/** End of a word. */
const R = String.raw`(?![\p{L}\p{N}])`;
/** Start of a number that is not part of a longer number like "1.2.3" or "12:15.10". */
const NL = String.raw`(?<![\p{L}\p{N}])(?<![\p{N}][.,/:-])`;
/** End of a number that does not continue ("15.10.5" / "15.104"). */
const NR = String.raw`(?![\p{N}])(?![.,/:-]\p{N})`;
const MERIDIEM = String.raw`a\.m\.|p\.m\.|a\.m|p\.m|am|pm`;
/** Prepositions that may precede a date and are included in the match ("do 15.10", "on Oct 15"). */
const DATE_PREP = String.raw`na|do|od|dnia|w\s+dniu|on|by|until|till`;

function re(src: string): RegExp {
  return new RegExp(src, 'gu');
}

/** Alternation of phrases where a space may be any whitespace. */
function phrases(words: Iterable<string>): string {
  return alternation(words).replace(/ /g, String.raw`\s+`);
}

/** Collapse whitespace of a matched phrase for table lookups. */
function key(s: string | undefined): string {
  return (s ?? '').replace(/\s+/g, ' ');
}

// ---------------------------------------------------------------------------
// Vocabulary
// ---------------------------------------------------------------------------

const DAY_WORDS: Record<string, number> = {
  dzis: 0, dzisiaj: 0, today: 0,
  jutro: 1, tomorrow: 1, tmrw: 1,
  pojutrze: 2, 'day after tomorrow': 2, 'the day after tomorrow': 2,
  wczoraj: -1, yesterday: -1,
  przedwczoraj: -2, 'day before yesterday': -2, 'the day before yesterday': -2,
  // genitive forms, only after "do"/"od"/"na": "do jutra"
  jutra: 1, pojutrza: 2,
};
const NEEDS_PREP = new Set(['jutra', 'pojutrza']);

type WeekdayMode = 'next' | 'this' | 'nextWeek' | 'last';

const WEEKDAY_MODS: Record<string, WeekdayMode> = {
  przyszly: 'nextWeek', przyszla: 'nextWeek', przyszle: 'nextWeek', przyszlego: 'nextWeek', przyszlej: 'nextWeek',
  nastepny: 'nextWeek', nastepna: 'nextWeek', nastepne: 'nextWeek', nastepnego: 'nextWeek', nastepnej: 'nextWeek',
  kolejny: 'nextWeek', kolejna: 'nextWeek', kolejne: 'nextWeek', next: 'nextWeek',
  ten: 'this', ta: 'this', te: 'this', tego: 'this', tej: 'this', this: 'this', coming: 'this', 'this coming': 'this',
  najblizszy: 'next', najblizsza: 'next', najblizsze: 'next', najblizszego: 'next', najblizszej: 'next',
  zeszly: 'last', zeszla: 'last', zeszle: 'last', zeszlego: 'last', zeszlej: 'last',
  ubiegly: 'last', ubiegla: 'last', ubiegle: 'last', ubieglego: 'last', ubieglej: 'last',
  poprzedni: 'last', poprzednia: 'last', poprzednie: 'last', poprzedniego: 'last', poprzedniej: 'last',
  last: 'last', previous: 'last', past: 'last',
};
const WEEKDAY_PREP = 'w|we|na|do|od|on|by|until|till|for';

type Unit = 'day' | 'week' | 'month' | 'year' | 'hour' | 'minute' | 'quarter';

const UNITS: Record<string, Unit> = {
  dzien: 'day', dni: 'day', dnia: 'day', day: 'day', days: 'day', d: 'day',
  tydzien: 'week', tygodnie: 'week', tygodni: 'week', tygodnia: 'week', tyg: 'week', week: 'week', weeks: 'week', wk: 'week', wks: 'week',
  miesiac: 'month', miesiace: 'month', miesiecy: 'month', miesiaca: 'month', mies: 'month', month: 'month', months: 'month',
  rok: 'year', lata: 'year', lat: 'year', roku: 'year', year: 'year', years: 'year', yr: 'year', yrs: 'year',
  godzina: 'hour', godzine: 'hour', godziny: 'hour', godzin: 'hour', godz: 'hour', h: 'hour', hour: 'hour', hours: 'hour', hr: 'hour', hrs: 'hour',
  minuta: 'minute', minute: 'minute', minuty: 'minute', minut: 'minute', min: 'minute', minutes: 'minute', mins: 'minute',
  kwadrans: 'quarter', kwadranse: 'quarter', kwadransow: 'quarter', kwadransa: 'quarter',
};
/**
 * Unit words that may stand without a number, in the case the preceding/following word
 * governs: accusative after "za" and before "temu" ("za tydzień", "tydzień temu"), genitive
 * after "w ciągu" ("w ciągu godziny"). "za dnia" (= in daylight) is therefore no offset.
 */
const BARE_UNITS_ACC: ReadonlySet<string> = new Set([
  'dzien', 'tydzien', 'miesiac', 'rok', 'godzine', 'minute', 'kwadrans',
]);
const BARE_UNITS_GEN: ReadonlySet<string> = new Set([
  'dnia', 'tygodnia', 'miesiaca', 'roku', 'godziny', 'minuty', 'kwadransa',
]);
/** Fractions: [unit, value in minutes or months]. */
const FRACTIONS: Record<string, Partial<Record<Unit, { minutes?: number; months?: number }>>> = {
  pol: { hour: { minutes: 30 }, year: { months: 6 }, day: { minutes: 720 } },
  'half an': { hour: { minutes: 30 } },
  'half a': { year: { months: 6 }, day: { minutes: 720 } },
  poltorej: { hour: { minutes: 90 } },
  poltora: { year: { months: 18 }, hour: { minutes: 90 } },
};

const DAYPARTS: Record<string, { spec: TimeSpec; weak?: Weakness; today?: boolean }> = {
  rano: { spec: { daypart: 'morning' } },
  rankiem: { spec: { daypart: 'morning' } },
  'z rana': { spec: { daypart: 'morning' } },
  'przed poludniem': { spec: { daypart: 'forenoon' } },
  'po poludniu': { spec: { daypart: 'afternoon' } },
  popoludniu: { spec: { daypart: 'afternoon' } },
  wieczorem: { spec: { daypart: 'evening' } },
  wieczor: { spec: { daypart: 'evening' }, weak: 'alone' },
  'w nocy': { spec: { daypart: 'night' } },
  noca: { spec: { daypart: 'night' } },
  'w poludnie': { spec: { hour: 12, minute: 0 } },
  'w samo poludnie': { spec: { hour: 12, minute: 0 } },
  'o polnocy': { spec: { hour: 0, minute: 0, midnight: true } },
  'in the morning': { spec: { daypart: 'morning' } },
  'in the afternoon': { spec: { daypart: 'afternoon' } },
  'in the evening': { spec: { daypart: 'evening' } },
  'at night': { spec: { daypart: 'night' } },
  morning: { spec: { daypart: 'morning' }, weak: 'alone' },
  afternoon: { spec: { daypart: 'afternoon' }, weak: 'alone' },
  evening: { spec: { daypart: 'evening' }, weak: 'alone' },
  night: { spec: { daypart: 'night' }, weak: 'alone' },
  noon: { spec: { hour: 12, minute: 0 } },
  'at noon': { spec: { hour: 12, minute: 0 } },
  midday: { spec: { hour: 12, minute: 0 } },
  midnight: { spec: { hour: 0, minute: 0, midnight: true } },
  'at midnight': { spec: { hour: 0, minute: 0, midnight: true } },
  tonight: { spec: { daypart: 'evening' }, today: true },
  'this morning': { spec: { daypart: 'morning' }, today: true },
  'this afternoon': { spec: { daypart: 'afternoon' }, today: true },
  'this evening': { spec: { daypart: 'evening' }, today: true },
};

const DAYPART_TIME: Record<Daypart, string> = {
  morning: '08:00',
  forenoon: '10:00',
  afternoon: '15:00',
  evening: '19:00',
  night: '22:00',
};

/** Words after a bare "o 9" / "at 9" that make it read as a time. */
const HOUR_FOLLOWERS = new Set([
  'w', 'we', 'na', 'do', 'z', 'ze', 'i', 'a', 'oraz', 'lub', 'albo', 'u', 'przy', 'pod', 'nad', 'przed', 'po',
  'dla', 'przez', 'ale', 'bo', 'zeby', 'gdy', 'jak', 'jesli', 'czy', 'wiec', 'tez', 'juz', 'trzeba', 'musze',
  'sie', 'mam', 'mamy', 'bede', 'bedzie', 'bedziemy', 'ide', 'idziemy', 'wyjazd', 'wyjscie', 'spotkanie',
  'in', 'on', 'at', 'with', 'for', 'to', 'and', 'or', 'but', 'until', 'by', 'then', 'sharp',
]);

/**
 * Words after a day-of-month ordinal ("on the 15th at 6pm", "15-go w biurze", "do 10-go
 * każdego miesiąca") that keep it a date: the Polish followers of a bare hour plus a few
 * period words. English prepositions like "in"/"on"/"to" are left out ("the 2nd in line").
 */
const ORDINAL_FOLLOWERS = new Set([
  ...[...HOUR_FOLLOWERS].filter((w) => !['in', 'on', 'with', 'for', 'to', 'sharp'].includes(w)),
  'of', 'next', 'this', 'every', 'please',
  'kazdego', 'tego', 'przyszlego', 'nastepnego', 'biezacego', 'miesiaca',
]);

/** Units/currencies after a number that mean it is a quantity, not a date (sticky: tested at an offset). */
const QUANTITY_AFTER =
  /\s*(?:zl|pln|gr|eur|euro|usd|chf|gbp|kg|dag|g|km|m|cm|mm|l|ml|szt|sztuk|osob|osoby|razy|procent|x|[%$€£°])(?![\p{L}])/uy;

/**
 * Original (lower-cased, NFC) words that fold to a month form but are common Polish words:
 * "się" -> "sie" (August), "mają" -> "maja" (May).
 */
const MONTH_COLLISIONS = new Set(['się', 'mają']);

/** Time-of-day phrases that may follow a bare hour: "7 rano", "8 wieczorem", "11 w nocy". */
const HOUR_DAYPARTS = [
  'rano', 'rankiem', 'z rana', 'przed poludniem', 'po poludniu', 'popoludniu', 'wieczorem', 'w nocy', 'noca',
  'in the morning', 'in the afternoon', 'in the evening', 'at night',
];

/** Run a sticky regex at `pos`. */
function execAt(re: RegExp, text: string, pos: number): RegExpExecArray | null {
  re.lastIndex = pos;
  return re.exec(text);
}

/** Whitespace / punctuation only (used to find where the real content of the input starts/ends). */
const NON_CONTENT = /[\s\p{P}]/u;

// ---------------------------------------------------------------------------
// Resolution helpers
// ---------------------------------------------------------------------------

function weekdayDate(day: number, mode: WeekdayMode, ctx: Ctx): LocalDate {
  const dow = weekdayOf(ctx.today);
  switch (mode) {
    case 'next':
      return addDaysLocal(ctx.today, (day - dow + 7) % 7 || 7);
    case 'this':
      return addDaysLocal(ctx.today, (day - dow + 7) % 7);
    case 'last':
      return addDaysLocal(ctx.today, -((dow - day + 7) % 7 || 7));
    case 'nextWeek': {
      const nextWeek = addDaysLocal(startOfWeekLocal(ctx.today, ctx.weekStartsOn), 7);
      return addDaysLocal(nextWeek, (day - ctx.weekStartsOn + 7) % 7);
    }
  }
}

/** A day/month without a year: this year, or the next year when already past (Feb 29 -> next leap year). */
function yearless(month: number, day: number, ctx: Ctx): LocalDate | null {
  if (month < 1 || month > 12 || day < 1 || day > daysInMonth(2000, month)) return null;
  let year = Number(ctx.today.slice(0, 4));
  for (let i = 0; i < 9; i++, year++) {
    if (!isValidYmd(year, month, day)) continue;
    const d = formatYmd(year, month, day);
    if (d >= ctx.today) return d;
  }
  return null;
}

function explicitDate(year: number, month: number, day: number): LocalDate | null {
  return isValidYmd(year, month, day) ? formatYmd(year, month, day) : null;
}

function fullYear(y: string): number {
  return y.length === 2 ? 2000 + Number(y) : Number(y);
}

/** Next date (today or later) with the given day of month. */
function nextMonthDay(day: number, ctx: Ctx): LocalDate | null {
  if (day < 1 || day > 31) return null;
  let year = Number(ctx.today.slice(0, 4));
  let month = Number(ctx.today.slice(5, 7));
  for (let i = 0; i < 13; i++) {
    if (day <= daysInMonth(year, month)) {
      const d = formatYmd(year, month, day);
      if (d >= ctx.today) return d;
    }
    month += 1;
    if (month > 12) {
      month = 1;
      year += 1;
    }
  }
  return null;
}

function parseCount(word: string | undefined): number | null {
  if (word === undefined) return null;
  if (/^\d+$/.test(word)) return Number(word);
  if (word === 'a' || word === 'an') return 1;
  return NUMBER_WORDS.get(word) ?? null;
}

/** Shift `now` by a count of units; day-level units give a date, hours/minutes an exact date-time. */
function offsetValue(unit: Unit, n: number, ctx: Ctx): TokenValue {
  switch (unit) {
    case 'day':
      return { date: addDaysLocal(ctx.today, n) };
    case 'week':
      return { date: addDaysLocal(ctx.today, 7 * n) };
    case 'month':
      return { date: addMonthsLocal(ctx.today, n) };
    case 'year':
      return { date: addYearsLocal(ctx.today, n) };
    case 'hour':
      return exactAfterMinutes(60 * n, ctx);
    case 'minute':
      return exactAfterMinutes(n, ctx);
    case 'quarter':
      return exactAfterMinutes(15 * n, ctx);
  }
}

/**
 * Value of "za N <unit>" / "N <unit> temu": `count` may be missing for the singular unit
 * words in `bare` ("za tydzień"); English ("in …") passes an empty set, so it always needs a
 * number or "a"/"an".
 */
function relativeValue(
  count: string | undefined,
  unitWord: string,
  sign: 1 | -1,
  bare: ReadonlySet<string>,
  ctx: Ctx,
): TokenValue | null {
  const unit = own(UNITS, unitWord);
  if (!unit) return null;
  let n = parseCount(count);
  if (n === null) {
    if (count !== undefined || !bare.has(unitWord)) return null;
    n = 1;
  }
  return offsetValue(unit, sign * n, ctx);
}

const NO_BARE_UNITS: ReadonlySet<string> = new Set();

/** Which bare unit words a relative-offset preposition accepts (folded, whitespace-collapsed). */
function bareUnitsAfter(prep: string): ReadonlySet<string> {
  if (prep === 'za') return BARE_UNITS_ACC;
  if (prep === 'w ciagu') return BARE_UNITS_GEN;
  return NO_BARE_UNITS; // "in" / "within"
}

function exactAfterMinutes(minutes: number, ctx: Ctx): TokenValue {
  const at = new Date(ctx.now.getTime() + minutes * 60_000);
  const sp = splitLocal(toLocalDateTime(at));
  return sp?.time ? { exact: { date: sp.date, time: sp.time } } : {};
}

/** Final "HH:mm" for a time spec, applying am/pm and time-of-day words; null if invalid. */
function resolveTime(spec: TimeSpec): string | null {
  if (spec.midnight) return '00:00';
  if (spec.hour === undefined) return spec.daypart ? DAYPART_TIME[spec.daypart] : null;
  let h = spec.hour;
  const min = spec.minute ?? 0;
  if (spec.meridiem === 'pm' && h < 12) h += 12;
  else if (spec.meridiem === 'am' && h === 12) h = 0;
  else if (!spec.meridiem && spec.daypart) {
    // Hour 0 is unambiguous (24-hour clock): "tonight at 0:00" must not become noon.
    if ((spec.daypart === 'afternoon' || spec.daypart === 'evening') && h >= 1 && h < 12) h += 12;
    // "tonight at 12" / "wieczorem o 12" is midnight (then moved to the following night).
    else if (spec.daypart === 'evening' && h === 12) h = 0;
    else if (spec.daypart === 'night') h = h === 12 ? 0 : h >= 6 && h < 12 ? h + 12 : h;
  }
  return h >= 0 && h <= 23 && min >= 0 && min <= 59 ? formatHm(h, min) : null;
}

/** Original-text slice for a folded range. */
function originalSlice(ctx: Ctx, start: number, end: number): string {
  const r = unfoldRange(ctx.folded, start, end);
  return ctx.original.slice(r.start, r.end);
}

function followedByQuantity(ctx: Ctx, end: number): boolean {
  return execAt(QUANTITY_AFTER, ctx.text, end) !== null;
}

/** "$ 15.10": a currency sign (then optional whitespace) right before `start`. Scans back only over whitespace. */
function precededByCurrency(ctx: Ctx, start: number): boolean {
  let i = start - 1;
  while (i >= 0 && /\s/u.test(ctx.text.charAt(i))) i--;
  return i >= 0 && '$€£'.includes(ctx.text.charAt(i));
}

/** "o 15 sie …": the folded text before `start` ends with the hour preposition "o"/"at". */
function precededByHourPreposition(ctx: Ctx, start: number): boolean {
  return /(?:^|[^\p{L}\p{N}])(?:o|at)\s+$/u.test(ctx.text.slice(Math.max(0, start - 12), start));
}

/**
 * Whether the month word of a "day + month" match is really a different Polish word that
 * only looks like a month after folding: "się" ("o 15 się spotkamy"), "mają" ("5 mają
 * wolne"), or "sie" typed without diacritics after an hour and before another word
 * ("o 15 sie spotkamy"). `m[1]` = day digits, `m[2]` = folded month word, `m[3]` = year.
 */
function monthWordCollides(m: RegExpExecArray, ctx: Ctx): boolean {
  const day = m[1] ?? '';
  const monthWord = m[2] ?? '';
  const rel = m[0].indexOf(monthWord, m[0].indexOf(day) + day.length);
  if (rel < 0) return false;
  const start = m.index + rel;
  const original = originalSlice(ctx, start, start + monthWord.length).toLowerCase().normalize('NFC');
  if (MONTH_COLLISIONS.has(original)) return true;
  if (monthWord !== 'sie' || m[3] !== undefined) return false;
  return precededByHourPreposition(ctx, m.index) && execAt(/\s+\p{L}/uy, ctx.text, m.index + m[0].length) !== null;
}

/** Numeric dates must not be amounts ("12.05 zł", "$15.10"). */
function numericDateOk(m: RegExpExecArray, ctx: Ctx): boolean {
  return !followedByQuantity(ctx, m.index + m[0].length) && !precededByCurrency(ctx, m.index);
}

// ---------------------------------------------------------------------------
// Matchers
// ---------------------------------------------------------------------------

function buildMatchers(): Matcher[] {
  const singularWeekdays = [...WEEKDAY_LOOKUP].filter(([, w]) => !w.plural).map(([k]) => k);
  const months = [...MONTH_LOOKUP.keys()];
  const enMonths = [...MONTH_LOOKUP].filter(([, w]) => w.en).map(([k]) => k);
  const numWords = alternation([...NUMBER_WORDS.keys(), 'an', 'a']);
  const unitWords = alternation(Object.keys(UNITS));
  const fractionWords = phrases(Object.keys(FRACTIONS));

  return [
    // dziś / jutro / pojutrze / wczoraj / today / tomorrow (+ "na", "do", "on", …)
    {
      re: re(`${L}(?:(na|do|od|on|by|for|until|till)\\s+)?(${phrases(Object.keys(DAY_WORDS))})${R}`),
      build(m, ctx) {
        const word = key(m[2]);
        if (NEEDS_PREP.has(word) && !m[1]) return null;
        const n = own(DAY_WORDS, word);
        return n === undefined ? null : { date: addDaysLocal(ctx.today, n) };
      },
    },
    // Weekdays: "w piątek", "we wtorek", "w przyszłą środę", "next friday", "pt"
    {
      re: re(
        `${L}(?:(${WEEKDAY_PREP})\\s+)?(?:(${phrases(Object.keys(WEEKDAY_MODS))})\\s+)?(${alternation(singularWeekdays)})${R}`,
      ),
      build(m, ctx) {
        const word = WEEKDAY_LOOKUP.get(m[3] ?? '');
        if (!word) return null;
        const mode = m[2] ? (own(WEEKDAY_MODS, key(m[2])) ?? 'next') : 'next';
        const value: TokenValue = { date: weekdayDate(word.day, mode, ctx) };
        if (word.abbr && !m[1] && !m[2]) value.weak = 'alone';
        return value;
      },
    },
    // "w przyszłym tygodniu", "w następnym miesiącu", "w przyszłym roku"
    {
      re: re(`${L}(?:(?:w|we)\\s+)(przyszlym|nastepnym|kolejnym)\\s+(tygodniu|miesiacu|roku)${R}`),
      build: (m, ctx) => ({ date: periodStart(key(m[2]), ctx) }),
    },
    // "przyszły tydzień", "do przyszłego tygodnia", "next week/month/year"
    {
      re: re(
        `${L}(?:(?:na|do|od|by|until)\\s+)?(?:przyszly|nastepny|przyszlego|nastepnego|next)\\s+(tydzien|tygodnia|miesiac|miesiaca|rok|roku|week|month|year)${R}`,
      ),
      build: (m, ctx) => ({ date: periodStart(key(m[1]), ctx) }),
    },
    // "koniec miesiąca", "do końca tygodnia", "end of the month", "end of next week"
    {
      re: re(
        `${L}(?:(?:na|do|w|przed|by|at|until|till|before)\\s+)?(?:the\\s+)?(?:koniec|konca|koncu|koncem|end\\s+of)\\s+(?:(tego|biezacego|przyszlego|nastepnego|the|this|next)\\s+)?(tygodnia|miesiaca|roku|week|month|year)${R}`,
      ),
      build(m, ctx) {
        const next = /^(?:przyszlego|nastepnego|next)$/.test(m[1] ?? '');
        return { date: periodEnd(key(m[2]), next, ctx) };
      },
    },
    // Weekend: "w weekend", "na weekend", "this weekend", "w przyszły weekend"
    {
      re: re(
        `${L}(?:(w|we|na|on|over|at)\\s+)?(?:(the|ten|this|przyszly|nastepny|najblizszy|next)\\s+)?(weekend|weekendzie|weekendu)${R}`,
      ),
      build(m, ctx) {
        const dow = weekdayOf(ctx.today);
        const next = /^(?:przyszly|nastepny|next)$/.test(m[2] ?? '');
        const date = next
          ? weekdayDate(6, 'nextWeek', ctx)
          : dow === 6 || dow === 0
            ? ctx.today
            : weekdayDate(6, 'next', ctx);
        const value: TokenValue = { date };
        if (!m[1] && !m[2]) value.weak = 'alone';
        return value;
      },
    },
    // Relative: "za 2 dni", "za tydzień", "za 3 godziny", "w ciągu godziny", "in 2 weeks", "in an hour"
    {
      re: re(`${L}(za|w\\s+ciagu|in|within)\\s+(?:(\\d{1,4})\\s*|(${numWords})\\s+)?(${unitWords})${R}`),
      build: (m, ctx) => relativeValue(m[2] ?? m[3], m[4] ?? '', 1, bareUnitsAfter(key(m[1])), ctx),
    },
    // Fractions: "za pół godziny", "za półtorej godziny", "in half an hour", "za pół roku"
    {
      re: re(`${L}(?:za|w\\s+ciagu|in|within)\\s+(${fractionWords})\\s+(godziny|godzine|hour|roku|year|dnia|day)${R}`),
      build(m, ctx) {
        const frac = own(FRACTIONS, key(m[1]));
        const unit = own(UNITS, m[2] ?? '');
        const v = unit ? frac?.[unit] : undefined;
        if (!v) return null;
        if (v.minutes !== undefined) return exactAfterMinutes(v.minutes, ctx);
        return { date: addMonthsLocal(ctx.today, v.months ?? 0) };
      },
    },
    // Ago: "2 dni temu", "tydzień temu", "3 weeks ago"
    {
      re: re(`${L}(?:(\\d{1,4})\\s*|(${numWords})\\s+)?(${unitWords})\\s+(?:temu|ago)${R}`),
      build: (m, ctx) => relativeValue(m[1] ?? m[2], m[3] ?? '', -1, BARE_UNITS_ACC, ctx),
    },
    // ISO: "2026-10-15", "2026/10/15", "2026-10-15T18:30"
    {
      re: re(`${NL}(?:(?:${DATE_PREP})\\s+)?(\\d{4})([-/.])(\\d{1,2})\\2(\\d{1,2})(?:t(\\d{1,2}):(\\d{2}))?${NR}`),
      build(m) {
        const date = explicitDate(Number(m[1]), Number(m[3]), Number(m[4]));
        if (!date) return null;
        if (m[5] === undefined) return { date };
        return { date, time: { hour: Number(m[5]), minute: Number(m[6]) } };
      },
    },
    // Day.month[.year]: "15.10", "15.10.2026", "15/10", "15.10.26r."
    {
      re: re(
        `${NL}(?:(?:${DATE_PREP})\\s+)?(\\d{1,2})([./])(\\d{1,2})(?:\\2(\\d{4}|\\d{2})(?:\\s?r\\.?(?![\\p{L}]))?)?${NR}`,
      ),
      build(m, ctx) {
        if (!numericDateOk(m, ctx)) return null;
        const a = m[1] ?? '';
        const sep = m[2];
        const b = m[3] ?? '';
        const year = m[4];
        if (year === undefined) {
          // Without a year require two-digit months ("15.10", not decimals like "12.5"),
          // and for slashes two-digit parts (not fractions like "1/2" or scores "8/10").
          if (sep === '.' && b.length !== 2) return null;
          if (sep === '/' && (a.length !== 2 || b.length !== 2)) return null;
        }
        let day = Number(a);
        let month = Number(b);
        // Slashes: "10/15" can only be month/day; ambiguous ones follow the locale.
        if (sep === '/' && ((ctx.locale === 'en' && day <= 12 && month <= 12) || (month > 12 && day <= 12))) {
          [day, month] = [month, day];
        }
        const date = year === undefined ? yearless(month, day, ctx) : explicitDate(fullYear(year), month, day);
        return date ? { date } : null;
      },
    },
    // Day-month-year with dashes: "15-10-2026"
    {
      re: re(`${NL}(?:(?:${DATE_PREP})\\s+)?(\\d{1,2})-(\\d{1,2})-(\\d{4})${NR}`),
      build(m, ctx) {
        if (!numericDateOk(m, ctx)) return null;
        const date = explicitDate(Number(m[3]), Number(m[2]), Number(m[1]));
        return date ? { date } : null;
      },
    },
    // Day + month name: "15 października", "15 paź 2026", "1st of October", "15th Oct"
    {
      re: re(
        `${L}(?:(?:${DATE_PREP})\\s+)?(\\d{1,2})(?:\\.|-go|-ego|-tego|st|nd|rd|th)?(?:\\s+of)?\\s*(${alternation(months)})(?![\\p{L}])(?:\\.?,?\\s+(\\d{4})(?![\\p{N}])(?:\\s?r\\.?(?![\\p{L}]))?)?`,
      ),
      build(m, ctx) {
        const word = MONTH_LOOKUP.get(m[2] ?? '');
        if (!word) return null;
        // "o 15 się spotkamy", "5 mają wolne": folded words that only look like months.
        if (monthWordCollides(m, ctx)) return null;
        const day = Number(m[1]);
        const date = m[3] ? explicitDate(Number(m[3]), word.month, day) : yearless(word.month, day, ctx);
        return date ? { date } : null;
      },
    },
    // English month first: "Oct 15", "October 15th, 2026", "on May 3"
    {
      re: re(
        `${L}(?:(?:on|by|until|till)\\s+)?(${alternation(enMonths)})\\.?\\s+(\\d{1,2})(?:st|nd|rd|th)?(?![\\p{L}\\p{N}])(?:,?\\s+(\\d{4})(?![\\p{N}]))?`,
      ),
      build(m, ctx) {
        const word = MONTH_LOOKUP.get(m[1] ?? '');
        if (!word) return null;
        const day = Number(m[2]);
        const date = m[3] ? explicitDate(Number(m[3]), word.month, day) : yearless(word.month, day, ctx);
        return date ? { date } : null;
      },
    },
    // Day of month only: "the 15th", "on the 3rd", "by the 15th", "15-go", "do 20-tego"
    {
      re: re(
        `${L}(?:(on|by|until|till|na|do|od|dnia)\\s+)?(?:the\\s+(\\d{1,2})(?:st|nd|rd|th)|(\\d{1,2})-(?:go|ego|tego))${R}`,
      ),
      build(m, ctx) {
        const date = nextMonthDay(Number(m[2] ?? m[3]), ctx);
        if (!date) return null;
        // "the 2nd bug", "on the 3rd floor", "na 2-go piętrze" are ordinals, not dates.
        return { date, weak: 'ordinal' };
      },
    },
    // "o 9", "o 9:30", "o 18.30", "o godz. 9", "at 9", "at 6pm", "o 18h", "o 9h30"
    {
      re: re(
        `${L}(o|at)\\s+(?:(godz\\.?|godzinie|godzina)\\s*)?(\\d{1,2})(?:[:.](\\d{2}))?(?![\\p{N}])(?![:.]\\p{N})(?:\\s*(${MERIDIEM})(?![\\p{L}]))?(?:\\s*(h(?:\\d{2})?|godz\\.?)(?![\\p{L}\\p{N}]))?(?![\\p{L}])`,
      ),
      build(m) {
        // "9h30": minutes after the "h" (never together with "9:30h15").
        const hMinutes = /^h(\d{2})$/.exec(m[6] ?? '')?.[1];
        if (hMinutes !== undefined && m[4] !== undefined) return null;
        const spec = timeSpec(m[3], m[4] ?? hMinutes, m[5]);
        if (!spec) return null;
        const value: TokenValue = { time: spec };
        if (m[2] === undefined && m[4] === undefined && m[5] === undefined && m[6] === undefined) value.weak = 'hour';
        return value;
      },
    },
    // Hour + time of day: "7 rano", "o 7 rano", "8 wieczorem", "6:30 rano", "7 in the morning"
    {
      re: re(
        `${NL}(?:(o|at)\\s+(?:(?:godz\\.?|godzinie|godzina)\\s*)?)?(\\d{1,2})(?:([:.])(\\d{2}))?(?![\\p{N}])\\s+(${phrases(HOUR_DAYPARTS)})${R}`,
      ),
      build(m) {
        // "10.10 rano" without "o" is a date (10 Oct) followed by "rano", not 10:10.
        if (m[3] === '.' && m[1] === undefined) return null;
        const spec = timeSpec(m[2], m[4], undefined);
        const daypart = own(DAYPARTS, key(m[5]))?.spec.daypart;
        if (!spec || !daypart || spec.hour === undefined) return null;
        if ((daypart === 'morning' || daypart === 'forenoon') && spec.hour > 12) return null;
        return { time: { ...spec, daypart } };
      },
    },
    // "godz. 18", "godzina 9:30"
    {
      re: re(`${L}(?:godz\\.?|godzina|godzinie)\\s*(\\d{1,2})(?:[:.](\\d{2}))?(?![\\p{N}])(?![:.]\\p{N})`),
      build(m) {
        const spec = timeSpec(m[1], m[2], undefined);
        return spec ? { time: spec } : null;
      },
    },
    // "9:30", "18:30", "6:30pm"
    {
      re: re(`${NL}(\\d{1,2}):(\\d{2})(?![\\p{N}])(?!:\\p{N})(?:\\s*(${MERIDIEM})(?![\\p{L}]))?`),
      build(m) {
        const spec = timeSpec(m[1], m[2], m[3]);
        return spec ? { time: spec } : null;
      },
    },
    // "6pm", "6 pm", "6.30 p.m."
    {
      re: re(`${NL}(\\d{1,2})(?:[.](\\d{2}))?\\s*(${MERIDIEM})(?![\\p{L}])`),
      build(m) {
        const spec = timeSpec(m[1], m[2], m[3]);
        return spec ? { time: spec } : null;
      },
    },
    // "6 o'clock"
    {
      re: re(`${NL}(\\d{1,2})\\s*o['’]?clock${R}`),
      build(m) {
        const spec = timeSpec(m[1], undefined, undefined);
        return spec ? { time: spec } : null;
      },
    },
    // Times of day: "rano", "wieczorem", "w południe", "po południu", "in the evening", "noon", "tonight"
    {
      re: re(`${L}(${phrases(Object.keys(DAYPARTS))})${R}`),
      build(m, ctx) {
        const dp = own(DAYPARTS, key(m[1]));
        if (!dp) return null;
        const value: TokenValue = { time: { ...dp.spec } };
        if (dp.today) value.date = ctx.today;
        if (dp.weak) value.weak = dp.weak;
        return value;
      },
    },
  ];
}

function timeSpec(h: string | undefined, min: string | undefined, mer: string | undefined): TimeSpec | null {
  const hour = Number(h);
  const minute = min === undefined ? 0 : Number(min);
  if (!Number.isInteger(hour) || minute > 59) return null;
  const spec: TimeSpec = { hour, minute };
  if (mer) {
    if (hour < 1 || hour > 12) return null;
    spec.meridiem = mer.startsWith('a') ? 'am' : 'pm';
  } else if (hour > 23) {
    return null;
  }
  return spec;
}

function periodStart(word: string, ctx: Ctx): LocalDate {
  if (/^(?:tydzien|tygodnia|tygodniu|week)$/.test(word)) {
    return addDaysLocal(startOfWeekLocal(ctx.today, ctx.weekStartsOn), 7);
  }
  if (/^(?:miesiac|miesiaca|miesiacu|month)$/.test(word)) return addMonthsLocal(startOfMonthLocal(ctx.today), 1);
  return `${String(Number(ctx.today.slice(0, 4)) + 1).padStart(4, '0')}-01-01`;
}

function periodEnd(word: string, next: boolean, ctx: Ctx): LocalDate {
  if (/^(?:tygodnia|week)$/.test(word)) {
    return addDaysLocal(endOfWeekLocal(ctx.today, ctx.weekStartsOn), next ? 7 : 0);
  }
  if (/^(?:miesiaca|month)$/.test(word)) {
    return endOfMonthLocal(next ? addMonthsLocal(startOfMonthLocal(ctx.today), 1) : ctx.today);
  }
  return `${String(Number(ctx.today.slice(0, 4)) + (next ? 1 : 0)).padStart(4, '0')}-12-31`;
}

/** Something usable and within the supported year range (e.g. not "za 9999 lat"). */
function hasValidValue(v: TokenValue): boolean {
  if (v.date !== undefined && !isLocalDate(v.date)) return false;
  if (v.exact && !isLocalDate(v.exact.date)) return false;
  return !!(v.date || v.time || v.exact);
}

let matchersCache: Matcher[] | null = null;

/** Matchers are compiled lazily on first use (keeps module load cheap and side-effect free). */
function matchers(): Matcher[] {
  matchersCache ??= buildMatchers();
  return matchersCache;
}

// ---------------------------------------------------------------------------
// Tokenising, grouping
// ---------------------------------------------------------------------------

/** Build a token value; out-of-range arithmetic (dates beyond 9999) just drops the candidate. */
function buildSafely(matcher: Matcher, m: RegExpExecArray, ctx: Ctx): TokenValue | null {
  try {
    return matcher.build(m, ctx);
  } catch (e) {
    if (e instanceof RangeError) return null;
    throw e;
  }
}

function collectTokens(ctx: Ctx): Token[] {
  const all: Token[] = [];
  matchers().forEach((matcher, prio) => {
    for (const m of ctx.text.matchAll(matcher.re)) {
      if (m[0].length === 0) continue;
      const value = buildSafely(matcher, m, ctx);
      if (!value || !hasValidValue(value)) continue;
      all.push({ ...value, start: m.index, end: m.index + m[0].length, prio });
    }
  });
  // Longest first, then leftmost, then matcher order; keep non-overlapping. An occupancy map
  // keeps this linear in the total token length (comparing against every picked token was
  // quadratic for long inputs with many phrases). It starts with the protected spans (URLs,
  // links, code, tags, date tokens), so no token may touch those.
  all.sort((a, b) => b.end - b.start - (a.end - a.start) || a.start - b.start || a.prio - b.prio);
  const taken = protectedMask(ctx.text);
  const picked: Token[] = [];
  for (const t of all) {
    if (taken.subarray(t.start, t.end).includes(1)) continue;
    taken.fill(1, t.start, t.end);
    picked.push(t);
  }
  return picked.sort((a, b) => a.start - b.start);
}

interface Group {
  start: number;
  end: number;
  tokens: Token[];
  date?: LocalDate;
  time: TimeSpec;
  hasTime: boolean;
  exact?: { date: LocalDate; time: string };
}

function newGroup(t: Token): Group {
  const g: Group = { start: t.start, end: t.end, tokens: [t], time: { ...t.time }, hasTime: !!t.time };
  if (t.date) g.date = t.date;
  if (t.exact) g.exact = t.exact;
  return g;
}

/**
 * What may separate merged tokens: whitespace with at most one ",", ":" or dash ("jutro: 18:00",
 * "15.10 – 18:30"). Written without two adjacent `\s*` (that form backtracks quadratically).
 */
const MERGE_GAP = /^\s*(?:[,:–—-]\s*)?$/u;

function canMerge(g: Group, t: Token, ctx: Ctx): boolean {
  if (g.exact || t.exact) return false;
  if (!MERGE_GAP.test(ctx.text.slice(g.end, t.start))) return false;
  if (t.date && g.date && t.date !== g.date) return false;
  const tt = t.time;
  if (tt) {
    if (tt.hour !== undefined && g.time.hour !== undefined) return false;
    if (tt.daypart && g.time.daypart) return false;
  }
  // Must contribute something new (two plain dates for the same day are merged too).
  return !!(tt || t.date);
}

function merge(g: Group, t: Token): void {
  g.end = t.end;
  g.tokens.push(t);
  g.date ??= t.date;
  if (t.time) {
    g.hasTime = true;
    g.time.hour ??= t.time.hour;
    g.time.minute ??= t.time.minute;
    g.time.meridiem ??= t.time.meridiem;
    g.time.daypart ??= t.time.daypart;
    g.time.midnight ??= t.time.midnight;
  }
}

function groupTokens(tokens: Token[], ctx: Ctx): Group[] {
  const groups: Group[] = [];
  for (const t of tokens) {
    const last = groups[groups.length - 1];
    if (last && canMerge(last, t, ctx)) merge(last, t);
    else groups.push(newGroup(t));
  }
  return groups;
}

/** Sticky: end of text or punctuation right after a bare hour ("o 9.", "o 9)"). */
const HOUR_PUNCT_AFTER = /\s*(?:$|[,.;:!?)\]}"'”’»–—-])/uy;
/** Sticky: the next word after a token. */
const WORD_AFTER = /\s+(\p{L}+)/uy;

/** Whether a lone weak token is acceptable in its context. Constant work per token (no prefix/suffix scans). */
function weakTokenOk(t: Token, ctx: Ctx): boolean {
  // The whole input apart from punctuation/whitespace ("pt", "  sob. ").
  if (ctx.firstContent >= t.start && ctx.contentEnd <= t.end) return true;
  if (t.weak === 'alone') return false;
  if (execAt(HOUR_PUNCT_AFTER, ctx.text, t.end)) return true;
  const w = execAt(WORD_AFTER, ctx.text, t.end);
  if (!w) return false;
  const word = w[1] ?? '';
  if ((t.weak === 'ordinal' ? ORDINAL_FOLLOWERS : HOUR_FOLLOWERS).has(word)) return true;
  // A Polish infinitive verb ("o 18 zadzwonić") — checked on the original text (ć vs c).
  const wordStart = t.end + w[0].length - word.length;
  return originalSlice(ctx, wordStart, wordStart + word.length).toLowerCase().endsWith('ć');
}

function groupValid(g: Group, ctx: Ctx): boolean {
  if (g.tokens.length > 1) return true;
  const t = g.tokens[0];
  return !!t && (!t.weak || weakTokenOk(t, ctx));
}

function finalize(g: Group, ctx: Ctx): { date: LocalDate; time?: string } | null {
  if (g.exact) return { ...g.exact };
  let time: string | undefined;
  if (g.hasTime) {
    const t = resolveTime(g.time);
    if (t === null) return null;
    time = t;
  }
  let date = g.date;
  if (!date) {
    if (!time) return null;
    date = time > ctx.nowHm ? ctx.today : addDaysLocal(ctx.today, 1);
  } else if (time && nightAfter(g.time, time)) {
    // "dziś o północy", "jutro o 2 w nocy": the night that follows the named day.
    date = addDaysLocal(date, 1);
  }
  return time ? { date, time } : { date };
}

/**
 * Midnight words and small hours (00:00-05:59) said with "w nocy"/"at night"/"tonight"/
 * "wieczorem" belong to the night after a named day.
 */
function nightAfter(spec: TimeSpec, time: string): boolean {
  return !!spec.midnight || ((spec.daypart === 'night' || spec.daypart === 'evening') && time < '06:00');
}

/** [first content index, end of last content] of `text`, ignoring whitespace and punctuation. */
function contentBounds(text: string): [number, number] {
  let first = 0;
  while (first < text.length && NON_CONTENT.test(text.charAt(first))) first++;
  let end = text.length;
  while (end > first && NON_CONTENT.test(text.charAt(end - 1))) end--;
  return [first, end];
}

function makeContext(input: string, now: Date, opts: NaturalDateOptions | undefined): Ctx {
  const folded = foldWithMap(input);
  const [firstContent, contentEnd] = contentBounds(folded.text);
  return {
    text: folded.text,
    folded,
    original: input,
    now,
    today: toLocalDate(now),
    nowHm: formatHm(now.getHours(), now.getMinutes()),
    weekStartsOn: normalizeWeekStart(opts?.weekStartsOn ?? 1),
    locale: opts?.locale === 'en' ? 'en' : 'pl',
    firstContent,
    contentEnd,
  };
}

/** {@link finalize}, or null when the resolved date would be beyond 9999-12-31. */
function finalizeSafely(g: Group, ctx: Ctx): { date: LocalDate; time?: string } | null {
  try {
    return finalize(g, ctx);
  } catch (e) {
    if (e instanceof RangeError) return null;
    throw e;
  }
}

/**
 * Find all date/time phrases in `input` (left to right), e.g. for highlighting while typing.
 * See {@link parseNaturalDate} for the rules.
 */
export function findNaturalDates(input: string, now: Date, opts?: NaturalDateOptions): NaturalDateResult[] {
  if (!input || Number.isNaN(now.getTime())) return [];
  const ctx = makeContext(input, now, opts);
  const out: NaturalDateResult[] = [];
  for (const g of groupTokens(collectTokens(ctx), ctx)) {
    if (!groupValid(g, ctx)) continue;
    const value = finalizeSafely(g, ctx);
    if (!value) continue;
    const r = unfoldRange(ctx.folded, g.start, g.end);
    out.push({ ...value, matched: input.slice(r.start, r.end), index: r.start });
  }
  return out;
}

/**
 * Find the first date/time phrase anywhere in `input` and resolve it relative to `now`.
 * Returns the date, the optional time ("HH:mm"), and the matched substring with its offset so
 * the caller can strip it (see {@link stripNaturalDate}). Polish and English are understood
 * regardless of `opts.locale`; see the module documentation for the exact semantics.
 *
 * @example parseNaturalDate('Zadzwoń do mamy jutro o 18', new Date(2026, 9, 14, 10))
 *   // { date: '2026-10-15', time: '18:00', matched: 'jutro o 18', index: 16 }
 */
export function parseNaturalDate(input: string, now: Date, opts?: NaturalDateOptions): NaturalDateResult | null {
  return findNaturalDates(input, now, opts)[0] ?? null;
}

/** One separator character around a stripped phrase. */
const STRIP_SEPARATOR = /[\s,;:–—-]/u;

/** Opening bracket/quote -> its closing counterpart. */
const BRACKET_PAIRS: Readonly<Record<string, string>> = {
  '(': ')', '[': ']', '{': '}', '„': '”', '“': '”', '"': '"', "'": "'", '«': '»', '‘': '’',
};

/** Index before the separators that end at `i` (loops: regexes like /[\s,]+$/ backtrack). */
function skipSeparatorsBack(s: string, i: number): number {
  while (i > 0 && STRIP_SEPARATOR.test(s.charAt(i - 1))) i--;
  return i;
}

/** Index after the separators that start at `i`. */
function skipSeparatorsForward(s: string, i: number): number {
  while (i < s.length && STRIP_SEPARATOR.test(s.charAt(i))) i++;
  return i;
}

/**
 * Remove a recognised phrase from the input and tidy whitespace / dangling separators:
 * ("Zadzwoń do mamy jutro o 18", result) -> "Zadzwoń do mamy". Brackets or quotes that only
 * wrapped the phrase go too: "Kupić mleko (jutro)" -> "Kupić mleko".
 */
export function stripNaturalDate(input: string, result: Pick<NaturalDateResult, 'matched' | 'index'>): string {
  let cut = skipSeparatorsBack(input, result.index);
  let resume = skipSeparatorsForward(input, result.index + result.matched.length);
  if (cut > 0 && resume < input.length && own(BRACKET_PAIRS, input.charAt(cut - 1)) === input.charAt(resume)) {
    cut = skipSeparatorsBack(input, cut - 1);
    resume = skipSeparatorsForward(input, resume + 1);
  }
  const before = input.slice(0, cut);
  const after = input.slice(resume);
  if (!before) return after.replace(/^[.!?…]+\s*/u, '').trim();
  if (!after) return before.trim();
  // No space before closing punctuation or after an opening bracket ("Kupić (2 l)").
  const glue = /^[.!?…)\]}”»’]/u.test(after) || /[([{„“«‘]$/u.test(before) ? '' : ' ';
  return `${before}${glue}${after}`.replace(/\s{2,}/g, ' ').trim();
}
