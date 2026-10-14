/**
 * User-visible strings of the calc module (errors and a few value words), Polish + English.
 *
 * Kept local to the module (instead of `src/core/i18n/messages/calc.ts`) so the calc engine has
 * no dependency on the i18n loader (which relies on `import.meta.glob`) and stays usable in
 * workers and plain Node. Keys mirror the `calc.*` naming convention of the i18n module.
 */
import type { Locale } from '../types';

const pl = {
  'calc.error.generic': 'Błąd obliczenia',
  'calc.error.undefinedSymbol': 'Nieznana zmienna: {name}',
  'calc.error.undefinedFunction': 'Nieznana funkcja: {name}',
  'calc.error.unitsMismatch': 'Niezgodne jednostki',
  'calc.error.unexpectedEnd': 'Niedokończone wyrażenie',
  'calc.error.parenExpected': 'Brakuje nawiasu „{paren}”',
  'calc.error.valueExpected': 'Brakuje wartości',
  'calc.error.unexpectedOperator': 'Nieoczekiwany operator „{op}”',
  'calc.error.unexpectedPart': 'Nieoczekiwany fragment „{part}”',
  'calc.error.disabled': 'Funkcja {name} jest wyłączona',
  'calc.error.cannotAssign': 'Nie można przypisać do „{name}”',
  'calc.error.notANumber': 'Nie można zamienić „{text}” na liczbę',
  'calc.error.wrongType': 'Nieprawidłowy typ argumentu funkcji {name}',
  'calc.error.tooManyArgs': 'Za dużo argumentów funkcji {name}',
  'calc.error.tooFewArgs': 'Za mało argumentów funkcji {name}',
  'calc.error.recursion': 'Zbyt głęboka rekurencja',
  'calc.error.tooLong': 'Wyrażenie jest za długie',
  'calc.error.tooLarge': 'Wynik jest za duży (limit {max} elementów)',
  'calc.error.tooComplex': 'Obliczenie jest zbyt złożone',
  'calc.error.tooBig': 'Liczba jest za duża',
  'calc.error.notAFunction': '„{name}” nie jest funkcją',
  'calc.error.currencyConversion': 'Przeliczanie walut nie jest obsługiwane',
  'calc.error.noValues': 'Brak wartości liczbowych',
  'calc.error.emptyArray': 'Pusty zbiór wartości',
  'calc.error.cycle': 'Cykliczne odwołanie',
  'calc.error.refError': 'Błąd w komórce {cell}',
  'calc.error.refOutside': 'Odwołanie poza tabelą: {ref}',
  'calc.error.unknownColumn': 'Nieznana kolumna: {name}',
  'calc.value.true': 'prawda',
  'calc.value.false': 'fałsz',
} as const;

export type CalcMessageKey = keyof typeof pl;

const en: Record<CalcMessageKey, string> = {
  'calc.error.generic': 'Calculation error',
  'calc.error.undefinedSymbol': 'Unknown variable: {name}',
  'calc.error.undefinedFunction': 'Unknown function: {name}',
  'calc.error.unitsMismatch': 'Units do not match',
  'calc.error.unexpectedEnd': 'Unexpected end of expression',
  'calc.error.parenExpected': 'Missing “{paren}”',
  'calc.error.valueExpected': 'Value expected',
  'calc.error.unexpectedOperator': 'Unexpected operator “{op}”',
  'calc.error.unexpectedPart': 'Unexpected “{part}”',
  'calc.error.disabled': 'Function {name} is disabled',
  'calc.error.cannotAssign': 'Cannot assign to “{name}”',
  'calc.error.notANumber': 'Cannot convert “{text}” to a number',
  'calc.error.wrongType': 'Wrong argument type for {name}',
  'calc.error.tooManyArgs': 'Too many arguments for {name}',
  'calc.error.tooFewArgs': 'Too few arguments for {name}',
  'calc.error.recursion': 'Recursion too deep',
  'calc.error.tooLong': 'Expression is too long',
  'calc.error.tooLarge': 'Result is too large (limit {max} elements)',
  'calc.error.tooComplex': 'Calculation is too complex',
  'calc.error.tooBig': 'Number is too large',
  'calc.error.notAFunction': '“{name}” is not a function',
  'calc.error.currencyConversion': 'Currency conversion is not supported',
  'calc.error.noValues': 'No numeric values',
  'calc.error.emptyArray': 'Empty set of values',
  'calc.error.cycle': 'Circular reference',
  'calc.error.refError': 'Error in cell {cell}',
  'calc.error.refOutside': 'Reference outside the table: {ref}',
  'calc.error.unknownColumn': 'Unknown column: {name}',
  'calc.value.true': 'true',
  'calc.value.false': 'false',
};

/** Same shape as the i18n message modules (`{ pl, en }`), so it can be merged there later. */
export const messages: { pl: Record<CalcMessageKey, string>; en: Record<CalcMessageKey, string> } = { pl, en };

/** Translate a calc message key with `{param}` interpolation. */
export function calcMessage(locale: Locale, key: CalcMessageKey, params?: Record<string, string | number>): string {
  const template = (messages[locale] ?? messages.pl)[key];
  if (!params) return template;
  return template.replace(/\{(\w+)\}/g, (m, name: string) => (name in params ? String(params[name]) : m));
}

/**
 * An error whose message is already user-facing (translated). Thrown by the calc engine for
 * conditions it detects itself; mathjs errors are translated by `describeError`.
 */
export class CalcError extends Error {
  constructor(
    readonly key: CalcMessageKey,
    readonly params: Record<string, string | number> = {},
  ) {
    // The message carries key and params, so the error can still be translated after mathjs
    // re-wraps it (e.g. "Function map cannot apply callback arguments ...: calc.error.x{...}").
    super(Object.keys(params).length ? `${key}${JSON.stringify(params)}` : key);
    this.name = 'CalcError';
  }

  /** Recover a CalcError embedded in another error's message (see constructor), or null. */
  static fromMessage(message: string): CalcError | null {
    const m = /(calc\.error\.[A-Za-z]+)(\{[^{}]*\})?/.exec(message);
    if (!m || !(m[1]! in pl)) return null;
    let params: Record<string, string | number> = {};
    try {
      if (m[2]) params = JSON.parse(m[2]) as Record<string, string | number>;
    } catch {
      params = {};
    }
    return new CalcError(m[1] as CalcMessageKey, params);
  }

  /** The translated message. */
  describe(locale: Locale): string {
    return calcMessage(locale, this.key, this.params);
  }
}
