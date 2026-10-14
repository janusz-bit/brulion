/**
 * Rewrites calc-dialect tokens into plain mathjs code.
 *
 * Passes (in order):
 * 1. numbers — decimal comma (`12,5`), thousands separators (`1 000`, English `1,000`),
 *    Excel-style `;` argument separators (`round(2,345; 2)`);
 * 2. `√x` → `sqrt(x)`;
 * 3. currencies — `zł`, `PLN`, `€`, `$`, ... are removed and collected for result labelling;
 * 4. identifiers — Unicode names mangled (see names.ts), aggregate keywords (`suma`), unit
 *    aliases (`min`, `godz`, `dni`), Polish `na` = `to`, case-insensitive function calls (`SUM(`);
 * 5. percent sugar — `x + 10%` → `x * (1 + 10/100)`, `20% of x` / `20% z x` → `20/100 * x`,
 *    `15%` → `(15/100)`, `x as % of y` / `x jako % z y` → `x / y`.
 */
import type { Locale } from '../types';
import {
  endsOperand,
  matchBrackets,
  nextSignificant,
  OPERATOR_WORDS,
  prevSignificant,
  tokenCode,
  type Token,
} from './lexer';
import { CalcError } from './messages';
import type { ValueMeta } from './model';
import { toInternalName, TEMP_PREFIX } from './names';
import { CURRENCY_CODES, type CurrencyInfo } from './numbers';

export type AggregateKind = 'sum' | 'avg';

/** Bare words that stand for the running aggregate of a calc block (case-insensitive). */
export const AGGREGATE_WORDS: ReadonlyMap<string, AggregateKind> = new Map([
  ['sum', 'sum'],
  ['total', 'sum'],
  ['suma', 'sum'],
  ['razem', 'sum'],
  ['avg', 'avg'],
  ['average', 'avg'],
  ['mean', 'avg'],
  ['średnia', 'avg'],
  ['srednia', 'avg'],
]);

/** Internal names of the aggregate temporaries. */
export const AGGREGATE_NAMES: Record<AggregateKind, string> = { sum: `${TEMP_PREFIX}sum`, avg: `${TEMP_PREFIX}avg` };

/** Unit words that clash with mathjs functions or are Polish → mathjs unit. */
const UNIT_ALIASES: ReadonlyMap<string, string> = new Map([
  ['min', 'minute'],
  ['sec', 'second'],
  ['godz', 'h'],
  ['godzina', 'h'],
  ['godziny', 'h'],
  ['godzin', 'h'],
  ['minut', 'minute'],
  ['minuta', 'minute'],
  ['minuty', 'minute'],
  ['sek', 's'],
  ['sekunda', 's'],
  ['sekundy', 's'],
  ['sekund', 's'],
  ['dzień', 'day'],
  ['dni', 'day'],
  ['dnia', 'day'],
  ['doba', 'day'],
  ['doby', 'day'],
  ['tydzień', 'week'],
  ['tygodnie', 'week'],
  ['tygodni', 'week'],
  ['tyg', 'week'],
  ['miesiąc', 'month'],
  ['miesiące', 'month'],
  ['miesięcy', 'month'],
  ['mies', 'month'],
  ['rok', 'year'],
  ['roku', 'year'],
  ['lata', 'year'],
  ['lat', 'year'],
]);

/** Words meaning "of" after a percentage: `20% of x`, `20% z x`, `20% od x`. */
const OF_WORDS = new Set(['of', 'z', 'od']);
/** Words of `x as % of y` / `x jako % z y`. */
const AS_WORDS = new Set(['as', 'jako']);
/** Operators with lower precedence than `+`/`-`; they delimit percent chains. */
const BOUNDARY_OPS = new Set(['=', '==', '!=', '<', '>', '<=', '>=', '?', ':', '&', '|', '<<', '>>', '>>>']);
/** Words after which a following identifier is a unit (`5 min`, `to min`, `km/godz`). */
const UNIT_CONTEXT_WORDS = new Set(['to', 'in', 'per']);

/** What the transformer needs to know about the evaluation environment. */
export interface TransformContext {
  locale: Locale;
  /** A variable with this internal name exists (user scope or temporaries). */
  isDefined(internal: string): boolean;
  /** Display metadata of a variable. */
  metaOf(internal: string): ValueMeta | undefined;
  /** Something callable exists under this internal name. */
  isFunction(internal: string): boolean;
  /** `name` is a mathjs unit (with or without prefix). */
  isUnit(name: string): boolean;
  /** Replace bare aggregate keywords (`suma`) by the running aggregate (calc blocks only). */
  aggregates: boolean;
}

