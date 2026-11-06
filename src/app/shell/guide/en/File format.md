---
tags: [documentation]
brulion: document
---
# File format

Brulion has no format of its own. It is CommonMark with the GFM additions (tables, task lists, strikethrough) plus a few extensions compatible with Obsidian (the Tasks plugin) and readable in Logseq. Everything Brulion saves can be read in any text editor.

## Folder layout
```
brulion/
  journals/2026-10-14.md    journal days
  Projects/Home.md          pages (folders = namespaces)
  assets/                   attachments
  templates/                templates (journal.md = a new day)
  .brulion/config.json      settings shared between devices
  .trash/                   trash
```

- A page's title is the file name without `.md`.
- Names that differ only in letter case are one page — Brulion never creates such twins.
- The device's working data (version history, sync traces, the index) lives outside the folder, so Syncthing and Nextcloud leave it alone.
- Among it is a ready-made index of the notebook (`index-cache.bin`) that makes opening a large notebook again faster. It can be safely deleted — Brulion builds it anew.
- `.brulion/`, `.trash/`, `.git/`, `.obsidian/`, `logseq/` and sync's temporary files are skipped.

## Syntax cheat sheet
| Notation | Meaning |
|---|---|
| `[[Page]]`, `![[Page]]` | link, embed |
| `((id))`, ` ^id` | block reference, block id |
| `#tag`, `#[[two words]]` | tag |
| `- [ ]`, `- [x]` | task |
| `📅 ⏳ 🛫 ✅ ➕` | task dates |
| `🔁 every week` | recurrence |
| `⏰ YYYY-MM-DD HH:MM`, `⏰!` | reminder, alarm |
| `key:: value` | block property |
| `> [!tip] Title` | callout |
| double `=` around text, `%%comment%%` | highlight, hidden comment |
| `<mark style="background: #FF5582A6;">text</mark>`, `<mark class="hltr-red">` | colored highlight (like the Highlightr plugin for Obsidian; see [[Document and outline#Colored highlights|Colored highlights]]) |
| `icon: 🚀`, `banner: "![[picture.jpg]]"`, `banner_y: 0.3` | page icon and cover (like Iconize and Banners; see [[Tags and properties#Page icon and cover|Page icon and cover]]) |
| a block with the language `calc` or `query` | calculation, query |
| `$…$`, `$$…$$`, a block with the language `math` | math formula (LaTeX) |
| a block with the language `mermaid` | Mermaid diagram (see [[Diagrams]]) |
| a block with the language `chart` | a chart from a table, a `calc` block or a query (see [[Tables and charts]]) |
| a cell starting with `=` | a formula in a table |
| `![[file.pdf]]`, `![[file.pdf#page=3]]`, `[[file.pdf#page=3]]` | a thumbnail of the first (third) page of a PDF, a link that opens the PDF viewer on that page — notation as in Obsidian (see [[Document and outline#PDFs|PDFs]]) |
| `![[Drawing ….svg]]` | a drawing (see [[Drawings]]): an SVG picture with an Excalidraw scene inside; the scene also holds the paper (`brulionBackground`), the background color (`viewBackgroundColor`), a light or dark background (`brulionTheme`: `light`, `dark`, no key — like the theme) and the backdrop — a picture or PDF page under the writing: a locked `image` element at the very bottom with `customData.brulionBackdrop` (`path` — the file's path in the notebook, `page` — the PDF page, 0 for a picture), with its picture in the scene (`files`) and in the SVG itself |
| `![[Sketch.excalidraw]]`, `*.excalidraw.md` | a drawing of the Excalidraw plugin for Obsidian (front matter `excalidraw-plugin`, the scene in the `## Drawing` block: `compressed-json` or `json`) or an `.excalidraw` file — Brulion edits it and saves it in the same format, changing only the scene and the entries of changed texts |

Files never contain dates in words (“tomorrow”) — only `YYYY-MM-DD` and `HH:MM`. HTML in a note is shown as text and never executed — except for the `<mark>` tags of colored highlights, from which Brulion takes only the color (always one of the colors of its own palette).

## What stays untouched
Brulion rewrites only what you changed: the rest of the file, including line endings and formatting, stays the same byte for byte. That is why diffs in a version control system stay small.

The full specification for developers: `docs/SYNTAX.md` in the repository. An overview of the features: [[Contents|Contents]].
