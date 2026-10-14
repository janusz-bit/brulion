/**
 * Tokenizer for calc expressions. It recognises just enough of the mathjs syntax to rewrite
 * expressions safely (numbers, identifiers, strings, operators, brackets) and adds the calc
 * dialect: Unicode identifiers, currency symbols, comments (`#`, `//`) and Unicode math symbols
 * (`×`, `÷`, `−`, `≤`, `²`, `π`, `√`, `°C`, ...).
 *
 * Tokens keep their source text; transformation passes set `out` (the mathjs code to emit).
 */
import { IDENT_PART_RE, IDENT_START_RE } from './names';
import { CURRENCY_SYMBOL_CHARS } from './numbers';

export type TokenKind =
  | 'num'
  | 'ident'
  | 'str'
  | 'ws'
  | 'op'
  | 'open'
  | 'close'
  | 'comma'
  | 'semi'
  | 'cur'
  | 'other';

export interface Token {
  kind: TokenKind;
  /** Source text (NFC). */
  text: string;
  /** Offset of the token in the lexed string. */
  pos: number;
  /** Code emitted instead of `text`; set by transformation passes. */
  out?: string;
  /** `out` is an internal name that must not be mangled again. */
  raw?: boolean;
  /** Internal variable name this identifier refers to (set by the identifier pass). */
  ref?: string;
}

export interface LexResult {
  tokens: Token[];
  /** Text of a trailing `#` / `//` comment, without the marker. */
  comment?: string;
}

const MULTI_OPS = ['>>>', '==', '!=', '<=', '>=', '<<', '>>', '.*', './', '.^'];
const SINGLE_OPS = new Set(['+', '-', '*', '/', '^', '%', '!', '=', '<', '>', '&', '|', '~', '?', ':', '.', "'"]);

/** Unicode operators → mathjs operators. */
const UNICODE_OPS: Record<string, string> = {
  '×': '*',
  '·': '*',
  '⋅': '*',
  '∙': '*',
  '÷': '/',
  '∶': '/',
  '−': '-',
  '–': '-',
  '≤': '<=',
  '≥': '>=',
  '≠': '!=',
};

const SUPERSCRIPTS: Record<string, string> = {
  '⁰': '0',
  '¹': '1',
  '²': '2',
  '³': '3',
  '⁴': '4',
  '⁵': '5',
  '⁶': '6',
  '⁷': '7',
  '⁸': '8',
  '⁹': '9',
  '⁻': '-',
};

