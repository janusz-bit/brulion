# Brulion in the browser (WebAssembly)

The browser version is the same C++/QML app as on NixOS and Android, compiled with
**Qt for WebAssembly** (single-threaded). After the first load it works **offline** as a
PWA (it can be "installed" from the browser menu), and it keeps notes in the browser's
**IndexedDB**.

* Two forms, both in every release: **a single HTML file** `brulion-web-<version>.html`,
  opened from disk without a server (below, "Single HTML file"), and **a site** `brulion-web-<version>.zip`
  for any static file server, under any path — all addresses are relative (on
  NixOS the `services.brulion-web` module). There is no public hosted version: the project's GitHub Pages
  (`https://janusz-bit.github.io/brulion/`) is the F-Droid repository (docs/ANDROID.md "Brulion's own F-Droid repository").
* Limitations compared with the native apps: reminders only while the tab/PWA is open,
  no access to folders on disk (file import/export instead), Nextcloud sync
  needs CORS (below), no Syncthing. Qt for WebAssembly has no time zone
  database: when importing a calendar (`.ics`), times with a zone (`TZID`) are taken as local
  time (UTC times are converted normally). The browser has a single notebook, so it has no
  documentation notebook ("Brulion Documentation", with the Polish UI "Dokumentacja
  Brulionu"): the welcome page of the sample notebook points to the documentation in the
  repository (`src/app/shell/guide/en`, with the Polish UI `src/app/shell/guide/pl`).

## Tool versions

| Component | Version | Source |
|---|---|---|
| Qt for WebAssembly | 6.11.2 `wasm_singlethread` | `aqt install-qt all_os wasm 6.11.2 wasm_singlethread` (aqtinstall 3.3.0) |
| Host Qt (moc, rcc, qmlcachegen, lrelease…) | 6.11.2 `linux_gcc_64` | `aqt install-qt linux desktop 6.11.2 linux_gcc_64` |
| Emscripten | **4.0.7** | `emsdk install 4.0.7 && emsdk activate 4.0.7` |

Qt 6.11 requires **exactly** Emscripten 4.0.7 (documentation
[doc.qt.io/qt-6.11/wasm.html](https://doc.qt.io/qt-6.11/wasm.html): "The supported version of
Emscripten for 6.11.2 is Emscripten 4.0.7"; the same in `QT_EMCC_RECOMMENDED_VERSION` in
`lib/cmake/Qt6/QtPublicWasmToolchainHelpers.cmake`). Emscripten does not guarantee ABI compatibility
between versions, so another emsdk (e.g. `emscripten` from nixpkgs, currently 6.0.x) will not do.
The build script checks the `emcc` version and stops on a mismatch. When bumping Qt, change
`QT_VERSION` and `EMSDK_VERSION` in `scripts/wasm-build.sh` together.

The `all_os/wasm` package from aqt is built on Windows: its `bin/qt-cmake` scripts are not
executable and point to `C:/…`. That is why we build with plain `cmake` and the
`lib/cmake/Qt6/qt.toolchain.cmake` file, which finds Emscripten through the `EMSDK` variable.

## Building

```sh
scripts/wasm-build.sh --setup   # first time: emsdk 4.0.7 + Qt 6.11.2 (wasm + host) into ~/.cache
scripts/wasm-build.sh           # configure + build + assemble the site in dist-wasm/
scripts/wasm-build.sh --clean   # from scratch
scripts/wasm-build.sh --assemble  # only reassemble dist-wasm/ from an existing build
```

The script is idempotent (running it again rebuilds only what changed, with `ccache` if present)
and configurable through environment variables:

| Variable | Default |
|---|---|
| `BRULION_CACHE` | `${XDG_CACHE_HOME:-~/.cache}` |
| `EMSDK` | `$BRULION_CACHE/brulion-emsdk/emsdk` |
| `QT_ROOT` | `$BRULION_CACHE/brulion-qt` (shared with the Android build) |
| `QT_WASM` / `QT_HOST` | `$QT_ROOT/6.11.2/wasm_singlethread` / `$QT_ROOT/6.11.2/gcc_64` |
| `BUILD_DIR` / `DIST_DIR` | `build-wasm` / `dist-wasm` |
| `BUILD_TYPE` | `MinSizeRel` (smallest `.wasm`) |
| `JOBS` | `nproc` |

On NixOS the script enters `nix shell` by itself (cmake, ninja, ccache, python3) and adds to
`NIX_LD_LIBRARY_PATH` the libraries that the prebuilt Qt tools from aqt need (glib, krb5,
brotli) — this requires `programs.nix-ld`. Other systems need cmake ≥ 3.24, ninja,
python3, git, and for `--setup` also `uvx` (uv) or `aqt`.

**The Raspberry Pi (dev machine) does not build WebAssembly** — linking a Qt app
(wasm-ld + wasm-opt) needs several GB of RAM; the script refuses with < 6 GB RAM
(`BRULION_FORCE_LOCAL=1` forces it). Build on the laptop (new files first `git add -N`):

```sh
flock <lock> laptop-run bash -c 'BUILD_DIR=~/.cache/brulion-wasm-build scripts/wasm-build.sh'
```

(the working directory on the laptop is cleaned on every `laptop-run`, which is why the build
directory lives in `~/.cache`).

### What ends up in `dist-wasm/`

| File | Description |
|---|---|
| `index.html` | our shell (from `wasm/index.html`, based on Qt's `wasm_shell.html`): loading screen with the logo and download progress, "Loading Brulion…", error screens, service worker registration. Black like the app's default theme; in English unless Polish was chosen in the app (the app remembers the language in `localStorage` "brulion.language": `rememberShellLanguage()`, `src/platform/wasm/wasmshell.h`) |
| `brulion.js`, `brulion.wasm` | the app (Emscripten glue + WebAssembly module) |
| `qtloader.js` | Qt loader (`qtLoad()`) |
| `sw.js` | service worker (from `wasm/sw.js`): offline, notification clicks |
| `manifest.webmanifest` | PWA manifest (from `wasm/`) |
| `icons/` | PWA icons (from `wasm/icons/` + `assets/logo.svg`) |
| `.nojekyll` | GitHub Pages (should anyone deploy the site there) without Jekyll processing |

The script writes into `index.html` the size of the `.wasm` (progress bar) and the hash of the app files
(`?v=<hash>` in the addresses — the browser never combines `brulion.js` and `brulion.wasm` from two
different releases), and into `sw.js` the cache version (hash of the whole site) and the list of files to cache.

Next to it, `dist-wasm-single/brulion.html` is produced (variable `SINGLE_FILE`): the same site in a single
file, opened from disk (below, "Single HTML file"); `--assemble` rebuilds both.

The `build-wasm/src/app/brulion.html` generated by Qt is only Qt's default page (no PWA)
— good enough for quick tries; `dist-wasm/` and `dist-wasm-single/` are for publishing.

## Running locally

The site in `dist-wasm/` cannot be opened from `file://` — it needs an HTTP server (only the single
file `dist-wasm-single/brulion.html` opens from disk, below):

```sh
python3 -m http.server 8000 --bind 127.0.0.1 --directory dist-wasm
# http://127.0.0.1:8000/
```

The service worker (offline) works only on `https://` and on `localhost`/`127.0.0.1`.
URL parameters (for debugging):

* `?nosw` — do not register the service worker.
* `?selftest=storage`, `?selftest=notify`, `?selftest=glyphs`, `?selftest=keyboard`,
  `?selftest=share`, `?selftest=storage,notify` — tests (below).
* Only in a tab marked for tests: `?selftest=webpictures`, `?selftest=pen` (tests on the
  open notebook: "Browser tests (Playwright)"), `?selftest=pickfiles,savefile`
  (file picker and download on successive clicks: `scripts/wasm-smoke.mjs
  --pick-files`) and together with them `?route=<view>` or
  `?route=<JSON route>` — the first view after the notebook opens (like `--route`, e.g.
  `{"view":"drawing","path":"Rysunki/a.svg"}`), `?now=2026-10-14 10:00` — a fixed clock (like
  `--now`: a known journal day). The tab marker is `sessionStorage` `brulion-selftest` = `1`:
  `tests/web` sets it before the page loads; by hand — in the browser console
  `sessionStorage.setItem('brulion-selftest', '1')`, then reload (the marker disappears
  when the tab is closed). A link cannot set it, so a link sent to someone will not run a test on their
  notebook (downloading pictures from the internet, export) nor change their clock or view —
  without the marker these parameters are ignored. The route and the clock reach the app as a single argument
  (`--route=…`, `--now=…`), so the value never becomes a separate option (`?route=--vault=…` is
  just an unknown view). `tests/web/shell.spec.mjs` checks this.
* `?keyboard=visual` — the on-screen keyboard shrinks only the visual viewport, not the page
  (the browsers' default behaviour, `interactive-widget=resizes-visual`; below, "On-screen
  keyboard") — for tests and diagnosis.
* `?report=/__report/<name>` — test results (`?selftest=…`) are also sent as a POST request to this
  path on the same site (`{name, json, t}`, without cookies; test server
  `scripts/web-test-server.py`) — for browsers without DevTools, e.g. on the Android emulator.
  Other paths, and addresses that the URL parser turns into another site (e.g. `/%09/host`: the parser
  strips tabs and newlines, leaving `//host`), are ignored — checked by
  `scripts/wasm-smoke.mjs --keyboard`. The `?selftest=keyboard` result contains the start of the text of the
  edited field (`focusText`), never of a password field.

A site under a path (e.g. `/brulion/` on your own server) can be checked locally by copying
`dist-wasm/` to `site/brulion/` and serving `site/` (`http://127.0.0.1:8000/brulion/`).

## Headless browser smoke test

`scripts/wasm-smoke.mjs` (Node ≥ 22, no dependencies) drives Chromium through the DevTools
Protocol: it opens the page, waits until the app replaces the loading screen, takes a screenshot,
checks the service worker. With `--selftest` it runs `?selftest=storage`: the app increments a
counter in a hidden file `.brulion-selftest` in every mounted directory, waits for the
automatic save to IndexedDB and publishes the result (`window.brulionSelfTest`). Each following
load must see the counter +1.

```sh
python3 -m http.server 8123 --bind 127.0.0.1 --directory site &
node scripts/wasm-smoke.mjs --url http://127.0.0.1:8123/brulion/ --profile /tmp/prof \
  --loads 2 --selftest --expect-first 1 --screenshot online.png    # save + reload
node scripts/wasm-smoke.mjs --url http://127.0.0.1:8123/brulion/ --profile /tmp/prof-index \
  --loads 2 --index-notes 3000                                     # index without input
kill %1                                                            # server stopped
node scripts/wasm-smoke.mjs --url http://127.0.0.1:8123/brulion/ --profile /tmp/prof \
  --loads 1 --selftest --expect-first 3 --screenshot offline.png   # offline from the SW cache
node scripts/wasm-smoke.mjs --url "file://$PWD/dist-wasm-single/brulion.html" \
  --profile /tmp/prof-single --loads 2 --selftest --expect-first 1  # single HTML file from disk
```

The second run (a new Chromium process, the same profile, the server stopped) proves that the
data survived a browser restart and that the app starts offline from the service worker cache.
A `file://` address (single HTML file) has no service worker — the script does not check it then.

Other options:

* `--notify` — grants the page notification permission, the app (`?selftest=notify`) shows
  a test notification, the script "clicks" it and checks that the click (id, path, line)
  reached C++ (`WasmNotifier::activated`).
* `--glyphs` — `?selftest=glyphs`: the app checks that the fallback fonts (below,
  "Fonts: emoji and symbols") are installed and draw all of Brulion's emoji and symbols
  without empty boxes (`window.brulionGlyphSelfTest`: for each font, the characters drawn as
  boxes before and after installation). CI runs it with the first test.
* `--keyboard` — injects its own `window.visualViewport` into the page (before the page's scripts) and
  runs `?selftest=keyboard`: on every change the app publishes
  `window.brulionKeyboardSelfTest` (`{overlap, shown, animating, source, mode, sample, …}`),
  and the script "opens" a 300 px keyboard, 250 px with the page scrolled by 20 px (as iOS does),
  gives an invalid value (NaN → 0), zooms the page with a gesture (`scale` 2: not a keyboard, 0),
  scrolls the page by 40 px (the app must scroll it back to the top) and hides the keyboard —
  each time the app must report the right height (below, "On-screen keyboard").
  Before that it checks the shell alone (the app's scripts held back, so the storage and test counters
  stay untouched): with `?report=/__report/smoke` the result is POSTed only to that address on the
  same site, and with `/%09/`, `/%0A/`, `/%0D/`, `//`, `/\`, a full address of another site
  and other paths — nowhere. With `--screenshot` a `…-keyboard.png` screenshot is added. CI runs it
  with the first test.
* `--keyboard-phone` — the same on an emulated phone (412×915 CSS pixels, density 2.625, touch
  screen) with today's journal written to IndexedDB (like `--copy`) and a block edited by
  a tap low on the screen: the page asks for `interactive-widget=resizes-content` (and with
  `?keyboard=visual` for `resizes-visual`), Qt's hidden keyboard input field stays in the top-left corner
  of the window, a keyboard that shrinks only the visual viewport (300 px; 180 px with the view shifted
  by 120 px) lifts the toolbar exactly to its top edge, and a page shrunk by the
  browser (the emulated phone's screen 300 px shorter) gives zero overlap, the keyboard counts
  as shown, and the toolbar ends at the bottom of the window — also with a stale `innerHeight` (the old
  formula would count the keyboard twice); Qt's containers are not scrolled. The app reports the position of the toolbar
  and content in `brulionKeyboardSelfTest` (`windowHeight`, `contentBottom`,
  `toolbarBottom`, `bottomNavTop`); when the page grows again (keyboard hidden with Back), the bottom
  navigation comes back and the toolbar sits right on it. With `--screenshot`, screenshots `…-edit.png`, `…-visual.png`,
  `…-content.png` are added. CI runs it with the first test.
* `--two-tabs` — opens a second tab: it must wait with the message "Brulion is open in another
  tab…", and take over the data once the app in the first tab is left.
* `--share` — share target (below, "Sharing to Brulion"): the manifest as read by
  Chrome (`Page.getAppManifest`) has a GET `share_target` `./` with `share-title`, `share-text`,
  `share-url` and no errors; a start with a shared title and text (`?selftest=share`:
  the app publishes the dialog's data, `window.brulionShareSelfTest`, with `dialog` — whether the dialog was meant to
  open; before the start, IndexedDB holds a notebook with today's journal, as with `--copy`)
  opens the **Save to Brulion** dialog with "[Przepis na żurek](https://kuchnia.pl/żurek) — Zobacz
  to" (Polish sample share: "Recipe for żurek — Check this out"), and the address bar loses the share parameters; a second tab with a share while the
  first is open hands it over (it does not load the app itself, shows a message and asks to be closed),
  and the first shows the dialog with "Zażółć gęślą jaźń" (Polish pangram). With `--screenshot` a
  `…-share.png` screenshot is added. CI runs it separately (profile `profile-share`).
* `--pick-files` — file picking and downloading without Qt Widgets (`platform/wasm/wasmfiles.h`): a tab
  marked for tests (sessionStorage `brulion-selftest`), without `showOpenFilePicker` /
  `showSaveFilePicker` (Qt then uses `<input type=file>` and a plain download, which every
  browser has), `?selftest=pickfiles,savefile`. A real click opens the file picker, which
  must accept several files (DevTools intercepts it), and gets seven: "Zażółć gęślą
  jaźń.md" (UTF-8, CRLF lines), an empty `pusty.md`, `duży.md` 2 MiB over the test limit of 1 MiB per
  file, `notatka.txt`, "pół — ą.md" exactly 1 MiB, "drugie pół.md" 600 KiB, which would exceed the
  test limit of 1.5 MiB in total, and a small "po limicie.txt"; the app
  (`window.brulionPickSelfTest`) must report them in order with sizes and SHA-256 — the large one
  unread, and from the file over the total onwards none (like `FileGatherer` on the desktop). The next
  click downloads "Zażółć.md" with the bytes "ąę\n"
  (`Browser.setDownloadBehavior`). CI runs it separately (profile `profile-pick`).
* `--loader-screenshot file.png` — on the first load throttles the network to ~3 MiB/s and takes
  a screenshot of the loading screen (progress bar).
* `--lang pl-PL` (default) — the browser language.
* `--index-notes N` — before the first load writes to IndexedDB a vault with N
  generated notes (the way the IDBFS mount stores them, with `settings.ini` pointing to
  it as the current one); every load must then report a complete index
  (`brulionShell.indexed()`, time in `brulionShell.indexedAt`) **without any input** (mouse,
  keyboard) — a regression test for stalled timers (below, "Technical limitations"). From 200
  notes on, the first load must also save the ready index (`brulionShell.indexCacheSavedAt`),
  and each following one take almost all notes from it (`brulionShell.indexStats()`:
  `indexedFromCache` ≥ N − 1). CI runs it with 3000 notes.
* `--copy` — copying blocks with real input: before loading writes to IndexedDB (like
  `--index-notes`) a notebook with today's journal (three blocks, the second with a child, Polish
  characters, bold, a link), then clicks the text of the first block, presses Escape (block selected),
  Shift+↓ (the next one with its child) and Ctrl+C, and the page (with clipboard permission granted) reads the browser
  clipboard: `navigator.clipboard.readText()` must give exactly the Markdown of these blocks,
  and `navigator.clipboard.read()` — Brulion's HTML (`text/html` with the `generator` marker). With
  `--screenshot` a `…-copy.png` screenshot is added. The result and the click position depend on the page layout
  1280×800 (the first line of the day about 153 px from the top; the script tries several heights).

Drawings in the app's theme are checked by a separate script, `tests/app/drawing/wasm-drawing-check.mjs`
(the same minimal DevTools client, no changes to `wasm-smoke.mjs`): it writes to IndexedDB
a notebook with today's journal embedding `Rysunek.svg` (the golden test file,
`tests/data/drawing/auto_theme.svg`, "Like the app") and `settings.ini` with the `ui/themeMode` theme
light, then dark; after the app starts it decodes the screenshot in a separate tab (Qt's WebGL
canvas cannot be read back) and checks that the drawing's paper has the theme's colour (#ffffff / #121212)
with ink in its colour (#1f2430 / #caced8). This proves that the `drawing` image provider works
on the browser's GUI thread and reads files from IDBFS (2026-10-28, laptop; CI does not run it).
With `--text` the script then clicks the drawing (the drawing editor opens), presses T (text tool),
clicks on the sheet, types "Zażółć gęślą jaźń" key by key and presses Esc; the drawing file
in IndexedDB must then have a text element with that text — typing text on a drawing works
in the browser (2026-10-29, laptop: both themes).

```sh
node tests/app/drawing/wasm-drawing-check.mjs --url http://127.0.0.1:8123/brulion/ [--shots directory] [--text]
```

Driving a running app by AI agents (`brulion --remote-control`, `brulion-cli app …`,
docs/CLI.md "Remote control") does not exist in the browser: the page is driven through the Chrome DevTools
Protocol — with Playwright, obscura or like this script — with its `?selftest=…`.

Chromium: `$CHROME` or `chromium`/`google-chrome` from `PATH`; WebGL 2 through SwiftShader.
`BRULION_CHROME_NO_SANDBOX=1` (or `CI=true`) turns off the sandbox when it cannot start
(containers, GitHub Actions on Ubuntu 24.04).

## Browser tests (Playwright)

`tests/web/` (a separate npm project: `@playwright/test` 1.64.1 and `pngjs` 7.0.0; CMake skips it)
checks the built site in **Chromium and Firefox** — pictures from the internet (below, "Pictures
from the internet"), the stylus in the drawing editor ("Stylus (pressure)"), PDFs ("PDFs in the browser") and parts
of a link to a page in a folder (`linkparts.spec.mjs`, below). GitHub Actions runs it in
`wasm.yml` (job `browsers`, after `build`; the release waits for it — it calls `wasm.yml`); the HTML report, screenshots and traces of failed tests are in the `web-browser-tests` artifact
(7 days). In CI a failed test is retried once (`retries: 1`; one that passes the second time is marked
"flaky" in the report). Locally, after `scripts/wasm-build.sh`:

```sh
scripts/web-test.sh --install                     # npm ci + Playwright's Chromium and Firefox
scripts/web-test.sh -- --project=firefox stylus.spec.mjs   # arguments for `playwright test`
scripts/web-test.sh --single-file file.html -- singlefile.spec.mjs   # another single HTML file
(cd tests/web && npx playwright show-report)     # HTML report (playwright-report/)
```

* **Servers** (`tests/web/lib/servers.mjs`, in the test process, ports chosen by the system): the site at
  `https://brulion.test:<p>/brulion/` and `http://brulion.test:<p>/brulion/` (insecure
  context), pictures at `https://img-cors.brulion.test` (`Access-Control-Allow-Origin: *`),
  `https://img-nocors.brulion.test` (no CORS headers) and `http://img-http.brulion.test`. The browsers
  themselves resolve the `*.test` names to 127.0.0.1 (Chromium `--host-resolver-rules`, Firefox
  `network.dns.forceResolve`) — no `/etc/hosts`, and no IP addresses: `127.0.0.1` is
  "trustworthy" for browsers (no mixed content blocking), and a private network for Brulion (export would
  skip it). A self-signed certificate is made by `openssl` on every run
  (`lib/cert.mjs`, the browsers accept it: `ignoreHTTPSErrors`). Every server records
  requests (method, path, headers), so the test sees what the browser really sent.
* **The notebook** is written by the test directly to IndexedDB (`lib/idbfs.mjs`, format as below in "Data
  in the browser" and in `scripts/wasm-smoke.mjs`), and the test opens the app with `?nosw` (the service worker
  could reject a page with such a certificate; it does not touch requests to other sites anyway),
  `?now=2026-10-14 10:00` and `?route=…` in a tab marked for tests (`markTestTab` in
  `tests/web/lib/brulion.mjs`; "Running locally" above).
* **The page around the app** (`shell.spec.mjs`; without the app: a stub `qtloader.js` records its
  arguments): without the tab marker a link does not run tests on the notebook and does not pass on the route
  or the clock; in a test tab each of them is a single argument (`--route=--vault=…` does not
  open another notebook); every `setTimeout` delay > 0 ms gets a 2 ms margin, 0 ms
  stays. With the real app in a browser that fires every `setTimeout` 1 ms too
  early, the drawing editor still saves a line after 1.2 s without any input (below,
  "Technical limitations": Qt timers).
* **App results** (`src/app/diag/webselftest.h`, library `brulion_webselftest`, in the app
  only in the web version; without `?selftest=webpictures`/`pen`/`linkparts` in a test tab it does nothing) are
  JavaScript globals:
  * `?selftest=webpictures` → `brulionWebPictureSelfTest` `{page, pageScheme, done, refused,
    timedOut, elapsedMs, pictures: [{url, state, error, format, width, height, uiFetched}],
    shownTexts}` — the pictures of today's journal as the editor fetched them (`uiFetched`: by itself,
    with the "Show pictures from the internet without asking" setting), and the messages visible in the editor; the test itself does not ask for any
    picture (a picture from the local network stays in the `ask` state); then an export of the whole
    notebook to a single HTML file with "Include copies" (`ExportController`) and `saveInBrowser()` →
    `brulionExportSelfTest` `{state, errorText, format, scope, fileName, bytes, dataPictures,
    warnings, saved}`. When the device asks before showing pictures from the internet (setting
    off), the test ends at once with `{page, done: true, refused: true, error, pictures:
    []}` — without fetching and without export (the tests turn this setting on in `settings.ini`);
  * `?selftest=pen` → `brulionPenSelfTest` `{found, editable, modified, stylusSeen, zoom,
    strokeCount, elements, changes, canvas: {x, y, width, height}, strokes: [{tool,
    simulatePressure, points, pressureMin, pressureMax, pressureFirst, pressureLast, width,
    bounds}]}` — the drawing editor's canvas (`modified`: changes not yet saved to the file;
    `strokeCount` counts freehand lines, `elements` all elements) and its freehand lines
    (window coordinates, to compare with where the drawing happened), after every change;
  * `?selftest=pdf` → `brulionPdfSelfTest` `{route: {view, path}, chip: {x, y, w, h, icon, text},
    link: {x, y, w, h}, message, button}` — where on screen the PDF tile and the link to it are
    (the test clicks them with a real mouse), the app's route, the message and the button of the PDF panel; after every
    change for two minutes. `?selftest=pdfauto` additionally opens `assets/wykład.pdf` by itself after 2 s,
    outside any click → `brulionPdfOpenSelfTest` `{path, opened, notices, errors}`
    (the browser may block the tab — then the file is downloaded, and the app says so).
  * `?selftest=linkparts` → `brulionLinkPartsSelfTest` `{ready, parts: [{href, x, y, width,
    height}], route: {view, path}}` — the links of a text that contains a part of a link's path
    (`part:…`, docs/SYNTAX.md §2 "Path parts"), with their place in the window (`linkAt` samples every 2 px),
    and the route shown; published on every change. `linkparts.spec.mjs` writes into
    today's journal `- Zapytać się [[Projekty/Remont mieszkania]] o plan` ("ask [[Projects/Flat renovation]]
    about the plan"; and the page
    `Projekty/Remont mieszkania.md`), clicks with the browser's real mouse in the middle of "Projekty" →
    route `{view: allPages, path: Projekty}`, after Alt+← on "Remont mieszkania" → the page; the same
    with a tap in Chromium with touch (412×915, `isMobile`); the journal file does not change.
* **Single HTML file** (`singlefile.spec.mjs`; the file from `--single-file`, by default
  `dist-wasm-single/brulion.html`): opened from `file://` it starts without any request other than to itself,
  does not register a service worker, releases the base64 text and after a reload sees its own
  notes (the `?selftest=storage` counters 1 higher).
* **The exported file**: in Chromium on `https://` Qt saves through File System Access
  (`showSaveFilePicker`), which needs a user gesture — the test replaces it on the test page
  with a recorder; in Firefox and on the `http://` page a plain file download arrives.
* **Firefox** draws WebGL 2 (which Qt requires) only with an X display: the script runs the tests under
  `xvfb-run` (Firefox with a window) when there is no `$DISPLAY`; missing WebGL 2 is a test failure, not
  a skip. On NixOS `scripts/web-test.sh` takes Node, `openssl` and `xvfb-run` from nixpkgs,
  and runs Playwright's browsers (prebuilt binaries) through nix-ld with their libraries
  (`NIX_LD_LIBRARY_PATH`).
* Results from 2026-10-30 (laptop and GitHub Actions; Chromium 156 — Chrome for Testing
  156.0.8078.4 — and Firefox 157 from Playwright 1.64.1): all tests pass in both
  browsers (the stylus test with DevTools only in Chromium — Playwright has no pen for
  Firefox).

## Publishing

### In CI

`.github/workflows/wasm.yml` does it (description: `docs/RELEASE.md`): it builds the site and the single file
with `scripts/wasm-build.sh --setup` and runs the smoke test in Chrome — the site under the `/brulion/` path
(start, persistence across reload and browser restart, full index of 3000 notes without any
input, offline start from the service worker) and the single file from `file://` (start, emoji, persistence across
reload and browser restart) — and then the Playwright tests (above). It does not
start on every push by itself: it runs manually or on a push to `main` with the `[ci]` marker
in the commit message (and changes in `src/`, `wasm/`…). It deploys nothing; called from `release.yml`
it gives the release `brulion-web-<version>.zip` (artifact `brulion-web`) and
`brulion-web-<version>.html` (artifact `brulion-web-html`).

### Single HTML file

`brulion-web-<version>.html` from a release (about 20 MB) is the whole browser version in a single file:
you open it from disk (double-click, "Open file…" in the browser, or drag it into its window),
without a server and without the internet. Locally `scripts/wasm-build.sh` makes it
(`dist-wasm-single/brulion.html`).

* **Notes** live in the browser's IndexedDB, just as on the site. Chromium and Firefox have one
  storage for all files opened from disk (origin `file://`): the file can be moved, renamed
  or replaced with a version from a newer release — the notes stay (checked in Chromium 156
  and Firefox from Playwright 1.64.1). Another browser or another profile has its own notes.
* **The same storage belongs to every HTML file opened from disk** in that browser: another
  `.html` file from disk could read the notes. Open only files you trust from disk in that browser
  (or keep Brulion in a separate browser profile).
* Missing: installing as an app (PWA) and the service worker (`file://` does not support them),
  **Share → Brulion** (needs the installed app) and notifications with the tab
  closed; reminders work while the tab is open and the browser allows notifications for
  pages from disk.
* **Nextcloud:** a page from disk has the origin `null`, and CORS for `null` would open WebDAV to every
  file on disk — for sync use the apps or the site on your own server (below,
  "Nextcloud (WebDAV) and CORS"). **Pictures from the internet** show only from servers that
  allow it for all sites (`Access-Control-Allow-Origin: *`).
* The browser must have `DecompressionStream` (Chrome and Edge 80+, Firefox 113+, Safari 16.4+).

How it works: `scripts/wasm-single-file.py` takes the assembled site from `dist-wasm/`. It compresses
`brulion.wasm` with gzip (level 9, no timestamp: the same build gives the same file) and inserts it as base64 into
a `<script id="brulion-wasm" type="application/octet-stream">` element; it inserts `qtloader.js` and
`brulion.js` as page scripts before the page's own (in place of the
`brulion-single-file` comment from `wasm/index.html`; it writes `</script` in them as `<\/script`, and
rejects `<!--`), turns the icons into `data:` addresses, and removes the manifest link. A reference to
any other file of the site stops the script — nothing is left in the file to download.
`wasm/index.html`, when it finds `#brulion-wasm`, downloads nothing: it decodes the base64,
decompresses it as a stream (`DecompressionStream('gzip')`) straight into
`WebAssembly.compileStreaming` (the progress bar counts decompressed bytes, "Unpacking
the app…"), releases the base64 text and does not register a service worker. Tests:
`tests/scripts/tst_wasm_single_file.sh` (ctest `tst_scripts_wasm_single_file`: the real
`wasm/index.html` with stub apps, `brulion.wasm` byte for byte, script order,
`</script`, no references, reproducibility, rejected builds), the smoke test in `wasm.yml` and
`tests/web/singlefile.spec.mjs` (above).

### Self-hosting

On NixOS: the `services.brulion-web` module (`docs/NIXOS.md`) — nginx with correct MIME types,
precompressed files (gzip/brotli), cache headers and an optional proxy to
Nextcloud. Elsewhere it is enough to copy `dist-wasm/` (any path). Recommendations:

* MIME type `application/wasm` for `.wasm` (required for streaming compilation),
* gzip/brotli compression (`.wasm` compresses about 3×, see the sizes below),
* long caching only for addresses with `?v=<hash>` (`brulion.js`, `brulion.wasm`, `qtloader.js` —
  their addresses change with every release); `index.html`, `sw.js`, the manifest and icons without
  long caching (`Cache-Control: no-cache`),
* HTTPS (the service worker, notifications and persistent IndexedDB need a secure context).

An nginx example (with the standard `include mime.types;` in the `http` block). Note: a `types { … }` block
inside `location` **replaces** the whole MIME type map — which is why the types of `.wasm` and
`.webmanifest` are set here in separate `location`s with an empty `types {}` and `default_type`:

```nginx
map $arg_v $brulion_cache {
    ""      "no-cache";
    default "public, max-age=31536000, immutable";
}
server {
    # …
    location /brulion/ {
        alias /srv/brulion/;
        gzip on;
        gzip_types application/wasm application/javascript text/javascript application/manifest+json image/svg+xml;
        add_header Cache-Control $brulion_cache always;
        location ~ \.wasm$        { types { } default_type application/wasm;           add_header Cache-Control $brulion_cache always; }
        location ~ \.webmanifest$ { types { } default_type application/manifest+json; add_header Cache-Control "no-cache" always; }
    }
}
```

## Data in the browser

Emscripten's file system lives in memory. `WasmStorage` (`src/platform/wasm/wasmstorage.h`)
mounts IDBFS (a mirror in IndexedDB) on it in three directories:

* `/home/web_user/Brulion` — the vault of the browser version,
* the app data directory (`QStandardPaths::AppDataLocation`,
  `/home/web_user/.local/share/Brulion/Brulion`) — version history, sync state,
  local settings,
* the cache directory (`WasmStorage::indexCacheRoot()`, `QStandardPaths::CacheLocation`
  + `/index`, i.e. `/home/web_user/.cache/Brulion/Brulion/index`) — the notebook's ready index
  (`<notebook id>/index-cache.bin`, docs/PERFORMANCE.md L2): on the next visit, notes
  whose text has not changed are not parsed again (the files are still read from memory).

In the browser the index is built single-threaded (`wasm_singlethread`: `IndexLoader` without a thread
pool, piece by piece on the GUI thread, as before); on the first visit the full-text index
is completed piece by piece after the "ready" index, on later visits it comes from the
cache. Saving the cache (encoding in 8 ms slices, about 5 s after the index is ready) is
a **cache mount**: a separate IndexedDB database saved in its own
transaction. IDBFS saves each mount in one transaction, and `QuotaExceededError` aborts the whole
transaction — if the index lived in the app data, a file that does not fit the storage quota
would stop saving the version history, sync state and settings on every following attempt.
Now a failed save of the cache mount:

* is not a save error (no "Saving to the browser's storage failed" message, nothing
  stays "unsaved", `beforeunload` does not ask), it only increments `brulionStorage.cacheFailures`,
* removes the files of that mount from memory (the next save also removes their old copies from IndexedDB,
  freeing space) — `WasmStorage::cacheNotSaved()`,
* stops saving the index until the end of work with the notebook (`Workspace::stopIndexCache()`,
  one line "Index cache not saved" in the event log).

On top of that the index never takes more than half of the free space: `navigator.storage.estimate()`
at load time (`WasmStorage::estimate()`) determines `IndexCacheKeeper::Policy::maxBytes`
(`notebook::indexCacheBudget()`: size of the previous file + half of the free space, at most
256 MB); a larger index is not saved. A load whose cache mount fails
does not block the start (that mount stays empty).
`scripts/wasm-smoke.mjs --index-notes N` checks that the index lies only in its own database, and
`--cache-quota` (a reduced quota through DevTools, `Storage.overrideQuotaForOrigin`) — that its
failed save stops nothing else.

Each mount is a separate IndexedDB database named after its path. The flow:

1. Start: mounting and loading from IndexedDB (IDBFS `syncfs` for each mount separately — the way
   `FS.syncfs` does it, but with our own completion, so that the cache mount can fail
   on its own —, asynchronously). Only
   after the `ready()` signal does the app open the vault (`main.cpp`); the loading screen then shows
   "Loading notes…".
2. Every change in these directories (closing a file being written, rename, delete,
   mkdir, utime…) is caught at the level of Emscripten's `FS` API and schedules a save to
   IndexedDB after 400 ms of quiet, at most 3 s after the first unsaved change. The core does not
   need to know anything; `scheduleFlush()`/`flushNow()` are available for manual control.
3. When the tab is hidden/closed (`visibilitychange`, `pagehide`) pending changes are saved
   at once; `beforeunload` asks to stay on the page if a save is still running.
4. A save never starts before a successful load — an empty state in memory would delete the notes.
   When IndexedDB is unavailable (some private modes, blocked site data),
   the app works in memory only, `isBackedByIndexedDb()` = false, and the `error()` signal
   lets the UI warn the user.
5. After a crash of the WebAssembly module the HTML shell still saves the state from memory (the JS part lives on).
6. **One tab at a time.** Each tab has its own copy of the files in memory, and a save removes from
   IndexedDB whatever the tab does not have — two tabs would delete each other's new notes.
   That is why the page holds a Web Locks lock for its whole lifetime; a second tab waits on the
   loading screen ("Brulion is open in another tab. Close it to continue
   here.") and starts by itself when the first one is closed. A page going into the
   back/forward cache (bfcache) saves its changes, releases the lock and reloads on return.
   Without Web Locks (very old browsers) there is no protection.

Notes:

* **Files written before `ready()` are lost** (the loaded state replaces them).
* **Data eviction:** the browser may delete a site's data when space runs out, and Safari
  deletes it after 7 days without a visit unless the page is added to the home screen.
  `WasmStorage::requestPersistentStorage()` asks for `navigator.storage.persist()` (Firefox
  asks the user — call it from a user action). Regular export/sync is the
  only full backup.
* **One origin:** all pages of one origin share IndexedDB — e.g. on GitHub
  Pages all projects `<user>.github.io/*`, and from disk all `file://` files
  (above, "Single HTML file"). Another page of that origin could read the notes — for private
  data it is best to host Brulion on your own domain (or subdomain).
* Qt on WebAssembly keeps settings via `QSettings` (native format) in `localStorage`.
* **Pictures from the internet** (docs/SYNTAX.md §2): the cache
  (`QStandardPaths::CacheLocation`/web-pictures) is not an IDBFS mount — it lives in the tab's
  memory (MEMFS) and disappears with it; a fetched picture has to be shown again after the tab is
  reopened (or saved in the notebook, then it is an attachment in IndexedDB). The limit
  is 50 MB instead of 200 MB, and the `data:` addresses of pictures evicted from the cache disappear from
  memory together with the files.
  * **CORS**: the request for a picture is sent by the browser (Qt for WebAssembly: `emscripten_fetch`),
    and the Brulion page gets the response only when the picture's server allows it
    (`Access-Control-Allow-Origin`; e.g. `upload.wikimedia.org` sends `*`, most sites do not).
    The browser does not tell the page why a request failed, so a failed connection
    is described as "%1 does not let other websites load its pictures (CORS), or there is no
    connection…" with the advice to save the picture in the notebook in the app on a computer or phone
    (`Workspace::setWebPageScheme`, set from `location.protocol` — `platform::pageScheme()`).
  * **Mixed content**: Brulion opened over `https://` will not fetch anything from `http://`
    (the browser blocks such a request) — the message says so plainly.
  * **Simple requests**: in the browser `PictureFetcher::setSimpleRequests` does not set
    `User-Agent` or `Accept-Language` (the browser sends its own; another header would need
    an OPTIONS preflight request, which picture servers usually do not answer), only
    `Accept: image/…` stays (a CORS-"safelisted" header). Qt rejects forbidden headers anyway
    (`Cookie`, `Referer`…); cookies are not sent (`withCredentials` off).
  * **Redirects** are followed by the browser itself: Qt for WebAssembly does not emit the
    `redirected` signal, so the manual redirect check (`webpicture::redirectAllowed`) works
    only in the app. The browser will not go from `https://` to `http://` anyway (mixed
    content), and Chrome restricts requests from internet pages to private network addresses
    (Private Network Access).
  * **Export** with "Include copies" fetches missing pictures the same way — with the same
    CORS limits; a picture that cannot be fetched stays an address in the export. Copies
    are at most 256 MB in total (the browser tab's memory; a single HTML file also holds them
    as `data:` text), further ones stay addresses.
  * **Checked automatically** in Chromium 156 and Firefox 157 (`tests/web/webpictures.spec.mjs`,
    GitHub Actions, 2026-10-30; "Browser tests" above): a picture from a server with CORS
    (with Polish characters in the address) is fetched by the editor by itself and drawn; without CORS — the message names CORS
    and the server; `http://` on an `https://` page — a message about http/https, and the request **never leaves
    the browser** (the `http://` server gets none, also after a redirect from `https://`,
    which the browser does not explain to the page: the message then talks about CORS); 404 and an HTML page
    instead of a picture — their own messages; **no preflight request (OPTIONS)** in either
    browser, and the GET has the page's `Origin`, only `Accept: image/avif,…` from Brulion (without
    `Accept-Language: *` and `User-Agent: Mozilla/5.0`) and no cookies; the HTML export (single
    file) with "Include copies" contains the picture from the cache and a picture it fetched itself
    (from another page of the notebook), the rest stay addresses with a warning for each, and the file
    reaches the browser (Chromium: File System Access, Firefox: download). On an
    `http://` page (insecure context: no Web Locks and no File System Access — the app still
    starts) an `http://` picture loads, and the export arrives as a download. On a real
    phone, what remains is what docs/ANDROID.md "Pictures from the internet" describes.

### Backups in the browser

Backups (`WorkspaceBackups`, docs/ARCHITECTURE.md "Data safety") live in a fourth mount,
`WasmStorage::backupsRoot()` (`<CacheLocation>/backups`, a subfolder per notebook). It is a mount like
the index cache — a separate database, a separate transaction, so `QuotaExceededError` does not touch notes,
history or settings — with three differences, because these files belong to the user:

* a failed save does not delete anything from memory wholesale: `backupsNotSaved()` removes only the new backup
  (the failed transaction leaves older backups in IndexedDB), and the status and message say it did not fit;
* removing the oldest backups and restoring after a safety backup wait until the new backup is saved to
  IndexedDB (`flushed(true)` with no pending changes → `WorkspaceBackups::storageFlushed()`), so that
  a failed save does not cost the older backups; with no answer within 60 s the restore is aborted;
* a mount that failed to load is not saved until the end of the session (`brulionStorage.unloaded`):
  its empty state would delete what is in IndexedDB. Saving such a mount would achieve nothing, so
  backups and restore are then off, and the settings say so permanently
  (`WasmStorage::backupsLoadError()` → `WorkspaceBackups::setStorageUnavailable()`; the event arrives
  before the controller is created, which is why main.cpp asks for it at startup).

The temporary file is created in `/tmp` (MEMFS outside the mounts): change tracking would save a growing file
over and over. At the end it is copied into the mount, so for a moment the archive is in memory twice.
The largest archive: half of the free space from `estimate()`, at most 512 MB. Writing proceeds piece by piece
on the GUI thread: the smallest piece is a file, and a file over 4 MB is stored without compression so that
a piece does not take long.

All IDBFS files are loaded into the tab's memory at startup, backups too — which is why automatic
backups are **off by default** here, the default limit is 2, and 1–3 can be chosen. Here backups protect
against a mistake, not against clearing the browser's data; the settings say so plainly, and "Download"
hands out a backup through `wasmfiles.h`.

## Sharing to Brulion

The browser version **installed as an app** appears in the system **Share** menu:
in Chrome, Edge or Samsung Internet on Android (browser menu → **Install app**
or **Add to Home screen**) and on ChromeOS. You send a page from the browser (its title and address) or
selected text from another app through **Share → Brulion**; it opens the same
**Save to Brulion** dialog as the Android app (docs/ANDROID.md "Share → Brulion"):
text to edit, **Today's journal** or **New page**, nothing is saved before
**Save**. Dates in the shared text do not create reminders.

* Text and links only. Files (`.md`, `.txt`) can be shared with the Android app; the browser
  version does not accept them (a share target with files requires sending them to a server
  or to a service worker, which a static server cannot replace).
* Brulion is already open in another tab: the share goes to that tab (the dialog opens
  there), and the new tab does not load the app, it only says "Sent to Brulion open
  in another tab." and closes itself when the browser allows it. When that tab does not answer
  within 4 s (frozen, an older version of Brulion), the new tab starts as usual and waits until the other
  is closed ("Brulion is open in another tab…"), then shows the dialog.
* On Android the installed app usually opens the share in its single window:
  an open Brulion reloads with the shared text (saved notes are safe;
  with a change that is still being saved, the browser may ask whether to leave the page).
* Without a notebook (first start) Brulion first asks to open or create a
  notebook — the share waits.
* Firefox, Safari (also on the iPhone) and browsers without installing as a WebAPK (e.g. Brave)
  do not support share targets — Brulion will not appear in their **Share** menu.

Technically: `wasm/manifest.webmanifest` has a `share_target` with method GET and action `./` (the
app itself: works without a service worker — a static server (e.g. nginx) serves `index.html` for it — and offline
from its cache) and parameters `share-title`, `share-text`, `share-url`. `index.html` takes them
from the address bar (`history.replaceState`: a reload does not share a second time; other parameters
stay) and passes them to the app as `--share-query` (form-encoded); the query
is read by the core (`notebook::sharedContentFromQuery`, `src/core/notebook/share.h`: `+` is a space,
`%XX` are UTF-8 bytes, invalid sequences → U+FFFD, unknown keys ignored, text up to 1,000,000
characters), and `AppController::receiveShareQuery` opens the dialog. When another tab holds the storage lock
(Web Lock `brulion-storage:…`, below "Data in the browser"), `index.html` sends
`{type: "share", id, query}` over the `BroadcastChannel` "`brulion-share:<page path>`";
only the tab holding the lock answers (`WasmShare`, `src/platform/wasm/wasmshare.h`, listens from
startup, before it asks for the lock) — `{type: "taken", id}` — and passes the query to
`receiveShareQuery`. No `launch_handler`: Chrome with `focus-existing` loses the share data.
Test: `scripts/wasm-smoke.mjs --share` (above); manually on a phone — TODO.md.

## Notifications (reminders)

`WasmNotifier` (`src/platform/wasm/wasmnotifier.h`) uses the Notification API:

* `permission()`, `requestPermission()` — browsers show the permission prompt only in
  response to a user action (a button click in the UI),
* `showNow(id, title, body, path, line, alarm, silent)` — alarms stay on screen until
  dismissed (`requireInteraction`) and vibrate where possible; `silent` — the `silent` option
  (no sound or vibration); the same `id` replaces the previous notification,
* `playTone(alarm)` — a tone in the page itself (Web Audio): an alarm with the "Default" sound beeps
  for a few seconds. The notification sound is chosen by the browser/system, so in the browser
  *Settings → Reminders* offers only "Default" and "Silent". Limitations: only with the tab
  open and only after the user's first interaction with the page (the browsers' autoplay
  policies) — otherwise the tone is not heard,
