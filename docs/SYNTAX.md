# Brulion Markdown dialect (developer spec)

Brulion stores every note as a plain UTF-8 `.md` file. The dialect is CommonMark + GFM
plus a small set of extensions chosen to be **compatible with Obsidian (incl. the Tasks
plugin) and readable by Logseq**. Anything Brulion writes must stay readable as plain
Markdown in any other editor.

This file is the single source of truth for parsers, renderers, importers and exporters.

---

## 1. Vault layout

```
<vault>/
  journals/2026-10-14.md     daily notes (folder + file-name format configurable)
  <any>/<folders>/Page.md    pages; folders act as namespaces ("Projekty/Dom")
  assets/                    attachments (images, pdf, ...)
  templates/                 templates (journal.md = template for new daily notes)
  .brulion/config.json       vault settings shared between devices (synced)
  .trash/                    deleted files (restorable)
```

* **Page title** = file base name without `.md` (`Projekty/Dom.md` → title `Dom`,
  full name `Projekty/Dom`). Frontmatter `aliases:` adds extra link names.
* **Letter case**: names that differ only in case (`dom.md` / `Dom.md`, `projekty/` /
  `Projekty/`) are one page / folder — on case-insensitive storage (Android shared storage, FAT,
  macOS) they are one file, and links do not tell them apart. Brulion never creates such a twin:
  a new page with that name opens the existing one, a rename to it is refused, an imported page
  gets ` 2`, and new files go into the folder as it is spelled.
* **Folders** are created with their names sanitised like page titles (`a:b?` → `a b`, per
  segment) and as the vault spells existing ones (`projekty/Nowy` goes into `Projekty/`); a name
  the vault would hide — a leading `.` or one of the ignored folders below (`logseq`,
  `node_modules`, …), also once sanitised (`logseq?`) — is refused ("Folder o nazwie „…” byłby
  ukryty"). Nothing is ever moved into an ignored folder.
* **Moving and renaming** (pages — daily notes too: they are files —, attachments, folders —
  "Przenieś do…", drag and drop, a `/` in
  a name typed in the Rename dialog is a path from the vault root; in a page title edited in place
  it is sanitised like the other characters a file name cannot have, `TCP/IP` → `TCP IP.md`):
  refused onto a name that exists (also in another letter case) and into the item itself, and a
  folder name the vault would hide (also once sanitised: `logseq?`); every link and tag keeps its
  target (§2 "Rewriting on rename and move"). Renaming or moving a special folder of
  `.brulion/config.json` (`journalsFolder`, `templatesFolder`, `attachmentsFolder`,
  `newPagesFolder`), a folder holding one, or the `journalTemplate` file updates those keys in the
  same operation.
* Device-local state (sync bookkeeping, merge bases, history snapshots, caches) is **never**
  stored inside the vault — it lives in the app data directory on the device (`QStandardPaths::AppDataLocation`). This keeps Syncthing /
  Nextcloud from syncing machine-specific data.
* Ignored when indexing: `.brulion/`, `.trash/`, `.git/`, `.obsidian/`, `logseq/`,
  `.stfolder`, `.stversions/`, `node_modules/`, temp files (`~syncthing~*.tmp`,
  `.syncthing.*.tmp`, `.~lock.*#`, `*.part`). Syncthing conflict copies
  (`Name.sync-conflict-20261014-101010-ABCDEF.md`) are **not** indexed as pages but are
  surfaced in the Sync Center as conflicts to merge.

### `.brulion/config.json`

```json
{
  "version": 1,
  "journalsFolder": "journals",
  "journalFileFormat": "yyyy-MM-dd",
  "newPagesFolder": "",
  "attachmentsFolder": "assets",
  "templatesFolder": "templates",
  "journalTemplate": "templates/journal.md",
  "defaultReminderTime": "09:00",
  "weekStartsOn": 1,
  "journalMode": "outline",
  "pageMode": "document"
}
```
`journalFileFormat` uses Qt `QDate` format strings (`yyyy-MM-dd`, `dd.MM.yyyy`, …).
`journalMode` / `pageMode` (`"outline"` | `"document"`) are the note modes of daily notes and of
pages the content does not decide (§4 "Tryb notatki"). Missing keys and invalid values fall back
to these defaults; unknown keys are kept when Brulion saves the file. When the file is missing
and the folder is an Obsidian vault (`.obsidian/`), `journalsFolder`, `journalFileFormat`
(converted from moment.js), `journalTemplate`, `templatesFolder` and `attachmentsFolder` default
to Obsidian's settings (`daily-notes.json`, `templates.json`, `app.json`; docs/IMPORT.md "Folder
Obsidiana bez importu") — in memory, until the settings are saved.

---

## 2. Links and references

| Syntax | Meaning |
|---|---|
| `[[Page]]` | link to page |
| `[[Page\|Alias]]` | link with display text |
| `[[Page#Heading]]` | link to heading |
| `[[Page#Parent#Heading]]` | Obsidian heading path: the heading under that parent (below) |
| `[[Page#^abc123]]` | link to block with id `abc123` in Page |
| `[[#^abc123]]` / `[[#Heading]]` | same-page link |
| `![[Page]]`, `![[Page#Heading]]`, `![[Page#^id]]` | embed (transclusion) |
| `![[photo.png]]`, `![[doc.pdf]]` | attachment embed |
| `((abc123))` | Logseq-style block reference — resolves the id **vault-wide** |
| `!((abc123))` | block embed |
| `[text](url)`, `[text](relative/path.md)`, `<https://..>`, bare `https://...` | standard links |
| `#tag`, `#[[Strona]]`, `#Projekty/Dom` | a tag — also a link to the page it names (§3 "Tags as page links") |

**Link resolution** (`target` → file): 1) exact vault path (with or without `.md`,
case-insensitive); 2) path relative to the linking file's folder; 3) unique base-name
match (case-insensitive, Unicode NFC); if several, the one with the shortest path, then
alphabetical; 4) frontmatter `aliases`. Unresolved links point to a page that will be
created on click (in `newPagesFolder`).

**Rewriting on rename and move** (`VaultIndex::planRelocation()`): before files move, every link
that the move can affect — wiki links, embeds and Markdown links to the moved files or to files of
the same name, the links of the moved notes themselves — is resolved as it is and as it will be
with the files at their new paths. A link whose file would change is rewritten so that it keeps
naming the same file; only its target changes (`|alias`, `#Heading`, `#^id`, `!` stay):
* a link to a moved file as before: a bare name keeps the bare new name (`[[Old]]` →
  `[[New]]`), a path-qualified link gets the new path (`[[Projekty/Dom]]` → `[[Archiwum/Dom]]`;
  a rename in place keeps the written folder prefix, `[[../b/Old]]` → `[[../b/New]]`), a
  Markdown link the new vault path (`[x](Projekty/Mój%20dom.md)`);
