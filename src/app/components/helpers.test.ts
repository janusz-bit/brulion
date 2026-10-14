/** Pure helpers of the smaller components: positioning, windowing, keyboard logic. */
import { describe, expect, it } from 'vitest';
import { formatShortcut, splitShortcut } from './Kbd';
import { menuNavIndex, typeaheadIndex } from './Menu';
import { computePosition } from './position';
import { clampSize, keyResize } from './Splitter';
import { rovingIndex } from './Tabs';
import { fixedRange, OffsetIndex, scrollTargetFor, visibleRange } from './virtual';

const VIEW = { width: 400, height: 800 };

describe('computePosition', () => {
  it('places below the anchor, start-aligned', () => {
    const p = computePosition({ top: 100, left: 50, width: 80, height: 30 }, { width: 200, height: 150 }, VIEW, 'bottom-start', 6);
    expect(p).toMatchObject({ top: 136, left: 50, placement: 'bottom-start' });
  });

  it('flips to the top when there is no room below', () => {
    const p = computePosition({ top: 700, left: 50, width: 80, height: 30 }, { width: 200, height: 150 }, VIEW, 'bottom-start', 6);
    expect(p.placement).toBe('top-start');
    expect(p.top).toBe(700 - 6 - 150);
  });

  it('keeps the preferred side when the other one is even smaller', () => {
    const p = computePosition({ top: 380, left: 50, width: 80, height: 30 }, { width: 200, height: 600 }, VIEW, 'bottom-start', 6, 8);
    expect(p.placement).toBe('bottom-start');
    // Clamped into the viewport.
    expect(p.top).toBe(800 - 600 - 8);
  });

  it('clamps horizontally (phone width) and aligns end/center', () => {
    const p = computePosition({ top: 100, left: 350, width: 40, height: 30 }, { width: 300, height: 100 }, VIEW, 'bottom-start');
    expect(p.left).toBe(400 - 300 - 8);
    const end = computePosition({ top: 100, left: 300, width: 80, height: 30 }, { width: 120, height: 100 }, VIEW, 'bottom-end');
    expect(end.left).toBe(260);
    const center = computePosition({ top: 100, left: 100, width: 100, height: 30 }, { width: 60, height: 40 }, VIEW, 'top');
    expect(center).toMatchObject({ left: 120, placement: 'top' });
  });

  it('flips horizontally for side placements', () => {
    const p = computePosition({ top: 100, left: 300, width: 80, height: 30 }, { width: 150, height: 100 }, VIEW, 'right-start');
    expect(p.placement).toBe('left-start');
    expect(p.left).toBe(300 - 6 - 150);
  });

  it('reports the available height on the chosen side', () => {
    const p = computePosition({ top: 100, left: 0, width: 10, height: 20 }, { width: 10, height: 10 }, VIEW, 'bottom-start', 6, 8);
    expect(p.maxHeight).toBe(800 - 120 - 6 - 8);
  });
});

describe('OffsetIndex / ranges', () => {
  it('computes offsets, finds rows and updates measured heights', () => {
    const idx = new OffsetIndex(5, (i) => (i + 1) * 10); // 10,20,30,40,50
    expect(idx.offsetOf(0)).toBe(0);
    expect(idx.offsetOf(3)).toBe(60);
    expect(idx.total()).toBe(150);
    expect(idx.indexAt(0)).toBe(0);
    expect(idx.indexAt(29.9)).toBe(1);
    expect(idx.indexAt(30)).toBe(2);
    expect(idx.indexAt(10_000)).toBe(4);
    expect(idx.setHeight(1, 20)).toBe(false);
    expect(idx.setHeight(1, 100)).toBe(true);
    expect(idx.offsetOf(2)).toBe(110);
    expect(idx.total()).toBe(230);
  });

  it('returns the visible window with overscan', () => {
    const idx = new OffsetIndex(1000, () => 20);
    expect(visibleRange(idx, 0, 100, 0)).toEqual({ start: 0, end: 6 });
    expect(visibleRange(idx, 400, 100, 40)).toEqual({ start: 18, end: 28 });
    expect(visibleRange(new OffsetIndex(0, () => 20), 0, 100)).toEqual({ start: 0, end: 0 });
    expect(fixedRange(1000, 20, 400, 100, 40)).toEqual({ start: 18, end: 27 });
    expect(fixedRange(3, 20, 0, 1000)).toEqual({ start: 0, end: 3 });
    expect(fixedRange(0, 20, 0, 1000)).toEqual({ start: 0, end: 0 });
  });

  it('computes scroll targets', () => {
    expect(scrollTargetFor(500, 20, 0, 100, 'start')).toBe(500);
    expect(scrollTargetFor(500, 20, 0, 100, 'end')).toBe(420);
    expect(scrollTargetFor(500, 20, 0, 100, 'center')).toBe(460);
    expect(scrollTargetFor(50, 20, 0, 100, 'auto')).toBeNull();
    expect(scrollTargetFor(500, 20, 0, 100, 'auto')).toBe(420);
    expect(scrollTargetFor(10, 20, 100, 100, 'auto')).toBe(10);
  });
});