* `activated(id, path, line)` after a click (the page gets focus),
* `firesWhenClosed()` = false: the browser notifies of nothing with the tab closed —
  the times are tracked by the core (`ReminderScheduler`) in the open app.

Chrome on Android does not let a page create notifications (`new Notification` →
"Illegal constructor"); then the notification goes through the service worker registration, and
`sw.js` passes the click to the open tab (`postMessage`) or opens the app with
`?brulion-id=…&brulion-note=…&brulion-line=…` (`WasmNotifier::takeLaunchActivation()`).

## Nextcloud (WebDAV) and CORS

The browser lets the Brulion page (e.g. `https://notatki.example.com`) connect to
`https://chmura.example.com/remote.php/dav/…` only when the server answers with
**CORS** headers (also to `OPTIONS` preflight requests for `PROPFIND`, `PUT`, `MKCOL`, `DELETE` with
the `Authorization`, `Depth`, `If-Match` headers). Nextcloud does not send them for
WebDAV by default, so sync from the browser version will not "just work". Options:

1. **The same origin (recommended):** host `dist-wasm/` on the same server as Nextcloud,
   e.g. `https://chmura.example.com/brulion/` (reverse proxy / an extra `location` in nginx
   as above). Requests are then same-origin, CORS is not needed, and the data in IndexedDB is
   separated from other sites.
