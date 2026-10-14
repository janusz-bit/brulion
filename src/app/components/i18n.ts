/**
 * Locale plumbing for the shared components. The shell provides the current locale with
 * `<LocaleContext.Provider value={locale}>`; without a provider components speak Polish.
 */
import { createContext, useContext } from 'react';
import { createTranslator, type Translator } from '@/core/i18n';
import type { Locale } from '@/core/types';

export const LocaleContext = createContext<Locale>('pl');

const translators = new Map<Locale, Translator>();

/** Cached translator for a locale (translators are pure, so one per locale is enough). */
export function translatorFor(locale: Locale): Translator {
  let t = translators.get(locale);
  if (!t) {
    t = createTranslator(locale);
    translators.set(locale, t);
  }
  return t;
}

/** Current UI locale ('pl' by default). */
export function useLocale(): Locale {
  return useContext(LocaleContext);
}

/** Translator for the current UI locale. */
export function useT(): Translator {
  return translatorFor(useContext(LocaleContext));
}
