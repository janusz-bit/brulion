/**
 * Slash menu: `/` at a line start or after whitespace opens a command list (filtered by Polish
 * and English names, diacritic-insensitive). Commands insert Markdown or open a dialog through
 * the `editorDialogs` facet (rendered by NoteEditor, see dialogs.tsx).
 */
import { startCompletion, type Completion, type CompletionSource } from '@codemirror/autocomplete';
import { EditorSelection, Transaction } from '@codemirror/state';
import type { EditorView } from '@codemirror/view';
import { addDaysLocal, foldText, formatHm, todayLocal, toFileDateTime } from '@/core/dates';
import { createTranslator, type Translator } from '@/core/i18n';
import { applyTemplate, formatReminderToken, journalPathForDate, stripTokens, TOKENS } from '@/core/markdown';
import { pageTitle } from '@/core/paths';
import { DEFAULT_VAULT_CONFIG, type LocalDate, type Locale } from '@/core/types';
import { editorContext, editorDialogs, editorNow, templatePaths, type EditorDialogRequest } from './context';
import { inNoCompleteContext, parseListLine } from './outliner';
import { trackRange } from './tracker';

export interface SlashCommand {
  id: string;
  /** i18n key of the label (`editor.slash.<id>`). */
  label: string;
  /** Extra search words (any language). */
  keywords?: readonly string[];
  /** Syntax preview shown next to the label. */
  hint?: string;
  /** Replace the `/query` text at [from, to) with the command's result. */
  run(view: EditorView, from: number, to: number): void;
}

const translators = new Map<Locale, Translator>();
function tr(locale: Locale): Translator {
  let t = translators.get(locale);
  if (!t) translators.set(locale, (t = createTranslator(locale)));
  return t;
}

function localeOf(view: EditorView): Locale {
  return view.state.facet(editorContext)?.locale ?? 'pl';
}

// ---------------------------------------------------------------------------------------------
// Editing helpers
// ---------------------------------------------------------------------------------------------

/** Replace [from, to) with `insert`; select [anchor, head] relative to `from` (default: end). */
export function replaceWith(view: EditorView, from: number, to: number, insert: string, anchor?: number, head?: number): void {
  const a = from + (anchor ?? insert.length);
  const h = from + (head ?? anchor ?? insert.length);
  view.dispatch({
    changes: { from, to, insert },
    selection: EditorSelection.range(a, h),
    scrollIntoView: true,
    userEvent: 'input.complete',
  });
}

/** Insert an inline token (` 📅 2026-10-15`), adding a separating space when needed. */
export function insertToken(view: EditorView, from: number, to: number, token: string): void {
  const before = from > 0 ? view.state.sliceDoc(from - 1, from) : '';
  const lead = before && !/\s/.test(before) ? ' ' : '';
  replaceWith(view, from, to, lead + token);
}

/** Insert text that must stand on its own line(s) (tables, code blocks, rules). */
export function insertBlock(view: EditorView, from: number, to: number, text: string, cursor: number, opts: { blankBefore?: boolean } = {}): void {
  const line = view.state.doc.lineAt(from);
  const before = line.text.slice(0, from - line.from);
  const after = line.text.slice(to - line.from);
  let prefix = '';
  if (before.trim() !== '') prefix = opts.blankBefore ? '\n\n' : '\n';
  else if (opts.blankBefore && line.number > 1 && view.state.doc.line(line.number - 1).text.trim() !== '') prefix = '\n';
  const suffix = after.trim() !== '' ? '\n' : '';
  replaceWith(view, from, to, prefix + text + suffix, prefix.length + cursor);
}

/** Remove the `/query` text, then edit the line prefix with `fn(lineText) → [from, to, insert]`. */
function editLinePrefix(view: EditorView, from: number, to: number, fn: (text: string) => [number, number, string]): void {
  const line = view.state.doc.lineAt(from);
  const text = line.text.slice(0, from - line.from) + line.text.slice(to - line.from);
  const [a, b, insert] = fn(text);
  const trigger = from - line.from;
  const cursor = line.from + (a <= trigger ? trigger + insert.length - (b - a) : trigger);
  view.dispatch(
    { changes: { from, to, insert: '' } },
    {
      changes: { from: line.from + a, to: line.from + b, insert },
      sequential: true,
      selection: EditorSelection.cursor(cursor),
      scrollIntoView: true,
      userEvent: 'input.complete',
    },
  );
}

