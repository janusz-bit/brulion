// Internal: work of the codefiles area off the GUI thread — building an index, reading and lexing
// a file, Brulion's own search — with the result handed back on the GUI thread, only while the
// context object lives (the io area's runInBackground() pattern). Builds without threads
// (WebAssembly) run it from the event loop.
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
#define BRULION_CODEFILES_THREADS 1
#endif

namespace brulion::codefiles::detail {

template<typename Result>
void onPool(QObject *context, std::function<Result()> work, std::function<void(Result)> done)
{
    QPointer<QObject> guard(context);
    auto finish = [guard, done = std::move(done)](std::shared_ptr<Result> result) {
        if (guard)
            done(std::move(*result));
    };
#ifdef BRULION_CODEFILES_THREADS
    QThreadPool::globalInstance()->start([work = std::move(work), finish = std::move(finish)]() mutable {
        auto result = std::make_shared<Result>(work());
        QMetaObject::invokeMethod(
                QCoreApplication::instance(), [finish = std::move(finish), result] { finish(result); },
                Qt::QueuedConnection);
    });
#else
    QTimer::singleShot(0, QCoreApplication::instance(), [work = std::move(work), finish = std::move(finish)] {
        finish(std::make_shared<Result>(work()));
    });
#endif
}

} // namespace brulion::codefiles::detail