export interface TransformResult {
  /** mathjs code. */
  code: string;
  /** The whole expression is a percentage (`15%`, `rabat`, `10% + 5%`, `x as % of y`). */
  percent: boolean;
  /** Currencies written in the expression or carried by referenced variables. */
  currencies: CurrencyInfo[];
  /** Aggregate keywords used. */
  aggregates: Set<AggregateKind>;
  /** Internal names of referenced variables. */
  refs: Set<string>;
}

// ---------------------------------------------------------------------------
// Pass 1: numbers
// ---------------------------------------------------------------------------

interface Frame {
  kind: string;
  call: boolean;
  semi: boolean;
}

function isCallTarget(tok: Token | undefined): boolean {
  return !!tok && tok.kind === 'ident' && !OPERATOR_WORDS.has(tok.text) && !OF_WORDS.has(tok.text);
}

/** Innermost bracket frame of every token; `semi` marks frames that contain `;`. */
function computeFrames(tokens: readonly Token[]): (Frame | null)[] {
  const frames: (Frame | null)[] = [];
  const stack: Frame[] = [];
  tokens.forEach((t, i) => {
    const top = stack[stack.length - 1] ?? null;
    if (t.kind === 'open') {
      frames[i] = top;
      const prev = tokens[prevSignificant(tokens, i - 1)];
      stack.push({ kind: t.text, call: t.text === '(' && isCallTarget(prev), semi: false });
    } else if (t.kind === 'close') {
      stack.pop();
      frames[i] = stack[stack.length - 1] ?? null;
    } else {
      frames[i] = top;
      if (t.kind === 'semi' && top) top.semi = true;
    }
  });
  return frames;
}

const adjacent = (a: Token, b: Token) => a.pos + a.text.length === b.pos;
const PURE_INT = /^\d+$/;

/** A decimal comma is allowed outside function arguments and matrices. */
function commaIsDecimal(frame: Frame | null): boolean {
  return frame === null || (frame.kind === '(' && (!frame.call || frame.semi));
}

function numberCode(tok: Token): string {
  return tok.out ?? tok.text;
}

function mergeDecimalCommas(tokens: Token[], frames: (Frame | null)[], locale: Locale): Token[] {
  const out: Token[] = [];
  for (let i = 0; i < tokens.length; i++) {
    const t = tokens[i]!;
    const comma = tokens[i + 1];
    const next = tokens[i + 2];
    if (
      t.kind === 'num' &&
      PURE_INT.test(t.text) &&
      comma?.kind === 'comma' &&
      next?.kind === 'num' &&
      /^\d/.test(next.text) &&
      adjacent(t, comma) &&
      adjacent(comma, next) &&
      commaIsDecimal(frames[i + 1] ?? null)
    ) {
      // English: 1,000 / 12,345,678 are thousands separators.
      if (locale === 'en' && /^\d{1,3}$/.test(t.text) && /^\d{3}(?:\.\d+)?$/.test(next.text)) {
        let text = t.text;
        let digits = t.text;
        let j = i + 1;
        while (
          tokens[j]?.kind === 'comma' &&
          tokens[j + 1]?.kind === 'num' &&
          /^\d{3}(?:\.\d+)?$/.test(tokens[j + 1]!.text) &&
          adjacent(tokens[j - 1]!, tokens[j]!) &&
          adjacent(tokens[j]!, tokens[j + 1]!)
        ) {
          text += `,${tokens[j + 1]!.text}`;
          digits += tokens[j + 1]!.text;
          j += 2;
          if (digits.includes('.')) break;
        }
        out.push({ kind: 'num', text, pos: t.pos, out: digits });
        i = j - 1;
        continue;
      }
      out.push({ kind: 'num', text: `${t.text},${next.text}`, pos: t.pos, out: `${t.text}.${next.text}` });
      i += 2;
      continue;
    }
    if (t.kind === 'semi' && frames[i]?.call && frames[i]?.kind === '(') {
      out.push({ ...t, out: ',' });
      continue;
    }
    out.push(t);
  }
  return out;
}

const GROUP_SPACE = /^[ \u00a0\u2009\u202f]$/;

