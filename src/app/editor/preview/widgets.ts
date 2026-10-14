/**
 * Inline widgets of the live preview. Widgets keep only presentational data (no document
 * positions), so `eq()` holds across edits elsewhere and CodeMirror keeps their DOM. Actions
 * are found by event delegation (`data-lp` attributes, see actions.ts); positions come from
 * `view.posAtDOM(widget root)` at event time.
 *
 * Widgets replacing text render a hidden copy of the source (`.b-lp-src`), so the textContent
 * of a line stays its Markdown (tests and tools read lines; screen readers skip it).
 */
import { WidgetType, type EditorView } from '@codemirror/view';
import type { Priority, TaskStatus } from '@/core/types';
import { h, icon, sourceSpan, type PreviewIcon } from './dom';
import type { CalloutType } from './callouts';
import { CALLOUT_ICON } from './callouts';
import type { ChipModel } from './tokens';
import { PRIORITY_ICON } from './tokens';

/** Elements inside widgets whose events belong to the browser, not the editor. */
const NATIVE = 'audio, video, input, textarea, select, details, summary';

export abstract class PreviewWidget extends WidgetType {
  override ignoreEvent(event: Event): boolean {
    const target = event.target as { closest?: (s: string) => unknown } | null;
    return !!target && typeof target.closest === 'function' && !!target.closest(NATIVE);
  }
}

/**
 * A visible label drawn with CSS (`content: attr(data-lp-label)`), so it adds no text to the
 * line's textContent (which stays the Markdown source); screen readers still read it.
 */
export function label(className: string, text: string): HTMLElement {
  return h('span', { class: className, 'data-lp-label': text });
}

/** Root element of a replacing widget: marks it for position lookups and carries the source. */
function root<K extends keyof HTMLElementTagNameMap>(tag: K, className: string, raw: string, attrs: Record<string, string | null | undefined | boolean> = {}): HTMLElementTagNameMap[K] {
  const el = h(tag, { class: className, 'data-lp-root': '', ...attrs });
  if (raw) el.appendChild(sourceSpan(raw));
  return el;
}

// ---------------------------------------------------------------------------------------------
// Hidden syntax
// ---------------------------------------------------------------------------------------------

/** Hides syntax (`**`, `[[`, `# `, indentation): zero width, source kept invisibly. */
export class HiddenWidget extends PreviewWidget {
  constructor(readonly raw: string) {
    super();
  }
  override eq(other: HiddenWidget): boolean {
    return other.raw === this.raw;
  }
  toDOM(): HTMLElement {
    return root('span', 'b-lp-hide', this.raw);
  }
}

// ---------------------------------------------------------------------------------------------
// Outline: bullets, numbers, checkboxes, fold toggles
// ---------------------------------------------------------------------------------------------

export class BulletWidget extends PreviewWidget {
  constructor(
    readonly raw: string,
    readonly depth: number,
    readonly hasChildren: boolean,
    readonly folded: boolean,
    readonly labels: { zoom: string; fold: string },
  ) {
    super();
  }
  override eq(o: BulletWidget): boolean {
    return o.raw === this.raw && o.depth === this.depth && o.hasChildren === this.hasChildren && o.folded === this.folded && o.labels.zoom === this.labels.zoom;
  }
  toDOM(): HTMLElement {
    const el = root('span', `b-lp-bullet${this.folded ? ' is-folded' : ''}`, this.raw, {
      'data-lp': 'bullet',
      'data-depth': String(Math.min(this.depth, 3)),
      title: this.labels.zoom,
    });
    if (this.hasChildren) {
      el.appendChild(
        h('span', { class: `b-lp-fold${this.folded ? ' is-folded' : ''}`, 'data-lp': 'fold', title: this.labels.fold, 'aria-hidden': 'true' }, icon('chevronRight', 'b-lp-icon b-lp-fold__icon')),
      );
    }
    el.appendChild(h('span', { class: 'b-lp-bullet__dot', 'aria-hidden': 'true' }));
    return el;
  }
}

