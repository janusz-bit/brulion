/**
 * Templates (docs/SYNTAX.md §12): `{{date}}`, `{{date:FORMAT}}`, `{{time}}`, `{{time:FORMAT}}`,
 * `{{title}}`, `{{weekday}}`, `{{cursor}}`.
 */
import { weekdayName } from '@/core/dates';
import type { Locale } from '../types';
import { formatDate } from './dateformat';

const VAR_RE = /\{\{[ \t]*([A-Za-z]+)[ \t]*(?::([^}\r\n]*))?\}\}/g;

/** Variables available to {@link applyTemplate}. */
export interface TemplateVars {
  now: Date;
  title: string;
  locale: Locale;
}

/** Replacement for one variable, or null to keep the placeholder as written. */
function resolveVar(name: string, arg: string | undefined, vars: TemplateVars): string | null {
  switch (name) {
    case 'date':
      return formatDate(vars.now, arg || 'yyyy-MM-dd', vars.locale);
    case 'time':
      return formatDate(vars.now, arg || 'HH:mm', vars.locale);
    case 'title':
      return vars.title;
    case 'weekday':
      return weekdayName(vars.now.getDay(), vars.locale);
    default:
      return null;
  }
}

/**
 * Fill a template. `{{date:FORMAT}}` / `{{time:FORMAT}}` take date-fns tokens (Moment.js
 * formats from Obsidian templates are converted) and use Polish month/weekday names for
 * locale `pl`. `{{cursor}}` is removed; `cursor` is the offset of its first occurrence in the
 * result (null when absent). Unknown variables and invalid formats are left as written.
 * Variable names are case-insensitive.
 */
export function applyTemplate(template: string, vars: TemplateVars): { text: string; cursor: number | null } {
  let out = '';
  let last = 0;
  let cursor: number | null = null;
  for (const m of template.matchAll(VAR_RE)) {
    const name = m[1]!.toLowerCase();
    const arg = m[2]?.trim();
    out += template.slice(last, m.index);
    last = m.index + m[0].length;
    if (name === 'cursor') {
      cursor ??= out.length;
      continue;
    }
    out += resolveVar(name, arg, vars) ?? m[0];
  }
  out += template.slice(last);
  return { text: out, cursor };
}
