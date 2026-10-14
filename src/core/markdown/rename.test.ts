import { describe, expect, it } from 'vitest';
import { renameLinksInContent } from './rename';

describe('renameLinksInContent — wiki links', () => {
  it('rewrites plain, aliased, heading, block and embed links', () => {
    const src = '[[Old]] [[Old|alias]] [[Old#Nagłówek]] [[Old#^abc]] ![[Old]] ![[Old#^abc|x]]';
    expect(renameLinksInContent(src, 'Old', 'Nowa strona')).toBe(
      '[[Nowa strona]] [[Nowa strona|alias]] [[Nowa strona#Nagłówek]] [[Nowa strona#^abc]] ![[Nowa strona]] ![[Nowa strona#^abc|x]]',
    );
  });

  it('matches case-insensitively and with Polish letters (NFC)', () => {
    expect(renameLinksInContent('[[żółw]] [[ŻÓŁW|Ż]]', 'Żółw', 'Kot')).toBe('[[Kot]] [[Kot|Ż]]');
    expect(renameLinksInContent('[[żółw]]', 'żółw', 'Kot')).toBe('[[Kot]]');
  });

  it('keeps spacing, .md suffixes and escaped table pipes', () => {
    expect(renameLinksInContent('[[ Old ]] [[Old.md]] | [[Old\\|a]] |', 'Old', 'New')).toBe('[[ New ]] [[New.md]] | [[New\\|a]] |');
  });

  it('rewrites path-qualified links when paths are given', () => {
    const src = '[[Projekty/Old]] [[projekty/old#h|x]] [[Inne/Old]] [[Old]]';
    expect(renameLinksInContent(src, 'Old', 'New', 'Projekty/Old.md', 'Archiwum/New.md')).toBe(
      '[[Archiwum/New]] [[Archiwum/New#h|x]] [[Inne/Old]] [[New]]',
    );
    // A plain rename keeps the written (partial) path prefix.
    expect(renameLinksInContent('[[Dom/Old]]', 'Old', 'New', 'Projekty/Dom/Old.md')).toBe('[[Dom/New]]');
  });

  it('handles a move without a rename (only path-qualified links change)', () => {
    expect(renameLinksInContent('[[a/Note]] [[Note]]', 'Note', 'Note', 'a/Note.md', 'b/Note.md')).toBe('[[b/Note]] [[Note]]');
  });

  it('does not touch other pages, same-page links, code or comments', () => {
    const src = '[[Older]] [[#Old]] `[[Old]]` %% [[Old]] %%\n```\n[[Old]]\n```\n    [[Old]]';
    expect(renameLinksInContent(src, 'Old', 'New')).toBe(src.replace('    [[Old]]', '    [[New]]'));
  });
});

describe('renameLinksInContent — markdown links', () => {
  it('rewrites relative .md links, encoding spaces and keeping fragments', () => {
    expect(renameLinksInContent('[x](Old.md) ![i](Old.md#^b) [y](./Old.md#Sekcja%201)', 'Old', 'Nowa nazwa')).toBe(
      '[x](Nowa%20nazwa.md) ![i](Nowa%20nazwa.md#^b) [y](./Nowa%20nazwa.md#Sekcja%201)',
    );
  });

  it('rewrites path-qualified and percent-encoded links', () => {
    const src = '[a](folder/Old%20Name.md) [b](<folder/Old Name.md>) [c](other/Old%20Name.md)';
    expect(renameLinksInContent(src, 'Old Name', 'New', 'folder/Old Name.md', 'dest/New.md')).toBe(
      '[a](dest/New.md) [b](<dest/New.md>) [c](other/Old%20Name.md)',
    );
  });

  it('leaves external URLs and other file types alone', () => {
    const src = '[a](https://x.pl/Old.md) [b](Old.png) [c](mailto:Old)';
    expect(renameLinksInContent(src, 'Old', 'New')).toBe(src);
  });
});

describe('renameLinksInContent — misc', () => {
  it('returns the same string when nothing changes', () => {
    const src = 'Brak linków.\r\n[[Inna]]';
    expect(renameLinksInContent(src, 'Old', 'New')).toBe(src);
    expect(renameLinksInContent('[[Old]]', '', 'New')).toBe('[[Old]]');
    expect(renameLinksInContent('[[Old]]', 'Old', 'Old')).toBe('[[Old]]');
  });

  it('preserves CRLF and frontmatter links', () => {
    const src = '---\r\nup: "[[Old]]"\r\n---\r\n- [[Old]]\r\n';
    expect(renameLinksInContent(src, 'Old', 'New')).toBe('---\r\nup: "[[New]]"\r\n---\r\n- [[New]]\r\n');
  });
});

describe('renameLinksInContent — keeps the written link form (review regressions)', () => {
  it('keeps relative and partial path prefixes on an in-place rename', () => {
    expect(
      renameLinksInContent('[d](../folder/Old%20Name.md#H) [e](./Old%20Name.md)', 'Old Name', 'Nowa nazwa', 'folder/Old Name.md', 'folder/Nowa nazwa.md'),
    ).toBe('[d](../folder/Nowa%20nazwa.md#H) [e](./Nowa%20nazwa.md)');
    expect(renameLinksInContent('[[b/Old]] [[a/b/Old|x]] ![[b/Old#^id]]', 'Old', 'Nowy', 'a/b/Old.md', 'a/b/Nowy.md')).toBe(
      '[[b/Nowy]] [[a/b/Nowy|x]] ![[b/Nowy#^id]]',
    );
  });

  it('writes the full new path when the page moves to another folder', () => {
    expect(renameLinksInContent('[[b/Old]] [x](b/Old.md)', 'Old', 'Nowy', 'a/b/Old.md', 'c/Nowy.md')).toBe(
      '[[c/Nowy]] [x](c/Nowy.md)',
    );
  });

  it('never writes empty link targets', () => {
    expect(renameLinksInContent('[[Old]] [x](Old.md)', 'Old', '')).toBe('[[Old]] [x](Old.md)');
    expect(renameLinksInContent('[[Old]]', 'Old', '   ')).toBe('[[Old]]');
  });
});
