---
tags: [documentation]
brulion: document
---
# Links and backlinks

Links tie your notes into a web. Every page shows its **backlinks** in the side panel (the **Backlinks** section) and below its content: the pages and blocks that lead to it — with a link or a [[Tags and properties#A tag is a link to a page|tag]] — including those that only mention its name without a link (the **Unlinked mentions** section).

The side panel folds into sections: for a page **Outline**, **Backlinks**, **Unlinked mentions** and **Local graph** (the pages linked to and from it — click one to open it); for a day of the journal **Day plan** (that day's reminders and dates by the hour), **Tasks** (open tasks due by that day, overdue ones in red), **A year ago** (the journal of the same day last year), **Backlinks** and **Calendar**. A click on a section's title folds or unfolds it. On a computer the panel sits beside the text (the panel button in the top bar, `Ctrl+\`); on a phone it slides in from the right edge: tap the small tab on the right edge (it shows the number of backlinks) or the panel button in the top bar, and close it with a tap on the dimmed page, a swipe to the right or Back.

## Kinds of links
| Notation | Meaning |
|---|---|
| `[[Page]]` | link to a page |
| `[[Folder/Page]]` | link to a page in a folder — each part of the path is a separate link |
| `[[Page\|label]]` | link with your own text |
| `[[Page#Heading]]` | link to a heading |
| `[[Page#Chapter#Heading]]` | link to the heading “Heading” in the chapter “Chapter” (when the name repeats) |
| `[[#Heading]]` | link to a heading on the same page |
| `[[Page#^id]]` | link to the block with the id `id` |
| `((id))` | reference to a block from any page — shows its text |
| `[text](https://address)` | ordinary Markdown link |
| `#page`, `#[[Two words]]` | tag — leads to the page with that name |

Examples: [[Contents|Contents]], [[Tasks#Recurrence]], and a page that doesn't exist yet: [[Note to create]]. Clicking such a link creates the page.

**Link menu** — a right click on a link (on a phone, a long press): “Open” (for a page that doesn't exist yet: “Create page”), “Open in side panel” (in a wide window) and “Copy link”. The copied link names the page so that it works when pasted into any note (`[[Page#Heading]]`). On a web address the menu has “Open in browser” and “Copy address”. A link to a journal day (`[[2026-10-20]]`) has the date menu — see [[Journal, calendar and templates#Journal|Journal]].

## Link preview
Rest the mouse pointer on a link to a page (`[[Page]]`, `[[Page#Heading]]`, `[[Page#^id]]`) or on a reference `((id))` — after a moment a pop-up appears next to it with the content of the page, section or block, without leaving your note. The same works in backlinks, **Tasks**, embedded pages, tables and query results.
- The pop-up stays while the pointer is on the link or on the pop-up — you scroll longer content with the mouse wheel. It goes away a moment after the pointer leaves both, or when you click elsewhere.
- Clicking the name at the top of the pop-up opens the page (with `Ctrl` — in the side panel), and clicking a link in the pop-up goes where that link leads.
- For a page that doesn't exist yet, the pop-up says so — clicking the name creates it. For a missing heading or block it says it wasn't found.
- The pop-up only shows: task checkboxes and dates in it don't change. Links to attachments (pictures, PDFs) and drawings have no preview.
- On phones and tablets there is no preview (a touch screen doesn't know “hovering”); a long press on a link opens its menu as before, and **Open in side panel** shows the page next to your note on a wide screen.

## How Brulion finds a page
The name in a link is matched against the files in this order: the full path in the notebook, the path relative to the current folder, the file name alone (ignoring letter case), and finally the aliases in a page's front matter (`aliases: [Short name]`). When several match, the page with the shortest path wins. When two pages have the same name in different folders, use the path: `[[Projects/Home]]`.

## Links to pages in folders
Folders work like namespaces (as in Logseq). In a link to a page in a folder, e.g. `[[Company/John Smith]]`, each part of the path is a separate link — the parts are separated by a grey slash:
- **John Smith** (the last part) opens, as before, the page `Company/John Smith`;
- **Company** opens the page “Company” — the one next to the folder that holds `John Smith`, or, when there is none, the page that the link `[[Company]]` would find (by path, name or alias);
- when there is no such page but the folder **Company** has pages, the list of its pages opens in **All pages**;
- when there is neither a page nor a folder, a click creates the page “Company” like any link to a page that doesn't exist.

Deeper paths (`[[Projects/Home/Kitchen]]`) work the same way: **Home** leads to the page or folder `Projects/Home`. The part under the mouse pointer is underlined, and on a phone the part under your finger — just tap it. A right click (on a phone, a long press) on a part opens its menu: **Open** (for a folder **Show folder**, for a missing page **Create page**), **Open in side panel** and **Copy link** (for a folder **Copy folder path**, e.g. `Projects/Home/` for the `path:` filter in a [[Queries|query]]; a name with a space is copied in quotes: `"Projects/My home/"`).

A link with your own text (`[[Company/John Smith|John]]`) stays one link. While you edit a block you see the plain link notation, and the file doesn't change. Links in query results, tables, embedded pages, properties, **Tasks** and backlinks work the same way. From the keyboard: in the block you are editing, put the cursor on a part of the link and press **Alt+Enter** (**Alt+Shift+Enter** opens it in the side panel). Screen readers (TalkBack, Orca, NVDA) see every part — and every link in the text — as a separate link.

## Embedding
An exclamation mark before a link inserts the content instead of a link:
- `![[Page]]` — the whole page, `![[Page#Heading]]` — a section,
- `!((id))` — a single block,
- `![[photo.png]]`, `![[document.pdf]]` — attachments.

An embedded fragment shows the current text of its source; you change it at the source.

## Pictures from the internet
You insert a picture from a web address as in any Markdown: `![description](https://example.com/cat.jpg)` (`![[https://example.com/cat.jpg]]` works too). The easiest way is the `/` command → **Picture from the internet…**: paste the address, and the **Save to notebook** option downloads a copy right away.

- **Asking first.** Normally, in place of the picture you see the name of the site it comes from and a **Show** button. Brulion downloads nothing until you click it, so merely opening a note from an import or from sync doesn't tell any site that you are reading it. In **Settings → Editor** you can turn on **Show pictures from the internet without asking**. Even then, pictures from your home network (e.g. from a NAS at `192.168.…` or `nas.local`, from the router `fritz.box`, from a Tailscale address `100.…` or from `localhost`) wait for **Show**, and turning the option off stops the downloads it started.
- **Cache.** A picture downloaded once stays on this device (up to 200 MB; the ones viewed longest ago go first) and shows up without a network too. Nothing is added to the note's file. In the browser version the cache holds 50 MB and disappears when you close the tab — save a picture that should stay to the notebook.
- **Zoom.** Clicking (tapping) a shown picture opens it in the whole window, like an attachment: fitted to the window, and another click or the magnifier button shows it at its real size, which you move around with your finger or the mouse. The bar shows where it comes from and has **Save to notebook**, the **⋯** menu and **Close** (or `Esc`, on Android the **Back** gesture).
- **Save to notebook** — a button next to the picture, in the zoomed view, or an item in the menu that you open with a right click or a long press on a shown picture (the top of the menu shows which site it comes from). The copy goes to the attachments folder, and the link changes to point to it: `![description](assets/cat.jpg)`. Such a picture works offline, syncs with your notes and doesn't disappear when the site removes it. You undo the change with `Ctrl+Z`. The command palette (Ctrl+K) has **Save pictures from the internet to notebook**, i.e. all pictures of the current page at once — in the journal, of the day you are editing (usually today).
- The picture menu also has **View picture**, **Open in browser** and **Copy picture address**.
- **Pasting.** When you choose **Copy image address** in a web browser and paste it into a note (`Ctrl+V`), the plain address is pasted, and Brulion offers **Save to notebook**: the address becomes a picture, and its copy goes to the attachments folder. When you paste a part of a web page with pictures, the pictures are inserted with their addresses, and Brulion offers to save all of them to the notebook. Nothing is downloaded until you choose to. **Copy image** in a web browser pastes right away as an attachment.
- **Export.** Export to HTML and Pandoc asks what to do with pictures from the internet: **Keep addresses** (the pictures show only with network access) or **Include copies** (Brulion takes the copies from the cache and downloads missing ones first — except pictures from your home network — and a picture that can't be downloaded stays an address). See [[Import and export#Export|Export]].
- Brulion downloads only `http://` and `https://` addresses, at most 20 MB, and checks that it really is a picture (a login page or an error isn't shown). It sends no cookies, no app name and no device language, and it rejects a redirect from the internet into your home network. When it fails, you see the reason and a **Try again** button in place of the picture.
- In the browser version many sites don't let other sites download their pictures (CORS) — then download the picture on a computer or phone, and the copy goes into the notebook. Brulion opened at an `https://` address can't download a picture from an `http://` address either (the browser doesn't allow it); in both cases you see why in place of the picture. The browser sends its name and language by itself anyway.

## Block references
To refer to a block, type `((` and start typing its content — Brulion gives the block an id, i.e. the ending ` ^abc123`. The reference `((abc123))` works across the whole notebook, so moving the block to another page breaks nothing. The link `[[Page#^abc123]]` shows the page's name and the start of the block's text.

## Copying a link to a block or heading
The block menu has a **Copy link** item that puts a link in the clipboard — you then paste it into any note:
- **Block reference** — `((abc123))`: works across the whole notebook and shows the block's text,
- **Link to block** — `[[Page#^abc123]]`: Obsidian understands it too,
- **Link to heading** (only on headings) — `[[Page#Heading]]`: doesn't change the file,
- **Embed** — `!((abc123))`, and for a heading `![[Page#Heading]]`, i.e. the whole section.

You open the block menu with a right click on any block (text, heading, code, table, quote), by long-pressing the bullet or the item number, and on a phone with the **More** (⋯) button in the toolbar above the keyboard while editing a block or a table cell. From the keyboard, **Ctrl+Shift+C** copies a reference to the edited or selected block. The command palette (Ctrl+K) has “Copy link to page”, “Copy block reference”, “Copy link to block” and “Copy link to heading” — the one the cursor is under. In the side panel, on the **Outline** tab, a right click or a long press on a heading copies a link to it or its embed. After each copy Brulion shows what it copied.

- A reference, a link to a block and an embedded block add an id to the block (` ^abc123`) if it had none — that is one change, undone with Ctrl+Z. A Logseq id (`id::`) is used as it is.
- A link to a heading adds nothing, but stops leading to its target when you change the heading's text. A block reference survives both a change of the heading and moving the block.
- In a link to a heading, formatting, the hash signs of tags and characters that break links (`#`, `|`, `^`, `:`, square brackets) are dropped: “Plan: **September** #work” becomes `[[Page#Plan September work]]`. Letter case and accents don't matter when the heading is looked up.
- When two headings on a page have the same text, Brulion copies a path as Obsidian does: `[[Page#Chapter#Heading]]`; if that repeats too — a link to the block.
- The page's name in a link is its title, and when several pages have the same title — the shortest path that tells them apart (`[[Projects/Home#^abc123]]`). “Copy link” in the page menu and in the folder tree and `brulion-cli block link` name a page the same way. A journal day goes by its file name: `[[2026-10-22#^abc123]]`.
- In this documentation, block links aren't created, because they would change its pages; links to headings and pages work.

Clicking a link to a block or heading scrolls the page to the target, selects it and highlights it for a moment — also when the target is on the same page or in the same journal day. When the target is missing, Brulion says so. A link to a block that was moved to another page opens the page where the block is now.

## Renaming and moving
Renaming a page (F2) updates the links to it and the tags that name it in all other notes. The same goes for moving a page, an attachment or a whole folder (“Move to…”, dragging in the tree, a slash in the new name in the rename dialog): every link and tag keeps leading to the same page, and the move dialog (and the hint while dragging) tells you beforehand how many of them will change.
- A link by name alone stays as long as it points to that page: `[[Home]]` doesn't change after moving the only page “Home”.
- When after the move the name would point to another page (e.g. the notebook has a second “Home” with a shorter path), the link gets the shortest unambiguous path: `[[Projects/Home]]`. Tags too: `#home` → `#Projects/Home`.
- Links with a path follow the page: `[[Projects/Home]]` → `[[Archive/Home]]`; so do pictures and files from a moved folder: `![[Projects/plan.png]]`.
- [[Queries|Queries]] follow pages and folders too: `ref:Home`, `[[Home]]`, `page:Home`, `tag:home` and `path:Projects/` get the new name or path (`path:Archive/Projects/`) and keep finding the same things. When the new name also belongs to another page, `page:` gets a notation that points to only one of them: after renaming “Home” to “Flat” when there already is a page `Archive/Flat`, `page:Home` becomes `page:Flat.md`, and `page:Flat` becomes `page:Archive/Flat`. `page:` with an asterisk, `page:this` and a name of several pages at once (two “Home” pages) stay unchanged. The rename dialog counts them together with links and tags.
- The heading, block and label stay: `[[Home#Kitchen|kitchen]]` → `[[Projects/Home#Kitchen|kitchen]]`.

The `[[` suggestions also insert a path when the chosen page has a namesake in another folder. Page history and the trash let you undo mistakes, and the message after a move has “Undo”.

## Graph
The **Graph** view draws pages and journal days as a web of connections: the whole notebook or the neighborhood of one page (depth 1–2). A tag that names a page connects to it like a link. Switches hide tags without a page, missing pages, journals and templates.

The graph lays itself out in the background — you can pan, zoom and drag pages right away, on a phone too. With the **Reduce motion** setting the graph doesn't unfold on screen: instead a spinner turns for a moment, and then the finished layout appears.

Next: [[Tags and properties|Tags and properties]].
