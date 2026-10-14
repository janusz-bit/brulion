/**
 * `<NoteEditor path=… />` — the CodeMirror note editor (contract in docs/UI.md).
 *
 * Needs an `<EditorHostProvider>` (context.ts) above it. One EditorSession (session.ts) per
 * `path`: created in an effect and destroyed (after flushing edits) in its cleanup, so React
 * StrictMode's mount → unmount → mount never leaves a second view behind.
 */
import { Fragment, useEffect, useMemo, useRef, useState, type CSSProperties } from 'react';
import { LocaleContext, Spinner, useMediaQuery, type VirtualAnchor } from '@/app/components';
import { pageTitle } from '@/core/paths';
import type { VaultPath } from '@/core/types';
import { contextFor, DEFAULT_EDITOR_SETTINGS, useEditorHost, type EditorHost } from './context';
import { DialogController, EditorDialogs } from './dialogs';
import { MobileToolbar } from './MobileToolbar';
import { EditorSession, type SessionStatus } from './session';
import { editorExtensions, reconfigureEditor, type EditorSetupOptions } from './setup';
import type { ZoomCrumb } from './zoom';

export interface NoteEditorProps {
  path: VaultPath;
  /** Scroll to / place cursor on this 0-based line or block id after load. */
  line?: number;
  blockId?: string;
  /** Outliner zoom: show only the list item at this line and its children. */
  zoomLine?: number;
  /** Journal stream / embeds: no title, auto height (no inner scroll), lighter chrome. */
  compact?: boolean;
  autoFocus?: boolean;
  readOnly?: boolean;
  /** Placeholder for empty notes (e.g. "Zacznij pisać…"). */
  placeholder?: string;
  /** Zoom changed inside the editor (0-based line of the zoomed item, null = whole page). */
  onZoomChange?: (line: number | null) => void;
  className?: string;
}

/** Options each session's view was last configured with (for incremental reconfiguration). */
const sessionOptions = new WeakMap<EditorSession, () => EditorSetupOptions | null>();

/** Popover anchor at the editor's caret. */
function caretAnchor(session: EditorSession): VirtualAnchor {
  return {
    getBoundingClientRect() {
      const view = session.view;
      if (!view) return { top: 0, left: 0, width: 0, height: 0 };
      const rect = view.coordsAtPos(view.state.selection.main.head);
      if (!rect) {
        const r = view.contentDOM.getBoundingClientRect();
        return { top: r.top, left: r.left, width: 0, height: 0 };
      }
      return { top: rect.top, left: rect.left, width: 1, height: rect.bottom - rect.top };
    },
    contains: (node) => !!session.view?.dom.contains(node),
  };
}

