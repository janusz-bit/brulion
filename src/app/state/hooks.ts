/**
 * React hooks over the app store (docs/UI.md). Views use these instead of touching the store
 * directly:
 *
 * ```tsx
 * const t = useT();
 * const ws = useWorkspace();          // the open vault (views only render when one is open)
 * useIndexVersion();                  // re-render when the index / files change
 * const note = useNote(path);         // parsed note, follows changes
 * const navigate = useNavigate();
 * ```
 */
import { useCallback, useMemo } from 'react';
import { translatorFor } from '@/app/components';
import type { Translator } from '@/core/i18n';
import type { SyncStatus } from '@/core/sync';
import type { Locale, ParsedNote, Platform, VaultPath } from '@/core/types';
import type { Workspace } from '../services/workspace';
import type { DeviceSettings } from './settings';
import { useAppStore, type Route } from './store';

export { useFocusedEditor } from '@/app/editor';
export { useAppStore } from './store';

/** Current UI language. */
export function useLocale(): Locale {
  return useAppStore((s) => s.locale);
}

/** Translator for the current UI language. */
export function useT(): Translator {
  const locale = useLocale();
  return translatorFor(locale);
}

/** The open workspace; throws when no vault is open (views render only when one is). */
export function useWorkspace(): Workspace {
  const ws = useAppStore((s) => s.workspace);
  if (!ws) throw new Error('useWorkspace() needs an open vault');
  return ws;
}

/** The open workspace or null (shell parts that also render before a vault is open). */
export function useMaybeWorkspace(): Workspace | null {
  return useAppStore((s) => s.workspace);
}

/** Bumps on every index / file change: call it to re-render when the vault changes. */
export function useIndexVersion(): number {
  return useAppStore((s) => s.indexVersion);
}

/** Parsed note at `path` (undefined when missing / not indexed yet); follows changes. */
export function useNote(path: VaultPath | null | undefined): ParsedNote | undefined {
  const ws = useMaybeWorkspace();
  const version = useIndexVersion();
  return useMemo(() => {
    void version;
    return path && ws ? ws.index.note(path) : undefined;
  }, [ws, path, version]);
}

/** Raw Markdown of `path` from the index (undefined when not indexed). */
export function useNoteContent(path: VaultPath | null | undefined): string | undefined {
  const ws = useMaybeWorkspace();
  const version = useIndexVersion();
  return useMemo(() => {
    void version;
    return path && ws ? ws.index.content(path) : undefined;
  }, [ws, path, version]);
}

export function usePlatform(): Platform | null {
  return useAppStore((s) => s.platform);
}

export function useRoute(): Route {
  return useAppStore((s) => s.route);
}

export function useSettings(): DeviceSettings {
  return useAppStore((s) => s.settings);
}

export function useSyncStatus(): SyncStatus {
  return useAppStore((s) => s.syncStatus);
}

/** `navigate(route, { newPane })` — newPane opens the route in the desktop side panel. */
export function useNavigate(): (r: Route, opts?: { replace?: boolean; newPane?: boolean }) => void {
  return useAppStore((s) => s.navigate);
}

/** Open a page by path (optionally at a line / block). */
export function useOpenPage(): (path: VaultPath, opts?: { line?: number; blockId?: string; newPane?: boolean }) => void {
  const navigate = useNavigate();
  return useCallback(
    (path, opts = {}) => {
      const r: Route = { view: 'page', path };
      if (opts.line !== undefined) r.line = opts.line;
      if (opts.blockId) r.blockId = opts.blockId;
      navigate(r, { newPane: opts.newPane });
    },
    [navigate],
  );
}

/** Show a toast: `toast({ kind: 'success', message: t('…') })`. */
export function useToast(): ReturnType<typeof useAppStore.getState>['toast'] {
  return useAppStore((s) => s.toast);
}

/** Favourite pages of the open vault. */
export function useFavourites(): VaultPath[] {
  const key = useAppStore((s) => s.workspace?.key);
  const favs = useAppStore((s) => (key ? s.settings.favourites[key] : undefined));
  return favs ?? EMPTY;
}

/** Recently opened pages of the open vault (newest first). */
export function useRecent(): VaultPath[] {
  const key = useAppStore((s) => s.workspace?.key);
  const recent = useAppStore((s) => (key ? s.settings.recent[key] : undefined));
  return recent ?? EMPTY;
}

const EMPTY: VaultPath[] = [];
