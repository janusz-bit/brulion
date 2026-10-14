/**
 * Calculations (SYNTAX §9):
 *
 * * ```` ```calc ```` blocks render as `<div class="calc">` listing each input line with its
 *   result (`evaluateCalc` with the page scope up to the block: earlier calc blocks and numeric
 *   properties);
 * * inline code starting with `=` (`` `= cena * 2` ``) renders as
 *   `<span class="calc-inline" title="= cena * 2">25</span>`; failing expressions stay visible
 *   as code with the class `calc-error` and the message as tooltip.
 *
 * Table formulas live in ./tables.ts. Evaluation never uses `eval` (mathjs sandbox in calc).
 */
import type { MarkdownIt, RendererRule, StateCore } from 'markdown-it';
import { evaluateCalc, evaluateInline, pageScope, type CalcLineResult } from '@/core/calc';
import { classNames, escapeHtml, renderAttrs } from '../escape';
import { dataLine, getEnv, tr } from '../env';
import type { RenderEnv } from '../types';

/** Calc scope (variables) in effect at source line `line` (memoised per render). */
export function calcScope(env: RenderEnv, line: number): Record<string, unknown> {
  const cached = env.scopes.get(line);
  if (cached) return cached;
  let scope: Record<string, unknown> = {};
  try {
    scope = pageScope(env.source, line, env.locale);
  } catch {
    // Never let a broken page scope break rendering.
  }
  env.scopes.set(line, scope);
  return scope;
}

function lineKind(l: CalcLineResult): string {
  const text = l.input.trim();
  if (!text) return 'calc-blank';
  if (text.startsWith('#') || text.startsWith('//')) return 'calc-comment';
  if (l.error) return 'calc-error';
  return l.assigned ? 'calc-assign' : '';
}

/**
 * HTML of a calc block. `source` is the block content, `line` the 0-based line of the opening
 * fence; `attrs` are extra serialised attributes for the wrapper.
 */
export function renderCalcBlock(env: RenderEnv, source: string, line: number, attrs = ''): string {
  const src = source.replace(/\r?\n$/, '');
  let lines: CalcLineResult[];
  try {
    lines = evaluateCalc(src, { scope: calcScope(env, line), locale: env.locale }).lines;
  } catch {
    const error = tr(env, 'render.calc.error');
    lines = src.split(/\r\n|\r|\n/).map((input, i) => ({ line: i, input, error }));
  }
  let rows = '';
  for (const l of lines) {
    const kind = lineKind(l);
    let result = '';
    if (l.error) result = `<span${renderAttrs({ class: 'calc-result calc-error', title: l.error })}>${escapeHtml(l.error)}</span>`;
    else if (l.value !== undefined) result = `<span class="calc-result">${escapeHtml(l.value)}</span>`;
    rows += `<div${renderAttrs({ class: classNames('calc-line', kind), 'data-line': dataLine(env, line + 1 + l.line) })}><span class="calc-input">${escapeHtml(l.input)}</span>${result}</div>\n`;
  }
  return `<div class="calc"${attrs}>\n${rows}</div>\n`;
}

/** Is the inline code `code` an inline calculation (`= expr`, not `==`)? */
export function isInlineCalc(code: string): boolean {
  const t = code.trim();
  return t.length > 1 && t.charCodeAt(0) === 0x3d && t.charCodeAt(1) !== 0x3d;
}

/** HTML of an inline calculation at source line `line`, or null when it is not one. */
export function renderInlineCalc(env: RenderEnv, code: string, line: number): string | null {
  if (!isInlineCalc(code)) return null;
  let res: { value?: string; error?: string };
  try {
    res = evaluateInline(code, calcScope(env, line), env.locale);
  } catch {
    res = { error: tr(env, 'render.calc.error') };
  }
  const expr = code.trim();
  if (res.value !== undefined) return `<span${renderAttrs({ class: 'calc-inline', title: expr })}>${escapeHtml(res.value)}</span>`;
  if (res.error !== undefined) return `<code${renderAttrs({ class: 'calc-inline calc-error', title: res.error })}>${escapeHtml(expr)}</code>`;
  return null;
}

/** The `calc_lines` core rule: remember the source line of inline code (for the calc scope). */
export function calcLinesRule(state: StateCore): void {
  let line = 0;
  for (const tok of state.tokens) {
    if (tok.map && tok.block) line = tok.map[0];
    if (tok.type !== 'inline' || !tok.children) continue;
    const at = tok.map ? tok.map[0] : line;
    for (const child of tok.children) {
      if (child.type === 'code_inline' && isInlineCalc(child.content)) child.meta = { ...(child.meta ?? {}), calcLine: at };
    }
  }
}

/** Install calc blocks (via the fence renderer) and inline calculations. */
export function calcPlugin(md: MarkdownIt): void {
  md.core.ruler.before('linkify', 'calc_lines', calcLinesRule);
  const defaultCode = md.renderer.rules.code_inline;
  const codeInline: RendererRule = (tokens, idx, options, mdEnv, self) => {
    const token = tokens[idx]!;
    const env = getEnv(mdEnv);
    const line = token.meta?.calcLine;
    if (env && typeof line === 'number') {
      const html = renderInlineCalc(env, token.content, line);
      if (html !== null) return html;
    }
    return defaultCode ? defaultCode(tokens, idx, options, mdEnv, self) : `<code>${escapeHtml(token.content)}</code>`;
  };
  md.renderer.rules.code_inline = codeInline;
}
