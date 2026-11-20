---
tags: [documentation]
brulion: document
---
# Import and export

You open the **Import and export** view from the **More** menu → **Notebook** or from the command palette. Import is also on the start page of an empty notebook (“Import notes”).

## Import
Supported sources: Obsidian, Logseq, Notion, Evernote, Google Keep, OneNote, Apple Notes, Microsoft To Do, Todoist, Joplin, Simplenote, Roam, Trilium, Bear, Airtable, Markdown, TXT and HTML files, spreadsheets (CSV) and iCalendar calendars. You can pick files, a ZIP archive or a whole folder.

1. Choose the source and the files.
2. Look at the **preview**: what will be created, what will be skipped and why.
3. Confirm. Nothing is saved before this step.

You pick files and folders in the system's file picker — on Android also from Google Drive or an SD card. In the browser you can pick several files at once too (you can't pick a folder there — zip it); a file larger than 512 MB is skipped with a warning in the preview, and when the chosen files together exceed 1.5 GB, Brulion reads only those that fit and tells you that too — just like on a computer.

Imported pages don't overwrite existing ones — when a name repeats, the new page gets the ending ` 2`. Before any changes, the previous versions go into the history.

Text from apps that don't write Markdown (HTML, Evernote, Google Keep, Notion databases, Trilium, calendars) stays text: characters that mean something in Brulion get a backslash. The same goes for `$` — but only where two dollar signs would make a [[Math formulas|formula]] (`Price $10, Netflix 15$` is saved as `Price \$10, Netflix 15$`); single prices, like `$5 and $10`, stay unchanged.

Drawings (*canvas*) from Trilium become Brulion [[Drawings|drawings]]: each is one SVG file with Trilium's picture and an Excalidraw scene inside (pasted pictures included), which you open in the drawing editor and keep changing.

