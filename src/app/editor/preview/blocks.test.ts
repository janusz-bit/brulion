import { ensureSyntaxTree } from '@codemirror/language';
import { EditorSelection, EditorState } from '@codemirror/state';
import { describe, expect, it } from 'vitest';
import { brulionLanguage } from '../setup';
import { blockPreview, calloutCollapsed, scanBlocks, setFrontmatterOpen, toggleCallout } from './blocks';
import { previewState, setPreviewFocus } from './state';

const DOC = ['---', 'tags: [dom]', '---', '# Tytuł', '', '| a | b |', '| - | - |', '| 1 | =A1*2 |', '', '```query', 'type:tasks', '```', '', '> [!tip]- Zwinięty', '> treść', '', '- lista', '\t| x | y |', '\t| - | - |', '\t| 1 | 2 |'].join('\n');

function state(cursor: number | null): EditorState {
  let s = EditorState.create({ doc: DOC, selection: EditorSelection.cursor(cursor ?? 0), extensions: [brulionLanguage(), previewState(), blockPreview] });
  ensureSyntaxTree(s, s.doc.length, 5000);
  s = s.update({ effects: setPreviewFocus.of(cursor !== null) }).state;
  return s;
}

function replaced(s: EditorState): string[] {
  const out: string[] = [];
  s.field(blockPreview).decorations.between(0, s.doc.length, (from, to, d) => {
    const w = (d.spec as { widget?: { constructor: { name: string } } }).widget;
    out.push(`${w ? w.constructor.name : 'hidden'}:${s.doc.lineAt(from).number}-${s.doc.lineAt(to).number}`);
  });
  return out;
}

describe('scanBlocks', () => {
  it('finds frontmatter, tables (also in lists), queries and foldable callouts', () => {
    const s = state(null);
    expect(scanBlocks(s).map((c) => `${c.kind}:${s.doc.lineAt(c.from).number}-${s.doc.lineAt(c.to).number}`)).toEqual([
      'frontmatter:1-3',
      'table:6-8',
      'query:10-12',
      'callout:14-15',
      'table:18-20',
    ]);
  });
});

describe('block decorations', () => {
  it('replaces everything in an unfocused editor', () => {
    expect(replaced(state(null))).toEqual(['FrontmatterWidget:1-3', 'TableWidget:6-8', 'QueryWidget:10-12', 'hidden:14-15', 'TableWidget:18-20']);
  });

  it('reveals the block the cursor is in', () => {
    const s = state(DOC.indexOf('=A1'));
    expect(replaced(s)).toEqual(['FrontmatterWidget:1-3', 'QueryWidget:10-12', 'hidden:14-15', 'TableWidget:18-20']);
    const q = state(DOC.indexOf('type:tasks'));
    expect(replaced(q)).not.toContain('QueryWidget:10-12');
  });

  it('a collapsed callout opens while the cursor is in its body, and toggles', () => {
    expect(replaced(state(DOC.indexOf('treść')))).not.toContain('hidden:14-15');
    let s = state(null);
    s = s.update({ effects: toggleCallout.of(s.doc.line(14).from) }).state;
    expect(replaced(s)).not.toContain('hidden:14-15');
    s = s.update({ effects: toggleCallout.of(s.doc.line(14).from) }).state;
    expect(replaced(s)).toContain('hidden:14-15');
  });

  it('keeps toggled callouts across edits above them', () => {
    let s = state(null);
    s = s.update({ effects: toggleCallout.of(s.doc.line(14).from) }).state;
    s = s.update({ changes: { from: s.doc.line(4).to, insert: ' i więcej' } }).state;
    expect(replaced(s)).not.toContain('hidden:14-15');
  });

  it('frontmatter opens as a property list', () => {
    let s = state(null);
    expect(s.field(blockPreview).frontmatterOpen).toBe(false);
    s = s.update({ effects: setFrontmatterOpen.of(true) }).state;
    expect(s.field(blockPreview).frontmatterOpen).toBe(true);
  });

  it('keeps the same decoration set when the selection moves within plain text', () => {
    const s = state(DOC.indexOf('Tytuł'));
    const next = s.update({ selection: EditorSelection.cursor(DOC.indexOf('Tytuł') + 2) }).state;
    expect(next.field(blockPreview).decorations).toBe(s.field(blockPreview).decorations);
  });

  it('calloutCollapsed follows the default and the toggles', () => {
    expect(calloutCollapsed({ from: 5, fold: '-' }, [])).toBe(true);
    expect(calloutCollapsed({ from: 5, fold: '-' }, [5])).toBe(false);
    expect(calloutCollapsed({ from: 5, fold: '+' }, [5])).toBe(true);
    expect(calloutCollapsed({ from: 5, fold: '' }, [])).toBe(false);
  });
});
