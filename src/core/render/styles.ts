/**
 * Base stylesheet for rendered notes, used by the HTML exporters (standalone pages) and usable
 * by any host. All rules are scoped under {@link RENDER_ROOT_CLASS} (wrap the rendered HTML in
 * `<article class="brulion-doc">`); `body.brulion-page` styles a standalone page. Light and dark
 * themes follow `prefers-color-scheme`; no external assets (icons are emoji / text).
 */

/** Class of the element wrapping rendered HTML that `RENDER_CSS` styles. */
export const RENDER_ROOT_CLASS = 'brulion-doc';

const LIGHT = `
  --br-fg:#1f2328;--br-muted:#59636e;--br-bg:#ffffff;--br-bg-soft:#f6f8fa;--br-border:#d1d9e0;
  --br-accent:#0969da;--br-accent-rgb:9,105,218;--br-unresolved:#bc4c00;--br-mark:rgba(255,212,0,.42);
  --br-code-bg:rgba(129,139,152,.14);--br-danger:#d1242f;--br-success:#1a7f37;--br-warning:#9a6700;`;

const DARK = `
  --br-fg:#e6edf3;--br-muted:#9198a1;--br-bg:#0d1117;--br-bg-soft:#151b23;--br-border:#3d444d;
  --br-accent:#4493f8;--br-accent-rgb:68,147,248;--br-unresolved:#f0883e;--br-mark:rgba(187,128,9,.5);
  --br-code-bg:rgba(101,108,118,.28);--br-danger:#f85149;--br-success:#3fb950;--br-warning:#d29922;`;

const D = `.${RENDER_ROOT_CLASS}`;

/** Callout accent colours (RGB triplets) by canonical type. */
const CALLOUT_COLORS: Readonly<Record<string, string>> = {
  note: '8,109,221',
  abstract: '0,176,255',
  info: '8,109,221',
  todo: '8,109,221',
  tip: '0,191,165',
  success: '8,185,78',
  question: '236,117,0',
  warning: '236,117,0',
  failure: '233,49,71',
  danger: '233,49,71',
  bug: '233,49,71',
  example: '120,82,238',
  quote: '158,158,158',
};

const calloutColors = Object.entries(CALLOUT_COLORS)
  .map(([type, rgb]) => `${D} .callout-${type}{--callout-color:${rgb}}`)
  .join('\n');

