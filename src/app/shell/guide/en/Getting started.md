---
tags: [documentation]
brulion: document
---
# Getting started

## Notebooks and folders
Brulion keeps your notes as plain `.md` files in a folder you choose — that folder is your **notebook**. There is no account and no database; you back it up by copying the folder, and you can open the same folder in Obsidian or Logseq.

On first start you pick a folder, create a new notebook or open the “Sample notebook” with notes to try things out. You switch between notebooks with the “Switch notebook” button — the first letter of the notebook's name at the very top of the icon bar on the left. Its menu lists your recent notebooks plus “Brulion Documentation” and “Open another notebook…”. Ctrl+Shift+B takes you to the documentation and back.

## Your first entry
1. Open the **Journal** (Ctrl+J) — that is today's page.
2. Start typing. Every line that starts with a dash is a separate block.
3. Changes are saved automatically; there is no “Save” button.

The day's page is only created with your first change — just looking doesn't create anything.

## Writing on a phone
While you type, the toolbar sits right above the keyboard — in the journal and on every page, even while you scroll. It has indenting, moving blocks, task, link `[[`, tag `#`, `/` commands, due date, reminder, picture, undo and redo, and **More** (⋯) — the block menu, e.g. with copying a link to the block; when there are more buttons than fit on the screen, swipe the toolbar sideways. The bottom navigation (with **+ Note** and **Menu**) hides while the keyboard is open; the sidebar is then opened with the menu button (three lines) in the top left corner, which is always there.

- **Hide keyboard** (the last button of the toolbar) ends typing: it hides the keyboard and the toolbar, and the bottom navigation comes back.
- When you hide the keyboard with the Back button or a key on the keyboard itself, the bottom navigation (Journal, Calendar, Tasks…) comes back and the toolbar stays right above it with all its tools — tap the text and the keyboard returns. A navigation button ends typing and opens its view, and **+ Note** ends typing and opens a quick capture.
- In a table cell the toolbar has Undo, Redo, **Next cell** and Hide keyboard; in the Markdown source (More → Mode → Source) — `[[`, `#`, `/`, Undo, Redo and Hide keyboard.
- Dialogs that slide up from the bottom (quick capture, reminder, rename), suggestions while typing and form fields (e.g. in the settings) also always stay above the keyboard.

## New page
- **Ctrl+N**, the pencil button next to the notebook's name, or “+” next to the **Pages** section in the sidebar (→ “New page”). When you are on a page inside a folder, the new page goes into the same folder — the dialog shows it as “In folder: Projects”; the × next to it or “Change…” picks another place. The name can also contain `/`, and then the page goes into a folder: `Projects/Home`.
- In the dialog you choose the mode: document or outline (see [[Document and outline|Document and outline]]).
- Shortcut: type `[[Name of a new page]]` in any note and click the link — the page is created.

## Quick capture
**Ctrl+Shift+N** (on a phone, **+ Note** in the bottom bar) opens a single text field. What you type goes to the end of today's journal (from the documentation, Brulion first returns to your own notebook). Dates in it become due dates and reminders: “call Anna tomorrow at 9”.

## Search and navigation
- **Ctrl+O** or the **Search or type a command…** field in the top bar — jump to a page by typing a few letters of its name. The last item of the list searches for the typed words in the text of all notes, and a `>` at the start switches to commands (like the command palette).
- **Ctrl+Shift+F** — search the text of all notes; accents can be left out (“cafe” finds “café”).
- **Ctrl+F** — find in the open note (on a phone: page menu → **Find and replace…**), **Ctrl+H** — straight to find and replace. Accents and letter case can be ignored; the “Aa”, “ąa” and “Word” buttons narrow the results. The search covers the note's Markdown text, including link addresses. A single **Ctrl+Z** undoes “Replace all”. In the journal stream you search in the day you are writing in.
- **Replace in notebook** (the **More** menu → **Notebook**, the command palette, “Replace…” in search) — replace in all notes. First you see every match as “before → after”; untick what you don't want to change. A match inside a link is marked, because the link would start pointing somewhere else: to rename a page together with its links, rename it instead. After replacing, the **Undo** button restores the notes, and **Version history** keeps the old text of each of them too. Trash, drawings and other files are skipped.
- **Ctrl+K** — command palette.
- **Alt+←** / **Alt+→** — back and forward, like in a browser.

