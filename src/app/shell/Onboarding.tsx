/**
 * Welcome screen / vault picker: shown on the first run, when the vault cannot be opened, and
 * from "Zmień skarbiec". Choices depend on the platform:
 * - desktop (Tauri): pick a folder (e.g. a Syncthing folder) or use ~/Brulion;
 * - Android: a folder on shared storage (needs "All files access") or app storage;
 * - browser: notes in the browser (IndexedDB) or a folder on disk (File System Access, Chromium);
 * plus the demo vault ("Przykładowy brulion") and recently opened vaults.
 */
import { FolderOpen, Globe, HardDrive, Laptop, Lightbulb, Smartphone } from 'lucide-react';
import { useCallback, useEffect, useState, type ReactNode } from 'react';
import { Badge, Button, cx, Icon, IconButton, TextInput, renderIcon, type IconSlot } from '@/app/components';
import { usePlatform, useSettings, useT } from '@/app/state/hooks';
import { useAppStore } from '@/app/state/store';
import type { Platform, VaultLocation } from '@/core/types';
import {
  cancelVaultPicker,
  demoLocation,
  forgetRecentVault,
  isCurrentVault,
  isSharedStoragePath,
  openVault,
  useBootStore,
} from '../services/boot';

export const ANDROID_DEFAULT_PATH = '/storage/emulated/0/Brulion';

function locationDetail(loc: VaultLocation, t: (k: string) => string): string {
  switch (loc.kind) {
    case 'tauri':
      return loc.path;
    case 'fsaccess':
      return t('shell.onboarding.kind.fsaccess');
    case 'idb':
      return t('shell.onboarding.kind.idb');
    case 'memory':
      return t('shell.onboarding.kind.memory');
  }
}

function Choice({
  icon,
  title,
  body,
  children,
  primary,
}: {
  icon: IconSlot;
  title: string;
  body: ReactNode;
  children: ReactNode;
  primary?: boolean;
}) {
  return (
    <div className={cx('b-onb__choice', primary && 'is-primary')}>
      <div className="b-onb__choice-icon">{renderIcon(icon, 22)}</div>
      <div className="b-onb__choice-text">
        <h3>{title}</h3>
        <p>{body}</p>
        <div className="b-onb__choice-actions">{children}</div>
      </div>
    </div>
  );
}

function useBusy(): [string | null, (id: string, fn: () => Promise<unknown>) => Promise<void>] {
  const [busy, setBusy] = useState<string | null>(null);
  const run = useCallback(async (id: string, fn: () => Promise<unknown>) => {
    setBusy(id);
    try {
      await fn();
    } finally {
      setBusy(null);
    }
  }, []);
  return [busy, run];
}

function errorToast(e: unknown): void {
  const s = useAppStore.getState();
  s.toast({ kind: 'error', message: e instanceof Error ? e.message : String(e) });
}

function WebChoices({ platform, busy, run }: { platform: Platform; busy: string | null; run: ReturnType<typeof useBusy>[1] }) {
  const t = useT();
  const fsAccess = platform.vaultKinds.includes('fsaccess');
  return (
    <>
      <Choice icon={Globe} title={t('shell.onboarding.web.title')} body={t('shell.onboarding.web.body')} primary>
        <Button
          variant="primary"
          loading={busy === 'idb'}
          onClick={() => void run('idb', async () => openVault(await platform.defaultVaultLocation()))}
        >
          {t('shell.onboarding.web.action')}
        </Button>
      </Choice>
      <Choice icon={FolderOpen} title={t('shell.onboarding.folder.title')} body={fsAccess ? t('shell.onboarding.folder.body') : t('shell.onboarding.folder.unsupported')}>
        <Button
          disabled={!fsAccess}
          loading={busy === 'folder'}
          onClick={() =>
            void run('folder', async () => {
              try {
                const loc = await platform.pickVaultFolder();
                if (loc) await openVault(loc);
              } catch (e) {
                errorToast(e);
              }
            })
          }
        >
          {t('shell.onboarding.folder.action')}
        </Button>
      </Choice>
    </>
  );
}

