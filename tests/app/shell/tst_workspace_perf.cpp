// Start-up of a big notebook as the app does it: Workspace::open() on a vault folder of 3000
// notes (~30 lines each: tasks, links, block references, reminders, tags) written to disk, the
// index built in chunks on the event loop, then the first queries the views make. Measures what
// matters for the UI thread: how long open() itself blocks, the longest pause between two
// event-loop turns while indexing (a 1 ms probe timer — what the user feels as a stutter), the
// time until `ready`, and the cost of the first search / backlinks / query / calendar calls.
//
// Bounds are generous (Raspberry Pi 4 under load, Debug builds); the measured times are printed
// (QTest::qDebug lines "perf: …") so a run on the laptop can be quoted. BRULION_PERF_NOTES
// overrides the number of notes.
#include "workspace.h"
#include "workspacereminders.h"
#include "workspacesync.h"

#include "index/vaultindex.h"
#include "reminders/notifications.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace Qt::StringLiterals;
using brulion::shell::Workspace;

namespace {

const QStringList Words = { u"notatka"_s, u"zadanie"_s, u"żółw"_s,  u"łódź"_s, u"projekt"_s, u"spotkanie"_s,
                            u"raport"_s,  u"zakupy"_s,  u"kawa"_s, u"ogród"_s, u"dom"_s,     u"praca"_s };

/// Deterministic synthetic note `i` (as in tests/core/index/tst_index_perf.cpp), with reminders
/// around "now" so the reminder scheduler has real work at start-up.
QPair<QString, QByteArray> syntheticNote(int i, int total)
{
    QStringList lines;
    const QString day = u"%1"_s.arg(i % 28 + 1, 2, 10, QChar(u'0'));
    const QString month = u"%1"_s.arg(i % 12 + 1, 2, 10, QChar(u'0'));
    const bool journal = i % 3 == 0;
    // Every note its own file: journals are consecutive days back from 2026-06-14.
    const QString path = journal ? u"journals/%1.md"_s.arg(QDate(2026, 6, 14).addDays(-i).toString(Qt::ISODate))
                                 : u"Strony/Folder %1/Strona %2.md"_s.arg(i % 40).arg(i);
    if (i % 5 == 0)
        lines << u"---"_s << u"aliases: [Alias %1]"_s.arg(i) << u"tags: [temat%1]"_s.arg(i % 30) << u"---"_s;
    lines << u"# Notatka %1"_s.arg(i) << QString();
    for (int k = 0; k < 24; ++k) {
        const QString &w = Words[(i + k) % Words.size()];
        const QString target = u"Strona %1"_s.arg((i * 7 + k * 13) % total);
        switch (k % 6) {
        case 0: lines << u"- [ ] %1 numer %2 📅 2026-%3-%4 #%1"_s.arg(w).arg(k).arg(month, day); break;
        case 1: lines << u"  - szczegóły %1 [[%2]] ^b%3x%4"_s.arg(w, target).arg(i).arg(k); break;
        case 2: lines << u"- Tekst o %1 i %2 ((b%3x1))"_s.arg(w, Words[(k * 3) % Words.size()]).arg((i + 1) % total); break;
        case 3: lines << u"- [x] zrobione %1 ✅ 2026-%2-%3"_s.arg(w, month, day); break;
        case 4: lines << u"- przypomnienie %1 ⏰ 2026-%2-%3 09:30"_s.arg(w, month, day); break;
        default: lines << u"Akapit %1: %2 [[Nieistniejąca %1]] https://example.com/%3"_s.arg(k).arg(w).arg(i);
        }
    }
    return { path, lines.join(u'\n').toUtf8() };
}

class NullSink : public brulion::NotificationSink {
public:
    int scheduledCount = 0;
    bool firesWhenClosed() const override { return false; }
    void schedule(const QList<brulion::ScheduledNotification> &list) override { scheduledCount = int(list.size()); }
    void showNow(const brulion::ScheduledNotification &) override { }
};

/// Records the longest gap between two turns of the event loop.
struct StallProbe {
    QTimer timer;
    QElapsedTimer clock;
    qint64 last = 0;
    qint64 worst = 0;
    int ticks = 0;
    StallProbe()
    {
        timer.setInterval(1);
        timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&timer, &QTimer::timeout, [this] {
            const qint64 t = clock.elapsed();
            worst = std::max(worst, t - last);
            last = t;
            ++ticks;
        });
    }
    void start()
    {
        clock.start();
        last = 0;
        worst = 0;
        ticks = 0;
        timer.start();
    }
};

} // namespace

