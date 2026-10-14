/**
 * Shared live-preview state: editor focus (an unfocused editor renders everything), the vault
 * version (bumped when other notes change, so block refs / embeds / queries refresh) and a few
 * accessors for the editor context with fallbacks for context-less editors (tests).
 */
import { StateEffect, StateField, type EditorState, type Extension } from '@codemirror/state';
import { EditorView, ViewPlugin, type ViewUpdate } from '@codemirror/view';
import { todayLocal } from '@/core/dates';
import { createTranslator, type Translator } from '@/core/i18n';
import type { LocalDate, Locale } from '@/core/types';
import { editorContext, type EditorContext } from '../context';

// ---------------------------------------------------------------------------------------------
// Focus
// ---------------------------------------------------------------------------------------------

export const setPreviewFocus = StateEffect.define<boolean>();

/** Whether the editor has focus (markers are only revealed in a focused editor). */
export const previewFocus = StateField.define<boolean>({
  create: () => false,
  update(value, tr) {
    for (const e of tr.effects) if (e.is(setPreviewFocus)) value = e.value;
    return value;
  },
});

const focusEffect = EditorView.focusChangeEffect.of((_state, focusing) => setPreviewFocus.of(focusing));

// ---------------------------------------------------------------------------------------------
// Vault version
// ---------------------------------------------------------------------------------------------

/** Dispatched (debounced) when files of the vault changed: re-resolve links, refs, embeds. */
export const vaultChanged = StateEffect.define<number>();

export const vaultVersion = StateField.define<number>({
  create: () => 0,
  update(value, tr) {
    for (const e of tr.effects) if (e.is(vaultChanged)) value = e.value;
    return value;
  },
});

/** Whether a transaction list carries a vault change. */
export function hasVaultChange(update: ViewUpdate): boolean {
  return update.transactions.some((tr) => tr.effects.some((e) => e.is(vaultChanged)));
}

interface Subscribable {
  subscribe?(cb: () => void): () => void;
}

/** Debounce for vault refreshes (saves while typing arrive every ~400 ms). */
export const VAULT_REFRESH_MS = 350;

/**
 * Subscribes to the workspace while the view lives: `index.subscribe` when the index has it
 * (VaultIndex; fires after the index is up to date), otherwise `onFileChanged`.
 */
const vaultWatcher = ViewPlugin.fromClass(
  class {
    private unsubscribe: (() => void) | null = null;
    private ctx: EditorContext | null = null;
    private timer: ReturnType<typeof setTimeout> | null = null;

    constructor(private readonly view: EditorView) {
      this.attach(view.state.facet(editorContext));
    }

    update(u: ViewUpdate): void {
      const ctx = u.state.facet(editorContext);
      if (ctx !== this.ctx) this.attach(ctx);
    }

    private attach(ctx: EditorContext | null): void {
      this.detach();
      this.ctx = ctx;
      if (!ctx) return;
      const schedule = () => this.schedule();
      const index = ctx.workspace.index as typeof ctx.workspace.index & Subscribable;
      try {
        this.unsubscribe = typeof index.subscribe === 'function' ? index.subscribe(schedule) : ctx.workspace.onFileChanged(schedule);
      } catch {
        this.unsubscribe = null;
      }
    }

    private schedule(): void {
      if (this.timer !== null) clearTimeout(this.timer);
      this.timer = setTimeout(() => {
        this.timer = null;
        if (!this.view.dom.isConnected) return;
        const next = this.view.state.field(vaultVersion, false);
        if (next === undefined) return;
        this.view.dispatch({ effects: vaultChanged.of(next + 1) });
      }, VAULT_REFRESH_MS);
    }

    private detach(): void {
      if (this.timer !== null) clearTimeout(this.timer);
      this.timer = null;
      this.unsubscribe?.();
      this.unsubscribe = null;
    }

    destroy(): void {
      this.detach();
    }
  },
);

export function previewState(): Extension {
  return [previewFocus, focusEffect, vaultVersion, vaultWatcher];
}

// ---------------------------------------------------------------------------------------------
// Context helpers
// ---------------------------------------------------------------------------------------------

const fallbackT: Record<Locale, Translator | null> = { pl: null, en: null };

export function translatorOf(state: EditorState): Translator {
  const ctx = state.facet(editorContext);
  if (ctx) return ctx.t;
  return (fallbackT.pl ??= createTranslator('pl'));
}

export function localeOf(state: EditorState): Locale {
  return state.facet(editorContext)?.locale ?? 'pl';
}

export function nowOf(state: EditorState): Date {
  return state.facet(editorContext)?.now() ?? new Date();
}

export function todayOf(state: EditorState): LocalDate {
  return todayLocal(nowOf(state));
}
