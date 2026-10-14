/**
 * Quick switcher (Ctrl/⌘+O): find a page by name or alias. Enter opens it, Ctrl/⌘+Enter
 * opens it in the side panel (desktop), Shift+Enter creates a page with the typed title.
 */
import { useMemo, useState } from 'react';
import { Kbd, NARROW_QUERY, useMediaQuery } from '@/app/components';
import { useAppStore, useIndexVersion, useRecent, useT, useWorkspace } from '@/app/state/hooks';
import { dirname, pageTitle, stripMdExtension } from '@/core/paths';
import type { VaultPath } from '@/core/types';
import { fuzzyFilter, fuzzyMatch } from './fuzzy';
import { Picker, type PickerItem } from './Picker';

const CREATE_ID = '\u0000create';
const LIMIT = 40;

export function QuickSwitcher() {
  const open = useAppStore((s) => s.switcherOpen);
  const ws = useAppStore((s) => s.workspace);
  if (!open || !ws) return null;
  return <SwitcherBody />;
}

function folderHint(path: VaultPath): string | undefined {
  const dir = dirname(path);
  return dir && dir !== '.' ? dir : undefined;
}

function SwitcherBody() {
  const t = useT();
  const ws = useWorkspace();
  const version = useIndexVersion();
  const recent = useRecent();
  const narrow = useMediaQuery(NARROW_QUERY);
  const setOpen = useAppStore((s) => s.setSwitcherOpen);
  const navigate = useAppStore((s) => s.navigate);
  const openDialog = useAppStore((s) => s.openDialog);
  const toast = useAppStore((s) => s.toast);
  const [query, setQuery] = useState('');

  const items = useMemo((): PickerItem[] => {
    void version;
    const q = query.trim();
    if (!q) {
      const paths = recent.filter((p) => ws.exists(p));
      const rest = ws.index
        .paths()
        .filter((p) => !paths.includes(p))
        .sort((a, b) => (ws.vault.stat(b)?.mtime ?? 0) - (ws.vault.stat(a)?.mtime ?? 0));
      return [...paths, ...rest].slice(0, LIMIT).map((path, i) => ({
        id: path,
        label: pageTitle(path),
        hint: folderHint(path),
        icon: 'file',
        group: i < paths.length ? t('shell.switcher.recent') : t('shell.switcher.changed'),
      }));
    }
    const seen = new Set<VaultPath>();
    const out: PickerItem[] = [];
    for (const s of ws.index.suggestPages(q, LIMIT)) {
      if (seen.has(s.path)) continue;
      seen.add(s.path);
      const label = s.alias ?? s.title;
      out.push({
        id: s.path,
        label,
        indices: fuzzyMatch(q, label)?.indices,
        hint: s.alias ? `→ ${s.title}${folderHint(s.path) ? ` · ${folderHint(s.path)}` : ''}` : folderHint(s.path),
        icon: 'file',
      });
    }
    // Fuzzy matches on full paths fill up the list ("proj remont" → Projekty/Remont…).
    if (out.length < LIMIT) {
      const rest = ws.index.paths().filter((p) => !seen.has(p));
      for (const { item, match } of fuzzyFilter(rest, q, (p) => [stripMdExtension(p)], LIMIT - out.length)) {
        const label = stripMdExtension(item);
        out.push({ id: item, label, indices: match.indices, icon: 'file' });
      }
    }
    const exact = out.some((it) => it.label.toLowerCase() === q.toLowerCase() || pageTitle(it.id).toLowerCase() === q.toLowerCase());
    if (!exact) out.push({ id: CREATE_ID, label: t('shell.switcher.create', { title: q }), icon: 'plus', hint: t('shell.switcher.createHint') });
    return out;
  }, [query, ws, version, recent, t]);

  const create = async (title: string, newPane: boolean) => {
    try {
      const path = await ws.createPage(title);
      navigate({ view: 'page', path }, { newPane });
    } catch (e) {
      toast({ kind: 'error', message: t('shell.error.create', { error: e instanceof Error ? e.message : String(e) }) });
    }
  };

  return (
    <Picker
      open
      onClose={() => setOpen(false)}
      title={t('shell.switcher.title')}
      placeholder={t('shell.switcher.placeholder')}
      query={query}
      onQueryChange={setQuery}
      items={items}
      className="b-switcher"
      onPick={(item, mods) => {
        const q = query.trim();
        setOpen(false);
        const newPane = mods.mod && !narrow;
        if (item.id === CREATE_ID || (mods.shift && q)) {
          if (q) void create(q, newPane);
          else openDialog({ kind: 'new-page' });
          return;
        }
        navigate({ view: 'page', path: item.id }, { newPane });
      }}
      empty={t('shell.switcher.empty')}
      footer={
        <>
          <span>
            <Kbd keys="Enter" /> {t('shell.switcher.open')}
          </span>
          {!narrow && (
            <span>
              <Kbd keys="Mod+Enter" /> {t('shell.switcher.openSide')}
            </span>
          )}
          <span>
            <Kbd keys="Shift+Enter" /> {t('shell.switcher.createKey')}
          </span>
        </>
      }
    />
  );
}