function headingPrefix(level: number) {
  return (text: string): [number, number, string] => {
    const li = parseListLine(text);
    const start = li ? li.contentFrom : /^[ \t]*/.exec(text)![0].length;
    const existing = /^#{1,6}[ \t]+/.exec(text.slice(start));
    return [start, start + (existing?.[0].length ?? 0), `${'#'.repeat(level)} `];
  };
}

function linePrefix(prefix: string) {
  return (text: string): [number, number, string] => {
    const start = /^[ \t]*/.exec(text)![0].length;
    return [start, start, prefix];
  };
}

function taskPrefix(text: string): [number, number, string] {
  const li = parseListLine(text);
  if (li) {
    if (li.checkbox !== null) return [0, 0, ''];
    return [li.markerTo + 1, li.markerTo + 1, '[ ] '];
  }
  const start = /^[ \t]*/.exec(text)![0].length;
  return [start, start, '- [ ] '];
}

/** Readable text of a line for dialog context: no list marker, checkbox or metadata tokens. */
export function lineContextText(line: string): string {
  const li = parseListLine(line);
  return stripTokens(li ? line.slice(li.contentFrom) : line.trim()).replace(/(^|\s)\/\S*$/, '').trim();
}

function journalLink(view: EditorView, date: LocalDate): string {
  const config = view.state.facet(editorContext)?.workspace.config ?? DEFAULT_VAULT_CONFIG;
  return `[[${pageTitle(journalPathForDate(date, config))}]]`;
}

/**
 * Remove the `/query` text before a dialog opens. Kept out of the undo history: the command's
 * result (inserted later) is its one undo step, and undoing it must not bring `/query` back.
 */
function removeTrigger(view: EditorView, from: number, to: number): void {
  view.dispatch({
    changes: { from, to, insert: '' },
    selection: EditorSelection.cursor(from),
    userEvent: 'input.complete',
    annotations: Transaction.addToHistory.of(false),
  });
}

/** Open a dialog; its result is inserted where the `/query` was (tracked across edits). */
function withDialog(view: EditorView, from: number, to: number, make: (insertAt: (text: string) => void) => EditorDialogRequest): void {
  removeTrigger(view, from, to);
  const tracked = trackRange(view, from);
  const insertAt = (text: string) => {
    const { from: pos } = tracked.range();
    tracked.dispose();
    insertToken(view, pos, pos, text);
    view.focus();
  };
  view.state.facet(editorDialogs)({ ...make(insertAt), onCancel: () => tracked.dispose() });
}

function dateCommand(field: 'due' | 'scheduled' | 'start', titleKey: string) {
  return (view: EditorView, from: number, to: number) =>
    withDialog(view, from, to, (insertAt) => ({
      kind: 'date',
      title: tr(localeOf(view))(titleKey),
      withTime: true,
      onSubmit: (value) => insertAt(`${TOKENS[field]} ${toFileDateTime(value)}`),
    }));
}

function priorityCommand(emoji: string) {
  return (view: EditorView, from: number, to: number) => insertToken(view, from, to, emoji);
}

// ---------------------------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------------------------

