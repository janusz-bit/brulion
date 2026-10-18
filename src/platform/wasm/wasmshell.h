#pragma once

// WebAssembly only (compiled when EMSCRIPTEN is set, see src/platform/CMakeLists.txt).

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

} // namespace brulion::platform
