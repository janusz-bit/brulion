#include "qmlshutdown.h"

#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickItem>

namespace brulion::ui {

namespace {

/// Rounds at most: each round reaches one level of views nested in delegates deeper (a block
/// editor's rows in a journal day in the journal: two).
constexpr int kMaxRounds = 64;

QList<QQuickItem *> itemsOnApplication(const QCoreApplication *app)
{
    QList<QQuickItem *> items;
    const QObjectList children = app->children();
    for (QObject *child : children) {
        if (auto *item = qobject_cast<QQuickItem *>(child))
            items.append(item);
    }
    return items;
}

} // namespace

void releaseQmlWindows(QQmlApplicationEngine *engine)
{
    QCoreApplication *app = QCoreApplication::instance();
    if (!app)
        return;
    if (engine) {
        // Each window leaves the engine's list as it goes (QObject::destroyed); guarded, since one
        // root object may own another.
        QList<QPointer<QObject>> roots;
        const QList<QObject *> objects = engine->rootObjects();
        for (QObject *object : objects)
            roots.append(object);
        for (const QPointer<QObject> &root : std::as_const(roots))
            delete root.data();
    }
    // Outside an event loop QCoreApplication delivers the deferred deletions asked for by type,
    // also those its loop posted. A round never delivers what its own deletions post (Qt's guard
    // against a live-lock): the next one does, for as long as the application's children change.
    for (int round = 0; round < kMaxRounds; ++round) {
        const QObjectList before = app->children();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (app->children() == before)
            return;
    }
}

void deleteQmlLeftovers()
{
    const QCoreApplication *app = QCoreApplication::instance();
    if (!app)
        return;
    for (int round = 0; round < kMaxRounds; ++round) {
        const QList<QQuickItem *> items = itemsOnApplication(app);
        if (items.isEmpty())
            return;
        // Guarded: deleting one of them may delete another of the list.
        QList<QPointer<QQuickItem>> guarded;
        for (QQuickItem *item : items)
            guarded.append(item);
        for (const QPointer<QQuickItem> &item : std::as_const(guarded))
            delete item.data();
    }
}

int qmlItemsLeftOnApplication()
{
    const QCoreApplication *app = QCoreApplication::instance();
    return app ? int(itemsOnApplication(app).size()) : 0;
}

} // namespace brulion::ui
