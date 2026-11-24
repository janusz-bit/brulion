// NearView — whether an item is in its window's view or within reach of it, for what is worth
// holding only there: a picture's decoded pixels and its texture (docs/PERFORMANCE.md "Pictures
// and memory", docs/UI.md "Pictures hold their pixels only near the view").
//
//   Rectangle {
//       id: frame                       // the picture's place: its size never depends on `near`
//       Image { source: frame.NearView.near ? url : "" }
//   }
//
// A page is created whole (BlockEditor.wholePageRows), a journal day too, and a kept view stays
// hidden with its rows: their pictures were all decoded at once and stayed so — forty photos of a
// page 1.3 GB. With NearView a picture loads when its frame comes within `enter` window heights
// of the window and lets go beyond `leave` (more, so that reading back and forth around one place
// decodes nothing again); hidden — its view, its window minimised or the app in the background —
// it lets go after `hiddenGraceMs`, so that a quick look at another view and back shows the
// pictures at once.
//
// How: the items of a window are looked at once a frame (QQuickWindow::afterAnimating: after the
// frame's polish, on the GUI thread; a frame is drawn whenever anything on screen moved), each by
// its rectangle in the window — one walk up its parents, ~0.5 µs —, whatever scrolls it: a list in
// a list, a split pane, a popup. Nothing runs while nothing is drawn, except one timer for the
// hidden items' grace. An item not in a window yet is not near.
//
// A row its list has not placed yet is not near either: an item view (ListView, GridView) that
// creates a row in the background, over several frames, parents it to its content at once — at
// the content's top left, where the first row is. Its pictures would load there and let go when
// the row is placed: every picture of a page decoded once for nothing. So an item's first verdict
// waits until every list row around it is one its view knows by its place (`itemAt()`; the view's
// header, footer and highlight are no rows), at most `placedWaitMs`.
#pragma once

#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QtQml/qqmlregistration.h>

#include <functional>

class QQuickItem;
class QQuickWindow;

namespace brulion::ui {

class NearViewTracker;

/// Attached to an Item: `NearView.near`. Not an Item: always near (warns once).
class NearView : public QObject {
    Q_OBJECT
    // (Resolved on this class; the app registers it as "NearView": src/app/ui/qmlforeign.h.)
    QML_ATTACHED(brulion::ui::NearView)

    /// The item is shown and in the window's view or within reach of it (see above).
    Q_PROPERTY(bool near READ isNear NOTIFY nearChanged FINAL)
    /// false: not watched, `near` is true wherever the item is (default true). Watched again,
    /// it stays near until its next look.
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged FINAL)

public:
    /// How far "near" reaches, for every NearView of the process.
    struct Reach {
        /// Window heights (widths, sideways) around the window within which an item becomes near.
        qreal enter = 1.0;
        /// … and beyond which a near item stops being so (≥ enter).
        qreal leave = 2.0;
        /// A window lower than this counts as this high (a small window scrolls as fast).
        qreal minScreen = 480;
        /// A hidden item stays near this long (ms).
        int hiddenGraceMs = 30000;
        /// An item waits this long (ms) at most for its list to place its row (see above).
        int placedWaitMs = 2000;
    };
    static Reach reach();
    /// (Tests; a device could be given another reach.) Applies from the next look.
    static void setReach(const Reach &reach);
    /// BRULION_NEAR_VIEW=0 in the environment: every item is near (A/B measurements).
    static bool switchedOff();

    explicit NearView(QObject *item);
    ~NearView() override;

    static NearView *qmlAttachedProperties(QObject *object);

    bool isNear() const { return m_near || !m_enabled; }
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    /// Every tracked item of every window looked at now (tests; the app never needs it).
    static void checkAll();
    /// How many items became far since the process started (tests).
    static qint64 releases();
    /// Called whenever an item becomes far (the memory keeper: what it held is let go).
    static void setReleased(std::function<void()> released);

Q_SIGNALS:
    void nearChanged();
    void enabledChanged();

private:
    friend class NearViewTracker;
    void attach(QQuickWindow *window);
    /// One look: `view` the window's rectangle, `shown` whether the window shows anything, `now`
    /// in ms; `near` gets the verdict (left alone: no change). Returns the ms until this item
    /// wants its next look whatever is drawn — its grace ends, its row should be placed by then —
    /// (-1: none).
    qint64 look(const QRectF &view, bool shown, qint64 now, const Reach &reach, bool *near);
    void setNear(bool near);

    QPointer<QQuickItem> m_item;
    QPointer<NearViewTracker> m_tracker;
    bool m_enabled = true;
    bool m_near = false;
    bool m_placed = false; ///< every list row around the item was placed by its view (once true, it stays)
    qint64 m_hiddenSince = -1;
    qint64 m_waitsSince = -1; ///< for its row to be placed
};

} // namespace brulion::ui
