#pragma once

// WebAssembly only (compiled when EMSCRIPTEN is set, see src/platform/CMakeLists.txt).

#include <functional>

namespace brulion::platform {

/// Startup stages shown by the loading screen of the HTML shell (wasm/index.html), which
/// owns the (Polish/English) texts — the shell runs before Qt and its translations load.
enum class ShellStage {
    LoadingNotes, ///< reading the vault from IndexedDB
    StartingUi,   ///< creating the QML UI
};

/// Updates the loading screen text. No-op without the Brulion shell (e.g. Qt's default page).
void setShellStage(ShellStage stage);

/// Hides the loading screen once the UI has rendered its first frames. The shell also hides it
/// by itself after a timeout, so a missing call never locks the user out.
void hideShellLoadingScreen();

/// Tells the shell that the open vault is fully indexed (`brulionShell.indexed()`; the shell
/// keeps the time in `brulionShell.indexedAt` for scripts/wasm-smoke.mjs and the benchmarks).
void reportIndexReady();

/// Calls `callback` with how much of the page's bottom the on-screen keyboard covers, in CSS
/// pixels (= Qt's logical pixels): the browser's visualViewport, `innerHeight - height -
/// offsetTop` (Chrome and Firefox on Android keep the page's size and shrink the visual viewport
/// for the keyboard; iOS Safari also scrolls the page, which is put back). Called once at once and
/// on every change, in Qt's event loop. Without visualViewport (old browsers) nothing is reported.
void watchVisualViewport(std::function<void(double overlap)> callback);

} // namespace brulion::platform
