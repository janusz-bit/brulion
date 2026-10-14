// @vitest-environment jsdom
import { it } from 'vitest';
import { renderMarkdown, type RenderContext } from './index';

const ctx: RenderContext = {
  path: 'notes/a.md',
  locale: 'pl',
  now: new Date(2026, 9, 14, 12, 0),
  resolveLink: (t) => (t.toLowerCase() === 'dom' ? 'Projekty/Dom.md' : t === 'photo.png' ? 'assets/photo.png' : t === 'Inna.md' ? 'Inna.md' : null),
  href: (p, t, h, b) => `#/page/${encodeURIComponent(p ?? t)}${h ? '#' + h : ''}${b ? '#^' + b : ''}`,
  assetUrl: (p) => `asset://localhost/${p}`,
  embed: (t, h, b, d) => `<p>EMBED ${t} ${h ?? ''} ${b ?? ''} ${d}</p>`,
  blockRef: (id) => (id === 'abc' ? { path: 'b.md', text: 'Treść **bloku**' } : null),
};

const samples = [
  '- item ^abc\n  key:: v\n  id:: 650e-uuid\n- [ ] zadanie ^t1\n  - dziecko\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n^tbl',
  '> [!danger] <img src=x onerror=alert(1)> **Tytuł**\n> <script>alert(1)</script>tekst <a href="javascript:alert(1)">x</a>\n> <svg onload=alert(1)><circle/></svg> <iframe src="https://evil"></iframe>',
  '<a href="jav&#x09;ascript:alert(1)">a</a> <img src="data:image/png;base64,AAA"> <img src="data:text/html,<script>"> <video src="blob:abc" onplay="x()"></video> <details open><summary>S</summary>in</details> <input type="text" onfocus="x()" value="v">',
  'Zażółć gęślą jaźń #święta ==ćma== [[Łódź|miasto]] ((brak))',
  'title:: Strona\ntags:: a, b\n\n# H1\n## H1',
  '%%a%% b %%c\nd%% e',
  '```\n%% not comment %%\n```\n\n`[[nie link]]` `#nietag`',
  '- [ ] task\n  ```calc\n  x = 2\n  ```\n- `= x * 3`',
  '[[x]]\n\n![[Brak]]\n\n![[brak.png]]\n\n![[plik.zip]]',
  '* SCHEDULED: <2026-10-15 Thu 10:00 .+1w>\n* LATER task [#A] DEADLINE: <2026-10-01 Wed>',
  'SCHEDULED: <2026-10-15 Thu>',
  '<https://x.pl> http://example.com i mail@x.pl',
  '![obraz|100x50](assets/a.png) ![zdalny](https://e.com/a.png) ![x](javascript:alert(1))',
];
import { writeFileSync } from 'node:fs';
it('explore', () => {
  const out: string[] = [];
  for (const s of samples) {
    out.push('-----\n' + s + '\n=>\n' + renderMarkdown(s, ctx));
  }
  writeFileSync("/tmp/claude-1000/-home-nixos-workspace-note-taking/e33aea8f-4baa-4b18-a73a-399503007e08/scratchpad/out.txt", out.join("\n"));
});
