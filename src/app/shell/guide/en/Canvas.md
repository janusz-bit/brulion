---
tags: [documentation]
aliases: [Canvases, Whiteboard, Whiteboards, Obsidian Canvas, JSON Canvas, Mind map, Board]
brulion: document
---
# Canvas

A **canvas** is a whiteboard: cards laid out freely on an endless surface and connected with lines. A card can be a short text (Markdown), one of your notes (the whole page or just one section), a picture, a drawing, a PDF, another canvas, any other file, or a web address. Groups frame several cards under a label. Use it for planning a project, mind maps, mood boards, a map of how your notes hang together, or a wall of sticky notes before an exam.

A canvas is a `.canvas` file in the open **JSON Canvas** format — the same one Obsidian Canvas uses. A canvas made in Obsidian opens and can be edited in Brulion, and the other way round; Brulion changes only what you change and keeps everything else in the file byte for byte (also settings of Obsidian plugins it doesn't know).

## A new canvas
- The command palette (`Ctrl+K`) → **New canvas**, or the sidebar's pages menu (**…** next to the pages) → **New canvas**, or a folder's menu → **New canvas here**. Brulion makes `Untitled canvas.canvas` and opens it; rename it in the sidebar like any file.
- A canvas opens from the sidebar, from a link `[[Plan.canvas]]`, from search and from the quick switcher (`Ctrl+O`).

## Adding cards
- **Double-click** (double tap) an empty spot — a text card appears there and you can type right away. Text is Markdown: headings, lists, `**bold**`, `[[links]]`, `#tags`, tasks.
- The toolbar: **Add a card**, **Add a note…** (pick a page or type a new name to create it), **Add a file…** (a picture, PDF or any file — it is copied into the notebook's attachments), **Add a web address…**, **Group** (around the selected cards). On a phone they are under **+**.
- **Drag a page** from the sidebar onto the canvas — it becomes a note card where you drop it. Files dragged from another app are added too.
- **Paste** (`Ctrl+V`): cards copied on a canvas (also from Obsidian), a picture, copied files, a web address (a link card) or any text (a text card).
- **Drag from a card's side**: when the pointer is over a card, four small circles show on its sides. Drag one onto another card to connect them; drop it on an empty spot and a new connected card appears there.

## What cards show
- A **text card** shows its Markdown formatted; double-click it to edit (`Esc` or a click outside ends editing, the card grows to fit the text).
- A **note card** shows the page as it looks in the editor, or only the section or block it points to (`#Heading`, `#^block`). Scroll a long note with the mouse wheel while the card is selected. Double-click to open the note.
- **Pictures**, **drawings** (in the app's theme), **PDFs** (the first page) and **canvases** (their picture) show their picture; other files their name. Links to web addresses show the address — Brulion never loads the page itself.
- When you zoom far out, cards show just their title in large letters, so you can still find your way around a big canvas.

## Moving around
- **Move the canvas**: the mouse wheel or two fingers on a touchpad, the middle mouse button, or hold `Space` and drag. On a touch screen drag with one finger.
- **Zoom**: `Ctrl` + wheel, a pinch on a touchpad or the screen, `Ctrl+=` / `Ctrl+-`, `Ctrl+0` for 100 %. **Show the whole canvas**: `Shift+1`; the selection: `Shift+2`.

## Selecting and editing
- Click a card or a line to select it; `Shift`+click adds to the selection. Drag on an empty spot to select several cards with a rectangle. On a phone, turn on **Select several** in the toolbar.
- Drag selected cards to move them — a group moves together with the cards inside it. Drag a corner or a side of a selected card to resize it.
- The bar above the selection: **colour** (six colours as in Obsidian), **edit**, **open**, **arrowheads** of lines (none, one way, both ways, reversed), **duplicate** (`Ctrl+D`), **group** / **ungroup** (`Ctrl+G` / `Ctrl+Shift+G`), **align** and **space evenly**, **arrange** (front, back), **delete** (`Del`).
- Double-click a **line** to give it a label, a **group** to rename it.
- A right click (a long press) opens the menu with the same and more: copy, cut, paste, open in a new tab.
- **Undo** and **Redo** (`Ctrl+Z`, `Ctrl+Shift+Z`) undo every change.
- The arrow keys move the selected cards (with `Shift` by one point), `Enter` edits the selected card, `Esc` clears the selection.

## Links, search and renaming
- Notes on a canvas and `[[links]]` in its text cards count as links: the note's **backlinks** show the canvas (a click opens it at that card), and so does the graph.
- **Search** finds canvases by the text of their cards; a result opens the canvas zoomed to the card.
- When you **rename or move** a page or a file, every canvas that shows it or links to it follows: Brulion rewrites the path in the canvas file.
- Tasks and reminders written on a canvas card are just text there: they don't appear in **Tasks** and never ring.

## Saving, sync and export
- A canvas saves itself a moment after each change. When another device or Obsidian changes it while you have it open, both versions are merged card by card.
- When the same canvas changed on two devices, the sync merges it card by card (a card moved on one and edited on the other keeps both changes). When that's not possible, the **Sync center** shows both versions as pictures and the **Merge canvases** button.
- **More** → **Export as PNG picture** or **Export as SVG picture** saves a picture of the whole canvas next to it. An HTML export of the notebook shows canvases as pictures.

> [!example] Try it
> The sample notebook (*Settings → About → Sample notebook*) has a canvas, *Home renovation plan.canvas* — open it from the sidebar and move things around.
