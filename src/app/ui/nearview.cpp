#include "nearview.h"

#include <QElapsedTimer>
#include <QEvent>
#include <QGuiApplication>
#include <QList>
#include <QLoggingCategory>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QVarLengthArray>
#include <QVariant>

#include <algorithm>
#include <utility>

namespace brulion::ui {

namespace {

Q_LOGGING_CATEGORY(lcNearView, "brulion.nearview", QtWarningMsg)

NearView::Reach g_reach;
qint64 g_releases = 0;

std::function<void()> &releasedHook()
{
    static std::function<void()> hook;
    return hook;
}

/// The window's tracker hangs on it under this dynamic property (no table of windows to keep).
constexpr char TrackerProperty[] = "_brulion_nearViewTracker";

/// Is every list row around `item` one its view has placed (nearview.h)? The rows of an item
/// view are children of its content item; the view tells a row it shows by a point in it.
bool placedByViews(const QQuickItem *item)
{
    for (const QQuickItem *row = item; row; row = row->parentItem()) {
        const QQuickItem *content = row->parentItem();
        QQuickItem *view = content ? content->parentItem() : nullptr;
        if (!view || !view->inherits("QQuickItemView")
            || view->property("contentItem").value<QQuickItem *>() != content)
            continue;
        if (row == view->property("headerItem").value<QQuickItem *>()
            || row == view->property("footerItem").value<QQuickItem *>()
            || row == view->property("highlightItem").value<QQuickItem *>())
            continue;
        QQuickItem *shown = nullptr;
        if (!QMetaObject::invokeMethod(view, "itemAt", Q_RETURN_ARG(QQuickItem *, shown),
                                       Q_ARG(qreal, row->x() + row->width() / 2),
                                       Q_ARG(qreal, row->y() + row->height() / 2)))
            continue; // (not a view that tells)
        if (shown != row)
            return false;
    }
    return true;
}

qint64 nowMs()
{
    static const QElapsedTimer clock = [] {
        QElapsedTimer timer;
        timer.start();
        return timer;
    }();
    return clock.elapsed();
}

} // namespace

/// The NearViews of one window, looked at once a frame (a child of the window: it goes with it).
class NearViewTracker : public QObject {
public:
    static NearViewTracker *existing(QQuickWindow *window)
    {
        return window ? static_cast<NearViewTracker *>(window->property(TrackerProperty).value<QObject *>()) : nullptr;
    }
    static NearViewTracker *forWindow(QQuickWindow *window)
    {
        if (NearViewTracker *tracker = existing(window))
            return tracker;
        auto *tracker = new NearViewTracker(window);
        window->setProperty(TrackerProperty, QVariant::fromValue<QObject *>(tracker));
        return tracker;
    }

    void add(NearView *view)
    {
        if (m_views.contains(view))
            return;
        m_views.append(view);
        if (m_views.size() == 1) {
            // After the frame's polish, before its sync: the rows are where this frame shows them.
            m_frame = connect(m_window, &QQuickWindow::afterAnimating, this, &NearViewTracker::check);
            m_window->installEventFilter(this);
        }
        schedule();
    }
    void remove(NearView *view)
    {
        const qsizetype at = m_views.indexOf(view);
        if (at < 0)
            return;
        m_views[at] = m_views.constLast();
        m_views.removeLast();
        if (m_views.isEmpty()) {
            disconnect(m_frame);
            m_window->removeEventFilter(this);
            m_grace.stop();
        }
    }
    /// A look in the next turn of the event loop (an item added or shown, the window exposed):
    /// before the next frame, and also when none is drawn.
    void schedule()
    {
        if (m_scheduled)
            return;
        m_scheduled = true;
        QMetaObject::invokeMethod(
                this,
                [this] {
                    m_scheduled = false;
                    check();
                },
                Qt::QueuedConnection);
    }

    void check()
    {
        if (m_views.isEmpty() || m_checking)
            return;
        m_checking = true;
        const NearView::Reach reach = g_reach;
        const bool shown = m_window->isVisible() && m_window->isExposed();
        const QRectF view(0, 0, m_window->width(), m_window->height());
        const qint64 now = nowMs();
        qint64 grace = -1;
        // The verdicts first, then their signals: a handler may add or remove items here.
        struct Change {
            QPointer<NearView> view;
            bool near = false;
        };
        QVarLengthArray<Change, 16> changes;
        for (NearView *v : std::as_const(m_views)) {
            bool near = v->m_near;
            const qint64 left = v->look(view, shown, now, reach, &near);
            if (left >= 0 && (grace < 0 || left < grace))
                grace = left;
            if (near != v->m_near)
                changes.append(Change{v, near});
        }
        // (Also the items waiting for their row to be placed: no frame may come for them.)
        if (grace < 0)
            m_grace.stop();
        else if (!m_grace.isActive() || m_grace.remainingTime() > grace + 1)
            m_grace.start(int(std::min<qint64>(grace + 1, 3600000)));
        for (const Change &change : std::as_const(changes)) {
            if (change.view)
                change.view->setNear(change.near);
        }
        m_checking = false;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_window) {
            switch (event->type()) {
            case QEvent::Expose:
            case QEvent::Show:
            case QEvent::Hide: schedule(); break;
            default: break;
            }
        }
        return false;
    }

private:
    explicit NearViewTracker(QQuickWindow *window) : QObject(window), m_window(window)
    {
        m_grace.setSingleShot(true);
        m_grace.setTimerType(Qt::CoarseTimer);
        connect(&m_grace, &QTimer::timeout, this, &NearViewTracker::check);
    }