class TestWorkspacePerf : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_total = 3000;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        bool ok = false;
        const int n = qEnvironmentVariableIntValue("BRULION_PERF_NOTES", &ok);
        if (ok && n > 0)
            m_total = n;
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < m_total; ++i) {
            const auto [path, bytes] = syntheticNote(i, m_total);
            const QString file = m_tmp.filePath(u"vault/"_s + path);
            QDir().mkpath(QFileInfo(file).absolutePath());
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(bytes + "\n");
        }
        qDebug("perf: wrote %d notes in %lld ms", m_total, qint64(t.elapsed()));
    }

    void openAndIndexInBackground()
    {
        NullSink sink;
        Workspace ws;
        ws.setClock([] { return QDateTime(QDate(2026, 6, 14), QTime(9, 0)); });
        ws.sync()->setSyncthingDiscovery(false);
        ws.reminders()->setSink(&sink);
        QSignalSpy ready(&ws, &Workspace::readyChanged);
        StallProbe probe;

        QElapsedTimer total;
        total.start();
        QString error;
        QVERIFY2(ws.open(m_tmp.filePath(u"vault"_s), m_tmp.filePath(u"state"_s), &error), qPrintable(error));
        const qint64 openMs = total.elapsed();
        QVERIFY(ws.indexing());

        probe.start();
        QVERIFY(ready.wait(120000) || ws.ready());
        const qint64 readyMs = total.elapsed();
        const qint64 indexingStall = probe.worst;
        // After `ready`: the reminders start in the readyChanged handler, and the index builds its
        // full-text documents once changes pause (300 ms), a few ms per turn: watch that second too.
        probe.worst = 0;
        QTest::qWait(1500);
        probe.timer.stop();
        const qint64 afterReadyStall = probe.worst;

        qDebug("perf: %lld pages, open() blocked %lld ms, ready after %lld ms, longest UI stall while "
               "indexing %lld ms, longest stall in the 1.5 s after ready %lld ms, %d reminders scheduled",
               qint64(ws.pageCount()), openMs, readyMs, indexingStall, afterReadyStall, sink.scheduledCount);
        QVERIFY(ws.ready());
        QVERIFY(ws.reminders()->isRunning());
        QVERIFY(sink.scheduledCount > 0);
        QVERIFY2(openMs < 15000, qPrintable(u"open() took %1 ms"_s.arg(openMs)));
        // The point of chunked indexing: no long freeze while the notebook loads.
        QVERIFY2(indexingStall < 1500, qPrintable(u"the UI thread stalled for %1 ms"_s.arg(indexingStall)));

        // The full-text index was built in the background meanwhile: the first search is fast.
        QCOMPARE(ws.index()->pendingSearchDocuments(), 0);
        // The first calls of the views after start-up (each one blocks the UI thread).
        QElapsedTimer t;
        t.start();
        const QVariantList hits = ws.search(u"zolw"_s);
        const qint64 searchMs = t.restart();
        const QVariantList hits2 = ws.search(u"spotkanie projekt"_s);
        const qint64 search2Ms = t.restart();
        const QVariantList back = ws.backlinks(u"Strony/Folder 1/Strona 1.md"_s);
        const qint64 backMs = t.restart();
        const QVariantMap query = ws.runQuery(u"type:tasks status:open sort:due"_s);
        const qint64 queryMs = t.restart();
        const QVariantList cal = ws.calendarItems(u"2026-06-01"_s, u"2026-06-30"_s);
        const qint64 calMs = t.restart();
        const QVariantList tags = ws.tagList();
        const QVariantList journals = ws.journals(0, 50);
        const qint64 listsMs = t.restart();
        const QVariantList switcher = ws.switcherPages(u"stro 12"_s);
        const qint64 switcherMs = t.restart();
        qDebug("perf: first search %lld ms (%lld hits), second search %lld ms, backlinks %lld ms (%lld), "
               "query tasks %lld ms (%lld of %d), calendar month %lld ms (%lld), tags+journals %lld ms, "
               "switcher %lld ms",
               searchMs, qint64(hits.size()), search2Ms, backMs, qint64(back.size()), queryMs,
               qint64(query.value(u"items"_s).toList().size()), query.value(u"total"_s).toInt(), calMs,
               qint64(cal.size()), listsMs, switcherMs);
        QVERIFY(!hits.isEmpty());
        QVERIFY(!hits2.isEmpty());
        QVERIFY(!tags.isEmpty());
        QVERIFY(!journals.isEmpty());
        QVERIFY(!switcher.isEmpty());

        // An edit in a big vault: the incremental path (index + reminders debounce + revision).
        t.restart();
        QVERIFY(ws.write(u"Strony/Folder 2/Strona 2.md"_s, u"- [ ] nowe zadanie ⏰ 2026-06-14 18:00\n"_s));
        const qint64 writeMs = t.elapsed();
        qDebug("perf: one edit (write + index update) %lld ms", writeMs);
        QVERIFY2(writeMs < 2000, qPrintable(u"an edit took %1 ms"_s.arg(writeMs)));
    }

    void reopenWithState()
    {
        // The second start: History and LocalStore exist, the reminder state is persisted.
        NullSink sink;
        Workspace ws;
        ws.sync()->setSyncthingDiscovery(false);
        ws.reminders()->setSink(&sink);
        QElapsedTimer total;
        total.start();
        QVERIFY(ws.open(m_tmp.filePath(u"vault"_s), m_tmp.filePath(u"state"_s)));
        const qint64 openMs = total.elapsed();
        QSignalSpy ready(&ws, &Workspace::readyChanged);
        QVERIFY(ready.wait(120000) || ws.ready());
        qDebug("perf: second start: open() %lld ms, ready after %lld ms", openMs, qint64(total.elapsed()));
    }
};

QTEST_GUILESS_MAIN(TestWorkspacePerf)
#include "tst_workspace_perf.moc"
