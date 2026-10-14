/**
 * Registry of the focused (or last focused) note editor, for the command palette and other
 * app-level actions that operate on "the current editor". NoteEditor registers on focus and
 * unregisters when destroyed; leaving the editor (e.g. opening the palette) keeps it current.
 */
import type { EditorView } from '@codemirror/view';
import { useSyncExternalStore } from 'react';
import type { VaultPath } from '@/core/types';

export interface FocusedEditor {
  view: EditorView;
  path: VaultPath;
}

let current: FocusedEditor | null = null;
const listeners = new Set<() => void>();

function emit(): void {
  for (const l of listeners) l();
}

export function setFocusedEditor(view: EditorView, path: VaultPath): void {
  if (current?.view === view && current.path === path) return;
  current = { view, path };
  emit();
}

/** Forget `view` (when it is destroyed). */
export function clearFocusedEditor(view: EditorView): void {
  if (current?.view !== view) return;
  current = null;
  emit();
}

export function getFocusedEditor(): FocusedEditor | null {
  return current;
}

export function subscribeFocusedEditor(cb: () => void): () => void {
  listeners.add(cb);
  return () => listeners.delete(cb);
}

/** The focused/last focused editor (re-renders when it changes). */
export function useFocusedEditor(): FocusedEditor | null {
  return useSyncExternalStore(subscribeFocusedEditor, getFocusedEditor, () => null);
}
