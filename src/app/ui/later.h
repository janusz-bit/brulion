// Later — Qt.callLater() for QML compiled ahead of time (docs/UI.md "Compiled QML"): closures do
// not compile, so the deferred work is the handler of `triggered`, and `restart()` asks for it.
//
//   Later { id: revealLater; onTriggered: editor.revealRow(row) }
//   revealLater.restart()
//
// Like Qt.callLater(), the call is a posted event: it runs once the event loop is back, before the
// timers that are due by then (a `Timer { interval: 0 }` runs after them — on a slow machine after
// an overdue timer started earlier), and requests before it runs are one call. stop() forgets a
// pending call. Library brulion_ui_helpers (QtCore only), type `Later` in module Brulion
// (registered by qmlforeign.h); tests/app/tst_later.cpp.
#pragma once

#include <QObject>

namespace brulion::ui {

class Later : public QObject {
    Q_OBJECT
    /// A call is pending.
    Q_PROPERTY(bool running READ running NOTIFY runningChanged FINAL)

public:
    explicit Later(QObject *parent = nullptr);

    bool running() const { return m_pending; }

    /// triggered() once the event loop is back (once, however often it is asked for meanwhile).
    Q_INVOKABLE void restart();
    /// The same as restart() (Timer's name).
    Q_INVOKABLE void start();
    /// A pending call does not happen.
    Q_INVOKABLE void stop();

Q_SIGNALS:
    void triggered();
    void runningChanged();

private:
    void fire(quint64 generation);

    bool m_pending = false;
    quint64 m_generation = 0;
};

} // namespace brulion::ui