function mergeSpaceGroups(tokens: Token[]): Token[] {
  const out: Token[] = [];
  for (let i = 0; i < tokens.length; i++) {
    const t = tokens[i]!;
    if (t.kind === 'num' && /^\d{1,3}$/.test(t.text) && tokens[i - 1]?.kind !== 'num') {
      let text = t.text;
      let code = t.text;
      let j = i + 1;
      while (
        tokens[j]?.kind === 'ws' &&
        GROUP_SPACE.test(tokens[j]!.text) &&
        tokens[j + 1]?.kind === 'num' &&
        /^\d{3}(?:[.,]\d+)?$/.test(tokens[j + 1]!.text) &&
        /^\d{3}(?:\.\d+)?$/.test(numberCode(tokens[j + 1]!))
      ) {
        text += tokens[j]!.text + tokens[j + 1]!.text;
        code += numberCode(tokens[j + 1]!);
        j += 2;
        if (code.includes('.')) break;
      }
      if (j > i + 1) {
        out.push({ kind: 'num', text, pos: t.pos, out: code });
        i = j - 1;
        continue;
      }
    }
    out.push(t);
  }
  return out;
}

/** Pass 1: decimal commas, thousands separators and `;` argument separators. */
export function normalizeNumbers(tokens: Token[], locale: Locale): Token[] {
  const merged = mergeDecimalCommas(tokens, computeFrames(tokens), locale);
  return mergeSpaceGroups(merged);
}

// ---------------------------------------------------------------------------
// Pass 2: √
// ---------------------------------------------------------------------------

/** Pass 2: `√16` → `sqrt(16)`, `√(x)` → `sqrt(x)`. */
export function expandRoots(tokens: Token[]): Token[] {
  const out = [...tokens];
  for (let i = 0; i < out.length; i++) {
    const t = out[i]!;
    if (t.kind !== 'op' || t.text !== '√') continue;
    out[i] = { kind: 'ident', text: '√', pos: t.pos, out: 'sqrt', raw: true };
    const n = nextSignificant(out, i + 1);
    const atom = out[n];
    if (!atom || atom.kind === 'open') continue;
    let end = n;
    if (atom.kind === 'ident') {
      const after = nextSignificant(out, n + 1);
      if (out[after]?.kind === 'open' && out[after]!.text === '(') {
        end = matchBrackets(out).get(after) ?? -1;
        if (end === -1) end = out.length - 1;
      }
    }
    out.splice(end + 1, 0, { kind: 'close', text: ')', pos: atom.pos });
    out.splice(n, 0, { kind: 'open', text: '(', pos: atom.pos });
  }
  return out;
}

// ---------------------------------------------------------------------------
// Pass 3: currencies
// ---------------------------------------------------------------------------

function isCurrencyToken(tok: Token, ctx: TransformContext): boolean {
  if (tok.kind === 'cur') return true;
  return tok.kind === 'ident' && !tok.raw && CURRENCY_CODES.has(tok.text) && !ctx.isDefined(toInternalName(tok.text));
}

/** Pass 3: strip currency symbols/codes (turned into whitespace) and collect them. */
export function extractCurrencies(tokens: Token[], ctx: TransformContext): { tokens: Token[]; currencies: CurrencyInfo[] } {
  const currencies: CurrencyInfo[] = [];
  const out = tokens.map((t, i) => {
    if (!isCurrencyToken(t, ctx)) return t;
    const prev = tokens[prevSignificant(tokens, i - 1)];
    if (prev && (prev.kind === 'ident' && (prev.text === 'to' || prev.text === 'in' || prev.text === 'na'))) {
      throw new CalcError('calc.error.currencyConversion');
    }
    const next = tokens[nextSignificant(tokens, i + 1)];
    const prefix = !endsOperand(prev) && !!next && (next.kind === 'num' || next.kind === 'open');
    currencies.push({ code: CURRENCY_CODES.get(t.text)!, symbol: t.text, prefix });
    return { kind: 'ws', text: ' ', pos: t.pos } satisfies Token;
  });
  return { tokens: out, currencies };
}

// ---------------------------------------------------------------------------
// Pass 4: identifiers
// ---------------------------------------------------------------------------

function isQuantityContext(prev: Token | undefined): boolean {
  if (!prev) return false;
  if (prev.kind === 'num' || prev.kind === 'close') return true;
  if (prev.kind === 'op') return ['/', '*', './', '.*'].includes(tokenCode(prev)) || (prev.out?.startsWith('^(') ?? false);
  return prev.kind === 'ident' && (UNIT_CONTEXT_WORDS.has(prev.text) || prev.out === 'to');
}

