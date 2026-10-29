#pragma once

// WebAssembly only (compiled when EMSCRIPTEN is set, see src/platform/CMakeLists.txt).

#include <QString>

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

/// One reading of the browser's viewports, in CSS pixels (= Qt's logical pixels, wasm/index.html).
/// Numbers the browser did not give are NaN. KeyboardInsets computes the keyboard from it
/// (src/app/ui/keyboardinsets.h, ViewportKeyboard).
struct BrowserViewport {
    double innerWidth = 0;      ///< window.innerWidth
    double innerHeight = 0;     ///< window.innerHeight
    double height = 0;          ///< window.visualViewport.height
    double offsetTop = 0;       ///< window.visualViewport.offsetTop
    double scale = 1;           ///< window.visualViewport.scale
    double containerHeight = 0; ///< Qt's container element (#screen): its layout height
    bool touch = false;         ///< a coarse pointer (phone, tablet)
    bool editing = false;       ///< Qt's hidden keyboard <input> has the focus
    bool settled = true;        ///< unchanged for 6 animation frames (or the last of 60 frames)
    QString mode;               ///< brulionShell.keyboardMode: "content", "visual" or "" (another page)
};

/// Calls `callback` (in Qt's event loop) with a reading of the browser's viewports at once and
/// whenever one changes: on visualViewport resize and scroll, window resize, focus changes, a
/// resize of Qt's container, and every animation frame after any of them until the values stay
/// put (the last call: `settled`). The page asks the browser to shrink itself for the on-screen
/// keyboard (interactive-widget=resizes-content, wasm/index.html); browsers that ignore it (iOS
/// Safari, older ones) shrink only the visual viewport, and a page iOS scrolled is put back at the
/// top. Without visualViewport (old browsers) nothing is reported. docs/WEB.md "Klawiatura ekranowa".
void watchVisualViewport(std::function<void(const BrowserViewport &viewport)> callback);

/// The scheme of the page the app runs on: "https", "http", or "" (another one, unknown). A
/// page opened over https cannot fetch anything over http (mixed content).
QString pageScheme();

} // namespace brulion::platform
