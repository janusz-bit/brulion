# Trilium export fixtures

These are real exports from Trilium servers. The generator built each document through the
server's HTTP API (ETAPI, plus the web API for protecting a note and for single-note/OPML
export), and the server then exported it. All content is synthetic (Polish text, fictional
person, `example.org`).

Generated on 2026-10-16 with `generator/run.sh` (a throwaway server on 127.0.0.1 with a temporary
data directory, deleted afterwards):

| Files | Trilium | How |
|---|---|---|
| everything without a version prefix | **0.106.0** (TriliumNext/Trilium `v0.106.0`, nixpkgs `trilium-next-server`), DB 240, UI locale `pl` | `./run.sh python3 generate.py out` |
| `0.99-*` | **0.99.0** (TriliumNext, nixos-25.05 `trilium-next-server`), DB 233, English | `TRILIUM_SERVER=<store path> ./run.sh python3 generate.py out` |
| `0.63-*` | **0.63.6** (zadam/trilium, nixos-24.11 `trilium-server`), DB 228, English | `LEGACY=1 ./run.sh python3 generate_legacy.py out` |

The format in short: a ZIP with `!!!meta.json` (tree, titles, types, attributes, attachments), lossy file names (titles
sanitised, `1_` prefixes for duplicates; 0.63 and 0.99 cut names to 30 characters) and links
rewritten to relative, percent-encoded paths. Attachment paths are not encoded.

## Files

* `html.zip`: the whole tree (`root/…`), HTML format. It holds the rich note
  `Projekty/Ogród: plan/budżet?` (every CKEditor feature: formatting, nested and to-do lists
  with task states, table, code blocks, quote, admonitions, math, footnote, collapsible,
  internal, external, attachment and clone links, link previews, icon, attachment image,
  image-note image, mermaid SVG embed, include-note), duplicate sibling titles (`Zadania` and
  `1_Zadania`), a folder-only note (`Kod`), code notes (JS, JSON, Python as `.dat`, Markdown),
  a book with a branch prefix (`Część I - …`), mermaid and canvas notes with their
  `*-export.svg` attachments, an image note, a file note (PDF), a note cloned into `Archiwum`
  (`*.clone.html` stub), a protected note exported **without** a protected session
  (`[protected]`, no data file), an `#excludeFromExport` note (absent), an `#archived` note, a
  long title, the title `???` (`note.html`), labels (valueless, valued, inheritable, Polish
  names, quotes in values), relations, a template with promoted attribute definitions, search,
  relation map, note map, web view, render, mind map and empty notes, and day notes
  (`Calendar/2025/12 - Grudzień/31 - Środa`, `Calendar/2026/10 - Październik/14 - Środa` and
  `16 - Piątek`, with to-dos).
* `markdown.zip`: the same tree, Markdown format.
* `subtree-projekty-html.zip`, `subtree-projekty-markdown.zip`: only `Projekty`. Links and
  relations to notes outside it stay `#root/…` or are dropped.
* `archiwum-protected-session.zip`: subtree `Archiwum`, HTML, exported while a protected
  session was open: the protected note is included decrypted, and the clone is a full note
  (its primary copy is outside).
* `single-ogrod.html`, `single-ogrod.md`: single-note export of the rich note (images inlined as
  `data:` URIs in HTML; nothing resolved in Markdown).
* `projekty.opml`: OPML export of `Projekty` (raw stored HTML in `_note`).
* `0.99-subtree-projekty-html.zip`, `0.99-subtree-projekty-markdown.zip`: the same `Projekty`
  subtree from 0.99 (30-character names, `.mkd`, no `<` escaping in Markdown, `internalLink`
  relations in the meta).
* `0.63-html.zip`, `0.63-markdown.zip`: a smaller tree in the markup of the zadam/trilium
  0.63 editor (`<i>`, `<s>`, `<mark>`): truncated names, setext headings and to-do lists without
  checkboxes in Markdown, links to the protected note as `…/undefined`.
* `ids.json`, `ids-0.63.json`: generator key → note id (also day notes and attachment ids),
  for assertions. The 0.99 run used the same seed, so its planned note ids equal `ids.json`.

The mermaid/canvas `*-export.svg` attachments were added through ETAPI the way the web client
saves them after rendering. Text content was written in the HTML form the editor stores (with
`data-list-item-id`, which the 0.106 export strips).

## Regenerating

On a machine with Nix (not the Raspberry Pi): copy `generator/` somewhere and run the commands
above. The scripts need only `python3` and `curl`. Note ids are seeded; attachment ids, dates
and ZIP timestamps change on every run.