/** Pass 4: mangle identifiers and apply keyword/alias rules. */
export function rewriteIdentifiers(
  tokens: Token[],
  ctx: TransformContext,
): { tokens: Token[]; refs: Set<string>; aggregates: Set<AggregateKind> } {
  const refs = new Set<string>();
  const aggregates = new Set<AggregateKind>();
  const out = tokens.map((t) => ({ ...t }));
  out.forEach((t, i) => {
    if (t.kind === 'ident' && t.raw && t.ref) refs.add(t.ref);
    if (t.kind !== 'ident' || t.raw || OPERATOR_WORDS.has(t.text)) return;
    const internal = toInternalName(t.text);
    const nextIdx = nextSignificant(out, i + 1);
    const next = out[nextIdx];
    const isCall = next?.kind === 'open' && next.text === '(';
    const prev = out[prevSignificant(out, i - 1)];
    const defined = ctx.isDefined(internal);

    if (!defined && t.text === 'na' && endsOperand(prev) && next?.kind === 'ident') {
      if (ctx.isUnit(next.text) || UNIT_ALIASES.has(next.text)) {
        t.out = 'to';
        t.raw = true;
        return;
      }
    }
    const aggregate = AGGREGATE_WORDS.get(t.text.toLowerCase());
    if (ctx.aggregates && aggregate && !isCall && !defined) {
      t.out = AGGREGATE_NAMES[aggregate];
      t.raw = true;
      aggregates.add(aggregate);
      return;
    }
    const alias = UNIT_ALIASES.get(t.text);
    if (alias && !isCall && !defined && isQuantityContext(prev)) {
      t.out = alias;
      t.raw = true;
      return;
    }
    if (isCall && !defined && !ctx.isFunction(internal) && t.text !== t.text.toLowerCase()) {
      const lower = toInternalName(t.text.toLowerCase());
      if (ctx.isFunction(lower)) {
        t.out = lower;
        t.raw = true;
        return;
      }
    }
    t.out = internal;
    t.raw = true;
    if (defined) {
      t.ref = internal;
      refs.add(internal);
    }
  });
  return { tokens: out, refs, aggregates };
}

// ---------------------------------------------------------------------------
// Pass 5: percent sugar (on a bracket tree)
// ---------------------------------------------------------------------------

type Node =
  | { type: 'tok'; tok: Token }
  | { type: 'group'; open: Token; close: Token | null; children: Node[]; code: string; percent?: boolean }
  | { type: 'code'; code: string; operand: boolean; fraction?: string };

function buildTree(tokens: Token[]): Node[] {
  const pairs = matchBrackets(tokens);
  const build = (from: number, to: number): Node[] => {
    const nodes: Node[] = [];
    let i = from;
    while (i < to) {
      const t = tokens[i]!;
      if (t.kind === 'open') {
        const close = pairs.get(i) ?? -1;
        const closed = close !== -1 && close < to;
        const end = closed ? close : to;
        nodes.push({ type: 'group', open: t, close: closed ? tokens[close]! : null, children: build(i + 1, end), code: '' });
        i = closed ? close + 1 : to;
      } else {
        nodes.push({ type: 'tok', tok: t });
        i++;
      }
    }
    return nodes;
  };
  return build(0, tokens.length);
}

function nodeCode(n: Node): string {
  if (n.type === 'tok') return tokenCode(n.tok);
  return n.code;
}

function emitNodes(nodes: readonly Node[]): string {
  return nodes.map(nodeCode).join('');
}

const isWs = (n: Node | undefined) => n?.type === 'tok' && n.tok.kind === 'ws';

function nextSig(nodes: readonly Node[], from: number): number {
  for (let i = from; i < nodes.length; i++) if (!isWs(nodes[i])) return i;
  return -1;
}

function prevSig(nodes: readonly Node[], from: number): number {
  for (let i = from; i >= 0; i--) if (!isWs(nodes[i])) return i;
  return -1;
}

function endsOperandNode(n: Node | undefined): boolean {
  if (!n) return false;
  if (n.type === 'group') return true;
  if (n.type === 'code') return n.operand;
  return endsOperand(n.tok);
}

function isOpNode(n: Node | undefined, ...ops: string[]): boolean {
  return n?.type === 'tok' && n.tok.kind === 'op' && ops.includes(tokenCode(n.tok));
}

