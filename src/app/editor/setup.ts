/**
 * The editor's extension list. `editorExtensions(opts)` builds everything a note editor needs;
 * parts that change at runtime live in compartments (`reconfigureEditor`).
 */
import { closeBrackets, closeBracketsKeymap } from '@codemirror/autocomplete';
import { history } from '@codemirror/commands';
import { markdown, markdownLanguage } from '@codemirror/lang-markdown';
import { codeFolding, indentUnit } from '@codemirror/language';
import { languages } from '@codemirror/language-data';
import { search } from '@codemirror/search';
import { Compartment, EditorState, Prec, type Extension } from '@codemirror/state';
import {
  crosshairCursor,
  drawSelection,
  dropCursor,
  EditorView,
  highlightActiveLine,
  highlightSpecialChars,
  keymap,
  placeholder as cmPlaceholder,
  rectangularSelection,
  tooltips,
  ViewPlugin,
} from '@codemirror/view';
import { createTranslator, type Translator } from '@/core/i18n';
import type { Locale } from '@/core/types';
import { brulionAutocomplete } from './autocomplete';
import {
  DEFAULT_EDITOR_SETTINGS,
  editorContext,
  editorDialogs,
  editorSettings,
  type DialogOpener,
  type EditorContext,
  type EditorSettings,
} from './context';
import { brulionHighlighting } from './highlight';
import { editorKeymaps } from './keymap';
import { linkClicks } from './links';
import { bottomOverlay, overlayMargins } from './margins';
import { outlineFolding } from './outliner';
import { pasteHandling } from './paste';
import { livePreview } from './preview';
import { brulionMarkdown } from './syntax';
import { rangeTracker } from './tracker';
import { zoomExtension } from './zoom';

export interface EditorSetupOptions {
  context?: EditorContext | null;
  settings?: Partial<EditorSettings>;
  dialogs?: DialogOpener;
  readOnly?: boolean;
  placeholder?: string;
  /** Journal stream / embeds: auto height, lighter chrome. */
  compact?: boolean;
  /** Accessible name of the editing area. */
  label?: string;
  /** UI language (CodeMirror phrases, `lang`); default: the context's locale. */
  locale?: Locale;
  extra?: Extension[];
}

/** Runtime-reconfigurable parts. One set per module is fine: compartments are per state. */
const compartments = {
  context: new Compartment(),
  readOnly: new Compartment(),
  placeholder: new Compartment(),
  attributes: new Compartment(),
  preview: new Compartment(),
};

/** The Markdown language with the Brulion dialect and lazily loaded code languages. */
export function brulionLanguage(): Extension {
  return markdown({
    base: markdownLanguage,
    codeLanguages: languages,
    extensions: brulionMarkdown,
    addKeymap: false,
    completeHTMLTags: false,
    pasteURLAsLink: false,
  });
}

/** Phrases of CodeMirror's own UI (search panel, folding, …) in the editor's language. */
export const CM_PHRASES = [
  'Find',
  'Replace',
  'next',
  'previous',
  'all',
  'match case',
  'by word',
  'regexp',
  'replace',
  'replace all',
  'close',
  'current match',
  'replaced $ matches',
  'replaced match on line $',
  'on line',
  'Go to line',
  'go',
  'Folded lines',
  'Unfold line',
  'Fold line',
  'unfold',
  'folded code',
  'Control character',
  'Selection deleted',
  'Completions',
] as const;

export function cmPhrases(t: Translator): Record<string, string> {
  return Object.fromEntries(CM_PHRASES.map((p) => [p, t(`editor.cm.${p}`)]));
}

function localeOf(opts: EditorSetupOptions): Locale {
  return opts.locale ?? opts.context?.locale ?? 'pl';
}

function contextExtension(opts: EditorSetupOptions): Extension {
  return [
    localeOf(opts) === 'pl' ? EditorState.phrases.of(cmPhrases(createTranslator('pl'))) : [],
    opts.context ? editorContext.of(opts.context) : [],
    opts.dialogs ? editorDialogs.of(opts.dialogs) : [],
    editorSettings.of(opts.settings ?? {}),
  ];
}

function readOnlyExtension(readOnly: boolean): Extension {
  return [EditorState.readOnly.of(readOnly), EditorView.editable.of(!readOnly)];
}

function attributesExtension(opts: EditorSetupOptions): Extension {
  const s: EditorSettings = { ...DEFAULT_EDITOR_SETTINGS, ...opts.settings };
  const attrs: Record<string, string> = {
    spellcheck: s.spellcheck ? 'true' : 'false',
    autocorrect: s.spellcheck ? 'on' : 'off',
    autocapitalize: 'sentences',
    lang: localeOf(opts),
  };
  if (opts.label) attrs['aria-label'] = opts.label;
  return [
    EditorView.contentAttributes.of(attrs),
    EditorView.editorAttributes.of({ class: opts.compact ? 'b-cm is-compact' : 'b-cm' }),
  ];
}

function previewExtension(opts: EditorSetupOptions): Extension {
  const enabled = opts.settings?.livePreview ?? DEFAULT_EDITOR_SETTINGS.livePreview;
  return enabled ? livePreview() : [];
}

/**
 * Re-measure line heights when the editor's font metrics change after creation (stylesheet
 * arriving late, font-size / font-family settings, fonts loading). CodeMirror only re-measures
 * when the content height changes, which short notes never do (`.cm-content` is pinned to the
 * scroller height), so a stale height map would put clicks on the wrong line. A hidden probe
 * inheriting the content font is watched with a ResizeObserver; a changed size bumps a theme
 * compartment, which makes CodeMirror measure the content again.
 */
