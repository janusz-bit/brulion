// KeyboardInsets — how much of the window's bottom edge the on-screen keyboard covers (QML
// singleton `KeyboardInsets`): the one number the shell lifts its content by, so views, the
// docked editing toolbar, bottom sheets, menus and toasts end at the keyboard's top on every
// platform (docs/UI.md "On-screen keyboard").
//
// Sources, by priority:
//   1. `simulated` — DevTools `--keyboard <px>`, tests and screenshots (simulate()).
//   2. the platform's own report: Android's IME insets (setPlatformState; android/src/…/
//      ImeInsets.kt via AndroidBridge::imeChanged, frame by frame while the keyboard slides on
//      Android 11+), the browser's viewports (setViewportSample; src/platform/wasm/wasmshell.h,
//      computed by ViewportKeyboard below). From its first report on, the platform wins over
//      Qt's own rectangle.
//   3. Qt's QInputMethod::keyboardRectangle (setInputMethodState), mapped with keyboardOverlap():
//      a fallback for platforms or devices that report nothing else.
// The values are idempotent: a window the system already resized above the keyboard gets 0.
//
// GUI-free (QtCore only; brulion_ui_helpers): src/app/ui/keyboardsources.cpp connects the
// platform and Qt's input method to the instance main() creates.
#pragma once

#include <QHash>
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

/// One reading of the browser's viewports in logical (CSS) pixels — the browser build reports
/// them (src/platform/wasm/wasmshell.h, BrowserViewport) on every change while the keyboard
/// moves, and once more when the values stay put (`settled`).
struct ViewportSample {
    qreal innerWidth = 0;      ///< window.innerWidth (one tallest height per width: rotation)
    qreal innerHeight = 0;     ///< window.innerHeight (may lag a frame behind while the page resizes)
    qreal height = 0;          ///< visualViewport.height
    qreal offsetTop = 0;       ///< visualViewport.offsetTop (from the layout viewport's top)
    qreal scale = 1;           ///< visualViewport.scale (pinch zoom)
    qreal containerHeight = 0; ///< Qt's container (#screen), fixed at the layout viewport's top; 0 unknown
    bool touch = false;        ///< a coarse pointer (phone, tablet)
    bool editing = false;      ///< Qt's hidden keyboard <input> has the focus (text is edited)
    bool settled = true;       ///< the values stayed put for a few animation frames
    QString mode;              ///< the page's interactive-widget: "content", "visual" or "" (unknown)
};

/// How much of Qt's container the visual viewport leaves out at the bottom:
/// containerHeight − (offsetTop + height), clamped to 0 … the container's height. The container's
/// height is `containerHeight`, else `windowHeight` (Qt's window), else innerHeight. 0 for a
/// pinch-zoomed page (scale > 1.01), malformed values (not finite, height <= 0) and below
/// ViewportKeyboard::MinOverlap (rounding); a negative offsetTop (overscroll) counts as 0.
/// Measured against the container (one layout snapshot with the visual viewport), not
/// innerHeight: a page the browser shrank itself for the keyboard
/// (interactive-widget=resizes-content) gives 0 at once, also while innerHeight is stale.
qreal viewportOverlap(const ViewportSample &sample, qreal windowHeight);

/// The browser's keyboard from successive viewport samples (docs/WEB.md "Klawiatura ekranowa").
/// `overlap`: viewportOverlap(). `shown`: an overlap, or — the browser shrank the page itself
/// (interactive-widget=resizes-content) — on a touch screen with Qt's keyboard <input> focused, a
/// visual viewport at least MinKeyboard lower than the tallest one seen at this width (one tallest
/// per width: a rotated phone starts again). Once shown so, the focus passing between Qt's
/// elements (Enter: the next block) does not hide it while the page stays shrunk and the samples
/// have not settled; a settled sample without the focus ends it (the editing is over: a page
/// shrunk for another reason, a split screen, is no keyboard). A phone turned with the keyboard
/// up reaches a width never seen with the keyboard in its first heights: there the keyboard stays
/// shown, and those heights do not count as the tallest, until the page grows by MinKeyboard (the
/// keyboard went away) or a settled sample without the focus comes. `animating`: the viewports
/// moved since the last settled sample and have not settled yet (a focus change alone does not
/// count). Feeding the same sample twice gives the same state.
class ViewportKeyboard {
public:
    struct State {
        qreal overlap = 0;
        bool shown = false;
        bool animating = false;
        bool operator==(const State &) const = default;
    };
    /// Below this the container and the visual viewport only differ by rounding.
    static constexpr qreal MinOverlap = 2;
    /// A smaller shrink is the browser's own bars (an address bar of 56 px), not a keyboard.
    static constexpr qreal MinKeyboard = 120;

    State update(const ViewportSample &sample, qreal windowHeight);
    /// The tallest visual viewport seen at this width (0: none yet).
    qreal tallest(qreal innerWidth) const;

private:
    QHash<int, qreal> m_tallest; // qRound(innerWidth) → the tallest visualViewport.height
    ViewportSample m_settled;    // the last settled sample (geometry only)
    bool m_hasSettled = false;
    bool m_moving = false; // an unsettled sample moved away from m_settled
    bool m_shrunk = false; // shown from the page's shrink (the last sample)
    // Turned with the keyboard up: the width whose heights do not count yet, the lowest seen there.
    bool m_carried = false;
    int m_carriedWidth = 0;
    qreal m_carriedLowest = 0;
};

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
    /// Self-tests (the browser's `?selftest=keyboard`): the keyboard's room and the docked editing
    /// toolbar are drawn in marker colours, so a screenshot shows where each one ends.
    Q_PROPERTY(bool marked READ marked NOTIFY markedChanged FINAL)

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
    /// The browser's viewports (the platform's report there), computed by ViewportKeyboard. The
    /// sample is kept: a new window height evaluates it again (without a container height the
    /// window's height is the container's).
    void setViewportSample(const ViewportSample &sample);
    /// The last viewport sample (setViewportSample), default-constructed before the first one.
    const ViewportSample &viewportSample() const { return m_viewportSample; }

    bool marked() const { return m_marked; }
    void setMarked(bool marked);
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
    void markedChanged();

private:
    struct State {
        qreal overlap = 0;
        bool shown = false;
        bool animating = false;
    };
    qreal clampOverlap(qreal overlap) const;
    void applyPlatform(qreal overlap, bool shown, bool animating, const QString &how);
    void update();

    State m_platform;
    State m_inputMethod;
    State m_current;
    bool m_hasPlatform = false;
    ViewportKeyboard m_viewportKeyboard;
    ViewportSample m_viewportSample;
    bool m_hasViewportSample = false;
    bool m_marked = false;
    qreal m_simulated = -1;
    bool m_simulatedAnimating = false;
    qreal m_windowHeight = 0;
    Source m_source = Source::None;
    // What the log said last (settled values only, not every frame of a slide).
    qreal m_loggedOverlap = 0;
    bool m_loggedShown = false;
};

} // namespace brulion::ui
