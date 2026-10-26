// GestureInsets — how much of the window's left and right edges the system keeps for its own
// swipes (QML singleton `GestureInsets`): Android's gesture navigation takes a swipe that starts
// there for "back", so the shell's drawer starts its edge swipe beyond them (docs/UI.md "Window
// layout"). 0 where nothing is reported: button navigation, desktops, the browser.
//
// Sources, by priority:
//   1. `simulate()` — tests (the shell's self-test) and manual checks on a desktop.
//   2. the platform's report (setPlatformInsets): Android's system gesture insets
//      (WindowInsets.Type.systemGestures(), android/src/…/ImeInsets.kt via
//      AndroidBridge::systemGesturesChanged; src/app/ui/keyboardsources.cpp connects it).
//
// GUI-free (QtCore only; brulion_ui_helpers).
#pragma once

#include <QObject>

namespace brulion::ui {

class GestureInsets : public QObject {
    Q_OBJECT

    /// Logical pixels of the left edge the system keeps for its gestures (0: none).
    Q_PROPERTY(qreal left READ left NOTIFY changed FINAL)
    /// Logical pixels of the right edge the system keeps for its gestures (0: none).
    Q_PROPERTY(qreal right READ right NOTIFY changed FINAL)
    /// The values come from simulate() (until simulate(-1, -1)).
    Q_PROPERTY(bool simulated READ simulated NOTIFY changed FINAL)

public:
    explicit GestureInsets(QObject *parent = nullptr);
    ~GestureInsets() override;

    /// The first instance created (main() makes the app's), or nullptr.
    static GestureInsets *instance();

    qreal left() const { return m_simulated ? m_simulatedLeft : m_platformLeft; }
    qreal right() const { return m_simulated ? m_simulatedRight : m_platformRight; }
    bool simulated() const { return m_simulated; }

    /// The platform's report in logical pixels; negative or not finite values count as 0.
    void setPlatformInsets(qreal left, qreal right);
    /// Simulated insets in logical pixels (negative or not finite count as 0); both < 0 (or not
    /// finite) turn the simulation off: the platform's values again.
    Q_INVOKABLE void simulate(qreal left, qreal right);

Q_SIGNALS:
    void changed();

private:
    void apply(qreal oldLeft, qreal oldRight, bool oldSimulated);

    qreal m_platformLeft = 0;
    qreal m_platformRight = 0;
    qreal m_simulatedLeft = 0;
    qreal m_simulatedRight = 0;
    bool m_simulated = false;
};

} // namespace brulion::ui