function isWordNode(n: Node | undefined, words: ReadonlySet<string>): boolean {
  return n?.type === 'tok' && n.tok.kind === 'ident' && words.has(n.tok.text.toLowerCase()) && !n.tok.ref;
}

function startsOperandNode(n: Node | undefined): boolean {
  if (!n) return false;
  if (n.type === 'group') return n.open.text !== '{';
  if (n.type === 'code') return n.operand;
  const t = n.tok;
  if (t.kind === 'num' || t.kind === 'str') return true;
  if (t.kind === 'ident') return !OPERATOR_WORDS.has(t.text);
  return t.kind === 'op' && (tokenCode(t) === '-' || tokenCode(t) === '+');
}

function isBoundary(n: Node): boolean {
  if (n.type !== 'tok') return false;
  const t = n.tok;
  if (t.kind === 'comma' || t.kind === 'semi') return true;
  if (t.kind === 'op') return BOUNDARY_OPS.has(tokenCode(t));
  if (t.kind === 'ident') return ['to', 'in', 'and', 'or', 'xor', 'not'].includes(tokenCode(t));
  return false;
}

/** Replace `base %` by `(base/100)` and `base % of x` by `(base/100)* x`. */
function applyPercentPostfix(nodes: readonly Node[], ctx: TransformContext): Node[] {
  const out: Node[] = [];
  for (let k = 0; k < nodes.length; k++) {
    const n = nodes[k]!;
    if (isOpNode(n, '%')) {
      const baseIdx = prevSig(out, out.length - 1);
      const base = out[baseIdx];
      const nextIdx = nextSig(nodes, k + 1);
      const next = nodes[nextIdx];
      let mode: 'postfix' | 'of' | 'mod' = 'mod';
      if (endsOperandNode(base)) {
        if (!next) mode = 'postfix';
        else if (next.type === 'tok' && next.tok.kind === 'op') mode = 'postfix';
        else if (isWordNode(next, OF_WORDS) && startsOperandNode(nodes[nextSig(nodes, nextIdx + 1)])) mode = 'of';
      }
      if (mode === 'mod') {
        out.push(n);
        continue;
      }
      // A call `f(x)%` takes the function name with it.
      let start = baseIdx;
      if (base?.type === 'group' && base.open.text === '(') {
        const before = out[prevSig(out, baseIdx - 1)];
        if (before?.type === 'tok' && isCallTarget(before.tok)) start = prevSig(out, baseIdx - 1);
      }
      const baseCode = emitNodes(out.slice(start, baseIdx + 1)).trim();
      out.splice(start);
      const fraction = `(${baseCode}/100)`;
      if (mode === 'postfix') {
        out.push({ type: 'code', code: fraction, operand: true, fraction });
      } else {
        out.push({ type: 'code', code: `${fraction} *`, operand: false });
        k = nextIdx; // skip the "of" word
      }
      continue;
    }
    // `rabat of x` where rabat holds a percentage.
    if (isWordNode(n, OF_WORDS)) {
      const prev = out[prevSig(out, out.length - 1)];
      if (prev?.type === 'tok' && prev.tok.ref && ctx.metaOf(prev.tok.ref)?.percent && startsOperandNode(nodes[nextSig(nodes, k + 1)])) {
        out.push({ type: 'code', code: '*', operand: false });
        continue;
      }
    }
    out.push(n);
  }
  return out;
}

interface Term {
  op: string | null;
  nodes: Node[];
}

function splitTerms(nodes: readonly Node[]): Term[] {
  const terms: Term[] = [{ op: null, nodes: [] }];
  let prev: Node | undefined;
  for (const n of nodes) {
    if (isOpNode(n, '+', '-') && endsOperandNode(prev)) {
      terms.push({ op: tokenCode((n as { tok: Token }).tok), nodes: [] });
      prev = n;
      continue;
    }
    terms[terms.length - 1]!.nodes.push(n);
    if (!isWs(n)) prev = n;
  }
  return terms;
}

/** The fraction code of a term that is just a percentage, else null. */
function percentFraction(term: Term, ctx: TransformContext): string | null {
  const sig = term.nodes.filter((n) => !isWs(n));
  const single = (n: Node | undefined): string | null => {
    if (n?.type === 'code' && n.fraction) return n.fraction;
    if (n?.type === 'group' && n.percent && n.open.text === '(' && n.close) return n.code;
    if (n?.type === 'tok' && n.tok.ref && ctx.metaOf(n.tok.ref)?.percent) return tokenCode(n.tok);
    return null;
  };
  if (sig.length === 1) return single(sig[0]);
  if (sig.length === 2 && isOpNode(sig[0], '-', '+')) {
    const f = single(sig[1]);
    return f === null ? null : `(${tokenCode((sig[0] as { tok: Token }).tok)}${f})`;
  }
  return null;
}

