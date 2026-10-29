// The sources of KeyboardInsets (keyboardinsets.h) in the app: the platform's own report —
// Android's IME insets (AndroidBridge::imeChanged, android/src/…/ImeInsets.kt), the browser's
// viewports (src/platform/wasm/wasmshell.h) —, Qt's input method as the fallback, and the
// window's height and device pixel ratio. Also the sources of GestureInsets (gestureinsets.h):
// Android's system gesture insets (AndroidBridge::systemGesturesChanged, the same observer).
#pragma once

class QQmlApplicationEngine;

namespace brulion::ui {

class GestureInsets;
class KeyboardInsets;

/// Feeds `insets` for the windows `engine` creates (call before loading the QML). Browser:
/// with `--selftest-keyboard` (?selftest=keyboard, scripts/wasm-smoke.mjs --keyboard and
/// --keyboard-phone, scripts/android-emulator-test.sh --web-keyboard) the keyboard's room and the
/// docked toolbar get marker colours (KeyboardInsets::marked) and every change is published as
/// `window.brulionKeyboardSelfTest` {overlap, shown, animating, source, mode, sample {…},
/// legacyOverlap, dpr, windowHeight, contentBottom, toolbarBottom, focusText} (logical px;
/// toolbarBottom -1: no toolbar shown; focusText: the focused editor's text, 120 characters,
/// left out for a password field).
void connectKeyboardSources(KeyboardInsets *insets, QQmlApplicationEngine *engine);

/// Feeds `insets` for the windows `engine` creates (call before loading the QML). Only Android
/// reports gesture zones; elsewhere they stay 0.
void connectGestureSources(GestureInsets *insets, QQmlApplicationEngine *engine);

} // namespace brulion::ui
