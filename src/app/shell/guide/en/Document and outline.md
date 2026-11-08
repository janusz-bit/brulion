---
tags: [documentation]
brulion: document
---
# Document and outline

Every note has one of two modes. **The file's content is the same** — only the way it is shown and edited changes.

| Mode | Looks like | Good for |
|---|---|---|
| **Document** (like Obsidian) | continuous text: headings, paragraphs, ordinary lists | descriptions, articles, meeting notes |
| **Outline** (like Logseq) | a bullet for every block, indentation, folding, zooming in | the journal, lists of thoughts, planning |

This page is a document; so is [[Tasks|Tasks]]. The journal is an outline by default.

## Choosing and switching the mode
- When you create a page (Ctrl+N), you choose the mode in the dialog.
- Later you switch it on a computer with the icon next to the title, and everywhere in the page menu **⋯** (“Switch to document” / “Switch to outline”) or with the “Toggle note mode” command in the palette; on a phone there is no icon next to the title — change the mode in the page menu.
- The default modes of new pages and of the journal are set in *Settings → Notebook*.
- The choice is saved in the note itself as the property `brulion: document` or `brulion: outline`, so it looks the same on every device.
- Notes without this key guess the mode from their content: a page made only of bullet points opens as an outline, a page of headings and paragraphs as a document.

## Blocks
A block is every list item and, outside lists, every paragraph, heading, table, quote and code block. Click a block to edit it; when you aren't editing it, you see it formatted.

| Keys | Action |
|---|---|
| Enter | split the block at the cursor (new block); at the start of the text: an empty block above it (for a task, an empty task — the text keeps its status and sub-items) |
| Shift+Enter | new line inside the block |
| Tab / Shift+Tab | indent / outdent the block with its children |
| Alt+↑ / Alt+↓ | move the block with its children |
| Ctrl+. | fold / unfold the children (outline) |
| Esc | stop editing and select the block |
| Shift+↑ / Shift+↓ | select several blocks (from the edited one: once the text selection reaches the edge of the block) |
| Ctrl+A twice | the block's text, then all blocks |
| Ctrl+C / Ctrl+X / Ctrl+V | copy / cut / paste — the selected blocks or the text in the block |
| Ctrl+Enter | change the task status |

In an outline, click a block's bullet to **zoom in** on it — you see only that block and its children. Enter in a zoomed-in list item adds a new sub-item that stays visible. A heading or paragraph has no children: Enter in a zoomed-in heading or paragraph (except at its start) ends the zoom and adds a new block below it — Ctrl+Z returns to the zoom. In a document, Enter on an empty list item ends the list, and Tab works only on list items.

**Code blocks:** type `` ``` `` with the name of a language (e.g. `` ```js ``) and press Enter. In a code block you see only the code — the fence lines and the block id (`^id`) below it are hidden. A `` ``` `` line typed inside the code stays code: Brulion makes the fence longer by itself. When you start code with a `` ``` `` line in a paragraph or list item, Brulion adds the closing line at the end of the block, so the code doesn't swallow the following blocks. A lone opening line (e.g. `` ```js `` without Enter) is closed by Brulion when you move to another block — an empty code block remains. Enter and pasting between these lines work as in a code block.

## Selecting and copying
In the block you are editing you select text as usual. A selection that goes beyond the block covers **whole blocks** together with their children (folded ones too) — the same in a document (paragraphs, headings, list items) and in an outline.