## Sidebar
A narrow icon bar runs along the left edge of the window: at the top the notebook (its first letter — switching notebooks), below it Journal, Calendar, Tasks, Habits, All pages, Tags, Graph and Search (a tooltip shows each icon's name and shortcut when you hover over it), and at the bottom the sync status (a click opens the Sync center) and Settings. Next to it is the pages panel: the notebook's name with a new page button, and the sections **Favorites**, **Recent** and **Pages** (the folder tree). **Favorites** appears once you star your first page (the star in the page header, the **More** menu or the page's menu in the tree), and **Recent** once you open another page (the open page isn't listed there; the tree shows it). You change the order of the sections in *Settings → Appearance → Layout*, e.g. the tree at the top and favorites at the bottom. The button on the left of the top bar hides the pages panel — the icon bar stays. On a phone the sidebar opens with the menu button (three lines) in the top left corner, with “Menu” in the bottom navigation or by swiping from the left edge of the screen. On Android the top bar has no back arrow: the system's Back button or gesture goes back to the previous view.

## Folders
Folders organize pages like directories on disk: the page `Projects/Home` is the file `Home.md` in the folder `Projects`.
- **New folder**: “+” next to the **Pages** section → “New folder” (in the notebook's top folder) or the folder's menu → “New folder…” (inside it). Names with characters that aren't allowed are corrected, and hidden ones (starting with a dot, e.g. `.private`) are rejected.
- The **menu** of a page or folder in the tree opens with a right click or a long press; on a phone also with the “⋯” button on every row. For a folder it has, among others, “Show folder pages”, “Search in folder” and “Export folder…”; for a page — “Copy link”.
- **Moving**: “Move to…” (in the tree menu, in a page's **More** menu, in the **All pages** list and in the command palette) shows the folders with a search field, and the last row, “New folder “…””, creates a folder with the typed name right away. Before moving you see how many links will change. On a computer you can also drag a page or folder with the mouse onto another folder (a closed one opens after a moment) or onto the **Pages** heading — into the top folder; Esc cancels the drag. A slash in the rename dialog (F2) moves as well: “Archive/Home” (in the title edited in the page header, a slash is an ordinary character of the title).
- Links and tags always keep leading to the same page (see [[Links and backlinks#Renaming and moving|Renaming and moving]]). Moving the journal, templates or attachments folder also updates the notebook settings.
- A journal day is an ordinary file too: in the tree, in the **All pages** list, with F2 and in the **More** menu of a day opened as a page (“Open as page”) you can rename or move it — e.g. an entry saved under the wrong date. The **Journal** view shows days by date and doesn't have these commands.
- The tree shows and highlights the open page by itself (turn it off in the “+” menu → “Show the open page”), and “Collapse all” closes all folders. The journal folder is at the top, days newest first.
- **All pages** in a folder (“Show folder pages” or the breadcrumbs above a page) show its subfolders above its pages — that is how you browse the notebook folder by folder, also on a phone. “With subfolders” decides whether the list also includes the pages of subfolders.

Views without their own button — the Sync center, import and export, trash, clean up — are in the **More** menu (⋯, on a phone ⋮) → **Notebook** and in the command palette; **Settings** is also at the bottom of the icon bar. At the top, the **More** menu has page actions as icons (Favorite, Rename, Move, History), the **Mode** switch (Document, Outline, Source) and — on a computer — **Open in** (New tab, Window, Side panel, Split). The event log is in *Settings → Application* and in the palette.

## Appearance
In *Settings → Appearance* you tailor Brulion to yourself — separately on each device:
- **Theme**: light, dark or following the system; **accent colour** (red, blue, green, teal, violet, graphite or **Custom…** — any color from the grid or typed as `#3b7dd8`). The accent highlights today, the current tab and selections; where the chosen color would be hard to read, Brulion uses a lighter or darker shade of it by itself. Warnings and deleting stay red.
- **Light background**: paper or white; **dark background**: blue-grey or black (saves battery on OLED screens).
- **Text size**, **interface font** (any installed font) and **density** — compact fits more on a computer screen (on a phone the buttons stay large).
- **Layout**: which items and sections the sidebar shows, in what order Favorites, Recent and Pages appear, which four buttons the bottom navigation has on a phone, and whether it has the quick capture button **+ Note**. Hidden views stay in the command palette and under their shortcuts.

The font of notes (serif, sans-serif or **Other…**), the code font, the text size and the **line spacing** are in *Settings → Editor*. “Restore the default look” goes back to the original colors and fonts. In the browser there is no choice of fonts — only the browser's built-in ones work.

## New version
On Android and Windows, Brulion asks GitHub once a day for the latest release. When there is a newer one, a message appears below the top bar, and **View on GitHub** opens the release page, where you can download it. The × hides the message until the next version. GitHub sees the device's address; nothing about your notes is sent. You turn this off in **Settings → Application → Check for new versions**.

## What next
- [[Document and outline|Document and outline]]
- [[Links and backlinks|Links and backlinks]]
- [[Contents|Contents]]
