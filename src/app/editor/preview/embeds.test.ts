import { describe, expect, it } from 'vitest';
import { createTranslator } from '@/core/i18n';
import { contextFor } from '../context';
import { FakeWorkspace } from '../fake-workspace';
import {
  assetCandidates,
  attachmentSpec,
  blockEmbedSpec,
  blockSource,
  dedent,
  headingSection,
  imageSize,
  mediaKind,
  noteEmbedSpec,
  parseEmbedTarget,
  withoutFrontmatter,
} from './embeds';

const labels = { missing: 'brak', open: 'Otwórz' };

describe('embed targets', () => {
  it('splits page, heading, block id and alias', () => {
    expect(parseEmbedTarget('Projekt ogrodu')).toEqual({ page: 'Projekt ogrodu' });
    expect(parseEmbedTarget('Projekt ogrodu#Rabaty')).toEqual({ page: 'Projekt ogrodu', heading: 'Rabaty' });
    expect(parseEmbedTarget('Dom#^abc-12')).toEqual({ page: 'Dom', blockId: 'abc-12' });
    expect(parseEmbedTarget('#Sekcja')).toEqual({ page: '', heading: 'Sekcja' });
    expect(parseEmbedTarget('zdjęcie.png|300')).toEqual({ page: 'zdjęcie.png', alias: '300' });
  });

  it('classifies attachments by extension', () => {
    expect(mediaKind('Strona')).toBe('note');
    expect(mediaKind('Strona.md')).toBe('note');
    expect(mediaKind('plan.PNG')).toBe('image');
    expect(mediaKind('nagranie.m4a')).toBe('audio');
    expect(mediaKind('film.webm')).toBe('video');
    expect(mediaKind('umowa.pdf')).toBe('pdf');
    expect(mediaKind('dane.zip')).toBe('file');
  });

  it('reads image sizes from the alias', () => {
    expect(imageSize('300')).toEqual({ width: 300 });
    expect(imageSize('300x200')).toEqual({ width: 300, height: 200 });
    expect(imageSize('opis zdjęcia')).toBeNull();
    expect(imageSize(undefined)).toBeNull();
  });

  it('lists candidate paths for attachments', () => {
    expect(assetCandidates('plan.png', 'Projekty/Dom.md', { attachmentsFolder: 'assets' })).toEqual(['plan.png', 'Projekty/plan.png', 'assets/plan.png']);
    expect(assetCandidates('assets/a%20b.png', 'x.md', { attachmentsFolder: 'assets' })).toEqual(['assets/a b.png', 'assets/assets/a b.png']);
  });
});

describe('sections', () => {
  const NOTE = ['# Ogród', 'wstęp', '## Rabaty', '- tulipany', '```', '# nie nagłówek', '```', '### Szczegóły', 'x', '## Warzywnik', '- marchew'].join('\n');

  it('extracts a heading section up to the next heading of the same level', () => {
    expect(headingSection(NOTE, 'rabaty')).toBe(['## Rabaty', '- tulipany', '```', '# nie nagłówek', '```', '### Szczegóły', 'x'].join('\n'));
    expect(headingSection(NOTE, 'Warzywnik')).toBe('## Warzywnik\n- marchew');
    expect(headingSection(NOTE, 'Brak')).toBeNull();
  });

  it('dedents blocks and strips block ids', () => {
    expect(dedent('\t- a\n\t\t- b')).toBe('- a\n\t- b');
    expect(blockSource('- x\n\t- zadanie ^abc\n\t\t- dziecko\n- y', { line: 1, endLine: 2 })).toBe('- zadanie\n\t- dziecko');
  });

  it('removes frontmatter', () => {
    expect(withoutFrontmatter('---\ntags: [a]\n---\n# T')).toBe('# T');
    expect(withoutFrontmatter('# T')).toBe('# T');
  });
});

describe('embed specs from the vault', () => {
  const ws = new FakeWorkspace({
    files: {
      'Projekty/Ogród.md': '---\naliases: [Działka]\n---\n# Ogród\n## Rabaty\n- tulipany ^tul\n\t- czerwone\n## Inne\n',
      'notatka.md': 'x',
    },
  });
  const ctx = contextFor({ workspace: ws, t: createTranslator('pl'), locale: 'pl', now: () => new Date(), navigate: () => {}, openLink: () => {} }, 'notatka.md');

  it('embeds a whole page without frontmatter', () => {
    const spec = noteEmbedSpec(ctx, parseEmbedTarget('Ogród'), labels);
    expect(spec).toMatchObject({ kind: 'note', title: 'Ogród', path: 'Projekty/Ogród.md' });
    expect(spec.kind === 'note' && spec.markdown?.startsWith('# Ogród')).toBe(true);
  });

  it('embeds a section and a block', () => {
    const section = noteEmbedSpec(ctx, parseEmbedTarget('Ogród#Rabaty'), labels);
    expect(section).toMatchObject({ title: 'Ogród › Rabaty', target: 'Ogród#Rabaty' });
    expect(section.kind === 'note' && section.markdown).toBe('## Rabaty\n- tulipany ^tul\n\t- czerwone');
    const block = noteEmbedSpec(ctx, parseEmbedTarget('Ogród#^tul'), labels);
    expect(block.kind === 'note' && block.markdown).toBe('- tulipany\n\t- czerwone');
    const ref = blockEmbedSpec(ctx, 'tul', labels);
    expect(ref).toMatchObject({ title: 'Ogród › tulipany', path: 'Projekty/Ogród.md' });
  });

  it('reports missing targets', () => {
    expect(noteEmbedSpec(ctx, parseEmbedTarget('Nie ma'), labels)).toMatchObject({ markdown: null, missingText: 'brak' });
    expect(blockEmbedSpec(ctx, 'zzz', labels)).toMatchObject({ markdown: null, title: '((zzz))' });
    expect(noteEmbedSpec(null, parseEmbedTarget('Ogród'), labels)).toMatchObject({ markdown: null });
  });

  it('resolves attachments (no blob URLs in node: missing)', () => {
    expect(attachmentSpec(ctx, 'plan.png', '300', labels)).toMatchObject({ kind: 'image', src: null, width: 300, name: 'plan.png' });
    expect(attachmentSpec(ctx, 'https://example.com/a.jpg', 'Opis', labels)).toMatchObject({ src: 'https://example.com/a.jpg', alt: 'Opis' });
    expect(attachmentSpec(ctx, 'umowa.pdf', undefined, labels).kind).toBe('pdf');
  });
});
