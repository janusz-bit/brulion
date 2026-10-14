/**
 * The vault's files in the sidebar: folders collapse (state kept per vault in localStorage),
 * pages open on click, attachments show their full name. Context menu (right click, or the
 * ⋯ button on touch screens): new page here, rename, show history, delete.
 */
import { FileImage, FolderClosed, FolderOpen, File as FileIcon } from 'lucide-react';
import { useEffect, useMemo, useRef, useState, type MouseEvent } from 'react';
import { cx, Icon, IconButton, Menu, type MenuItem, type VirtualAnchor } from '@/app/components';
import { useAppStore, useIndexVersion, useRoute, useT, useWorkspace } from '@/app/state/hooks';
import { basename, isConflictCopy, isMarkdown, stripMdExtension } from '@/core/paths';
import type { VaultPath } from '@/core/types';

export interface TreeNode {
  name: string;
  path: VaultPath;
  kind: 'file' | 'dir';
  children: TreeNode[];
}

const collator = new Intl.Collator('pl', { numeric: true, sensitivity: 'base' });

/** Build a sorted tree (folders first) from file and folder paths. */
export function buildTree(files: readonly VaultPath[], folders: readonly VaultPath[] = []): TreeNode {
  const root: TreeNode = { name: '', path: '', kind: 'dir', children: [] };
  const dirs = new Map<string, TreeNode>([['', root]]);
  const dirFor = (path: string): TreeNode => {
    const known = dirs.get(path);
    if (known) return known;
    const i = path.lastIndexOf('/');
    const parent = dirFor(i === -1 ? '' : path.slice(0, i));
    const node: TreeNode = { name: i === -1 ? path : path.slice(i + 1), path, kind: 'dir', children: [] };
    parent.children.push(node);
    dirs.set(path, node);
    return node;
  };
  for (const f of folders) if (f) dirFor(f);
  for (const f of files) {
    const i = f.lastIndexOf('/');
    const parent = dirFor(i === -1 ? '' : f.slice(0, i));
    parent.children.push({ name: i === -1 ? f : f.slice(i + 1), path: f, kind: 'file', children: [] });
  }
  const sort = (n: TreeNode) => {
    n.children.sort((a, b) => (a.kind !== b.kind ? (a.kind === 'dir' ? -1 : 1) : collator.compare(a.name, b.name)));
    n.children.forEach(sort);
  };
  sort(root);
  return root;
}

function storageKey(vault: string): string {
  return `brulion.tree.${vault}`;
}

function loadExpanded(vault: string): Set<string> {
  try {
    const raw = localStorage.getItem(storageKey(vault));
    const list = raw ? (JSON.parse(raw) as unknown) : null;
    return new Set(Array.isArray(list) ? list.filter((x): x is string => typeof x === 'string') : []);
  } catch {
    return new Set();
  }
}

function saveExpanded(vault: string, set: Set<string>): void {
  try {
    localStorage.setItem(storageKey(vault), JSON.stringify([...set]));
  } catch {
    // ignore
  }
}

const IMAGE_EXT = /\.(png|jpe?g|gif|webp|avif|svg|bmp)$/i;

interface MenuState {
  node: TreeNode;
  anchor: VirtualAnchor;
}

