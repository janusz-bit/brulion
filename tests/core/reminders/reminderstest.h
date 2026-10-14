// Shared helpers for the reminders tests: index fixtures, a recording NotificationSink and a
// controllable clock. "Today" for the fixtures is Wednesday 2026-10-14 (no DST change before
// 2026-10-25 in Europe, so wall-clock arithmetic in the scheduler tests is time-zone safe).
#pragma once

#include "index/vaultindex.h"
#include "reminders/reminders.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace remtest {

using namespace Qt::StringLiterals;
using brulion::ScheduledNotification;

using Files = QList<QPair<QString, QString>>;

inline const QDate Today(2026, 10, 14);

inline QString lines(const QStringList &l)
{
    return l.join(u'\n');
}

/// Local date-time.
inline QDateTime dt(int y, int m, int d, int h = 0, int min = 0, int s = 0, int ms = 0)
{
    return QDateTime(QDate(y, m, d), QTime(h, min, s, ms));
}

/// Fill `index` with `files` in one batch.
inline void fill(brulion::VaultIndex &index, const Files &files)
{
    brulion::VaultIndex::Batch batch(index);
    for (const auto &[path, content] : files)
        index.update(path, content);
}

/// A sample vault with reminders of every kind (Polish text).
inline Files vault()
{
    return {
            {u"journals/2026-10-14.md"_s,
             lines({u"- [ ] Odebrać paczkę 📅 2026-10-14 ⏰ 2026-10-14 17:30"_s,
                    u"- [x] Zadzwonić do hydraulika ⏰ 2026-10-14 09:00 ✅ 2026-10-14"_s,
                    u"- [-] Anulowane spotkanie ⏰ 2026-10-14 10:00"_s, u"- ⏰! 2026-10-15 06:30 Pobudka na lotnisko"_s,
                    u"- Wczorajsze przypomnienie ⏰ 2026-10-13 08:00"_s, u"- Bez godziny ⏰ 2026-10-16"_s})},
            {u"Zdrowie.md"_s,
             lines({u"# Zdrowie"_s, u""_s, u"- [ ] Łyknąć **witaminy** 🔁 every day ⏰ 2026-10-10 08:00"_s,
                    u"- Siłownia z [[Ludzie/Łukasz|Łukaszem]] 🔁 every week on Monday, Friday ⏰ 2026-10-12 18:00"_s})},
            {u"Projekty/Dom.md"_s, lines({u"- [/] Malowanie salonu 📅 2026-10-16 ⏰ 2026-10-15 12:00"_s,
                                          u"- Spotkanie z ekipą (@2026-10-20 07:30)"_s, u""_s, u"```"_s,
                                          u"- [ ] w kodzie ⏰ 2026-10-14 12:00"_s, u"```"_s})},
    };
}

/// NotificationSink recording every call.
class FakeSink : public brulion::NotificationSink {
public:
    explicit FakeSink(bool firesWhenClosed = false) : m_firesWhenClosed(firesWhenClosed) { }

    bool firesWhenClosed() const override { return m_firesWhenClosed; }
    void schedule(const QList<ScheduledNotification> &notifications) override { scheduled.append(notifications); }
    void showNow(const ScheduledNotification &notification) override { shown.append(notification); }

    QStringList shownTitles() const
    {
        QStringList out;
        for (const ScheduledNotification &n : shown)
            out.append(n.title);
        return out;
    }
    /// "yyyy-MM-dd HH:mm title" of every shown notification.
    QStringList shownKeys() const
    {
        QStringList out;
        for (const ScheduledNotification &n : shown)
            out.append(n.at.toString(u"yyyy-MM-dd HH:mm "_s) + n.title);
        return out;
    }

    QList<QList<ScheduledNotification>> scheduled; ///< every schedule() call
    QList<ScheduledNotification> shown;            ///< every showNow() call

private:
    bool m_firesWhenClosed;
};

/// Wall clock controlled by the test: a fixed time moved with set()/advance(), optionally
/// running in real time from there (run()) for tests of the real timers.
class TestClock {
public:
    explicit TestClock(const QDateTime &start) : m_base(start) { }

    QDateTime now() const { return m_running ? m_base.addMSecs(m_elapsed.elapsed()) : m_base; }
    void set(const QDateTime &time)
    {
        m_base = time;
        m_elapsed.restart();
    }
    void advance(qint64 msec) { set(now().addMSecs(msec)); }
    void run()
    {
        m_running = true;
        m_elapsed.start();
    }
    brulion::ReminderScheduler::Clock fn()
    {
        return [this] { return now(); };
    }

private:
    QDateTime m_base;
    QElapsedTimer m_elapsed;
    bool m_running = false;
};

/// "yyyy-MM-dd HH:mm title" of notifications, for compact comparisons.
inline QStringList keys(const QList<ScheduledNotification> &list)
{
    QStringList out;
    for (const ScheduledNotification &n : list)
        out.append(n.at.toString(u"yyyy-MM-dd HH:mm "_s) + n.title);
    return out;
}

} // namespace remtest
