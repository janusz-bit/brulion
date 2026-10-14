/**
 * Fenced code blocks: ```` ```calc ```` and ```` ```query ```` are special (./calc.ts,
 * ./query.ts); everything else renders as `<pre><code class="language-x">` with the code
 * escaped (no syntax highlighting, no execution).
 */
import type { MarkdownIt, RendererRule, Token } from 'markdown-it';
import { escapeHtml, renderAttrPairs } from '../escape';
import { getEnv } from '../env';
import { renderCalcBlock } from './calc';
import { renderQueryBlock } from './query';

/** Language of a fence: the first word of its info string, lower-cased and made class-safe. */
export function fenceLanguage(info: string): string {
  const first = info.trim().split(/\s+/)[0] ?? '';
  // `{lang}` / `{.lang}` (pandoc style) → lang
  return first.replace(/^\{\.?|\}$/g, '').toLowerCase().replace(/[^a-z0-9_+#.-]/g, '');
}

/** Attributes of the wrapper (data-line, block id) serialised. */
function wrapperAttrs(token: Token): string {
  return renderAttrPairs(token.attrs);
}

/** Install the fence renderer. */
export function fencePlugin(md: MarkdownIt): void {
  const fence: RendererRule = (tokens, idx, _options, mdEnv) => {
    const token = tokens[idx]!;
    const lang = fenceLanguage(token.info);
    const env = getEnv(mdEnv);
    const attrs = wrapperAttrs(token);
    if (env && lang === 'calc') return renderCalcBlock(env, token.content, token.map?.[0] ?? 0, attrs);
    if (env && lang === 'query') return renderQueryBlock(env, token.content, attrs);
    const cls = lang ? ` class="language-${escapeHtml(lang)}"` : '';
    return `<pre${attrs}><code${cls}>${escapeHtml(token.content)}</code></pre>\n`;
  };
  md.renderer.rules.fence = fence;
}
