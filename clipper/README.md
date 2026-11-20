# Brulion Web Clipper

A browser extension (Manifest V3, one code base for Chrome, Chromium, Brave, Edge and Firefox) that
saves an article, a selection, a whole page, a screenshot, a link or a picture into a Brulion
notebook as Markdown, with its pictures, through `brulion-cli` as a native messaging host.

User guide, installation, the host, templates and privacy: [docs/CLIPPER.md](../docs/CLIPPER.md).

* `manifest.json` — the extension (its `key` fixes the Chromium id `calcbkckojhmdfecpcohaangneahmkgg`;
  Firefox's id is `brulion-clipper@januszbit.github.io`).
* `background.js` — context menus, the shortcut, clips of the popup, notifications.
* `popup.*`, `options.*`, `ui.css` — the popup and the options page.
* `content/extract.js` — reads the page (injected on a click only: `activeTab`).
* `lib/` — the host's messages, the clip request, settings, templates (pure parts tested by
  `test/*.test.mjs` with `node --test`, ctest `tst_clipper_extension`).
* `vendor/Readability.js` — Mozilla's Readability (Apache-2.0), see `vendor/README.md`.
* `_locales/en`, `_locales/pl` — the texts.

Development: load this folder as an unpacked extension (Chromium: chrome://extensions → Developer
mode → Load unpacked; Firefox: about:debugging → This Firefox → Load Temporary Add-on → manifest.json),
then run `brulion-cli clip install`. Packages: `scripts/clipper-pack.sh` (a Chrome ZIP and a Firefox
XPI in `dist-clipper/`, linted with web-ext). Browser test: `scripts/web-test.sh --clipper`.
