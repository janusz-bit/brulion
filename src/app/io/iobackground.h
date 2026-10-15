// runInBackground — heavy, self-contained work (unpacking an archive, running an importer or an
// exporter over plain data) off the GUI thread, with the result handed back on the GUI thread.
//
//   runInBackground<io::ImportResult>(this, [files, options] { return io::runImport(…); },
//                                     [this](io::ImportResult r) { … });
//
// `work` runs on a QThreadPool thread when the build has threads (desktop, Android) and must only
// touch the data it captured by value (Qt containers are implicitly shared: copies are safe);
// single-threaded builds (WebAssembly) run it from the event loop instead, so the UI can show a
// busy state first. `done` runs on the thread of QCoreApplication, and only while `context` still
// exists (results for a destroyed controller are dropped). Callers that start several runs keep a
// generation counter to ignore stale results.
#pragma once

#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <memory>

#if QT_CONFIG(thread) && !defined(Q_OS_WASM)
#include <QThreadPool>
#define BRULION_IOVIEWS_THREADS 1
#endif

namespace brulion::ioviews {

/// Force running inline from the event loop also where threads exist (tests of the fallback).
inline bool &backgroundInline()
{
    static bool inlineRuns = false;
    return inlineRuns;
}

template<typename Result>
void runInBackground(QObject *context, std::function<Result()> work, std::function<void(Result)> done)
{
    QPointer<QObject> guard(context);
    auto finish = [guard, done = std::move(done)](std::shared_ptr<Result> result) {
        if (guard)
            done(std::move(*result));
    };
#ifdef BRULION_IOVIEWS_THREADS
    if (!backgroundInline()) {
        QThreadPool::globalInstance()->start([work = std::move(work), finish = std::move(finish)]() mutable {
            auto result = std::make_shared<Result>(work());
            QMetaObject::invokeMethod(
                    QCoreApplication::instance(), [finish = std::move(finish), result] { finish(result); },
                    Qt::QueuedConnection);
        });
        return;
    }
#endif
    QTimer::singleShot(0, QCoreApplication::instance(), [work = std::move(work), finish = std::move(finish)] {
        finish(std::make_shared<Result>(work()));
    });
}

} // namespace brulion::ioviews
