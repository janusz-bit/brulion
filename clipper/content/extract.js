// What the clipper reads from a page (docs/CLIPPER.md "What a clip becomes"). Injected into the tab
// with vendor/Readability.js when the user clips (activeTab: only after a click, a menu or a shortcut)
// and run in the extension's isolated world: the page's scripts never see it, nothing of the page
// runs. It reads, never changes the page: Readability works on a copy of the document. Addresses are
// made absolute here, where the page's base is known; lazily loaded pictures get the address the
// page would load. The result goes to Brulion's host, which converts the HTML itself.
/* global Readability */
(() => {
  'use strict';

  /** The largest HTML of an article, a selection or a page (characters). */
  const MAX_HTML = 12 * 1024 * 1024;

  /** The address of the largest `srcset` candidate (by width, else density), or "". */
  function largestSrcset(srcset) {
    let bestWidth = '';
    let bestDensity = '';
    let width = -1;
    let density = -1;
    const text = String(srcset || '');
    let i = 0;
    while (i < text.length) {
      while (i < text.length && /[\s,]/.test(text[i])) i++;
      const start = i;
      while (i < text.length && !/\s/.test(text[i])) i++;
      let url = text.slice(start, i);
      let ended = false;
      while (url.endsWith(',')) {
        url = url.slice(0, -1);
        ended = true;
      }
      if (!url) continue;
      let w = -1;
      let x = 1;
      if (!ended) {
        const from = i;
        while (i < text.length && text[i] !== ',') i++;
        for (const d of text.slice(from, i).trim().split(/\s+/)) {
          const value = parseFloat(d);
          if (Number.isNaN(value)) continue;
          if (d.endsWith('w')) w = value;
          else if (d.endsWith('x')) x = value;
        }
      }
      if (w >= 0 && w > width) {
        width = w;
        bestWidth = url;
      } else if (w < 0 && x > density) {
        density = x;
        bestDensity = url;
      }
    }
    return bestWidth || bestDensity;
  }

  function absolute(url, base) {
    try {
      return new URL(url, base).href;
    } catch {
      return '';
    }
  }

  const LAZY = ['data-src', 'data-lazy-src', 'data-original', 'data-actualsrc', 'data-hi-res-src', 'data-full-src'];

  /** Absolute addresses in `root` (a copy): links, pictures (lazy ones too), sources of players. */
  function absolutize(root, base) {
    for (const a of root.querySelectorAll('a[href]')) {
      const href = a.getAttribute('href');
      if (href && !href.startsWith('#') && !/^\s*javascript:/i.test(href)) a.setAttribute('href', absolute(href, base));
    }
    for (const img of root.querySelectorAll('img')) {
      let src = img.getAttribute('src') || '';
      const placeholder = !src || (src.startsWith('data:') && src.length < 512);
      if (placeholder) {
        for (const name of LAZY) {
          const value = img.getAttribute(name);
          if (value && !value.startsWith('data:')) {
            src = value;
            break;
          }
        }
      }
      if (!src || (src.startsWith('data:') && src.length < 512)) {
        src = largestSrcset(img.getAttribute('srcset') || img.getAttribute('data-srcset')) || src;
      }
      if (src && !src.startsWith('data:')) img.setAttribute('src', absolute(src, base));
      img.removeAttribute('srcset');
      img.removeAttribute('sizes');
      img.removeAttribute('loading');
    }
    for (const element of root.querySelectorAll('iframe[src], video[src], audio[src], source[src]')) {
      element.setAttribute('src', absolute(element.getAttribute('src'), base));
    }
  }

  /** The current address of each picture of the page (what it shows), for the copies. */
  function currentSources(root) {
    const map = new Map();
    for (const img of root.querySelectorAll('img')) {
      if (img.currentSrc && !img.currentSrc.startsWith('data:') && !img.currentSrc.startsWith('blob:'))
        map.set(img, img.currentSrc);
    }
    return map;
  }

  function meta(...names) {
    for (const name of names) {
      const element =
        document.querySelector(`meta[property="${name}"]`) || document.querySelector(`meta[name="${name}"]`);
      const content = element?.getAttribute('content')?.trim();
      if (content) return content;
    }
    return '';
  }

  /** Author and publication date from the page's JSON-LD (schema.org Article). */
  function jsonLd() {
    const out = { author: '', published: '' };
    for (const script of document.querySelectorAll('script[type="application/ld+json"]')) {
      let data;
      try {
        data = JSON.parse(script.textContent || '');
      } catch {
        continue;
      }
      const items = Array.isArray(data) ? data : data?.['@graph'] ?? [data];
      for (const item of items) {
        if (!item || typeof item !== 'object') continue;
        if (!out.published && typeof item.datePublished === 'string') out.published = item.datePublished;
        const author = Array.isArray(item.author) ? item.author[0] : item.author;
        if (!out.author && author) out.author = typeof author === 'string' ? author : author.name || '';
      }
    }
    return out;
  }

  function cut(html) {
    return html.length > MAX_HTML ? html.slice(0, MAX_HTML) : html;
  }

  /** A document without a browsing context: what is put into it never loads or runs. */
  function inertDocument() {
    return document.implementation.createHTMLDocument('');
  }

  function selection(base) {
    const sel = window.getSelection();
    if (!sel || sel.isCollapsed || sel.rangeCount === 0) return { html: '', text: '' };
    const inert = inertDocument();
    const box = inert.createElement('div');
    for (let i = 0; i < sel.rangeCount; i++) {
      const range = sel.getRangeAt(i);
      const fragment = inert.importNode(range.cloneContents(), true);
      // A selection inside one element keeps that element's kind (a code block, a quote, a list item).
      const container = range.commonAncestorContainer;
      let element = container.nodeType === Node.ELEMENT_NODE ? container : container.parentElement;
      if (element?.tagName === 'CODE' && element.parentElement?.tagName === 'PRE') element = element.parentElement;
      const kinds = ['PRE', 'CODE', 'BLOCKQUOTE', 'LI', 'H1', 'H2', 'H3', 'H4', 'H5', 'H6'];
      if (element && kinds.includes(element.tagName)) {
        const wrapper = inert.importNode(element, false);
        if (element.tagName === 'PRE' && element.firstElementChild?.tagName === 'CODE') {
          const code = inert.importNode(element.firstElementChild, false);
          code.appendChild(fragment);
          wrapper.appendChild(code);
        } else {
          wrapper.appendChild(fragment);
        }
        if (element.tagName === 'LI') {
          const list = inert.createElement(element.parentElement?.tagName === 'OL' ? 'ol' : 'ul');
          list.appendChild(wrapper);
          box.appendChild(list);
        } else {
          box.appendChild(wrapper);
        }
      } else {
        box.appendChild(fragment);
      }
    }
    absolutize(box, base);
    return { html: cut(box.innerHTML), text: cut(sel.toString()) };
  }

  function article(base) {
    if (typeof Readability !== 'function') return null;
    try {
      // A copy: Readability changes the document it reads.
      const copy = document.cloneNode(true);
      const parsed = new Readability(copy, { keepClasses: true, charThreshold: 300 }).parse();
      if (!parsed || !parsed.content) return null;
      // Parsed into a document of its own: nothing of it loads or runs.
      const box = new DOMParser().parseFromString(parsed.content, 'text/html').body;
      absolutize(box, base);
      return { ...parsed, content: cut(box.innerHTML) };
    } catch {
      return null;
    }
  }

  function wholePage(base) {
    const sources = currentSources(document.body);
    const copy = inertDocument().importNode(document.body, true);
    // The copies of the pictures show what the page shows (a lazy picture already loaded).
    const originals = [...document.body.querySelectorAll('img')];
    const copies = [...copy.querySelectorAll('img')];
    originals.forEach((img, i) => {
      const current = sources.get(img);
      if (current && copies[i]) copies[i].setAttribute('src', current);
    });
    for (const element of copy.querySelectorAll('script, style, noscript, template, link, meta')) element.remove();
    absolutize(copy, base);
    return cut(copy.innerHTML);
  }

  /**
   * Read the page: `options.page` also the whole page's HTML, `options.article` the readable article.
   */
  function extract(options = {}) {
    const base = document.baseURI;
    const ld = jsonLd();
    const sel = selection(base);
    const art = options.article === false ? null : article(base);
    const canonical = document.querySelector('link[rel="canonical"]')?.href || '';
    const image = meta('og:image', 'twitter:image');
    return {
      url: location.href,
      canonical,
      baseUrl: base,
      title: meta('og:title') || document.title || art?.title || '',
      articleTitle: art?.title || '',
      author: art?.byline || meta('author', 'article:author') || ld.author,
      site: art?.siteName || meta('og:site_name', 'application-name') || '',
      published: art?.publishedTime || meta('article:published_time', 'datePublished', 'date') || ld.published,
      description: meta('description', 'og:description') || art?.excerpt || '',
      language: document.documentElement.lang || art?.lang || '',
      image: image ? absolute(image, base) : '',
      articleHtml: art?.content || '',
      articleLength: art?.length || 0,
      pageHtml: options.page ? wholePage(base) : '',
      selectionHtml: sel.html,
      selectionText: sel.text,
    };
  }

  globalThis.brulionClipperExtract = extract;
  globalThis.brulionClipperHelpers = { largestSrcset, absolute };
})();