/** The stylesheet (plain CSS text). */
export const RENDER_CSS = `
:root{color-scheme:light dark;${LIGHT}}
@media (prefers-color-scheme:dark){:root{${DARK}}}
body.brulion-page{margin:0;background:var(--br-bg);color:var(--br-fg)}
${D}{font:16px/1.6 system-ui,-apple-system,"Segoe UI",Roboto,"Noto Sans",Ubuntu,Cantarell,sans-serif;color:var(--br-fg);max-width:48rem;margin:0 auto;padding:2rem 1rem;overflow-wrap:break-word}
${D} h1,${D} h2,${D} h3,${D} h4,${D} h5,${D} h6{line-height:1.25;margin:1.5em 0 .5em;font-weight:600}
${D} h1{font-size:1.9em}${D} h2{font-size:1.5em;padding-bottom:.2em;border-bottom:1px solid var(--br-border)}
${D} h3{font-size:1.25em}${D} h4{font-size:1.1em}${D} h5,${D} h6{font-size:1em;color:var(--br-muted)}
${D} p,${D} ul,${D} ol,${D} dl,${D} pre,${D} table,${D} blockquote,${D} figure{margin:0 0 1em}
${D} ul,${D} ol{padding-left:1.6em}${D} li>ul,${D} li>ol{margin-bottom:0}
${D} a{color:var(--br-accent);text-decoration:none}${D} a:hover{text-decoration:underline}
${D} a.wikilink.is-unresolved{color:var(--br-unresolved);text-decoration:underline dotted}
${D} a.external-link[target="_blank"]::after{content:"\\2197";font-size:.75em;margin-left:.1em}
${D} a.tag{display:inline-block;background:rgba(var(--br-accent-rgb),.1);color:var(--br-accent);border-radius:999px;padding:0 .5em;font-size:.875em;line-height:1.5;white-space:nowrap}
${D} code{font:.875em/1.4 ui-monospace,SFMono-Regular,"SF Mono",Menlo,Consolas,"Liberation Mono",monospace;background:var(--br-code-bg);padding:.15em .35em;border-radius:4px}
${D} pre{background:var(--br-bg-soft);border:1px solid var(--br-border);border-radius:6px;padding:.75em 1em;overflow:auto;line-height:1.45}
${D} pre code{background:none;padding:0;font-size:.85em}
${D} blockquote{margin-left:0;padding:0 1em;border-left:.25em solid var(--br-border);color:var(--br-muted)}
${D} mark{background:var(--br-mark);color:inherit;padding:0 .1em;border-radius:2px}
${D} hr{border:0;border-top:1px solid var(--br-border);margin:1.5em 0}
${D} img,${D} video{max-width:100%;height:auto;border-radius:4px}
${D} audio{width:100%;max-width:28rem;vertical-align:middle}
${D} table{border-collapse:collapse;display:block;width:max-content;max-width:100%;overflow-x:auto}
${D} th,${D} td{border:1px solid var(--br-border);padding:.35em .75em;vertical-align:top}
${D} th{background:var(--br-bg-soft);font-weight:600}
${D} td.formula{font-variant-numeric:tabular-nums;background:rgba(var(--br-accent-rgb),.06)}
${D} td.formula-error{color:var(--br-danger)}
${D} table.properties{font-size:.875em;border:0;margin-bottom:1.25em;color:var(--br-muted)}
${D} table.properties th{background:none;border:0;padding:.1em 1.25em .1em 0;font-weight:500;text-align:left;white-space:nowrap}
${D} table.properties td{border:0;padding:.1em 0;color:var(--br-fg)}
${D} .prop-sep{color:var(--br-muted)}
${D} .block-props{font-size:.875em;color:var(--br-muted);margin:.15em 0 .5em}
${D} .block-prop{display:block}${D} .prop-key{font-weight:600;margin-right:.25em}${D} .prop-key::after{content:"::"}
${D} .prop-value{color:var(--br-fg)}
${D} ul.contains-task-list,${D} ol.contains-task-list{padding-left:1.4em}
${D} ul.contains-task-list>li.task-list-item{list-style:none}
${D} .task-checkbox{margin:0 .45em 0 -1.4em;vertical-align:-.1em;width:1em;height:1em}
${D} .task-done .task-text,${D} .task-cancelled .task-text{text-decoration:line-through;color:var(--br-muted)}
${D} .task-cancelled .task-text{opacity:.7}${D} .task-doing>.task-checkbox,${D} .task-doing>p>.task-checkbox{accent-color:var(--br-warning)}
${D} .task-keyword{font-size:.72em;font-weight:700;letter-spacing:.03em;padding:.05em .4em;border-radius:4px;border:1px solid var(--br-border);background:var(--br-bg-soft);color:var(--br-muted);vertical-align:.1em}
${D} .task-keyword-doing{color:var(--br-warning)}${D} .task-keyword-cancelled{text-decoration:line-through}
${D} .chip{display:inline-block;font-size:.8em;line-height:1.5;padding:0 .5em;margin:0 .1em;border-radius:999px;border:1px solid var(--br-border);background:var(--br-bg-soft);color:var(--br-muted);white-space:nowrap}
${D} .chip-today{color:var(--br-accent);border-color:currentColor}
${D} .chip-overdue,${D} .chip-alarm{color:var(--br-danger);border-color:currentColor}
${D} .chip-done{color:var(--br-success)}
${D} .priority{display:inline-block;margin:0 .1em}
${D} .priority-logseq{font-size:.75em;font-weight:700;padding:0 .35em;border-radius:4px;background:var(--br-bg-soft);border:1px solid var(--br-border)}
${D} .priority-logseq.priority-high,${D} .priority-logseq.priority-highest{color:var(--br-danger)}
${D} .callout{--callout-color:8,109,221;border-left:4px solid rgb(var(--callout-color));background:rgba(var(--callout-color),.08);border-radius:6px;padding:.6em 1em;margin:0 0 1em}
${calloutColors}
${D} .callout-title{display:flex;gap:.5em;align-items:center;font-weight:600;color:rgb(var(--callout-color))}
${D} details.callout>summary{cursor:pointer;list-style:none}${D} details.callout>summary::-webkit-details-marker{display:none}
${D} details.callout>summary::after{content:"\\25B8";margin-left:auto;transition:transform .15s}
${D} details.callout[open]>summary::after{transform:rotate(90deg)}
${D} .callout-content{margin-top:.4em}${D} .callout-content>:last-child{margin-bottom:0}
${D} .embed{display:block;border:1px solid var(--br-border);border-left:3px solid var(--br-accent);border-radius:6px;padding:.5em 1em;margin:0 0 1em;background:var(--br-bg)}
${D} span.embed{display:inline-block;padding:.1em .6em;margin:0}
${D} .embed-title{font-size:.85em;margin-bottom:.3em}
${D} .embed-content>:last-child{margin-bottom:0}
${D} .embed-missing,${D} .embed-cycle,${D} .embed-too-deep{color:var(--br-muted);font-style:italic;border-style:dashed}
${D} .embed-pdf,${D} .embed-file{display:inline-block;padding:.2em .6em;border:1px solid var(--br-border);border-radius:6px;background:var(--br-bg-soft)}
${D} .blockref{border-bottom:1px solid rgba(var(--br-accent-rgb),.55);background:rgba(var(--br-accent-rgb),.05);cursor:pointer}
${D} .blockref.is-unresolved{color:var(--br-unresolved);font-family:ui-monospace,monospace;font-size:.9em}
${D} .calc{font:.875em/1.5 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;background:var(--br-bg-soft);border:1px solid var(--br-border);border-radius:6px;padding:.5em 1em;margin:0 0 1em;overflow-x:auto}
${D} .calc-line{display:flex;justify-content:space-between;gap:2em;white-space:pre}
${D} .calc-blank{min-height:.75em}${D} .calc-comment .calc-input{color:var(--br-muted)}
${D} .calc-result{color:var(--br-success);font-weight:600;text-align:right}
${D} .calc-line.calc-error .calc-result{color:var(--br-danger);font-weight:400;white-space:normal}
${D} .calc-inline{font-weight:600;color:var(--br-success);border-bottom:1px dotted currentColor}
${D} code.calc-error{color:var(--br-danger);border-bottom:1px dotted currentColor}
${D} .query,${D} .query-placeholder{border:1px dashed var(--br-border);border-radius:6px;padding:.5em 1em;margin:0 0 1em}
${D} .query-title{font-size:.85em;color:var(--br-muted);margin-bottom:.3em}
${D} .query-placeholder pre{margin:0}
${D} .footnotes{font-size:.875em;color:var(--br-muted);margin-top:2em}${D} .footnotes-sep{margin:1em 0}
${D} .footnote-ref a{font-size:.8em}${D} .footnote-backref{margin-left:.25em}
${D} :target{scroll-margin-top:1rem;background:var(--br-mark);border-radius:3px}
@media print{${D}{max-width:none;padding:0}${D} a.external-link[target="_blank"]::after{content:""}${D} .callout,${D} pre{break-inside:avoid}}
`.trim();
