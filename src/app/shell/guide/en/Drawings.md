---
tags: [documentation]
aliases: [Drawing, Sketching, Handwritten notes, Sketches, Shapes, Notes on slides, Backdrop]
brulion: document
---
# Drawings

In a note you can draw and write by hand — with a stylus on a tablet, a finger on a phone or the mouse on a computer — and also draw diagrams: rectangles, ellipses, diamonds, lines, arrows, triangles, stars, speech bubbles, clouds and other shapes, plus text. A drawing is an ordinary picture in the notebook, so you can also see it in Obsidian, in a web browser and in a file manager, and Syncthing syncs it like any other file.

## New drawing
Type `/Drawing` in a block (or `/sketch`, `/draw`) and press Enter. On a phone there is also a **Drawing** button in the toolbar above the keyboard, and in the block menu (right click or a long press on the bullet) — **Insert drawing**. Brulion saves an empty drawing `Drawing 2026-10-27 14.32.svg` in the attachments folder, inserts it into the note as `![[Drawing 2026-10-27 14.32.svg]]` and opens it for drawing right away.

A drawing in a note has a pen icon in its corner. Click or tap it to keep drawing. In the view of the picture alone (a link to the file) there is an **Edit drawing** button.

## Tools
- **Pen** (key P) — the line gets thicker when you press the stylus harder. With the mouse and a finger the line gets thinner when you move faster, like real ink.
- **Highlighter** (H) — a wide, semi-transparent line, e.g. for marking parts of lecture notes.
- **Eraser** (E) — removes every line, shape or text it touches (a shape together with the text inside it). The other end of the stylus (the eraser on the stylus) or a pressed stylus button always erases, without switching tools.
- **Select** (V, a lasso or dashed square icon) — with a lasso or a rectangle, see [[#Lasso selection|Lasso selection]] below.
- **Shapes** — rectangle (R), ellipse (O), diamond (D), line (L), arrow (A) and eleven more in the list, see [[#Shapes and arrows|Shapes and arrows]] below.
- **Text** (T) — see [[#Text|Text]] below.
- **Colors and thicknesses** — six pen colors (ink, blue, red, green, orange, violet), five highlighter colors and three thicknesses of each; for text four sizes: small, medium, large and extra large. On a phone they are under the circle with the current color. Brulion remembers the last choice on this device.
- **Paper** — plain, lined or squared (on a phone in the **More** menu).
- **Drawing background** — light or dark, see [[#Light and dark background|Light and dark background]] below (on a phone in the **More** menu).
- **Undo** and **Redo** (`Ctrl+Z`, `Ctrl+Shift+Z`) — undo lines, shapes, text, erasing, every change of a selection (moving, size, rotation, flipping, color, style, copy, grouping, deleting) and changes of paper and background.
- **Show the whole drawing** (`Ctrl+0`) and **Done** — back to the note.

On a phone the toolbar is at the bottom of the screen. When you hold the phone upright, the toolbar has two rows: the tools and the color circle at the top, **Undo**, **Redo**, **More** and **Done** below — all buttons are on the screen.

## Shapes and arrows
The shape button in the toolbar shows the last used shape (the small arrow in its corner marks the list behind it); click it again while the shape is selected and the list unfolds: **Rectangle** (R), **Ellipse** (O), **Diamond** (D), **Line** (L), **Arrow** (A), and below them **Triangle**, **Right triangle**, **Parallelogram**, **Trapezoid**, **Pentagon**, **Hexagon**, **Star**, **Block arrow**, **Speech bubble**, **Heart** and **Cloud**.
- Drag across the paper — the shape grows with the movement of the stylus, finger or mouse. With `Shift` held down a rectangle becomes a square, an ellipse a circle, and a line or arrow turns in 15° steps; the other shapes are then drawn within a square.
- A shape has the pen's color and twice its thickness (fine, medium, thick). Rectangles have rounded corners, as in Excalidraw.
- The tool stays selected, so you can draw several shapes in a row. You pick the pen with the P key.
- The shapes from the lower part of the list are ordinary closed Excalidraw lines in the file, so excalidraw.com and Obsidian show them too and let you edit them. They can have text inside, just like a rectangle (see [[#Text|Text]]): excalidraw.com knows text only inside a rectangle, ellipse, diamond and on arrows, so Brulion saves the text in a triangle or star as text grouped with the shape — in excalidraw.com it shows in the same place and moves with the shape.

## Text
- Choose **Text** (T) and tap the paper — a field for typing appears. `Enter` starts a new line.
- A click next to the field, `Esc`, `Ctrl+Enter` or the ✓ button (on a touch screen) ends typing. Empty text disappears.
- Tap existing text to correct it. Tap the middle of a rectangle, ellipse or diamond to write a label in it: centered and wrapped as in Excalidraw — narrower in an ellipse and a diamond so that it fits inside. The same in a triangle, star, speech bubble and the other shapes in the list: the text goes where the shape is widest (lower down in a triangle, in the “bubble” of a speech bubble). When the text doesn't fit, the shape grows. The label moves, rotates and is deleted together with the shape.
- A field closed without changes changes nothing in the drawing — text from Excalidraw stays exactly as it was.
- With the **Select** tool a double click (or two taps) does the same.
- On a phone the paper moves so that the text you are writing is above the keyboard.

## Lasso selection
Choose **Select** (V) and draw around what you want to select with the stylus, finger or mouse — everything that is at least half inside gets selected. Click **Select** again while it is already chosen (it has the small arrow in its corner too) to pick the way: **Lasso** or **Rectangle selection** (you drag a rectangle instead of drawing around). Brulion remembers the choice on this device. A tap selects a single element, a tap on an empty spot clears the selection, and `Shift` adds to the selection (with the mouse).
- **Drag** inside the selection frame to move it. Arrows attached to the moved shapes follow them.
- A bar appears above the selection: **colors** (change the color), **Style** (see [[#Selection style|Selection style]] below), **Duplicate** (`Ctrl+D`), **Bring to front** (`Ctrl+Shift+]`), **Send to back** (`Ctrl+Shift+[`), **More** and **Delete** (`Delete`). On a phone **Bring to front** and **Send to back** are under **More**.
- **More** also has **Copy** (`Ctrl+C`), **Cut** (`Ctrl+X`), **Rotate by 90°**, **Flip horizontally** (`Shift+H`), **Flip vertically** (`Shift+V`), **Group** (`Ctrl+G`) and **Ungroup** (`Ctrl+Shift+G`). Text stays readable when flipped — only its place changes. A group is selected and moved as a whole.
- Keys: `Ctrl+A` selects everything, the arrows move the selection (with `Shift` ten times further), `Ctrl+]` and `Ctrl+[` move it one layer up and down, `Ctrl+Shift+←`, `→`, `↑`, `↓` align left, right, top and bottom, `Alt+H` and `Alt+V` distribute evenly horizontally and vertically, `Esc` clears the selection.
- Groups from Excalidraw are selected as a whole. Elements locked in Excalidraw — and text in locked shapes — can't be selected, erased or changed.
- When **Draw with finger** is off, a finger only moves the paper — a hand resting on the selection doesn't move it.

## Resizing and rotating
A selection has handles on its frame — drag them with the stylus, the mouse or a finger (with **Draw with finger** on):
- **Squares in the corners and in the middle of the sides** change the size, and the opposite corner or side stays in place. `Shift` keeps the proportions. The line thickness doesn't change.
- **The circle above the frame** (below the frame when there is no room above it) rotates the selection; with `Shift` in 15° steps. The frame of a single element rotates with it.
- Pictures and text keep their proportions (a picture loses them with `Shift`), and bigger text simply has bigger letters. Several selected elements scale together from the corners of the frame, always keeping their proportions.
- Text in a shape wraps anew, and the shape doesn't get smaller than needed to hold it. Arrows attached to the shape follow it.
- `Esc` while dragging cancels the change.

## Selection style
The **Style** button in the bar above the selection shows what the selected elements have:
- **Fill** and **Fill pattern** (solid, hatched, cross-hatched) — shapes;
- **Line thickness** (fine, medium, thick) — everything drawn with a line;
- **Line style** (solid, dashed, dotted) — shapes, lines and arrows;
- **Corners** (sharp, rounded) — rectangles, diamonds, and on lines and arrows a gentle curve instead of sharp bends;
- **Line start** and **Line end** (none, arrow, triangle, circle, bar, diamond) — lines and arrows;
- **Text size**;
- **Align** — when at least two things are selected: left, center horizontally, right, top, center vertically, bottom; with three or more also **Distribute horizontally** and **Distribute vertically** (the outermost stay in place, the others get equal gaps). A group, a shape with text and every other element move as a whole, by the frame they take up — a rotated element by its rotated outline. Arrows attached to shapes follow them;
- **Opacity** (100, 75, 50, 25 %).

Each choice is one undo step. The panel stays open, so you can change several things in a row.

## Light and dark background
The **Drawing background** button (sun with moon, sun or moon in the toolbar) has three options:
- **Like the app** (the default) — the drawing is light in Brulion's light theme and dark in the dark one, and it changes right away when you switch the theme (also in the evening, when the theme follows the system);
- **Light** — always a white sheet (or one in the chosen background color);
- **Dark** — always a dark sheet, also in the light theme.

The choice is saved in the drawing file, so every device shows it the same way. It works in the drawing editor, in the preview in a note and in the view of the picture alone.

On a dark sheet the colors are adjusted just as in Excalidraw's dark mode: ink is light grey, blue, red and violet are lighter, paper lines darker. The highlighter keeps its color, only a bit stronger, so that it is visible on the dark background and the writing under it stays readable. The colors in the file don't change: switching back to the light background shows exactly what you drew. The color circles in the toolbar show the colors as they look on the current sheet.

**Your own background color.** In the same menu, **Background colour…** (on a phone under **More**) lets you pick the sheet's color from a grid or type any color, e.g. `#fff9db`; white brings back the ordinary sheet. The preview shows ink on that color, and when the ink would be hard to see, the dialog warns you — lines already drawn don't change color, but you can choose another pen color. Paper lines and squares adapt to the color of the sheet. On a dark sheet (**Dark**, or **Like the app** in the dark theme) the background color changes just like the other colors, e.g. cream becomes dark brown; to see exactly the chosen color, set **Light**. The background color is saved in the drawing and can be undone; Excalidraw and Obsidian see it too.

In other apps:
- a drawing with the **Dark** background is dark everywhere;
- a drawing with the **Like the app** background is dark in Obsidian with a dark theme (on a computer even when the system is light), and in web browsers and usually on GitHub too when they use a dark theme — the file contains a description of the dark colors for browsers (`prefers-color-scheme`). An app that doesn't understand it shows a light drawing;
- **excalidraw.com** shows the drawing in its own theme, regardless of this choice.

## Notes on slides and PDFs
A drawing can lie on top of a picture, a slide exported as a picture or a PDF page — then you write straight on the lecture slides. Such a picture under a drawing is the **backdrop**.

**Inserting from a note.** Type `/Drawing on a picture or PDF…` in a block (or, shorter, `/slide`, `/pdf`, `/backdrop`, `/lecture`) or choose **Insert a drawing on a picture or PDF…** in the block menu, and pick a file:
- a **picture** (a photo of a whiteboard, a slide saved as PNG or JPEG) — one drawing “slide (drawing).svg” is created and opens for drawing right away; it starts with a **light** background, because the photo has its own colors;
- a **PDF** — a dialog opens with the file's pages as thumbnails. Tap the pages you want (a second tap unselects them), choose **All** or **None**, or type them in the **Pages** field, e.g. `1-3, 7` (the thumbnails and the field change together). From the keyboard: `Tab` goes to the thumbnails, the arrows move between them, `Space` selects a page. **Insert drawings (N)** creates one drawing per page — “lecture 3 p. 4.svg” — the first at the cursor, the others in separate blocks below it, in order. You can insert at most 60 pages at once. Preparing the pages takes a moment (a progress bar, “Page 3 of 12…”); **Cancel** stops it. A single drawing opens right away; several stay in the note. A single `Ctrl+Z` undoes the whole insertion (the drawing files stay in the attachments, as with `/Drawing`).

**Draw on this page** in the PDF viewer (see [[Document and outline#PDFs|PDFs]]) makes a drawing on the page you are reading — “lecture 3 p. 4.svg” — and opens it right away. The drawing goes into the note the PDF was opened from, as a separate block right below the block with the PDF (even if the PDF has moved in the note in the meantime); a PDF opened from the tree or the quick switcher, without a note, adds the drawing to today's journal (Brulion tells you so). **Back** from the drawing editor returns to the viewer on the same page, and another **Back** — to the note, where the drawing already sits below the PDF. When that note already has a drawing on this page, the button first offers to open it (**Open the drawing “…”**) or to make a **New drawing on this page** — no second file gets created by accident.

A file from outside the notebook is copied by Brulion to the attachments folder, like pictures. A file that is already in the notebook (e.g. a PDF synced into the lecture's folder) stays where it is — on Android too, when you pick it in the phone's storage, on a memory card or in Downloads — and the same file picked a second time isn't copied again. If no drawing is created from the chosen file (you cancel the page choice, the PDF is password-protected or damaged, the picture can't be shown), the copied file is removed from the attachments.

**Backdrop in the drawing editor.** In the **Paper** menu (on a phone under **More**):
- **Backdrop from a picture or PDF…** — puts a picture or a PDF page under an existing drawing (or changes the backdrop that is already there);
- **Another PDF page…** — another page of the same PDF (when the backdrop is a PDF page); if the file is no longer in the notebook (moved, deleted), Brulion says so — the drawing still contains the page's picture, though, and keeps looking the same;
- **Previous PDF page** and **Next PDF page** (`PgUp`, `PgDown`) — go to the drawing on the neighboring page of the same PDF, without going back to the note. Brulion looks for it in the note that holds the current drawing: if the note already has a drawing on that page, it opens it; if not, it makes a new one (“lecture 3 p. 5.svg”) and inserts it into the note in a separate block right after the current drawing (next page) or right before it (previous page), so the drawings stand in order in the note. The current drawing is saved first, and **Back** returns to the note. For a drawing that isn't in any note, the new one is added to today's journal;
- **Remove backdrop** — leaves only the writing.

You can undo every change of the backdrop. The backdrop is locked: the eraser doesn't erase it, the lasso and `Ctrl+A` don't select it, and **Send to back** puts elements just above it.

**Dark background.** On a dark sheet the backdrop darkens too — like the night mode of a PDF reader: a white page becomes dark grey, black text light — so light writing stays easy to see. A drawing on a PDF page starts with the **Like the app** background (it darkens together with Brulion), a drawing on a picture with **Light**; you can change both with the **Drawing background** button. A drawing with the **Dark** background has a dark backdrop in Obsidian and in a web browser too.

**Where PDF works.** You insert PDF pages — from the page picker and with the **Draw on this page** button in the PDF viewer — in the app for Android, Windows and Linux (the Nix package and the AppImage). The browser version has no PDFs (the browser build of Qt can't draw them; there a PDF opens in a new tab) — there a backdrop can be a picture, and drawings on PDF pages made on another device look and edit normally, because the page's picture is inside the drawing file.

**In the file.** The backdrop picture is inside the drawing itself (at most 2400 pixels on the longer side and 4 MB), so the drawing looks the same on every device, in Obsidian, in a web browser and in excalidraw.com — there the backdrop is an ordinary locked picture at the bottom. The picture is in the file twice (in the scene and in the picture itself), so a drawing with a slide usually weighs between 0.5 and 1.7 MB.

## Copy and paste (also with excalidraw.com)
You copy a selection with `Ctrl+C` (or **Copy** under **More** above the selection), cut it with `Ctrl+X`, and paste with `Ctrl+V` or **Paste** in the **Paper** menu (on a phone under **More**). Pasted elements appear under the mouse pointer (or in the middle of the view), already selected — you can move them; a single `Ctrl+Z` undoes the whole paste.

Brulion copies in the same format as Excalidraw, so it works both ways: elements copied in Brulion paste into **excalidraw.com** (and into another Brulion drawing), and elements copied in excalidraw.com paste into Brulion. You can also paste the whole content of an `.excalidraw` file copied as text. Brulion doesn't draw Excalidraw's frames and embedded web pages — when pasting, it skips them and tells you how many there were. Plain text from the clipboard is pasted as new text in the drawing.

## Stylus, finger and mouse
- The **stylus** draws. **Fingers** then don't draw, they only move the paper (one finger) and zoom it (two fingers) — so you can rest your hand on the screen. While the stylus touches the screen, your hand changes nothing: it doesn't move the paper and doesn't break the line.
- On a phone without a stylus **the finger draws**, and you move and zoom the paper with two fingers. When Brulion sees a stylus for the first time, it switches to drawing with the stylus. You choose this yourself with the **Draw with finger** switch.
- In the browser version the stylus works the same way — with pressure and the other end as an eraser — as long as the browser passes the stylus pressure on to the page (Chrome and Edge, among others, do).
- With the **mouse** you draw holding the left button. You move the paper with the middle or right button, with the wheel or holding the space bar, and zoom with `Ctrl` and the wheel or a touchpad gesture.

## Saving and sync
You don't need to save anything: Brulion saves the drawing a moment after every line, and also when you leave the drawing or the app. The note shows the new look right away.

When the same drawing changes on another device (Syncthing, Nextcloud) while you are drawing it, Brulion merges both versions: what was added here and there stays, what was moved or changed here wins, and what was erased disappears. The line you are drawing right now stays too.

If someone on another device adds a frame or an embedded web page to the drawing in excalidraw.com, Brulion shows that version as it is, and saves what was drawn here and not yet saved next to it in a new file “… (this device).svg” (or `.excalidraw`, `.excalidraw.md` — in the drawing's format).

When a drawing changes on two devices before they have synced, **Nextcloud merges it by itself**: what was added on both stays, and what was erased disappears. A drawing is never joined line by line, so it can't be broken that way.

When **Syncthing** leaves a conflict copy of a drawing (`Drawing.sync-conflict-….svg`) — or Nextcloud couldn't merge a drawing by itself, for example one with a frame from excalidraw.com — in **Sync center → Conflicts** you see both versions and the merged result, with the number of elements in each. **Merge drawings** combines the lines and shapes of both versions: your version stays as it was, and what is only in the copy is added. Lines erased on just one device come back — erase them again. The previous version of the drawing stays in the version history (even a very large one, with pasted photos), and the copy goes to the trash, so everything can be undone. A drawing Brulion doesn't merge (with a frame or an embedded page) you keep in one of its versions: **Keep mine** or **Keep the copy**.

## Drawings and other apps
A drawing file is an SVG picture with an Excalidraw scene hidden inside:
- **Obsidian**, **Logseq**, web browsers and file managers show it as an ordinary picture;
- **excalidraw.com** opens it for editing (drag the file onto the page) — shapes, arrows and text from Brulion are ordinary Excalidraw elements there;
- a drawing from Excalidraw exported as SVG with the **Embed scene** option can be opened by Brulion too, and you can keep editing it: handwriting, shapes, arrows, text and pasted pictures. Brulion draws them smoothly — Excalidraw's “hand-drawn” look stays in the file, but in Brulion the strokes are straight — and shows text in its own font, so it may stick out of its box a little. Brulion doesn't re-route elbow arrows, it only moves them;
- *canvas* drawings from **Trilium** are saved by the import as the same kind of SVG files: the picture drawn by Trilium with the Excalidraw scene inside and the pictures pasted into the drawing — you open them in the drawing editor like any other drawing (see [[Import and export|Import and export]]);
- a drawing with **frames** or **embedded web pages** is only shown by Brulion (you change it in excalidraw.com), so that nothing gets lost. In the dark theme such a drawing darkens too, as in Excalidraw's dark mode, and the photos and emoji pasted into it keep their colors.

**`.excalidraw`** files (excalidraw.com: “Save to disk”, the Excalidraw plugins for Logseq and Obsidian in compatibility mode) are also opened and edited by Brulion — it keeps saving them as `.excalidraw`.

## Drawings from Obsidian (Excalidraw)
Drawings of the **Excalidraw plugin for Obsidian** are `*.excalidraw.md` files (for example `Drawing 2026-10-28 14.32.05.excalidraw.md`) — notes whose header (front matter) has the `excalidraw-plugin` key. Brulion recognizes them by that key, even when the name doesn't end in `.excalidraw.md`.
- **In a note** they show as a drawing: `![[Drawing.excalidraw]]`, `![[Drawing.excalidraw.md]]`, and also `![[Drawing]]` when that page is a drawing. A click opens the drawing editor.
- **From the page list, a link or search** such a file opens as the drawing's picture with an “Edit drawing” button. “Show as text” shows its Markdown (read-only) — the notes “behind” the drawing aren't edited in Brulion, only in Obsidian.
- **Editing:** handwriting, shapes, arrows, text and selection work just as in Brulion's drawings.
- **Saving in the same format:** Brulion changes only the `## Drawing` block with the scene (compressed — `compressed-json` — stays compressed, `json` stays JSON) and the “Text Elements” entries of texts you changed or deleted (text that was only moved, recolored or moved together with its shape keeps its links). The front matter, the notes behind the drawing, element links (“Element Links”), embedded files (“Embedded Files”) and `%%` markers stay byte for byte. Obsidian keeps opening the drawing, and what you erased in Brulion disappears in Obsidian too, even if it has the drawing open.
- **Pictures pasted in Obsidian** (the “Embedded Files” section, e.g. `Pasted Image ….png`) are shown by Brulion — in the editor and in the preview in a note — by looking for them the way Obsidian looks up links in that drawing. The picture stays in its own file; the drawing only has a link to it, as in Obsidian. LaTeX formulas and addresses from that section, and a picture that isn't in the notebook, are grey rectangles — you can move and delete them.
- **Export:** in export to HTML (site and document) and to Pandoc such a drawing is a picture — inserted into a note, pointed to by a link `![[Drawing.excalidraw]]` and on its own page — not the plugin's text. Brulion takes the plugin's export lying next to it (`Drawing.excalidraw.svg` or `.png`), and when there is none, it makes an SVG picture from the drawing by itself (with the pasted pictures).
- **Limitations:** a link in text changed in Brulion becomes plain text. A drawing with **frames** or **embedded pages** is only shown by Brulion (you change it in Obsidian); its preview is the plugin's export next to it (`Drawing.excalidraw.svg` or `.png`), and when there is none — the drawing with dashed frames in place of those elements. A drawing in a very old format of the plugin (without a code block) just needs to be opened in Obsidian once.
- **Sync:** when such a drawing changes on two devices at once, Nextcloud merges it element by element, like Brulion's drawings, and never joins it line by line. The notes behind the drawing and the front matter are taken from the version in which they were changed; when they were changed on both devices, Nextcloud keeps both versions (a conflict copy).
- **A conflict copy with different notes:** when a copy of such a drawing (from Syncthing or Nextcloud) has different notes than your drawing — front matter, notes behind it, links, embedded files — the Sync center never calls it “the same” or “older”. **Merge drawings** combines the elements of both versions in your drawing (with your notes), and the copy stays where it is. The center then shows “Only the notes around the drawing differ” and a comparison of the notes of both versions: move what you need over in Obsidian, and only then **Remove the copy**. When the drawing is open in Brulion, the version from another device is merged with yours stroke by stroke.
- Brulion always creates new drawings as SVG.

See also [[File format|File format]] and [[Links and backlinks|Links and backlinks]].
