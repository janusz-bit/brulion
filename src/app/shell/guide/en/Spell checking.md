---
tags: [documentation]
brulion: document
---
# Spell checking

Brulion underlines in red the words its dictionary doesn't know — in English and in Polish, also both languages at once. It checks the text of the block you are editing; the rest of the page stays clean, so nothing distracts you while reading your notes.

## Correcting
- **On a computer** right-click the underlined word. At the top of the menu are the suggestions — a click inserts the corrected word (Ctrl+Z undoes it). Below them:
  - **Add “…” to dictionary** — the word goes into your personal dictionary and isn't underlined any more,
  - **Ignore** — the word isn't underlined until you close Brulion.
- **On phones and tablets** tap the underlined word — the same suggestions appear.

The word you are typing right now isn't underlined until you finish it.

## What is checked
Only ordinary text. Brulion skips:
- `inline code`, code blocks, calculations (`calc`) and queries,
- `[[Page]]` links, block references, web and email addresses (the text of a link `[description](address)` is checked),
- `#tags`, dates, reminders, recurrence and task priorities (📅 ⏰ 🔁 ⏫…),
- formulas `$…$`, property names `key::`, the words `TODO`/`DONE`, comments `%%…%%`,
- numbers and words with digits (`mp3`, `COVID-19`), abbreviations in capitals (`PDF`, `NASA`), names like `iPhone`, file names and paths.

Words with a hyphen (`well-known`) and with an apostrophe (`don't`) are checked as a whole or in parts. An abbreviation with a period (`e.g.`, `np.`) is correct if the dictionary knows it with the period.

## Settings
**Settings → Editor**:
- **Check spelling** — turns underlining on and off.
- **Spelling languages** — *Automatic* (the app's language, the system's language and English: e.g. with a Polish interface, Polish and English), *English*, *Polish* or *Both*. A word is correct if any of the chosen languages knows it.
- **Personal dictionary** — the list of added words: add or remove them here.

The personal dictionary is a setting of this device and covers all notebooks. It isn't saved in the notebook, so it doesn't go through sync — on another device you add words separately.

A word added as `London` is also correct as `LONDON`, but not as `london`. A word added in lowercase (`brulion`) is also correct at the start of a sentence (`Brulion`).

## On different devices
- **Computer (Linux, Windows)** — Brulion uses Hunspell with the English and Polish dictionaries that come with the app (AppImage, Windows, Nix package). Other builds of Brulion use the system's dictionaries (e.g. the `hunspell-en-us` and `hunspell-pl` packages). Without any dictionary, the spell checking settings are hidden.
- **Android** — Brulion asks the system spell checker (the same one the keyboard uses). If it is turned off in Android's settings (Settings → System → Language → Spell checker), Brulion underlines nothing, and its spelling settings are hidden.
- **Browser** — the web version doesn't check spelling.
