import { buildTree } from './FileTree';

describe('buildTree', () => {
  it('nests files in folders, folders first, natural Polish order', () => {
    const tree = buildTree(['b.md', 'Projekty/Dom.md', 'a.md', 'Projekty/Ćwiczenia.md', 'Projekty/Archiwum/2025.md', 'Zadanie 10.md', 'Zadanie 2.md'], ['puste']);
    expect(tree.children.map((n) => `${n.kind}:${n.name}`)).toEqual([
      'dir:Projekty',
      'dir:puste',
      'file:a.md',
      'file:b.md',
      'file:Zadanie 2.md',
      'file:Zadanie 10.md',
    ]);
    const projekty = tree.children[0]!;
    expect(projekty.children.map((n) => n.name)).toEqual(['Archiwum', 'Ćwiczenia.md', 'Dom.md']);
    expect(projekty.children[0]!.children[0]!.path).toBe('Projekty/Archiwum/2025.md');
  });

  it('handles an empty vault', () => {
    expect(buildTree([]).children).toEqual([]);
  });
});