export class OrderedWidget extends PreviewWidget {
  constructor(
    readonly raw: string,
    readonly label: string,
    readonly hasChildren: boolean,
    readonly folded: boolean,
    readonly foldLabel: string,
  ) {
    super();
  }
  override eq(o: OrderedWidget): boolean {
    return o.raw === this.raw && o.label === this.label && o.hasChildren === this.hasChildren && o.folded === this.folded;
  }
  toDOM(): HTMLElement {
    const el = root('span', `b-lp-number${this.folded ? ' is-folded' : ''}`, this.raw);
    if (this.hasChildren) {
      el.appendChild(
        h('span', { class: `b-lp-fold${this.folded ? ' is-folded' : ''}`, 'data-lp': 'fold', title: this.foldLabel, 'aria-hidden': 'true' }, icon('chevronRight', 'b-lp-icon b-lp-fold__icon')),
      );
    }
    el.appendChild(label('b-lp-number__label', this.label));
    return el;
  }
}

export class CheckboxWidget extends PreviewWidget {
  constructor(
    readonly raw: string,
    readonly status: TaskStatus,
    /** Logseq keyword shown next to the box (DOING, LATER, …), '' for none. */
    readonly keyword: string,
    readonly label: string,
  ) {
    super();
  }
  override eq(o: CheckboxWidget): boolean {
    return o.raw === this.raw && o.status === this.status && o.keyword === this.keyword && o.label === this.label;
  }
  toDOM(): HTMLElement {
    const el = root('span', `b-lp-task b-lp-task--${this.status}`, this.raw);
    const box = h(
      'span',
      {
        class: 'b-lp-checkbox',
        role: 'checkbox',
        'aria-checked': this.status === 'done' ? 'true' : this.status === 'doing' ? 'mixed' : 'false',
        'aria-label': this.label,
        title: this.label,
        'data-lp': 'checkbox',
      },
      this.status === 'done' ? icon('check', 'b-lp-icon b-lp-checkbox__mark') : null,
      this.status === 'cancelled' ? icon('minus', 'b-lp-icon b-lp-checkbox__mark') : null,
      this.status === 'deferred' ? icon('chevronRight', 'b-lp-icon b-lp-checkbox__mark') : null,
    );
    el.appendChild(box);
    if (this.keyword) {
      const kw = label('b-lp-task__keyword', this.keyword);
      kw.setAttribute('data-lp', 'checkbox-menu');
      el.appendChild(kw);
    }
    return el;
  }
}

/** Fold toggle in front of a heading (headings have no bullet to carry it). */
export class HeadingFoldWidget extends PreviewWidget {
  constructor(
    readonly folded: boolean,
    readonly label: string,
  ) {
    super();
  }
  override eq(o: HeadingFoldWidget): boolean {
    return o.folded === this.folded && o.label === this.label;
  }
  toDOM(): HTMLElement {
    const el = h('span', { class: `b-lp-hfold${this.folded ? ' is-folded' : ''}`, 'data-lp-root': '', 'aria-hidden': 'true' });
    el.appendChild(h('span', { class: `b-lp-fold${this.folded ? ' is-folded' : ''}`, 'data-lp': 'fold', title: this.label }, icon('chevronRight', 'b-lp-icon b-lp-fold__icon')));
    return el;
  }
}

// ---------------------------------------------------------------------------------------------
// Chips: task tokens, priorities, inline calculations
// ---------------------------------------------------------------------------------------------

export class ChipWidget extends PreviewWidget {
  constructor(
    readonly raw: string,
    readonly chip: ChipModel,
  ) {
    super();
  }
  override eq(o: ChipWidget): boolean {
    const a = o.chip;
    const b = this.chip;
    return o.raw === this.raw && a.text === b.text && a.tone === b.tone && a.title === b.title && a.flags.join() === b.flags.join();
  }
  toDOM(): HTMLElement {
    const c = this.chip;
    const el = root('span', ['b-lp-chip', `b-lp-chip--${c.kind}`, `is-${c.tone}`, ...c.flags].join(' '), this.raw, {
      'data-lp': 'chip',
      'data-kind': c.kind,
      title: c.title,
    });
    el.append(icon(c.icon, 'b-lp-icon b-lp-chip__icon'), label('b-lp-chip__text', c.text));
    return el;
  }
}

