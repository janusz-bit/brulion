// KeyboardInsets — how much of the window's bottom edge the on-screen keyboard covers (QML
// singleton `KeyboardInsets`): the one number the shell lifts its content by, so views, the
// docked editing toolbar, bottom sheets, menus and toasts end at the keyboard's top on every
// platform (docs/UI.md "On-screen keyboard").
//
// Sources, by priority:
//   1. `simulated` — DevTools `--keyboard <px>`, tests and screenshots (simulate()).
//   2. the platform's own report (setPlatformState): Android's IME insets (android/src/…/
//      ImeInsets.kt via AndroidBridge::imeChanged, frame by frame while the keyboard slides on
//      Android 11+), the browser's visualViewport (src/platform/wasm/wasmshell.h). From its first
//      report on, the platform wins over Qt's own rectangle.
//   3. Qt's QInputMethod::keyboardRectangle (setInputMethodState), mapped with keyboardOverlap():
//      a fallback for platforms or devices that report nothing else.
// The values are idempotent: a window the system already resized above the keyboard gets 0.
//
// GUI-free (QtCore only; brulion_ui_helpers): src/app/ui/keyboardsources.cpp connects the
// platform and Qt's input method to the instance main() creates.
#pragma once

#include <QObject>
#include <QRect>
#include <QRectF>
#include <QString>

namespace brulion::ui {

/// How much of a window's bottom edge a keyboard covers, in logical pixels: 0 … window height.
/// `keyboard` is the keyboard's rectangle in screen coordinates — native (physical) pixels when
/// `nativePixels` (what Qt for Android reports), logical ones otherwise; `dpr` converts native
/// pixels to logical ones (an invalid ratio counts as 1). `window` is the window's geometry in
/// logical screen coordinates (a window in the bottom half of a split screen starts lower).
/// A hidden keyboard, an empty, negative or non-finite rectangle, a keyboard below the window
/// (the system resized the window above it) or beside it give 0.
qreal keyboardOverlap(const QRectF &keyboard, bool nativePixels, qreal dpr, const QRect &window, bool visible);

class KeyboardInsets : public QObject {
    Q_OBJECT

    /// How much of the window's bottom edge the on-screen keyboard covers (logical pixels); 0
    /// when it is hidden or the window was already resized above it.
    Q_PROPERTY(qreal overlap READ overlap NOTIFY overlapChanged FINAL)
    /// The keyboard is up or coming up (also while it slides in; false as soon as it starts
    /// going away).
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged FINAL)
    /// The keyboard is sliding in or out and `overlap` follows it frame by frame: callers wait
    /// for the end before scrolling a caret into view.
    Q_PROPERTY(bool animating READ animating NOTIFY animatingChanged FINAL)
    /// A simulated keyboard of this height (tests, screenshots: `--keyboard <px>`); -1 = off.
    Q_PROPERTY(qreal simulated READ simulated WRITE setSimulated NOTIFY simulatedChanged FINAL)
    /// Where the values come from: "none", "inputMethod", "platform" or "simulated".
    Q_PROPERTY(QString source READ sourceName NOTIFY sourceChanged FINAL)

public:
    enum class Source { None, InputMethod, Platform, Simulated };

    explicit KeyboardInsets(QObject *parent = nullptr);
    ~KeyboardInsets() override;

    /// The first instance created (main() makes the app's), or nullptr.
    static KeyboardInsets *instance();

    qreal overlap() const { return m_current.overlap; }
    bool shown() const { return m_current.shown; }
    bool animating() const { return m_current.animating; }
    qreal simulated() const { return m_simulated; }
    Source source() const { return m_source; }
    QString sourceName() const;

    /// < 0 or not finite: no simulated keyboard (the platform's values again).
    void setSimulated(qreal overlap);
    /// A simulated keyboard of `overlap` px that is still sliding (`animating`) or has settled;
    /// overlap < 0 turns the simulation off.
    Q_INVOKABLE void simulate(qreal overlap, bool animating = false);

    /// The platform's report in logical pixels (see above). Not finite or negative overlaps
    /// count as 0; overlaps are clamped to the window's height when it is known.
    void setPlatformState(qreal overlap, bool shown, bool animating);
    /// Qt's input method, already mapped with keyboardOverlap() (the fallback).
    void setInputMethodState(qreal overlap, bool shown);
    /// The window's logical height: overlaps are clamped to it (0 = unknown, no clamping).
    void setWindowHeight(qreal height);
    /// The platform has reported at least once (its values win over the input method's).
    bool hasPlatformSource() const { return m_hasPlatform; }

    /// Native (physical) pixels to logical ones; 0 for anything not finite or negative, an
    /// invalid ratio counts as 1.
    static qreal toLogical(qreal nativePixels, qreal dpr);

Q_SIGNALS:
    void overlapChanged();
    void shownChanged();
    void animatingChanged();
    void simulatedChanged();
    void sourceChanged();

private:
    struct State {
        qreal overlap = 0;
        bool shown = false;
        bool animating = false;
    };
    qreal clampOverlap(qreal overlap) const;
    void update();

    State m_platform;
    State m_inputMethod;
    State m_current;
    bool m_hasPlatform = false;
    qreal m_simulated = -1;
    bool m_simulatedAnimating = false;
    qreal m_windowHeight = 0;
    Source m_source = Source::None;
    // What the log said last (settled values only, not every frame of a slide).
    qreal m_loggedOverlap = 0;
    bool m_loggedShown = false;
};

} // namespace brulion::ui
