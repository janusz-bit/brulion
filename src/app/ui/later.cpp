#include "later.h"

#include <QMetaObject>

namespace brulion::ui {

Later::Later(QObject *parent)
    : QObject(parent)
{
}

void Later::restart()
{
    if (m_pending)
        return;
    m_pending = true;
    const quint64 generation = ++m_generation;
    // A posted event, like Qt.callLater(): ahead of the timers due when the event loop is back.
    QMetaObject::invokeMethod(this, [this, generation] { fire(generation); }, Qt::QueuedConnection);
    Q_EMIT runningChanged();
}

void Later::start()
{
    restart();
}

void Later::stop()
{
    if (!m_pending)
        return;
    m_pending = false;
    ++m_generation; // the posted call finds another generation and does nothing
    Q_EMIT runningChanged();
}

void Later::fire(quint64 generation)
{
    if (!m_pending || generation != m_generation)
        return;
    m_pending = false;
    Q_EMIT runningChanged();
    Q_EMIT triggered();
}

} // namespace brulion::ui
