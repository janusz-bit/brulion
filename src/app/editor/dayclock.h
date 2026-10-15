// DayClock — the local date for every open page: one timer, aligned to the next midnight
// (re-checked at least hourly, as the clock or the time zone may jump: suspend, travel), emits
// dayChanged() when the date is no longer the one seen last. PageController follows it, so
// "dziś"/"jutro" chips, overdue colours and ```query results of an open page move on at
// midnight without anyone asking.
#pragma once

#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QTimer>

#include <functional>

namespace brulion::editor {

class DayClock : public QObject {
    Q_OBJECT
public:
    /// The shared clock (created on first use, lives as long as the application).
    static DayClock *instance();

    /// The date seen last ("today").
    QDate today() const { return m_day; }
    /// Tests: the wall clock to read (default QDateTime::currentDateTime). Checks at once.
    void setClock(std::function<QDateTime()> clock);
    /// Compare the date with the one seen last now (the timer does this after midnight).
    void check();

Q_SIGNALS:
    void dayChanged(QDate today);

private:
    explicit DayClock(QObject *parent = nullptr);
    void schedule();

    std::function<QDateTime()> m_clock;
    QDate m_day;
    QTimer m_timer;
};

} // namespace brulion::editor
