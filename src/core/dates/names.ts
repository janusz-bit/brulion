/**
 * Weekday and month name tables (Polish + English) for parsing and formatting.
 *
 * Weekday indexes follow `Date#getDay`: 0 = Sunday … 6 = Saturday. Month indexes are 1-12
 * in lookups and 0-11 in the display arrays.
 *
 * Lookup keys are *folded* (lower-case, no diacritics — see fold.ts) so "środę", "Srode" and
 * "ŚRODĘ" all hit the same entry.
 */
import type { Locale } from '../types';

// ---------------------------------------------------------------------------
// Display names
// ---------------------------------------------------------------------------

/** Polish weekday names, nominative ("poniedziałek"). */
export const PL_WEEKDAYS = ['niedziela', 'poniedziałek', 'wtorek', 'środa', 'czwartek', 'piątek', 'sobota'] as const;
/** Polish weekday names, accusative as used after "w" ("w środę"). */
export const PL_WEEKDAYS_ACC = ['niedzielę', 'poniedziałek', 'wtorek', 'środę', 'czwartek', 'piątek', 'sobotę'] as const;
/** Polish short weekday names used in compact dates ("pt 17 paź"). */
export const PL_WEEKDAYS_SHORT = ['nd', 'pn', 'wt', 'śr', 'czw', 'pt', 'sob'] as const;
/** English weekday names. */
export const EN_WEEKDAYS = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'] as const;
/** English short weekday names. */
export const EN_WEEKDAYS_SHORT = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'] as const;

/** Polish month names, nominative ("październik"). */
export const PL_MONTHS = [
  'styczeń', 'luty', 'marzec', 'kwiecień', 'maj', 'czerwiec',
  'lipiec', 'sierpień', 'wrzesień', 'październik', 'listopad', 'grudzień',
] as const;
/** Polish month names, genitive as used in dates ("15 października"). */
export const PL_MONTHS_GEN = [
  'stycznia', 'lutego', 'marca', 'kwietnia', 'maja', 'czerwca',
  'lipca', 'sierpnia', 'września', 'października', 'listopada', 'grudnia',
] as const;
/** Polish month abbreviations ("paź"). */
export const PL_MONTHS_SHORT = ['sty', 'lut', 'mar', 'kwi', 'maj', 'cze', 'lip', 'sie', 'wrz', 'paź', 'lis', 'gru'] as const;
/** English month names. */
export const EN_MONTHS = [
  'January', 'February', 'March', 'April', 'May', 'June',
  'July', 'August', 'September', 'October', 'November', 'December',
] as const;
/** English month abbreviations. */
export const EN_MONTHS_SHORT = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'] as const;

/** Localised full weekday name (0 = Sunday). Polish names are lower-case, English capitalised. */
export function weekdayName(day: number, locale: Locale): string {
  const i = ((day % 7) + 7) % 7;
  return (locale === 'pl' ? PL_WEEKDAYS[i] : EN_WEEKDAYS[i]) ?? '';
}

/** Localised short weekday name (0 = Sunday): "pt" / "Fri". */
export function weekdayShortName(day: number, locale: Locale): string {
  const i = ((day % 7) + 7) % 7;
  return (locale === 'pl' ? PL_WEEKDAYS_SHORT[i] : EN_WEEKDAYS_SHORT[i]) ?? '';
}

/** Localised month abbreviation (month 1-12): "paź" / "Oct". */
export function monthShortName(month: number, locale: Locale): string {
  const i = month - 1;
  return (locale === 'pl' ? PL_MONTHS_SHORT[i] : EN_MONTHS_SHORT[i]) ?? '';
}

/** Localised month name (month 1-12): "października" (genitive, as used after a day) / "October". */
export function monthName(month: number, locale: Locale, form: 'nominative' | 'genitive' = 'genitive'): string {
  const i = month - 1;
  if (locale === 'en') return EN_MONTHS[i] ?? '';
  return (form === 'genitive' ? PL_MONTHS_GEN[i] : PL_MONTHS[i]) ?? '';
}

