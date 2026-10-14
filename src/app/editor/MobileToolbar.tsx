/**
 * Outliner/formatting toolbar for touch screens, shown while the editor is focused and kept
 * just above the virtual keyboard (visualViewport). Taps never take focus from the editor
 * (pointerdown is prevented), so the keyboard stays open. Keyboard / screen-reader users get an
 * ARIA toolbar (one tab stop, arrow keys, Escape back to the text); actions refocus the editor.
 */
import { redo, undo } from '@codemirror/commands';
import { startCompletion } from '@codemirror/autocomplete';
import { EditorSelection, type StateCommand } from '@codemirror/state';
import { EditorView } from '@codemirror/view';
import {
  AlarmClock,
  ArrowDown,
  ArrowUp,
  Brackets,
  CalendarDays,
  Hash,
  KeyboardOff,
  ListIndentDecrease,
  ListIndentIncrease,
  Redo2,
  Slash,
  SquareCheckBig,
  Undo2,
  type LucideIcon,
} from 'lucide-react';
import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import { Icon } from '@/app/components';
import type { Translator } from '@/core/i18n';
import { formatReminderToken, journalPathForDate, TOKENS } from '@/core/markdown';
import { pageTitle } from '@/core/paths';
import { DEFAULT_VAULT_CONFIG } from '@/core/types';
import { editorContext, type DialogOpener } from './context';
import { setBottomOverlay } from './margins';
import { cycleTask, indentItems, moveItemDown, moveItemUp, outdentItems } from './outliner';
import { insertToken, lineContextText } from './slash';
import { trackRange } from './tracker';

export interface MobileToolbarProps {
  view: EditorView;
  t: Translator;
  dialogs: DialogOpener;
  /** Focus left the toolbar for something other than the editor (it may hide now). */
  onFocusLeave?: () => void;
}

/** Insert text at the cursor (replacing the selection), optionally placing the cursor inside. */
function insertAtCursor(view: EditorView, text: string, cursorOffset = text.length): void {
  const { from, to } = view.state.selection.main;
  view.dispatch({
    changes: { from, to, insert: text },
    selection: EditorSelection.cursor(from + cursorOffset),
    userEvent: 'input',
    scrollIntoView: true,
  });
}

/** Keyboard offset: how much of the layout viewport the on-screen keyboard covers. */
function useKeyboardInset(): number {
  const [inset, setInset] = useState(0);
  useEffect(() => {
    const vv = window.visualViewport;
    if (!vv) return;
    const update = () => setInset(Math.max(0, Math.round(window.innerHeight - vv.height - vv.offsetTop)));
    update();
    vv.addEventListener('resize', update);
    vv.addEventListener('scroll', update);
    return () => {
      vv.removeEventListener('resize', update);
      vv.removeEventListener('scroll', update);
    };
  }, []);
  return inset;
}

interface ToolAction {
  id: string;
  icon: LucideIcon;
  label: string;
  run: () => void;
  /** Wider glyph button instead of an icon. */
  text?: string;
}