export const SLASH_COMMANDS: readonly SlashCommand[] = [
  {
    id: 'task',
    label: 'editor.slash.task',
    keywords: ['todo', 'checkbox', 'zrobic'],
    hint: '- [ ]',
    run: (view, from, to) => editLinePrefix(view, from, to, taskPrefix),
  },
  {
    id: 'today',
    label: 'editor.slash.today',
    keywords: ['dzisiaj', 'dzis', 'today', 'dziennik'],
    hint: '[[…]]',
    run: (view, from, to) => replaceWith(view, from, to, journalLink(view, todayLocal(editorNow(view.state)))),
  },
  {
    id: 'tomorrow',
    label: 'editor.slash.tomorrow',
    keywords: ['tomorrow', 'jutro'],
    hint: '[[…]]',
    run: (view, from, to) => replaceWith(view, from, to, journalLink(view, addDaysLocal(todayLocal(editorNow(view.state)), 1))),
  },
  {
    id: 'date',
    label: 'editor.slash.date',
    keywords: ['kalendarz', 'dzien', 'calendar', 'day'],
    hint: '[[…]]',
    run: (view, from, to) =>
      withDialog(view, from, to, (insertAt) => ({
        kind: 'date',
        title: tr(localeOf(view))('editor.dialog.date'),
        onSubmit: (value) => insertAt(journalLink(view, value.slice(0, 10))),
      })),
  },
  {
    id: 'reminder',
    label: 'editor.slash.reminder',
    keywords: ['przypomnij', 'alarm', 'budzik', 'remind', 'powiadomienie'],
    hint: TOKENS.reminder,
    run: (view, from, to) =>
      withDialog(view, from, to, (insertAt) => ({
        kind: 'reminder',
        context: lineContextText(view.state.doc.lineAt(from).text),
        onSubmit: (v) => insertAt(formatReminderToken(v.at, v.alarm) + (v.recurrence ? ` ${TOKENS.recurrence} ${v.recurrence}` : '')),
      })),
  },
  { id: 'due', label: 'editor.slash.due', keywords: ['deadline', 'termin', 'due'], hint: TOKENS.due, run: dateCommand('due', 'editor.dialog.due') },
  {
    id: 'scheduled',
    label: 'editor.slash.scheduled',
    keywords: ['zaplanowane', 'schedule', 'plan'],
    hint: TOKENS.scheduled,
    run: dateCommand('scheduled', 'editor.dialog.scheduled'),
  },
  { id: 'start', label: 'editor.slash.start', keywords: ['start', 'poczatek', 'od'], hint: TOKENS.start, run: dateCommand('start', 'editor.dialog.start') },
  {
    id: 'recurrence',
    label: 'editor.slash.recurrence',
    keywords: ['powtarzanie', 'cykliczne', 'co tydzien', 'repeat', 'recurring', 'every'],
    hint: TOKENS.recurrence,
    run: (view, from, to) =>
      withDialog(view, from, to, (insertAt) => ({
        kind: 'recurrence',
        onSubmit: (v) => insertAt(`${TOKENS.recurrence} ${v.rule}`),
      })),
  },
  {
    id: 'priorityHigh',
    label: 'editor.slash.priorityHigh',
    keywords: ['priorytet', 'pilne', 'ważne', 'priority', 'high', 'urgent'],
    hint: TOKENS.priority.high,
    run: priorityCommand(TOKENS.priority.high),
  },
  {
    id: 'priorityMedium',
    label: 'editor.slash.priorityMedium',
    keywords: ['priorytet', 'priority', 'medium'],
    hint: TOKENS.priority.medium,
    run: priorityCommand(TOKENS.priority.medium),
  },
  {
    id: 'priorityLow',
    label: 'editor.slash.priorityLow',
    keywords: ['priorytet', 'priority', 'low'],
    hint: TOKENS.priority.low,
    run: priorityCommand(TOKENS.priority.low),
  },
  {
    id: 'table',
    label: 'editor.slash.table',
    keywords: ['tabelka', 'table', 'grid', 'arkusz'],
    hint: '| |',
    run: (view, from, to) =>
      insertBlock(view, from, to, '|  |  |  |\n| --- | --- | --- |\n|  |  |  |\n|  |  |  |', 2, { blankBefore: true }),
  },
  {
    id: 'calc',
    label: 'editor.slash.calc',
    keywords: ['kalkulator', 'liczenie', 'calc', 'math', 'matematyka', 'oblicz'],
    hint: '```calc',
    run: (view, from, to) => insertBlock(view, from, to, '```calc\n\n```', 8),
  },
  {
    id: 'query',
    label: 'editor.slash.query',
    keywords: ['query', 'kwerenda', 'wyszukaj', 'lista zadan', 'filtr'],
    hint: '```query',
    // A working example query (open tasks due within 7 days). No comment line: the query
    // syntax (docs/SYNTAX.md §10) has none, `# …` would be read as search words.
    run: (view, from, to) => {
      const text = '```query\ntype:tasks status:open due:<=+7d sort:due\n```';
      insertBlock(view, from, to, text, text.length - 4);
    },
  },
  { id: 'h1', label: 'editor.slash.h1', keywords: ['heading', 'tytul', 'h1'], hint: '#', run: (v, f, t) => editLinePrefix(v, f, t, headingPrefix(1)) },
  { id: 'h2', label: 'editor.slash.h2', keywords: ['heading', 'h2'], hint: '##', run: (v, f, t) => editLinePrefix(v, f, t, headingPrefix(2)) },
  { id: 'h3', label: 'editor.slash.h3', keywords: ['heading', 'h3'], hint: '###', run: (v, f, t) => editLinePrefix(v, f, t, headingPrefix(3)) },
  { id: 'quote', label: 'editor.slash.quote', keywords: ['quote', 'cytat', 'blockquote'], hint: '>', run: (v, f, t) => editLinePrefix(v, f, t, linePrefix('> ')) },
  ...(['note', 'tip', 'warning'] as const).map(
    (kind): SlashCommand => ({
      id: `callout${kind[0]!.toUpperCase()}${kind.slice(1)}`,
      label: `editor.slash.callout${kind[0]!.toUpperCase()}${kind.slice(1)}`,
      keywords: ['callout', 'ramka', 'admonition', kind],
      hint: `> [!${kind}]`,
      run: (view, from, to) => {
        const line = view.state.doc.lineAt(from);
        const rest = (line.text.slice(0, from - line.from) + line.text.slice(to - line.from)).trim();
        if (rest === '') {
          const text = `> [!${kind}] \n> `;
          insertBlock(view, from, to, text, text.length);
        } else editLinePrefix(view, from, to, linePrefix(`> [!${kind}] `));
      },
    }),
  ),
  { id: 'code', label: 'editor.slash.code', keywords: ['code', 'kod', 'snippet', 'fence'], hint: '```', run: (v, f, t) => insertBlock(v, f, t, '```\n\n```', 4) },
  {
    id: 'embed',
    label: 'editor.slash.embed',
    keywords: ['embed', 'osadz', 'transclude', 'wstaw strone'],
    hint: '![[ ]]',
    run: (view, from, to) => {
      replaceWith(view, from, to, '![[]]', 3);
      startCompletion(view);
    },
  },
  {
    id: 'blockRef',
    label: 'editor.slash.blockRef',
    keywords: ['block', 'reference', 'blok', 'odwolanie', 'ref'],
    hint: '(( ))',
    run: (view, from, to) => {
      replaceWith(view, from, to, '(())', 2);
      startCompletion(view);
    },
  },
  {
    id: 'template',
    label: 'editor.slash.template',
    keywords: ['template', 'szablon', 'wzor'],
    hint: '{{ }}',
    run: (view, from, to) => {
      const ctx = view.state.facet(editorContext);
      if (!ctx) return;
      removeTrigger(view, from, to);
      const tracked = trackRange(view, from);
      view.state.facet(editorDialogs)({
        kind: 'template',
        templates: templatePaths(ctx.workspace),
        onCancel: () => tracked.dispose(),
        onSubmit: (path) => {
          void ctx.workspace.read(path).then(
            (template) => {
              const { text, cursor } = applyTemplate(template, { now: ctx.now(), title: pageTitle(ctx.path), locale: ctx.locale });
              const { from: pos } = tracked.range();
              tracked.dispose();
              replaceWith(view, pos, pos, text, cursor ?? text.length);
              view.focus();
            },
            () => tracked.dispose(),
          );
        },
      });
    },
  },
  {
    id: 'hr',
    label: 'editor.slash.hr',
    keywords: ['separator', 'linia', 'hr', 'divider', 'rule'],
    hint: '---',
    run: (view, from, to) => insertBlock(view, from, to, '---\n', 4, { blankBefore: true }),
  },
  {
    id: 'property',
    label: 'editor.slash.property',
    keywords: ['property', 'wlasciwosc', 'atrybut', 'metadata'],
    hint: 'key::',
    run: (view, from, to) => {
      const key = tr(localeOf(view))('editor.slash.propertyKey');
      replaceWith(view, from, to, `${key}:: `, 0, key.length);
    },
  },
  {
    id: 'time',
    label: 'editor.slash.time',
    keywords: ['godzina', 'czas', 'teraz', 'time', 'now', 'dzisiejsza godzina'],
    hint: 'HH:mm',
    run: (view, from, to) => {
      const now = editorNow(view.state);
      replaceWith(view, from, to, formatHm(now.getHours(), now.getMinutes()));
    },
  },
];