* **Markdown links and pictures** (`[x](…)`, `![x](…)`) whose written path names the file
  exactly — relative to the linking note's folder or from the vault root, read in the order links
  resolve (an explicit `./` / `../` path relative to the note; one starting with `/` from the
  root; any other first from the root, then from the note's folder; in a note at the vault root
  the two readings are one: relative) — keep naming it exactly, as Obsidian, GitHub and any
  Markdown viewer read them, even where Brulion would still find the file by a path suffix or its
  name: a relative path stays relative, also when the linking note itself moves
  (`[x](../Zakupy.md)` in `Projekty/Plan.md` → `[x](../../Zakupy.md)` once Projekty is
  `Archiwum/Projekty`), a vault path stays a vault path (with its leading `/`):
  `[x](Projekty/Remont%20mieszkania.md)` → `[x](Archiwum/Projekty/Remont%20mieszkania.md)` when
  the folder Projekty moves to Archiwum/Projekty. `./` (unless the path climbs with `../`), the
  extension as written (also none), `#fragment`, the title and `<…>` stay, and so does the
  percent-encoding as written (spaces as `%20`, letters beyond ASCII as `%C5%BC` when the link
  had them so, in lower case when it did). A relative path that Brulion would read from the vault
  root as another file gets `./`. Links that named their file otherwise (a path suffix
  `[x](Stare/Dom.md)`, a base name, an alias) and wiki links follow the rules above;
* when that name would reach another file — or a link to a file that did not move would now
  reach a moved one (a page moved to a shallower folder takes `[[Dom]]` over) — the **shortest
  unique path**: `[[Projekty/Dom]]` (the title, then longer path suffixes, then the vault path;
  `VaultIndex::linkTarget()`, also what `[[` completion inserts for a duplicate name);
* tags the same way (they resolve like links at the vault root): `#dom` → `#Projekty/Dom`,
  `#Projekty/Dom` → `#Archiwum/Dom`, `#[[…]]`, `tags::` and frontmatter items in their form.
* the filters of ```` ```query ```` blocks (§10) that name a moved file or a renamed tag, in every
  note (`findQueryNames()` / `retargetQueryNames()`: any spelling of the key, negated, each value
  of a comma list, never in comments, phrases or outside the fence): `tag:` / `#tag` exactly that
  tag as tags are (sub-tags stay); `ref:` / `[[…]]` like a link written in the query's note;
  `page:` (no `*`, not `this`) when it would no longer match exactly the page it matched (it
  matches a title, alias, path or link target): a value naming the moved page is written as a
  link to it would be or, when another page answers to that name too, as the shortest form naming
  only the moved page (the title, longer path suffixes, the path without, then with `.md`:
  `page:Dom` → `page:Mieszkanie.md` when Dom.md becomes Mieszkanie.md next to
  Archiwum/Mieszkanie.md), and a value naming a page that stays is qualified the same way when a
  moved page would answer to it too (`page:Mieszkanie` → `page:Archiwum/Mieszkanie`); a value that
  matched several pages (two pages titled Dom) or none stays as written; `path:` (no
  `*`) when every file it matched moved by one folder change (`path:Projekty/` →
  `path:Archiwum/Projekty/`; a prefix that also matches a file that stays is left, and so is one
  whose new prefix would match other files too — a page moved into a folder with other pages
  keeps `path:Skrzynka/`). The new value
  keeps the written form (`[[…]]`, quotes) or goes into `[[…]]` when it holds a blank or a comma
  (`tag:[[Mój dom]]`). They count as links (`ref:`, `page:`, `path:`) and tags (`tag:`) in the
  rename and move dialogs.
Links that were unresolved stay so. Each rewritten note is written once (one history step).
Attachments inside a moved folder are covered too (`![[Projekty/plan.png]]`,
`![](Projekty/plan.png)`).

**Pictures:** an attachment whose extension is `.png`, `.jpg`, `.jpeg`, `.gif`, `.webp`, `.svg`,
`.bmp`, `.avif` or `.ico` (any case; `paths::pictureExtensions()`, the one list the editor, the
picture view, the file dialog and the HTML export share) is embedded as a picture; any other
attachment as a file. Whether the app can draw it depends on the device's image decoders (WebP
and AVIF are often missing): the picture view then names the format instead of a load error.

**Drawings / Rysunki:** a drawing is an ordinary SVG attachment, embedded like any picture
(`![[Rysunek 2026-10-27 14.32.svg]]`; "/Rysunek" creates `Rysunek <yyyy-MM-dd HH.mm>.svg` in
`attachmentsFolder`, a free name `… 2.svg` when taken), that carries its editable data inside: an
**Excalidraw scene embedded the way excalidraw.com's "Export → SVG → Embed scene" writes it** (core
module `drawing`, `drawing/excalidraw.h`):

```
<svg version="1.1" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 W H" width="W" height="H">
<!-- svg-source:excalidraw -->
<metadata><!-- payload-type:application/vnd.excalidraw+json --><!-- payload-version:2 --><!-- payload-start -->BASE64<!-- payload-end --></metadata>
<rect …paper…/> <path …lines or grid…/> <path d="…" fill="#1f2430"/> …
</svg>
```

