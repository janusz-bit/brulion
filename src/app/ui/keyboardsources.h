// The sources of KeyboardInsets (keyboardinsets.h) in the app: the platform's own report —
// Android's IME insets (AndroidBridge::imeChanged, android/src/…/ImeInsets.kt), the browser's
// visualViewport (src/platform/wasm/wasmshell.h) —, Qt's input method as the fallback, and the
// window's height and device pixel ratio.
#pragma once

class QQmlApplicationEngine;

namespace brulion::ui {

class KeyboardInsets;

/// Feeds `insets` for the windows `engine` creates (call before loading the QML). Browser:
/// with `--selftest-keyboard` (?selftest=keyboard, scripts/wasm-smoke.mjs --keyboard) every
/// change is published as `window.brulionKeyboardSelfTest` {overlap, shown, animating, source}.
void connectKeyboardSources(KeyboardInsets *insets, QQmlApplicationEngine *engine);

} // namespace brulion::ui
