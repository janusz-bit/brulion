// @vitest-environment jsdom
import { describe, expect, it } from 'vitest';
import { renderHref } from './embeds';
import { blockHref, pageHref, parseRenderedHref, plainInline, renderedLinkOf, renderInlineSafe, renderMarkdownSafe, tagHref, type RenderContext } from './render';

const ctx: RenderContext = {
  path: 'a.md',
  locale: 'pl',
  resolveLink: (t) => (t === 'Dom' ? 'Projekty/Dom.md' : null),
  href: renderHref,
  assetUrl: () => null,
  blockRef: (id) => (id === 'abc' ? { path: 'b.md', text: 'Treść **bloku**' } : null),
};

describe('plainInline', () => {
  it('strips inline Markdown', () => {
    expect(plainInline('**Zażółć** [[Projekty/Gęślą jaźń|jaźń]] i [[Dom#Kuchnia]] `kod` ==mark== [x](http://a)')).toBe('Zażółć jaźń i Dom › Kuchnia kod mark x');
    expect(plainInline('zadanie ^abc123')).toBe('zadanie');
    expect(plainInline('%%ukryte%% widoczne')).toBe('widoczne');
  });

  it('truncates long text', () => {
    expect(plainInline('a'.repeat(200), 10)).toBe(`${'a'.repeat(9)}…`);
  });
});

describe('rendered hrefs', () => {
  it('round-trips page, block and tag links', () => {
    expect(parseRenderedHref(pageHref('Projekty/Dom#Kuchnia'))).toEqual({ kind: 'page', target: 'Projekty/Dom#Kuchnia' });
    expect(parseRenderedHref(blockHref('abc'))).toEqual({ kind: 'block', id: 'abc' });
    expect(parseRenderedHref(tagHref('#ogród'))).toEqual({ kind: 'tag', tag: 'ogród' });
    expect(parseRenderedHref('https://example.com')).toEqual({ kind: 'url', url: 'https://example.com' });
    expect(parseRenderedHref('Inna%20strona.md')).toEqual({ kind: 'page', target: 'Inna strona.md' });
    expect(parseRenderedHref('javascript:alert(1)')).toBeNull();
    expect(parseRenderedHref('#kotwica')).toBeNull();
  });

  it('builds hrefs from resolved paths, tags and fragments', () => {
    expect(renderHref(null, '#ogród')).toBe(tagHref('ogród'));
    expect(renderHref('Projekty/Dom.md', 'Dom', 'Kuchnia')).toBe(pageHref('Projekty/Dom#Kuchnia'));
    expect(renderHref('Projekty/Dom.md', 'Dom', undefined, 'x1')).toBe(pageHref('Projekty/Dom#^x1'));
    expect(renderHref(null, 'Nowa strona')).toBe(pageHref('Nowa strona'));
  });

  it('finds what a click on rendered markup opens', () => {
    const div = document.createElement('div');
    div.innerHTML = renderMarkdownSafe('[[Dom|mój dom]] #ogród ((abc)) [x](https://example.com)', ctx);
    expect(renderedLinkOf(div.querySelector('a.wikilink')!)).toEqual({ kind: 'page', target: 'Projekty/Dom' });
    expect(renderedLinkOf(div.querySelector('.tag')!)).toEqual({ kind: 'tag', tag: 'ogród' });
    expect(renderedLinkOf(div.querySelector('.blockref strong') ?? div.querySelector('.blockref')!)).toEqual({ kind: 'block', id: 'abc' });
    expect(renderedLinkOf(div.querySelector('a[href^="https"]')!)).toEqual({ kind: 'url', url: 'https://example.com' });
    expect(renderedLinkOf(div)).toBeNull();
  });
});

describe('Markdown rendering', () => {
  it('renders the dialect through @/core/render, sanitised', () => {
    const html = renderMarkdownSafe('# Tytuł\n\n<img src=x onerror="alert(1)"> **mocny** [[Dom]] [[Brak]]\n- [x] zrobione ✅ 2026-10-10', ctx);
    expect(html).toMatch(/<h1[^>]*>Tytuł<\/h1>/);
    expect(html).toContain('<strong>mocny</strong>');
    expect(html).toContain(`href="${pageHref('Projekty/Dom')}"`);
    expect(html).toMatch(/class="wikilink is-unresolved"/);
    expect(html).toContain('task-done');
    expect(html).not.toContain('onerror');
  });

  it('renders inline Markdown for cells and query rows', () => {
    expect(renderInlineSafe('**Suma** i ==ważne== `x<y`', ctx)).toBe('<strong>Suma</strong> i <mark>ważne</mark> <code>x&lt;y</code>');
    expect(renderInlineSafe('<script>alert(1)</script>żółw', ctx)).not.toContain('<script');
  });
});
