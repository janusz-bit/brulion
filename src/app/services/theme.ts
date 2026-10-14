/** Theme, language and editor font on <html> (from the device settings). */
import { detectLocale } from '@/core/i18n';
import type { DeviceSettings } from '../state/settings';

function prefersDark(): boolean {
  return typeof matchMedia === 'function' && matchMedia('(prefers-color-scheme: dark)').matches;
}

/** Resolved theme for a setting ('system' follows the OS). */
export function resolveTheme(theme: DeviceSettings['theme']): 'light' | 'dark' {
  return theme === 'system' ? (prefersDark() ? 'dark' : 'light') : theme;
}

/** Apply theme, language and font settings to the document. */
export function applyDocumentSettings(s: DeviceSettings, doc: Document = document): void {
  const root = doc.documentElement;
  const theme = resolveTheme(s.theme);
  root.dataset.theme = theme;
  root.dataset.editorFont = s.editorFont;
  root.lang = s.locale ?? detectLocale();
  root.style.setProperty('--editor-font-size', `${s.fontSize / 16}rem`);
  const meta = doc.querySelector('meta[name="theme-color"]');
  meta?.setAttribute('content', theme === 'dark' ? '#1c2130' : '#fbf7ee');
}
