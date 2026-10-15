#include "dayclock.h"

#include <QCoreApplication>

#include <algorithm>
#include <chrono>

namespace brulion::editor {

DayClock *DayClock::instance()
{
    // Owned by the application object (gone with it), or leaked deliberately without one.
    static DayClock *clock = new DayClock(QCoreApplication::instance());
    return clock;
}

DayClock::DayClock(QObject *parent) : QObject(parent), m_clock([] { return QDateTime::currentDateTime(); })
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &DayClock::check);
    m_day = m_clock().date();
    schedule();
}

void DayClock::setClock(std::function<QDateTime()> clock)
{
    m_clock = clock ? std::move(clock) : std::function<QDateTime()>([] { return QDateTime::currentDateTime(); });
    check();
}

void DayClock::check()
{
    const QDate day = m_clock().date();
    schedule();
    if (day == m_day || !day.isValid())
        return;
    m_day = day;
    Q_EMIT dayChanged(day);
}

void DayClock::schedule()
{
    const QDateTime now = m_clock();
    const QDateTime next(now.date().addDays(1), QTime(0, 0, 1));
    const qint64 ms = std::clamp<qint64>(now.msecsTo(next), 1000, 3600 * 1000);
    m_timer.start(std::chrono::milliseconds(ms));
}

} // namespace brulion::editor
