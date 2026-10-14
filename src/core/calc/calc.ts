/**
 * Calc blocks (```` ```calc ````) and inline calculations (`` `= expr` ``), SYNTAX.md §9.
 */
import type { MathType } from 'mathjs';
import type { Locale } from '../types';
import { getEngine, MAX_EXPRESSION_LENGTH, withBudget } from './engine';
import { evaluateTokens } from './expression';
import { describeError, formatValue, numericValue } from './format';
import { lex, matchBrackets, nextSignificant, significant, type Token } from './lexer';
import { CalcError } from './messages';
import { CalcScope, type CalcValue } from './model';
import { toInternalName } from './names';
import { AGGREGATE_WORDS, agreedCurrency, type AggregateKind } from './transform';
import type { CurrencyInfo } from './numbers';

/** Result of one line of a calc block. */
export interface CalcLineResult {
  /** 0-based line index within the block source. */
  line: number;
  /** The line as written. */
  input: string;
  /** Formatted result (absent for blank lines, comments, label-only lines and errors). */
  value?: string;
  /** Short human-readable error message. */
  error?: string;
  /** Name of the variable assigned on this line (as written, e.g. `ilość`). */
  assigned?: string;
  /** Numeric magnitude of the result (numbers; units in their displayed unit). */
  numeric?: number;
  /** Text before `:` (`Czynsz: 1800` → `Czynsz`). */
  label?: string;
}

/** Options of `evaluateCalc`. */
export interface EvaluateCalcOptions {
  /** Variables in scope before the first line (e.g. from `pageScope`). Not mutated. */
  scope?: Record<string, unknown>;
  /** Number formatting / decimal comma handling; default 'pl'. */
  locale?: Locale;
}

/** The internal name of `ans`. */
const ANS = 'ans';

/** Split `Label: expression`; returns the label (if any) and the expression tokens. */
export function splitLabel(tokens: Token[]): { label?: string; rest: Token[] } {
  let depth = 0;
  let colon = -1;
  for (let i = 0; i < tokens.length; i++) {
    const t = tokens[i]!;
    if (t.kind === 'open') depth++;
    else if (t.kind === 'close') depth--;
    else if (depth === 0 && t.kind === 'op' && t.text === ':') {
      colon = i;
      break;
    }
  }
  if (colon < 0) return { rest: tokens };
  const prefix = tokens.slice(0, colon);
  const sig = significant(prefix);
  const first = sig[0];
  if (!first || first.kind !== 'ident' || first.raw || !/^\p{L}/u.test(first.text)) return { rest: tokens };
  if (sig.some((t) => (t.kind === 'op' && (t.text.includes('=') || t.text === '?')) || t.kind === 'str')) {
    return { rest: tokens };
  }
  const label = prefix.map((t) => t.text).join('').trim();
  return { label, rest: tokens.slice(colon + 1) };
}

function hasTopLevelSemicolon(tokens: Token[]): boolean {
  let depth = 0;
  for (const t of tokens) {
    if (t.kind === 'open') depth++;
    else if (t.kind === 'close') depth--;
    else if (t.kind === 'semi' && depth === 0) return true;
  }
  return false;
}

type Assignment = { name: string; rhs: Token[]; simple: true } | { name: string; simple: false };

/** Detect `name = expr` (simple) and `f(x) = …` / `a[1] = …` (handled by mathjs). */
export function detectAssignment(tokens: Token[]): Assignment | null {
  const first = nextSignificant(tokens, 0);
  const target = tokens[first];
  if (!target || target.kind !== 'ident' || target.raw) return null;
  const second = nextSignificant(tokens, first + 1);
  const t2 = tokens[second];
  if (t2?.kind === 'op' && t2.text === '=') {
    const rhs = tokens.slice(second + 1);
    // `a = 1; b = 2` — several statements: let mathjs assign them all.
    return hasTopLevelSemicolon(rhs) ? { name: target.text, simple: false } : { name: target.text, rhs, simple: true };
  }
  if (t2?.kind === 'open' && (t2.text === '(' || t2.text === '[')) {
    const close = matchBrackets(tokens).get(second) ?? -1;
    if (close < 0) return null;
    const eq = tokens[nextSignificant(tokens, close + 1)];
    if (eq?.kind === 'op' && eq.text === '=') return { name: target.text, simple: false };
  }
  return null;
}

function aggregateKeyword(tokens: Token[], scope: CalcScope): AggregateKind | null {
  const sig = significant(tokens);
  if (sig.length !== 1 || sig[0]!.kind !== 'ident' || sig[0]!.raw) return null;
  const kind = AGGREGATE_WORDS.get(sig[0]!.text.toLowerCase());
  return kind && !scope.has(toInternalName(sig[0]!.text)) ? kind : null;
}

/** Values that take part in `sum` / `avg`: numbers and units, but not percentages. */
function isAggregatable(item: CalcValue): boolean {
  const math = getEngine().math;
  if (item.meta.percent) return false;
  if (typeof item.value === 'number') return true;
  return math.isUnit(item.value) && (item.value as unknown as { value: unknown }).value !== null;
}

/**
 * Sum or average of the results of a group of lines. Mixed units fall back to the plain
 * numbers; currencies label the result when they agree.
 */
export function aggregateValues(kind: AggregateKind, items: readonly CalcValue[]): CalcValue {
  const math = getEngine().math;
  const usable = items.filter(isAggregatable);
  if (usable.length === 0) {
    if (kind === 'sum') return { value: 0, meta: {} };
    throw new CalcError('calc.error.noValues');
  }
  let used = usable;
  let total: unknown;
  try {
    total = usable.slice(1).reduce<unknown>((acc, it) => math.add(acc as MathType, it.value as MathType), usable[0]!.value);
  } catch (err) {
    used = usable.filter((it) => typeof it.value === 'number');
    if (used.length === 0) throw err;
    total = used.reduce((acc, it) => acc + (it.value as number), 0);
  }
  const value = kind === 'sum' ? total : math.divide(total as MathType, used.length);
  const currencies = used.map((it) => it.meta.currency).filter((c): c is CurrencyInfo => !!c);
  const currency = agreedCurrency(currencies);
  return { value, meta: currency ? { currency } : {} };
}

