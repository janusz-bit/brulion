---
tags: [documentation]
brulion: document
---
# Tags and properties

## Tags
A tag starts with `#`: `#project`, `#project/home` (nested), `#[[to read]]` (with spaces). A tag can contain letters (accented ones too), digits, `_`, `-` and `/`, but not digits alone (`#1` isn't a tag). A heading `# Title` isn't a tag either. Letter case doesn't matter.

Tags work anywhere in a note, and also in the page's front matter: `tags: [documentation, help]`. The **Tags** view in the sidebar lists all tags with the number of uses. The `tag:project` filter in [[Queries|queries]] also catches `project/home`.

## A tag is a link to a page
A tag names a page, as in Logseq: clicking `#work` opens the page “work”. If it doesn't exist yet, it opens empty and is created once you write something — a nested tag `#work/reports` goes into the folder `work`, and a tag with a date (`#2026-10-13`) opens that journal day. A tag finds its page just like a [[Links and backlinks|link]] written in the notebook's top folder: by its full path (`#Projects/Home`), by name (with several pages of the same name — the one with the shortest path) or by alias. Letter case doesn't count, accents do: `#cafe` isn't `Café`.

- A **nested tag** is a single label, but each of its parts can be clicked separately: in `#work/reports`, clicking `#work` opens the tag `work`, and clicking `reports` the whole tag `work/reports`.
- **Backlinks** below a page show the blocks with its tag next to the blocks that link to it, and pages tagged as a whole (in the front matter) as “whole page”. Subtags (`#work/reports`) are separate pages.
- The **tag overview** — everything that has the tag, subtags included — is under the **#** button next to the page title (“Tag overview”) and in the **Tags** view (the button at the end of a row; the row itself opens the page). “Tasks with this tag” is in the page menu **⋯**, and subtags below the page title.
- **Renaming a page** also fixes the tags that name it (`#old` → `#New` or `#[[New name]]`), also in `tags::` and in the page's front matter; subtags stay unchanged. So do the filters `tag:old` and `#old` in [[Queries|queries]] (`tag:old/x` stays).
- After you type `#`, the suggestions show tags and pages; a name with a space or characters that can't be in a tag is written by Brulion as `#[[John Smith]]`.
- **Tag menu** — a right click on a tag (on a phone, a long press): “Open tag page”, “Open in side panel”, “Tag overview”, “Tasks with this tag”, “Copy tag”.
- In the **Tasks** view clicking a tag filters the list; Ctrl+click opens its page, and a right click (on a phone, a long press) opens the tag menu with “Filter by tag” at the top.

This page has the tag #documentation — click it to see its page, and below it everything tagged with it.

## Page properties
At the very start of a file there can be a YAML header (front matter):

```
---
tags: [project]
aliases: [Renovation]
status: in progress
brulion: outline
---
```

- `tags` — tags of the whole page,
- `aliases` — extra names under which the page answers to links,
- `brulion` — the note's mode ([[Document and outline|document or outline]]),
- `icon`, `banner`, `banner_y` — [[#Page icon and cover|page icon and cover]],
- any keys of your own, e.g. `status` — Brulion shows them in the **Properties** panel above the content.

Other apps see the same entries as ordinary properties. Instead of the YAML header you can also start a page with `key:: value` lines (Logseq style).

### Editing in the Properties panel
You change properties without writing YAML: click the **Properties** header above the content to expand the panel (it stays expanded until you collapse it). Every property gets a field according to the type of its value:

- **text** — click the value to change it; `[[page]]` links and `#tags` in the text work. Type `[[` and Brulion suggests pages,
- **number** — also accepts “3,5” and “1 200” (and writes `3.5` and `1200`),
- **checkbox** — `true` or `false`, a single click,
- **date** and **date and time** — a dialog with a calendar; you can type “tomorrow”, “friday 9:00”,
- **list**, **tags** and **aliases** — chips; “+” adds another (Enter or a comma), Backspace in an empty field removes the last one. On a computer a chip is removed with its ×, on a phone with its menu: long-press the chip (an ordinary list item or alias just needs a tap) and choose **Remove** — like the chips below the page title. A tag and a `[[page]]` open their page when tapped.

You don't need to set the type: Brulion recognizes it from the value, and an empty value gets the type this key has most often on other pages of the notebook.

You add a new property with the **Add property** button at the bottom of the panel, in the page menu or with **Ctrl+;** (also on a page that has no properties yet). The suggestions show the keys used across the notebook and on how many pages. You can't add a key the page already has a second time (not even written in different letter case) — Brulion says such a property already exists, and its value stays unchanged. The type icon next to a property (or a right click, on a phone a long press) opens a menu: **Type** (changing the type — when the value can't be converted, it is cleared and you can undo that), **Rename**, **Copy value** and **Delete**. You can undo every change (Ctrl+Z or “Undo” in the message).

Keyboard in the panel: the ↑ and ↓ arrows move between properties, → or Enter changes the value, ← or F2 renames, Delete deletes, Alt+↓ goes to the first block, Esc returns to the editor.

Brulion changes only that one property — the rest of the header (comments, order, quotes, line endings) stays as it was. This works the same in a YAML header as in `key:: value` lines; in the latter a key can contain only the letters a–z, digits, `-` and `_`. A header that can't be safely changed in fields (for example an indented one) is shown read-only — you change it with the **Edit as text** button. The menu of a property the fields can't handle (for example a nested map) has the same command. A value opened in a field and left unchanged is saved exactly as it was.

### Tags and aliases in the page header
A page's tags and aliases are also shown below its title. A right click on a chip (on a phone, a long press) opens a menu: you can open a tag or remove it from this page, and copy or remove an alias. The “+” button at the end adds a tag or alias without opening the panel. While the **Properties** panel is expanded, these chips below the title aren't shown — the same tags and aliases (with “+”, on a computer also with ×, on a phone with the same chip menu) are in the panel right below; once you collapse the panel, they come back.

## Page icon and cover
A page can have an **icon** — an emoji or one of the app's icons — and a **cover**, i.e. a picture at the top of the page. You set them in the page menu **⋯**: **Add icon…** and **Add cover…** (later “Change…”). You can also change the icon by clicking it next to the title.

- **Icon**: type or paste any emoji (on a phone from the emoji keyboard), pick one of the popular ones or an app icon. It shows before the title, in the page tree in the sidebar, in favorites and recent pages, in **All pages**, in the quick switcher (Ctrl+O) and before every link to this page.
- **Cover**: pick a picture from the notebook (thumbnails with a search by name), **From file…** (the picture goes to the attachments folder) or give the address of a picture on the internet. Brulion downloads a picture from the internet only when you click **Show** on the cover (or when you allow such pictures without asking in the settings), and keeps it on this device. The **Reposition** button on the cover lets you drag the picture up or down so that its more important part shows; **Save position** remembers that.

Both are ordinary page properties, so they travel with the file to other devices and to other apps — in the keys read by the Iconize and Banners plugins for Obsidian and by Logseq:

```
---
icon: 🚀
banner: "![[mountains.jpg]]"
banner_y: 0.3
---
```

An app icon is stored as in Iconize, e.g. `icon: LiBookOpen`. Removing the icon or the cover in its dialog removes these keys (for the cover together with `banner_y`); Brulion leaves the rest of the header unchanged. You can undo every change.

## Block properties
In Logseq/Dataview style: a `key:: value` line right inside a block.

Estimated cost:: $1200

A list of values written as links or tags (`author:: [[John]], [[Anna]]`) is understood by Brulion as a list. Numeric page properties can also be used as variables in [[Calculations|calculations]].

## Searching by properties
Property values are part of the block's text, so you find them with an ordinary search, and in queries with a text filter.