const NUMBER_RE = /^(?:0[xX][0-9a-fA-F]+|0[bB][01]+|0[oO][0-7]+|(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)/;

/** Token kinds after which an operand has just ended (so `'` is a transpose, `-` is binary). */
export function endsOperand(tok: Token | undefined): boolean {
  if (!tok) return false;
  if (tok.kind === 'num' || tok.kind === 'str' || tok.kind === 'close' || tok.kind === 'cur') return true;
  if (tok.kind === 'ident') return !OPERATOR_WORDS.has(tok.text);
  if (tok.kind === 'op') return tok.text === '!' || tok.text === "'" || (tok.out?.startsWith('^(') ?? false);
  return false;
}

/** Identifiers that act as operators in mathjs (never mangled, never operands). */
export const OPERATOR_WORDS = new Set(['to', 'in', 'and', 'or', 'xor', 'not', 'mod']);

function lastSignificant(tokens: Token[]): Token | undefined {
  for (let i = tokens.length - 1; i >= 0; i--) {
    if (tokens[i]!.kind !== 'ws') return tokens[i];
  }
  return undefined;
}

function readString(src: string, start: number): number {
  const quote = src[start];
  let i = start + 1;
  while (i < src.length) {
    const ch = src[i];
    if (ch === '\\') {
      i += 2;
      continue;
    }
    if (ch === quote) return i + 1;
    i++;
  }
  return src.length;
}

/**
 * Split an expression into tokens. Never throws: unknown characters become `other` tokens
 * (mathjs reports them later) and unterminated strings run to the end.
 */
export function lex(input: string): LexResult {
  const src = input.normalize('NFC');
  const tokens: Token[] = [];
  let i = 0;
  const push = (kind: TokenKind, text: string, pos: number, out?: string, raw?: boolean) => {
    const tok: Token = { kind, text, pos };
    if (out !== undefined) tok.out = out;
    if (raw) tok.raw = true;
    tokens.push(tok);
  };
  while (i < src.length) {
    const ch = src[i]!;
    const rest = src.slice(i);
    // Comments.
    if (ch === '#' || rest.startsWith('//')) {
      const comment = src.slice(i + (ch === '#' ? 1 : 2)).trim();
      return { tokens, comment };
    }
    if (/\s/.test(ch)) {
      let j = i + 1;
      while (j < src.length && /\s/.test(src[j]!)) j++;
      push('ws', src.slice(i, j), i);
      i = j;
      continue;
    }
    if (ch === '"' || (ch === "'" && !endsOperand(lastSignificant(tokens)))) {
      const end = readString(src, i);
      push('str', src.slice(i, end), i);
      i = end;
      continue;
    }
    const num = NUMBER_RE.exec(rest);
    if (num && !(ch === '.' && lastSignificant(tokens)?.kind === 'ident')) {
      push('num', num[0], i);
      i += num[0].length;
      continue;
    }
    if (IDENT_START_RE.test(ch)) {
      let j = i + 1;
      while (j < src.length && IDENT_PART_RE.test(src[j]!)) j++;
      const word = src.slice(i, j);
      if (word === 'π') push('ident', word, i, 'pi', true);
      else push('ident', word, i);
      i = j;
      continue;
    }
    if (ch in SUPERSCRIPTS) {
      let j = i;
      let digits = '';
      while (j < src.length && src[j]! in SUPERSCRIPTS) {
        digits += SUPERSCRIPTS[src[j]!];
        j++;
      }
      push('op', src.slice(i, j), i, `^(${digits})`);
      i = j;
      continue;
    }
    if (ch === '°') {
      const unit = /^°([CF])(?![\p{L}\p{Nd}_])/u.exec(rest);
      const text = unit ? unit[0] : ch;
      push('ident', text, i, unit ? ` deg${unit[1]}` : ' deg', true);
      i += text.length;
      continue;
    }
    if (ch === '∞') {
      push('ident', ch, i, 'Infinity', true);
      i++;
      continue;
    }
    if (CURRENCY_SYMBOL_CHARS.has(ch)) {
      push('cur', ch, i);
      i++;
      continue;
    }
    if (ch === '(' || ch === '[' || ch === '{') {
      push('open', ch, i);
      i++;
      continue;
    }
    if (ch === ')' || ch === ']' || ch === '}') {
      push('close', ch, i);
      i++;
      continue;
    }
    if (ch === ',') {
      push('comma', ch, i);
      i++;
      continue;
    }
    if (ch === ';') {
      push('semi', ch, i);
      i++;
      continue;
    }
    if (ch in UNICODE_OPS) {
      push('op', ch, i, UNICODE_OPS[ch]);
      i++;
      continue;
    }
    if (ch === '√') {
      push('op', ch, i);
      i++;
      continue;
    }
    const multi = MULTI_OPS.find((op) => rest.startsWith(op));
    if (multi) {
      push('op', multi, i);
      i += multi.length;
      continue;
    }
    if (SINGLE_OPS.has(ch)) {
      push('op', ch, i);
      i++;
      continue;
    }
    // Any other code point (keep surrogate pairs together).
    const cp = String.fromCodePoint(src.codePointAt(i)!);
    push('other', cp, i);
    i += cp.length;
  }
  return { tokens };
}

/** The code a token contributes to the emitted expression. */
export function tokenCode(tok: Token): string {
  if (tok.out !== undefined) return tok.out;
  if (tok.kind === 'ws') return ' ';
  return tok.text;
}

/** Concatenate the code of tokens. */
export function emitTokens(tokens: readonly Token[]): string {
  let out = '';
  for (const t of tokens) out += tokenCode(t);
  return out;
}

/** Index of the next non-whitespace token at or after `from`, or -1. */
export function nextSignificant(tokens: readonly Token[], from: number): number {
  for (let i = from; i < tokens.length; i++) if (tokens[i]!.kind !== 'ws') return i;
  return -1;
}

/** Index of the previous non-whitespace token at or before `from`, or -1. */
export function prevSignificant(tokens: readonly Token[], from: number): number {
  for (let i = from; i >= 0; i--) if (tokens[i]!.kind !== 'ws') return i;
  return -1;
}

/** Tokens without whitespace. */
export function significant(tokens: readonly Token[]): Token[] {
  return tokens.filter((t) => t.kind !== 'ws');
}

/** For each `open` token index, the index of its matching `close` (or -1 when unbalanced). */
export function matchBrackets(tokens: readonly Token[]): Map<number, number> {
  const pairs = new Map<number, number>();
  const stack: number[] = [];
  const closing: Record<string, string> = { ')': '(', ']': '[', '}': '{' };
  tokens.forEach((t, i) => {
    if (t.kind === 'open') stack.push(i);
    else if (t.kind === 'close') {
      const top = stack[stack.length - 1];
      if (top !== undefined && tokens[top]!.text === closing[t.text]) {
        stack.pop();
        pairs.set(top, i);
      }
    }
  });
  for (const open of stack) pairs.set(open, -1);
  return pairs;
}