// ---------------------------------------------------------------------------
// Parsing lookups (folded keys)
// ---------------------------------------------------------------------------

/** A weekday word recognised by the parsers. */
export interface WeekdayWord {
  day: number;
  /** Abbreviations ("pt", "fri") are ambiguous in prose and only accepted in a clear context. */
  abbr: boolean;
  /** Plural-only forms ("poniedziałki", "mondays") — recurrence only, not single dates. */
  plural: boolean;
}

/** A month word recognised by the parsers. */
export interface MonthWord {
  /** 1-12. */
  month: number;
  /** English words may precede the day ("Oct 15"); Polish ones only follow it ("15 paź"). */
  en: boolean;
  abbr: boolean;
}

/** Folded weekday forms: [day, singular forms (all cases), plural-only forms, abbreviations]. */
const WEEKDAY_FORMS: readonly [number, readonly string[], readonly string[], readonly string[]][] = [
  [0, ['niedziela', 'niedziele', 'niedzieli', 'sunday'], ['niedziel', 'sundays'], ['ndz', 'nd', 'niedz', 'sun']],
  [1, ['poniedzialek', 'poniedzialku', 'monday'], ['poniedzialki', 'poniedzialkow', 'mondays'], ['pon', 'pn', 'mon']],
  [2, ['wtorek', 'wtorku', 'tuesday'], ['wtorki', 'wtorkow', 'tuesdays'], ['wt', 'tue', 'tues']],
  [3, ['sroda', 'srode', 'srody', 'wednesday'], ['srod', 'wednesdays'], ['sr', 'wed']],
  [4, ['czwartek', 'czwartku', 'thursday'], ['czwartki', 'czwartkow', 'thursdays'], ['czw', 'thu', 'thur', 'thurs']],
  [5, ['piatek', 'piatku', 'friday'], ['piatki', 'piatkow', 'fridays'], ['pt', 'fri']],
  [6, ['sobota', 'sobote', 'soboty', 'saturday'], ['sobot', 'saturdays'], ['sob', 'sat']],
];

/** Folded month forms: [month, Polish forms, English forms]. Abbreviations are flagged separately. */
const MONTH_FORMS: readonly [number, readonly string[], readonly string[]][] = [
  [1, ['styczen', 'stycznia', 'styczniu', 'sty'], ['january', 'jan']],
  [2, ['luty', 'lutego', 'lutym', 'lut'], ['february', 'feb']],
  [3, ['marzec', 'marca', 'marcu'], ['march', 'mar']],
  [4, ['kwiecien', 'kwietnia', 'kwietniu', 'kwi'], ['april', 'apr']],
  [5, ['maj', 'maja', 'maju'], ['may']],
  [6, ['czerwiec', 'czerwca', 'czerwcu', 'cze'], ['june', 'jun']],
  [7, ['lipiec', 'lipca', 'lipcu', 'lip'], ['july', 'jul']],
  [8, ['sierpien', 'sierpnia', 'sierpniu', 'sie'], ['august', 'aug']],
  [9, ['wrzesien', 'wrzesnia', 'wrzesniu', 'wrz'], ['september', 'sept', 'sep']],
  [10, ['pazdziernik', 'pazdziernika', 'pazdzierniku', 'paz'], ['october', 'oct']],
  [11, ['listopad', 'listopada', 'listopadzie', 'lis'], ['november', 'nov']],
  [12, ['grudzien', 'grudnia', 'grudniu', 'gru'], ['december', 'dec']],
];

const MONTH_ABBREVIATIONS = new Set([
  'sty', 'lut', 'mar', 'kwi', 'cze', 'lip', 'sie', 'wrz', 'paz', 'lis', 'gru',
  'jan', 'feb', 'apr', 'jun', 'jul', 'aug', 'sep', 'sept', 'oct', 'nov', 'dec',
]);

/** Folded weekday word -> weekday. */
export const WEEKDAY_LOOKUP: ReadonlyMap<string, WeekdayWord> = buildWeekdayLookup();

