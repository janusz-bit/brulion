/**
 * Settings (route view 'settings'): Ogólne, Edytor, Skarbiec (vault config, synced with the
 * vault), Przypomnienia (permissions, Android exact/full-screen alarms, test notification),
 * Synchronizacja (status + link to the sync center), Skróty, Aplikacja (tray, version, licence).
 * Device settings save immediately; the vault config has an explicit "Zapisz".
 */
import { useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import {
  Badge,
  Button,
  cx,
  Kbd,
  SegmentedControl,
  Select,
  Switch,
  TextInput,
  type SelectOption,
} from '@/app/components';
import { useAppStore, usePlatform, useSettings, useT, useWorkspace } from '@/app/state/hooks';
import type { DeviceSettings } from '@/app/state/settings';
import type { ViewProps } from '@/app/state/store';
import { journalPathForDate } from '@/core/markdown';
import type { ScheduledNotification, VaultConfig } from '@/core/types';
import { androidNotifications, desktopShell } from '@/platform';
import pkg from '../../../package.json';
import { showVaultPicker } from '../services/boot';
import { allCommands, commandTitle } from '../services/commands';
import { SyncIndicator } from './SyncIndicator';

export const REPO_URL = 'https://github.com/janusz-bit/brulion';

type SectionId = 'general' | 'editor' | 'vault' | 'reminders' | 'sync' | 'shortcuts' | 'app';
const SECTIONS: SectionId[] = ['general', 'editor', 'vault', 'reminders', 'sync', 'shortcuts', 'app'];

function Section({ id, title, children }: { id: SectionId; title: string; children: ReactNode }) {
  return (
    <section className="b-settings__section" id={`settings-${id}`} aria-labelledby={`settings-${id}-title`} data-section={id}>
      <h2 id={`settings-${id}-title`}>{title}</h2>
      <div className="b-settings__rows">{children}</div>
    </section>
  );
}

function Row({ label, hint, children, stack }: { label: ReactNode; hint?: ReactNode; children: ReactNode; stack?: boolean }) {
  return (
    <div className={cx('b-settings__row', stack && 'is-stacked')}>
      <div className="b-settings__label">
        <div className="b-settings__name">{label}</div>
        {hint && <div className="b-settings__hint">{hint}</div>}
      </div>
      <div className="b-settings__control">{children}</div>
    </div>
  );
}

function General({ s, update }: { s: DeviceSettings; update: (p: Partial<DeviceSettings>) => void }) {
  const t = useT();
  const locale = useAppStore((x) => x.locale);
  const sizes: SelectOption[] = [14, 15, 16, 17, 18, 19, 20, 22, 24].map((n) => ({ value: String(n), label: `${n} px` }));
  return (
    <Section id="general" title={t('shell.settings.general')}>
      <Row label={t('shell.settings.language')}>
        <Select
          aria-label={t('shell.settings.language')}
          value={s.locale ?? locale}
          options={[
            { value: 'pl', label: 'Polski' },
            { value: 'en', label: 'English' },
          ]}
          onValueChange={(v) => update({ locale: v === 'en' ? 'en' : 'pl' })}
        />
      </Row>
      <Row label={t('shell.settings.theme')}>
        <SegmentedControl
          aria-label={t('shell.settings.theme')}
          value={s.theme}
          onChange={(theme) => update({ theme })}
          options={[
            { value: 'system', label: t('shell.settings.theme.system'), icon: 'monitor' },
            { value: 'light', label: t('shell.settings.theme.light'), icon: 'sun' },
            { value: 'dark', label: t('shell.settings.theme.dark'), icon: 'moon' },
          ]}
        />
      </Row>
      <Row label={t('shell.settings.fontSize')}>
        <Select aria-label={t('shell.settings.fontSize')} value={String(s.fontSize)} options={sizes} onValueChange={(v) => update({ fontSize: Number(v) })} />
      </Row>
      <Row label={t('shell.settings.editorFont')} hint={t('shell.settings.editorFontHint')}>
        <SegmentedControl
          aria-label={t('shell.settings.editorFont')}
          value={s.editorFont}
          onChange={(editorFont) => update({ editorFont })}
          options={[
            { value: 'serif', label: t('shell.settings.font.serif') },
            { value: 'sans', label: t('shell.settings.font.sans') },
          ]}
        />
      </Row>
    </Section>
  );
}

function Editor({ s, update }: { s: DeviceSettings; update: (p: Partial<DeviceSettings>) => void }) {
  const t = useT();
  const e = s.editor;
  const set = (patch: Partial<DeviceSettings['editor']>) => update({ editor: { ...e, ...patch } });
  return (
    <Section id="editor" title={t('shell.settings.editor')}>
      <Row label={t('shell.settings.livePreview')} hint={t('shell.settings.livePreviewHint')}>
        <Switch checked={e.livePreview} onCheckedChange={(v) => set({ livePreview: v })} aria-label={t('shell.settings.livePreview')} />
      </Row>
      <Row label={t('shell.settings.outliner')} hint={t('shell.settings.outlinerHint')}>
        <Switch checked={e.outlinerMode} onCheckedChange={(v) => set({ outlinerMode: v })} aria-label={t('shell.settings.outliner')} />
      </Row>
      <Row label={t('shell.settings.lineWidth')}>
        <Select
          aria-label={t('shell.settings.lineWidth')}
          value={String(e.lineWidth)}
          options={[
            { value: '38', label: t('shell.settings.width.narrow') },
            { value: '46', label: t('shell.settings.width.normal') },
            { value: '60', label: t('shell.settings.width.wide') },
            { value: '0', label: t('shell.settings.width.full') },
            ...(![38, 46, 60, 0].includes(e.lineWidth) ? [{ value: String(e.lineWidth), label: `${e.lineWidth} rem` }] : []),
          ]}
          onValueChange={(v) => set({ lineWidth: Number(v) })}
        />
      </Row>
      <Row label={t('shell.settings.spellcheck')}>
        <Switch checked={e.spellcheck} onCheckedChange={(v) => set({ spellcheck: v })} aria-label={t('shell.settings.spellcheck')} />
      </Row>
    </Section>
  );
}

function VaultSection() {
  const t = useT();
  const ws = useWorkspace();
  const toast = useAppStore((x) => x.toast);
  const [draft, setDraft] = useState<VaultConfig>(() => ({ ...ws.config }));
  const [saving, setSaving] = useState(false);
  useEffect(() => setDraft({ ...ws.config }), [ws]);
  const dirty = JSON.stringify(draft) !== JSON.stringify(ws.config);
  const set = (patch: Partial<VaultConfig>) => setDraft((d) => ({ ...d, ...patch }));
  const preview = useMemo(() => {
    try {
      return journalPathForDate(ws.today(), draft);
    } catch {
      return '—';
    }
  }, [draft, ws]);
  const templates = ws.listTemplates();
  const save = async () => {
    setSaving(true);
    try {
      await ws.saveConfig({ ...draft, defaultReminderTime: draft.defaultReminderTime || '09:00' });
      toast({ kind: 'success', message: t('shell.settings.vaultSaved') });
    } catch (e) {
      toast({ kind: 'error', message: t('shell.settings.vaultSaveFailed', { error: e instanceof Error ? e.message : String(e) }) });
    } finally {
      setSaving(false);
    }
  };
  return (
    <Section id="vault" title={t('shell.settings.vault')}>
      <Row label={ws.name} hint={ws.vault.fs.label}>
        <Button icon="folder" onClick={() => showVaultPicker()}>
          {t('shell.cmd.switchVault')}
        </Button>
      </Row>
      <p className="b-settings__note">{t('shell.settings.vaultNote')}</p>
      <Row label={t('shell.settings.journalsFolder')}>
        <TextInput aria-label={t('shell.settings.journalsFolder')} value={draft.journalsFolder} onValueChange={(v) => set({ journalsFolder: v })} />
      </Row>
      <Row label={t('shell.settings.journalFormat')} hint={t('shell.settings.journalFormatHint', { example: preview })}>
        <TextInput aria-label={t('shell.settings.journalFormat')} value={draft.journalFileFormat} onValueChange={(v) => set({ journalFileFormat: v })} />
      </Row>
      <Row label={t('shell.settings.newPagesFolder')} hint={t('shell.settings.newPagesFolderHint')}>
        <TextInput aria-label={t('shell.settings.newPagesFolder')} value={draft.newPagesFolder} onValueChange={(v) => set({ newPagesFolder: v })} />
      </Row>
      <Row label={t('shell.settings.attachmentsFolder')}>
        <TextInput aria-label={t('shell.settings.attachmentsFolder')} value={draft.attachmentsFolder} onValueChange={(v) => set({ attachmentsFolder: v })} />
      </Row>
      <Row label={t('shell.settings.templatesFolder')}>
        <TextInput aria-label={t('shell.settings.templatesFolder')} value={draft.templatesFolder} onValueChange={(v) => set({ templatesFolder: v })} />
      </Row>
      <Row label={t('shell.settings.journalTemplate')}>
        <Select
          aria-label={t('shell.settings.journalTemplate')}
          value={draft.journalTemplate}
          options={[
            { value: '', label: t('shell.settings.noTemplate') },
            ...templates.map((p) => ({ value: p, label: p })),
            ...(draft.journalTemplate && !templates.includes(draft.journalTemplate) ? [{ value: draft.journalTemplate, label: draft.journalTemplate }] : []),
          ]}
          onValueChange={(v) => set({ journalTemplate: v })}
        />
      </Row>
      <Row label={t('shell.settings.reminderTime')} hint={t('shell.settings.reminderTimeHint')}>
        <TextInput
          aria-label={t('shell.settings.reminderTime')}
          type="time"
          value={draft.defaultReminderTime}
          onValueChange={(v) => set({ defaultReminderTime: v })}
        />
      </Row>
      <Row label={t('shell.settings.weekStart')}>
        <SegmentedControl
          aria-label={t('shell.settings.weekStart')}
          value={String(draft.weekStartsOn) as '0' | '1'}
          onChange={(v) => set({ weekStartsOn: v === '0' ? 0 : 1 })}
          options={[
            { value: '1', label: t('shell.settings.monday') },
            { value: '0', label: t('shell.settings.sunday') },
          ]}
        />
      </Row>
      <div className="b-settings__actions">
        <Button disabled={!dirty || saving} onClick={() => setDraft({ ...ws.config })}>
          {t('shell.settings.reset')}
        </Button>
        <Button variant="primary" disabled={!dirty} loading={saving} onClick={() => void save()}>
          {t('shell.settings.saveVault')}
        </Button>
      </div>
    </Section>
  );
}

type Perm = 'granted' | 'denied' | 'prompt' | null;

function PermissionBadge({ state }: { state: Perm | boolean }) {
  const t = useT();
  if (state === null) return null;
  const ok = state === true || state === 'granted';
  return (
    <Badge variant={ok ? 'success' : state === 'denied' || state === false ? 'warning' : 'neutral'}>
      {ok ? t('shell.settings.perm.granted') : state === 'prompt' ? t('shell.settings.perm.prompt') : t('shell.settings.perm.denied')}
    </Badge>
  );
}

function Reminders({ s, update }: { s: DeviceSettings; update: (p: Partial<DeviceSettings>) => void }) {
  const t = useT();
  const platform = usePlatform();
  const ws = useWorkspace();
  const toast = useAppStore((x) => x.toast);
  const android = platform ? androidNotifications(platform) : undefined;
  const [perm, setPerm] = useState<Perm>(null);
  const [exact, setExact] = useState<boolean | null>(null);
  const [fullScreen, setFullScreen] = useState<boolean | null>(null);
  const r = s.reminders;
  const set = (patch: Partial<DeviceSettings['reminders']>) => update({ reminders: { ...r, ...patch } });

  const refresh = useMemo(
    () => () => {
      if (!platform) return;
      void platform.notifications.permission().then(setPerm, () => setPerm('denied'));
      if (android) {
        void android.canScheduleExact().then(setExact);
        void android.canUseFullScreenIntent().then(setFullScreen);
      }
    },
    [platform, android],
  );
  useEffect(() => {
    refresh();
    const onVisible = () => document.visibilityState === 'visible' && refresh();
    document.addEventListener('visibilitychange', onVisible);
    return () => document.removeEventListener('visibilitychange', onVisible);
  }, [refresh]);

  const test = async () => {
    if (!platform) return;
    const now = new Date();
    const item: ScheduledNotification = {
      id: `test-${now.getTime()}`,
      nativeId: 2_000_000_000 + (now.getTime() % 100_000_000),
      title: t('shell.settings.testTitle'),
      body: t('shell.settings.testBody'),
      at: `${ws.today()}T${String(now.getHours()).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}`,
      alarm: false,
      path: ws.journalPath(ws.today()),
      line: 0,
    };
    try {
      await platform.notifications.showNow(item);
    } catch (e) {
      toast({ kind: 'error', message: e instanceof Error ? e.message : String(e) });
    }
  };

  return (
    <Section id="reminders" title={t('shell.settings.reminders')}>
      <Row label={t('shell.settings.remindersOn')} hint={t('shell.settings.remindersOnHint')}>
        <Switch checked={r.enabled} onCheckedChange={(v) => set({ enabled: v })} aria-label={t('shell.settings.remindersOn')} />
      </Row>
      <Row label={t('shell.settings.permission')} hint={platform?.notifications.firesWhenClosed ? t('shell.settings.firesClosed') : t('shell.settings.firesOpen')}>
        <div className="b-settings__inline">
          <PermissionBadge state={perm} />
          {perm !== 'granted' && (
            <Button size="sm" onClick={() => void platform?.notifications.requestPermission().then(setPerm)}>
              {t('shell.settings.allow')}
            </Button>
          )}
        </div>
      </Row>
      {android && (
        <>
          <Row label={t('shell.settings.exact')} hint={t('shell.settings.exactHint')}>
            <div className="b-settings__inline">
              <PermissionBadge state={exact} />
              {exact === false && (
                <Button size="sm" onClick={() => void android.requestExactAlarms().catch((e: unknown) => console.warn(e))}>
                  {t('shell.settings.openSettings')}
                </Button>
              )}
            </div>
          </Row>
          <Row label={t('shell.settings.fullScreen')} hint={t('shell.settings.fullScreenHint')}>
            <div className="b-settings__inline">
              <PermissionBadge state={fullScreen} />
              {fullScreen === false && (
                <Button size="sm" onClick={() => void android.requestFullScreenIntent().catch((e: unknown) => console.warn(e))}>
                  {t('shell.settings.openSettings')}
                </Button>
              )}
            </div>
          </Row>
        </>
      )}
      <Row label={t('shell.settings.sound')} hint={t('shell.settings.soundHint')}>
        <Switch checked={r.sound} onCheckedChange={(v) => set({ sound: v })} aria-label={t('shell.settings.sound')} />
      </Row>
      <Row label={t('shell.settings.horizon')} hint={t('shell.settings.horizonHint')}>
        <Select
          aria-label={t('shell.settings.horizon')}
          value={String(r.horizonDays)}
          options={[7, 14, 30, 60, 90, 180].map((n) => ({ value: String(n), label: t('shell.settings.days', { count: n }) }))}
          onValueChange={(v) => set({ horizonDays: Number(v) })}
        />
      </Row>
      <Row label={t('shell.settings.test')}>
        <Button icon="bell" onClick={() => void test()}>
          {t('shell.settings.sendTest')}
        </Button>
      </Row>
    </Section>
  );
}

function SyncSection({ s, update }: { s: DeviceSettings; update: (p: Partial<DeviceSettings>) => void }) {
  const t = useT();
  const navigate = useAppStore((x) => x.navigate);
  const [name, setName] = useState(s.deviceName ?? '');
  return (
    <Section id="sync" title={t('shell.settings.sync')}>
      <Row label={t('shell.settings.syncStatus')}>
        <SyncIndicator onClick={() => navigate({ view: 'sync' })} />
      </Row>
      <Row label={t('shell.settings.deviceName')} hint={t('shell.settings.deviceNameHint')}>
        <TextInput
          aria-label={t('shell.settings.deviceName')}
          value={name}
          onValueChange={setName}
          onBlur={() => name.trim() && name.trim() !== s.deviceName && update({ deviceName: name.trim() })}
        />
      </Row>
      <div className="b-settings__actions">
        <Button variant="primary" icon="repeat" onClick={() => navigate({ view: 'sync' })}>
          {t('shell.settings.openSync')}
        </Button>
      </div>
    </Section>
  );
}

function Shortcuts() {
  const t = useT();
  const cmds = allCommands().filter((c) => c.shortcut);
  return (
    <Section id="shortcuts" title={t('shell.settings.shortcuts')}>
      <dl className="b-settings__shortcuts">
        {cmds.map((c) => (
          <div key={c.id} className="b-settings__shortcut">
            <dt>{commandTitle(c, t)}</dt>
            <dd>
              <Kbd keys={c.shortcut} />
            </dd>
          </div>
        ))}
      </dl>
    </Section>
  );
}

function AppSection({ s, update }: { s: DeviceSettings; update: (p: Partial<DeviceSettings>) => void }) {
  const t = useT();
  const platform = usePlatform();
  const desktop = platform ? desktopShell(platform) : undefined;
  const open = (url: string) => void platform?.openExternal(url).catch((e: unknown) => console.warn(e));
  return (
    <Section id="app" title={t('shell.settings.app')}>
      {desktop && (
        <Row label={t('shell.settings.tray')} hint={t('shell.settings.trayHint')}>
          <Switch checked={s.closeToTray} onCheckedChange={(v) => update({ closeToTray: v })} aria-label={t('shell.settings.tray')} />
        </Row>
      )}
      <Row label={t('shell.settings.version')}>
        <span className="b-settings__value">
          Brulion {pkg.version} · {platform?.name}
        </span>
      </Row>
      <Row label={t('shell.settings.license')} hint={t('shell.settings.licenseHint')}>
        <Button variant="ghost" icon="external" onClick={() => open(`${REPO_URL}/blob/main/LICENSE`)}>
          AGPL-3.0-or-later
        </Button>
      </Row>
      <Row label={t('shell.settings.source')}>
        <Button variant="ghost" icon="external" onClick={() => open(REPO_URL)}>
          github.com/janusz-bit/brulion
        </Button>
      </Row>
    </Section>
  );
}

export function SettingsView({ route }: ViewProps<'settings'>) {
  const t = useT();
  const s = useSettings();
  const update = useAppStore((x) => x.updateSettings);
  const rootRef = useRef<HTMLDivElement | null>(null);
  const section = route.section;

  useEffect(() => {
    if (!section) return;
    const el = rootRef.current?.querySelector<HTMLElement>(`#settings-${CSS.escape(section)}`);
    el?.scrollIntoView({ block: 'start' });
  }, [section]);

  const patch = (p: Partial<DeviceSettings>) => update(p);
  return (
    <div className="b-settings" ref={rootRef}>
      <nav className="b-settings__nav" aria-label={t('shell.nav.settings')}>
        {SECTIONS.map((id) => (
          <a
            key={id}
            href={`#settings-${id}`}
            className={cx('b-settings__navlink', section === id && 'is-active')}
            onClick={(e) => {
              e.preventDefault();
              rootRef.current?.querySelector(`#settings-${id}`)?.scrollIntoView({ block: 'start', behavior: 'smooth' });
            }}
          >
            {t(`shell.settings.${id}`)}
          </a>
        ))}
      </nav>
      <div className="b-settings__content">
        <h1 className="b-settings__title">{t('shell.nav.settings')}</h1>
        <General s={s} update={patch} />
        <Editor s={s} update={patch} />
        <VaultSection />
        <Reminders s={s} update={patch} />
        <SyncSection s={s} update={patch} />
        <Shortcuts />
        <AppSection s={s} update={patch} />
      </div>
    </div>
  );
}