describe('shortcuts', () => {
  it('splits and formats per platform', () => {
    expect(splitShortcut('Mod+Shift+K')).toEqual(['Mod', 'Shift', 'K']);
    expect(splitShortcut('Mod++')).toEqual(['Mod', '+']);
    expect(splitShortcut('+')).toEqual(['+']);
    expect(formatShortcut('Mod+Shift+f', false)).toEqual(['Ctrl', 'Shift', 'F']);
    expect(formatShortcut('Mod+Shift+f', true)).toEqual(['⌘', '⇧', 'F']);
    expect(formatShortcut('Alt+ArrowLeft', false)).toEqual(['Alt', '←']);
    expect(formatShortcut('Escape', false)).toEqual(['Esc']);
    expect(formatShortcut('constructor', false)).toEqual(['constructor']);
  });
});

describe('keyboard navigation helpers', () => {
  it('roves over enabled items with wrap-around', () => {
    const dis = [false, true, false, false];
    expect(rovingIndex('ArrowRight', 0, dis)).toBe(2);
    expect(rovingIndex('ArrowLeft', 0, dis)).toBe(3);
    expect(rovingIndex('Home', 3, dis)).toBe(0);
    expect(rovingIndex('End', 0, [false, false, true])).toBe(1);
    expect(rovingIndex('ArrowDown', 0, dis)).toBeNull();
    expect(rovingIndex('ArrowDown', 0, dis, 'vertical')).toBe(2);
    expect(rovingIndex('ArrowDown', 0, dis, 'both')).toBe(2);
    expect(rovingIndex('ArrowRight', 0, [])).toBeNull();
  });

  it('moves through menu items, skipping disabled ones', () => {
    const en = [true, false, true, true];
    expect(menuNavIndex('ArrowDown', 0, en)).toBe(2);
    expect(menuNavIndex('ArrowDown', 3, en)).toBe(0);
    expect(menuNavIndex('ArrowUp', 0, en)).toBe(3);
    expect(menuNavIndex('Home', 3, en)).toBe(0);
    expect(menuNavIndex('End', 0, [true, true, false])).toBe(1);
    expect(menuNavIndex('x', 0, en)).toBeNull();
  });

  it('type-ahead matches Polish labels without diacritics', () => {
    const texts = ['Otwórz', 'Zmień nazwę', 'Łącza', 'Usuń', 'Źródło'];
    const en = [true, true, true, true, true];
    expect(typeaheadIndex('z', texts, en, -1)).toBe(1);
    expect(typeaheadIndex('z', texts, en, 1)).toBe(4);
    expect(typeaheadIndex('l', texts, en, 0)).toBe(2);
    expect(typeaheadIndex('zr', texts, en, 0)).toBe(4);
    expect(typeaheadIndex('q', texts, en, 0)).toBeNull();
    expect(typeaheadIndex('u', texts, [true, true, true, false, true], 0)).toBeNull();
  });

  it('resizes splitter panes from the keyboard', () => {
    const base = { min: 100, max: 400, step: 16, direction: 'horizontal' as const, fixed: 'first' as const, defaultSize: 240 };
    expect(keyResize(200, 'ArrowRight', base)).toBe(216);
    expect(keyResize(200, 'ArrowLeft', base)).toBe(184);
    expect(keyResize(200, 'ArrowLeft', { ...base, fixed: 'second' })).toBe(216);
    expect(keyResize(395, 'ArrowRight', base)).toBe(400);
    expect(keyResize(200, 'Home', base)).toBe(100);
    expect(keyResize(200, 'End', base)).toBe(400);
    expect(keyResize(200, 'Enter', base)).toBe(240);
    expect(keyResize(200, 'ArrowDown', base)).toBeNull();
    expect(keyResize(200, 'ArrowDown', { ...base, direction: 'vertical' })).toBe(216);
    expect(clampSize(50, 100, 80)).toBe(100);
  });
});
