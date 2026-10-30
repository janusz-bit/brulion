// The pen in the web build's drawing editor (docs/WEB.md "Rysik (nacisk)"): pointer events with
// pointerType "pen" → Qt for WebAssembly → QTabletEvent → DrawingCanvas. The app reports its
// canvas (?selftest=pen: src/app/diag/webselftest.h) — the freehand lines with their pressures
// and where they are on screen; the screenshot shows the line growing thicker; the drawing file
// keeps the pressures.
//
//  1. Chromium: a trusted pen from DevTools (Input.dispatchMouseEvent pointerType "pen" with
//     `force`): hovering draws nothing, a pressure ramp is kept (and drawn thicker where pressed
//     harder), one constant pressure counts as none, a mouse line has none, the barrel button
//     erases, the file holds the pressures;
//  2. Chromium and Firefox (Playwright has no pen for Firefox): pen events made by the page — a
//     ramp, pressures out of range (clamped), the eraser end (button 5) erases, on empty paper it
//     draws nothing, a mouse line has no pressure.
import { expect, test } from './lib/fixtures.mjs';
import {
  cdpPen, decodeDrawing, inkThickness, installSyntheticPen, lightShare, line, mouseStroke, openApp, png,
  requireWebGl2, screenOffset, syntheticStroke, waitGlobal,
} from './lib/brulion.mjs';
import { readFile, seed } from './lib/idbfs.mjs';

const DRAWING = 'Rysunki/Nacisk rysika — zażółć.svg';
const ROUTE = { view: 'drawing', path: DRAWING };

/** Opens the new (empty) drawing and returns its geometry on the page. */
async function openDrawing(page, origin) {
  await seed(page, origin, { files: { [DRAWING]: Buffer.alloc(0) } }); // 0 bytes: a new drawing
  await openApp(page, origin, { selftest: 'pen', route: ROUTE });
  const report = await waitGlobal(page, 'brulionPenSelfTest', (v) => v.found && v.editable, 150_000);
  expect(report.strokeCount).toBe(0);
  const off = await screenOffset(page);
  const c = report.canvas;
  const geo = {
    report,
    x0: off.x + c.x + 0.25 * c.width,
    x1: off.x + c.x + 0.75 * c.width,
    xm: off.x + c.x + 0.5 * c.width,
    cy: (f) => off.y + c.y + f * c.height,
    off,
  };
  // The canvas has painted the paper (and nothing on it) where the lines will go.
  await expect.poll(async () => {
    const shot = png(await page.screenshot());
    return Math.min(...[0.35, 0.5, 0.65, 0.8].map((f) => lightShare(shot, geo.x0, geo.x1, geo.cy(f) - 30, geo.cy(f) + 30)));
  }, { message: 'empty paper under the lines to be drawn', timeout: 30_000 }).toBeGreaterThanOrEqual(0.98);
  return geo;
}

/** The pen report once it has `n` freehand lines. */
function strokes(page, n, timeout = 15_000) {
  return waitGlobal(page, 'brulionPenSelfTest', `(v) => v.strokeCount === ${n}`, timeout);
}

/** The stroke's bounds (window coordinates) are where the path went, within `tol` px. */
function expectAlong(stroke, off, x0, y0, x1, y1, tol = 8) {
  const b = stroke.bounds;
  expect(Math.abs(b.x + off.x - Math.min(x0, x1)), 'left').toBeLessThanOrEqual(tol);
  expect(Math.abs(b.x + b.width + off.x - Math.max(x0, x1)), 'right').toBeLessThanOrEqual(tol);
  expect(Math.abs(b.y + off.y - Math.min(y0, y1)), 'top').toBeLessThanOrEqual(tol);
  expect(Math.abs(b.y + b.height + off.y - Math.max(y0, y1)), 'bottom').toBeLessThanOrEqual(tol);
}

/** The freehand lines the drawing file holds, once it holds `n` (the editor saves 1.2 s after the
 *  last change, IndexedDB follows). */
async function savedStrokes(page, n) {
  let lines = [];
  await expect.poll(async () => {
    const bytes = await readFile(page, DRAWING);
    if (!bytes || !bytes.length) return -1;
    const scene = decodeDrawing(bytes);
    lines = (scene?.elements || []).filter((e) => e.type === 'freedraw' && !e.isDeleted);
    return lines.length;
  }, { message: 'freehand lines in the saved drawing', timeout: 25_000 }).toBe(n);
  return lines;
}