// ---------------------------------------------------------------------------------------------
// Filtering
// ---------------------------------------------------------------------------------------------

/** Names a command can be found by: labels in both languages + keywords, folded. */
function searchNames(cmd: SlashCommand, locale: Locale): { primary: string[]; extra: string[] } {
  const own = tr(locale)(cmd.label);
  const other = tr(locale === 'pl' ? 'en' : 'pl')(cmd.label);
  return {
    primary: [own, other].map((s) => foldText(s.replace(/…$/, ''))),
    extra: (cmd.keywords ?? []).map((k) => foldText(k)),
  };
}

function score(names: { primary: string[]; extra: string[] }, q: string): number {
  let best = 0;
  names.primary.forEach((name, i) => {
    const bonus = i === 0 ? 5 : 0;
    if (name.startsWith(q)) best = Math.max(best, 100 + bonus);
    else if (name.split(/[\s:]+/).some((w) => w.startsWith(q))) best = Math.max(best, 70 + bonus);
    else if (name.includes(q)) best = Math.max(best, 30 + bonus);
  });
  for (const k of names.extra) {
    if (k.startsWith(q)) best = Math.max(best, 60);
    else if (k.includes(q)) best = Math.max(best, 20);
  }
  return best;
}

/** Commands matching `query` (diacritic- and case-insensitive, PL + EN), best first. */
export function filterSlashCommands(query: string, locale: Locale, commands: readonly SlashCommand[] = SLASH_COMMANDS): SlashCommand[] {
  const q = foldText(query.trim());
  if (!q) return [...commands];
  return commands
    .map((c, i) => ({ c, i, s: score(searchNames(c, locale), q) }))
    .filter((x) => x.s > 0)
    .sort((a, b) => b.s - a.s || a.i - b.i)
    .map((x) => x.c);
}

