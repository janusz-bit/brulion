import { describe, expect, it } from 'vitest';
import type { WidgetType } from '@codemirror/view';
import { createTranslator } from '@/core/i18n';
import { FrontmatterWidget } from './blocks';
import { EmbedWidget } from './embeds';
import { QueryWidget, type QueryLabels } from './query';
import { TableWidget } from './tables';
import { chipFor } from './tokens';
import {
  BlockRefWidget,
  BulletWidget,
  CalcResultWidget,
  CalloutHeadWidget,
  CheckboxWidget,
  ChipWidget,
  HiddenWidget,
  InlineCalcWidget,
  LinkWidget,
  OrderedWidget,
  PriorityWidget,
  PropertyKeyWidget,
} from './widgets';

const env = { now: new Date(2026, 9, 14, 10, 30), locale: 'pl' as const, t: createTranslator('pl') };
const qLabels: QueryLabels = { title: 'Zapytanie', unavailable: '', empty: '', edit: '', error: '', count: String, toggle: '', open: '' };
const tLabels = { addRow: 'dodaj wiersz', addColumn: 'dodaj kolumnę', align: 'wyrównaj', formula: 'Formuła', table: 'Tabela' };

/** Pairs of [a, same-as-a, different-from-a]. */
function cases(): [string, WidgetType, WidgetType, WidgetType][] {
  const zoom = { zoom: 'Powiększ', fold: 'Zwiń' };
  return [
    ['hidden', new HiddenWidget('**'), new HiddenWidget('**'), new HiddenWidget('~~')],
    ['bullet', new BulletWidget('- ', 1, true, false, zoom), new BulletWidget('- ', 1, true, false, { ...zoom }), new BulletWidget('- ', 1, true, true, zoom)],
    ['ordered', new OrderedWidget('1. ', '1.', false, false, 'Zwiń'), new OrderedWidget('1. ', '1.', false, false, 'Zwiń'), new OrderedWidget('2. ', '2.', false, false, 'Zwiń')],
    ['checkbox', new CheckboxWidget('[ ] ', 'todo', '', 'Zadanie'), new CheckboxWidget('[ ] ', 'todo', '', 'Zadanie'), new CheckboxWidget('[x] ', 'done', '', 'Zadanie')],
    [
      'chip',
      new ChipWidget('📅 2026-10-15', chipFor('due', '2026-10-15', env)),
      new ChipWidget('📅 2026-10-15', chipFor('due', '2026-10-15', env)),
      new ChipWidget('📅 2026-10-16', chipFor('due', '2026-10-16', env)),
    ],
    ['priority', new PriorityWidget('⏫', 'high', 'Wysoki'), new PriorityWidget('⏫', 'high', 'Wysoki'), new PriorityWidget('🔼', 'medium', 'Średni')],
    ['calc', new InlineCalcWidget('`= 2*2`', '4', null), new InlineCalcWidget('`= 2*2`', '4', null), new InlineCalcWidget('`= 2*2`', null, 'błąd')],
    ['calc-result', new CalcResultWidget('42', null, false), new CalcResultWidget('42', null, false), new CalcResultWidget('43', null, false)],
    ['link', new LinkWidget('[[a/B]]', 'B', 'a/B', false, 'a/B'), new LinkWidget('[[a/B]]', 'B', 'a/B', false, 'a/B'), new LinkWidget('[[a/B]]', 'B', 'a/B', true, 'a/B')],
    ['blockref', new BlockRefWidget('((x))', 'x', 'tekst', 't'), new BlockRefWidget('((x))', 'x', 'tekst', 't'), new BlockRefWidget('((x))', 'x', 'nowy tekst', 't')],
    ['property', new PropertyKeyWidget('kwota:: ', 'kwota'), new PropertyKeyWidget('kwota:: ', 'kwota'), new PropertyKeyWidget('cena:: ', 'cena')],
    ['callout', new CalloutHeadWidget('> [!tip] ', 'tip', '', false, false, 'Zwiń'), new CalloutHeadWidget('> [!tip] ', 'tip', '', false, false, 'Zwiń'), new CalloutHeadWidget('> [!tip]- ', 'tip', '', true, true, 'Rozwiń')],
    [
      'embed',
      new EmbedWidget('![[A]]', { kind: 'note', title: 'A', target: 'A', markdown: '- x', path: 'A.md', missingText: '', openLabel: '' }),
      new EmbedWidget('![[A]]', { kind: 'note', title: 'A', target: 'A', markdown: '- x', path: 'A.md', missingText: '', openLabel: '' }),
      new EmbedWidget('![[A]]', { kind: 'note', title: 'A', target: 'A', markdown: '- y', path: 'A.md', missingText: '', openLabel: '' }),
    ],
    ['table', new TableWidget('| a |\n| - |\n| 1 |', 3, 'pl', tLabels, true), new TableWidget('| a |\n| - |\n| 1 |', 10, 'pl', { ...tLabels }, true), new TableWidget('| a |\n| - |\n| 2 |', 3, 'pl', tLabels, true)],
    ['query', new QueryWidget('type:tasks', 1, qLabels, true), new QueryWidget('type:tasks', 1, qLabels, true), new QueryWidget('type:tasks', 2, qLabels, true)],
    ['frontmatter', new FrontmatterWidget('a: 1', false, { title: 'W', edit: '', toggle: '', empty: '' }), new FrontmatterWidget('a: 1', false, { title: 'W', edit: '', toggle: '', empty: '' }), new FrontmatterWidget('a: 1', true, { title: 'W', edit: '', toggle: '', empty: '' })],
  ];
}

describe('widget eq()', () => {
  for (const [name, a, same, other] of cases()) {
    it(`${name}: equal data keeps the DOM, changed data re-renders`, () => {
      expect(a.eq(same)).toBe(true);
      expect(same.eq(a)).toBe(true);
      expect(a.eq(other)).toBe(false);
    });
  }

  it('widgets keep no document positions (a table moved down still compares equal)', () => {
    const a = new TableWidget('| a |\n| - |', 3, 'pl', tLabels, true);
    const b = new TableWidget('| a |\n| - |', 30, 'pl', tLabels, true);
    expect(a.eq(b)).toBe(true);
  });

  it('let native media controls keep their events, everything else goes to the editor', () => {
    const w = new HiddenWidget('x');
    expect(w.ignoreEvent({ target: null } as unknown as Event)).toBe(false);
  });
});
