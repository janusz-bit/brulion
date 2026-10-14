# e2e — Playwright smoke tests and screenshots

Browser tests for Brulion's web build. Two jobs:

1. **Smoke checks**: the app (`index.html`) and the component gallery (`gallery.html`) load with
   no `console.error` output and no uncaught exceptions.
2. **Screenshots for visual review**: the gallery and the app, in light and dark, on a desktop
   (1280×800) and a phone (412×915, touch) viewport. People and coding agents look at these
   PNGs to check the UI. They are **not** compared against baselines. There is no pixel-diff
   regression testing (yet).

```
playwright.config.ts      projects, webServer (build + vite preview), Chromium lookup
scripts/e2e.sh            resolves Chromium from nixpkgs, exports CHROMIUM_PATH, runs playwright
gallery.html              extra Vite build input → src/app/dev/gallery-entry.tsx
e2e/harness.ts            `test`/`expect` with the console-error guard, settle(), capture()
e2e/smoke.spec.ts         index.html boots (#root has children), screenshots *-app.png
e2e/gallery.spec.ts       gallery.html?theme=system renders, screenshots *-gallery*.png
e2e/a11y.spec.ts          gallery: WCAG AA text contrast, no horizontal overflow, ≥ 24 px touch targets
e2e/png.ts                tiny PNG decode/crop (node:zlib) for tiles and sections
e2e/screenshots/          output (gitignored, rewritten on every run)
e2e/.dist/                the production build under test (gitignored)
```

## Running

```sh
npm run e2e                      # all tests, both projects (~3 min on the Raspberry Pi 4)
npm run e2e:shots                # only the screenshot tests (tag @shots)
npm run e2e -- --project=desktop # one project (use '=': --project takes several values)
npm run e2e -- --grep @gallery   # only the gallery (screenshots + a11y checks)
npm run e2e -- --grep @a11y      # only the accessibility checks (fast, no screenshots)
npm run e2e -- e2e/smoke.spec.ts
E2E_SKIP_BUILD=1 npm run e2e     # reuse the last build in e2e/.dist (faster iteration)
```

Every run does `npm run build -- --outDir e2e/.dist`, then serves that directory with
`vite preview` on `127.0.0.1:4173` (`--strictPort`). The build goes to its own directory so
other `npm run build`s writing `dist/` can't change files under the server mid-run.
Playwright starts and stops the server itself. If a run is killed hard and leaves it behind:
`pkill -f "vite preview"`. If the port is taken the run fails rather than testing whatever is
listening there. Pick another port with `E2E_PORT=4174`.

To test against a server that is already running, for example the dev server with HMR:

```sh
npx vite &                                         # http://localhost:1420/
E2E_BASE_URL=http://localhost:1420/ npm run e2e    # no build, no webServer
kill %1                                            # don't leave dev servers running on the Pi
```

### Chromium on NixOS

Playwright's downloaded browsers don't run on NixOS, so **don't run `npx playwright install`**
here. `scripts/e2e.sh` takes Chromium from nixpkgs:

```sh
nix build --dry-run nixpkgs#chromium                     # must not say "will be built"
nix build --no-link --print-out-paths nixpkgs#chromium   # → /nix/store/…-chromium-…/bin/chromium
```

The script refuses to continue if Chromium would be compiled locally (that takes hours). Set
`E2E_ALLOW_CHROMIUM_BUILD=1` to allow it anyway, or point `CHROMIUM_PATH` at any Chromium.
Running `npx playwright test` directly also works when `CHROMIUM_PATH` is exported or a
`chromium` binary is on `PATH`. On non-Nix machines (CI) with neither, the config falls back to
Playwright's bundled browser (`npx playwright install --with-deps chromium`).

