// PLACEHOLDER — replaced by the views agent
/**
 * Content of the desktop right side panel (default tabs). The shell renders one of these
 * for the page shown in the main pane (`path`; null on non-page views, e.g. the journals).
 * Contract: named exports `BacklinksPanel`, `OutlinePanel`, `CalendarPanel`, props {@link PanelProps}.
 */
import { DatePicker, EmptyState } from '@/app/components';
import { useIndexVersion, useNavigate, useT, useWorkspace } from '@/app/state/hooks';
import { pageTitle } from '@/core/paths';
import type { VaultPath } from '@/core/types';

export interface PanelProps {
  /** Page shown in the main pane, or null. */
  path: VaultPath | null;
}

export function BacklinksPanel({ path }: PanelProps) {
  const t = useT();
  const ws = useWorkspace();
  const navigate = useNavigate();
  useIndexVersion();
  const links = path ? ws.index.backlinks(path) : [];
  if (links.length === 0) return <EmptyState size="sm" icon="link" title={t('shell.panel.backlinks')} body={t('shell.placeholder')} />;
  return (
    <ul className="b-placeholder-list">
      {links.map((b, i) => (
        <li key={`${b.path}:${b.link.line}:${i}`}>
          <button type="button" onClick={() => navigate({ view: 'page', path: b.path, line: b.link.line })}>
            {pageTitle(b.path)}
          </button>
        </li>
      ))}
    </ul>
  );
}

export function OutlinePanel({ path }: PanelProps) {
  const t = useT();
  const ws = useWorkspace();
  const navigate = useNavigate();
  useIndexVersion();
  const headings = path ? (ws.index.note(path)?.headings ?? []) : [];
  if (!path || headings.length === 0) return <EmptyState size="sm" icon="rows" title={t('shell.panel.outline')} body={t('shell.placeholder')} />;
  return (
    <ul className="b-placeholder-list">
      {headings.map((h) => (
        <li key={h.line} style={{ paddingLeft: `${(h.level - 1) * 0.75}rem` }}>
          <button type="button" onClick={() => navigate({ view: 'page', path, line: h.line })}>
            {h.text}
          </button>
        </li>
      ))}
    </ul>
  );
}

export function CalendarPanel(_props: PanelProps) {
  const ws = useWorkspace();
  const navigate = useNavigate();
  return (
    <DatePicker
      value={ws.today()}
      naturalInput={false}
      weekStartsOn={ws.config.weekStartsOn}
      onChange={(d) => {
        void ws.openJournal(d.slice(0, 10)).then((path) => navigate({ view: 'page', path }));
      }}
    />
  );
}