2. **CORS headers on a reverse proxy in front of Nextcloud** — only for a specific origin
   (`Access-Control-Allow-Origin: https://notatki.example.com`, never `*` with
   authentication), `Access-Control-Allow-Methods: GET, PUT, DELETE, PROPFIND, MKCOL,
   MOVE, COPY, OPTIONS`, `Access-Control-Allow-Headers: Authorization, Content-Type, Depth,
   Destination, If-Match, If-None-Match, Overwrite`, `Access-Control-Expose-Headers: ETag,
   OC-ETag, DAV`, a `204` response to `OPTIONS` without requiring login. Note: this opens
   WebDAV to scripts from that origin (on a shared origin, e.g. GitHub Pages — from every project
   of the user).
   There are also Nextcloud apps that add CORS for WebDAV (e.g. WebAppPassword) —
   check their current state and scope before using them.
3. **Native apps:** the NixOS and Android versions are not subject to CORS and sync
   with Nextcloud without any changes on the server side (and Syncthing is available only there).

Use a Nextcloud app password (Settings → Security), not your main password.

## Fonts: emoji and symbols

The text itself is in Brulion's bundled fonts, as on every platform (docs/UI.md "Look & feel":
Inter, Literata, JetBrains Mono from `assets/fonts/ui/`, `src/app/ui/uifonts.h`).
Qt for WebAssembly does not see the system's fonts. It has only two of its own, DejaVu Sans and DejaVu
Sans Mono (which is why in the browser Settings → Appearance and Editor have no choice of the
interface, note or code font; accent colour, background and density work as everywhere;
compiled into `brulion.wasm`; the computer's fonts could be loaded only through the
Local Font Access API, only in Chromium and after the user's consent, which Qt does not ask for
by default). DejaVu has none of the emoji Brulion uses: reminders ⏰, alarms 🔔,
task markers 📅 ⏳ 🛫 ✅ ❌ 🔁 🔺 ⏫ 🔼 🔽 ⏬ ➕, and ⧉ of embedded blocks — they were all
**empty boxes** (white in the dark theme), in notes, the editor, settings and
suggestions. Lucide icons (`BIcon`, SVG) worked correctly.