export class PriorityWidget extends PreviewWidget {
  constructor(
    readonly raw: string,
    readonly priority: Priority,
    readonly label: string,
  ) {
    super();
  }
  override eq(o: PriorityWidget): boolean {
    return o.raw === this.raw && o.priority === this.priority && o.label === this.label;
  }
  toDOM(): HTMLElement {
    const el = root('span', `b-lp-priority b-lp-priority--${this.priority}`, this.raw, { 'data-lp': 'priority', title: this.label, role: 'img', 'aria-label': this.label });
    el.appendChild(icon(PRIORITY_ICON[this.priority], 'b-lp-icon'));
    return el;
  }
}

/** Result of `` `= expr` `` replacing the code (formula in the tooltip). */
export class InlineCalcWidget extends PreviewWidget {
  constructor(
    readonly raw: string,
    readonly value: string | null,
    readonly error: string | null,
  ) {
    super();
  }
  override eq(o: InlineCalcWidget): boolean {
    return o.raw === this.raw && o.value === this.value && o.error === this.error;
  }
  toDOM(): HTMLElement {
    const formula = this.raw.replace(/^`+|`+$/g, '').trim();
    const el = root('span', `b-lp-calc${this.error ? ' is-error' : ''}`, this.raw, {
      'data-lp': 'calc',
      title: this.error ? `${formula} — ${this.error}` : formula,
    });
    el.appendChild(label('b-lp-calc__value', this.error ? '#ERR' : (this.value ?? '…')));
    return el;
  }
}

/** Ghost result after an inline calculation being edited. */
export class CalcHintWidget extends PreviewWidget {
  constructor(
    readonly value: string | null,
    readonly error: string | null,
  ) {
    super();
  }
  override eq(o: CalcHintWidget): boolean {
    return o.value === this.value && o.error === this.error;
  }
  toDOM(): HTMLElement {
    const el = label(`b-lp-calc-hint${this.error ? ' is-error' : ''}`, this.error ? ' ⚠' : ` = ${this.value ?? ''}`);
    el.setAttribute('aria-hidden', 'true');
    if (this.error) el.title = this.error;
    return el;
  }
}

/** Per-line result in a ```calc block (right-aligned). */
export class CalcResultWidget extends PreviewWidget {
  constructor(
    readonly value: string | null,
    readonly error: string | null,
    readonly assigned: boolean,
  ) {
    super();
  }
  override eq(o: CalcResultWidget): boolean {
    return o.value === this.value && o.error === this.error && o.assigned === this.assigned;
  }
  toDOM(): HTMLElement {
    const cls = `b-lp-calcres${this.error ? ' is-error' : ''}${this.assigned ? ' is-assigned' : ''}`;
    if (this.error) return h('span', { class: cls, title: this.error, 'aria-label': this.error }, icon('warning', 'b-lp-icon'));
    return label(cls, this.value ?? '');
  }
}

// ---------------------------------------------------------------------------------------------
// Links and references
// ---------------------------------------------------------------------------------------------

/** A wiki link whose label differs from its source (page title, heading, block). */
export class LinkWidget extends PreviewWidget {
  constructor(
    readonly raw: string,
    readonly text: string,
    readonly target: string,
    readonly missing: boolean,
    readonly title: string,
  ) {
    super();
  }
  override eq(o: LinkWidget): boolean {
    return o.raw === this.raw && o.text === this.text && o.missing === this.missing && o.title === this.title;
  }
  toDOM(): HTMLElement {
    const el = root('span', `b-lp-link b-lp-wikilink${this.missing ? ' is-missing' : ''}`, this.raw, {
      'data-lp': 'link',
      'data-target': this.target,
      title: this.title,
    });
    el.appendChild(label('b-lp-link__text', this.text));
    return el;
  }
}

export class BlockRefWidget extends PreviewWidget {
  constructor(
    readonly raw: string,
    readonly id: string,
    readonly text: string | null,
    readonly title: string,
  ) {
    super();
  }
  override eq(o: BlockRefWidget): boolean {
    return o.raw === this.raw && o.text === this.text && o.title === this.title;
  }
  toDOM(): HTMLElement {
    const el = root('span', `b-lp-blockref${this.text === null ? ' is-missing' : ''}`, this.raw, {
      'data-lp': 'blockref',
      'data-id': this.id,
      title: this.title,
    });
    el.appendChild(label('b-lp-blockref__text', this.text ?? `((${this.id}))`));
    return el;
  }
}

