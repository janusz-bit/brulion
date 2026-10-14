/**
 * Shared e2e helpers. Specs import `test` / `expect` from here instead of '@playwright/test':
 * every test then fails on `console.error` output or uncaught page exceptions.
 */
import { mkdir, readdir, rm, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { test as base, expect, type Page, type TestInfo } from '@playwright/test';
import { cropPng, decodePng } from './png';

export { expect };

export const SCHEMES = ['light', 'dark'] as const;
export type Scheme = (typeof SCHEMES)[number];

interface Fixtures {
  /** Console errors that are expected in a test (matched against the message text). */
  allowedErrors: RegExp[];
  /** Problems collected so far; asserted to be empty when the test ends. */
  pageProblems: string[];
}

export const test = base.extend<Fixtures>({
  allowedErrors: [[], { option: true }],
  pageProblems: [
    async ({ page, allowedErrors }, use) => {
      const problems: string[] = [];
      const allowed = (text: string) => allowedErrors.some((re) => re.test(text));
      page.on('console', (msg) => {
        if (msg.type() !== 'error' || allowed(msg.text())) return;
        const { url, lineNumber } = msg.location();
        problems.push(`console.error: ${msg.text()}${url ? ` (${url}:${lineNumber})` : ''}`);
      });
      page.on('pageerror', (err) => {
        if (!allowed(err.message)) problems.push(`pageerror: ${err.stack ?? err.message}`);
      });
      await use(problems);
      expect(problems, 'console errors / uncaught exceptions on the page').toEqual([]);
    },
    { auto: true },
  ],
});

/** Children of #root that render boxes (a leading <style>/<script> is never "visible"). */
export const RENDERED_CHILD = '#root > :not(style, script, link, template)';

/** Guards against a silently ignored `colorScheme` emulation (light/dark shots would match). */
export async function expectScheme(page: Page, scheme: Scheme): Promise<void> {
  const dark = await page.evaluate(() => matchMedia('(prefers-color-scheme: dark)').matches);
  expect(dark, `prefers-color-scheme emulated as ${scheme}`).toBe(scheme === 'dark');
}

/** Resolves after two animation frames (layout and paint caught up). */
async function frames(page: Page): Promise<void> {
  await page.evaluate(
    () => new Promise<void>((resolve) => requestAnimationFrame(() => requestAnimationFrame(() => resolve()))),
  );
}

/** Waits until the page is quiet: network idle, web fonts loaded, two frames painted. */
export async function settle(page: Page): Promise<void> {
  await page.waitForLoadState('networkidle');
  await page.evaluate(() => document.fonts.ready.then(() => undefined));
  await frames(page);
}

/** e2e/screenshots — gitignored, recreated by every run. */
export function screenshotDir(testInfo: TestInfo): string {
  return join(testInfo.project.testDir, 'screenshots');
}

export interface CaptureOptions {
  /** Viewport-sized tiles of the full page, readable when a long page is downscaled. Default true. */
  tiles?: boolean;
  /** Also save every element matching this selector as its own image, named after its
   *  `data-section` (else `id`) attribute. */
  sections?: string;
  /** Upper bound for tiles and sections each. Default 40. */
  max?: number;
}

const slug = (s: string) => s.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
const nn = (i: number) => String(i + 1).padStart(2, '0');

/** Tallest full-page capture (CSS px); verified to render correctly in headless Chromium. */
const MAX_CAPTURE_HEIGHT = 32_000;

/**
 * Full-page screenshot `<project>-<scheme>-<name>.png`, plus optional
 * `<project>-<scheme>-<name>.tile-NN.png` and `<project>-<scheme>-<name>.section-NN-<id>.png`.
 *
 * The full page is taken by growing the viewport to the document height and taking a normal
 * screenshot, not with Playwright's `fullPage` option: Chromium's capture-beyond-viewport
 * re-lays the page out without touch emulation, so `(pointer: coarse)` styles vanish on the
 * mobile project and the image doesn't match the real layout. Section boxes are measured in
 * that same layout, and tiles/sections are cropped from the one image (cheap, unlike more
 * screenshots on the Pi). Stale files of the same prefix are removed first. Returns the written
 * paths; the full page is also attached to the test report.
 */
export async function capture(
  page: Page,
  testInfo: TestInfo,
  scheme: Scheme,
  name: string,
  opts: CaptureOptions = {},
): Promise<string[]> {
  const { tiles = true, sections, max = 40 } = opts;
  const dir = screenshotDir(testInfo);
  const stem = `${testInfo.project.name}-${scheme}-${name}`;
  await mkdir(dir, { recursive: true });
  for (const file of await readdir(dir)) {
    if (file === `${stem}.png` || file.startsWith(`${stem}.`)) await rm(join(dir, file), { force: true });
  }

  const viewport = page.viewportSize();
  if (!viewport) throw new Error('capture() needs a fixed viewport');
  const fullPath = join(dir, `${stem}.png`);
  const docHeight = await page.evaluate(() => {
    scrollTo(0, 0);
    return Math.ceil(Math.max(document.documentElement.scrollHeight, document.body.scrollHeight));
  });
  const height = Math.min(MAX_CAPTURE_HEIGHT, Math.max(viewport.height, docHeight));
  if (height < docHeight) {
    testInfo.annotations.push({
      type: 'warning',
      description: `${stem}: page is ${docHeight}px tall, only the first ${height}px were captured`,
    });
  }

  let shot: Buffer;
  let boxes: { label: string; x: number; y: number; w: number; h: number }[] = [];
  let dpr = 1;
  try {
    if (height !== viewport.height) {
      await page.setViewportSize({ width: viewport.width, height });
      await frames(page);
    }
    // Section boxes in document coordinates (CSS px), in the layout being captured.
    if (sections) {
      boxes = await page.locator(sections).evaluateAll((els) =>
        els.map((el, i) => {
          const r = el.getBoundingClientRect();
          const label = el.getAttribute('data-section') || el.id || String(i + 1);
          return { label, x: r.left + scrollX, y: r.top + scrollY, w: r.width, h: r.height };
        }),
      );
    }
    dpr = await page.evaluate(() => devicePixelRatio);
    shot = await page.screenshot({ path: fullPath, animations: 'disabled' });
  } finally {
    if (height !== viewport.height) await page.setViewportSize(viewport);
  }
  await testInfo.attach(`${stem}.png`, { path: fullPath, contentType: 'image/png' });
  const written = [fullPath];

  const img = tiles || boxes.length ? decodePng(shot) : null;
  if (!img) return written;
  const save = async (file: string, png: Buffer | null) => {
    if (!png) return;
    const path = join(dir, file);
    await writeFile(path, png);
    written.push(path);
  };

  if (tiles) {
    const tileHeight = Math.round(viewport.height * dpr);
    const count = Math.min(max, Math.ceil(img.height / tileHeight));
    // A page that fits in one viewport needs no tiles.
    for (let i = 0; count > 1 && i < count; i++) {
      await save(`${stem}.tile-${nn(i)}.png`, cropPng(img, 0, i * tileHeight, img.width, tileHeight));
    }
  }

  for (const [i, b] of boxes.slice(0, max).entries()) {
    const png = cropPng(img, b.x * dpr, b.y * dpr, b.w * dpr, b.h * dpr);
    await save(`${stem}.section-${nn(i)}-${slug(b.label) || nn(i)}.png`, png);
  }
  return written;
}
