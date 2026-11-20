# Web Clipper — web pages into the notebook

The Brulion Web Clipper is a browser extension that saves what you read into your notebook as
Markdown, pictures included: the **article** of a page (its readable text, without menus and
adverts), the **selection**, the **whole page**, a **screenshot** of the visible part, or only the
**link**. A clip goes to a **new page** in a folder, into **today's journal**, or **under a heading** of
a page you choose ("## To read"), filled into a **template** (Brulion's own, in the style of
Obsidian's Web Clipper or Joplin, a page of your templates folder, or your own text).

One code base for **Chrome, Chromium, Brave, Edge** (Manifest V3, version 121 or newer) and
**Firefox** (140 or newer), in the browser's language: Polish or English.

There is no server and no account. The extension hands the clip to **`brulion-cli`** on the same
computer — the browser starts it as a *native messaging host* — which converts the HTML with
Brulion's own converter, saves the pictures into the notebook's attachments and writes the note the
way the command line writes every note (the notebook's lock, compare-and-swap, the page's history).
Nothing leaves the computer except the requests for the clip's pictures, to the sites they come from.

* [Installing](#installing): the extension, then `brulion-cli clip install` once.
* [Clipping](#clipping): the popup, context menus, shortcuts.
* [What a clip becomes](#what-a-clip-becomes), [pictures](#pictures), [templates](#templates),
  [options](#options).
* [Without the host](#without-the-host-phone-browser-version): the phone and the browser version.
* [The host](#the-host): messages, limits, security; [privacy](#privacy); [packages](#packages);
  [tests](#tests); [troubleshooting](#troubleshooting).

## Installing

### 1. The extension

The extension is not in the browsers' stores yet. Get `brulion-clipper-<version>-chrome.zip` and
`brulion-clipper-<version>-firefox.xpi` from a release (or make them: [Packages](#packages)), or use
the `clipper/` folder of the source code.

* **Chrome, Chromium, Brave, Edge**: unzip the ZIP into a folder that stays, open
  `chrome://extensions` (Edge: `edge://extensions`, Brave: `brave://extensions`), turn on
  **Developer mode**, **Load unpacked** → the folder. The extension's id is always
  `calcbkckojhmdfecpcohaangneahmkgg` (its manifest's `key` fixes it), the id the host lets in.
* **Firefox**: `about:debugging` → **This Firefox** → **Load Temporary Add-on…** → the `.xpi`
  (or `clipper/manifest.json`); it stays until Firefox closes. To keep it: Firefox Developer
  Edition, Nightly or ESR with `xpinstall.signatures.required` set to `false` in `about:config`,
  then **Install Add-on From File…** in `about:addons`. Release Firefox installs only signed
  add-ons. Its id is `brulion-clipper@januszbit.github.io`.

### 2. The connection: Settings or `brulion-cli clip install`

In the app: **Settings → Application → Browser extension (Web Clipper) → Connect browsers** — each
browser's row says whether it is connected (the same as `clip install` below, for your user;
**Disconnect** removes them again). In a terminal:

```sh
brulion-cli clip install                  # every browser found for you
brulion-cli clip install --browser firefox,chromium
brulion-cli clip status                   # which browsers can reach brulion-cli
brulion-cli clip uninstall
```

`clip install` writes a *host manifest* — a small JSON file naming the program the browser starts
(`brulion-cli`), the host's name `io.github.januszbit.brulion` and the extension allowed to start
it — where each browser looks for it. Without `--browser` it connects the browsers it finds for you
(their profile folder exists; `--all` connects all five); `--extension-id <id>` allows another
Chromium id too (a copy from a store). `--system` writes the system's folders, for every user
(administrator rights). Restart nothing: the browser reads the manifest at each clip.

| Browser | Linux (you) | Linux (`--system`) |
|---|---|---|
| Google Chrome | `~/.config/google-chrome/NativeMessagingHosts/` | `/etc/opt/chrome/native-messaging-hosts/` |
| Chromium | `~/.config/chromium/NativeMessagingHosts/` | `/etc/chromium/native-messaging-hosts/` |
| Brave | `~/.config/BraveSoftware/Brave-Browser/NativeMessagingHosts/` | `/etc/opt/brave/native-messaging-hosts/` |
| Microsoft Edge | `~/.config/microsoft-edge/NativeMessagingHosts/` | `/etc/opt/edge/native-messaging-hosts/` |
| Firefox | `~/.mozilla/native-messaging-hosts/` | `/usr/lib/mozilla/native-messaging-hosts/` |

(`~/.config` is `$XDG_CONFIG_HOME` when it is set.) On **macOS** the files go to
`~/Library/Application Support/Google/Chrome/NativeMessagingHosts/` (`Chromium`,
`BraveSoftware/Brave-Browser`, `Microsoft Edge`, `Mozilla` instead of `Google/Chrome`), with
`--system` to `/Library/Google/Chrome/NativeMessagingHosts/`, `/Library/Microsoft/Edge/…`,
`/Library/Application Support/{Chromium,BraveSoftware/Brave-Browser,Mozilla}/…`. On **Windows**
(`brulion-cli.exe clip install`) one manifest for the Chromium browsers and one for Firefox go to
`%LOCALAPPDATA%\Brulion\NativeMessagingHosts\` (`--system`: `%ProgramData%\Brulion\…`), and each
browser's registry key names them: `HKEY_CURRENT_USER\Software\Google\Chrome\NativeMessagingHosts\io.github.januszbit.brulion`
(`…\Chromium\…`, `…\BraveSoftware\Brave-Browser\…`, `…\Microsoft\Edge\…`, `…\Mozilla\…`; `--system`:
`HKEY_LOCAL_MACHINE`).

The manifest names the program `clip install` runs as — `brulion-cli` of the AppImage is the AppImage
itself (`$APPIMAGE`: the browser starts `Brulion.AppImage`, which hands over to the host), a
program of the Nix store its stable link on `PATH` (`/run/current-system/sw/bin/brulion-cli`,
`~/.nix-profile/bin/…`) when there is one; `--program <file>` names another.

**NixOS**: the module connects the browsers itself (`programs.brulion.webClipper`, on by default:
docs/NIXOS.md) — `/etc/chromium`, `/etc/opt/chrome`, `/etc/opt/edge`, `/etc/opt/brave` and Firefox of
`programs.firefox`. `clip install --system` is refused there. The Nix packages carry the manifests
(`lib/mozilla/native-messaging-hosts/`, `share/brulion/native-messaging-hosts/`).

## Clipping

Click the extension's button (**Alt+Shift+B** opens it), choose, **Clip**:

* **What**: *Article* (the page's readable content, found by Mozilla's Readability), *Selection*
  (chosen when there is one), *Whole page*, *Screenshot* (the visible part of the page), *Link only*
  (the title and the address — with the selection, when there is one). What a page does not have is
  greyed out.
* **Title**: the new page's name (the page's own title by default).
* **Save to**: *A new page* in a **folder** (`Clippings` — `Wycinki` in Polish — by default; empty:
  the notebook's main folder; `Reading/Nature` makes the folders), optionally **also linked from
  today's journal**; *Today's journal* (a block at its end, the journal template filling a new day);
  *Under a heading of a page*: type a page's name, pick its heading — the clip is the last block of
  that heading's section.
* **Template**, **Tags** (comma-separated: `reading, recipes`), **Save the pictures into the
  notebook**.
* **Preview** shows the Markdown that would be written (nothing is downloaded or written for it).
* The result names the page; **Open in Brulion** shows it in the app (the running one, else the app
  starts there).

The popup remembers where the last clip went. A notebook switcher appears when Brulion has more
than one notebook (its current and recent ones). Closing the popup does not stop a clip: it is
saved by the extension's background, and a notification says where.

**Context menus** (right click): *Clip article / selection / whole page / screenshot to Brulion* on
a page, *Clip link to Brulion* on a link (its text and address), *Clip picture to Brulion* on a
picture — with the options' defaults, a notification with **Open in Brulion** at the end (in
Firefox click the notification). **Alt+Shift+K** clips the selection, else the article, the same way.
Shortcuts are changed in `chrome://extensions/shortcuts` or Firefox's `about:addons` → ⚙ →
*Manage Extension Shortcuts*.

The browser lets no extension read its own pages (`chrome://`, the stores, the PDF viewer): there
only a link or a screenshot can be clipped.

## What a clip becomes

The extension reads the page in its own isolated world after your click (`activeTab`: no access to
any page before), runs Readability on a copy of the document, makes the addresses absolute and
picks the address a lazily loaded picture would load. Brulion's host converts the HTML with the
same converter as imports and pasting (`io::htmlToMarkdown`, QTextDocument: nothing in the HTML is
ever loaded or run):

* headings, paragraphs, **bold**, *italic*, ~~strike~~, ==highlights==, `code`, quotes, rules,
  bullet, numbered (`start`) and nested lists, task checkboxes, GFM tables;
* **figures**: the picture, then its caption as a paragraph in italics; HTML5 sections (`article`,
  `header`, `aside`, `footer`, `nav` …) stay apart;
* **code blocks** with their language — `class="language-x"` / `lang-x` on `<code>` or `<pre>`,
  `data-lang`, GitHub's `highlight-source-x`; the code's lines exactly;
* **links** resolved against the page (`<base>` included): `../a.html`, `/b`, `//cdn…`;
  a link to the page's own section (`#notes`, footnotes) keeps the page's address; `javascript:`,
  `data:` and `file:` links keep only their text; a picture linking to itself is the picture alone;
* **players** (YouTube, Vimeo, `<iframe>`, `<video>`, `<audio>`) become links to them;
* scripts, styles, forms, `<svg>`, `<canvas>` and event handlers are dropped; text that would read
  as Markdown syntax is escaped (`\*`, `\#`), dollars that would pair into a formula too.

**A new page** is named after the title (characters a file name cannot have become spaces, at most
100 characters; never an existing page — `Title 2` —, never a daily note: a title that is a date
gets the site's name). **The journal** and **a heading's section** get one block: the template's text
indented under its first line, its YAML frontmatter turned into `key:: value` properties of the
block (lists joined with commas, tags as `#tag`). The note's other bytes, its line endings and its
BOM stay as they were.

## Pictures

With **Save the pictures into the notebook** on, `brulion-cli` downloads the clip's pictures into the
attachments folder (`assets/` by default) and embeds them as the app embeds a pasted picture —
`![[name.jpg]]`, by name when no other file of the notebook has it, else by path; a picture inside a
link as `[![alt](assets/name.jpg)](link)`. The file is named after the address's last part and the
format the bytes really have (`photo.php` serving a JPEG is `photo.jpg`); a screenshot `<title> 1.png`.

Limits: http(s) only; at most **60 pictures** a clip, **15 MB** each, **60 MB** together, **90 seconds**
for all of them (a server silent for 30 s fails); the bytes must be a picture by their signature (an
error page is none); no cookies are sent, the agent is a generic `Mozilla/5.0`, redirects are
checked (never from https to http, never from the internet into a private network). **A page on
the internet never makes the computer fetch from its own network**: pictures on `localhost`,
`192.168.x.x`, `*.local` and the like are downloaded only when the clipped page is there itself
(an intranet, a NAS). `data:` pictures are decoded, never fetched. What is not saved stays a picture
from the internet (`![alt](https://…)`, which the app shows when you allow it); the result lists why
(too many, too large, not a picture, the server refused it, a private host…).

## Templates

A template is Markdown with variables, as Brulion's page templates (docs/TEMPLATES.md): every
variable of those (`{{date}}`, `{{date:FORMAT}}`, `{{time}}`, `{{weekday}}`, `{{month}}`, `{{week}}`,
`{{cursor}}`) works, and a clip adds:

| Variable | What it inserts |
|---|---|
| `{{title}}` | the title |
| `{{url}}` | the page's address (a link's, for a link) |
| `{{domain}}` | the address's host without `www.` |
| `{{site}}` | the site's name (`og:site_name`), else the domain |
| `{{author}}` | the author (Readability's byline, `meta author`, JSON-LD) |
| `{{published}}`, `{{published:FORMAT}}` | the publication date as `yyyy-MM-dd`, or in a [Qt format](TEMPLATES.md#date-and-time-format) (`{{published:d MMMM yyyy}}`); the page's own text when it is no date |
| `{{description}}` | the page's description (meta description, the article's excerpt) |
| `{{content}}` | what was clipped, as Markdown (the article, the selection, the page, the screenshot's or the picture's embed) |
| `{{content:quote}}`, `{{content:text}}` | the same as a `> ` quote, as plain text |
| `{{selection}}`, `{{selection:quote}}`, `{{selection:text}}` | the selection (also with an article or a link) |
| `{{tags}}` | the tags: `#a #[[b c]]` in the text, `[a, "b c"]` in the frontmatter, `#a, #[[b c]]` in `key::` lines |
| `{{image}}` | the page's picture (`og:image`), saved like the others |
| `{{screenshot}}` | the screenshot's embed |

Values are inserted as data: a title holding `{{date}}` stays `{{date}}`, `*` and `[` of a title are
escaped in the text, and where a value goes decides its form —

* in a **YAML frontmatter** at the template's start a value in quotes (`title: "{{title}}"`) is
  escaped for them; a bare value is quoted as a whole when another YAML reader would read it
  differently (`a: b`, `yes`, `1984`, `18:30`); line breaks become spaces; `tags: {{tags}}` is a flow
  list and `  - {{tags}}` (quoted or not) an item per tag;
* in **`key:: value`** lines values are one line;
* **a line left empty**: a frontmatter entry or a `key::` line whose variables are all empty is left
  out (an article without an author has no `author:`), and so is a line of the text holding only
  variables that are all empty, with one of the blank lines around it.

The built-in templates:

**Brulion** (a new page's default):

```markdown
---
source: {{url}}
author: {{author}}
published: {{published}}
clipped: {{date}} {{time}}
tags: {{tags}}
---

{{content}}
```

**Obsidian Web Clipper** (its default template's properties):

```markdown
---
title: "{{title}}"
source: "{{url}}"
author:
  - "[[{{author}}]]"
published: {{published}}
created: {{date}}
description: "{{description}}"
tags:
  - "clippings"
  - "{{tags}}"
---
{{content}}
```

**Joplin** (the frontmatter of Joplin's Markdown export): `title`, `source`, `author`, `created:
{{date:yyyy-MM-dd HH:mm}}`, `tags`, then `{{content}}`.

**Link and selection** (the journal's and a heading's default):

```markdown
[{{title}}]({{url}}) {{tags}}
{{selection:quote}}
```

Your own: a page of the notebook's **templates folder** (it is offered in the list; the journal's,
the week's and the month's templates are not), or a template of the extension's **options** (kept
in the browser).

## Options

The extension's options (its menu → *Options*, or the popup's *Options*): the connection to Brulion
(which `brulion-cli`, which notebook), **the notebook** (the one open in Brulion, or a fixed one of
Brulion's notebooks), **where clips go** (the place, the folder, the journal link, a page and heading),
the **template** of a new page and of a block, **tags of every clip**, **pictures**, and **your
templates** (a name and a text; *Variables* lists them). The context menus and Alt+Shift+K use
these.

## Without the host (phone, browser version)

Native messaging needs a program on the same computer. Where there is none — **Android** (Firefox for
Android has no native messaging), Brulion **in the browser** (WebAssembly), a computer without
`brulion-cli` — the popup says how to connect and offers:

* **Copy as rich text**: the selection, else the article, with a link to the page, as HTML on the
  clipboard. Pasted into a note in Brulion — on the computer, the phone or in the browser version —
  it becomes Markdown with the same converter (links, lists, code; pictures from the internet stay
  links).
* **Copy title and link**: `[Title](address)`.

On the phone **Share → Brulion** (docs/ANDROID.md) saves the shared title and address into today's
journal, and the browser version's share target does the same (docs/WEB.md "Sharing to Brulion").

For a `brulion://` address (an app handling such links, when there is one): the clip is the JSON
object of the host's `clip` message below, and `brulion-cli clip save` takes the same fields as
options — a handler converts it with the same code (`src/core/clipper`), nothing in the extension
changes but its delivery (`clipper/lib/clip.js`, `saveClip()`).

## The host

The browser starts the program a host manifest names — `brulion-cli` with the extension's origin
(`chrome-extension://…/`, on Windows also `--parent-window=…`) or, from Firefox, the manifest's path
and the extension's id; brulion-cli recognises both and serves the clipper (also through the app's
binary: `brulion chrome-extension://…`); `brulion-cli clip host` does the same by hand. It talks over
standard input and output in **frames**: a 32-bit length in the machine's byte order, then that
many bytes of UTF-8 JSON (`src/core/clipper/nativemessaging.h`).

Every request is an object with a `type` and an `id` the answer repeats; `vault` chooses the
notebook — one of those `hello` lists (the app's current and recent notebooks, `BRULION_VAULT`),
else the default one:

| `type` | Answer |
|---|---|
| `hello` | `{version, protocol: 1, notebooks: [{path, name, current, exists}], notebook: {path, name, newPagesFolder, journalsFolder, attachmentsFolder, templatesFolder}, templates: [{id, name, builtin} \| {page, name}], folders, language}`; `notebookError` when there is no notebook yet |
| `folders` | `{folders}` |
| `pages` `{query, limit}` | `{pages: [{path, title}]}` — titles starting with the query first, recently changed first (diacritics ignored) |
| `headings` `{page}` | `{headings: [{level, text}]}` |
| `preview` `{clip}` | `{result: {title, path, markdown, truncated, pictures, template, place}}` — nothing downloaded or written |
| `clip` `{clip, progress}` | `{result: {path, title, created, line, ref, open: {path, line}, journal?, pictures: {total, saved, kept, failed: [{source, reason, message}]}, template, kind, place}, warnings}`; with `progress: true` first `{type: "progress", done, total}` frames while pictures download |
| `open` `{path, line}` | shows the page in the running app, else starts it there |

A failure is `{type: "error", id, code, message, details}` (`bad_folder`, `page_not_found`,
`under_not_found`, `unknown_notebook`, `lock_timeout`, `too_large` …); the session goes on. The
`clip` object: `{kind: article|selection|page|screenshot|bookmark|picture|link, url, title, html,
selectionHtml, selectionText, baseUrl, author, site, published, description, language, image,
screenshot (data:image/…;base64), pictureUrl, linkUrl, linkText, tags (a list or "a, b"), place: {kind:
page|journal|heading, folder, page, heading, journalLink}, template: {builtin | page | text},
downloadPictures}`.

**Everything from a page is hostile.** The host runs nothing of it (the HTML is parsed by
QTextDocument, which loads no resource); a message over **64 MiB** ends the session (a browser sends
no more), an answer never exceeds 1 MiB (a browser's limit); HTML is limited to 24 MiB, a selection
to 8 MiB, a screenshot to 40 MiB, titles to 300 characters, tags to 32; folders and pages with `..`,
`.`, hidden parts (`.git`, `.brulion`, also as `GIT~1`), a leading `/` or a drive (`C:`) or a backslash
are refused, titles are only file names (`../../etc/passwd` is the page `etc passwd`); links keep
only http(s), `file:` and `mailto:`; control characters and bidi overrides leave titles; only the
app's notebooks can be written. Writes are the command line's (`--read-only` refuses them).

`brulion-cli clip save` does the same from a shell: `brulion-cli clip save --url https://… --stdin
< page.html --folder Reading --tags "a, b"` (docs/CLI.md).

## Privacy

* The extension asks for `activeTab` (a page only after your click, menu or shortcut), `scripting`,
  `contextMenus`, `nativeMessaging`, `storage` and `notifications` — no host permissions, nothing
  runs in pages by itself, nothing is sent to any server. Its settings stay in the browser's local
  extension storage.
* Firefox's manifest declares that no data is collected (`data_collection_permissions: none`).
* The clip goes to `brulion-cli` on the same computer; the only requests are for the clip's pictures,
  to their own sites, with no cookies, no referrer and a generic agent (see [Pictures](#pictures));
  turn **Save the pictures** off and there are none.

## Packages

```sh
scripts/clipper-pack.sh [--store] [--lint]
```

makes `dist-clipper/brulion-clipper-<version>-chrome.zip` (with the `key`: unzip and *Load unpacked*),
`…-firefox.xpi` (unsigned) and with `--store` `…-chrome-store.zip` (without the `key`: the Chrome Web
Store gives an id of its own — `clip install --extension-id <id>` then lets it in). Each gets its
browser's manifest (Chromium: the service worker, no Firefox settings; Firefox: background scripts,
no `key`), the version of CMakeLists.txt, sorted entries with fixed times. `--lint` runs Mozilla's
`web-ext lint` (nixpkgs#web-ext): no errors; the two warnings are Readability's own `innerHTML`.
Firefox's manifest asks for Firefox 140 (`data_collection_permissions`; 142 on Android, where native
messaging is missing anyway). Publishing in the stores (signing for Firefox) is not done.

`clipper/vendor/Readability.js` is Mozilla's Readability 0.6.0, unchanged (Apache License 2.0,
`clipper/vendor/Readability-LICENSE.md`; `clipper/vendor/README.md` says how to update it).

## Tests

* `tst_clipper` (src/core/clipper): frames in any pieces, malformed and oversized ones; requests
  (hostile values refused or cleaned); templates (YAML, left-out lines, the built-ins, Polish); a
  Polish article (`tests/core/clipper/fixtures/artykul.html` → `artykul.md`;
  `BRULION_CLIPPER_UPDATE_FIXTURES=1` rewrites it) and HTML pieces (figures, code, links, players,
  scripts); pictures from a server on 127.0.0.1 (limits, private hosts, a silent server); manifests
  and their places on Linux, macOS and Windows; installing, reading and removing them in a private
  home (another program's file of the same name stays); the browsers' launch arguments.
* `tst_cli_clip`: brulion-cli as a process — clips into a page (its picture saved), the journal and
  a heading's section with CRLF kept, previews and dry runs writing nothing, refusals, the host's
  frames (every request, another notebook, an unknown one, malformed and oversized frames), the
  browsers' arguments, `clip install` / `status` / `uninstall` / `manifest` in a private home.
* `tst_io_html`: the converter's article rows.
* `tst_clipper_extension`: `node --test clipper/test/` — the clip request, settings, templates, the
  manifest's ids against the host's, the translations (both languages, every key used), the page
  reader's `srcset` (skipped without node).
* `scripts/web-test.sh --clipper` (`tests/web/clipper.e2e.mjs`, Playwright): the extension in a real
  Chromium with brulion-cli as its host — the popup clipping an article into a page with its picture
  (Polish texts, the preview), a selection and a link from the context menus into the journal, a
  screenshot, the popup without a host. `BRULION_CLIPPER_SCREENSHOTS=<dir>` saves the popup and the
  options page, light and dark. Firefox is checked by `web-ext lint` only (Playwright cannot load an
  extension into Firefox).

## Troubleshooting

* **"Brulion is not connected"**: `brulion-cli clip status` names each browser's manifest and its
  program (`programExists`, `thisProgram`); run `clip install` again after moving or upgrading
  Brulion. Chromium logs a host that cannot start to its standard error (`chrome --enable-logging=stderr`).
* **A browser from Flatpak or Snap** cannot start programs outside its sandbox: native messaging
  does not work there without extra setup of the sandbox; use a browser installed otherwise, or the
  copy fallback.
* **"No notebook yet"**: open a notebook in the app once, or set `BRULION_VAULT`.
* **Pictures left on the internet**: the result lists each with its reason; a site that needs a
  login for its pictures answers the host with an error page (the host has none of the browser's
  cookies).