- **Keyboard:** Esc selects the edited block, Shift+↑ / Shift+↓ grows and shrinks the selection, Ctrl+A twice selects all blocks.
- **Mouse:** Shift+click selects from the edited (or selected) block to the clicked one — also calculations, a table, a query, a picture or a bullet; dragging from one block to another selects all blocks in between, and near the edge of the window the page scrolls by itself. Dragging with the mouse no longer scrolls the page — the wheel and the touchpad do. Ctrl+click adds a single block to the selection or takes it out — that is how you select, for example, items 1, 3 and 5 with gaps between them; Shift+click then selects a run of blocks again, starting from the last one you added.
- **Phone:** long-press a block (one you aren't editing: on its text, calculations, a table cell, a query) — the block gets selected, the keyboard goes away and the selection tools appear at the bottom. Without lifting your finger, move it to select more. After that, tapping another block moves the end of the selection, and two round handles at the start and end of the selection let you adjust it. The “One by one” button in the selection tools makes taps add or remove single blocks (with gaps, without handles); tapping it again turns this off. “Done” or Back ends selecting. The “Select” button is also in the toolbar above the keyboard and in the (long-pressed) bullet's menu. Inside the block you are editing, a long press still selects a word with the system's handles.
- **What to do with a selection:** Copy, Cut, Delete (Ctrl+Z or “Undo” in the “Deleted…” / “Cut…” notification brings it back), indent and outdent, move up and down, task status — all at once. A selection with gaps is copied, cut and deleted the same way, while indenting and moving work only on blocks selected without gaps — Brulion then tells you why nothing happened. When a single block with children is selected, Ctrl+Enter changes the status of that block only. On a computer, with two or more blocks selected, a bar with these buttons appears at the top; a right click on a selected block opens a menu that acts on the whole selection. Ctrl+V replaces the selected blocks with the clipboard's content.
- **What goes to the clipboard:** blocks as Markdown — pasted into Brulion, Obsidian or a plain text field they stay an indented list, and in an email or a word processor they paste formatted (list, headings, task checkboxes). “Copy as plain text” (the *More* menu of the selection, the block menu) gives plain text: bullets •, ☐ and ☑ instead of task checkboxes, no markup. A selection that is too large (over about 800 KB) isn't copied — Brulion tells you so instead of cutting the text short.
- **Several journal days at once:** in the **Journal** view a selection can span the blocks of several days. Select a block in one day and Shift+click a block of another day, drag with the mouse from one day to the next, or go past the last / first block of a day with Shift+↓ / Shift+↑; on a phone, while selecting is on, tap a block of another day. The days in between are selected completely. Such a selection can only be **copied** (Copy, Ctrl+C, “Copy as plain text”): each day's blocks go to the clipboard after a link to that day (`[[2026-10-27]]`, in plain text after the date, e.g. “Tuesday, October 27, 2026”). Cut, Delete, indenting and moving are then greyed out and say “Only copying works across several days”. Clicking a block, Esc or “Done” ends the selection in all days. Dragging with the mouse scrolls the journal about one screen beyond the day where the selection started — make a longer selection with the mouse wheel and Shift+click.
- **Pasting:** Markdown with several blocks becomes blocks with their indentation kept; a single copied list item pasted into an empty item doesn't get a second marker (a task stays a task); text copied from a web page or a word processor arrives as Markdown (lists, headings, links, bold). Pasted block ids (`^abc123`) that already exist elsewhere in the notebook are dropped so that `((references))` don't point to two blocks — after cut and paste (a move) they are kept.

**Parts of several paragraphs** (the end of one and the start of the next): select the whole blocks and copy them, or switch the page to Markdown source (Ctrl+E, *More* menu → *Mode* → “Source”) and select exactly those characters there — by dragging the mouse, or on a phone by long-pressing a word.

## The `/` menu
Type `/` at the start of a block and the list of all commands appears — scroll it, or type a few letters to narrow it down. It starts with the ones used most: task, headings, table, reminder, due date, file and image; further down are code block, quote, callout, divider, alarm, repeat, dates, picture from the internet, drawing, embed, block reference, template, table from CSV, calculation, query, diagram, chart, math formula and the other task statuses. Accents are optional in the filter, and the Polish names of the commands work too.

## Suggestions while typing
- `[[` — pages; when the typed name isn't in the notebook yet, the first item, “Create page “…””, inserts a link to a new page with that name. Enter picks the highlighted item: the best matching existing page (go up with the arrow key to get the new one), or the new page when nothing matches,
- `((` — blocks from the whole notebook,
- `#` — tags,
- after `📅`, `⏳`, `⏰` or `@` — dates in words: “tomorrow at 9”, “friday 18:00”.

## Callouts, highlights, comments
> [!note] Callout
> Kinds: `note`, `tip`, `warning`, `danger`, `info`, `todo`, `quote`. You type `> [!tip] Title`, and on the following lines `> content`.

==Highlight== goes between double equals signs, ~~strikethrough~~ between `~~tildes~~`, and a comment visible only while editing between `%%percent signs%%`.

### Colored highlights
A highlight can have a color: <mark style="background: #FF5582A6;">red</mark>, <mark style="background: #BBFABBA6;">green</mark>, <mark style="background: #ADCCFFA6;">blue</mark> and others. Select text in the block you are editing, right-click it and choose **Highlight…** (on a phone, the highlighter button in the toolbar above the keyboard), then a color. When the cursor or selection is inside a highlight, the same choice changes its color, and **Remove highlight** takes it off. You can undo every change (Ctrl+Z).

In the file, a colored highlight looks the way the Highlightr plugin for Obsidian writes it: `<mark style="background: #FF5582A6;">text</mark>` — so Obsidian, GitHub and Pandoc show it without Brulion. Brulion also reads `<mark class="hltr-red">` and other colors, but always shows them in one of the nine colors of its own palette: nothing of the look stored in the note (style, script) gets into the app. The default color is the plain highlight between double equals signs.

## Source mode
**Ctrl+E** shows the whole file as raw Markdown with syntax highlighting. It comes in handy for bulk changes; pressing it again returns to blocks. On a phone you scroll the text with your finger, a tap places the cursor, and a long press selects a word. (To copy parts of several blocks you no longer need the source: see [[#Selecting and copying|Selecting and copying]].)

## Statistics and focus mode
In the bottom right corner of a page (in a wide window) you see how many words and characters it has and how long it takes to read — with text or blocks selected, the numbers are for the selection. Hover over them to also see the number of characters without spaces. The same is always at the top of the page menu (**⋯** next to the title), on a phone too. You turn the counter off in *Settings → Editor → Word count*.

Brulion counts what a reader sees: without Markdown markup, YAML front matter and `key:: value` properties, block ids, task and reminder dates, `%%…%%` comments, pictures and embeds; a link counts as its text (`[[Page|label]]` is “label”), and code, query, calculation and diagram blocks as well as formulas in `$$` don't count at all. A word is anything between spaces that has at least one letter or digit: “well-known”, “e.g.”, “3.14” and a web address are one word each, while a lone dash “—” or an emoji is none. A non-breaking space after a short word (as Polish typography puts after “w” or “i”) separates words, but between digits (“1 000 000”) it doesn't. Reading time is calculated at 200 words per minute.

**Focus mode** (F8, “Focus mode” in the palette or *Settings → Editor*) hides the sidebar, the top bar, the side panel and the bottom navigation — on a page and in the journal only the text remains, in the middle of the screen. You leave it with F8, Esc (when the editor no longer needs it to close suggestions or a selection), the button in the top right corner, or the Back gesture on a phone. Brulion remembers it on this device, also after a restart; other views (tasks, calendar, settings) show their bars as usual.

**Typewriter scrolling** (*Settings → Editor*, or “Typewriter scrolling” in the palette) keeps the line you are typing in in the middle of the screen — also in source mode and in the journal. There is then some room below the last block so that the last line, too, can stand in the middle.

## Reading mode
**Reading mode** shows pages and the journal for reading only: a tap or click follows links, opens pictures, PDFs and embedded notes, folds and zooms in — but never starts editing, so on a phone the keyboard doesn't pop up while you read, and nothing changes a note by accident. You switch it with the **eye** button in the top bar, “Reading mode” in the page menu (**⋯**) or in the palette, **Ctrl+Shift+E**, or in *Settings → Editor*. Brulion remembers it on this device, for every page and the journal.

While you read you can still select blocks and copy them (also “Copy link” — except a link to a block that has no id yet, which would add one to the note), search the note with Ctrl+F, filter a table, see the version history, export and print. Whatever would change the note — a tap on the text or a checkbox, the task status menu, Delete, Ctrl+V, Ctrl+Z, Ctrl+E — shows a notification instead, with **Edit**, which turns reading mode off. A drawing opens as a picture; “Edit drawing” there opens it for drawing. Changes from sync and other devices still show up right away.

## Images and files
Paste a picture (Ctrl+V), drop files onto the page or use `/Image…` — the file picker accepts several pictures at once, in the browser too. The files go to the attachments folder (`assets/`) and are embedded as `![[file.png]]`.

You insert a file of any type as a link with the `/File…` command: `[[report.docx]]` appears in the text (several chosen files — several links). A file from outside the notebook goes to the attachments folder, and a file that is already in the notebook stays where it is. A note, a picture and a PDF open as usual when clicked; other files open in Brulion read-only, as text.

Clicking an embedded picture shows the picture alone, fitted to the view (a small picture stays at its natural size, without blurry enlarging). When the picture is larger than the view, clicking it or the “Zoom to actual size” button (on a phone and in a narrow view, the magnifier icon) shows it at its natural size, and you can drag it around. Below the picture are the notes that embed it or link to it (on a wide screen also in the side panel, on the Links tab).

“Show as text” in the bar or in the More menu (or Ctrl+E) shows the file as text — read-only, useful for SVG files (the content of a binary picture is unreadable characters; of a large file you see the first 200 KB; a change of the file, e.g. from sync, shows up here right away); “Show picture” or Ctrl+E again goes back to the picture. If the device has no decoder for a format (e.g. WebP or AVIF on a phone or in the browser), Brulion says so instead of showing an error — also for a picture embedded in a note.

Other files that aren't a note, a picture or a PDF (e.g. DOCX or ZIP) open read-only as text — nothing you type overwrites them.

## PDFs
A PDF embedded in a note (`![[lecture.pdf]]`, just like a picture: pasted, dropped or chosen with `/Image…`) shows a **thumbnail of the first page** — a white sheet in a thin frame, in the dark theme too — and below it the file name and the number of pages. `![[lecture.pdf#page=3]]` (the Obsidian notation) shows the third page. The thumbnail is drawn once and kept in the device's cache, so the journal scrolls smoothly, and after a restart it shows up right away.

Clicking the thumbnail, a link `[[lecture.pdf]]` (or `[[lecture.pdf#page=3]]` — straight on the third page), or a file chosen in the tree or the quick switcher opens the **PDF viewer**:
- you scroll the pages vertically; they are drawn only when you get to them, so even a thick script opens right away;
- **zooming**: with two fingers, `Ctrl` + mouse wheel, the **+** and **−** buttons or the percentage in the bar (e.g. 100 %, 200 %, **Fit width**); a double tap switches between page width and double zoom; you move a zoomed page with your finger or the mouse;
- **“Page 3 of 12”** in the bar (on a phone “3/12”) — tap it to go to a page;
- **search** (magnifier, `Ctrl+F`) ignores accents and letter case: “cafe” finds “Café”; matches are highlighted, and `Enter` / the arrows go to the next one. A scan (a PDF without a text layer) has nothing to search — Brulion tells you so. On Android, search works from version 15; on older versions there is no magnifier;
- **text selection**: drag the mouse over the page (the cursor turns into a vertical bar) or long-press a word — the whole word gets selected — and, without lifting your finger, move on. **Copy** above the selection, `Ctrl+C` or a right click copies the text to the clipboard, `Ctrl+A` selects the whole current page, `Esc` or a tap elsewhere ends the selection. A selection covers one page. A scan (without a text layer) has nothing to select; on Android, selection isn't available yet;
- **table of contents** — a button with a list in the bar, when the PDF has one (bookmarks): it shows the chapters with page numbers, marks the one you are in, and a tap takes you to the chapter. For now the table of contents works in builds with poppler (e.g. the package for NixOS); in the Windows version, the AppImage and on Android the button isn't there yet;
- **links in the PDF**: a link to another page of the same PDF (e.g. in the table of contents on the first page) takes you there, and a web or email address opens in the browser or the email app — just like links in notes, only `http`, `https` and `mailto`; scripts and launching programs from a PDF never work. Hover with the mouse to see where a link leads;
- **two pages side by side** (the zoom menu, on a phone **⋯**) — like an open book; with **Cover page alone** the first page stands by itself, and then 2–3, 4–5… Brulion remembers this choice on the device;
- **annotations** stored in the PDF (highlights, notes, stamps) are visible on the pages; add new ones in another app (**Open in another app**);
- **Draw on this page** — makes a drawing with the current page as its backdrop (see [[Drawings#Notes on slides and PDFs|Notes on slides and PDFs]]);
- **Open in another app** (in the **⋯** or **More** menu) — hands the PDF to the system's PDF app, e.g. to print it or add annotations;
- **Back**, `Esc` or `Alt+←` closes the viewer. Below the last page are the notes that embed the PDF or link to it.

A **password-protected** PDF shows a padlock, the file's name and size and a password field: type the password and tap **Open the PDF** (or press `Enter`). With a wrong password Brulion asks again. It never stores the password — you type it again every time you open the PDF — so the thumbnail of such a PDF in a note stays a padlock, and **Draw on this page** isn't available for it. On Android the system's PDF module can't open password-protected files yet: there you get the message “This PDF is protected with a password: Brulion cannot open it.”, and **Open in another app** hands the file to an app that can. A damaged PDF shows as much as it can, and a page that can't be drawn says so instead of freezing the app.

**Other attachments** — Word and LibreOffice documents, spreadsheets, presentations, e-books, recordings, videos, text and CSV files (`.docx`, `.xlsx`, `.pptx`, `.odt`, `.ods`, `.odp`, `.epub`, `.doc`, `.xls`, `.ppt`, `.rtf`, `.mp3`, `.m4a`, `.ogg`, `.opus`, `.wav`, `.flac`, `.mp4`, `.mov`, `.webm`, `.mkv`, `.txt`, `.csv`, `.tsv`) — are shown by Brulion read-only as text, and the **Open in another app** button in the bar (and in the **More** menu) hands them to an app that opens them. Brulion checks not only the name but also the start of the file: a program, a script, a web page or an archive — even one renamed to `.docx` — is never handed to another app.

**In the browser** Brulion doesn't draw PDFs: an embedded PDF is a file tile, and clicking it or a link opens the PDF in a **new tab**, where the browser's built-in PDF viewer gives you scrolling, zooming and search. When the browser blocks the new tab, the PDF is downloaded, and Brulion tells you so.

See also: [[Links and backlinks|Links and backlinks]], [[Keyboard shortcuts|Keyboard shortcuts]].