function DesktopChoices({ platform, busy, run }: { platform: Platform; busy: string | null; run: ReturnType<typeof useBusy>[1] }) {
  const t = useT();
  const [defaultLoc, setDefaultLoc] = useState<VaultLocation | null>(null);
  useEffect(() => {
    void platform.defaultVaultLocation().then(setDefaultLoc, (e: unknown) => console.warn('[onboarding] default folder unknown', e));
  }, [platform]);
  return (
    <>
      <Choice icon={FolderOpen} title={t('shell.onboarding.desktop.pickTitle')} body={t('shell.onboarding.desktop.pickBody')} primary>
        <Button
          variant="primary"
          loading={busy === 'pick'}
          onClick={() =>
            void run('pick', async () => {
              try {
                const loc = await platform.pickVaultFolder();
                if (loc) await openVault(loc);
              } catch (e) {
                errorToast(e);
              }
            })
          }
        >
          {t('shell.onboarding.desktop.pick')}
        </Button>
      </Choice>
      <Choice
        icon={Laptop}
        title={t('shell.onboarding.desktop.defaultTitle')}
        body={defaultLoc && defaultLoc.kind === 'tauri' ? <code>{defaultLoc.path}</code> : t('shell.onboarding.desktop.defaultBody')}
      >
        <Button loading={busy === 'default'} disabled={!defaultLoc} onClick={() => defaultLoc && void run('default', () => openVault(defaultLoc))}>
          {t('shell.onboarding.desktop.useDefault')}
        </Button>
      </Choice>
    </>
  );
}

/** Polls "All files access" (the user grants it in system settings and comes back). */
function useStorageAccess(platform: Platform): [boolean | null, () => void] {
  const [granted, setGranted] = useState<boolean | null>(null);
  const check = useCallback(() => {
    const sa = platform.storageAccess;
    if (!sa) {
      setGranted(true);
      return;
    }
    void sa.granted().then(setGranted, () => setGranted(false));
  }, [platform]);
  useEffect(() => {
    check();
    const onVisible = () => document.visibilityState === 'visible' && check();
    document.addEventListener('visibilitychange', onVisible);
    window.addEventListener('focus', check);
    return () => {
      document.removeEventListener('visibilitychange', onVisible);
      window.removeEventListener('focus', check);
    };
  }, [check]);
  return [granted, check];
}

function AndroidChoices({ platform, busy, run }: { platform: Platform; busy: string | null; run: ReturnType<typeof useBusy>[1] }) {
  const t = useT();
  const issue = useBootStore((s) => s.issue);
  const [path, setPath] = useState(issue?.location.kind === 'tauri' ? issue.location.path : ANDROID_DEFAULT_PATH);
  const [granted, recheck] = useStorageAccess(platform);
  const shared = isSharedStoragePath(path.trim());
  const name = path.trim().replace(/\/+$/, '').split('/').pop() || 'Brulion';
  return (
    <>
      <Choice icon={Smartphone} title={t('shell.onboarding.android.title')} body={t('shell.onboarding.android.body')} primary>
        <div className="b-onb__android">
          <TextInput label={t('shell.onboarding.android.path')} value={path} onValueChange={setPath} spellCheck={false} autoCapitalize="off" />
          {shared && (
            <div className="b-onb__access">
              {granted ? (
                <Badge variant="success" icon="check">
                  {t('shell.onboarding.android.granted')}
                </Badge>
              ) : (
                <Button
                  icon="check"
                  loading={busy === 'grant'}
                  onClick={() =>
                    void run('grant', async () => {
                      try {
                        await platform.storageAccess?.request();
                      } catch (e) {
                        errorToast(e);
                      }
                      recheck();
                    })
                  }
                >
                  {t('shell.onboarding.android.grant')}
                </Button>
              )}
            </div>
          )}
          <Button
            variant="primary"
            disabled={!path.trim().startsWith('/') || (shared && !granted)}
            loading={busy === 'open'}
            onClick={() => void run('open', () => openVault({ kind: 'tauri', path: path.trim(), name }))}
          >
            {t('shell.onboarding.android.open')}
          </Button>
        </div>
      </Choice>
      {!granted && (
        <Choice icon={HardDrive} title={t('shell.onboarding.android.appTitle')} body={t('shell.onboarding.android.appBody')}>
          <Button loading={busy === 'app'} onClick={() => void run('app', async () => openVault(await platform.defaultVaultLocation()))}>
            {t('shell.onboarding.android.useApp')}
          </Button>
        </Choice>
      )}
    </>
  );
}

