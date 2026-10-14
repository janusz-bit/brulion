import { Text } from '@codemirror/state';
import { describe, expect, it } from 'vitest';
import { clipSpans, hiddenMarks, mergeSpans, revealDelta, sameSelection, selectedLines, touches, touchesInside } from './reveal';

describe('touches', () => {
  it('counts boundaries as touching (cursor right before / after an element)', () => {
    expect(touches([{ from: 5, to: 5 }], 5, 10)).toBe(true);
    expect(touches([{ from: 10, to: 10 }], 5, 10)).toBe(true);
    expect(touches([{ from: 4, to: 4 }], 5, 10)).toBe(false);
    expect(touches([{ from: 11, to: 11 }], 5, 10)).toBe(false);
  });

  it('handles ranges and several selections', () => {
    expect(touches([{ from: 0, to: 6 }], 5, 10)).toBe(true);
    expect(touches([{ from: 0, to: 1 }, { from: 8, to: 8 }], 5, 10)).toBe(true);
    expect(touches([], 5, 10)).toBe(false);
  });

  it('touchesInside excludes the boundaries', () => {
    expect(touchesInside([{ from: 5, to: 5 }], 5, 10)).toBe(false);
    expect(touchesInside([{ from: 6, to: 6 }], 5, 10)).toBe(true);
    expect(touchesInside([{ from: 0, to: 20 }], 5, 10)).toBe(true);
  });
});

describe('hiddenMarks', () => {
  // "a **pogrubienie** b": strong 2..18, marks 2..4 and 16..18
  const marks = [
    { from: 2, to: 4 },
    { from: 16, to: 18 },
  ];

  it('hides the marks while the cursor is elsewhere', () => {
    expect(hiddenMarks([{ from: 0, to: 0 }], 2, 18, marks)).toEqual(marks);
  });

  it('reveals them when the cursor is inside or next to the element', () => {
    expect(hiddenMarks([{ from: 10, to: 10 }], 2, 18, marks)).toEqual([]);
    expect(hiddenMarks([{ from: 18, to: 18 }], 2, 18, marks)).toEqual([]);
    expect(hiddenMarks([{ from: 2, to: 2 }], 2, 18, marks)).toEqual([]);
  });

  it('reveals nothing in an unfocused editor (no selection) and skips empty marks', () => {
    expect(hiddenMarks([], 2, 18, [...marks, { from: 9, to: 9 }])).toEqual(marks);
  });
});

describe('spans', () => {
  it('merges overlapping and adjacent spans', () => {
    expect(
      mergeSpans([
        { from: 10, to: 20 },
        { from: 0, to: 5 },
        { from: 6, to: 8 },
        { from: 15, to: 30 },
      ]),
    ).toEqual([
      { from: 0, to: 8 },
      { from: 10, to: 30 },
    ]);
  });

  it('clips to the visible ranges', () => {
    expect(clipSpans([{ from: 0, to: 100 }], [{ from: 10, to: 20 }, { from: 50, to: 60 }])).toEqual([
      { from: 10, to: 20 },
      { from: 50, to: 60 },
    ]);
    expect(clipSpans([{ from: 0, to: 5 }], [{ from: 10, to: 20 }])).toEqual([]);
  });

  it('compares selections', () => {
    expect(sameSelection([{ from: 1, to: 2 }], [{ from: 1, to: 2 }])).toBe(true);
    expect(sameSelection([{ from: 1, to: 2 }], [{ from: 1, to: 3 }])).toBe(false);
    expect(sameSelection([], [])).toBe(true);
  });
});

describe('selection lines and rebuild ranges', () => {
  const doc = Text.of(['# Tytuł', '- **zażółć** gęślą', '- jaźń', '', 'koniec']);

  it('lists the lines a selection touches', () => {
    expect([...selectedLines(doc, [{ from: 9, to: 9 }])]).toEqual([2]);
    expect([...selectedLines(doc, [{ from: 0, to: 30 }])]).toEqual([1, 2, 3]);
  });

  it('rebuilds the old and new cursor lines only', () => {
    const l2 = doc.line(2);
    const l5 = doc.line(5);
    expect(revealDelta(doc, [{ from: l2.from + 3, to: l2.from + 3 }], [{ from: l5.from, to: l5.from }])).toEqual([
      { from: l2.from, to: l2.to },
      { from: l5.from, to: l5.to },
    ]);
  });

  it('widens to the enclosing node and merges neighbours', () => {
    const l2 = doc.line(2);
    const l3 = doc.line(3);
    const delta = revealDelta(doc, [{ from: l2.from, to: l2.from }], [{ from: l3.from, to: l3.from }], () => ({ from: l2.from, to: l3.to }));
    expect(delta).toEqual([{ from: l2.from, to: l3.to }]);
  });
});