export function NoteEditor(props: NoteEditorProps) {
  const { path, line, blockId, zoomLine, compact = false, autoFocus = false, readOnly = false, placeholder, className } = props;
  const host = useEditorHost();
  const t = host.t;
  // Latest props/host for callbacks created once per session.
  const hostRef = useRef<EditorHost>(host);
  hostRef.current = host;
  const propsRef = useRef(props);
  propsRef.current = props;

  const mountRef = useRef<HTMLDivElement | null>(null);
  const [session, setSession] = useState<EditorSession | null>(null);
  const [status, setStatus] = useState<SessionStatus>('loading');
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [crumbs, setCrumbs] = useState<ZoomCrumb[] | null>(null);
  const [focused, setFocused] = useState(false);
  const controller = useMemo(() => new DialogController(), []);
  const coarse = useMediaQuery('(pointer: coarse)');

  const settings = host.settings;
  const setupOptions = useMemo((): Omit<EditorSetupOptions, 'context' | 'dialogs'> => {
    return {
      settings,
      readOnly,
      compact,
      locale: host.locale,
      placeholder: placeholder ?? t('editor.placeholder'),
      label: t('editor.region', { title: pageTitle(path) }),
    };
  }, [settings, readOnly, compact, placeholder, t, path, host.locale]);
  const setupRef = useRef(setupOptions);
  setupRef.current = setupOptions;

  // One session per path (and workspace).
  const workspace = host.workspace;
  useEffect(() => {
    const parent = mountRef.current;
    if (!parent) return;
    const proxyHost: EditorHost = {
      workspace,
      get t() {
        return hostRef.current.t;
      },
      get locale() {
        return hostRef.current.locale;
      },
      now: () => hostRef.current.now(),
      navigate: (r, o) => hostRef.current.navigate(r, o),
      openLink: (target, o) => hostRef.current.openLink(target, o),
    };
    const context = contextFor(proxyHost, path);
    let applied: EditorSetupOptions | null = null;
    const s = new EditorSession({
      path,
      workspace,
      parent,
      readOnly: setupRef.current.readOnly,
      extensions: () => {
        applied = { ...setupRef.current, context, dialogs: controller.open };
        return editorExtensions(applied);
      },
      callbacks: {
        onStatus: (st, err) => {
          setStatus(st);
          setError(err ? String((err as Error).message ?? err) : null);
        },
        onSaveError: (err) => setNotice(hostRef.current.t('editor.saveError', { error: String((err as Error).message ?? err) })),
        onMerged: (clean) => setNotice(hostRef.current.t(clean ? 'editor.merged' : 'editor.mergeConflict')),
        onZoom: (c, l) => {
          setCrumbs(c);
          propsRef.current.onZoomChange?.(l);
        },
        // Focus moving into the mobile toolbar (screen readers may focus its buttons) keeps it.
        onFocus: (f) => setFocused(f || !!document.activeElement?.closest('.b-editor-toolbar')),
        onDeleted: () => setNotice(hostRef.current.t('editor.deleted')),
        onRenamed: (p) => {
          context.path = p;
        },
      },
    });
    // Remember what the view was built with, for later reconfiguration.
    sessionOptions.set(s, () => applied);
    controller.anchor = caretAnchor(s);
    controller.onClosed = () => requestAnimationFrame(() => s.view?.focus());
    setSession(s);
    setStatus('loading');
    setCrumbs(null);
    setNotice(null);
    // A view destroyed while focused never reports the blur.
    setFocused(false);
    void s.start();
    return () => {
      controller.onClosed = null;
      controller.close();
      void s.destroy();
    };
  }, [path, workspace, controller]);

  // Live option changes.
  useEffect(() => {
    if (!session?.alive || status !== 'ready') return;
    const get = sessionOptions.get(session);
    const prev = get?.() ?? undefined;
    const next: EditorSetupOptions = { ...setupOptions, context: prev?.context, dialogs: controller.open };
    reconfigureEditor(session.view!, next, prev);
    session.setReadOnly(!!next.readOnly);
    sessionOptions.set(session, () => next);
  }, [session, status, setupOptions, controller]);

  // Targets: applied once the note is loaded (pending until then) and whenever they change.
  useEffect(() => {
    if (session && (line !== undefined || blockId)) session.reveal({ line, blockId });
  }, [session, line, blockId]);
  useEffect(() => {
    session?.setZoomLine(zoomLine ?? null);
  }, [session, zoomLine]);

  useEffect(() => {
    if (autoFocus && status === 'ready') session?.focus();
  }, [autoFocus, status, session]);

  // Notices disappear after a while.
  useEffect(() => {
    if (!notice) return;
    const id = setTimeout(() => setNotice(null), 6000);
    return () => clearTimeout(id);
  }, [notice]);

  const s = { ...DEFAULT_EDITOR_SETTINGS, ...settings };
  const style: CSSProperties & Record<`--${string}`, string> = {};
  if (s.lineWidth > 0) style['--editor-measure'] = `${s.lineWidth}rem`;
  if (s.fontSize) style['--editor-font-size'] = `${s.fontSize}px`;
  const title = pageTitle(path);
  const now = host.now();

  return (
    <LocaleContext.Provider value={host.locale}>
      <div
        className={['b-editor', compact && 'is-compact', readOnly && 'is-readonly', s.outlinerMode && 'is-outliner', className]
          .filter(Boolean)
          .join(' ')}
        style={style}
        data-path={path}
        data-status={status}
      >
        {crumbs && (
          <nav className="b-editor__crumbs" aria-label={t('editor.zoom.nav')}>
            <button type="button" className="b-editor__crumb" onClick={() => session?.setZoomLine(null)} title={t('editor.zoom.out', { title })}>
              {title}
            </button>
            {crumbs.map((c, i) => (
              <Fragment key={c.line}>
                <span className="b-editor__crumb-sep" aria-hidden="true">
                  ›
                </span>
                {i < crumbs.length - 1 ? (
                  <button
                    type="button"
                    className="b-editor__crumb"
                    onClick={() => session?.setZoomLine(c.line)}
                    title={t('editor.zoom.parent', { text: c.text })}
                  >
                    {c.text}
                  </button>
                ) : (
                  <span className="b-editor__crumb is-current" aria-current="location">
                    {c.text}
                  </span>
                )}
              </Fragment>
            ))}
          </nav>
        )}
        <div className="b-editor__cm" ref={mountRef} />
        {status === 'loading' && !compact && (
          <div className="b-editor__status">
            <Spinner size={18} label={t('editor.loading')} />
          </div>
        )}
        {status === 'error' && (
          <div className="b-editor__error" role="alert">
            {t('editor.loadError', { error: error ?? '' })}
          </div>
        )}
        {notice && (
          <div className="b-editor__notice" role="status">
            {notice}
          </div>
        )}
        <EditorDialogs
          controller={controller}
          now={now}
          weekStartsOn={workspace.config.weekStartsOn}
          defaultTime={workspace.config.defaultReminderTime}
          templatesFolder={workspace.config.templatesFolder}
        />
        {session?.view && focused && coarse && !readOnly && (
          <MobileToolbar view={session.view} t={t} dialogs={controller.open} onFocusLeave={() => setFocused(false)} />
        )}
      </div>
    </LocaleContext.Provider>
  );
}