interface Chunk {
  code: string;
  percent: boolean;
}

function transformChain(nodes: readonly Node[], ctx: TransformContext): Chunk {
  // x as % of y
  for (let k = 0; k < nodes.length; k++) {
    if (!isWordNode(nodes[k], AS_WORDS)) continue;
    const pct = nextSig(nodes, k + 1);
    const of = nextSig(nodes, pct + 1);
    if (pct > 0 && of > 0 && isOpNode(nodes[pct], '%') && isWordNode(nodes[of], OF_WORDS)) {
      const left = transformChain(nodes.slice(0, k), ctx).code.trim();
      const right = transformChain(nodes.slice(of + 1), ctx).code.trim();
      return { code: `((${left}) / (${right}))`, percent: true };
    }
  }
  const terms = splitTerms(applyPercentPostfix(nodes, ctx));
  const fractions = terms.map((t) => percentFraction(t, ctx));
  if (fractions.every((f) => f !== null)) {
    return { code: terms.map((t) => (t.op ?? '') + emitNodes(t.nodes)).join(''), percent: true };
  }
  let acc = emitNodes(terms[0]!.nodes);
  for (let k = 1; k < terms.length; k++) {
    const term = terms[k]!;
    const fraction = fractions[k];
    if (fraction && acc.trim() !== '') acc = `(${acc.trim()}) * (1 ${term.op} ${fraction}) `;
    else acc = `${acc}${term.op}${emitNodes(term.nodes)}`;
  }
  return { code: acc, percent: false };
}

function transformSeq(nodes: readonly Node[], ctx: TransformContext): Chunk {
  for (const n of nodes) {
    if (n.type === 'group') {
      const inner = transformSeq(n.children, ctx);
      n.code = n.open.text + inner.code + (n.close ? n.close.text : '');
      n.percent = inner.percent;
    }
  }
  const chunks: Chunk[] = [];
  let current: Node[] = [];
  let code = '';
  for (const n of nodes) {
    if (isBoundary(n)) {
      const c = transformChain(current, ctx);
      chunks.push(c);
      code += c.code + nodeCode(n);
      current = [];
    } else {
      current.push(n);
    }
  }
  const last = transformChain(current, ctx);
  chunks.push(last);
  code += last.code;
  return { code, percent: chunks.length === 1 && last.percent };
}

/** Pass 5: percent sugar over the whole token list. */
export function rewritePercents(tokens: Token[], ctx: TransformContext): Chunk {
  return transformSeq(buildTree(tokens), ctx);
}

// ---------------------------------------------------------------------------
// Pipeline
// ---------------------------------------------------------------------------

/** String literals (a `'` transpose may be taken for one; that only skips some collapsing). */
const STRING_LITERAL_RE = /("(?:\\.|[^"\\])*"?|'(?:\\.|[^'\\])*'?)/;

/** Collapse runs of whitespace to one space, except inside string literals. */
export function collapseWhitespace(code: string): string {
  return code
    .split(STRING_LITERAL_RE)
    .map((part, i) => (i % 2 === 1 ? part : part.replace(/\s+/g, ' ')))
    .join('')
    .trim();
}

/** Run all passes over the tokens of one expression. */
export function transformTokens(tokens: Token[], ctx: TransformContext): TransformResult {
  let toks = normalizeNumbers(tokens, ctx.locale);
  toks = expandRoots(toks);
  const cur = extractCurrencies(toks, ctx);
  const ids = rewriteIdentifiers(cur.tokens, ctx);
  const { code, percent } = rewritePercents(ids.tokens, ctx);
  const currencies = [...cur.currencies];
  for (const ref of ids.refs) {
    const c = ctx.metaOf(ref)?.currency;
    if (c) currencies.push(c);
  }
  return { code: collapseWhitespace(code), percent, currencies, aggregates: ids.aggregates, refs: ids.refs };
}

/** The single currency all operands agree on, or undefined (none or mixed). */
export function agreedCurrency(currencies: readonly CurrencyInfo[]): CurrencyInfo | undefined {
  const first = currencies[0];
  if (!first) return undefined;
  return currencies.every((c) => c.code === first.code) ? first : undefined;
}
