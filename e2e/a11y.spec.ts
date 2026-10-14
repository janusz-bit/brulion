/**
 * Automated visual-accessibility checks on the component gallery, in light and dark, on the
 * desktop and mobile projects:
 *
 * * text contrast meets WCAG AA (4.5:1, 3:1 for large text) against the composited
 *   background; disabled controls, decorative and screen-reader-only text are exempt;
 * * nothing overflows the page horizontally;
 * * on the touch project every control is at least 24×24 px (WCAG 2.5.8), counting the
 *   invisible ::before hit areas some controls use.
 *
 * Pure DOM measurements: no screenshots, so it is quick. See e2e/README.md.
 */
import { expect, expectScheme, SCHEMES, settle, test } from './harness';

interface ContrastProblem {
  section: string | null;
  text: string;
  className: string;
  ratio: number;
  required: number;
}

for (const scheme of SCHEMES) {
  test.describe(`gallery a11y ${scheme}`, () => {
    test.use({ colorScheme: scheme });

    test('text contrast, overflow and target sizes', { tag: ['@a11y', '@gallery'] }, async ({ page }, testInfo) => {
      await page.goto('gallery.html?theme=system');
      await expect(page).toHaveTitle(/galeria/i);
      await expectScheme(page, scheme);
      await settle(page);

      const contrast = await page.evaluate((): ContrastProblem[] => {
        type RGBA = [number, number, number, number];
        const parse = (c: string): RGBA | null => {
          const m = /rgba?\(([^)]+)\)/.exec(c);
          if (m) {
            const p = m[1]!.split(/[\s,/]+/).filter(Boolean).map(Number);
            return [p[0]!, p[1]!, p[2]!, p[3] ?? 1];
          }
          const s = /color\(srgb ([^)]+)\)/.exec(c);
          if (!s) return null;
          const p = s[1]!.split(/[\s/]+/).filter(Boolean).map(Number);
          return [p[0]! * 255, p[1]! * 255, p[2]! * 255, p[3] ?? 1];
        };
        const over = (top: RGBA, bottom: RGBA): RGBA => {
          const a = top[3];
          return [top[0] * a + bottom[0] * (1 - a), top[1] * a + bottom[1] * (1 - a), top[2] * a + bottom[2] * (1 - a), 1];
        };
        const lum = (c: RGBA) => {
          const f = (v: number) => {
            const x = v / 255;
            return x <= 0.03928 ? x / 12.92 : ((x + 0.055) / 1.055) ** 2.4;
          };
          return 0.2126 * f(c[0]) + 0.7152 * f(c[1]) + 0.0722 * f(c[2]);
        };
        const ratio = (a: RGBA, b: RGBA) => {
          const la = lum(a);
          const lb = lum(b);
          return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
        };
        const background = (el: Element): RGBA => {
          const layers: RGBA[] = [];
          for (let e: Element | null = el; e; e = e.parentElement) {
            const c = parse(getComputedStyle(e).backgroundColor);
            if (c && c[3] > 0) layers.push(c);
            if (c && c[3] >= 1) break;
          }
          let acc: RGBA = [255, 255, 255, 1];
          for (let i = layers.length - 1; i >= 0; i--) acc = over(layers[i]!, acc);
          return acc;
        };
        const opacity = (el: Element) => {
          let o = 1;
          for (let e: Element | null = el; e; e = e.parentElement) o *= Number(getComputedStyle(e).opacity);
          return o;
        };
        const out: ContrastProblem[] = [];
        const seen = new Set<string>();
        const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
        for (let n = walker.nextNode(); n; n = walker.nextNode()) {
          const text = n.textContent?.trim();
          const el = n.parentElement;
          if (!text || !el) continue;
          // Exempt: screen-reader-only, decorative, hidden tooltips, disabled controls.
          if (el.closest('.b-sr-only, [aria-hidden="true"], .b-tooltip__bubble, .b-gallery__bar')) continue;
          if (el.closest(':disabled, .is-disabled, [aria-disabled="true"]')) continue;
          const rect = el.getBoundingClientRect();
          const cs = getComputedStyle(el);
          if (rect.width === 0 || rect.height === 0 || cs.visibility === 'hidden') continue;
          const op = opacity(el);
          if (op < 0.6) continue; // dimmed = disabled styling
          const fg = parse(cs.color);
          if (!fg) continue;
          const bg = background(el);
          const r = ratio(over([fg[0], fg[1], fg[2], fg[3] * op], bg), bg);
          const size = parseFloat(cs.fontSize);
          const large = size >= 24 || (size >= 18.66 && Number(cs.fontWeight) >= 700);
          const required = large ? 3 : 4.5;
          if (r >= required) continue;
          const section = el.closest('[data-section]')?.getAttribute('data-section') ?? null;
          const key = `${section}|${el.className}|${cs.color}`;
          if (seen.has(key)) continue;
          seen.add(key);
          out.push({ section, text: text.slice(0, 40), className: String(el.className), ratio: Math.round(r * 100) / 100, required });
        }
        return out;
      });
      expect(contrast, 'text below WCAG AA contrast').toEqual([]);

      const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
      expect(overflow, 'horizontal overflow of the page (px)').toBeLessThanOrEqual(0);

      if (testInfo.project.use.hasTouch) {
        const small = await page.evaluate(() => {
          const out: string[] = [];
          const sel = 'button, input:not([type="hidden"]), select, textarea, summary, [role="switch"], [role="separator"][tabindex]';
          for (const el of document.querySelectorAll<HTMLElement>(sel)) {
            if (el.closest('.b-gallery__bar, [aria-hidden="true"], [inert]')) continue;
            const r = el.getBoundingClientRect();
            if (r.width === 0 || r.height === 0) continue;
            let w = r.width;
            let h = r.height;
            // Invisible hit areas: absolutely positioned ::before with negative insets.
            const b = getComputedStyle(el, '::before');
            if (b.content !== 'none' && b.position === 'absolute') {
              const [top, right, bottom, left] = [b.top, b.right, b.bottom, b.left].map(parseFloat);
              if (Number.isFinite(top) && Number.isFinite(bottom)) h = Math.max(h, h - top! - bottom!);
              if (Number.isFinite(left) && Number.isFinite(right)) w = Math.max(w, w - left! - right!);
            }
            // A checkbox's label is part of its target.
            if (el instanceof HTMLInputElement && el.labels?.length) continue;
            if (w < 24 || h < 24) {
              const name = el.getAttribute('aria-label') ?? el.textContent?.trim() ?? el.className;
              out.push(`${el.tagName.toLowerCase()}.${el.className} "${name.slice(0, 30)}" ${Math.round(w)}×${Math.round(h)}`);
            }
          }
          return [...new Set(out)];
        });
        expect(small, 'touch targets smaller than 24×24 px').toEqual([]);
      }
    });
  });
}