/** Folded month word -> month. */
export const MONTH_LOOKUP: ReadonlyMap<string, MonthWord> = buildMonthLookup();

function buildWeekdayLookup(): Map<string, WeekdayWord> {
  const map = new Map<string, WeekdayWord>();
  for (const [day, singular, plural, abbr] of WEEKDAY_FORMS) {
    for (const w of singular) map.set(w, { day, abbr: false, plural: false });
    for (const w of plural) map.set(w, { day, abbr: false, plural: true });
    for (const w of abbr) map.set(w, { day, abbr: true, plural: false });
  }
  return map;
}

function buildMonthLookup(): Map<string, MonthWord> {
  const map = new Map<string, MonthWord>();
  for (const [month, pl, en] of MONTH_FORMS) {
    for (const w of pl) map.set(w, { month, en: false, abbr: MONTH_ABBREVIATIONS.has(w) });
    for (const w of en) map.set(w, { month, en: true, abbr: MONTH_ABBREVIATIONS.has(w) });
  }
  return map;
}

/** Own-property lookup in a plain record (never returns inherited members like "constructor"). */
export function own<V>(rec: Readonly<Record<string, V>>, key: string): V | undefined {
  return Object.hasOwn(rec, key) ? rec[key] : undefined;
}

/** Escape a string for use inside a RegExp. */
export function escapeRegExp(s: string): string {
  return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

/** Regex alternation of words, longest first (so prefixes never shadow longer words). */
export function alternation(words: Iterable<string>): string {
  return [...new Set(words)]
    .sort((a, b) => b.length - a.length || (a < b ? -1 : 1))
    .map(escapeRegExp)
    .join('|');
}

/** Folded weekday word -> day (0-6), or undefined. */
export function lookupWeekday(folded: string): WeekdayWord | undefined {
  return WEEKDAY_LOOKUP.get(folded);
}

/** Folded month word -> month info, or undefined. */
export function lookupMonth(folded: string): MonthWord | undefined {
  return MONTH_LOOKUP.get(folded);
}

// ---------------------------------------------------------------------------
// Number words (used for "za dwa dni", "every other week", "co trzy tygodnie")
// ---------------------------------------------------------------------------

/** Folded cardinal number words (Polish + English) -> value. */
export const NUMBER_WORDS: ReadonlyMap<string, number> = new Map([
  ['jeden', 1], ['jedna', 1], ['jedno', 1], ['jednego', 1], ['jednej', 1],
  ['dwa', 2], ['dwie', 2], ['dwoch', 2], ['trzy', 3], ['cztery', 4], ['piec', 5], ['szesc', 6],
  ['siedem', 7], ['osiem', 8], ['dziewiec', 9], ['dziesiec', 10], ['jedenascie', 11], ['dwanascie', 12],
  ['pietnascie', 15], ['dwadziescia', 20], ['trzydziesci', 30], ['czterdziesci', 40],
  ['one', 1], ['two', 2], ['three', 3], ['four', 4], ['five', 5], ['six', 6], ['seven', 7],
  ['eight', 8], ['nine', 9], ['ten', 10], ['eleven', 11], ['twelve', 12], ['fifteen', 15],
  ['twenty', 20], ['thirty', 30], ['forty', 40],
]);

/** Folded ordinal words used in "co drugi tydzień" / "every second week" -> value. */
export const ORDINAL_WORDS: ReadonlyMap<string, number> = new Map([
  ['drugi', 2], ['druga', 2], ['drugie', 2], ['drugiego', 2],
  ['trzeci', 3], ['trzecia', 3], ['trzecie', 3], ['trzeciego', 3],
  ['czwarty', 4], ['czwarta', 4], ['czwarte', 4], ['czwartego', 4],
  ['piaty', 5], ['piata', 5], ['piate', 5], ['piatego', 5],
  ['szosty', 6], ['szosta', 6], ['szoste', 6], ['szostego', 6],
  ['other', 2], ['second', 2], ['third', 3], ['fourth', 4], ['fifth', 5], ['sixth', 6],
]);
