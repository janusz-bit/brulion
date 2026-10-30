#pragma once

// WebAssembly only (compiled when EMSCRIPTEN is set, see src/platform/CMakeLists.txt).
//
// Debug flows for the browser build, driven by scripts/wasm-smoke.mjs. The HTML shell turns
// `?selftest=storage,notify,glyphs,keyboard,share` into the app arguments `--selftest-storage
// --selftest-notify --selftest-glyphs --selftest-keyboard --selftest-share` (keyboard:
// src/app/ui/keyboardsources.h; share: main.cpp publishes `window.brulionShareSelfTest`, the share
// dialog's data — AppController::pendingShare — with `dialog`: whether the dialog was asked to open).
// Results are published as JavaScript globals (and console lines `BRULION_SELFTEST …`; with
// `?report=/__report[/<name>]` the shell also POSTs each one to that path of the same site:
// {name, json, t}).
// No flow touches notes.

#include <QJsonObject>

namespace brulion::platform {

class WasmStorage;

/// Publishes `result` as the JavaScript global `name` (and the console line
/// `BRULION_SELFTEST <name> <json>`), for checks that live in the app: `brulionGlyphSelfTest`
/// (`?selftest=glyphs`, the fallback fonts of src/app/ui/fallbackfonts.h).
void publishSelfTestResult(const char *name, const QJsonObject &result);

/// Proves that browser storage survives a page reload. Call after `WasmStorage::ready()`.
///
/// In every mounted directory (cache mounts too) it increments the counter in the hidden file `.brulion-selftest`
/// (atomic `QSaveFile` write), waits for the automatic write-back to IndexedDB (no explicit
/// flush: this also checks the change tracking) and publishes `window.brulionSelfTest`
/// `{backed, flushed, counters: [{path, previous, now, written}], …}`. Reloading the page must
/// show every counter grown by one.
void runStorageSelfTest(WasmStorage *storage);

/// Shows a test notification through `WasmNotifier` (when permission is granted — the driver
/// grants it over the DevTools protocol) and publishes `window.brulionNotifySelfTest`
/// `{permission, shown}`, later `activated: {id, path, line}` once the notification is clicked.
void runNotifierSelfTest();

/// The browser's file picker and download (wasmfiles.h), `?selftest=pickfiles,savefile` in a test
/// tab (scripts/wasm-smoke.mjs --pick-files). `pick`: at the first mouse button release (the
/// user's gesture browsers want) openFiles({"Notatki (*.md *.txt)"}, several files, 1 MiB per
/// file, 1.5 MiB in all) and `window.brulionPickSelfTest`
/// `{cancelled, files: [{name, size, tooLarge, overTotal, read, sha256}]}` once they are read.
/// `save` (after the pick, if both): at the next release saveFile("ąę\n", "Zażółć.md") and
/// `window.brulionSaveSelfTest` `{started: true}`.
void runFilePickerSelfTest(bool pick, bool save);

} // namespace brulion::platform
