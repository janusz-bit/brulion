/**
 * Global keyboard shortcuts (docs/UI.md "Keyboard"): every registered command with
 * `global: true` and a `shortcut` (Ctrl+K palette, Ctrl+O quick switcher, Ctrl+Shift+F
 * search, Alt+←/→ back/forward, Ctrl+J today, Ctrl+, settings, Ctrl+\ sidebar). Editor
 * shortcuts are handled by the editor's own keymap.
 */
import { useEffect } from 'react';
import { isApplePlatform, layerCount } from '@/app/components';
import { allCommands, commandContext, matchesShortcut, parseShortcut, runCommand, type ParsedShortcut } from '../services/commands';

/** Commands that still work while a dialog is open (they toggle overlays). */
const OVERLAY_SAFE = new Set(['app.palette', 'app.switcher']);

export function findShortcutCommand(
  e: Pick<KeyboardEvent, 'key' | 'code' | 'ctrlKey' | 'metaKey' | 'altKey' | 'shiftKey'>,
  apple: boolean,
  cache: Map<string, ParsedShortcut>,
): string | null {
  for (const cmd of allCommands()) {
    if (!cmd.global || !cmd.shortcut) continue;
    let sc = cache.get(cmd.shortcut);
    if (!sc) {
      sc = parseShortcut(cmd.shortcut);
      cache.set(cmd.shortcut, sc);
    }
    if (matchesShortcut(e, sc, apple)) return cmd.id;
  }
  return null;
}

export function useGlobalShortcuts(): void {
  useEffect(() => {
    const apple = isApplePlatform();
    const cache = new Map<string, ParsedShortcut>();
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.defaultPrevented || e.isComposing || e.repeat) return;
      // Plain keys and Shift+key never trigger app shortcuts (typing).
      if (!e.ctrlKey && !e.metaKey && !e.altKey) return;
      const id = findShortcutCommand(e, apple, cache);
      if (!id) return;
      const target = e.target instanceof Element ? e.target : null;
      // Alt+arrows inside the editor belong to the editor (cursor by syntax).
      if (e.altKey && !e.ctrlKey && !e.metaKey && target?.closest('.cm-editor')) return;
      if (layerCount() > 0 && !OVERLAY_SAFE.has(id)) return;
      const ctx = commandContext();
      const cmd = allCommands().find((c) => c.id === id);
      if (cmd?.when && !cmd.when(ctx)) return;
      e.preventDefault();
      e.stopPropagation();
      void runCommand(id, ctx);
    };
    window.addEventListener('keydown', onKeyDown, true);
    return () => window.removeEventListener('keydown', onKeyDown, true);
  }, []);
}
