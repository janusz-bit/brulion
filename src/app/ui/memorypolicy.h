// MemoryPolicy — what the app holds on to for speed, and when it lets go of it
// (docs/PERFORMANCE.md "Memory: only what is in use"; QML singleton `MemoryPolicy`, qmlforeign.h).
//
// Speed is bought with memory in three places: views kept hidden once shown (ShellRouteView),
// views and popups built ahead in the background, and what backs a window nobody looks at — its
// scene graph and graphics context, the spell checker's dictionaries, decoded icons and pictures,
// QML types of views long left. The policy bounds each of them by use:
//
//   * a pane keeps `keptViews` hidden views (the most recently shown: three, so the four views
//     of a phone's bottom navigation never rebuild each other), each for `hiddenViewMs`;
//   * `buildAhead`: views and popups are built ahead only while the app is in use;
//   * `inUse`: false once the app was away — another program in front for `unfocusedMs`, or no
//     window shown at all for `hiddenMs` (the tray, a minimised window, a phone's home screen).
//     letGo(hidden) is emitted then: everything hidden goes (ShellRouteView's kept views,
//     AppShell's closed popups, SpellCheck's dictionaries), and a moment later the policy itself
//     has the QML engine collect its garbage and drop the types nothing uses, every window drop
//     its cached graphics — a window that is not shown its whole scene graph and graphics
//     context (`releasesGraphics`), rebuilt when it is shown again — and MemoryKeeper hand the
//     freed pages back. Active again, the app is in use at once (and builds ahead again).
//
// An app whose window is shown but was never in front (a test under an X server without a
// window manager) stays in use; one that never showed a window (a start into the tray) is away
// `startMs` after its start.
//
//   BRULION_MEMORY_POLICY=0   the old behaviour for A/B measurements: every view kept, everything
//                             built ahead, nothing let go
//   BRULION_AWAY_MS=<ms>      both delays (tests, measurements)
//   BRULION_HIDDEN_VIEW_MS=<ms>   `hiddenViewMs` (measurements: the state after a while in one view)
//   BRULION_RELEASE_GRAPHICS=1 / 0   a hidden window's scene graph and graphics context released
//                             or not, whatever the platform's default (`releasesGraphics`)
//
//   ui::MemoryPolicy *policy = ui::MemoryPolicy::instance();
//   policy->setEngine(&engine);
//   connect(policy, &ui::MemoryPolicy::letGo, spell, &SpellCheck::unload);
#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>

class QQmlEngine;

namespace brulion::ui {

class MemoryPolicy : public QObject {
    Q_OBJECT
    /// Views and popups may be built ahead in the background now (the app is in use).
    Q_PROPERTY(bool buildAhead READ buildAhead NOTIFY inUseChanged FINAL)
    /// Hidden views a pane holds on to, the most recently shown (-1: all of them).
    Q_PROPERTY(int keptViews READ keptViews WRITE setKeptViews NOTIFY limitsChanged FINAL)
    /// A hidden view goes when no view of its pane was switched for this long (0: never).
    Q_PROPERTY(int hiddenViewMs READ hiddenViewMs WRITE setHiddenViewMs NOTIFY limitsChanged FINAL)
    /// The app is in front, or was a moment ago.
    Q_PROPERTY(bool inUse READ inUse NOTIFY inUseChanged FINAL)
    /// BRULION_MEMORY_POLICY=0: everything is kept and built ahead as before the policy.
    Q_PROPERTY(bool switchedOff READ switchedOff CONSTANT FINAL)

public:
    /// The application's policy (made on first use, a child of the application object; null
    /// without one). It follows the application's state and its windows by itself.
    static MemoryPolicy *instance();

    explicit MemoryPolicy(QObject *parent = nullptr);
    ~MemoryPolicy() override;

    static bool switchedOff();

    bool buildAhead() const { return m_inUse; }
    bool inUse() const { return m_inUse; }
    int keptViews() const { return switchedOff() ? -1 : m_keptViews; }
    void setKeptViews(int count);
    int hiddenViewMs() const { return switchedOff() ? 0 : m_hiddenViewMs; }
    void setHiddenViewMs(int ms);

    /// Another program in front this long: the app is away (default 60 s).
    int unfocusedMs() const { return m_unfocusedMs; }
    void setUnfocusedMs(int ms) { m_unfocusedMs = ms; }
    /// No window shown this long: the app is away (default 8 s; 2 s on phones).
    int hiddenMs() const { return m_hiddenMs; }
    void setHiddenMs(int ms) { m_hiddenMs = ms; }
    /// … and at least this long while no window was shown yet (a start into the tray; default 20 s).
    int startMs() const { return m_startMs; }
    void setStartMs(int ms) { m_startMs = ms; }

    /// The engine whose garbage and unused types go when the app is away (main() sets it).
    void setEngine(QQmlEngine *engine);
    /// A window that is not shown gives up its scene graph and its graphics context when the app
    /// is away, and builds them again when it is shown (false, the default: it only drops what it
    /// cached). main() turns it on where it was tried — Linux, X11 and Wayland —;
    /// BRULION_RELEASE_GRAPHICS=1 / 0 in the environment decides elsewhere, or against it.
    bool releasesGraphics() const { return m_releasesGraphics; }
    void setReleasesGraphics(bool on);

    /// How many times the app let go (tests).
    int runs() const { return m_runs; }

    /// Tests and measurements drive the state themselves: the application's own state and its
    /// windows are not followed any more.
    void setFollowsApplication(bool follows);
    /// The state as the application reports it: in front; some window shown.
    void setState(bool active, bool shown);

public Q_SLOTS:
    /// Everything hidden goes now, as when the app has been away long enough (`hidden`: as with no
    /// window shown). The app stays in use.
    void letGoNow(bool hidden = false);
    /// Tests and measurements: the app away at once (`hidden`: as with no window shown) — not in
    /// use, everything hidden let go —, or (`away` false) in use again. The application's own
    /// state is not followed any more (setFollowsApplication(true) to follow it again).
    void setAway(bool away, bool hidden = false);

Q_SIGNALS:
    void inUseChanged();
    void limitsChanged();
    /// Let go of everything that is not shown; `hidden`: no window of the app is shown at all.
    void letGo(bool hidden);
    /// The engine, the windows and the allocator were asked to give back what letGo() freed.
    void released(bool hidden);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void follow();
    void evaluate();
    void startAway(int ms);
    void away();
    void release();

    QPointer<QQmlEngine> m_engine;
    QTimer m_awayTimer;    ///< the app becomes "away" when it fires
    QTimer m_releaseTimer; ///< after letGo(): the deletions it started have run
    int m_keptViews = 3;
    int m_hiddenViewMs = 5 * 60 * 1000;
    int m_unfocusedMs = 60 * 1000;
    int m_hiddenMs = 8 * 1000;
    int m_startMs = 20 * 1000;
    int m_runs = 0;
    bool m_follows = true;
    bool m_releasesGraphics = false;
    bool m_active = false;
    bool m_shown = true;
    bool m_everActive = false; ///< the app was in front at least once
    bool m_everShown = false;  ///< a window of the app was shown at least once
    bool m_inUse = true;
    bool m_awayHidden = false;    ///< what the pending or last "away" is: no window shown
    bool m_releaseHidden = false; ///< the pending release() is for a hidden app
    bool m_letGoHidden = false;   ///< away, and let go as hidden already
};

} // namespace brulion::ui