    QQuickWindow *m_window = nullptr;
    QList<NearView *> m_views;
    QMetaObject::Connection m_frame;
    QTimer m_grace;
    bool m_scheduled = false;
    bool m_checking = false;
};

NearView::Reach NearView::reach()
{
    return g_reach;
}

void NearView::setReach(const Reach &reach)
{
    g_reach = reach;
    g_reach.leave = std::max(reach.leave, reach.enter);
}

bool NearView::switchedOff()
{
    static const bool off = qEnvironmentVariable("BRULION_NEAR_VIEW") == QLatin1String("0");
    return off;
}

NearView::NearView(QObject *item) : QObject(item), m_item(qobject_cast<QQuickItem *>(item))
{
    if (!m_item) {
        static bool warned = false;
        if (!std::exchange(warned, true))
            qCWarning(lcNearView) << "NearView attached to" << item << "which is no Item: always near";
        m_near = true;
        return;
    }
    if (switchedOff()) {
        m_near = true;
        return;
    }
    connect(m_item, &QQuickItem::windowChanged, this, &NearView::attach);
    connect(m_item, &QQuickItem::visibleChanged, this, [this] {
        if (m_tracker)
            m_tracker->schedule();
    });
    attach(m_item->window());
}

NearView::~NearView()
{
    if (m_tracker)
        m_tracker->remove(this);
}

NearView *NearView::qmlAttachedProperties(QObject *object)
{
    return new NearView(object);
}

void NearView::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    const bool was = isNear();
    m_enabled = enabled;
    // Watched from now on: what it held while it was not stays until a look says it is far.
    if (enabled)
        m_near = true;
    if (m_item && !switchedOff())
        attach(m_item->window());
    Q_EMIT enabledChanged();
    if (was != isNear())
        Q_EMIT nearChanged();
}

void NearView::checkAll()
{
    const QWindowList windows = QGuiApplication::allWindows();
    for (QWindow *window : windows) {
        if (NearViewTracker *tracker = NearViewTracker::existing(qobject_cast<QQuickWindow *>(window)))
            tracker->check();
    }
}

qint64 NearView::releases()
{
    return g_releases;
}

void NearView::setReleased(std::function<void()> released)
{
    releasedHook() = std::move(released);
}

void NearView::attach(QQuickWindow *window)
{
    NearViewTracker *tracker = m_enabled && window ? NearViewTracker::forWindow(window) : nullptr;
    if (tracker == m_tracker) {
        if (tracker)
            tracker->schedule();
        return;
    }
    if (m_tracker)
        m_tracker->remove(this);
    m_tracker = tracker;
    if (tracker) {
        tracker->add(this);
    } else if (m_enabled) {
        // No window: nothing shows the item. Told in the next turn of the event loop, not from
        // here: an item leaves its window also while it is destroyed, and what follows `near`
        // (QML) must not run on an item half gone — this object goes with it, and the call too.
        m_hiddenSince = -1;
        QMetaObject::invokeMethod(
                this,
                [this] {
                    if (!m_tracker)
                        setNear(false);
                },
                Qt::QueuedConnection);
    }
}

qint64 NearView::look(const QRectF &view, bool shown, qint64 now, const Reach &reach, bool *near)
{
    const QQuickItem *item = m_item;
    if (!item) {
        *near = false;
        return -1;
    }
    if (!shown || !item->isVisible()) {
        if (!m_near)
            return -1;
        if (m_hiddenSince < 0)
            m_hiddenSince = now;
        const qint64 left = reach.hiddenGraceMs - (now - m_hiddenSince);
        if (left > 0)
            return left;
        *near = false;
        m_hiddenSince = -1;
        return -1;
    }
    m_hiddenSince = -1;
    if (!m_placed) {
        // The row it is in may still be being created, at its list's top (nearview.h).
        if (m_waitsSince < 0)
            m_waitsSince = now;
        const qint64 left = reach.placedWaitMs - (now - m_waitsSince);
        if (left > 0 && !placedByViews(item))
            return left;
        m_placed = true;
    }
    const qreal k = m_near ? reach.leave : reach.enter;
    const qreal dy = k * std::max(view.height(), reach.minScreen);
    const qreal dx = k * std::max(view.width(), reach.minScreen);
    // (An item without a size yet still has a place.)
    const QRectF rect =
            item->mapRectToScene(QRectF(0, 0, std::max<qreal>(1, item->width()), std::max<qreal>(1, item->height())));
    *near = rect.bottom() >= -dy && rect.top() <= view.height() + dy && rect.right() >= -dx
            && rect.left() <= view.width() + dx;
    return -1;
}

void NearView::setNear(bool near)
{
    if (m_near == near)
        return;
    const bool was = isNear();
    m_near = near;
    // (QT_LOGGING_RULES=brulion.nearview.debug=true: what comes within reach and what leaves.)
    qCDebug(lcNearView) << (near ? "near" : "far") << m_item.data();
    if (was != isNear())
        Q_EMIT nearChanged();
    if (!near) {
        ++g_releases;
        if (const std::function<void()> &hook = releasedHook())
            hook();
    }
}

} // namespace brulion::ui
