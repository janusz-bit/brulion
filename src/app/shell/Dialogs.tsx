/**
 * Dialogs owned by the shell (opened through `store.openDialog`): new page, rename (links in
 * other notes are rewritten), delete (to the trash) and the vault switcher; plus the toasts.
 */
import { useEffect, useRef, useState } from 'react';
import { Button, ConfirmDialog, Modal, TextInput, ToastStack, ToastView } from '@/app/components';
import { useAppStore, useT, useWorkspace } from '@/app/state/hooks';
import type { ShellDialog } from '@/app/state/store';
import { isMarkdown, pageTitle, stripMdExtension } from '@/core/paths';
import { showVaultPicker } from '../services/boot';

function errorText(e: unknown): string {
  return e instanceof Error ? e.message : String(e);
}

function NewPageDialog({ dialog }: { dialog: Extract<ShellDialog, { kind: 'new-page' }> }) {
  const t = useT();
  const ws = useWorkspace();
  const close = useAppStore((s) => s.closeDialog);
  const navigate = useAppStore((s) => s.navigate);
  const [title, setTitle] = useState(dialog.title ?? '');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const ref = useRef<HTMLInputElement | null>(null);
  const folder = dialog.folder ?? ws.config.newPagesFolder;

  const submit = async () => {
    const name = title.trim();
    if (!name) return;
    setBusy(true);
    try {
      const path = await ws.createPage(name, folder);
      close();
      navigate({ view: 'page', path }, { newPane: dialog.newPane });
    } catch (e) {
      setError(errorText(e));
      setBusy(false);
    }
  };

  return (
    <Modal
      open
      size="sm"
      onClose={close}
      title={t('shell.newPage.title')}
      description={folder ? t('shell.newPage.inFolder', { folder }) : undefined}
      initialFocus={ref}
      footer={
        <>
          <Button onClick={close}>{t('common.cancel')}</Button>
          <Button variant="primary" loading={busy} disabled={!title.trim()} onClick={() => void submit()}>
            {t('shell.newPage.create')}
          </Button>
        </>
      }
    >
      <form
        onSubmit={(e) => {
          e.preventDefault();
          void submit();
        }}
      >
        <TextInput
          ref={ref}
          label={t('shell.newPage.name')}
          hint={t('shell.newPage.hint')}
          error={error ?? undefined}
          value={title}
          onValueChange={setTitle}
          enterKeyHint="done"
        />
      </form>
    </Modal>
  );
}

function RenameDialog({ path }: { path: string }) {
  const t = useT();
  const ws = useWorkspace();
  const close = useAppStore((s) => s.closeDialog);
  const toast = useAppStore((s) => s.toast);
  const initial = isMarkdown(path) ? pageTitle(path) : (path.split('/').pop() ?? path);
  const [name, setName] = useState(initial);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const ref = useRef<HTMLInputElement | null>(null);
  const linking = isMarkdown(path) ? ws.index.linksTo(path).filter((p) => p !== path).length : 0;

  useEffect(() => {
    // Select the name for quick replacement.
    requestAnimationFrame(() => ref.current?.select());
  }, []);

  const submit = async () => {
    const value = name.trim();
    if (!value || value === initial) {
      close();
      return;
    }
    setBusy(true);
    try {
      const to = await ws.renamePage(path, value);
      close();
      const st = useAppStore.getState();
      if (st.route.view === 'page' && st.route.path === path) st.navigate({ view: 'page', path: to }, { replace: true });
      if (st.sidePanel?.view === 'page' && st.sidePanel.path === path) st.openSidePanel({ view: 'page', path: to });
      toast({
        kind: 'success',
        message: linking > 0 ? t('shell.rename.doneLinks', { count: linking }) : t('shell.rename.done', { name: stripMdExtension(to.split('/').pop() ?? to) }),
      });
    } catch (e) {
      setError(errorText(e));
      setBusy(false);
    }
  };

  return (
    <Modal
      open
      size="sm"
      onClose={close}
      title={t('shell.rename.title')}
      description={linking > 0 ? t('shell.rename.links', { count: linking }) : undefined}
      initialFocus={ref}
      footer={
        <>
          <Button onClick={close}>{t('common.cancel')}</Button>
          <Button variant="primary" loading={busy} disabled={!name.trim()} onClick={() => void submit()}>
            {t('shell.rename.submit')}
          </Button>
        </>
      }
    >
      <form
        onSubmit={(e) => {
          e.preventDefault();
          void submit();
        }}
      >
        <TextInput
          ref={ref}
          label={t('shell.rename.name')}
          hint={t('shell.rename.hint')}
          error={error ?? undefined}
          value={name}
          onValueChange={setName}
          enterKeyHint="done"
        />
      </form>
    </Modal>
  );
}

function DeleteDialog({ path }: { path: string }) {
  const t = useT();
  const ws = useWorkspace();
  const close = useAppStore((s) => s.closeDialog);
  const toast = useAppStore((s) => s.toast);
  const navigate = useAppStore((s) => s.navigate);
  const folder = ws.vault.stat(path)?.kind === 'dir';
  const name = folder ? path : isMarkdown(path) ? pageTitle(path) : (path.split('/').pop() ?? path);
  return (
    <ConfirmDialog
      open
      danger
      title={folder ? t('shell.delete.folderTitle', { name }) : t('shell.delete.title', { name })}
      body={t('shell.delete.body')}
      confirmLabel={t('shell.delete.confirm')}
      onCancel={close}
      onConfirm={async () => {
        try {
          await ws.deletePage(path);
          close();
          const st = useAppStore.getState();
          const inside = (p: string) => p === path || p.startsWith(`${path}/`);
          if ((st.route.view === 'page' || st.route.view === 'history') && inside(st.route.path)) navigate({ view: 'journals' }, { replace: true });
          if (st.sidePanel && (st.sidePanel.view === 'page' || st.sidePanel.view === 'history') && inside(st.sidePanel.path)) st.openSidePanel(null);
          toast({
            kind: 'success',
            message: t('shell.delete.done', { name }),
            action: { label: t('shell.nav.trash'), onClick: () => navigate({ view: 'trash' }) },
          });
        } catch (e) {
          toast({ kind: 'error', message: t('shell.error.delete', { error: errorText(e) }) });
          throw e;
        }
      }}
    />
  );
}

export function ShellDialogs() {
  const dialog = useAppStore((s) => s.dialog);
  const ws = useAppStore((s) => s.workspace);
  useEffect(() => {
    if (dialog?.kind === 'switch-vault') showVaultPicker();
  }, [dialog]);
  if (!dialog || !ws) return null;
  switch (dialog.kind) {
    case 'new-page':
      return <NewPageDialog dialog={dialog} />;
    case 'rename':
      return <RenameDialog key={dialog.path} path={dialog.path} />;
    case 'delete':
      return <DeleteDialog key={dialog.path} path={dialog.path} />;
    default:
      return null;
  }
}

export function Toasts({ aboveNav = false }: { aboveNav?: boolean }) {
  const toasts = useAppStore((s) => s.toasts);
  const dismiss = useAppStore((s) => s.dismissToast);
  if (toasts.length === 0) return null;
  return (
    <ToastStack className={aboveNav ? 'b-toasts--above-nav' : undefined}>
      {toasts.map((toast) => (
        <ToastView key={toast.id} toast={toast} onClose={() => dismiss(toast.id)} />
      ))}
    </ToastStack>
  );
}