function isDataValue(value: unknown): boolean {
  return value !== undefined && value !== null && typeof value !== 'function';
}

interface LineState {
  scope: CalcScope;
  group: CalcValue[];
  locale: Locale;
}

/** Evaluate one non-blank, non-comment line; fills `res`. Throws on errors. */
function evaluateLine(text: string, res: CalcLineResult, state: LineState): void {
  const { scope, locale } = state;
  if (text.length > MAX_EXPRESSION_LENGTH) throw new CalcError('calc.error.tooLong');
  const { tokens } = lex(text);
  const { label, rest } = splitLabel(tokens);
  if (label) res.label = label;

  let outcome: CalcValue | null = null;
  let isAggregate = false;
  const labelAggregate = label && significant(rest).length === 0 ? AGGREGATE_WORDS.get(label.toLowerCase()) : undefined;
  const keyword = labelAggregate ?? aggregateKeyword(rest, scope);
  const env = { locale, scope, aggregate: (kind: AggregateKind) => aggregateValues(kind, state.group) };

  if (keyword) {
    outcome = aggregateValues(keyword, state.group);
    isAggregate = true;
  } else {
    if (significant(rest).length === 0) return;
    const assignment = detectAssignment(rest);
    if (assignment?.simple) {
      const internal = toInternalName(assignment.name);
      if (getEngine().blocked.has(internal)) {
        throw new CalcError('calc.error.cannotAssign', { name: assignment.name });
      }
      const rhsKeyword = aggregateKeyword(assignment.rhs, scope);
      outcome = rhsKeyword ? aggregateValues(rhsKeyword, state.group) : evaluateTokens(assignment.rhs, env);
      isAggregate = !!rhsKeyword;
      if (outcome.value === undefined || outcome.value === null) throw new CalcError('calc.error.valueExpected');
      scope.set(internal, outcome.value, outcome.meta);
      res.assigned = assignment.name;
    } else {
      outcome = evaluateTokens(rest, env);
      if (assignment) {
        res.assigned = assignment.name;
        scope.meta.delete(toInternalName(assignment.name));
      }
    }
  }

  const formatted = formatValue(outcome.value, outcome.meta, locale);
  if (formatted !== undefined) res.value = formatted;
  const numeric = numericValue(outcome.value);
  if (numeric !== undefined) res.numeric = numeric;
  // Percentages are rates, not results: they neither become `ans` nor join the group.
  if (isDataValue(outcome.value) && !outcome.meta.percent) {
    scope.set(ANS, outcome.value, outcome.meta);
    if (!isAggregate) state.group.push(outcome);
  }
}

/**
 * Evaluate a calc block line by line into `scope` (mutated). Used by `evaluateCalc` and
 * `pageScope`.
 */
export function evaluateCalcInto(source: string, scope: CalcScope, locale: Locale): CalcLineResult[] {
  const state: LineState = { scope, group: [], locale };
  const results: CalcLineResult[] = [];
  source.split(/\r\n|\r|\n/).forEach((input, line) => {
    const res: CalcLineResult = { line, input };
    results.push(res);
    const text = input.trim();
    if (!text) {
      state.group = [];
      return;
    }
    if (text.startsWith('#') || text.startsWith('//')) return;
    try {
      evaluateLine(text, res, state);
    } catch (err) {
      res.error = describeError(err, locale);
    }
  });
  return results;
}

/**
 * Evaluate the source of a calc block. Each line is evaluated top to bottom with a shared
 * scope: assignments (`ilość = 4`), `ans`, labels (`Czynsz: 1800`), comments (`#`, `//`),
 * aggregates (`suma`, `avg` — of the lines above, up to the previous blank line), percent
 * sugar, units, currencies and decimal commas. Never throws: errors are reported per line.
 */
export function evaluateCalc(
  source: string,
  opts: EvaluateCalcOptions = {},
): { lines: CalcLineResult[]; scope: Record<string, unknown> } {
  const locale = opts.locale ?? 'pl';
  const scope = CalcScope.fromRecord(opts.scope);
  const lines = withBudget(() => evaluateCalcInto(source, scope, locale));
  return { lines, scope: scope.toRecord() };
}

/**
 * Evaluate an inline calculation (the code of `` `= cena * 2` `` without the leading `=`; a
 * leading `=` is tolerated). `scope` is not modified. Returns `{}` for an empty expression.
 */
export function evaluateInline(
  expr: string,
  scope: Record<string, unknown>,
  locale: Locale = 'pl',
): { value?: string; error?: string } {
  const text = expr.trim().replace(/^=/, '').trim();
  if (!text) return {};
  return withBudget(() => evaluateInlineText(text, scope, locale));
}

function evaluateInlineText(text: string, scope: Record<string, unknown>, locale: Locale): { value?: string; error?: string } {
  try {
    if (text.length > MAX_EXPRESSION_LENGTH) throw new CalcError('calc.error.tooLong');
    const local = CalcScope.fromRecord(scope);
    const { tokens } = lex(text);
    const assignment = detectAssignment(tokens);
    const outcome = evaluateTokens(assignment?.simple ? assignment.rhs : tokens, { locale, scope: local });
    const value = formatValue(outcome.value, outcome.meta, locale);
    return value === undefined ? {} : { value };
  } catch (err) {
    return { error: describeError(err, locale) };
  }
}
