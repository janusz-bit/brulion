/**
 * `@/core/dates` — local date utilities, Polish + English natural-language dates, recurrence
 * rules, query date expressions and human formatting. See docs/ARCHITECTURE.md.
 */
export {
  addDaysLocal,
  addMinutesLocal,
  addMonthsLocal,
  addYearsLocal,
  compareLocal,
  datePart,
  dayNumber,
  daysInMonth,
  diffDaysLocal,
  endOfMonthLocal,
  endOfWeekLocal,
  formatHm,
  formatYmd,
  fromDayNumber,
  isLeapYear,
  isLocalDate,
  isLocalDateTime,
  isValidTime,
  isValidYmd,
  joinDateTime,
  localParts,
  normalizeLocalDateTime,
  normalizeTime,
  normalizeWeekStart,
  parseLocal,
  splitLocal,
  startOfMonthLocal,
  startOfWeekLocal,
  timePart,
  toFileDateTime,
  toLocalDate,
  toLocalDateTime,
  todayLocal,
  weekdayOf,
} from './local';
export type { LocalParts, SplitLocal } from './local';

export { findNaturalDates, parseNaturalDate, stripNaturalDate } from './natural';
export type { NaturalDateOptions, NaturalDateResult } from './natural';

export {
  canonicalRecurrence,
  formatRecurrence,
  MAX_INTERVAL,
  nextOccurrence,
  normalizeRule,
  occurrencesBetween,
  ordinalEn,
  parseRecurrence,
  parseWeekdayList,
  repeaterToRule,
  ruleToRepeater,
} from './recurrence';
export type { RecurrenceRule } from './recurrence';

export { resolveDateExpr } from './expr';
export type { DateExprResult, DateRange } from './expr';

export { formatDateHuman, formatDateShort } from './format';

export { foldText } from './fold';

export {
  EN_MONTHS,
  EN_MONTHS_SHORT,
  EN_WEEKDAYS,
  EN_WEEKDAYS_SHORT,
  PL_MONTHS,
  PL_MONTHS_GEN,
  PL_MONTHS_SHORT,
  PL_WEEKDAYS,
  PL_WEEKDAYS_ACC,
  PL_WEEKDAYS_SHORT,
  monthName,
  monthShortName,
  weekdayName,
  weekdayShortName,
} from './names';