export function MobileToolbar({ view, t, dialogs, onFocusLeave }: MobileToolbarProps) {
  const inset = useKeyboardInset();
  const [active, setActive] = useState(0);
  const barRef = useRef<HTMLDivElement | null>(null);
  // The toolbar (and the keyboard under it) covers the bottom of the screen: tell CodeMirror,
  // so the cursor is scrolled clear of it (also right after the toolbar appears).
  useLayoutEffect(() => {
    setBottomOverlay(view, (barRef.current?.offsetHeight ?? 0) + inset);
    const frame = requestAnimationFrame(() => {
      if (view.hasFocus) view.dispatch({ effects: EditorView.scrollIntoView(view.state.selection.main.head) });
    });
    return () => cancelAnimationFrame(frame);
  }, [view, inset]);
  useEffect(() => () => setBottomOverlay(view, null), [view]);
  // Buttons normally never take focus (pointerdown is prevented); a screen reader or a
  // keyboard can still move it here, so editing actions hand it back to the editor.
  const refocus = () => {
    if (!view.hasFocus) view.focus();
  };
  const cmd = (c: StateCommand) => () => {
    c(view);
    refocus();
  };
  const withDialog = (open: (insert: (text: string) => void, cancel: () => void) => void) => () => {
    const pos = view.state.selection.main.head;
    const tracked = trackRange(view, pos);
    open(
      (text) => {
        const { from } = tracked.range();
        tracked.dispose();
        insertToken(view, from, from, text);
        view.focus();
      },
      () => tracked.dispose(),
    );
  };
  const config = view.state.facet(editorContext)?.workspace.config ?? DEFAULT_VAULT_CONFIG;

  const actions: ToolAction[] = [
    { id: 'outdent', icon: ListIndentDecrease, label: t('editor.toolbar.outdent'), run: cmd(outdentItems) },
    { id: 'indent', icon: ListIndentIncrease, label: t('editor.toolbar.indent'), run: cmd(indentItems) },
    { id: 'up', icon: ArrowUp, label: t('editor.toolbar.moveUp'), run: cmd(moveItemUp) },
    { id: 'down', icon: ArrowDown, label: t('editor.toolbar.moveDown'), run: cmd(moveItemDown) },
    { id: 'task', icon: SquareCheckBig, label: t('editor.toolbar.task'), run: cmd(cycleTask) },
    {
      id: 'link',
      icon: Brackets,
      label: t('editor.toolbar.link'),
      run: () => {
        insertAtCursor(view, '[[]]', 2);
        refocus();
        startCompletion(view);
      },
    },
    {
      id: 'tag',
      icon: Hash,
      label: t('editor.toolbar.tag'),
      run: () => {
        const { from } = view.state.selection.main;
        const before = from > 0 ? view.state.sliceDoc(from - 1, from) : '';
        insertAtCursor(view, before && !/\s|\(/.test(before) ? ' #' : '#');
        refocus();
        startCompletion(view);
      },
    },
    {
      id: 'slash',
      icon: Slash,
      label: t('editor.toolbar.slash'),
      run: () => {
        const { from } = view.state.selection.main;
        const before = from > 0 ? view.state.sliceDoc(from - 1, from) : '';
        insertAtCursor(view, before && !/\s/.test(before) ? ' /' : '/');
        refocus();
        startCompletion(view);
      },
    },
    {
      id: 'date',
      icon: CalendarDays,
      label: t('editor.toolbar.date'),
      run: withDialog((insert, cancel) =>
        dialogs({
          kind: 'date',
          title: t('editor.dialog.date'),
          onCancel: cancel,
          onSubmit: (v) => insert(`[[${pageTitle(journalPathForDate(v.slice(0, 10), config))}]]`),
        }),
      ),
    },
    {
      id: 'reminder',
      icon: AlarmClock,
      label: t('editor.toolbar.reminder'),
      run: withDialog((insert, cancel) =>
        dialogs({
          kind: 'reminder',
          onCancel: cancel,
          context: lineContextText(view.state.doc.lineAt(view.state.selection.main.head).text),
          onSubmit: (v) => insert(formatReminderToken(v.at, v.alarm) + (v.recurrence ? ` ${TOKENS.recurrence} ${v.recurrence}` : '')),
        }),
      ),
    },
    { id: 'undo', icon: Undo2, label: t('editor.toolbar.undo'), run: cmd(undo) },
    { id: 'redo', icon: Redo2, label: t('editor.toolbar.redo'), run: cmd(redo) },
    { id: 'hide', icon: KeyboardOff, label: t('editor.toolbar.hideKeyboard'), run: () => view.contentDOM.blur() },
  ];

  const bar = (
    <div
      ref={barRef}
      className="b-editor-toolbar"
      role="toolbar"
      aria-label={t('editor.toolbar')}
      style={{ bottom: `${inset}px` }}
      onBlur={(e) => {
        const next = e.relatedTarget as Node | null;
        if (!next || (!e.currentTarget.contains(next) && !view.dom.contains(next))) onFocusLeave?.();
      }}
      onPointerDown={(e) => {
        // Keep focus (and the keyboard) in the editor.
        if ((e.target as HTMLElement).closest('button')) e.preventDefault();
      }}
      onKeyDown={(e) => {
        // ARIA toolbar: one tab stop, arrows move between buttons, Escape returns to the text.
        const buttons = Array.from(e.currentTarget.querySelectorAll<HTMLButtonElement>('button'));
        const i = buttons.indexOf(e.target as HTMLButtonElement);
        let next = -1;
        if (e.key === 'ArrowRight') next = (i + 1) % buttons.length;
        else if (e.key === 'ArrowLeft') next = (i - 1 + buttons.length) % buttons.length;
        else if (e.key === 'Home') next = 0;
        else if (e.key === 'End') next = buttons.length - 1;
        else if (e.key === 'Escape') {
          e.preventDefault();
          view.focus();
          return;
        }
        if (next < 0 || i < 0) return;
        e.preventDefault();
        setActive(next);
        buttons[next]?.focus();
      }}
    >
      <div className="b-editor-toolbar__scroll">
        {actions.map((a, i) => (
          <button
            key={a.id}
            type="button"
            className={`b-editor-toolbar__btn b-editor-toolbar__btn--${a.id}`}
            aria-label={a.label}
            title={a.label}
            tabIndex={i === active ? 0 : -1}
            onFocus={() => setActive(i)}
            onMouseDown={(e) => e.preventDefault()}
            onClick={a.run}
          >
            <Icon icon={a.icon} size={20} />
          </button>
        ))}
      </div>
    </div>
  );
  return typeof document === 'undefined' ? null : createPortal(bar, document.body);
}
