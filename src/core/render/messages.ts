/**
 * User-visible strings of the renderer (Polish + English).
 *
 * Kept local to the module (like `@/core/calc`) so rendering works without the i18n loader
 * (which relies on `import.meta.glob`), e.g. in workers or plain Node. The shape matches
 * `MessageModule` of `@/core/i18n`, so the app may register it there; a translator passed as
 * `RenderContext.t` takes precedence for keys it knows.
 */
import type { Locale } from '../types';
import type { RenderTranslator } from './types';

const pl = {
  'render.callout.note': 'Notatka',
  'render.callout.abstract': 'Streszczenie',
  'render.callout.info': 'Informacja',
  'render.callout.todo': 'Do zrobienia',
  'render.callout.tip': 'Wskazówka',
  'render.callout.success': 'Sukces',
  'render.callout.question': 'Pytanie',
  'render.callout.warning': 'Ostrzeżenie',
  'render.callout.failure': 'Niepowodzenie',
  'render.callout.danger': 'Niebezpieczeństwo',
  'render.callout.bug': 'Błąd',
  'render.callout.example': 'Przykład',
  'render.callout.quote': 'Cytat',
  'render.embed.missing': 'Nie znaleziono: {target}',
  'render.embed.cycle': 'Cykliczne osadzenie: {target}',
  'render.embed.tooDeep': 'Zbyt głębokie zagnieżdżenie osadzeń: {target}',
  'render.blockRef.missing': 'Nie znaleziono bloku {id}',
  'render.query.title': 'Zapytanie',
  'render.footnotes.back': 'Wróć do treści',
  'render.task.todo': 'Do zrobienia',
  'render.task.doing': 'W trakcie',
  'render.task.done': 'Zrobione',
  'render.task.cancelled': 'Anulowane',
  'render.task.deferred': 'Odłożone',
  'render.chip.due': 'Termin',
  'render.chip.scheduled': 'Zaplanowane',
  'render.chip.start': 'Początek',
  'render.chip.created': 'Utworzono',
  'render.chip.done': 'Wykonano',
  'render.chip.cancelled': 'Anulowano',
  'render.chip.reminder': 'Przypomnienie',
  'render.chip.alarm': 'Alarm',
  'render.chip.recurrence': 'Powtarzanie',
  'render.chip.overdue': 'Po terminie',
  'render.priority.highest': 'Priorytet: najwyższy',
  'render.priority.high': 'Priorytet: wysoki',
  'render.priority.medium': 'Priorytet: średni',
  'render.priority.low': 'Priorytet: niski',
  'render.priority.lowest': 'Priorytet: najniższy',
  'render.calc.error': 'Błąd obliczenia',
  'render.properties.yes': 'tak',
  'render.properties.no': 'nie',
} as const;

export type RenderMessageKey = keyof typeof pl;

const en: Record<RenderMessageKey, string> = {
  'render.callout.note': 'Note',
  'render.callout.abstract': 'Abstract',
  'render.callout.info': 'Info',
  'render.callout.todo': 'Todo',
  'render.callout.tip': 'Tip',
  'render.callout.success': 'Success',
  'render.callout.question': 'Question',
  'render.callout.warning': 'Warning',
  'render.callout.failure': 'Failure',
  'render.callout.danger': 'Danger',
  'render.callout.bug': 'Bug',
  'render.callout.example': 'Example',
  'render.callout.quote': 'Quote',
  'render.embed.missing': 'Not found: {target}',
  'render.embed.cycle': 'Circular embed: {target}',
  'render.embed.tooDeep': 'Embeds nested too deeply: {target}',
  'render.blockRef.missing': 'Block {id} not found',
  'render.query.title': 'Query',
  'render.footnotes.back': 'Back to text',
  'render.task.todo': 'To do',
  'render.task.doing': 'In progress',
  'render.task.done': 'Done',
  'render.task.cancelled': 'Cancelled',
  'render.task.deferred': 'Deferred',
  'render.chip.due': 'Due',
  'render.chip.scheduled': 'Scheduled',
  'render.chip.start': 'Start',
  'render.chip.created': 'Created',
  'render.chip.done': 'Done',
  'render.chip.cancelled': 'Cancelled',
  'render.chip.reminder': 'Reminder',
  'render.chip.alarm': 'Alarm',
  'render.chip.recurrence': 'Repeats',
  'render.chip.overdue': 'Overdue',
  'render.priority.highest': 'Priority: highest',
  'render.priority.high': 'Priority: high',
  'render.priority.medium': 'Priority: medium',
  'render.priority.low': 'Priority: low',
  'render.priority.lowest': 'Priority: lowest',
  'render.calc.error': 'Calculation error',
  'render.properties.yes': 'yes',
  'render.properties.no': 'no',
};

/** Built-in renderer messages in the `MessageModule` shape of `@/core/i18n`. */
export const messages: { pl: Record<RenderMessageKey, string>; en: Record<RenderMessageKey, string> } = { pl, en };

function interpolate(template: string, params?: Record<string, string | number>): string {
  if (!params) return template;
  return template.replace(/\{(\w+)\}/g, (m, name: string) => (Object.hasOwn(params, name) ? String(params[name]) : m));
}

/**
 * Translate `key`: the host translator wins when it knows the key (returns something other than
 * the key itself), otherwise the built-in text for `locale` (unknown locales fall back to Polish).
 */
export function translate(
  locale: Locale,
  key: RenderMessageKey,
  params?: Record<string, string | number>,
  t?: RenderTranslator,
): string {
  if (t) {
    try {
      const s = t(key, params);
      if (typeof s === 'string' && s !== key && s !== '') return s;
    } catch {
      // A broken host translator must never break rendering.
    }
  }
  const dict = locale === 'en' ? messages.en : messages.pl;
  return interpolate(dict[key], params);
}
