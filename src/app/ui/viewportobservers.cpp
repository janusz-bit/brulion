#include "viewportobservers.h"

#include <QEvent>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQuick/private/qquickitem_p.h>
#include <QtQuick/private/qquicktext_p.h>

namespace brulion::ui {

namespace {

/// A `Text` whose text stays empty: its flag is Qt's start, never dropped (see the header).
bool isEmptyText(QQuickItem *item)
{
    const auto *text = qobject_cast<QQuickText *>(item);
    return text && text->text().isEmpty();
}

/// The look at `item`'s tree, along the paths Qt walks (QQuickItemPrivate::transformChanged):
/// whether an observer is left in it.
bool look(QQuickItem *item, int &letGo)
{
    QQuickItemPrivate *d = QQuickItemPrivate::get(item);
    bool below = false;
    if (d->subtreeTransformChangedEnabled) {
        for (QQuickItem *child : std::as_const(d->childItems))
            below |= look(child, letGo);
    }
    if (item->flags().testFlag(QQuickItem::ItemObservesViewport) && isEmptyText(item)) {
        item->setFlag(QQuickItem::ItemObservesViewport, false);
        ++letGo;
    }
    const bool observes = below || item->flags().testFlag(QQuickItem::ItemObservesViewport);
    // As Qt does after a walk: an observer added later switches the path on again
    // (QQuickItem::setFlag, a child added: QQuickItemPrivate::itemChange).
    if (!observes && d->componentComplete)
        d->subtreeTransformChangedEnabled = false;
    return observes;
}

} // namespace

ViewportObservers::ViewportObservers() = default;

ViewportObservers *ViewportObservers::instance()
{
    static ViewportObservers *installed = [] {
        auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
        if (!app || switchedOff())
            return static_cast<ViewportObservers *>(nullptr);
        auto *observers = new ViewportObservers;
        observers->setParent(app);
        app->installEventFilter(observers);
        const QWindowList windows = QGuiApplication::allWindows();
        for (QWindow *window : windows)
            watch(qobject_cast<QQuickWindow *>(window));
        return observers;
    }();
    return installed;
}

bool ViewportObservers::switchedOff()
{
    return qEnvironmentVariable("BRULION_VIEWPORT_OBSERVERS") == u"0";
}

void ViewportObservers::watch(QQuickWindow *window)
{
    // (Marked on the window: Qt::UniqueConnection does not tell two lambdas apart.)
    static constexpr const char *Watched = "_brulionViewportObservers";
    if (!window || window->property(Watched).toBool())
        return;
    window->setProperty(Watched, true);
    QObject::connect(window, &QQuickWindow::afterAnimating, window, [window] { sweep(window->contentItem()); });
}

int ViewportObservers::sweep(QQuickItem *root)
{
    int letGo = 0;
    if (root)
        look(root, letGo);
    return letGo;
}

bool ViewportObservers::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Show && watched->isWindowType())
        watch(qobject_cast<QQuickWindow *>(watched));
    return QObject::eventFilter(watched, event);
}

} // namespace brulion::ui
