# Notebook settings in a note, and the layout as text

Design (TODO.md "Notebook settings in notes", "A small language for the interface layout"). What is
built is marked **(done)**; the rest is the plan the implementation follows, in this order.

## Why

The notebook's own settings live in `.brulion/config.json` (docs/SYNTAX.md §1): synced with the
notebook, but hidden — no editor, no history the user sees, no comment saying why a value is what it
is, and a JSON syntax error loses the whole file. The interface's layout (the icon bar, the pages
panel's sections, the "More" menu, the tabs, the bottom navigation) is set only in Settings →
Appearance → Layout, per device, as a handful of switches and orders.

A note is what Brulion is good at: it is edited like any other, kept in History, synced, searched,
linked from the journal ("changed the week to start on Sunday, see [[Notebook settings]]").

## The settings note

* **Where**: the page `Notebook settings.md` at the notebook's root — in a Polish notebook
  `Ustawienia notatnika.md`; both names are read, the first one found in that order wins, and a page
  of that name anywhere else is an ordinary page. Its frontmatter says `brulion: settings`, so a page
  renamed by hand is still found (the index's frontmatter, not a scan of files).
* **Format**: the frontmatter holds exactly the keys of `config.json` (docs/SYNTAX.md §1:
  `journalsFolder`, `journalFileFormat`, … `flashcards` as a nested mapping, `themePage`, `agentHosts`
  …), YAML instead of JSON, read by the same parser as every page's properties
  (`markdown/frontmatter.h`) and checked by the same rules as config.json (`parseVaultConfig()` over a
  QJsonObject made of the YAML mapping: one code path, one set of warnings). The page's text is free:
  the template written by the migration explains each key in the notebook's language, and the user
  may write anything else there (it is never read for settings — only the ` ```brulion-layout `
  block below).

  ```markdown
  ---
  brulion: settings
  journalsFolder: Dziennik
  journalFileFormat: yyyy-MM-dd
  weekStartsOn: 1
  defaultReminderTime: "08:30"
  flashcards:
    tags: [fiszki]
  ---
  # Ustawienia notatnika

  Ustawienia, które dzielą wszystkie urządzenia tego notatnika. …
  ```

* **Precedence**: config.json first, the note's keys over it, key by key — so a note holding only
  `weekStartsOn` changes only that. A key the note has with an invalid value falls back to
  config.json's (a warning names the note and the key), never to the default: a typo in the note
  cannot reset the notebook. Unknown keys are kept in both (a newer Brulion's keys survive an older
  one's save).
* **Writing** (Settings → Notebook, `b.app.set`, the CLI): while the note exists, a changed key is
  written into its frontmatter in place (`setFrontmatterValue()`: the other keys, comments and text
  untouched, its line endings kept) **and** into config.json, so a Brulion older than this design —
  which reads only config.json — keeps seeing the same settings. Without the note: config.json only,
  as today.
* **Migration** ("Move the settings into a note", Settings → Notebook): makes the note from
  config.json's current values (only the keys that differ from the defaults, plus a comment line for
  each key the template explains). **Idempotent**: run again, or after a crash half-way (the note
  written, config.json not touched — it never is by the migration), it finds the note and only adds
  keys config.json has that the note lacks; a key both have keeps the note's value. Twice in a row
  writes nothing the second time. A notebook taken over from Obsidian (no config.json, settings
  adopted from `.obsidian/`) migrates the adopted values the same way.
* **Removing the note** (to the trash): config.json still holds the same values (every write went to
  both), so nothing changes but where they are edited.
* **What stays a device setting** (AppSettings, never in the note): everything that is about this
  device or about trust — the theme and fonts, the editor's switches, the layout's own choice of
  following the notebook (below), sync accounts and folders, AI keys and providers, the app lock,
  backups, notifications, the remote control, links out of the notebook, Code mode's projects (paths
  of this device). The rule of `b.app.settings()` (docs/EXTENSIONS.md): `scope = "notebook"` keys
  only.
* **Safety**: the note comes from anywhere (sync, imports): its values are data, checked as
  config.json's are; a folder key never leaves the notebook; an unknown key is never executed or
  shown as markup.

## The layout language

A text describing what the interface shows and in what order, in the style of Typst's function
calls: one call a place, its arguments the items in order, `|` a separator (a group), `-name` an
item hidden where it would be by default.

```brulion-layout
#rail(journal, pages, tasks, calendar, search, |, code)
#panel(favorites, recent, pages, tags, -outline)
#more(gym, habits, flashcards, |, settings)
#bottom(journal, tasks, capture, search, more)
#tabs(on)
```

* **Places**: `rail` (the icon bar), `panel` (the pages panel's sections — today's "order of the
  panel's sections" and hidden ones), `more` (the "More" menu), `bottom` (the phone's bottom
  navigation: at most five), `tabs` (`on` / `off`). A place left out keeps the app's own layout.
* **Items**: the ids the app already has for them (the rail's `sidebarNav_<id>`, the panel's section
  ids, the bottom navigation's buttons — `AppSettings.bottomNav`), plus `|`; an unknown item, a
  duplicate, a sixth bottom button are left out with a warning, never an error that stops the rest.
* **Where it is written**: in a ` ```brulion-layout ` block — of the settings note (the notebook's
  layout, every device that follows it), or of Settings → Appearance → Layout → "Edit as text" (this
  device's, kept in its settings). Settings → Appearance → Layout's switches and orders stay: they
  edit the same layout and write it back as text (one model, two editors).
* **Which applies**: this device's layout text when it has one, else the notebook's when the device
  follows it (Settings → Appearance → Layout → "Use the notebook's layout"; off by default — a phone
  and a desktop rarely want the same bottom bar), else the app's.
* **Parsing**: a pure core function (text in → `LayoutSpec` + warnings out), no evaluation — the
  calls are a fixed set, the arguments names; the editor shows the warnings under the block.

## Order of work

1. **(done)** Core: `settingsnote` (`src/core/vault/settingsnote.h`, `tst_settingsnote`) — the note found (names, `brulion: settings`), its frontmatter → QJsonObject
   → `parseVaultConfig()` merged over config.json's, the migration (pure: config.json + the note's
   text → the note's new text, idempotent), the write of one key. Tests with Polish text, CRLF,
   malformed YAML, a note with only some keys.
2. **(done)** Vault: the note read with config.json (`Vault::settingsNote()`; `configChanged()` when
   either changes, the note made, renamed or deleted), `saveConfig()` writing the note's changed keys
   in place and config.json whole (`tst_vault` settingsNote…).
3. Settings → Notebook: "Move the settings into a note", "Open the settings note"; guide en/pl.
4. Core `layoutspec`: the language's parser and printer (round trip), warnings.
5. The app's layout from a `LayoutSpec` (rail, panel, more, bottom, tabs), "Edit as text", "Use the
   notebook's layout".