test.describe('Rysik w przeglądarce', () => {
  test('nacisk rysika z DevTools (Chromium)', async ({ page, servers, watched, browserName }, testInfo) => {
    test.skip(browserName !== 'chromium', 'only Chromium has a trusted pen in Playwright (test 2: page events)');
    void watched;
    const origin = servers.page.origin;
    await requireWebGl2(page, origin, browserName);
    const g = await openDrawing(page, origin);
    const pen = await cdpPen(page);

    // Hovering draws nothing.
    for (const pt of line(g.x0, g.cy(0.2), g.x1, g.cy(0.2), 12)) await pen.hover(pt.x, pt.y);
    await page.waitForTimeout(500);
    expect((await waitGlobal(page, 'brulionPenSelfTest')).strokeCount).toBe(0);

    // A: pressure 0.1 → 0.9.
    await pen.stroke(line(g.x0, g.cy(0.35), g.x1, g.cy(0.35), 40, 0.1, 0.9));
    let r = await strokes(page, 1);
    const a = r.strokes[0];
    expect(r.stylusSeen).toBe(true);
    expect(a).toMatchObject({ tool: 'pen', simulatePressure: false });
    expect(a.pressureMin).toBeLessThanOrEqual(0.15);
    expect(a.pressureMax).toBeGreaterThanOrEqual(0.85);
    expect(a.pressureFirst).toBeLessThan(a.pressureLast);
    expectAlong(a, g.off, g.x0, g.cy(0.35), g.x1, g.cy(0.35));

    // Drawn thicker where pressed harder.
    await page.waitForTimeout(300);
    const shotBytes = await page.screenshot();
    await testInfo.attach('nacisk.png', { body: shotBytes, contentType: 'image/png' });
    const shot = png(shotBytes);
    const y = g.cy(0.35);
    const t25 = inkThickness(shot, g.x0 + 0.25 * (g.x1 - g.x0), y - 20, y + 20);
    const t75 = inkThickness(shot, g.x0 + 0.75 * (g.x1 - g.x0), y - 20, y + 20);
    testInfo.annotations.push({ type: 'ink thickness at 25 % / 75 %', description: `${t25} px / ${t75} px` });
    expect(t75).toBeGreaterThanOrEqual(4);
    expect(t75).toBeGreaterThanOrEqual(1.3 * t25);

    // B: one constant pressure is no pressure.
    await pen.stroke(line(g.x0, g.cy(0.5), g.x1, g.cy(0.5), 40, 0.5, 0.5));
    r = await strokes(page, 2);
    expect(r.strokes[1]).toMatchObject({ tool: 'pen', simulatePressure: true });

    // C: the mouse has none.
    await mouseStroke(page, line(g.x0, g.cy(0.65), g.x1, g.cy(0.65), 40));
    r = await strokes(page, 3);
    expect(r.strokes[2].simulatePressure).toBe(true);

    // The barrel button erases B (and only B).
    await pen.stroke(line(g.xm, g.cy(0.44), g.xm, g.cy(0.56), 20, 0.5, 0.5), 'right');
    r = await strokes(page, 2);
    expect(r.strokes[0].simulatePressure).toBe(false); // A
    expectAlong(r.strokes[1], g.off, g.x0, g.cy(0.65), g.x1, g.cy(0.65)); // C
    await pen.detach();

    // The file keeps A's pressures, one per point, and none for C.
    const saved = await savedStrokes(page, 2);
    const [sa, sc] = saved;
    expect(sa.simulatePressure).toBe(false);
    expect(sa.pressures.length).toBe(sa.points.length);
    expect(Math.min(...sa.pressures)).toBeLessThanOrEqual(0.15);
    expect(Math.max(...sa.pressures)).toBeGreaterThanOrEqual(0.85);
    expect(sc.simulatePressure).toBe(true);
    expect(sc.pressures).toEqual([]);
  });

  test('nacisk i gumka rysika ze zdarzeń strony (Chromium, Firefox)',
    async ({ page, servers, watched, browserName }, testInfo) => {
      void watched;
      const origin = servers.page.origin;
      await requireWebGl2(page, origin, browserName);
      await installSyntheticPen(page);
      const g = await openDrawing(page, origin);

      // D: pressure 0.2 → 1.0.
      await syntheticStroke(page, line(g.x0, g.cy(0.35), g.x1, g.cy(0.35), 40, 0.2, 1.0));
      let r = await strokes(page, 1);
      const d = r.strokes[0];
      expect(r.stylusSeen).toBe(true);
      expect(d).toMatchObject({ tool: 'pen', simulatePressure: false });
      expect(d.pressureMax).toBeGreaterThanOrEqual(0.95);
      expect(d.pressureMin).toBeLessThanOrEqual(0.25);
      expectAlong(d, g.off, g.x0, g.cy(0.35), g.x1, g.cy(0.35));

      // E (malformed): pressures 1.7 and −0.3 by turns — kept within 0…1.
      await syntheticStroke(page, line(g.x0, g.cy(0.5), g.x1, g.cy(0.5), 30, 0, 0, (i) => (i % 2 ? -0.3 : 1.7)));
      r = await strokes(page, 2);
      const e = r.strokes[1];
      expect(e.pressureMin).toBeGreaterThanOrEqual(0);
      expect(e.pressureMax).toBeLessThanOrEqual(1);
      testInfo.annotations.push({ type: 'out-of-range pressures', description: JSON.stringify(e) });

      // The eraser end (button 5) across D erases it.
      await syntheticStroke(page, line(g.xm, g.cy(0.29), g.xm, g.cy(0.41), 20, 0.5, 0.5), { eraser: true });
      r = await strokes(page, 1);
      expectAlong(r.strokes[0], g.off, g.x0, g.cy(0.5), g.x1, g.cy(0.5)); // E is left

      // On empty paper it draws nothing.
      await syntheticStroke(page, line(g.x0, g.cy(0.8), g.x1, g.cy(0.8), 20, 0.3, 0.9), { eraser: true });
      await page.waitForTimeout(700);
      expect((await waitGlobal(page, 'brulionPenSelfTest')).strokeCount).toBe(1);

      // The mouse: no pressure.
      await mouseStroke(page, line(g.x0, g.cy(0.65), g.x1, g.cy(0.65), 40));
      r = await strokes(page, 2);
      expect(r.strokes[1].simulatePressure).toBe(true);

      const saved = await savedStrokes(page, 2);
      expect(saved[0].pressures.every((v) => v >= 0 && v <= 1)).toBe(true);
    });
});
