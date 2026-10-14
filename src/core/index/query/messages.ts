/**
 * Localised texts of the query language (errors and group labels). Kept inside the module
 * (Polish default, English) so query errors can be shown without UI-side wiring.
 */
import type { Locale, Priority, TaskStatus } from '@/core/types';
import type { QueryErrorCode } from './types';

type Table = Record<QueryErrorCode, string>;

const ERRORS: Record<Locale, Table> = {
  pl: {
    unknownKey: 'Nieznany filtr „{key}:”. Dostępne: {allowed}. Tekst z dwukropkiem ujmij w cudzysłów.',
    emptyValue: 'Filtr „{key}:” wymaga wartości, np. {example}.',
    badValue: 'Nieprawidłowa wartość „{value}” w „{key}:”. Dozwolone: {allowed}.',
    badDate:
      'Nie rozumiem daty „{value}” w „{key}:”. Przykłady: today, tomorrow, 2026-10-15, +7d, -2w, this-week, next-month, overdue, none, any.',
    opWithKeyword: 'Operatora „{op}” nie można łączyć z „{value}” w „{key}:”.',
    badLimit: 'Limit musi być liczbą od 1 do {max} (jest „{value}”).',
    unclosedQuote: 'Brak zamykającego cudzysłowu.',
    notNegatable: 'Filtra „{key}:” nie można zanegować minusem.',
    typeConflict: '„{key}:{value}” nie działa z type:{type}.',
    duplicate: 'Filtr „{key}:” może wystąpić tylko raz.',
  },
  en: {
    unknownKey: 'Unknown filter "{key}:". Available: {allowed}. Put text containing a colon in quotes.',
    emptyValue: 'Filter "{key}:" needs a value, e.g. {example}.',
    badValue: 'Invalid value "{value}" in "{key}:". Allowed: {allowed}.',
    badDate:
      'Cannot understand the date "{value}" in "{key}:". Examples: today, tomorrow, 2026-10-15, +7d, -2w, this-week, next-month, overdue, none, any.',
    opWithKeyword: 'Operator "{op}" cannot be combined with "{value}" in "{key}:".',
    badLimit: 'Limit must be a number from 1 to {max} (got "{value}").',
    unclosedQuote: 'Missing closing quote.',
    notNegatable: 'Filter "{key}:" cannot be negated with a minus.',
    typeConflict: '"{key}:{value}" does not work with type:{type}.',
    duplicate: 'Filter "{key}:" may appear only once.',
  },
};

/** Format an error message with `{name}` placeholders. */
export function errorMessage(locale: Locale, code: QueryErrorCode, params: Record<string, string | number> = {}): string {
  const template = (ERRORS[locale] ?? ERRORS.pl)[code];
  return template.replace(/\{(\w+)\}/g, (m, name: string) => (name in params ? String(params[name]) : m));
}

const STATUS_LABELS: Record<Locale, Record<TaskStatus, string>> = {
  pl: { todo: 'Do zrobienia', doing: 'W toku', done: 'Zrobione', cancelled: 'Anulowane', deferred: 'Odłożone' },
  en: { todo: 'To do', doing: 'In progress', done: 'Done', cancelled: 'Cancelled', deferred: 'Deferred' },
};

const PRIORITY_LABELS: Record<Locale, Record<Priority | 'none', string>> = {
  pl: { highest: 'Najwyższy', high: 'Wysoki', medium: 'Średni', none: 'Normalny', low: 'Niski', lowest: 'Najniższy' },
  en: { highest: 'Highest', high: 'High', medium: 'Medium', none: 'Normal', low: 'Low', lowest: 'Lowest' },
};

const NONE_LABELS: Record<Locale, { date: string; tag: string; status: string; priority: string }> = {
  pl: { date: 'Bez daty', tag: 'Bez tagu', status: 'Bez statusu', priority: 'Bez priorytetu' },
  en: { date: 'No date', tag: 'No tag', status: 'No status', priority: 'No priority' },
};

export function statusLabel(locale: Locale, s: TaskStatus): string {
  return (STATUS_LABELS[locale] ?? STATUS_LABELS.pl)[s];
}

export function priorityLabel(locale: Locale, p: Priority | 'none'): string {
  return (PRIORITY_LABELS[locale] ?? PRIORITY_LABELS.pl)[p];
}

export function noneLabel(locale: Locale, what: 'date' | 'tag' | 'status' | 'priority'): string {
  return (NONE_LABELS[locale] ?? NONE_LABELS.pl)[what];
}
