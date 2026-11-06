---
tags: [documentation]
brulion: document
---
# Keyboard shortcuts

Shortcuts on a computer — you can change the ones in the “General” table: see [[#Custom shortcuts|Custom shortcuts]]. On a phone, indenting, moving, tasks, links, tags, commands, dates and undo are in the toolbar right above the keyboard (long-press the Task button to choose a task status; see [[Getting started#Writing on a phone|Writing on a phone]]).

## General
| Shortcut | Action |
|---|---|
| Ctrl+K | command palette |
| Ctrl+O | jump to a page |
| Ctrl+Shift+F | search |
| Ctrl+J | today's journal |
| Ctrl+Alt+W | this week's note |
| Ctrl+Alt+M | this month's note |
| Ctrl+N | new page |
| Ctrl+Shift+N | quick capture into the journal |
| Ctrl+Shift+B | Brulion Documentation / back to the previous notebook |
| Alt+← / Alt+→ | back / forward (in the current tab) |
| Ctrl+T | new tab |
| Ctrl+W | close the tab (in a page's separate window: that window) |
| Ctrl+Tab / Ctrl+Shift+Tab | next / previous tab (also Ctrl+PgDown / Ctrl+PgUp) |
| Ctrl+1 … Ctrl+8 / Ctrl+9 | the tab with that number / the last tab |
| Ctrl+\ | side panel (on a phone: slides in from the right) |
| F2 | rename the page |
| Ctrl+P | print the page ([[Import and export#A single page|exporting and printing a page]]) |
| Ctrl+, | settings |
| F8 | focus mode: only the text, no bars (Esc leaves it too; [[Document and outline#Statistics and focus mode|Focus mode]]) |
| Ctrl+E | Markdown source mode |
| Ctrl+F | find in the note, in the journal in all days (Enter: next, Shift+Enter: previous, Esc: close) |
| Ctrl+H | find and replace in the note |
| Ctrl+; | add a page property |
| Delete | in the page tree: move the page, file or folder to the trash (after confirming; “Undo” in the notification brings it back) |
| Ctrl+Enter | in dialogs: confirm |

## Tabs, split view and windows
Several pages can be open at once, each in its own tab. Every tab has its own history (back / forward) and remembers where the page was scrolled. Tabs are remembered separately for each notebook on this device and come back after a restart.

* **New tab**: Ctrl+T or “+” in the tab bar. You open a page from the sidebar, the list of all pages, search results or backlinks in a new tab with the **middle mouse button** or **Ctrl+click**. A link in a note — with the middle button or from the link menu (right click): “Open in new tab” (in the editor Ctrl+click selects blocks).
* The **tab bar** appears when there are at least two tabs. Tabs can be dragged, the middle button or × closes a tab, and a right click opens its menu.
* **Split view**: *More* → *Open in* → “Split” (or the command palette) shows a second page next to the first — click the half of the window you want to work in. “Close split view” goes back to a single view.
* **Separate window** (computer): “Open in new window” from the link menu, the page's menu in the tree or *More* → *Open in* → “Window”. Changes in one window show right away in the other. “Move to main window” turns the window back into a tab.
* **On a phone** there is no tab bar: when there are several tabs, a small square with their number appears in the top bar — tap it to switch tabs or close the others.

## Custom shortcuts
You change the shortcuts of the palette's commands (Ctrl+K) in *Settings → Keyboard shortcuts* (or in the palette: “Keyboard shortcuts”). The list has all commands with their shortcut — also those that have none by default, e.g. “Sync now” or “Tasks”. The search field above the list finds a command by name or by keys (“ctrl+j”).

- **Change** waits for the new shortcut: press a key with Ctrl or Alt (or a function key F1–F12). Esc cancels.
- When the shortcut already belongs to another command, Brulion tells you which; **Assign here** moves it, and that other command is left without a shortcut.
- You can't choose keys you type with (letters without Ctrl or Alt, Enter, Tab, arrows), the editor's keys (Ctrl+C, Ctrl+V, Ctrl+Z, Ctrl+A, Ctrl+Enter, Alt+↑…), those of drawings, the PDF viewer and tab switching (Ctrl+1…9, Ctrl+PgUp, Ctrl+PgDown), or Ctrl+Alt with a letter that AltGr produces on Windows (on a Polish keyboard Ctrl+Alt+A types “ą”).
- **✕** leaves the command without a shortcut, **↺** restores its default, and **Restore all** — all the defaults.

A new shortcut works right away across the whole app: the palette and the button tooltips show it instead of the default. Custom shortcuts are a setting of this device (every computer has its own); the block editing keys from the tables below stay as they are.

## Editing blocks
| Shortcut | Action |
|---|---|
| Enter | split the block / new block (at the start of the text: an empty block above it, for a task an empty task) |
| Shift+Enter | new line in the block |
| Tab / Shift+Tab | indent / outdent with children |
| Alt+↑ / Alt+↓ | move the block with its children |
| Alt+Enter | open the link at the cursor (in a link `[[Folder/Page]]` — the part at the cursor) |
| Alt+Shift+Enter | the same in the side panel (wide window) |
| Ctrl+Enter | change the task status (in turn) |
| Ctrl+Shift+Enter | task status menu (any status or no status) |
| Ctrl+. | fold / unfold |
| Ctrl+Shift+C | copy a reference to the block `((…))` |
| Ctrl+Z, Ctrl+Y | undo / redo |
| Ctrl+C / Ctrl+X | copy / cut the selected text of the block (exactly, with non-breaking spaces) |
| Ctrl+V | paste: Markdown with several blocks becomes blocks, a copied list item is pasted without a second marker, text from a web page arrives with its lists and headings; a picture or files as attachments |
| Ctrl+A | select the block's text; a second time: all blocks of the page |
| Shift+↑ / Shift+↓ | select text; on the first / last line, once the selection reaches the edge of the block: select this block and the previous / next one |
| Esc | close the suggestions, then stop editing and select the block |
| / | command menu |

## Table cell

| Shortcut | Action |
|---|---|
| Tab / Shift+Tab | next or previous cell (Tab in the last cell adds a row) |
| Enter | the cell below (in the last row adds a row) |
| Ctrl+Enter | insert a row below |
| Alt+↑ / Alt+↓ | move the row |
| Alt+← / Alt+→ | move the column |
| Esc | leave the cell and undo the text typed in it |

## Properties panel
| Shortcut | Action |
|---|---|
| ↑ / ↓ | previous / next property |
| → or Enter | change the value |
| ← or F2 | rename |
| Delete or Ctrl+Backspace | delete the property |
| Ctrl+; | add a property |
| Alt+↓ | to the first block below the panel |
| Esc | leave the field (the value stays unchanged), then return to the editor |

More: [[Tags and properties#Editing in the Properties panel|Editing properties]].

## Selected blocks
After Esc, Shift+arrows, Ctrl+A, Shift+click or dragging with the mouse from block to block, whole blocks are selected together with their children (folded ones too). The selection shows as a single band. Ctrl+click adds a single block or takes it out — also with gaps between the selected blocks.

| Shortcut | Action |
|---|---|
| Shift+↑ / Shift+↓ | grow or shrink the selection (in the journal also past the edge of a day — over several days at once, then only for copying) |
| Ctrl+A | select all blocks |
| Ctrl+C | copy (Markdown; in email and word processors it pastes formatted) |
| Ctrl+X | cut (copy and delete in one undo step) |
| Ctrl+V | replace the selected blocks with what is in the clipboard |
| Delete / Backspace | delete the selected blocks (Ctrl+Z or “Undo” in the notification brings them back selected) |
| Tab / Shift+Tab | indent / outdent all of them (indent only when each can be indented; no gaps in the selection) |
| Alt+↑ / Alt+↓ | move all of them together (no gaps in the selection) |
| Ctrl+Enter | the next task status for all of them (one selected block: only that one, without children) |
| Ctrl+Shift+Enter | task status menu — all of them get the chosen status |
| Ctrl+. | fold / unfold the block at the end of the selection |
| Ctrl+Shift+C | copy a reference `((…))` to the block at the end of the selection |
| Enter | edit the block |
| ↑ / ↓ | select only the previous / next block |
| Esc | clear the selection |

When at least two blocks are selected, a bar appears at the top: Copy, Cut, Delete, Select all and More (Copy as plain text — plain text with bullets, indenting, moving, task status). You copy parts of several paragraphs by selecting whole blocks, or character by character in Markdown source mode (Ctrl+E, *More* → *Mode* → “Source”). More: [[Document and outline#Selecting and copying|Selecting and copying]].

A right click on a block opens the block menu (Copy, Cut, Copy as plain text, Select, Copy link, indenting, moving, deleting) — on a block of the selection it acts on the whole selection. A right click in edited text: Cut, Copy, Paste, Select all. In a web browser Ctrl+Shift+C may open the browser's developer tools — then copy the reference from the block menu or from the command palette (Ctrl+K → “Copy block reference”). More: [[Links and backlinks#Copying a link to a block or heading|Copying a link]].

The task status menu also opens with a right click on the checkbox (on a phone: a long press). More: [[Tasks|Tasks]].

In a document, Backspace at the start of a list item first removes the task checkbox, then the list marker. How the modes work: [[Document and outline|Document and outline]].

## Drawings
In the drawing editor (when you aren't typing text). More: [[Drawings|Drawings]].

| Shortcut | Action |
|---|---|
| P / H / E | pen / highlighter / eraser |
| V | selection (lasso) |
| R / O / D / L / A | rectangle / ellipse / diamond / line / arrow |
| T | text |
| Shift (while dragging) | square or circle; line and arrow in 15° steps |
| Ctrl+A | select all |
| Delete / Backspace | delete the selection |
| Ctrl+D | duplicate the selection |
| ← ↑ → ↓ (with Shift) | move the selection by 1 (by 10) |
| Ctrl+] / Ctrl+[ | the selection one layer up / down |
| Ctrl+Shift+] / Ctrl+Shift+[ | the selection to the front / to the back |
| Ctrl+C / Ctrl+X / Ctrl+V | copy / cut / paste (also from and to excalidraw.com) |
| Ctrl+Shift+← → ↑ ↓ | align the selection left / right / top / bottom |
| Alt+H / Alt+V | distribute the selection evenly horizontally / vertically |
| PgUp / PgDown | the drawing on the previous / next page of the PDF |
| Esc | cancel the shape or lasso being drawn; clear the selection |
| Ctrl+Z / Ctrl+Shift+Z | undo / redo |
| Space (held) | moving the paper |
| Ctrl+0 | show the whole drawing |
| Ctrl++ / Ctrl+- | zoom in / zoom out |

While typing text in a drawing: Enter — a new line, Esc or Ctrl+Enter — finish typing.

## PDF viewer
When a PDF is open. More: [[Document and outline#PDFs|PDFs]].

| Shortcut | Action |
|---|---|
| Ctrl+F | search in the PDF (where search works) |
| Enter / F3 | next match |
| Shift+Enter / Shift+F3 | previous match |
| Ctrl+G | go to a page |
| Ctrl+C | copy the selected text |
| Ctrl+A | select the text of the whole current page |
| Ctrl++ / Ctrl+- | zoom in / zoom out |
| Ctrl+0 | fit width |
| Ctrl + mouse wheel | zoom around the pointer |
| PageDown / Space, PageUp / Shift+Space | a screen down / up |
| ↑ ↓ (← → when zoomed in) | scrolling |
| Home / End | first / last page |
| Esc | clear the selection, close the search, then the viewer |
| Alt+← | back (closes the viewer) |