Brulion is compatible with the Obsidian and Logseq formats, so often it is enough to simply open their folder as a notebook. Notes from Obsidian (headings and paragraphs) open as documents, and those from Logseq (bullet points only) as outlines. In Brulion a tag is a [[Tags and properties#A tag is a link to a page|link to a page]], as in Logseq: tag pages from Logseq connect to their tags. In Obsidian tags and pages are separate — a tag `#project` next to a page “Project” will start leading to it.

### Microsoft To Do
Every task list from Microsoft To Do becomes a page with [[Tasks|tasks]]: status, importance (`⏫`, `🔽`), due date `📅`, reminder `⏰`, recurrence `🔁`, categories as tags, the note below the task, steps as sub-items, links and files. Dates are converted to the device's time zone. It is a one-time move, not a sync.

**Sign in to Microsoft** downloads everything by itself, but needs your own free app registration in Microsoft Entra (once):

1. At entra.microsoft.com: **App registrations → New registration**, a name such as “Brulion”, account type: **Personal Microsoft accounts only** (for outlook.com and hotmail.com) or organizational accounts for a work account. Leave the redirect URI empty.
2. **Authentication → Allow public client flows: Yes** — without it, signing in ends with the error AADSTS7000218.
3. **API permissions → Add a permission → Microsoft Graph → Delegated permissions → `Tasks.Read`**.
4. Copy the **Application (client) ID** and paste it into Brulion (**Import notes → Microsoft To Do**). Under **More** there is the authority: `consumers` (the default) for personal accounts, `organizations` for work accounts, `common` for both.

Then **Sign in to Microsoft**: open microsoft.com/devicelogin (on a phone too), type the code shown and sign in. Brulion asks only for reading tasks, and keeps the sign-in token in memory only for the time of the download — it saves nothing.

Without a registration: in Graph Explorer (developer.microsoft.com/graph/graph-explorer) sign in, consent to `Tasks.Read`, run `GET https://graph.microsoft.com/v1.0/me/todo/lists?$expand=tasks`, save the response as a `.json` file and pick it with the **Choose files…** button — without steps and files. In the browser only this way works.

### Spreadsheets (CSV) and Airtable
A spreadsheet saved as CSV — from Excel, LibreOffice, Google Sheets, Numbers or another app — becomes **a page per row**: the cells go into the page's [[Tags and properties|properties]] with their types (numbers, dates, checkboxes, lists, tags, links to the pages of other rows), and an Obsidian base next to the pages shows them all as a table ([[Queries#Obsidian Bases|bases]]). Or choose **One page with the table**.

Before importing, the **Columns and properties** card shows what Brulion found and lets you change it:
- **Page titles from** a column (detected: *Name*, *Title*, *Tytuł*… or the first column with unique values), or a **title template** such as `{{Author}} – {{Title}}`; **Subfolders** such as `{{Category}}`;
- **Page text from** a column (detected: a long *Notes* or *Description* column) — the other columns are properties;
- for every column its property name and type, or **Leave out**;
- under **Encoding, separator, header row…**: Polish exports from old Excel (Windows-1250), `;` separators and files without a header row are recognised by themselves, and can be chosen by hand;
- the first rows with the titles their pages get, and the first page exactly as it will be written.

**Airtable**: download a view as CSV (**••• → Download CSV**) and pick it — attachments (“photo.jpg (https://…)”) can be downloaded into the notebook with **Download the attachments** (Airtable's links expire after a few hours), and linked records become links to the pages of the rows they name. Or connect with a **personal access token** (create it at airtable.com/create/tokens with the scopes `data.records:read` and `schema.bases:read`): choose the base and its tables, and Brulion downloads them with their field types and attachments. The token is kept only in memory until the download ends.

### Todoist
Each project becomes a page with [[Tasks|tasks]]: sections as headings, subtasks indented, priorities (p1 `🔺`, p2 `⏫`, p3 `🔼`), due dates `📅` (with a deadline: the date `⏳` and the deadline `📅`), recurring dates as `🔁` rules where Brulion understands them (“every monday”, “co tydzień”, `every!` counts from the completion), labels as tags, descriptions and comments under the task.
- **With the API token** (Todoist: **Settings → Integrations → Developer**): paste it and Brulion downloads every project with its comments and their files. The token is not saved anywhere.
- **Without it**: export a project as a template (**••• → Export as a template**, CSV) or download a backup (**Settings → Backups**) and pick the files — several projects or a whole backup ZIP at once.

### OneNote
**Sign in to Microsoft** with the same free app registration as for Microsoft To Do (above) — add the delegated permission **`Notes.Read`** to it. After signing in, choose the notebooks; Brulion downloads their sections, section groups and pages with pictures (in full resolution) and attached files. Notebooks, section groups and sections become folders, a page's subpages a folder next to it; to-do tags become tasks, other tags (`#important`…) tags, links between pages wiki links. OneNote lets apps download only a few pages a minute, so a large notebook takes a while. In the browser version OneNote cannot be imported (Microsoft does not allow this sign-in from web pages).

### Apple Notes
Apple Notes keeps its notes in a database on the Mac, which Brulion reads without the Mac:
1. On the Mac, in Finder choose **Go → Go to Folder…** and enter `~/Library/Group Containers/group.com.apple.notes`.
2. Copy that whole folder (or a ZIP of it: right click → **Compress**) to the computer or phone with Brulion.
3. **Import notes → Apple Notes** and choose the folder or the ZIP.

Folders stay folders (several accounts — a folder each), the first line of a note is its title, and the formatting comes along: headings, bold, italic, strikethrough, highlights, lists, checklists, code, quotes, tables, links between notes, `#tags`. Pictures and files, drawings (as pictures) and scanned documents (as PDF or pictures) become attachments. Locked notes and those in *Recently Deleted* are left out (the preview says how many); attachments that are only in iCloud need to be opened on the Mac first.

### From the command line
`brulion-cli import <file, folder or ZIP>` does the same as this screen — with `--dry-run` it only shows what would be written, `--from` chooses the app, and the CSV mapping has its options (`--title-template`, `--columns "Year=text; Price=skip"`…). See [[Command line and AI agents]].

### An Obsidian vault without importing
Simply open an Obsidian folder — also one synced with Syncthing — as a notebook (*Switch notebook → Open another notebook…*) on every device, and leave syncing to Syncthing; don't turn on Nextcloud for it as well. Brulion doesn't show the `.obsidian/` folder and never changes it — it only reads the journal and attachment settings from it.

- The journal's folder, file names (Obsidian's `DD.MM.YYYY` pattern is `dd.MM.yyyy` here) and template, the templates folder and the attachments folder are taken over by Brulion from Obsidian's settings, as long as the notebook has no `.brulion/config.json` file. **Settings → Notebook** shows what it took over; changing any of these settings or the **Save to the notebook** button saves them in `.brulion/config.json` — do that if you also open the notebook in the browser via Nextcloud, which doesn't transfer `.obsidian/`. A pattern with a week number or `Do` (“27th”), or attachments “next to the note”, aren't taken over — the settings show a warning, and then you enter it yourself. A notebook in which Brulion has already saved days in `journals/` (or attachments in `assets/`) keeps using those.
- Link to blocks with **Copy link to block** (`[[Page#^id]]`) — Obsidian shows a `((id))` reference as plain text. `⏰` reminders and `calc` and `query` blocks are Brulion syntax; Tasks plugin tasks work in both apps, and Dataview's `dataview` blocks show their results in Brulion too ([[Queries#Dataview queries from Obsidian|Dataview queries]]).
- Obsidian's canvases (`.canvas`) open in the canvas view and stay readable for Obsidian after editing in Brulion ([[Canvas]]).
- Don't edit the same note in both apps at the same moment; Brulion picks up changes from disk as they happen, and previous versions stay in the history.
- *Sync center → Syncthing → Create .stignore* adds rules that skip Obsidian's window layout (`.obsidian/workspace*.json`) and the `.trash` folder, so these files don't cause conflicts. Copy `.stignore` to every device — Syncthing doesn't transfer it.

### Web pages from the browser (Web Clipper)
The **Brulion Web Clipper** extension for Chrome, Chromium, Brave, Edge and Firefox saves what you read: the **article** of a page (its text without menus and adverts), the **selection**, the **whole page**, a **screenshot** of the visible part or only the **link** — as Markdown, with the pictures saved into the notebook's attachments.

1. Install the extension (from its package: in Chromium browsers *Extensions → Developer mode → Load unpacked*, in Firefox *about:debugging → Load Temporary Add-on*).
2. Connect it to Brulion once: **Settings → Application → Browser extension (Web Clipper) → Connect browsers** (each browser's row shows whether it is connected), or in a terminal `brulion-cli clip install` (`brulion-cli clip status` checks it; on NixOS the module does it).
3. On a page click the extension's button (**Alt+Shift+B**): choose what, the title, **where** — a new page in a folder (`Clippings` by default; optionally also linked from today's journal), today's [[Journal, calendar and templates|journal]], or the end of a heading's section of a page (“## To read”) —, the template, tags, and **Clip**. **Open in Brulion** shows the result.

Right click on a page, a selection, a link or a picture clips it with the options' defaults, and **Alt+Shift+K** clips the selection or the article at once. Templates: Brulion's own (frontmatter with the source, author, dates and tags), in the style of Obsidian's Web Clipper and of Joplin, a short “link and selection” for the journal, a page of your templates folder, or your own text in the extension's options — with the variables of [[Journal, calendar and templates|templates]] and `{{url}}`, `{{site}}`, `{{author}}`, `{{published}}`, `{{content}}`, `{{selection}}`, `{{tags}}`.

There is no server: the browser hands the clip to `brulion-cli` on the same computer, and the only requests go for the clip's pictures, to their own sites — a page from the internet never makes the computer fetch from your home network. Without that connection — on the phone, in the browser version — the extension copies the page **as rich text**: paste it into a note and it becomes Markdown; on Android **Share → Brulion** saves a page's title and address. The developer documentation: docs/CLIPPER.md in the repository.

## Export
- a **ZIP** archive with Markdown, also in the Obsidian or Logseq layout,
- a static **HTML site** or a single HTML file (Mermaid diagrams and charts as pictures, see [[Diagrams#Export|Diagrams]] and [[Tables and charts|Tables and charts]]),
- **PDF** and **Word (DOCX)** — ready for printing and editing, made by Brulion itself, without Pandoc or LaTeX, also on a phone (see [[#A single page|A single page]]),
- **Pandoc Markdown** — one document to turn into a Word, LibreOffice, PDF or EPUB file,
- **JSON** with all the content,
- **iCalendar** — tasks and reminders for a calendar,
- **OPML** — an outline,
- **CSV** — a task list.

You copy a single table from a note or save it as CSV from its menu, and insert a CSV file as a table with the `/Table from CSV…` command — see [[Tables and charts#CSV: spreadsheets|Tables and charts]].

You save the finished file with the **Save…** button in the system's save dialog; in the browser the file is downloaded (Chrome and Edge on a computer first ask where to save it).

**Pictures from the internet** (`![description](https://…)`) in HTML, PDF, DOCX and Pandoc export: choose **Keep addresses** — the file is small, but the pictures show only with network access — or **Include copies**: the pictures go into the export (in a single HTML file — inside the file, in an HTML site and a Pandoc archive — into the `assets/web` folder). Brulion takes the copies from the device's cache and downloads missing ones first; a picture that can't be downloaded stays an address, and the report after the export lists it. Pictures from this device or your home network (e.g. `192.168.…`, `nas.local`) aren't downloaded by the export itself — it includes them if you have displayed them in a note before. The copies take at most 256 MB in total; further pictures stay addresses. This device remembers your choice. Other formats leave the text of your notes unchanged.

In HTML export a tag leads to the page it names, and export to Logseq writes `#[[Projects/Home]]` when the page has a different name there than the tag. Export to Obsidian and Markdown leaves tags unchanged.

### A document for Pandoc
The **Pandoc Markdown (ZIP)** format is an archive with one `.md` file and the attachments its links lead to. Export the whole notebook, a folder or a single page (then the document is that page). Unpack the archive and run [pandoc](https://pandoc.org) in its folder:

```
pandoc "My notebook.md" -o "My notebook.docx"
```

Instead of `.docx` you can give `.odt`, `.epub`, `.html` or `.pdf` (PDF needs LaTeX). Every note is a chapter with its title as the heading. `[[…]]` links, block and heading references lead to places in the document, and embeds are inserted. Formulas (`$…$`, `$$…$$`, a ` ```math ` block) become pandoc formulas, callouts become boxes (`::: callout`), and tasks become checkboxes. Dates and reminders are written in words (“due: 2026-10-20”), because emoji don't work in every format. Text that pandoc would read differently than Brulion (HTML, `@`, `^`, `~`) is preceded by a backslash.

### A single page
You export the open page without coming here: in the page menu (⋮) choose **Export page…** or **Print…** (also from the command palette; on a computer Ctrl+P). The formats for a single page:

- **Markdown** — the note's file exactly as it is,
- **Markdown with attachments** — a ZIP archive with the note and the pictures and files it links to,
- **HTML** — a single web page with the pictures inside,
- **PDF** — pages ready to print or send,
- **Word (DOCX)** — a document for further editing in Word, LibreOffice Writer or Google Docs,
- **Pandoc Markdown** — a document for pandoc (see above).

**Save as file…** opens the system's save dialog (on Android “Save as”, and next to it there is also **Save to Downloads**); in the browser the file is downloaded. You can also copy Markdown and HTML to the clipboard.

PDF and DOCX are made by Brulion itself: headings, lists with task checkboxes, tables, code, formulas, pictures fitted to the page and [[Diagrams|diagrams]] as pictures. Due dates and reminders are written in words (“due: 2026-11-20”), because colored emoji can't be printed. Links to web pages work, and links to other notes are plain text. The PDF has its fonts built in, so accented letters look the same on every device. The paper is A4 — or Letter, if that is what the device's region uses (e.g. the USA, Canada). A very large table (over 1500 rows) goes on paper only with its first rows, and Word accepts at most 63 columns — the report after the export lists this. When you export a whole notebook or folder to PDF or DOCX, every note starts on a new page.

**Print…** on a computer opens the system's print dialog (printer, number of copies, paper size), on Android the system's printing with its printers and the “Save as PDF” option, and in the browser its own print dialog.

### A QR code
The quickest way to hand a note to someone in the same room: **QR code…** in the page menu (⋮), in **More** or in the command palette (“Share as QR code…”) shows the note as a QR code. Point another phone's camera at it and the note's text appears there — no internet, no account and no app on the other side. **QR code…** in a block's menu (or in **More** of a selection) does the same with the chosen blocks only.

- **Markdown** gives the note exactly as written — another notes app (Brulion, Obsidian, Logseq) gets it whole, with links and tasks.
- **Plain text** gives what the note shows, as “Copy as plain text” does: bullets “•”, tasks ☐ and ☑, links by their labels.
- **Include the title** puts the page's title first (a daily note: its day); a note that starts with its own `# Title` already has one.

One code holds about three thousand letters (fewer with emoji and accented letters). A longer note is cut into a few codes of about the same size — **Code 1 of 3**, with ‹ › or the arrow keys: scan them in turn and join the texts. Twenty codes at most; for a longer note select the part you want to share.

Because your notes are ordinary files, the simplest backup is copying the notebook folder.

Back to the [[Contents|contents]]. If you want to understand what's in the files, see [[File format|File format]].
