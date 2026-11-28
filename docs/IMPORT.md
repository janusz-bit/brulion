# Importing notes from other apps

The import screen (**More → Import notes**, on the phone also from the drawer — **Menu** in the
bottom bar) accepts files, ZIP archives and whole folders. Choose the app the notes come from —
the export steps are under its name — or point to the files right away: Brulion recognises the
format itself. Before writing anything it shows a preview (pages, daily notes, attachments, renamed
files, warnings), and after the import a report. Pages go to the import folder (`Import/<app>` by
default), daily notes to the journal, attachments to `assets/<import folder>/`. Nothing is
overwritten: when a name is taken, the new page gets the suffix " 2", and links to it are
adjusted. Details for the phone:
[ANDROID.md](ANDROID.md#importing-notes-obsidian-logseq-notion).

Supported apps and formats: Obsidian, Logseq, Notion, Evernote, Google Keep,
**[OneNote](#onenote)**, **[Apple Notes](#apple-notes)**, **[Microsoft To Do](#microsoft-to-do)**
(tasks), **[Todoist](#todoist)**, Joplin, Simplenote, Roam Research, **Trilium Notes**, Bear,
**[Airtable](#csv-and-airtable)**, Markdown/TXT files, HTML, **[spreadsheets (CSV)](#csv-and-airtable)**
and iCalendar calendars. The same importers run on the command line: `brulion-cli import <path>`
(docs/CLI.md).

Text from apps that do not write Markdown (HTML, Evernote, Google Keep, Notion databases,
Trilium, calendars) stays text: characters that mean something in Brulion get a `\`. This also
applies to the `$` character — but only where two `$` would form a formula ("Price $10,
Netflix 15$", "$x$"); single prices ("$5 and $10", "15$") stay unchanged. Code and link
addresses are not changed.

## Obsidian folder without importing

An Obsidian vault does not need to be imported — including one you already sync with Syncthing.
Brulion reads the same `.md` files, so you can simply open the vault folder as a notebook (on the
start screen or via *Switch notebook → Open another notebook…*) on every device where Syncthing
keeps it; on Android Brulion asks for "All files access" (details: [ANDROID.md](ANDROID.md)). Both
apps can take turns working on the same folder.

**Recommendation:** stay with Syncthing and open the folder. Do *not* additionally turn on
Nextcloud sync for this notebook — two sync mechanisms on one folder send the same changes along
two paths and multiply conflicts. Nextcloud makes sense where there is no Syncthing (the browser,
notes in the app's own storage on the phone). Choose import only when you want a separate copy
(e.g. to try Brulion without touching the vault): imported notes go to another notebook and are
no longer the same files.

**What works right away:** links `[[Page]]`, `[[Page|alias]]`, `[[Page#Heading]]`,
`[[Page#^id]]`, embeds `![[…]]` (also of pictures in any folder), Markdown links, the YAML
header (`tags`, `aliases`), tags, `> [!note]` callouts, `==highlights==`, `%%comments%%`,
`$…$` formulas and tasks in the syntax of the **Tasks** plugin (`📅 ⏳ 🛫 ✅ 🔁`) — Brulion
writes them that way too. Notes with headings and paragraphs open as documents, as in Obsidian.
Renaming or moving a page fixes the links to it in all notes (as in Obsidian). The `.obsidian/`
folder (settings, plugins, themes) is not shown and never changed (Brulion only reads a few
settings from it, see below), and Syncthing's conflict copies of notes (`*.sync-conflict-*`) are
not shown as pages, only in the sync center as conflicts to merge.

**What Brulion takes over by itself:** as long as the notebook has no `.brulion/config.json`, it
takes the journal and attachment settings from Obsidian's settings in `.obsidian/` — it changes
nothing there and writes nothing itself:

* **Journal folder** — from `.obsidian/daily-notes.json` (the `folder` field; empty or `/` is the
  vault's top folder). Without this file (Obsidian writes it only after the *Daily notes* settings
  are changed) Brulion assumes Obsidian's default top folder only when a day `YYYY-MM-DD.md`
  already lies there and the *Daily notes* plugin is not turned off
  (`.obsidian/core-plugins.json`).
* **Journal file names** — the pattern from the `format` field, rewritten from Moment notation
  (Obsidian) to Qt notation (Brulion): `YYYY-MM-DD` becomes `yyyy-MM-dd` (the default in both),
  `DD.MM.YYYY` becomes `dd.MM.yyyy`, `[Day] YYYY-MM-DD` becomes `'Day' yyyy-MM-dd`, and `/` in the
  pattern means subfolders, as in Obsidian. Brulion does not use patterns with elements that have
  no counterpart (`Do` — "27th", week numbers `ww`/`WW`, day of the year `DDD`, hours): it stays
  with `yyyy-MM-dd`, and the settings show a warning — then enter the file names yourself. Brulion
  writes month and day names (`MMMM`, `dddd`) in English, also when Obsidian writes them in Polish
  (a warning in the settings).
* **Journal template** — from the `template` field, with the `.md` extension added (Obsidian
  stores the path without it), e.g. `Templates/Daily.md`; without a template in Obsidian, new
  days start empty. The variables `{{date}}`, `{{time}}`, `{{title}}` work in both apps.
* **Templates folder** — from `.obsidian/templates.json` (the *Templates* plugin, the `folder`
  field).
* **Attachments folder** — from `.obsidian/app.json` (*Settings → Files and links → Default
  location for new attachments*): the chosen folder or the vault's top folder. Brulion has no
  "next to the note" location (`./`): it stays with `assets/` and shows a warning. Pictures already
  inserted work in every case.

*Settings → Notebook* tells what was taken over. Changing any notebook setting saves all of them
(the taken-over ones too) in `.brulion/config.json`; from then on that file applies, and changes
in Obsidian's settings no longer count. Until then, Brulion reads `.obsidian/` every time the
notebook is opened and during the periodic folder check (every few minutes and on returning to
the app). The **Save to notebook** button saves the taken-over settings right away — useful when
you open the same notebook in the browser through Nextcloud: sync does not transfer `.obsidian/`,
so without `config.json` the browser would have Brulion's default settings.

A folder that Brulion already used with default settings (without `config.json`) does not change
after an update: it does not take over the journal when some note already lies in `journals/`,
the attachments folder when some file lies in `assets/`, the templates folder when some file lies
in `templates/`. Files in a folder that Obsidian itself chose for that setting do not count here
(e.g. Obsidian's days in `journals/` or `Journals/`): then Brulion takes over the name pattern and
the template, and still recognises its own earlier days `YYYY-MM-DD.md`. Brulion checks this again
when files in these folders appear or disappear (e.g. from sync on a new device that delivered
`.obsidian/` before `journals/`). A value of the wrong type in Obsidian's files (e.g. a number
instead of a path) is skipped with a warning, and Brulion keeps its own.

**What to set once** (*Settings → Notebook*):

* **Journal mode** — days are outlines by default (bullets, as in Logseq); if you write paragraphs
  in your daily notes, choose *Document*.

**What is different:**

* **Tags** are links to pages in Brulion (as in Logseq): `#project` leads to the page "project" —
  see [Tags and pages](#tags-and-pages) below. Nothing changes in the files because of it.
* **A block reference** `((id))` (*Copy block reference*, `Ctrl+Shift+C`) is Logseq syntax:
  Obsidian shows it as plain text. In a vault you open in both apps, use *Copy link to block* —
  `[[Page#^id]]` works in both.
* Reminders `⏰ …`, `calc` and `query` code blocks and formulas in tables are Brulion syntax —
  Obsidian shows them as text or a code block. The other way round: ```` ```dataview ```` blocks
  show their results in Brulion too — `LIST`, `TABLE` and `TASK` queries with `FROM`, `WHERE`,
  `SORT`, `GROUP BY`, `FLATTEN`, `LIMIT` and Dataview's fields and functions, read-only (tasks can
  be checked off; docs/SYNTAX.md §10 "Dataview queries"); `dataviewjs` blocks, inline `= …` queries,
  regular-expression functions and `CALENDAR` stay code or say they are not supported. Files of
  other plugins are plain text or a page (exceptions: Excalidraw drawings and canvases, below, and
  bases).
* **Bases** (Obsidian's core plugin): a `.base` file opens in Brulion as a page of its views — a
  table, cards (a gallery), a list or a Kanban board — with its filters, formulas, display names,
  sort, groups, limits and summaries; a note embedding it (`![[Books.base]]`, `![[Books.base#View]]`)
  and a ```` ```base ```` block show its views in the note (docs/SYNTAX.md §10 "Obsidian Bases"). Editing a cell writes the note's property as Obsidian
  would; dragging a card on a board writes the property it groups by; sorting by a column and
  hiding, adding or moving columns write the view's `sort:` / `order:` into the YAML — those lines
  only, the rest of the file (comments, other views, a plugin's keys) as it was. A map view and a
  plugin's view type are shown as a table with a note; regular expressions and plugins' functions
  say they are not supported instead of failing. Brulion's own ```` ```query ```` blocks have
  views too (`view:table`, `view:board`, …; docs/SYNTAX.md §10 "Views"), which Obsidian shows as
  code.
* Brulion **drawings** (`/Drawing`) are SVG files with an embedded Excalidraw scene: Obsidian
  shows them as pictures, and excalidraw.com opens them for editing. A drawing from Excalidraw
  saved as SVG with the *Embed scene* option opens in Brulion's drawing editor — Brulion edits
  handwriting, shapes, arrows, text and pictures, and only shows a drawing with frames or embedded
  web pages.
* **Drawings of the Excalidraw plugin for Obsidian**
  (`Drawing 2026-10-28 14.32.05.excalidraw.md` — any `.md` file with the `excalidraw-plugin` key in
  its frontmatter) are shown by Brulion as drawings: in notes (`![[Drawing.excalidraw]]`,
  `![[Drawing.excalidraw.md]]`, `![[Drawing]]`), when opened from the page list (a picture view
  with "Edit drawing") and in the drawing editor. Brulion saves them in the same format (the scene
  in the `## Drawing` block: `compressed-json` — LZ-String — or `json`), changing only that block
  and the entries of texts changed or deleted in Brulion; the frontmatter, the notes "on the back"
  of the drawing, element links and embedded files stay byte for byte — Obsidian still opens the
  drawing. `.excalidraw` files can be edited too. Limitations: pictures from the "Embedded Files"
  section (pasted in Obsidian) appear as grey rectangles; a link in text changed in Brulion becomes
  plain text; a drawing with frames or embedded web pages can only be viewed (its preview is the
  plugin's export next to it, if there is one); Brulion creates new drawings as SVG
  (docs/SYNTAX.md §2 "Drawings").
* **Canvases** (`*.canvas`, Obsidian Canvas — the open JSON Canvas format) open in Brulion's canvas
  view and are edited there (docs/UI.md "Canvas view"): every node type of the format — text
  (rendered as Markdown), notes and their sections (`subpath`), pictures, PDFs, drawings, other
  canvases, any file, web addresses (shown as an address, never loaded), groups with labels and
  background pictures —, edges with sides, arrowheads, colours (the six presets and hex colours)
  and labels. Brulion writes only what you change: a card it did not touch is written back byte
  for byte, a changed one keeps its keys in their order, and keys it does not know — Advanced
  Canvas' `styleAttributes`, `zIndex`, the file's `metadata`, a plugin's own node types — stay as
  they are, so Obsidian and its plugins keep their settings (docs/SYNTAX.md §2 "Canvases"). A
  canvas's notes and the links in its text cards count as links in Brulion too (backlinks, the
  graph), its cards are found by search, and renaming or moving a page rewrites the canvases that
  show it. Not shown: Obsidian's embedded web pages (a link card shows its address), the
  Advanced Canvas plugin's portals (kept in the file); its shapes, borders and text alignment are
  drawn in the canvas's picture, a pill, a circle and an invisible border on the board too. In notes, a canvas is
  linked as `[[Plan.canvas]]`, as in Obsidian.
* Brulion keeps **favorites** (Obsidian's bookmarks from `.obsidian/bookmarks.json` are not read)
  and **folded blocks** on the device, not in the files.

**What Brulion adds to the folder:** `.brulion/config.json` (only after the notebook settings are
changed or after *Save to notebook*), `.trash/` for deleted files (the same folder Obsidian uses
with the *Move to Obsidian trash* option; both apps skip it), new journal days (only after the
first entry — just looking at a day creates nothing), pasted pictures in the attachments folder,
the `brulion: document`/`outline` key in the YAML header of a note whose mode you change by hand
(Obsidian shows it as an ordinary property), and the ` ^id` identifier at the end of a block whose
link you copy — in Obsidian's format. On request (*Create .stignore*, see below) also a
`.stignore` file for Syncthing. Version history, the index and the sync state lie outside the
folder, so Syncthing does not transfer them.

**Risk of conflicts:** do not edit the same note at the same moment in both apps or on two
devices at once. Brulion picks up changes from disk as they happen (also in an open note, merging
them with what you are typing), and earlier versions stay in *Version history*. When Syncthing
still leaves a `*.sync-conflict-*` copy, merge it in the sync center. Conflicts in `.obsidian/`
(e.g. `workspace.json`) concern Obsidian only; the recommended `.stignore` file prevents them:
*Sync center → Syncthing → Create .stignore* adds rules that skip Obsidian's window layout
(`.obsidian/workspace*.json`, each device has its own) and the `.trash` folder. Syncthing does not
transfer `.stignore` itself — copy it to every device.

## Notes in a GitHub repository

Markdown notes kept in a GitHub repository — an Obsidian vault pushed there, a project's
documentation folder — need no import either: **Open from GitHub…** on the start screen opens the
repository, or one of its folders, as a notebook that stays linked to it. Brulion downloads the
notes, gets new versions by itself and sends your changes back as commits or as a pull request
(docs/UI.md "Notebooks from GitHub"). The same works in the browser. An existing notebook can be
linked to a repository too (Sync center → GitHub).

## Tags and pages

In Brulion a tag names a page, as in Logseq: `#work` leads to the page "work" (details:
[SYNTAX.md](SYNTAX.md) §3). Tag pages from Logseq (e.g. `pages/work.md`) therefore join their tags
after the import, and nested tags `#project/home` join the namespace pages that the import writes
into folders (`project/home.md`). In Obsidian tags and pages are separate: a tag `#project` next to
a page "Project" starts leading to it after the import, and the page's backlinks show the blocks
with that tag. On export: Obsidian and Markdown get the tags unchanged (Obsidian keeps them
separate as before), Logseq gets `#[[Projects/Home]]` when the page has a different name there
than the tag, and in the HTML export a tag leads to the page it names.

## Microsoft To Do

Brulion moves task lists from Microsoft To Do: each list becomes a page with tasks in the import
folder (`Import/Microsoft To Do/Tasks.md`, `…/Groceries.md`). There are two ways:

* **Sign in to Microsoft** (recommended) — Brulion downloads all lists itself, together with
  steps, links and files. It needs a one-time, free registration of your own app in Microsoft
  Entra (described below).
* **A file from Graph Explorer** — no registration, but without steps, links and files.

This is a one-time move, not a sync: changes made later in To Do do not reach Brulion, and
nothing changes in To Do. Importing again creates pages with the suffix " 2".

### What becomes what

| In To Do | In Brulion |
|---|---|
| list | page `<import folder>/<list name>.md` with `source: Microsoft To Do` in the frontmatter; a list without tasks — nothing (a warning says how many there were) |
| task | `- [ ] Title`; characters that mean something in Brulion (`#`, `[[`, `*`, `$`) get a `\` |
| in progress / waiting on others, deferred | `- [/]` / `- [>]` |
| completed | `- [x] … ✅ 2026-10-25` (the completion day, as To Do recorded it) — below the uncompleted ones, most recently completed first |
| important / low importance | `⏫` / `🔽` |
| due date, start date, creation date | `📅 2026-10-27`, `🛫 2026-10-20`, `➕ 2026-10-14` |
| reminder | `⏰ 2026-10-27 08:00` (only in uncompleted tasks) |
| repeat | `🔁 every day`, `every 2 weeks on Monday, Friday`, `every weekday` (working days), `every month on the 15th`, `every year`. "Second Tuesday of the month" and the like — no 🔁, only the due date stays (a warning); the end of a series (a date or a number of repeats) is skipped (a warning) |
| categories | tags: "Home office" → `#Home-office` |
| note | lines below the task (a note in HTML — converted to Markdown) |
| steps | sub-items `- [ ]` / `- [x]`; unchecked steps in a completed task — `- [-]` (cancelled), so that they do not hang around as open tasks |
| links (e.g. to a message in Outlook) | sub-items `[name](address)` |
| files | attachments in `assets/Microsoft To Do/`, embedded below the task; files larger than 25 MB are skipped (a warning) |

Microsoft gives dates and times in UTC — Brulion converts them to the device's time zone, so a
due date set for 27 October stays 27 October. Not carried over: "My Day", the manual order of
tasks (tasks come in the order Microsoft gives them), list groups, the people a list is shared
with, and task assignments.

### Signing in to Microsoft

Microsoft lets only an app registered in Microsoft Entra access tasks. Brulion has no server of
its own and no shared registration — you register "your" app once, for free, and enter its ID.
Registration needs a Microsoft Entra directory: a Microsoft 365 work or school account already
has one (an administrator can block registrations, though), and for a personal account
(outlook.com, hotmail.com, live.com) the directory "Default Directory" is created when you set up
a free Azure account. Since 2024 Microsoft does not allow registering apps with a personal account
alone, without a directory.

**Registering the app (once):**

1. Open [entra.microsoft.com](https://entra.microsoft.com) and sign in.
2. Choose **Applications → App registrations → New registration**.
3. Name: e.g. "Brulion". **Supported account types**:
   * tasks on a personal account (outlook.com, hotmail.com): **Personal Microsoft accounts only**;
   * tasks on a work account: **Accounts in this organizational directory only** or **Accounts in
     any organizational directory**;
   * both kinds: **Accounts in any organizational directory and personal Microsoft accounts**.
4. Leave the **Redirect URI** empty and click **Register**.
5. **Authentication** → **Advanced settings** → **Allow public client flows**: **Yes**, then
   **Save**. Without this, signing in ends with the error AADSTS7000218.
6. **API permissions** → **Add a permission** → **Microsoft Graph** → **Delegated permissions** →
   tick `Tasks.Read` → **Add permissions**. `User.Read`, added by default, can stay.
7. On the **Overview** page copy the **Application (client) ID**, e.g.
   `3f2504e0-4f89-41d3-9a0c-0305e82c3301`.

No client secret or certificate is needed — Brulion signs in as a public app, with a device
code.

**Import:**

1. **Import notes → Microsoft To Do**, paste the ID into the **Application (client) ID** field.
   Brulion remembers it on this device (one registration is enough for all notebooks).
2. Under **More** there is the **Authority**: `consumers` (the default) for an app for personal
   accounts only, `organizations` or the directory's ID or domain (`contoso.onmicrosoft.com`) for
   work accounts, `common` when the app accepts both kinds of accounts.
3. Click **Sign in to Microsoft**. Brulion shows a code (e.g. `F7GH2K9QX`). Open
   [microsoft.com/devicelogin](https://microsoft.com/devicelogin) — on the same or another device,
   e.g. on your phone — enter the code, sign in with the account that has the tasks, and agree to
   reading the tasks. The code is valid for about 15 minutes.
4. Brulion downloads the lists (a bar shows the progress), and then the usual import preview.
   Nothing is written before you confirm. Choosing a file or entering a path in the meantime
   cancels the sign-in — what you pointed to is imported instead.

Security: Brulion asks only to read tasks (`Tasks.Read`) and connects only to
`login.microsoftonline.com` and `graph.microsoft.com`. The access token is in memory only while
downloading — it does not reach the notebook, the settings or the event log, and Brulion does not
ask for a refresh token, so every import is a new sign-in. Only the app ID and the authority are
remembered. You withdraw consent in your Microsoft account (personal account:
[account.live.com/consent/Manage](https://account.live.com/consent/Manage)) or by deleting the app
registration.

| Error | What to do |
|---|---|
| AADSTS7000218 | in the app registration turn on **Allow public client flows** (step 5) |
| AADSTS700016 | check the ID; an app for personal accounts only needs the authority `consumers` |
| AADSTS50059, AADSTS9002331, AADSTS50020 | the account kind does not match the registration or the authority: personal account — `consumers` and a registration for personal accounts; work account — `organizations` or the company's directory |
| AADSTS65001 | no consent to `Tasks.Read`: add the permission (step 6); in a company an administrator may have to give consent |
| "Brulion may not read your tasks." | the registration lacks the `Tasks.Read` permission — add it and sign in again |
| "Microsoft To Do is not available for this account" | the account has no Exchange Online mailbox, where To Do keeps the tasks |

### Without registration: a file from Graph Explorer

[Graph Explorer](https://developer.microsoft.com/graph/graph-explorer) is Microsoft's page for
sending queries to Microsoft Graph — it needs no app registration of your own.

1. Open Graph Explorer and sign in (**Sign in**) with the account that has the tasks.
2. **Modify permissions** → next to `Tasks.Read` click **Consent** and agree.
3. Enter the `GET` query `https://graph.microsoft.com/v1.0/me/todo/lists?$expand=tasks` and click
   **Run query**.
4. Copy the whole response (**Response preview**) into a text file with the `.json` extension
   (e.g. `To Do.json`, UTF-8 encoding).
5. In Brulion: **Import notes → Microsoft To Do → Choose files…** and pick that file.

Such a file has no steps, links or files (Brulion says that the attachments stayed in To Do). For
large lists Microsoft may return only part of the tasks — Brulion warns that the export is
incomplete. Then download each list's tasks separately: find the list's `id` in the response, run
`GET https://graph.microsoft.com/v1.0/me/todo/lists/<list id>/tasks`, save the response as
`<list name>.json` (the page gets the file's name) and import all files at once — you can also
pack them into a ZIP. Such a response can be split into parts too; if Brulion warns that the file
has only the first tasks of the list, **Sign in to Microsoft** downloads all of them.

In the browser version signing in to Microsoft does not work (Microsoft does not let web pages
sign in with a device code) — use a file from Graph Explorer there.

## CSV and Airtable

A spreadsheet saved as CSV (Excel, LibreOffice, Google Sheets, Numbers, an export of another app)
becomes **a page per row** with the columns as frontmatter properties, plus an Obsidian base next to
the pages that shows them as a table — or, chosen in the mapping, **one page with the whole
table**. After Obsidian Importer's CSV import, with Brulion's own reading and type detection.

### Reading the file

| What | How |
|---|---|
| encoding | a BOM decides (UTF-8, UTF-16 LE/BE); UTF-16 without a BOM by its zero bytes; valid UTF-8; else 8-bit text: ISO-8859-2 when its Polish letters (`ą ś ź` = 0xB1 0xB6 0xBC) outnumber Windows-1250's and no byte 0x80–0x9F occurs, Windows-1250 when the text looks Polish (old Excel on Polish Windows), else Windows-1252. Can be chosen. |
| separator | `,` `;` tab `|` — the one that splits the first 100 records into the same number of fields most often; Excel's first line `sep=;` decides; `.tsv` is tab. Can be chosen. |
| header row | detected as Python's `csv.Sniffer` does: a column of numbers, dates or booleans under a text name votes for a header; repeated or mostly empty names in the first row mean data (columns are then "Column 1", "Column 2" …). Can be chosen. |
| quoting | RFC 4180: `"…"` fields with separators, `""` and line breaks; CRLF / LF / CR; a stray quote stays text; ragged rows are padded; trailing empty rows (`;;;`) dropped |
| bounds | 100 000 rows, 200 columns, 100 000 characters a cell — more is left out with a warning; repeated column names get a number |

### What becomes what

| Column type (detected or chosen) | In the page |
|---|---|
| title | the page's name (`Pan Tadeusz.md`); characters file names cannot hold are dropped and the real title goes to `aliases`; a title that repeats gets " 2"; a row without a title is named after the file and its row ("Książki 7"). Detected: a column named Name, Title, Nazwa, Tytuł, Temat, Imię i nazwisko …, else the first column with distinct short values. A **title template** `{{Autor}} – {{Tytuł}}` (`{{row}}` the row's number, Obsidian Importer's `{{source["Column"]}}` too) overrides it. |
| page text | the page's body (a long "Notes", "Opis", "Description", "Treść" … column is detected; HTML is converted to Markdown) |
| subfolders | a **folder template** `{{Kategoria}}/{{Podkategoria}}` |
| text | `Autor: Adam Mickiewicz` (several lines: a YAML block) |
| number | `Cena: 29.99` — also `29,99`, `1 234,5`, `1.234,56`, `1,234,567.25`; a leading zero (`007`, `00-950`) or `+48 …` stays text |
| date | `Data zakupu: 2026-04-15` — from `2026-04-15`, `15.04.2026`, `15.04.26`, `04/15/2026` (day or month first: decided per column by the values over 12; `/` alone means month first), `15 kwietnia 2026`, `Apr 15, 2026` |
| date and time | `2026-11-20T14:30` (wall-clock; `…Z` and offsets are converted to the device's zone), also `11/20/2026 2:30pm` |
| checkbox | `true` / `false` from tak/nie, yes/no, true/false, prawda/fałsz, checked (Airtable), ✓ ✗; an empty cell of a checkbox column is `false` |
| list | `Gatunki: [powieść, klasyka]` — items separated by `,` `;` `|` or line breaks (detected per column; detected as a list when short items repeat across rows) |
| tags | added to `tags` (a column named Tags, Tagi, Labels, Etykiety, Kategorie … is detected) |
| links | `Osoby: ["[[Jan Kowalski]]", "[[Anna Nowak]]"]` — detected when most of a column's items are titles of rows of the import (Airtable's linked records, a "Related" column); links to renamed pages follow |
| attachments | `photo.jpg (https://…)` cells (Airtable): links to the files in the notebook when they were downloaded (**Download the attachments**; pictures and PDFs also embedded below the text), else their addresses |
| left out | nothing |

Empty cells are left out (but a checkbox's). Property names keep the header's text, without the
characters YAML keys would need quotes for; `tags`, `aliases`, `source` and `brulion` (Brulion's own
keys) get " (CSV)". The base (`Książki.base` next to the pages: `file.inFolder("…")` and `file.ext ==
"md"`, a table view of `file.name` and the properties in column order) can be turned off; pages at
the notebook's top get none (it would show every note). Several CSV files of one import: each
becomes a folder of its own (with its base), and the mapping applies to columns of the same name in
all of them. The table page (**One page with the table**) holds the first 5000 rows: cells escaped
(`|`, a leading `=` stays text), line breaks as `<br>`.

### Airtable

Choose **Airtable** or let Brulion recognise its CSV (a file named "<table>-Grid view.csv", or
attachment cells on `*.airtableusercontent.com` / `dl.airtable.com`): "checked" checkboxes, the
table's name without "-Grid view", attachments, linked records as links (when the linked table's
CSV is imported too, to its rows' pages). The attachment links of a CSV expire after a few hours:
download them right after exporting.

**With a personal access token** (the API): create one at airtable.com/create/tokens with the scopes
`data.records:read` and `schema.bases:read` and access to the bases, paste it, choose the base (the
only one is chosen by itself) and its tables. Brulion reads the schema and every record (100 a
request, at most 5 requests a second), and the fields' types decide the properties' types:
number, currency, percent, rating, duration → number; checkbox; date; date and time, created and
modified times → date-time; multiple select, collaborators, lookups → list; linked records → links
to the linked records' pages (by their primary field); attachments → downloaded into the notebook;
buttons left out; formulas and rollups by their values. The token goes only to api.airtable.com and
lives in memory until the download ends; the attachment files come without it.

## Todoist

Every project becomes a page of tasks (Obsidian Tasks syntax, docs/SYNTAX.md §5):
`Import/Todoist/<project>.md`, sub-projects `<project>/<sub-project>.md`.

| In Todoist | In Brulion |
|---|---|
| section | a heading `## Section` |
| task, subtask | `- [ ] …`, indented under its parent |
| completed task | `- [x] … ✅ 2026-11-19` |
| priority p1 / p2 / p3 / p4 | `🔺` / `⏫` / `🔼` / none |
| due date (and time) | `📅 2026-11-24 09:00` (fixed times in UTC converted to the device's zone) |
| due date and deadline | `⏳ due` and `📅 deadline` |
| recurring due date | `🔁 every week on Saturday` where Brulion's rules can say it ("every monday", "every 2 weeks", "every weekday", "co tydzień"; `every!` / "after" → `when done`; "at 9am" stays the due time); else only the next date (a warning) |
| reminder (Pro) | `⏰ 2026-11-24 08:30` (relative ones from the due time) |
| labels, `@label` in a template | tags `#label` |
| description | lines under the task |
| comments | items under the task, their files as attachments (downloaded with the token; a file stored elsewhere stays a link) |
| project comments, the template's notes before the first task | the page's first lines |
| duration, assignee | left out (a warning) |

`#word` in a task's text gets `\#` (it would be a tag), `[[` likewise; Todoist's own Markdown (links,
bold) stays.

Three ways in:

* **API token** (Todoist: Settings → Integrations → Developer): Brulion asks the Sync API
  (`POST https://api.todoist.com/api/v1/sync`, every active project, section, task, label, comment
  and reminder) and downloads the comments' files. Completed tasks are not part of it. The token
  lives in memory until the download ends.
* **A project's template** (••• → Export as a template → CSV): `TYPE,CONTENT,DESCRIPTION,PRIORITY,
  INDENT,…,DATE,DATE_LANG,…,DEADLINE`. Dates are Todoist's words in DATE_LANG ("every monday",
  "jutro o 9", "Nov 25"): Brulion reads them relative to the import's day and time.
* **A backup** (Settings → Backups): a ZIP of such CSV files, one per project (the project's id in
  the file name is dropped).

## OneNote

OneNote has no export file Brulion could read (its `.one` files are a binary format of its own),
so Brulion downloads the notebooks from Microsoft Graph, like Microsoft To Do: **Sign in to
Microsoft** with your own app registration (the steps under [Microsoft To Do](#signing-in-to-microsoft);
one registration serves both — add the delegated permission **`Notes.Read`** to it), then choose
the notebooks.

Brulion lists `/me/onenote/notebooks`, then per notebook its sections and section groups (nested
ones too, eight levels), each section's pages (`pagelevel=true`: their order and level), each page's
content (Graph's HTML) and the resources it shows — pictures in full resolution
(`data-fullres-src`), attached files (`<object data-attachment>`). Requests go one after another,
only to graph.microsoft.com; `Retry-After` is honoured (OneNote allows apps only a few requests a
minute: a large notebook takes a while); files over 50 MB or more than 300 MB together are left out
with a warning.

| In OneNote | In Brulion |
|---|---|
| notebook, section group, section | folders `Import/OneNote/<notebook>/<group>/<section>/` |
| page | a page with `created`, `updated`, `source: OneNote`; an untitled page "Untitled page" |
| subpage (page level 1, 2) | a folder named after the page above it: `Plan.md` and `Plan/Details.md` |
| to-do tag (checked) | `- [ ]` / `- [x]` |
| other tags (Important, Question …) | `#important`, `#question` … |
| picture | an attachment (full resolution), embedded; OneNote's OCR text of it dropped |
| attached file | an attachment, linked |
| link to another page | `[[path\|text]]` (by the page's id, else its title); to a page outside the import: the text (a warning) |
| embedded video | a link |
| text, lists, tables, headings | Markdown (the HTML converter) |

Not offered in the browser (Microsoft refuses the device-code sign-in from web pages). The access
token lives in memory between listing the notebooks and downloading them, never on disk.

## Apple Notes

Apple Notes stores its notes on a Mac in a Core Data database:
`~/Library/Group Containers/group.com.apple.notes/NoteStore.sqlite` (with `NoteStore.sqlite-wal`,
the changes not yet written into it), the attachments under `Accounts/<account>/` (`Media`,
`FallbackImages` — drawings —, `FallbackPDFs` — scans —, `Previews`). Brulion reads that folder
without a Mac: copy it (or a ZIP of it) from the Mac and choose it. Notes that are only in iCloud
must have been opened on the Mac; a ZIP keeps the folder's names, so it is the easiest way to a
phone.

**How it is read.** Obsidian Importer reads the database with SQLite and the notes' protocol
buffers on a Mac. Brulion does it on any device with its own small readers in the io module (no
SQLite library, no protobuf library): a read-only reader of the SQLite file format (the schema, table
B-trees with their interior, leaf and overflow pages, records, and the write-ahead log: its committed
frames — salts and checksums checked — replace pages of the file) and a schema-less protocol buffers
decoder; the note data is gzip (miniz). Both are bounded for hostile input: page numbers, offsets,
lengths, the tree's depth (40), cycles, a record (64 MB) and an unpacked note (64 MB); a damaged
file gives the notes that could be read and a warning. Exports of other apps (Exporter, HTML or PDF
from Notes) can be imported as HTML or Markdown files as before.

| In Apple Notes | In Brulion |
|---|---|
| account, folder, subfolder | folders (the default "Notes" folder is the import folder itself; several accounts: a folder each) |
| note | a page named after its first line (which is left out of the text), with `created`, `updated`, `pinned`, `source: Apple Notes` |
| title, heading, subheading | `#`, `##`, `###` |
| bold, italic, strikethrough, highlight (any colour) | `**…**`, `*…*`, `~~…~~`, `==…==` |
| underline, superscript, colours, alignment | the text alone |
| bulleted, dashed, numbered list, levels | `- `, `1. `, tab-indented |
| checklist | `- [ ]` / `- [x]` |
| monospaced | a code block |
| block quote | `> ` |
| link | `[text](url)`; a link to another note `[[path\|text]]` |
| #tag, @mention | `#tag`, the name |
| table | a Markdown table (from the table's CRDT: rows and columns in their order) |
| picture, file | an attachment, embedded / linked |
| drawing | its picture (`FallbackImage.png`) |
| scanned document | its PDF, else the pages' images |
| web link card | `[**title**](url)` |
| locked note | left out (a warning: unlock it on the Mac) |
| Recently Deleted | left out (a warning) |
| attachment not in the folder | left out (a warning) |

## Trilium Notes

Works with ZIP exports from Trilium 0.10x (the TriliumNext project, again called just Trilium),
TriliumNext 0.9x and recent versions of zadam/trilium (0.6x).

### How to export the notes

1. In the note tree, right-click the note you want to move together with everything below it. All
   notes at once: the *root* note at the very top of the tree.
2. Choose **Export**, then **This note & all descendants** and the format **HTML** (recommended —
   keeps all formatting) or **Markdown**.
3. In Brulion choose the downloaded ZIP file — there is no need to unpack it.

Tips:

* **Protected notes** (encrypted) are included in the export only when you open a protected
  session (enter the password) before exporting. Otherwise the ZIP holds only their place in the
  tree — Brulion skips them and says how many there were.
* Import several exports **one after another**, each separately (chosen together they would unpack
  into the same place). Brulion warns when the selection contains files from outside the export.
* The format **"HTML for web publishing"** is a ready-made website, not notes — Brulion does not
  import it. **OPML** is not supported either. Exporting a single note (without descendants)
  gives an ordinary `.html` or `.md` file — import it as HTML or Markdown.
* The HTML export is more accurate than Markdown: Trilium's Markdown export loses underlining, and
  in older versions also task states, collapsible blocks and included notes.
* Notes larger than 4 MB or with more than 5000 list items are saved by Brulion as files in the
  attachments (with a link on the page and a warning) — converting them would take too long.
* Importing the same export again overwrites nothing: pages get the suffix " 2" (and the content
  of daily notes is appended a second time), links lead to the pages from the new import.

### What becomes what

| In Trilium | In Brulion |
|---|---|
| text note | page `<import folder>/<path in the tree>/<title>.md`; characters a file name cannot have (`/ : ? "` …) are dropped from the name, and the real title goes to `aliases`. Names reserved in Windows (`CON`, `NUL` …) get a `_` (the notebook may sync with Windows), names of folders the notebook skips (`logseq`, `node_modules`) — the suffix " 2" |
| note with child notes | a page and a folder with the same name (`Projects.md` and `Projects/`); without content of its own — just the folder. Below the page's content, after a `---` line, there is a list of child notes — like the list of notes Trilium shows below a note (pictures embedded, files as links; with the label `#hideChildrenOverview` only clones, pictures and files) |
| branch prefix (e.g. "Part I") | part of the name: `Part I - Chapter 1.md`, the title alone in `aliases` |
| notes with the same title in one place | "Tasks" and "Tasks 2" (case does not count) |
| order in the tree | Brulion sorts pages in a folder alphabetically; Trilium's order stays in the lists of child notes |
| collection (*book*) | a page with a list of links to the child notes, in Trilium's order (a collection of pictures — the pictures); an empty collection without child notes — nothing |
| note without content but with labels, links to it or clones | a page with the list of child notes |
| day note (`#dateNote`) | a daily note in the journal (`journals/2026-10-16.md`, folder and format from the notebook settings; appended when that day already exists). A suffix of your own in the title ("24 - Thursday - Christmas Eve!") — a heading `# Christmas Eve!` at the start of the day. The day's child notes are pages in the calendar folder and are linked from the day. A page from the import never takes the path of a daily note |
| calendar, year, quarter, month, week | folders only (a page — when they have content of their own) |
| clone (the same note in several places) | imported once, in the first place in the tree; in the other places — a link in the parent's list of child notes |
| code note | a page with a code block in its language (```` ```python ````); a note of type Markdown — its content |
| Mermaid diagram | a ```` ```mermaid ```` block with the source (Brulion draws it itself: docs/SYNTAX.md §11 "Mermaid diagrams") and a link to the SVG picture rendered by Trilium (such an SVG has HTML labels and CSS styles that Brulion cannot draw); a picture that can be shown is embedded above the source |
| canvas (Excalidraw) | a page with a drawing (see **drawings** above): one SVG file in the attachments (`assets/<import folder>/<path>/<title>.svg`) — the picture drawn by Trilium with the Excalidraw scene inside. Brulion opens it in the drawing editor (handwriting, shapes, arrows, text and pictures; a drawing with a frame or an embedded web page is only shown), excalidraw.com does too, and other programs show Trilium's picture. Pictures pasted into the drawing in Trilium are inside it (when together they are too large — photos fit up to about 15 MB — they stay next to it as files, with a warning; a picture that another note also shows stays a separate file as well). A drawing that Brulion only shows has the scene exactly as in Trilium (with the order of elements). Without Trilium's picture (older versions, an empty drawing, a PNG alone) Brulion draws the picture itself. A note whose content is not an Excalidraw scene (damaged), and a drawing with an embedded web page without Trilium's picture — as before: a picture and an `.excalidraw` file |
| mind map | a nested list, a link to the picture (if there is one) and a `.json` file |
| relation map | a list of links to the notes on the map |
| web view | a link to its address |
| picture, file | an attachment in `assets/<import folder>/<path>/`; notes that show it embed it |
| a note's attachments | `assets/<import folder>/<note path>/…`, embedded where they were in the content; the others — at the end of the page |
| saved search, note map, render note, launcher | skipped — a warning lists their titles |
| very deep tree, very long titles | the deepest notes go to an ancestor's folder (a file path has a length limit) — a warning |

Attributes:

| In Trilium | In Brulion |
|---|---|
| label without a value `#project` | tag (`tags: [project]`) |
| label with a value `#status=in progress` | property `status: in progress` (several labels with the same name — a list) |
| relation `~author` → note | property with a link: `author: "[[Import/Trilium/People/Jan Kowalski\|Jan Kowalski]]"` |
| `#pageUrl` (pages saved from the browser) | property `source` |
| template (`#template`) with attribute definitions (`#label:phone=…`) | a page with empty properties (`phone: ""`) — move it to the templates folder if you want to use it |
| Trilium's own labels and relations (`#iconClass`, `#cssClass`, `#archived`, `#sorted`, `#viewType`, `#bookZoomLevel`, `#calendarRoot`, `~template`, scripts, sharing…) | skipped |
| inherited attribute | only on the note that has it (Brulion does not inherit attributes) |

Note content:

| In Trilium | In Brulion |
|---|---|
| to-do list and its states | `- [ ]`, `- [x]`, `- [/]` (in progress), `- [-]` (cancelled); "maybe" and custom states — `- [ ]`; empty tasks (an empty checkbox from the day template) are skipped |
| list item with several blocks (code, picture, quote, several paragraphs) | the blocks stay in the item, indented below its marker |
| two lists or two quotes right next to each other | separate (the second list with the marker `*` or `1)`, so that Markdown does not join them) |
| admonition (note, tip, important, caution, warning) | callout `> [!tip]`, `> [!warning]` … |
| collapsible block | a folded callout `> [!note]- Title` (unfolded: `+`) |
| footnotes (also in notes imported into Trilium from Markdown) | `[^1]` in the text, the footnotes' content at the end of the page |
| inline / block formula | `$E=mc^2$` / a ```` ```math ```` block — Brulion typesets formulas, the LaTeX source stays in the file; a `$` from the surrounding text that would join the formula (e.g. a price in the same paragraph) gets `\$`; when a letter or digit right after a formula would change what Brulion takes as a formula, the formulas of that line stay code `` `$E=mc^2$` `` |
| `$` in text (prices, `$x$` as plain text) | unchanged, and where two `$` would form a formula ("Price $10, Netflix 15$"), the first one gets `\$` — it shows as `$` |
| reference link to a note | `[[Import/Trilium/Projects/Page\|Page]]` (shows the title from Trilium) |
| link with its own text | `[[Import/Trilium/Projects/Page\|text]]`; a link to a day note — `[[2026-10-16]]` |
| link to a note outside the export, excluded or protected | the link's text alone (a warning) |
| included note (*include note*) | `![[Import/Trilium/Projects/Page]]` |
| `#word` in text (in Trilium it is plain text, not a label) | `\#word` — it does not become a tag (in a formula `#` stays unchanged: it belongs to the formula) |
| link preview | an ordinary link `[title](address)` |
| picture in the content | `![[assets/…/picture.png]]`; pictures in `data:` — saved as attachments |
| headings, bold, italic, strikethrough, highlight, code, quotes, tables, lines | Brulion Markdown (`==highlight==`, GFM tables, ```` ```language ````) |
| underline, text colour, superscript and subscript, inline icons, page break | the text alone (icons and breaks are dropped) |

Links point to pages by their path in the notebook, with the title as the link text: in Trilium a
link leads to one particular note, and the title alone could point to another page with the same
title (e.g. from an earlier import or your own note "Tasks"). When you rename a page or folder in
Brulion, the links to them are fixed.

Security: Trilium notes may contain scripts (JS code notes, `#run`, `~renderNote`) — Brulion runs
nothing, code goes onto pages as text. HTML from notes passes through a converter that removes
scripts, frames and dangerous links.