BASE64 (one line) is base64 of the Latin-1 bytes of `{"version":"1","encoding":"bstring",
"compressed":true,"encoded":"…"}`, whose `encoded` holds the zlib-compressed scene JSON, one
character per byte; the scene is `{"type":"excalidraw","version":2,"source","elements","appState",
"files"}`, every stroke a `freedraw` element (`x`, `y`, `points` relative to them, `pressures`,
`simulatePressure`, `strokeColor`, `strokeWidth`, `opacity`; a highlighter stroke has
`customData.brulionTool: "highlighter"`), the paper `appState.viewBackgroundColor` and the pattern
`appState.brulionBackground` (`lines`, `grid`; none: plain — Excalidraw ignores it). The picture
covers the strokes plus a 16 px margin at one scene unit a pixel (an empty drawing: 320 × 180), on
the paper, drawn as filled outlines (perfect-freehand's, as Excalidraw draws freehand lines): every
program shows the file as a picture (Obsidian, Logseq, browsers, file managers), excalidraw.com
opens it as a drawing again, Brulion's editor (docs/UI.md "Drawing editor") edits it. Brulion reads
payload versions 1 and 2, compressed or not, and `.excalidraw` JSON. An SVG is a drawing when its
first 4 KB name the payload type (`looksLikeExcalidrawSvg`); one without the scene is a picture only.
Brulion edits only freehand strokes: a scene with other elements (shapes, arrows, text, pictures,
rotated or filled freehand lines) is shown, never written. Elements and fields Brulion does not
model are written back as they were; deleted elements are dropped (as Excalidraw's own export does).

**Pictures from the internet:** a Markdown picture whose destination is an `http:` / `https:` URL
with a host — `![alt](https://example.com/kot.jpg)`, `![](<https://example.com/mój kot.png>)`,
also the embed `![[https://example.com/kot.jpg]]` (`webpicture::isWebUrl`) — is a picture whatever
its URL ends with (`![…](…)` says so). Other schemes (`![x](ftp://…)`, `data:`) stay text. The file
keeps the URL; nothing is written for showing it. The editor shows such a picture only from the
device's cache, fetched when the user allows it — "Pokaż" on that picture, or the device setting
"show pictures from the internet without asking" (off by default: opening a synced or imported
note contacts no server). Without asking, a picture is fetched only from a host of the internet:
loopback, private-use, shared (100.64.0.0/10: carrier NAT, Tailscale), link-local, site-local and
unspecified IP addresses, and the names of a home network by their form — one label (`localhost`,
`nas`) or a suffix such as `.local`, `.lan`, `.home.arpa`, `.internal`, `.fritz.box`
(`webpicture::isPrivateHost`; other names are not resolved) — wait for "Pokaż", as does a picture fetched
so already this session and dropped from the cache since; turning the setting off cancels the
fetches it started. The fetch: http(s) only, every redirect checked (`webpicture::redirectAllowed`:
never https → http, never from a host of the internet to a private one), no cookies, a generic
`User-Agent: Mozilla/5.0` and `Accept-Language: *` (in the browser neither: the browser sends its
own, and a page may send other sites only such "simple" requests without a CORS preflight; the
browser follows redirects itself), at most 20 MB, a 30 s stall limit, and the
bytes must be a picture by their signature (PNG, JPEG, GIF, WebP, BMP, ICO, AVIF, SVG —
`webpicture::sniffFormat`); the cache keeps up to 200 MB, the least recently used dropped first
(a file shown is marked used by its modification time, at most once an hour). In the browser the
cache is in the tab's memory: up to 50 MB, gone with the tab (docs/WEB.md). Its format is the one
found in the bytes (an SVG served from a URL without `.svg` is drawn as one).
**"Zapisz w notatniku"** stores the picture as an attachment (`attachmentsFolder`, named after the URL's last path segment with the extension of its real
format: `kot.png` for a PNG served as `kot.jpg`) and points every picture of that URL on the page
at it (`retargetWebPicture`, one undo step): `![alt](assets/kot.png)` — exactly the destination the
parser read is replaced (`markdown::markdownDestination`; one written with backslash escapes as a
whole), the alt text, a title (also one naming the URL or holding `](`) and `<…>` stay, blanks,
parentheses, `%`, `#`, `<` and `>` of the path percent-encoded (`assets/Mój%20kot.png`, letters as
they are); `![[https://…|alias]]` → `![[assets/kot.png|alias]]`. A picture is counted as stored only
when a note changed (nothing is left in the attachments folder otherwise). Links
`[x](https://…)`, bare URLs, code and blocks that show no pictures (code fences, formulas, tables,
HTML) are never fetched or touched. A pasted picture address alone (`https://…/kot.jpg`, by its
form: an http(s) URL whose path ends with a picture extension) stays the bare URL it is until the
user chooses "Zapisz w notatniku" (docs/UI.md). Exports: the HTML (`html-site`, `html-single`) and
Pandoc ones show every such picture — `![alt](url)` and `![[url|alt]]` alike — as `<img>` /
`![alt](url)` with the URL as written ("Zostaw adresy", the default) or, with "Dołącz kopie", a copy
the app takes from the device's cache, fetching the missing ones first: `assets/web/<name>` in the
site's and Pandoc's ZIP (a free name: `kot 2.jpg`), a `data:` URI in the single HTML file; a picture
that cannot be fetched keeps its URL (named in the export's warnings), and so does one of this
device or its private network not in the cache (the export never asks those hosts) and every one
past 256 MB of copies in all. The other exports keep the
notes' text, URLs included, as written.

**Display:** a link naming a daily note — `[[2026-10-13]]` or the vault's `journalFileFormat`,
without an alias — is shown as a readable date in the UI language (`wt 13 paź`, `Tue, Oct 13`;
with the year when it is not the current one). The file keeps `[[2026-10-13]]`. A
`[[Page#^id]]` without an alias shows "Page › " and the first 60 characters of the block's text,
like `((id))` does: the block with that id on that page (Obsidian ids are the page's own: another
page may have the same one), else, moved away, the block the link now leads to (below); the id
itself while the block is unknown. Both show the block's current text, in table cells too: an edit
of a block on the same page shows at once.

### Headings and blocks in links / Linki do nagłówków i bloków

**Heading fragments** (`markdown::headingFragment`, `src/core/markdown/linkformat.h`): what
`[[Page#…]]` names a heading by — its line as shown: inline markup removed (`**`, `*`, `~~`,
`==`, backticks; a wiki link by its alias, else its target; a Markdown link by its label; `#tag`
and `#[[tag]]` by their name), a trailing ` ^id` dropped, the characters that split or end a wiki
link or that Obsidian drops from heading links replaced by a space (`#`, `|`, `^`, `:`, `[`, `]`,
`\`, `%%` — a comment's text stays), whitespace (tabs, no-break spaces) collapsed, NFC. Letters,
Polish ones too, digits, emoji and other punctuation stay: "Plan: **wrzesień** #praca" →
`Plan wrzesień praca`, "C# i C++" → `C i C++`, "Q&A: test" → `Q&A test`. Obsidian writes the same
fragment for headings without inline markup or links; for those it keeps the markup in its own
links, so a fragment Brulion copied from such a heading may not scroll in Obsidian (the page
opens).

**Matching** (`markdown::findHeading` — one matcher for following links, `![[Page#Heading]]`
embeds and copying): the fragment and every heading are compared by key — headingFragment, then
case and diacritics folded (`[[Page#zazolc gesla]]` finds "Zażółć **gęślą**"):
1. the first heading whose key is the whole fragment's (a fragment written by hand with a `#` of
   the heading's own text: `[[Page#C# i C++]]`);
2. an Obsidian heading path `[[Page#A#B#C]]`: the first heading `C` whose enclosing headings
   (earlier headings of a lower level, Logseq "- ## …" items included) contain `A` and `B` in that
   order — levels may be skipped;
3. else the first heading matching the last segment.

**Following** a link to a block or heading scrolls to it, selects it and flashes it, also when it
is on the page already shown (`[[#Heading]]`, `[[#^id]]`, a link naming the current page — again
on every click; in the journal stream the stream scrolls to it). In a query result a same-page
link names the result's own page. A `[[Page#^id]]` whose block is no longer on that page opens the
page holding the id now (vault-wide block ids, like `((id))`). A heading or block that is nowhere:
the page opens at its top and the editor says so. "Nowhere" is decided on the complete index
(right after opening a notebook it is still being built: it is completed first). The HTML export
finds headings by the same matcher (sections of `![[Page#Heading]]`, anchors of links).

**Copied links** — the block menu's "Kopiuj odnośnik", the command palette, Ctrl+Shift+C,
the side panel's outline (docs/UI.md "The block editor"; `PageController::copyLink`,
`markdown::formatReference`):

| Command | Writes |
|---|---|
| Odwołanie do bloku (Ctrl+Shift+C) | `((id))` |
| Link do bloku | `[[Page#^id]]` |
| Link do nagłówka | `[[Page#Heading]]`; a heading name the page has twice: the shortest unique heading path `[[Page#Parent#Heading]]`, else `[[Page#^id]]` |
| Osadzenie | `!((id))`; on a heading its section `![[Page#Heading]]` (same fallbacks, `![[Page#^id]]`) |
| Kopiuj link do strony | `[[Page]]` |

* `Page` is the same in every copied link — the editor's, the page menu's and the folder tree's
  "Kopiuj link", `brulion-cli block link` (`notebook::pageLinkTarget`): the page's title, or, when
  the title leads to another page (the same name in another folder), the shortest unique path
  `VaultIndex::linkTarget()` gives (`[[Projekty/Dom]]`, not `[[Archiwum/2025/Projekty/Dom]]`; what
  `[[` completion and moves write too); a daily note is named by its file name
  (`[[2026-10-22#^abc123]]`), also when a page elsewhere has that name.
* The block kinds use the block's `^id` or its Logseq `id::` (a UUID, as it is); a block without
  one gets a generated id (§4 "Block id": placement, 6 characters `[a-z0-9]`, never one any block
  of the vault has, also a block without text) as one undoable edit. Heading and page links change
  nothing. A heading link breaks when the heading's text changes (as in Obsidian); `((id))`
  survives that and moving the block to another page.
* The documentation notebook gets no new ids (its pages are brought up to date by new versions
  only while unchanged); its heading and page links, and blocks with an id, are copied.

## 3. Tags

`#tag`, `#nested/tag`, `#[[tag with spaces]]`. Tag body: Unicode letters/digits, `_`,
`-`, `/`; must contain at least one non-digit (`#1` is not a tag); must be preceded by
start of line, whitespace or `(`. Never recognised inside code spans, fenced code,
formulas (§11), URLs or link targets. A heading `# Title` is not a tag. Frontmatter
`tags: [a, b]` or `tags: a, b` also tags the page. Tags compare case-insensitively; display
keeps the first-seen casing.

### Tagi jako linki do stron / Tags as page links

A tag names a page, as in Logseq: `#praca` is a link to the page "praca" and every tag feature
keeps working (counts, the tag list and overview, `tag:` queries with sub-tags, colours). This
covers `#tag`, `#[[multi word]]`, `#a/b`, the items of `tags::` / `tag::` properties and of the
frontmatter's `tags:`.

* **Resolution** — like `[[tag]]` written at the vault root, so one tag always means one page
  wherever it is written: 1) exact vault path (± `.md`): `#Projekty/Dom` is `Projekty/Dom.md`,
  Logseq's namespace tag `#a/b` is `a/b.md`; 2) base name (also a path suffix: `#Stare/Dom`),
  shortest path, then alphabetical; 3) frontmatter / Logseq aliases (`#Janek` opens "Jan
  Kowalski"). Notes only: a tag never names an attachment, and a file that is no note does not
  hide one (with a `TODO` file at the root `#todo` names `Projekty/TODO.md`). Case-insensitive
  and NFC, **not** diacritic-insensitive (`#zolw` ≠ `Żółw`) — exactly like links. Unlike
  `[[dom]]` there is no "relative to the linking file" step: with duplicate base names `#dom`
  names the shortest path everywhere, `#Archiwum/Dom` the other one.
* **Click** — a tag opens its page. A page that does not exist yet opens empty and is created by
  its first content, where a link's would be (`newPagesFolder`; `#praca/raporty` →
  `praca/raporty.md`, a folder; a date-like tag `#2026-10-13` opens that day's journal), named as
  the tag was first written. The tag overview (the pages and blocks carrying the tag, sub-tags,
  "Zadania z tym tagiem") stays one tap away. In Tasks a click on a tag filters the list;
  Ctrl+click opens the page; a long press or right-click opens the tag's menu (Filtruj po tagu,
  Otwórz stronę tagu, Widok tagu, Otwórz w panelu bocznym, Kopiuj tag — `#tag`, or `#[[tag ze
  spacjami]]`), as in the editor, where the tag's menu has "Zadania z tym tagiem" instead.
* **References** — a page's "Odnośniki" list the blocks that link to it *and* the blocks tagged
  with a tag naming it (one row per block), and pages tagged as a whole (frontmatter `tags:`,
  leading `tags::`) as a group marked "cała strona"; self-references are excluded. Exactly the
  tag: sub-tags (`#praca/raporty` under "praca") are pages of their own (chips in the page
  header). A page that exists only as a tag still lists them, and the unresolved `[[links]]` with
  its name.
* **Rename** — renaming a page rewrites the tags that name it (counted in the confirmation):
  `#old` → `#New` or `#[[New name]]`, `#[[old]]` keeps the bracket form, `tags:: old` and
  frontmatter items in place (byte-preserving; a YAML value that would read differently is
  rewritten as a list). Only tags that name this page change (with a duplicate name, `#dom` or
  `#Stare/Dom` naming the other page stays). Sub-tags are not renamed; an alias tag (`#Janek`)
  still names the page. A folder rename rewrites path-qualified tags (`#Projekty/Dom` →
  `#Archiwum/Dom`); a move keeps `#Dom` while it still names the page and writes the shortest path
  that does (`#Projekty/Dom`) when it would not (§2 "Rewriting on rename and move"). The `tag:`
  filters of ```` ```query ```` blocks follow the same way — exactly the tag, `tag:old/x` stays
  (§2).
* **Queries** — `tag:` stays tags-only; `ref:` also matches tags naming the page (§10).
* **Graph** — a tag naming a page is an edge to it; "#tag" nodes remain only for tags that name
  no page. Unresolved tags are never "missing pages" (hex colours, issue numbers…).
* **Writing** — `#` completion offers tags and pages; a name that is no simple tag body is
  written `#[[C++]]`, `#[[Jan Kowalski]]` (`markdown::tagSyntax`).
* **Other apps** — Logseq reads tags the same way (the Logseq export writes
  `#[[Projekty/Dom]]` when the page has another name there); Obsidian keeps tags and pages
  separate (its export leaves tags unchanged). The HTML and Pandoc exports link a tag to the
  page it names when that page is exported.

## 4. Blocks and outline

* Every **list item** is a block. Nesting by indentation (tab = one level; for spaces the
  child must be indented past the parent's marker). Markers: `-`, `*`, `+`, `1.`, `1)`.
* Outside lists, each paragraph, heading, fenced code, table, blockquote/callout, `---`
  rule is a top-level block (depth 0). A display formula between two lines that are only `$$`
  stays in one block whatever its lines start with (§11).
* **Fenced code**: three or more backticks or tildes at any indentation (also as the text of a
  list item); a backtick fence's info string has no backtick. The block ends at the first line
  with the same character, at least as many of them and nothing else but spaces and tabs (a
  no-break space makes it a code line). Without one it runs to the end of its container: the
  page, a quote, or a list item up to the next line indented no deeper than its marker. A longer
  outer fence (````` ```` `````) holds ```` ``` ```` lines as code. A code block's id is a standalone
  `^id` line after its closing fence (in a list item also `id::`).
* **Block id**: ` ^abc-123` (caret + `[A-Za-z0-9-]+`) at the end of a block's first or last
  line (Obsidian style; Brulion writes it at the end of the first line for list items and
  at the end of the last line for paragraphs). Generated ids are 6 chars `[a-z0-9]`.
* **Block properties** (Logseq/Dataview style): lines `key:: value` directly in the block
  (continuation lines of a list item, or lines of a paragraph). Keys `[A-Za-z0-9_-]+`
  (lower-cased). Values: raw string; comma-separated values that are all `[[links]]` or
  `#tags` are parsed as lists. Logseq's `id:: <uuid>` property is treated as the block id.
* **Page properties**: YAML frontmatter at the very start (`---` … `---`), or `key:: value`
  lines at the very top of the file before any other block (Logseq style).

### Tryb notatki / Note mode

Every note is shown in one of two modes; the Markdown is the same in both:

* **outline** (*konspekt*, Logseq-like) — the block editor: a bullet per block, Tab indents,
  folding, zoom, guide lines;
* **document** (*dokument*, Obsidian-like live preview) — a continuous formatted document:
  paragraphs and headings without bullets, lists drawn as ordinary Markdown lists; clicking a
  block edits its Markdown in place.

An explicit choice is stored in the note itself — portable and synced — as the frontmatter key
`brulion`, following the Obsidian plugin convention (like Kanban's `kanban-plugin: board`):

```yaml
---
brulion: document   # or: outline
---
```

**Resolution** (`markdown::resolveNoteMode`, first match wins):

1. the frontmatter key `brulion` (lower-case key) with the value `document` or `outline`
   (case-insensitive, may be quoted); any other value is ignored;
2. daily notes → `journalMode` of `.brulion/config.json` (default `outline`);
3. other pages → their content: the note's top-level blocks as defined above (YAML
   frontmatter and Logseq page properties do not count; headings, code, tables, quotes… count
   as non-list blocks; nested list items are part of their top-level item). With **at least
   two** such blocks: **≥ 70 %** list items → outline, **≤ 30 %** → document;
4. otherwise (empty, a single block, more than 30 % but less than 70 % list items) →
   `pageMode` (default `document`).

So imported Logseq pages (bullets only) open as outlines and Obsidian notes (headings and
paragraphs) as documents without anything being written.

**Writing.** The key is written only on an explicit choice: by the new-page dialog when the chosen
mode differs from what the resolution gives for the initial content (template), and by every
mode switch (`markdown::setNoteModeKey`). A switch changes nothing but that one entry: an
existing `brulion:` entry is replaced in place (keeping its line ending; duplicates are dropped),
a missing one is appended before the closing `---`, and a note without frontmatter gets
`---` / `brulion: …` / `---` in front (after a BOM, with the note's line ending). Other keys,
comments, their order, line endings and the body stay byte-for-byte. Removing the key (no UI
offers it) also removes a frontmatter block that holds nothing else afterwards. Frontmatter that
cannot be edited safely (malformed YAML, a `brulion` key that is not on a line of its own at
the start of the line, e.g. in an indented mapping, or YAML that is no mapping at all — a list,
a single value, or plain text between a first `---` and a later one used as horizontal rules) is
left unchanged — a second `brulion` entry is never added, because other YAML readers refuse
duplicate keys. Obsidian and Logseq show `brulion` as an ordinary page property.

## 5. Tasks (Obsidian Tasks compatible)

A list item starting with a checkbox:

| Checkbox | Status |
|---|---|
| `- [ ]` | `todo` |
| `- [/]` | `doing` |
| `- [x]` / `- [X]` | `done` |
| `- [-]` | `cancelled` |
| `- [>]` | `deferred` |

Logseq markers are also tasks: `- TODO`/`- LATER` → todo, `- DOING`/`- NOW` → doing,
`- DONE` → done, `- CANCELED`/`- CANCELLED` → cancelled, `- WAITING` → deferred.
Logseq priorities `[#A]`/`[#B]`/`[#C]` map to high/medium/low.

Metadata tokens anywhere on the task's first line, outside code spans and formulas (Obsidian
Tasks signifiers); dates are `YYYY-MM-DD`, Brulion additionally allows an optional ` HH:mm`
after due/scheduled/start:

| Token | Field |
|---|---|
| `📅 2026-10-15` | due (deadline) |
| `⏳ 2026-10-15` | scheduled |
| `🛫 2026-10-15` | start |
| `➕ 2026-10-14` | created |
| `✅ 2026-10-14` | done date |
| `❌ 2026-10-14` | cancelled date |
| `🔁 every week` | recurrence (see §7) |
| `🔺` `⏫` `🔼` `🔽` `⏬` | priority highest / high / medium / low / lowest |

Logseq planning lines inside the block are understood too:
`SCHEDULED: <2026-10-15 Thu 10:00 .+1w>` and `DEADLINE: <2026-10-20 Tue>` (repeaters
`+1d`, `++1w`, `.+1m`, `+1y` → recurrence).

**Completing a recurring task** inserts the next occurrence as a new task line directly
above the completed one (dates shifted by the rule), exactly like Obsidian Tasks, and the
completed line gets `✅ <today>`.

## 6. Reminders and alarms

| Syntax | Meaning |
|---|---|
| `⏰ 2026-10-15 09:00` | reminder notification at local date-time |
| `⏰ 2026-10-15` | reminder at `defaultReminderTime` |
| `⏰! 2026-10-15 06:30` | **alarm**: loud, high-priority, stays until dismissed |
| `(@2026-10-15 09:00)` | obsidian-reminder plugin syntax, read-only compatibility |

Reminders may appear in any block (not only tasks), outside code spans and formulas. A block
may carry several. A `🔁` rule in the same block makes the reminder recur. A reminder on a
task whose status is `done` or `cancelled` never fires. Times are **local wall-clock time**
(no time zone stored). The notification title is the block text with all tokens stripped;
tapping it opens the note at that block.

## 7. Recurrence rules

Canonical (written by Brulion, compatible with Obsidian Tasks): `every day`,
`every 3 days`, `every weekday`, `every week`, `every 2 weeks`,
`every week on Monday, Friday`, `every month`, `every month on the 15th`,
`every month on the last`, `every year`, `every 2 years`; optional suffix `when done`
(next date computed from completion date instead of the original date).

Polish input is accepted and normalised to canonical form when Brulion rewrites the line:
`codziennie`, `co 3 dni`, `w dni robocze`, `co tydzień`, `co 2 tygodnie`,
`co tydzień w poniedziałek, piątek`, `co miesiąc`, `co miesiąc 15.`,
`co miesiąc ostatniego`, `co rok`, `co 2 lata`, suffix `po wykonaniu`.

The token menus (a right-click or long press on a chip, docs/UI.md "Token and link menus")
rewrite only the token they act on: a 🔁 rule gets the canonical text (only the rule's words —
in `🔁 co tydzień kupić mleko` the rule is `co tydzień`, the rest stays text), a priority gets
its emoji (a Logseq `[#A]`/`[#B]`/`[#C]` stays Logseq: highest and high → `[#A]`, medium →
`[#B]`, low and lowest → `[#C]`), a date or reminder only its `YYYY-MM-DD[ HH:mm]` value (`⏰` /
`⏰!` for an alarm; `(@…)` is written as `⏰` then), a link to a day only the date part of its
target, in the format it was written in. A token removed takes one adjacent space with it. A
rule set to the rule it already is (in any phrasing: the reminder dialog with only the time
changed) stays as written. In a table cell only the cell's text changes — the other cells, the
padding and the pipes stay. Everything else in the file — other tokens, indentation, line
endings — stays byte for byte. Logseq planning lines (`SCHEDULED:` / `DEADLINE:`) have no
token menu.

## 8. Natural-language dates (editor input only)

The date picker / slash commands accept Polish and English phrases and insert canonical
tokens: `dziś`, `jutro`, `pojutrze`, `w piątek`, `w przyszły poniedziałek`, `za 2 dni`,
`za 3 godziny`, `za tydzień`, `15 października`, `15.10`, `15.10.2026 18:30`, `o 9`,
`jutro o 7:30`, `today`, `tomorrow`, `next friday`, `in 2 hours`, `Oct 15 6pm`.
Files never contain natural-language dates — only `YYYY-MM-DD[ HH:mm]`.

## 9. Calculations

Brulion's own expression engine (`src/core/calc`, no script evaluation): numbers, variables,
percentages, units, currencies and functions. Results are formatted in the UI language
(Polish `42,5`, English `42.5`) and shown next to the input; the file keeps only the inputs.

### Calc blocks
````
```calc
cena = 12.50          → 12,5
ilość = 4             → 4
cena * ilość          → 50
rabat = 15%           → 15%
ans - rabat           → 42,5
5 km to mi            → 3,11 mi
20% of 150            → 30
sum                   → 139
```
````
* The `→ …` column is what Brulion shows next to each line, not part of the file.
* Each line is evaluated top-to-bottom with a shared scope (one expression, or several
  separated by `;`).
* `name = expr` assigns a variable (Unicode names allowed, e.g. `ilość`); `f(x) = x^2`
  defines a function. `ans` = the previous result (percentages don't replace it).
* `sum`/`total`/`suma`/`razem` alone on a line = the sum of the results above up to the
  previous blank line; `avg`/`average`/`mean`/`średnia` = their average. Percentages,
  aggregate lines and errors don't count; when the results don't add up (a length and plain
  numbers) only the plain numbers are summed — in the example `3,11 mi` is left out:
  12,5 + 4 + 50 + 42,5 + 30 = 139.
* Percent sugar: `x + 10%`, `x - 10%`, `20% of x` / `20% z x` / `20% od x`,
  `x as % of y` / `x jako % z y`; a percentage variable keeps its meaning (`cena - rabat`).
* Units and conversions: `5 km`, `3 h + 20 min`, `100 km / 2 h` → `50 km/h`,
  `5 km to mi` (also `in`, `na`, `w`), `20°C to °F`. Currencies are tags that survive
  arithmetic: `12 zł + 5 zł` → `17 zł`, `€10`, `$5`, `5 PLN`; different currencies are never
  converted.
* Functions (case-insensitive, `,` or `;` between arguments): `sum`/`suma`, `avg`/`średnia`,
  `min`, `max`, `count`, `median`, `round(x[, n])`/`zaokr`, `floor`, `ceil`, `abs`, `sqrt`,
  `pow`, `exp`, `ln`, `log`, trigonometry, `if(cond, a[, b])`/`jeżeli`/`jeśli`, …
* `#` or `//` start a comment (a whole line or after an expression); text before `:` is a
  label (`Czynsz: 1800` → 1800).
* Decimal comma is accepted (`12,5`) outside function arguments, and everywhere in a call that
  uses `;` between its arguments (`round(2,345; 2)`, `max(1,5; 2)`). In a call without `;` a
  comma written between digits without spaces (`2,5`) is read the way the builtin function
  allows: a decimal comma when only that gives a valid number of arguments (`sqrt(2,25)` = 1,5,
  `floor(1,5)` = 1), a separator when only that does (`pow(2,5)` = 32); when both fit
  (`round(2,5)`, `zaokr(netto * 1,23)`, `max(1,5)`) the line is an error ("Niejednoznaczny
  przecinek…") instead of a guess. `round(2, 5)` with a space, runs like `sum(1,2,3)` and calls
  of the notebook's own functions take the commas as separators. Also `1 000 000`, `1e3`,
  English `1,000.5`.
* Display rounding — only the display, values keep full precision (`5 km to mi to km` is
  `5 km` again): plain numbers up to 10 significant digits; **quantities with a unit at most
  2 decimals** (`3,11 mi`, `3,33 h`; magnitudes below 1 keep two significant digits:
  `0,0042 km`); money exactly 2 decimals when not whole (`12,50 zł`, `17 zł`).

### Inline calculations
Inline code starting with `=`: `` `= cena * 2` `` renders as the result — in any block text,
table cells included (spaces or a line break may come before the `=`). Variables from calc
blocks earlier on the same page are in scope, as are numeric page properties (frontmatter and
`key:: value`; `due-date` becomes `due_date`); the result follows them when they change.

### Table formulas
Any GFM table body cell whose content starts with `=` is a formula, rendered as its result:

| Ref | Meaning |
|---|---|
| `A1`, `C3` | cell by column letter / data-row number (row 1 = first row under the header) |
| `A1:A5`, `B2:C4`, `B:B` | range, whole column |
| `[Cena]` | cell in the **current row** of the column whose header is `Cena` (case-insensitive) |
| `above`, `below`, `left`, `right` (`powyżej`, `poniżej`) | all numeric cells above / below (same column), left / right (same row) |
| `sum avg min max count round abs if …` | the calc functions |

Example: `| Suma | =sum(above) |`, `| | =[Cena]*[Ilość] |`. Plain cells are read like
people write them (`12,50 zł`, `23%`); empty cells count as 0, ranges skip text. Formulas
are evaluated in dependency order and may use the page's calc variables. Errors render as
`#ERR` with a tooltip; cycles render `#CYCLE`.

## 10. Queries

A fenced block with language `query` renders live results in the editor (tasks can be checked
off right there). Without `limit:` it shows at most 500 items (the total is still reported).

````
```query
# Praca na najbliższy tydzień
type:tasks status:open tag:praca due:<=+7d   // otwarte zadania #praca
sort:due,-priority limit:20
```
````

**Syntax.**
* Tokens are separated by spaces or line breaks. All filters are **ANDed**; the
  comma-separated values of one filter are **ORed** (`status:todo,doing`, `due:today,+7d`).
* `-` before a filter negates it (`-tag:prywatne`, `-path:journals/`); before a word or phrase
  it excludes it (`-kupić`, `-"bez daty"`). `sort`, `group`, `limit` and `type` cannot be
  negated.
* Comments: `//` up to the end of the line; a line starting with `#` followed by a space (or a
  lone `#`) is a comment line (`# Moje zadania`). `#praca` is a tag filter; a `#` in the middle
  of a line is plain text.
* Quotes `"…"`, `„…”`, `“…”`, `«…»` group a phrase; `[[…]]` may contain spaces.
* Shorthands: bare words and quoted phrases = `text:`, `#tag` = `tag:tag`,
  `[[Strona]]` = `ref:Strona`. A value with `//` right after the colon is text (URLs). Since a tag
  names a page (§3), `ref:` matches the page's tags too, while `tag:` stays tags-only (with
  sub-tags).
* Renaming or moving a page, a folder or a tag's page rewrites the `tag:`, `#tag`, `ref:`,
  `[[…]]`, `page:` and `path:` values that name it, so that they keep matching what they matched
  (§2 "Rewriting on rename and move").
* Keys and keyword values are case- and diacritic-insensitive (`ścieżka:` = `sciezka:`,
  `STATUS:Zrobione`) and have Polish spellings. An unknown key or value shows a helpful error
  (in the UI language) instead of results.

| Key (aliases) | Values |
|---|---|
| `type:` (`typ:`) | `tasks` (`zadania`), `blocks` (`bloki`), `pages` (`strony`, `notes`, `notatki`), `reminders` (`przypomnienia`). Default: `tasks` when a task filter, sort or group is used (status, priority, due, scheduled, start, done, created), `reminders` when `remind:` is, else `blocks`. |
| `status:` (`stan:`) | `todo` (`do-zrobienia`), `doing` (`in-progress`, `w-toku`), `done` (`zrobione`, `wykonane`), `cancelled` (`canceled`, `anulowane`), `deferred` (`waiting`, `odłożone`), `open` (`otwarte`) = todo + doing, `closed` (`zamknięte`) = done + cancelled, `all` (`any`, `wszystkie`) |
| `priority:` (`priorytet:`, `prio:`) | `highest` (`najwyższy`), `high` (`wysoki`), `medium` (`średni`), `low` (`niski`), `lowest` (`najniższy`), `none` (`normal`, `brak`, `normalny`) = no priority set |
| `tag:` (`tags:`, `tagi:`), `#tag` | tag with or without `#`; `projekt` also matches nested `projekt/dom`. A block has its own tags, its ancestors' and the page's tags. |
| `page:` (`strona:`) | page title, path, alias or link target; `*` wildcards (`page:*akup*`); `this` (`current`, `ta`, `ten`, `bieżąca`, `tutaj`) = the page containing the query |
| `path:` (`ścieżka:`, `folder:`) | path prefix (`Projekty/`, `journals/2026-10`); with `*` a wildcard pattern matched from the start (`journals/*-13`) |
| `ref:` (`link:`, `links:`, `linki:`, `odnośnik:`), `[[Strona]]` | items linking to a page (`ref:[[Jan Kowalski]]`, `ref:this`); a block counts when it or an ancestor holds the link **or a tag naming the page** (`ref:Dom` matches `#dom`, `#[[Dom]]`, `#Remont` for an alias; exactly the tag, no sub-tags; a page that does not exist: the tag of that name); a page counts with any of its tags |
| `text:` (`tekst:`), words, `"phrase"` | contains, case- and diacritic-insensitive (`zolw` finds `żółw`) |
| `due:` (`termin:`, `deadline:`), `scheduled:` (`zaplanowane:`, `plan:`), `start:` (`początek:`), `done:` (`zrobione:`, `wykonane:`), `created:` (`utworzone:`) | date condition on the task's 📅 ⏳ 🛫 ✅ ➕ date |
| `remind:` (`reminder:`, `przypomnienie:`) | date condition on ⏰ reminders |
| `date:` (`data:`, `dzień:`) | date condition on the journal date of the page |
| `sort:` (`sortuj:`) | comma list of `due` (`termin`), `scheduled` (`zaplanowane`), `start`, `done` (`zrobione`), `created` (`utworzone`), `remind` (`przypomnienie`), `date` (`data`), `priority` (`priorytet`), `status`, `page` (`title`, `strona`, `tytuł`), `path` (`ścieżka`); `-key` descending. Missing values sort last. Default order: tasks and blocks by path then line, reminders by time, pages by title. |
| `group:` (`grupuj:`) | `page` (`strona`), `due` (`termin`), `scheduled` (`zaplanowane`), `date` (`data`), `tag` (one group per tag), `status`, `priority` (`priorytet`) |
| `limit:` | 1 … 10000 |

**Date conditions.** An optional operator `<`, `<=`, `>`, `>=` (or `=`) followed by an
expression; dates compare by day, and an item without that date matches only `none`:
* days: `today` (`dziś`, `dzisiaj`), `tomorrow` (`jutro`), `yesterday` (`wczoraj`),
  `pojutrze`, `przedwczoraj`, `2026-10-15`;
* offsets from today: `+7d`, `-2w`, `+1m`, `+1y`, `7d`, `+7` (days); Polish units `+2t`,
  `+1r`, `+3dni`, `+2tyg`;
* periods: `this-week`, `next-week`, `last-week`, `this-month`, `next-month`, `last-month`,
  `this-year`, `next-year`, `last-year` (`week` = `this-week`, …), Polish `ten-tydzień`,
  `przyszły-tydzień`, `zeszły-tydzień`, `ten-miesiąc`, `przyszły-miesiąc`, `ten-rok`, …
  (weeks start on the vault's `weekStartsOn`);
* ranges `A..B`: `today..+7d`, `2026-10-13..2026-10-16`;
* keywords (no operator): `overdue` (`zaległe`) — an open task's date before today, for
  `remind:` before now; `none` (`brak`) — no such date; `any` (`dowolna`) — has one.

Without an operator the date must lie inside the expression's day or period; `<` means before
its start, `<=` up to its end, `>` after its end, `>=` from its start (`due:<=+7d` = due
within a week, overdue included).

**Results.** `type:blocks` hides blocks whose ancestor also matched (they are shown inside
it), frontmatter and `query` blocks. `type:pages` accepts only `tag`, `page`, `path`, `ref`,
`text` and `date` filters, sorting by `page`, `path`, `date` and grouping by `page`, `tag`,
`date`.

More examples:

````
```query
// Przypomnienia na ten tydzień (type:reminders, po kolei)
remind:this-week
```
```query
type:pages tag:projekt sort:page        // strony projektów
```
```query
ref:this                                // bloki, które linkują do tej strony (albo mają jej tag)
```
```query
type:tasks ref:this status:open group:page
```
````

## 11. Other rendering extensions (Obsidian compatible)

`==highlight==`, `%%hidden comment%%`, callouts `> [!note] Title` (`note`, `tip`,
`warning`, `danger`, `info`, `todo`, `quote`), footnotes `[^1]`, GFM tables, task lists,
strikethrough, autolinks. Raw HTML in notes is rendered as escaped text (never
executed).

### Math formulas

Formulas are written as LaTeX and drawn by Brulion as Unicode rich text (no KaTeX, no web
view, nothing is executed); the note keeps the LaTeX source. Inline: `$E = mc^2$`. Display,
also over several lines: `$$\frac{a}{b}$$`. A fenced block:

````
```math
\sum_{i=1}^{n} i = \frac{n(n+1)}{2}
```
````

The fence languages are `math`, `latex` and `tex`; such a block is edited in place (the
LaTeX source while editing, the formula otherwise), and Enter starts a new formula line.
Supported: Greek letters, common operators, arrows and set symbols; big operators (`\sum`,
`\int`, `\bigcup`, `\bigoplus`…; `\limits`, `\nolimits` and the styles `\displaystyle`…
are accepted and show nothing); `^` and `_` (also `^{…}`, `_{…}`); `\frac`/`\dfrac`/`\tfrac`
(vulgar fractions where one exists), `\sqrt[n]{…}`; `\text` (with `\&`, `\%`, `\$`, `\#`,
`\_`, `\{`, `\}`), `\operatorname`, `\mathbf`, `\mathbb`, `\boldsymbol`/`\bm`, `\vec`,
`\hat`, `\bar`, accents; `\left…\right`, `\middle` and the sizes `\big`, `\Big`, `\bigg`,
`\Bigg` (also with `l`, `r`, `m`) before a delimiter: a character (`<` and `>` are ⟨ ⟩, `.` is
none), an escaped one (`\{`, `\}`, `\|` is ‖) or a command (`\langle`); matrices (`pmatrix`,
`bmatrix`, `Bmatrix`, `vmatrix`, `Vmatrix`), `cases`, `aligned` and `array`, also nested (only
an environment's own `&` and `\\` separate its cells and rows, not those in braces or escaped).
Unknown commands stay as written; a part nested deeper than 48 levels (braces, arguments,
environments) is shown as its source. Spaces between atoms are ignored as in LaTeX (`\ `,
`\,`, `\quad` are explicit spaces); lowercase Greek is italic, uppercase upright. The HTML
export shows the formula (display formulas are centred), also a ```` ```math ```` block that is a
list item of its own (what `/Wzór matematyczny` writes in an outline or a journal); other fenced
list items are exported as code blocks.

Where a formula is (in a block's inline text, after code spans and comments are masked):

* `$$…$$` runs to the next `$$`, also over several lines; content of spaces only is no formula.
* For the block structure (§4), a line that is only `$$` (any indentation, also inside a list
  item or a quote) opens a display formula that runs to the next line that is only `$$`, like a
  fence: the lines between belong to the formula and its block even when they look like block
  syntax (`+ b`, `- c`, `> d`, `1. e`, `# f`, `---`). A blank line before that closing line ends
  it (TeX allows none in a formula): the `$$` line is then ordinary text and the following lines
  are blocks as usual. A `$$` that shares its line with the formula does not hold the lines
  together (`$$a` / `+ b$$` breaks at `+ b`) — write such formulas between `$$` lines or in a
  ```` ```math ```` block. The editor writes a blank line after a block whose `$$` line it leaves
  unclosed when a later `$$` line would close it (the blocks between stay blocks), and never
  makes a line that is only `$$` a task (`- [ ] $$` opens no formula).
* Inline `$…$` follows pandoc's `tex_math_dollars`, tightened for prices and variables: the
  opening `$` has a non-space character right after it and does not directly follow a letter
  or digit; the closing `$` has a non-space character right before it and is not followed by
  a letter or digit. A `$` with a space before it never closes a formula: the attempt ends
  there, and that `$` may open a formula itself (`$5 a wzór $x$`: only `$x$`). An inline
  formula may go on to the next line of the same block but never crosses a blank line. Text,
  not formulas: `$5 i $10`, `15$, 10$`, `10$/h … 15$/h`, `$PATH:$HOME`, `Niech$x$`.
* A `$` or `$$` without a partner is text and hides no later formula.
* What starts inside a formula belongs to it — in the editor, the HTML export and the index
  (tags, backlinks, tasks, reminders) alike, as with a code span: a `#tag`, `[[link]]`, URL,
  task date (`📅 …`), reminder (`⏰ …`) or `key:: value` there is part of the formula. A
  block's id (`^id`, `id::`) and `collapsed::` stay the block's even there, so references to it
  keep working. Whatever starts first wins: a formula in a link label (`[$x^2$](Wykres.md)`)
  leaves the link a link.
  The formula is found in the block's text as the editor shows it: a list item's blank lines
  and code lines end an inline formula like a blank line.
* `\$` is a literal dollar: importers that write plain text (HTML, Evernote, Google Keep, Notion
  databases, Trilium, iCalendar) escape the opening `$` of whatever would be a formula by these
  rules, block by block and table cell by table cell (`Cena \$10, Netflix 15$`, `\$x$`); dollars
  that pair with nothing stay as written, code, link targets, bare URLs and property values are
  never changed (when the opening `$` is in one of them, the closing one is escaped). `\(`, `\)`,
  `\[` and `\]` are ordinary escapes (literal brackets, as the importers write them), not math
  delimiters — as in Obsidian. (Brulion 0.3.3–0.3.5 read `\(…\)` and `\[…\]` as formulas; such
  notes now show the brackets and should use `$…$` / `$$…$$`.)

Polish typography (display only, the file is unchanged): a one-letter word (`a`, `i`, `o`, `u`,
`w`, `z`) never ends a line — the space after it is shown as a no-break space.

## 12. Templates

Files in `templatesFolder`. Variables: `{{date}}` (yyyy-MM-dd), `{{date:FORMAT}}`
(Qt date format), `{{time}}` (HH:mm), `{{title}}`, `{{weekday}}` (localised),
`{{cursor}}` (caret position after insert). `journalTemplate` is applied to new daily notes:
a day without a note shows the template as its starting text, and the note is created — with
the template, filled for that day — by the first change; looking at a day never creates it.