function IssueBanner({ platform, run, busy }: { platform: Platform; busy: string | null; run: ReturnType<typeof useBusy>[1] }) {
  const t = useT();
  const issue = useBootStore((s) => s.issue);
  if (!issue) return null;
  const name = issue.location.name || locationDetail(issue.location, t);
  return (
    <div className="b-onb__issue" role="alert">
      <Icon name="warning" size={18} />
      <div className="b-onb__issue-text">
        {issue.kind === 'permission' && <p>{t('shell.onboarding.issue.permission', { name })}</p>}
        {issue.kind === 'storage-access' && <p>{t('shell.onboarding.issue.storage', { name })}</p>}
        {issue.kind === 'error' && <p>{t('shell.onboarding.issue.error', { name, error: issue.message })}</p>}
        <div className="b-onb__issue-actions">
          {issue.kind === 'storage-access' ? (
            <Button
              variant="primary"
              size="sm"
              loading={busy === 'issue'}
              onClick={() =>
                void run('issue', async () => {
                  await platform.storageAccess?.request();
                  if (await platform.storageAccess?.granted()) await openVault(issue.location);
                })
              }
            >
              {t('shell.onboarding.android.grant')}
            </Button>
          ) : (
            <Button variant="primary" size="sm" loading={busy === 'issue'} onClick={() => void run('issue', () => openVault(issue.location))}>
              {issue.kind === 'permission' ? t('shell.onboarding.issue.allow') : t('shell.onboarding.issue.retry')}
            </Button>
          )}
          <Button
            size="sm"
            variant="ghost"
            onClick={() => {
              forgetRecentVault(issue.location);
              useBootStore.setState({ issue: null });
            }}
          >
            {t('shell.onboarding.issue.forget')}
          </Button>
        </div>
      </div>
    </div>
  );
}

export function Onboarding() {
  const t = useT();
  const platform = usePlatform();
  const settings = useSettings();
  const switching = useBootStore((s) => s.switching);
  const ws = useAppStore((s) => s.workspace);
  const [busy, run] = useBusy();
  if (!platform) return null;
  const recent = settings.vaults.filter((v) => !isCurrentVault(v.location));

  return (
    <div className="b-onb">
      <div className="b-onb__card">
        <header className="b-onb__head">
          <img src={`${import.meta.env.BASE_URL}favicon.svg`} alt="" width={72} height={72} className="b-onb__logo" />
          <h1>{switching ? t('shell.onboarding.switchTitle') : t('shell.onboarding.title')}</h1>
          {!switching && <p className="b-onb__lead">{t('shell.onboarding.lead')}</p>}
        </header>

        <IssueBanner platform={platform} busy={busy} run={run} />

        <section className="b-onb__section" aria-labelledby="b-onb-where">
          <h2 id="b-onb-where">{t('shell.onboarding.where')}</h2>
          <div className="b-onb__choices">
            {platform.kind === 'web' && <WebChoices platform={platform} busy={busy} run={run} />}
            {platform.kind === 'tauri-desktop' && <DesktopChoices platform={platform} busy={busy} run={run} />}
            {platform.kind === 'tauri-android' && <AndroidChoices platform={platform} busy={busy} run={run} />}
            <Choice icon={Lightbulb} title={t('shell.onboarding.demo.title')} body={t('shell.onboarding.demo.body')}>
              <Button
                loading={busy === 'demo'}
                onClick={() => void run('demo', () => openVault(demoLocation(t('shell.onboarding.demo.name')), { demo: true }))}
              >
                {t('shell.onboarding.demo.action')}
              </Button>
            </Choice>
          </div>
        </section>

        {recent.length > 0 && (
          <section className="b-onb__section" aria-labelledby="b-onb-recent">
            <h2 id="b-onb-recent">{t('shell.onboarding.recent')}</h2>
            <ul className="b-onb__recent">
              {recent.map((v) => (
                <li key={JSON.stringify(v.location)}>
                  <button type="button" className="b-onb__recent-btn" onClick={() => void run('recent', () => openVault(v.location))}>
                    <Icon name="folder" size={16} />
                    <span className="b-onb__recent-text">
                      <span className="b-onb__recent-name">{v.location.name || locationDetail(v.location, t)}</span>
                      <span className="b-onb__recent-path">{locationDetail(v.location, t)}</span>
                    </span>
                  </button>
                  <IconButton icon="x" size="sm" label={t('shell.onboarding.forget', { name: v.location.name })} onClick={() => forgetRecentVault(v.location)} />
                </li>
              ))}
            </ul>
          </section>
        )}

        <details className="b-onb__sync">
          <summary>{t('shell.onboarding.sync.title')}</summary>
          <p>{t('shell.onboarding.sync.syncthing')}</p>
          <p>{t('shell.onboarding.sync.nextcloud')}</p>
          <p>{t('shell.onboarding.sync.files')}</p>
        </details>

        {switching && ws && (
          <div className="b-onb__back">
            <Button icon="arrow-left" variant="ghost" onClick={() => cancelVaultPicker()}>
              {t('shell.onboarding.backTo', { name: ws.name })}
            </Button>
          </div>
        )}
      </div>
    </div>
  );
}