/** Where a slash query starts in the text before the cursor, or null. */
export function slashQueryAt(lineBefore: string): { offset: number; query: string } | null {
  const m = /(?:^|[\s(])\/([^\s/]{0,30})$/u.exec(lineBefore);
  if (!m) return null;
  const query = m[1]!;
  return { offset: lineBefore.length - query.length - 1, query };
}

/** Autocomplete source for the slash menu. */
export function slashCompletionSource(commands: readonly SlashCommand[] = SLASH_COMMANDS): CompletionSource {
  return (context) => {
    const { state, pos } = context;
    const line = state.doc.lineAt(pos);
    const hit = slashQueryAt(line.text.slice(0, pos - line.from));
    if (!hit || inNoCompleteContext(state, pos)) return null;
    const locale = state.facet(editorContext)?.locale ?? 'pl';
    const t = tr(locale);
    const list = filterSlashCommands(hit.query, locale, commands);
    if (list.length === 0) return null;
    const from = line.from + hit.offset;
    return {
      from,
      filter: false,
      options: list.map(
        (cmd, i): Completion => ({
          label: t(cmd.label),
          detail: cmd.hint,
          type: 'slash',
          boost: -i,
          apply: (view, _completion, f, to) => cmd.run(view, f, to),
        }),
      ),
    };
  };
}