// ---------------------------------------------------------------------------------------------
// Lines: rules, properties, callouts, fences
// ---------------------------------------------------------------------------------------------

export class HrWidget extends PreviewWidget {
  constructor(readonly raw: string) {
    super();
  }
  override eq(o: HrWidget): boolean {
    return o.raw === this.raw;
  }
  toDOM(): HTMLElement {
    const el = root('span', 'b-lp-hr', this.raw);
    el.setAttribute('role', 'separator');
    return el;
  }
}

export class PropertyKeyWidget extends PreviewWidget {
  constructor(
    readonly raw: string,
    readonly key: string,
  ) {
    super();
  }
  override eq(o: PropertyKeyWidget): boolean {
    return o.raw === this.raw;
  }
  toDOM(): HTMLElement {
    const el = root('span', 'b-lp-propkey', this.raw, { title: `${this.key}::` });
    el.appendChild(label('b-lp-propkey__text', this.key));
    return el;
  }
}

export class CalloutHeadWidget extends PreviewWidget {
  constructor(
    readonly raw: string,
    readonly type: CalloutType,
    /** Title shown when the callout has none. */
    readonly defaultTitle: string,
    readonly foldable: boolean,
    readonly collapsed: boolean,
    readonly foldLabel: string,
  ) {
    super();
  }
  override eq(o: CalloutHeadWidget): boolean {
    return (
      o.raw === this.raw &&
      o.type === this.type &&
      o.defaultTitle === this.defaultTitle &&
      o.foldable === this.foldable &&
      o.collapsed === this.collapsed &&
      o.foldLabel === this.foldLabel
    );
  }
  toDOM(): HTMLElement {
    const el = root('span', `b-lp-callout-head${this.collapsed ? ' is-collapsed' : ''}`, this.raw, this.foldable ? { 'data-lp': 'callout-fold', title: this.foldLabel } : {});
    el.appendChild(icon(CALLOUT_ICON[this.type], 'b-lp-icon b-lp-callout-head__icon'));
    if (this.defaultTitle) el.appendChild(label('b-lp-callout-head__title', this.defaultTitle));
    if (this.foldable) el.appendChild(icon(this.collapsed ? 'chevronRight' : 'chevronDown', 'b-lp-icon b-lp-callout-head__fold'));
    return el;
  }
}

/** Fold chevron at the end of a titled callout header (the title is real text before it). */
export class CalloutFoldWidget extends PreviewWidget {
  constructor(
    readonly collapsed: boolean,
    readonly label: string,
  ) {
    super();
  }
  override eq(o: CalloutFoldWidget): boolean {
    return o.collapsed === this.collapsed && o.label === this.label;
  }
  toDOM(): HTMLElement {
    const el = h('span', { class: 'b-lp-callout-fold', 'data-lp-root': '', 'data-lp': 'callout-fold', title: this.label, 'aria-hidden': 'true' });
    el.appendChild(icon(this.collapsed ? 'chevronRight' : 'chevronDown', 'b-lp-icon'));
    return el;
  }
}

/** Opening fence of a code block shown as a small language label. */
export class FenceLabelWidget extends PreviewWidget {
  constructor(
    readonly raw: string,
    readonly lang: string,
    readonly iconName: PreviewIcon | null,
  ) {
    super();
  }
  override eq(o: FenceLabelWidget): boolean {
    return o.raw === this.raw && o.lang === this.lang;
  }
  toDOM(): HTMLElement {
    const el = root('span', 'b-lp-fence-label', this.raw);
    if (this.iconName) el.appendChild(icon(this.iconName, 'b-lp-icon'));
    if (this.lang) el.appendChild(label('b-lp-fence-label__text', this.lang));
    return el;
  }
}

/** Measure again once something inside a widget changed size (image loaded, details opened). */
export function remeasure(view: EditorView | null | undefined): void {
  if (view && view.dom.isConnected) view.requestMeasure();
}
