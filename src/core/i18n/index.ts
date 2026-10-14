/**
 * Minimal i18n. Messages are split per UI area in ./messages/<area>.ts, each exporting
 * `messages: { pl: Record<string, string>; en: Record<string, string> }`. Keys should be
 * prefixed with the area ("calendar.today"). Files are picked up automatically.
 *
 * Interpolation: "{name}". Plurals: "{count, plural, one {# notatka} few {# notatki} many {# notatek} other {# notatki}}".
 */
import type { Locale } from '../types';

export type Messages = Record<string, string>;
export interface MessageModule {
  messages: { pl: Messages; en: Messages };
}

const modules = import.meta.glob<MessageModule>('./messages/*.ts', { eager: true });

const dictionaries: Record<Locale, Messages> = { pl: {}, en: {} };
for (const mod of Object.values(modules)) {
  Object.assign(dictionaries.pl, mod.messages.pl);
  Object.assign(dictionaries.en, mod.messages.en);
}

export type PluralCategory = 'one' | 'few' | 'many' | 'other';

const rules: Record<Locale, Intl.PluralRules> = {
  pl: new Intl.PluralRules('pl'),
  en: new Intl.PluralRules('en'),
};

export function pluralCategory(locale: Locale, n: number): PluralCategory {
  return rules[locale].select(n) as PluralCategory;
}

/** Pick a plural form. Polish: one (1), few (2-4, 22-24…), many (0, 5-21…), other (fractions). */
export function plural(locale: Locale, n: number, forms: Partial<Record<PluralCategory, string>>): string {
  const cat = pluralCategory(locale, n);
  return (forms[cat] ?? forms.other ?? forms.many ?? '').replace(/#/g, String(n));
}

const PLURAL_RE = /\{(\w+),\s*plural,\s*((?:\w+\s*\{[^{}]*\}\s*)+)\}/g;
const FORM_RE = /(\w+)\s*\{([^{}]*)\}/g;

function format(locale: Locale, template: string, params?: Record<string, string | number>): string {
  if (!params) return template;
  let out = template.replace(PLURAL_RE, (_m, name: string, body: string) => {
    const n = Number(params[name] ?? 0);
    const forms: Partial<Record<PluralCategory, string>> = {};
    for (const f of body.matchAll(FORM_RE)) forms[f[1] as PluralCategory] = f[2]!;
    return plural(locale, n, forms);
  });
  out = out.replace(/\{(\w+)\}/g, (m, name: string) => (name in params ? String(params[name]) : m));
  return out;
}

export type Translator = (key: string, params?: Record<string, string | number>) => string;

export function createTranslator(locale: Locale): Translator {
  return (key, params) => {
    const template = dictionaries[locale][key] ?? dictionaries.pl[key] ?? dictionaries.en[key] ?? key;
    return format(locale, template, params);
  };
}

/** For tests / tooling: keys present in one language but missing in the other. */
export function missingKeys(): { pl: string[]; en: string[] } {
  const pl = Object.keys(dictionaries.en).filter((k) => !(k in dictionaries.pl));
  const en = Object.keys(dictionaries.pl).filter((k) => !(k in dictionaries.en));
  return { pl, en };
}

export function detectLocale(): Locale {
  const lang = typeof navigator !== 'undefined' ? navigator.language : 'pl';
  return lang.toLowerCase().startsWith('en') ? 'en' : 'pl';
}