That is why the app contains two small fallback fonts from `assets/fonts/` (subsets of Noto Emoji
and Noto Sans Math, SIL OFL 1.1 licence, made by `scripts/fallback-fonts.py`) and in the
browser always installs them at startup as fallback fonts: text stays in DejaVu, and
only the missing characters come from them. Emoji are monochrome, in the text colour. Details,
rules on other platforms and the list of characters: docs/UI.md, "Emoji and symbol fonts". Test:
`scripts/wasm-smoke.mjs --glyphs`.

## PDFs in the browser

The browser has no Qt PDF module (Qt PDF has no WebAssembly version), and pdf.js would be several MB
more; on the other hand every browser has its own PDF viewer. That is why a PDF embedded in a note
(`![[wykład.pdf]]`) is a file tile with a page icon, and clicking it — or a link
`[[wykład.pdf]]` — opens the PDF in a **new tab** (docs/UI.md "PDF viewer"): the file's bytes from IndexedDB
as a `blob:` address of type `application/pdf` (`src/platform/wasm/wasmopen.cpp`; the type is given explicitly,
so a file that is HTML inside never becomes a page with Brulion's origin),
`window.open` in the click handler (browsers open tabs only on a user gesture; Qt
handles input events in the browser's handler), `opener` cut off. Scrolling,
zooming, searching and "Save" are provided by the browser (Chrome, Firefox — pdf.js —, Safari). The last three
`blob:` addresses are kept (reloading the tab with the PDF and its "Save" need them),
older ones are released; they disappear with Brulion's tab. The app's route does not change.

* **Blocked tab** (Safari can be stricter, pop-up blockers): the PDF is downloaded
  (`saveFile`), and the app says "The browser blocked the new tab: the PDF was downloaded instead.".
* **A route to a PDF** (tree, quick switcher, history): the panel "Brulion here does not draw PDFs:
  the browser's own viewer opens it in a new tab (scrolling, zoom, search)." with the
  **Open in a new tab** button (a real click: a later Back does not
  open tabs by itself).
* A file larger than the attachment limit (200 MB) — refused with a message (memory: bytes and blob).
* Brulion never makes a `data:` address from a PDF (it used to do so for every embedded file).

Tests: `tests/web/pdf.spec.mjs` (Chromium and Firefox; `?selftest=pdf`, above): a click on the tile and
on the link opens a tab with a `blob:` address of this site, `content-type` `application/pdf`, bytes as
in `tests/data/pdf/wyklad.pdf` (they start with `%PDF-`), `window.opener` empty, the app's route
still the journal; a route to a PDF shows the panel with "Open in a new tab"; opening without a click
(`?selftest=pdfauto`) ends with a tab or a download with a message — the result for each browser
in a report annotation. Playwright's Firefox has pdf.js turned off in the test profile, so there the new tab
turns into a download of the same `blob:` address and closes: the test then checks the downloaded file
(address, bytes, name `wykład.pdf`) instead of the tab's document. Locally: `scripts/web-test.sh
pdf.spec.mjs`. Left for manual testing: Safari on iOS and Firefox with pdf.js turned on.

## Size

`MinSizeRel` build (Qt 6.11.2, emsdk 4.0.7), full app:

| File | Raw | gzip -9 | brotli -q 11 |
|---|---|---|---|
| `brulion.wasm` | 40 259 601 B (38.39 MiB) | 13 670 465 B (13.04 MiB) | 9 403 485 B (8.97 MiB) |
| `brulion.js` | 275 745 B (0.26 MiB) | 64 196 B | |

Of this, about 26.2 MB is code, 13.8 MB data (compiled QML, Qt resources: fonts, Unicode
tables). Measured on 2026-10-18; `scripts/wasm-build.sh` prints the current values after
every build. The first visit therefore downloads about 9–13 MB (depending on the server's compression),
later starts come from the service worker cache (locally < 1 s).

The emoji and symbol fallback fonts (above, "Fonts: emoji and symbols") add
+190 272 B of raw `brulion.wasm` (+118 111 B after gzip -9, +103 429 B after brotli; measured on
2026-10-22, the same toolchain before and after).

The bundled text fonts (Inter, Literata, JetBrains Mono: eleven subsets, `scripts/ui-fonts.py`) add
1 415 380 B of resources (693 676 B after gzip -9; fonts barely compress) — about 0.7 MB more to
download on the first visit, for the same letters as on the desktop and Android instead of DejaVu.

The browser build contains no developer QML (the gallery, the editor playground, the
`--editor-script` scripts and self-tests; option `BRULION_DEV_QML`, docs/UI.md): −597 014 B of raw
`brulion.wasm` (−160 008 B after gzip -9, −104 716 B after brotli).

Without the unused Qt Quick Controls styles, QtQuick.Dialogs and Qt Widgets (2026-10-30: QML imports
`QtQuick.Controls.Basic`, file picking and downloading through `wasmfiles.h`; docs/PERFORMANCE.md, L9):
`brulion.wasm` 47 225 371 → 41 909 609 B raw (−5.07 MiB), 15 803 378 → 13 498 551 B after gzip -9
(−2.20 MiB), 10 841 274 → 9 231 654 B after brotli (−1.54 MiB); the first visit at 20 Mbit/s (gzip)
draws the first frame after 7.3–7.4 instead of 8.4–8.5 s. `scripts/wasm-build.sh` stops the build when
the app's list of QML modules again contains another style or QtQuick.Dialogs.

## On-screen keyboard (phone)

The editing toolbar should sit exactly on the on-screen keyboard. The page asks the browser
for `interactive-widget=resizes-content` (`wasm/index.html`): an open keyboard shrinks the whole
page, and with it the Qt screen (`#screen`) and the app window, so the toolbar at the bottom edge of the window sits
on the keyboard, and the app lifts nothing (overlap 0). The browsers' default behaviour
(`resizes-visual`: the page stays, only the visual viewport shrinks, `window.visualViewport`)
does not work in Brave: Brave on Android hides its bottom bar when the keyboard opens, but
`visualViewport.height` still subtracts it
([brave-browser#60074](https://github.com/brave/brave-browser/issues/60074); on a Pixel
412×915: visible 433.9 px, `visualViewport.height` 385.9 px) — the app lifted the toolbar about
48 px too high and an empty strip remained between it and the keyboard. No page API shows the
real value; the page size that the browser determines itself is right. The cost is
a relayout of the window when the keyboard opens and closes (on the emulator: one change of the
Qt window's height each way; in Chrome and Brave one more when the browser's autofill bar
appears before the keyboard, below). `resizes-content`
is supported by Chrome ≥ 108, Brave, Edge, Samsung Internet and Firefox ≥ 132; iOS Safari and older
browsers ignore it.

Where the browser shrinks only the visual viewport (iOS Safari, older browsers,
`?keyboard=visual`), the app lifts the content by the part of the Qt screen that the visual viewport does not
cover: height of the `#screen` container − (`visualViewport.offsetTop` + `visualViewport.height`).
It is computed relative to the Qt container, not `innerHeight` (Chromium reports a stale
`innerHeight` while the page is being resized: the old formula `innerHeight − height − offsetTop` then counted the keyboard
twice). A page zoomed with a gesture (`visualViewport.scale` > 1.01, e.g. a touchpad on a computer),
invalid values (NaN, height 0 after rotation) and differences below 2 px (rounding) are not a keyboard.

The shell (`watchVisualViewport` in `src/platform/wasm/wasmshell.cpp`), on `visualViewport`
events (`resize`, `scroll`), window `resize`, focus changes and size changes of the
Qt container (`ResizeObserver`), passes on raw readings (`innerHeight`, `visualViewport`,
container height, touch screen, focus in Qt's keyboard field) every animation frame until the values
have not changed for 6 frames (the last reading: "settled"); C++ does the maths (`ViewportKeyboard`
in `src/app/ui/keyboardinsets.h`, tests `tst_keyboardinsets` with sequences recorded
in browsers). The keyboard counts as shown (`shown`: the bottom navigation hides at
once — the page changes size in a single step, so the navigation does not wait under the sliding keyboard
as on Android, `KeyboardInsets.slidesOver` is false here —, messages go to the top;
a hidden keyboard brings the navigation back, and the editing toolbar, while typing goes on, sits on it) when there is
overlap or — when the browser shrank the page — on a touch
screen with focus in a Qt field, when the visual viewport is at least 120 px lower than the highest
seen at this width (rotating the phone starts over; the browser's address bar, 56 px, is
not a keyboard). Focus moving between Qt elements (Enter: the next block) does not hide it until
the readings settle; a settled reading without focus in a Qt field hides it (editing finished — on
a split screen the page stays lower without a keyboard). A phone rotated with the keyboard
open (checked on the emulator): Chrome and Brave hide the keyboard, and the block stays
being edited — the first height in landscape is the full one; Firefox keeps the keyboard, but first
reports the landscape page without it (301 px), then with it (62 px). In both cases the highest
height at the new width is the page without the keyboard. `animating` lasts until the readings
settle: the cursor is scrolled into view only after that. `KeyboardInsets` (docs/UI.md "On-screen keyboard") lifts by the overlap the view, the editing
toolbar, bottom sheets, menus and messages — just as on Android. Without
`visualViewport` (old browsers) nothing is reported, and Qt does not know about the keyboard
(`keyboardRectangle` in the browser is empty).

The hidden text field through which Qt receives typing (`.qt-window-input-element`) is placed by Qt
in the top-left corner of the edited text; after tapping a block low on the screen the browser
scrolled the view to it (Chrome: the app's top bar escaped off screen, iOS scrolled the page),
and under a window shrunk by the keyboard it scrolled Qt's containers. On touch screens the shell
pins it in the top-left corner of the window (a style in the open shadow root `#qt-shadow-container`), and
resets scrolled Qt containers to 0; a page scrolled by iOS is scrolled back to the top.
Candidate windows of CJK keyboards therefore appear in the corner of the window.

Chrome and Brave put their own autofill bar above the keyboard of every text field
(passwords, cards, addresses; 48 px in Chrome, 64 px in Brave — a strip with three icons on the right): Brulion's
toolbar then sits on it, and it sits on the keyboard. It is part of the browser, not of the page
(`visualViewport` and the page size do not include it). Checked on the emulator: `autocomplete="off"`,
the field's name and labels do not hide it; only a search field (`type="search"`) hides it, but
in it the keyboard started a word typed in the middle of a sentence with a capital letter — so Qt's field
stays a text field.

Known limitations: a browser that ignores `interactive-widget` and has Brave's bug will still
leave the strip (it cannot be detected). A browser that, after the phone is rotated, at once reports the page
with the keyboard (none of the three checked) will remember it as the highest at that width:
the keyboard then counts as hidden until it is dismissed once (the toolbar still stays at the
bottom of the window, i.e. on the keyboard). A new tab in Brave briefly gets a view as if the
bar were hidden ([brave-browser#59940](https://github.com/brave/brave-browser/issues/59940)) — that is a Brave
bug. iOS Safari — "best effort" only, without an automated test on iOS: the
`visualViewport` path with the pinned field, checked by `scripts/wasm-smoke.mjs --keyboard`
and `--keyboard-phone` (above).

Test in real browsers: `scripts/android-emulator-test.sh --web-keyboard dist-wasm`
(Android 16 emulator with KVM, Pixel 6; job `keyboard-android` in `.github/workflows/wasm.yml`).
It serves the site under `/brulion/` (`scripts/web-test-server.py`; in the emulator `http://10.0.2.2`,
which the browsers treat as a secure context: Chrome and Brave through
`--unsafely-treat-insecure-origin-as-secure`, Firefox through `dom.securecontext.allowlist`),
in Chrome from the system image, Brave 1.97.59 (the version with #60074) and Firefox 157.0.1 (pinned APKs
with SHA-256 sums) it writes a journal of 40 Polish blocks, opens
`?selftest=keyboard&report=…` (and with `&keyboard=visual`), taps a block and waits for the keyboard.
In this test the app paints the keyboard's area purple and the toolbar
turquoise (`KeyboardInsets.marked`): in the screenshot, above the keyboard's top edge (the IME frame
from `dumpsys window`) there must be no purple rows (a strip; 3 px allowed), only ≥ 20
rows of the toolbar — at most above the browser's autofill bar (≤ 72 dp) — and according to the
app the toolbar ends at the bottom of the Qt window, which fills the page. A word typed with the
keyboard's keys reaches the block, and the keyboard's action key is Enter, which splits the block
(`focusText` in the test result). Then the phone is rotated to landscape with the keyboard open: when the
keyboard stays (Firefox), it counts as shown, and the toolbar ends at the bottom of the window; when
the browser hides it (Chrome, Brave), the bottom navigation comes back and the toolbar sits right on it; after rotating
back the keyboard counts again (Chrome and Brave: after tapping a block). After "Back" (keyboard
hidden) nothing purple remains, the bottom navigation comes back, and the toolbar sits right on it (`toolbarBottom` =
`bottomNavTop` in the test result). With `?keyboard=visual` the result is only
recorded (Brave shows the strip from #60074).
The script records the readings (`samples-<browser>-<mode>.jsonl`), prints them as rows for the
`tst_keyboardinsets` test and counts how many times the Qt window changed height when the keyboard opened.

Result from 2026-10-28 (Android 16, Pixel 6, 1080×2400, density 420): in Chrome 133, Brave 1.97.59
and Firefox 157.0.1 the toolbar sits on the keyboard (Firefox: 0 px; Chrome and Brave: on their
autofill bar, 126 and 168 px, without a single pixel of the page between them), the Qt window
shrinks once on opening and once on closing; with `?keyboard=visual` Brave shows a strip of
124 px (#60074: it reports an overlap of 376 px with 328 px actual), Chrome and Firefox do not. Rotation
(2026-10-29): Chrome and Brave hide the keyboard (landscape page 866×308 and 865×307 px), Firefox
keeps it (first a 871×301 px page without it, after a moment 871×62 px with it); the toolbar sits at the bottom of the window
in Firefox, and in Chrome and Brave on the bottom navigation, which comes back after the keyboard is hidden (60 px
above the bottom of the window). Non-ASCII characters typed on the keyboard (à, ß, ç, ñ — long-press in Gboard's English
layout; the emulator image has no Polish layout, and downloading it requires Gboard settings)
reach the block unchanged in all three (checked manually with a script, not in the test).

## Stylus (pressure)

The stylus reaches the drawing editor in the browser like this: pointer events (`pointerType: "pen"`,
`pressure`, `tiltX/Y`, `buttons`) on the Qt window element → Qt for WebAssembly
(`QWindowSystemInterface::handleTabletEvent`, a single pen-type device; pressure 0 on
lift) → `QTabletEvent` → `DrawingCanvas` (src/app/drawing/drawingcanvas.h). **The other end of the
stylus (eraser)** is reported by the browser as button 5 (`buttons & 32`), which Qt passes on as
`Qt::ExtraButton3` — the canvas treats it as an eraser (it used to draw like the pen); the barrel button
(`buttons & 2`) is `Qt::RightButton`, also an eraser.

Checked automatically (`tests/web/stylus.spec.mjs`, "Browser tests" above):

* Chromium — real pen events from DevTools (`Input.dispatchMouseEvent`, `pointerType:
  "pen"`, `force`): a hovering pen does not draw; a line with pressure 0.1 → 0.9 keeps its pressure
  (`simulatePressure: false`, from ≤ 0.15 to ≥ 0.85) and gets thicker in the screenshot (at 85 %
  of the length at least 1.3 times thicker than at 15 %: 4 → 8 px); constant pressure means no pressure
  (a line as with a mouse); a mouse line has no pressure; the barrel button erases; the saved file has pressure
  at every point.
* Chromium and Firefox — pen events created by the page (Playwright has no pen for
  Firefox; the test page lets `setPointerCapture` of the made-up pointer through): pressure
  0.2 → 1.0, out-of-range pressure (1.7 and −0.3) clamped to 0…1, the eraser (button 5) erases a line,
  does not draw on an empty sheet, the mouse draws without pressure.

The test also found a bug in the whole app (not only in the browser): the shell's notification (toast) host
lies in the Qt Quick Controls popup layer over the whole window, and that layer is visible when
a Drawer exists — the canvas took it for an item above itself and left the stylus events to it,
so the stylus drew only through the mouse events that Qt makes from them (without pressure; on Android
Qt does not make them). Now only a popup, and whatever in that layer accepts input (a toast), covers it.

Left for manual testing: a real stylus in a real browser (an Android tablet, Natalia) —
whether the browser passes on pressure and the other end of the stylus.

## Technical limitations

* The command line (`brulion-cli`, docs/CLI.md) and receiving commands from it (`app open`) exist only
  on a Linux computer; an agent reads notes from the browser through Nextcloud.
* Single-threaded Qt without asyncify/JSPI: no nested event loops (`QDialog::exec()`,
  `QEventLoop::exec()`); dialogs and operations must be asynchronous (signals, `open()`).
* Files from/to the user: our own browser file picking (`platform/wasm/wasmfiles.h` over
  Qt's private `QWasmLocalFileAccess`; `showOpenFilePicker`, and where it is missing, `<input
  type=file>`): several files at once for import, "Image…" and "File…", files over the limit (as on the
  desktop: 512 MB for import, 200 MB for a picture) skipped without reading, and reading stops at the
  file with which the selection would exceed the total (import: 1.5 GB as on the desktop, pictures: 512 MB
  — the browser holds all read files in memory before they reach the notebook;
  `PickCollector`, `platform/pickedfiles.h`, checked on every platform); saving — the browser's save
  dialog (`showSaveFilePicker`, Chromium on https) or a plain download. No Qt Widgets
  (`QFileDialog::getOpenFileContent()` needs them), and Qt's own file dialog for the browser
  gives `weblocalfile:` addresses, which the single-threaded build cannot read (it needs JSPI or
  asyncify). Cancelling an `<input>` without a `cancel` event (older browsers) simply changes
  nothing.
* **No PDFs as a drawing backdrop** (docs/UI.md "Drawing editor", "Backdrop"): Qt has no Qt PDF
  for WebAssembly (the Qt 6.11.2 repository has no `qtpdf` for `wasm_singlethread`), and pdf.js
  would add more than 2 MB and a bridge to JavaScript — so "Drawing on a picture or PDF…" and "Backdrop from a picture
  or PDF…" accept only pictures here (our own browser file picking,
  `DrawingBackdrops.pickInBrowser`), and the backend `src/platform/pdfpages_none.cpp` says there are no
  PDFs. Drawings on PDF pages made on another device look and edit normally: the picture of the
  page is in the drawing file.
* **No spell checking** (docs/UI.md "Spell checking"): Hunspell with dictionaries would add several MB
  to the download, and the browser's spell checking does not reach text fields drawn by Qt
  on the canvas — the backend `src/platform/spell_none.cpp` has no languages, so the spelling settings are hidden.
* WebGL 2 required (the shell warns when it is missing and lets you try anyway).
* Memory: 50 MB at start, growing up to 4 GB (Qt defaults).
* Network only through `QNetworkAccessManager` (the browser's fetch) and under CORS rules.
* **Clipboard.** "Copy link" (block menu, command palette, Ctrl+Shift+C; docs/UI.md "The
  block editor") writes through `QClipboard`, which in Qt for WebAssembly uses the browser's asynchronous
  Clipboard API: it needs a secure context (https or `localhost`) and a user
  gesture (clicking a menu item is one). Qt does not report the browser's refusals — Brulion
  says "Copied …" only when the text comes back from Qt's clipboard, so it will not detect a refusal
  by the browser itself (not yet checked manually in Firefox and Safari). Ctrl+Shift+C in Chromium and
  Firefox opens the developer tools before the page gets the keys — then the block menu
  or the command palette remain (Ctrl+K → "Copy block reference").
  The same goes for copying selected blocks (docs/UI.md "Selecting and copying": Ctrl+C / Ctrl+X,
  "Copy" in the selection bar and in the block menu): `QClipboard` gets Markdown
  as text/plain and — when the browser accepts it (Clipboard API with `ClipboardItem`; Firefox and Safari
  vary) — HTML as text/html, created at once (on Linux only when another app asks for it:
  the browser takes the data at the moment of copying, `clipboardReadsAtOnce()`); copying happens
  in a key or click handler, so it counts as a user gesture. The check read after copying (on the desktop and Android
  it guards against "Copied" without a copy) guarantees nothing in the browser: Qt writes to the
  browser's clipboard asynchronously and returns its own copy of the data, and a refusal (no gesture, http,
  Safari, Firefox without `ClipboardItem` with HTML) is only logged — the "Copied" toast may then be
  untrue. There is no "primary selection" clipboard (X11) in the browser.
  Pasting: Ctrl+V reaches the page as a `paste` event, from which Qt takes the clipboard's text and HTML
  (Brulion turns HTML from other apps into Markdown); the edited text's menu has no
  "Paste" in the browser — the page cannot read the clipboard by itself without permission. Copying selected
  blocks (Ctrl+C) is checked headless in Chromium by `scripts/wasm-smoke.mjs --copy` (2026-10-23,
  Chromium 155: Markdown as text/plain and Brulion's HTML as text/html in the browser clipboard);
  pasting and other browsers (Firefox, Safari) have not been checked manually.
* **No repeating 0 ms timers.** The Qt 6.11.2 event dispatcher for WebAssembly
  (`QEventDispatcherWasm::sendTimerEvents()`) sets the browser's next `setTimeout` only
  when `QTimerInfoList::activateTimers()` returns > 0, and that function does not count timers with an
  interval of 0. A repeating `QTimer` with interval 0 therefore fires once and stalls **all**
  Qt timers until another timer is started/stopped or input arrives
  (mouse movement, a key). That is how the vault index in the browser stopped halfway (370–570 of 1000
  notes not indexed after 60 s without touching the tab — in Chromium and Firefox; the journal
  showed "Reading the notebook…", search, links, tasks and reminders were
  incomplete), and only mouse movement pushed it on by a few batches. That is why `Workspace` indexes
  in the browser with a 1 ms timer (the browser waits ≥ 4 ms between successive `setTimeout`s anyway).
  Safe are: an interval ≥ 1 ms, single-shot timers (stopping a timer sets
  `setTimeout` again), `QTimer::singleShot(0, …)` and queued calls (they go through events, not
  timers). Test: `scripts/wasm-smoke.mjs --index-notes` (above).
  Minimal case: a `QCoreApplication` with a single repeating 0 ms `QTimer` — the call
  counter stops at 1 (with a 1 ms interval: ~230 calls/s); an extra 250 ms timer
  then does not fire even once (next to a 1 ms timer: 4 times per second).
* **Qt timers and a `setTimeout` fired too early.** The Qt 6.11.2 event dispatcher
  (`QEventDispatcherWasm::updateNativeTimer()`) sets one browser `setTimeout` for
  the nearest Qt timer with the wait truncated to whole milliseconds, and sets the next one
  only when some timer has fired (`sendTimerEvents()`). When the `setTimeout` arrives
  before the timer's deadline (Firefox keeps `setTimeout` to the millisecond, and the
  `performance.now()` clock, from which Qt computes time, has a coarse resolution), no
  timer fires and no new `setTimeout` is created: **all** Qt timers stall until another timer
  is started/stopped — input (mouse movement) does not wake them. That is how in Firefox
  (2026-10-30, laptop: 2 of 30 and 1 of 15 runs of the stylus test) the drawing editor did not save
  the last line (saved 1.2 s after a change), even though the canvas had it. That is why `index.html` adds
  a 2 ms margin to every page `setTimeout` with a delay > 0 ms (`TIMEOUT_MARGIN_MS`; 0 ms —
  Qt's wake-ups — stays): after this change 0 of 60 runs. Test:
  `tests/web/shell.spec.mjs` (a browser with every `setTimeout` 1 ms early — without the margin the
  save in Chromium never arrives; Firefox fires its `setTimeout`s late enough here to
  make it anyway). 2 ms cover the truncation (< 1 ms) and the coarse clock (< 1 ms), and the 1 ms
  indexing timer is not slowed down: the browser defers nested `setTimeout`s by ≥ 4 ms anyway.
* Compiled code: `index.html` gives `WebAssembly.compileStreaming()` the original `fetch`
  response (it reads download progress from a copy, `response.clone()`), because only a response with
  a URL can go into the browser's compiled code cache. Chrome
  then stores the compiled module on the first visits (with a response from the service worker
  next to it in Cache Storage) and from the third visit on does not compile functions again: the first
  frame 0.85 → 0.63 s, main-thread work 1.78 → 1.28 s (laptop, 1000 notes); with a
  4× slower CPU (like a phone) 2.47 → 1.77 s and 6.7 → 5.4 s. When the server sends a type other
  than `application/wasm`, the shell creates a response with that type (required by
  `compileStreaming`) — then without this cache.

## Files

| Path | Contents |
|---|---|
| `wasm/index.html`, `wasm/sw.js`, `wasm/manifest.webmanifest` | HTML shell, service worker, manifest |
| `wasm/icons/` | PWA icons; PNGs generated from `assets/logo.svg` and `wasm/icons/maskable.svg` (safe zone for maskable icons) and `badge.svg` (monochrome notification badge) |
| `src/platform/wasm/` | `WasmStorage` (IDBFS), `WasmNotifier`, `WasmShare` (shares handed over by another tab), the bridge to the loading screen and `visualViewport` readings (on-screen keyboard; `wasmshell.h`), file picking and downloading (`wasmfiles.h`, without Qt Widgets), a new tab with a file and page printing (`wasmopen.h`: "Print…" — the export's HTML document in a hidden frame without scripts and the browser's print dialog), persistence and file-picking tests (`wasmselftest.h`) |
| `src/app/diag/webselftest.h` | app tests for browsers: `?selftest=webpictures` (pictures from the internet, export with copies), `?selftest=pen` (the canvas under the stylus); tests on the desktop: `tests/app/diag/tst_webselftest.cpp` |
| `tests/web/`, `scripts/web-test.sh` | Playwright tests in Chromium and Firefox (pictures from the internet, stylus) and running them (CI: `wasm.yml`, job `browsers`) |
| `assets/fonts/`, `src/app/ui/fallbackfonts.h` | emoji and symbol fallback fonts (without them: empty boxes), `scripts/fallback-fonts.py` regenerates them |
| `scripts/wasm-build.sh`, `scripts/wasm-smoke.mjs` | build + site assembly, test in Chromium |
| `scripts/wasm-single-file.py`, `tests/scripts/tst_wasm_single_file.sh`, `tests/web/singlefile.spec.mjs` | single HTML file from `dist-wasm/` and its tests |
| `tests/app/drawing/wasm-drawing-check.mjs` | a drawing in a note in the light and dark theme, in Chromium; with `--text` text typed on the drawing |

Icons after a logo change:

```sh
cd wasm/icons
rsvg-convert -w 192 -h 192 ../../assets/logo.svg -o icon-192.png
rsvg-convert -w 512 -h 512 ../../assets/logo.svg -o icon-512.png
rsvg-convert -w 192 -h 192 maskable.svg -o maskable-192.png
rsvg-convert -w 512 -h 512 maskable.svg -o maskable-512.png
rsvg-convert -w 180 -h 180 maskable.svg -o apple-touch-icon.png
rsvg-convert -w 96 -h 96 badge.svg -o badge-96.png
optipng -o5 *.png
```
