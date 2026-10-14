# Brulion — notes for contributors and coding agents

Offline-first Markdown notes app (Obsidian-style files + Logseq-style outliner/journals) for
NixOS (Tauri), Android (Tauri) and the browser (PWA). Read these before changing code:

* `docs/SYNTAX.md` — the Markdown dialect (single source of truth for parsing/rendering).
* `docs/ARCHITECTURE.md` — layers, module ownership and public API signatures.
* `src/core/types.ts` — shared contracts. Change it only when strictly needed and keep it
  backwards compatible; other modules are developed against it in parallel.

## Commands

* `npm test` — vitest (all), `npx vitest run src/core/<module>` — one module.
* `npm run typecheck` — TypeScript 7 (native `tsc`, fast). Must be clean.
* `npm run lint` — oxlint. `npm run build` — production web build (PWA).
* This dev machine is a Raspberry Pi 4 (aarch64, ~3.7 GB RAM): don't run several heavy
  builds at once; never start long-running dev servers without killing them afterwards.
* Rust/Tauri: `nix develop` provides the toolchain (`cargo`, `cargo-tauri`, webkitgtk).

## Conventions

* TypeScript strict (`noUncheckedIndexedAccess` on). No `any` unless commented why.
* ES modules, **named exports only**. Each core module's public API is re-exported from its
  `index.ts`; import other modules only through their `index.ts` (`@/core/dates`, ...). The
  `@/` alias points at `src/`.
* `src/core` must stay platform independent: no React, no `@tauri-apps/*`, no `window`/
  `document` at module top level. DOM APIs only inside functions that are documented as
  browser-only, or injected.
* Tests live next to code as `*.test.ts`. Default environment is node; put
  `// @vitest-environment jsdom` on the first line of tests that need DOM. `fake-indexeddb`
  is loaded globally in tests. Every module needs meaningful tests, including edge cases and
  Polish text (diacritics: ąćęłńóśźż).
* Dates: never use `new Date('YYYY-MM-DD')` (UTC!). Use helpers from `@/core/dates`
  (`parseLocal`, `toLocalDate`, ...). Reminders are local wall-clock time.
* Pure functions for content edits (`content in → content out`), line endings preserved.
* User-visible strings go through i18n (`src/core/i18n/messages/<area>.ts`, Polish + English).
  Polish is the default UI language.
* Comments and identifiers in English; user docs in Polish (README has an English section).
* Security: notes may come from anywhere (imports, sync). Rendered HTML is always sanitised
  with DOMPurify; never `eval`/`new Function` on note content (calc uses mathjs' parser with
  dangerous functions disabled).
* Keep dependencies minimal; prefer what is already in package.json.

## Resource limits on the dev machine (important)

The Raspberry Pi crashed once from memory exhaustion when many agents ran tests in parallel.
* vitest is capped at 2 workers (`vite.config.ts`); never raise it here. Prefer running only
  the test files you touched (`npx vitest run path/to/file.test.ts`).
* `scripts/e2e.sh` takes a global lock — only one Chromium/Playwright run at a time. Always
  go through it; never start Chromium directly in parallel with it.
* Heavy builds (Rust, Nix, Android) run on the x86_64 laptop via
  `flock <lock> laptop-run …` (see docs/NIXOS.md / docs/ANDROID.md), never on the Pi.