Settings in `playwright.config.ts`: one worker; headless; `--no-sandbox` (Playwright's default);
`pl-PL` locale and `Europe/Warsaw` time zone; `reducedMotion: 'reduce'`; service workers blocked,
so the PWA cache never serves a stale build. `E2E_DPR=2.625` renders the mobile project at
Pixel 7 density. The default is 1, which keeps the images small.

## Output files

`e2e/screenshots/<project>-<scheme>-<name>…png`, where project is `desktop` or `mobile` and
scheme is `light` or `dark`:

| File | What |
| --- | --- |
| `desktop-light-gallery.png` | full page, the whole gallery in one image |
| `desktop-light-gallery.tile-01.png`, `-02`, … | the full page cut into viewport-sized tiles (max 40) |
| `desktop-light-gallery.section-02-buttons.png` | one image per gallery `<section data-section="…">` (max 40) |
| `mobile-dark-app.png` | the app (`index.html`) after boot (no tiles; the shell is usually one screen) |

How the full page is captured: `capture()` grows the viewport to the document height (max
32 000 px; longer pages are cut off, with a warning annotation) and takes an ordinary
screenshot. It does not use Playwright's `fullPage: true`. In Chromium that re-lays out the
page without touch emulation, so the many `(pointer: coarse)` rules disappear on the mobile
project and the image doesn't match the real phone layout. Elements sized in `vh`/`dvh` are
measured against the tall viewport, as in any full-page capture.

Tiles and sections are cropped from that one image with `e2e/png.ts`, not taken as separate
screenshots. Each extra capture takes seconds on the Pi, and element screenshots would include
the gallery's sticky header. Each run deletes the old files for a name before writing new ones,
so a shorter gallery leaves no stale tiles behind.

On failure, Playwright writes a screenshot of the failing page to
`test-results/<test>/test-failed-1.png`.

## Visual verification for coding agents

After changing UI code (components, `theme.css`, layouts), look at the result:

1. Run the screenshot tests. A single project is enough while iterating:
   `npm run e2e:shots -- --project=desktop`. Before finishing, run both projects.
2. Open the PNGs with the **Read tool** (it displays images), for example
   `Read e2e/screenshots/desktop-light-gallery.section-02-buttons.png`. List them with
   `ls e2e/screenshots`.
3. **Prefer sections and tiles to the full-page image.** Read downscales large images, so a
   tall full-page shot (for example 1280×6000) shrinks until text is unreadable. Section
   images are the sharpest. Tiles cover everything at the viewport's size.
4. Check **both schemes and both projects**. Typical problems:
   * low contrast or a hard-coded colour in dark mode (white boxes, invisible text or icons);
   * overflow or horizontal scrolling at 412 px, clipped labels, wrapped buttons;
   * touch targets smaller than about 44 px on mobile;
   * missing focus rings, overlapping popovers, broken Polish diacritics (ąćęłńóśźż);
   * spacing or alignment that doesn't follow the look described in `docs/UI.md`.
5. Say what you looked at in your report. Name the files, for example "checked
   `mobile-dark-gallery.section-10-dialogs.png`: …".

A test fails on any `console.error` or uncaught exception. Read the failure output: it lists the
messages, with source locations for console errors and stacks for exceptions.

## The gallery (`src/app/dev/gallery-entry.tsx` → `Gallery.tsx`)

`gallery.html` is built only when `src/app/dev/gallery-entry.tsx` exists (see
`vite.config.ts`). Without it, `vite preview` serves `index.html` for `/gallery.html` and the
gallery test fails with a message that names the missing file.

The spec opens `gallery.html?theme=system`. The gallery then follows the emulated
`prefers-color-scheme`, as the app does with its default setting. Without the parameter it
shows light and dark side by side. Other parameters, useful when you look at the page by hand
or write another spec: `locale=en`, `section=<id>` (one section only), `font=sans`, and
`now=real` (a fixed date is the default, so shots stay stable).

To keep the gallery screenshot-friendly:

* Each group of components is a `<section data-section="<id>">`. `capture()` saves one image
  per section, named after `data-section`.
* Keep it deterministic: no random data and no output that depends on the current time.
* It must render without `console.error`, like the app.
* Show popovers and modals open (inline) where possible, so the shots include them.

## Accessibility checks (`a11y.spec.ts`)

Measured in the page, no screenshots, for both schemes and both projects:

* **Text contrast** against the composited background must meet WCAG AA (4.5:1; 3:1 for
  text ≥ 24 px or bold ≥ 18.66 px). Exempt: `.b-sr-only`, `aria-hidden`, tooltips, disabled
  controls (`:disabled`, `.is-disabled`, `aria-disabled`) and anything dimmed below 60 %
  opacity. A failure lists section, class, text and ratio. Fix the token in `theme.css`
  rather than the component when a token is the cause (`--fg-faint` is the usual suspect).
* **No horizontal overflow** of the page.
* **Touch targets** (mobile project only): buttons, inputs, selects, summaries, switches and
  separators are at least 24×24 px (WCAG 2.5.8), counting an absolutely positioned `::before`
  hit area. Inputs with a `<label>` are exempt (the label is part of the target).

Focus rings are not checked automatically; when changing focus styles, look at them (focus a
control from the keyboard and screenshot it), in both schemes. Watch for state rules with
higher specificity than `:focus-visible` (a selected segment's shadow hid its ring once).

## Writing more specs

```ts
import { capture, expect, expectScheme, SCHEMES, settle, test } from './harness';

for (const scheme of SCHEMES) {
  test.describe(`calendar ${scheme}`, () => {
    test.use({ colorScheme: scheme });
    test('month view', { tag: '@shots' }, async ({ page }, testInfo) => {
      await page.goto('./');                       // relative: works with BRULION_BASE too
      await expectScheme(page, scheme);
      await settle(page);                          // network idle + fonts + 2 frames
      await capture(page, testInfo, scheme, 'calendar', { tiles: false });
    });
  });
}
```

* Import `test`/`expect` from `./harness`, not from `@playwright/test`. That keeps the
  console-error guard on.
* For a console error that is expected, add `test.use({ allowedErrors: [/regex/] })`.
* Tag screenshot tests `@shots` so `npm run e2e:shots` picks them up.
* Files are `e2e/**/*.spec.ts`. Vitest only collects `src/**/*.test.ts(x)`, so specs never run
  under `npm test`. `e2e/` is type-checked by `npm run typecheck`.