const metricsCompartment = new Compartment();

const metricsWatcher = ViewPlugin.fromClass(
  class {
    private readonly probe: HTMLElement;
    private readonly observer: ResizeObserver | null = null;
    private last = '';
    private frame = 0;

    constructor(private readonly view: EditorView) {
      this.probe = document.createElement('div');
      this.probe.className = 'b-cm-probe';
      this.probe.setAttribute('aria-hidden', 'true');
      this.probe.textContent = 'Mg ąę';
      view.scrollDOM.appendChild(this.probe);
      if (typeof ResizeObserver !== 'undefined') {
        this.observer = new ResizeObserver(() => this.check());
        this.observer.observe(this.probe);
      }
    }

    private check(): void {
      const r = this.probe.getBoundingClientRect();
      const key = `${r.width.toFixed(2)}x${r.height.toFixed(2)}`;
      if (key === this.last) return;
      this.last = key;
      cancelAnimationFrame(this.frame);
      this.frame = requestAnimationFrame(() => {
        if (this.view.dom.isConnected) this.view.dispatch({ effects: metricsCompartment.reconfigure(EditorView.theme({})) });
      });
    }

    destroy(): void {
      cancelAnimationFrame(this.frame);
      this.observer?.disconnect();
      this.probe.remove();
    }
  },
);

/**
 * Tooltips (completion, slash menu) stay out of the area covered by the mobile toolbar and the
 * on-screen keyboard: near the bottom they open above the cursor instead of under the toolbar.
 */
const tooltipSpace = tooltips({
  tooltipSpace: (view) => {
    const win = view.dom.ownerDocument.defaultView ?? window;
    return { top: 0, left: 0, right: win.innerWidth, bottom: Math.max(0, win.innerHeight - bottomOverlay(view)) };
  },
});

/** Brackets auto-paired in notes: `[` and `(` (so `[[` → `[[]]`, `((` → `(())`). */
const bracketData = EditorState.languageData.of(() => [{ closeBrackets: { brackets: ['(', '['] } }]);

export function editorExtensions(opts: EditorSetupOptions = {}): Extension[] {
  return [
    compartments.context.of(contextExtension(opts)),
    history(),
    drawSelection(),
    dropCursor(),
    rectangularSelection(),
    crosshairCursor(),
    highlightActiveLine(),
    highlightSpecialChars(),
    EditorState.allowMultipleSelections.of(true),
    EditorView.lineWrapping,
    EditorState.tabSize.of(4),
    indentUnit.of('\t'),
    brulionLanguage(),
    brulionHighlighting(),
    codeFolding({ placeholderText: '…' }),
    outlineFolding,
    bracketData,
    closeBrackets(),
    Prec.high(keymap.of(closeBracketsKeymap)),
    brulionAutocomplete(),
    editorKeymaps(),
    search({ top: true }),
    zoomExtension(),
    rangeTracker,
    pasteHandling(),
    linkClicks,
    metricsCompartment.of([]),
    metricsWatcher,
    overlayMargins,
    tooltipSpace,
    compartments.attributes.of(attributesExtension(opts)),
    compartments.readOnly.of(readOnlyExtension(opts.readOnly ?? false)),
    compartments.placeholder.of(opts.placeholder ? cmPlaceholder(opts.placeholder) : []),
    compartments.preview.of(previewExtension(opts)),
    ...(opts.extra ?? []),
  ];
}

function sameSettings(a?: Partial<EditorSettings>, b?: Partial<EditorSettings>): boolean {
  const ka = Object.keys(a ?? {}) as (keyof EditorSettings)[];
  const kb = Object.keys(b ?? {});
  return ka.length === kb.length && ka.every((k) => a?.[k] === b?.[k]);
}

/**
 * Apply new options to a running editor. Only parts that differ from `prev` are reconfigured
 * (re-creating the live preview needlessly would reset its widgets).
 */
export function reconfigureEditor(view: EditorView, opts: EditorSetupOptions, prev?: EditorSetupOptions): void {
  const effects = [];
  const settingsChanged = !prev || !sameSettings(opts.settings, prev.settings);
  const localeChanged = !prev || localeOf(opts) !== localeOf(prev);
  if (!prev || localeChanged || opts.context !== prev.context || opts.dialogs !== prev.dialogs || settingsChanged) {
    effects.push(compartments.context.reconfigure(contextExtension(opts)));
  }
  if (!prev || !!opts.readOnly !== !!prev.readOnly) effects.push(compartments.readOnly.reconfigure(readOnlyExtension(opts.readOnly ?? false)));
  if (!prev || opts.placeholder !== prev.placeholder) {
    effects.push(compartments.placeholder.reconfigure(opts.placeholder ? cmPlaceholder(opts.placeholder) : []));
  }
  if (!prev || settingsChanged || localeChanged || opts.compact !== prev.compact || opts.label !== prev.label) {
    effects.push(compartments.attributes.reconfigure(attributesExtension(opts)));
  }
  if (!prev || (opts.settings?.livePreview ?? true) !== (prev.settings?.livePreview ?? true)) {
    effects.push(compartments.preview.reconfigure(previewExtension(opts)));
  }
  if (effects.length) view.dispatch({ effects });
}