export function FileTree() {
  const t = useT();
  const ws = useWorkspace();
  const version = useIndexVersion();
  const route = useRoute();
  const navigate = useAppStore((s) => s.navigate);
  const openDialog = useAppStore((s) => s.openDialog);
  const [expanded, setExpanded] = useState<Set<string>>(() => loadExpanded(ws.key));
  const [menu, setMenu] = useState<MenuState | null>(null);
  const activePath = route.view === 'page' ? route.path : null;

  useEffect(() => setExpanded(loadExpanded(ws.key)), [ws.key]);

  // Reveal the open page's folder.
  const revealed = useRef<string | null>(null);
  useEffect(() => {
    if (!activePath || revealed.current === activePath) return;
    revealed.current = activePath;
    const parts = activePath.split('/').slice(0, -1);
    if (parts.length === 0) return;
    setExpanded((prev) => {
      const next = new Set(prev);
      let changed = false;
      for (let i = 1; i <= parts.length; i++) {
        const p = parts.slice(0, i).join('/');
        if (!next.has(p)) {
          next.add(p);
          changed = true;
        }
      }
      if (!changed) return prev;
      saveExpanded(ws.key, next);
      return next;
    });
  }, [activePath, ws.key]);

  const tree = useMemo(() => {
    void version;
    const files = ws
      .files()
      .map((f) => f.path)
      .filter((p) => !isConflictCopy(p));
    const folders = ws.vault.folders().map((f) => f.path);
    return buildTree(files, folders);
  }, [ws, version]);

  const toggle = (path: string) => {
    setExpanded((prev) => {
      const next = new Set(prev);
      if (next.has(path)) next.delete(path);
      else next.add(path);
      saveExpanded(ws.key, next);
      return next;
    });
  };

  const openMenu = (node: TreeNode, e: MouseEvent<HTMLElement>, fromButton: boolean) => {
    e.preventDefault();
    const rect = fromButton ? e.currentTarget.getBoundingClientRect() : { top: e.clientY, left: e.clientX, width: 0, height: 0 };
    setMenu({ node, anchor: { getBoundingClientRect: () => rect } });
  };

  const menuItems = (node: TreeNode): MenuItem[] => {
    if (node.kind === 'dir') {
      return [
        { id: 'new', label: t('shell.tree.newHere'), icon: 'plus', onSelect: () => openDialog({ kind: 'new-page', folder: node.path }) },
        { type: 'separator' },
        { id: 'delete', label: t('shell.tree.deleteFolder'), icon: 'trash', danger: true, onSelect: () => openDialog({ kind: 'delete', path: node.path }) },
      ];
    }
    return [
      { id: 'open', label: t('shell.tree.open'), icon: 'file', onSelect: () => navigate({ view: 'page', path: node.path }) },
      {
        id: 'side',
        label: t('shell.cmd.openSide'),
        icon: 'columns',
        onSelect: () => navigate({ view: 'page', path: node.path }, { newPane: true }),
      },
      { type: 'separator' },
      { id: 'rename', label: t('shell.tree.rename'), icon: 'pencil', onSelect: () => openDialog({ kind: 'rename', path: node.path }) },
      ...(isMarkdown(node.path)
        ? [{ id: 'history', label: t('shell.tree.history'), icon: 'clock' as const, onSelect: () => navigate({ view: 'history', path: node.path }) }]
        : []),
      { type: 'separator' },
      { id: 'delete', label: t('shell.tree.delete'), icon: 'trash', danger: true, onSelect: () => openDialog({ kind: 'delete', path: node.path }) },
    ];
  };

  const renderNodes = (nodes: TreeNode[], depth: number) => (
    <ul className="b-tree__list" role="group">
      {nodes.map((node) => {
        const isDir = node.kind === 'dir';
        const open = isDir && expanded.has(node.path);
        const label = isDir ? node.name : isMarkdown(node.path) ? stripMdExtension(node.name) : node.name;
        const icon = isDir ? (open ? FolderOpen : FolderClosed) : IMAGE_EXT.test(node.path) ? FileImage : FileIcon;
        return (
          <li key={node.path} className="b-tree__item">
            <div
              className={cx('b-tree__row', node.path === activePath && 'is-active', !isDir && !isMarkdown(node.path) && 'is-attachment')}
              style={{ paddingLeft: `calc(${depth} * var(--tree-indent) + var(--space-2))` }}
              onContextMenu={(e) => openMenu(node, e, false)}
            >
              <button
                type="button"
                className="b-tree__btn"
                aria-expanded={isDir ? open : undefined}
                aria-current={node.path === activePath ? 'page' : undefined}
                title={node.path}
                onClick={(e) => {
                  if (isDir) toggle(node.path);
                  else navigate({ view: 'page', path: node.path }, { newPane: e.ctrlKey || e.metaKey });
                }}
              >
                <Icon icon={icon} size={15} className="b-tree__icon" />
                <span className="b-tree__label">{label}</span>
              </button>
              <IconButton
                icon="more"
                size="sm"
                label={t('shell.tree.menu', { name: label })}
                className="b-tree__more"
                onClick={(e) => openMenu(node, e, true)}
              />
            </div>
            {open && node.children.length > 0 && renderNodes(node.children, depth + 1)}
          </li>
        );
      })}
    </ul>
  );

  return (
    <div className="b-tree" aria-label={t('shell.sidebar.files')}>
      {tree.children.length === 0 ? <p className="b-tree__empty">{t('shell.tree.empty')}</p> : renderNodes(tree.children, 0)}
      {menu && (
        <Menu
          open
          anchor={menu.anchor}
          placement="bottom-start"
          items={menuItems(menu.node)}
          aria-label={basename(menu.node.path)}
          onClose={() => setMenu(null)}
        />
      )}
    </div>
  );
}
