/**
 * Sync state at a glance (sidebar footer, settings): icon + "Zsynchronizowano 5 min temu",
 * "Synchronizacja…", "Błąd synchronizacji", "Offline", "Tylko na tym urządzeniu"; plus the
 * number of unresolved conflict copies.
 */
import { Cloud, CloudAlert, CloudOff, HardDrive, RefreshCw } from 'lucide-react';
import { cx, Icon, useNow } from '@/app/components';
import { useIndexVersion, useLocale, useMaybeWorkspace, useSettings, useSyncStatus, useT } from '@/app/state/hooks';
import type { Translator } from '@/core/i18n';
import type { SyncStatus } from '@/core/sync';
import type { Locale } from '@/core/types';

/** "przed chwilą", "5 min temu", "o 14:05", "12 paź 14:05". */
export function formatSyncTime(ms: number, now: Date, t: Translator, locale: Locale): string {
  const diff = Math.max(0, now.getTime() - ms);
  const min = Math.floor(diff / 60_000);
  if (min < 1) return t('shell.sync.justNow');
  if (min < 60) return t('shell.sync.minutesAgo', { count: min });
  const d = new Date(ms);
  const time = d.toLocaleTimeString(locale === 'pl' ? 'pl-PL' : 'en-GB', { hour: '2-digit', minute: '2-digit' });
  if (d.toDateString() === now.toDateString()) return t('shell.sync.at', { time });
  const date = d.toLocaleDateString(locale === 'pl' ? 'pl-PL' : 'en-GB', { day: 'numeric', month: 'short' });
  return `${date} ${time}`;
}

export interface SyncSummary {
  tone: 'ok' | 'busy' | 'error' | 'muted';
  label: string;
  detail?: string;
}

/** Describe a sync status for people. */
export function describeSync(s: SyncStatus, opts: { syncthing: boolean; now: Date; t: Translator; locale: Locale }): SyncSummary {
  const { t, now, locale } = opts;
  switch (s.state) {
    case 'syncing': {
      const p = s.progress;
      const detail = p && p.total > 0 ? `${p.done} / ${p.total}` : undefined;
      return { tone: 'busy', label: t('shell.sync.syncing'), detail };
    }
    case 'error':
      return { tone: 'error', label: t('shell.sync.error'), detail: s.lastError };
    case 'offline':
      return { tone: 'muted', label: t('shell.sync.offline'), detail: s.lastSync ? t('shell.sync.last', { when: formatSyncTime(s.lastSync, now, t, locale) }) : undefined };
    case 'idle':
      return {
        tone: 'ok',
        label: s.lastSync ? t('shell.sync.synced', { when: formatSyncTime(s.lastSync, now, t, locale) }) : t('shell.sync.waiting'),
      };
    case 'disabled':
    default:
      return opts.syncthing
        ? { tone: 'muted', label: t('shell.sync.syncthing') }
        : { tone: 'muted', label: t('shell.sync.local') };
  }
}

function StateIcon({ status, syncthing }: { status: SyncStatus; syncthing: boolean }) {
  switch (status.state) {
    case 'syncing':
      return <Icon icon={RefreshCw} size={16} className="b-spin" />;
    case 'error':
      return <Icon icon={CloudAlert} size={16} />;
    case 'offline':
      return <Icon icon={CloudOff} size={16} />;
    case 'idle':
      return <Icon icon={Cloud} size={16} />;
    default:
      return <Icon icon={syncthing ? Cloud : HardDrive} size={16} />;
  }
}

export function SyncIndicator({ onClick, compact = false }: { onClick?: () => void; compact?: boolean }) {
  const t = useT();
  const locale = useLocale();
  const status = useSyncStatus();
  const settings = useSettings();
  const ws = useMaybeWorkspace();
  useIndexVersion();
  const now = useNow(undefined, 30_000);
  const syncthing = !!settings.syncthing?.enabled || ws?.location.kind === 'tauri';
  const summary = describeSync(status, { syncthing, now, t, locale });
  const conflicts = ws ? ws.conflicts().length : 0;
  const content = (
    <>
      <span className={cx('b-sync__icon', `is-${summary.tone}`)}>
        <StateIcon status={status} syncthing={syncthing} />
      </span>
      <span className="b-sync__text">
        <span className="b-sync__label">{summary.label}</span>
        {!compact && summary.detail && <span className="b-sync__detail">{summary.detail}</span>}
      </span>
      {conflicts > 0 && (
        <span className="b-sync__conflicts" title={t('shell.sync.conflicts', { count: conflicts })}>
          {t('shell.sync.conflicts', { count: conflicts })}
        </span>
      )}
    </>
  );
  if (!onClick) return <div className="b-sync">{content}</div>;
  return (
    <button type="button" className="b-sync is-button" onClick={onClick} aria-label={`${t('shell.nav.sync')}: ${summary.label}`}>
      {content}
    </button>
  );
}
