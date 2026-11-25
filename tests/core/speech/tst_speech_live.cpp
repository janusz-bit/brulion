// Text while recording (src/core/speech/live.h) and the threads of speech recognition on a busy
// processor (src/core/speech/load.h). The windows: due a step at a time, growing until long enough,
// then kept — all their segments but one cut at the window's end, which the next window hears again
// —, a window of one segment kept whole with an overlap and the word repeated at the seam dropped,
// silence kept as nothing, a window the recording moved past ignored, a recognizer far behind (the
// oldest never heard live), the longest window, too little to hear; the tail a small field shows;
// Whisper's loops (a phrase again and again) said once.
// The load: /proc/stat's and /proc/<pid>/stat's lines (names with spaces and parentheses, old
// kernels, garbage), what others kept busy between two looks, the threads chosen from it.
#include "speech/live.h"
#include "speech/load.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using speech::LiveWindows;
using speech::Segment;

namespace {

/// `ms` of a quiet tone at 1000 samples a second (one sample a millisecond: the arithmetic stays
/// readable).
QList<float> sound(qint64 ms)
{
    return QList<float>(ms, 0.1f);
}

} // namespace

class TestSpeechLive : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void windowsGrowThenAreKept()
    {
        LiveWindows live(1000);
        QCOMPARE(live.sampleRate(), 1000);
        live.append(sound(2999));
        QVERIFY(!live.due());
        live.append(sound(1));
        QVERIFY(live.due());
        QCOMPARE(live.recordedMs(), 3000);

        LiveWindows::Window w = live.take();
        QCOMPARE(w.from, 0);
        QCOMPARE(w.to, 3000);
        QCOMPARE(w.samples.size(), 3000);
        QVERIFY(!w.closing);
        QVERIFY(!live.due()); // (taken: the next one a step later)
        live.heard(w, {{0, 2800, u" Dzień dobry"_s}});
        QCOMPARE(live.tentative(), u"Dzień dobry"_s);
        QCOMPARE(live.committed(), QString());
        QCOMPARE(live.text(), u"Dzień dobry"_s);

        // The same window, longer: its text replaces the one before.
        live.append(sound(3000));
        QVERIFY(live.due());
        w = live.take();
        QCOMPARE(w.from, 0);
        QCOMPARE(w.to, 6000);
        QVERIFY(!w.closing);
        live.heard(w, {{0, 2500, u"Dzień dobry,"_s}, {2500, 5900, u"dziś omówimy"_s}});
        QCOMPARE(live.text(), u"Dzień dobry, dziś omówimy"_s);

        // Ten seconds: kept — but the last segment reaches the end (a word may be cut there): heard
        // again by the next window.
        live.append(sound(4000));
        w = live.take();
        QCOMPARE(w.to, 10000);
        QVERIFY(w.closing);
        live.heard(w, {{0, 2500, u"Dzień dobry,"_s}, {2500, 6000, u"dziś omówimy plan"_s}, {6000, 10000, u"na jut"_s}});
        QCOMPARE(live.committed(), u"Dzień dobry, dziś omówimy plan"_s);
        QCOMPARE(live.tentative(), QString());
        QCOMPARE(live.committedSegments(),
                 (QList<Segment>{{0, 2500, u"Dzień dobry,"_s}, {2500, 6000, u"dziś omówimy plan"_s}}));
        QVERIFY(!live.due());

        live.append(sound(3000));
        QVERIFY(live.due());
        w = live.take();
        QCOMPARE(w.from, 6000);
        QCOMPARE(w.to, 13000);
        QCOMPARE(w.samples.size(), 7000);
        QVERIFY(!w.closing);
        live.heard(w, {{0, 1500, u"na jutro."_s}, {1500, 6800, u"Żółwie też."_s}});
        QCOMPARE(live.text(), u"Dzień dobry, dziś omówimy plan na jutro. Żółwie też."_s);
    }

    void oneSegmentKeptWholeAndTheSeam()
    {
        LiveWindows live(1000);
        live.append(sound(10000));
        LiveWindows::Window w = live.take();
        QVERIFY(w.closing);
        live.heard(w, {{0, 9990, u"Kupić mleko"_s}});
        QCOMPARE(live.committed(), u"Kupić mleko"_s);
        // The next window starts a moment before the end (the overlap) …
        live.append(sound(3000));
        w = live.take();
        QCOMPARE(w.from, 9800);
        QCOMPARE(w.to, 13000);
        // … and the word heard twice there counts once.
        live.heard(w, {{0, 3000, u"Mleko i chleb"_s}});
        QCOMPARE(live.text(), u"Kupić mleko i chleb"_s);
    }

    void silenceIsKeptAsNothing()
    {
        LiveWindows live(1000);
        live.append(sound(12000));
        const LiveWindows::Window w = live.take();
        QCOMPARE(w.to, 12000);
        QVERIFY(w.closing);
        live.heard(w, {{0, 12000, u"[BLANK_AUDIO]"_s}});
        QCOMPARE(live.text(), QString());
        live.append(sound(3000));
        QCOMPARE(live.take().from, 11800);
    }

    void aWindowLeftBehindIsIgnored()
    {
        LiveWindows live(1000);
        live.append(sound(4000));
        const LiveWindows::Window first = live.take();
        LiveWindows::Window other = first;
        other.from = 500;
        live.heard(other, {{0, 1000, u"nie ten"_s}});
        QCOMPARE(live.text(), QString());
        live.heard(first, {{0, 1000, u"ten"_s}});
        QCOMPARE(live.text(), u"ten"_s);
        // Nothing heard: no window at all.
        live.heard(LiveWindows::Window{}, {{0, 1000, u"nic"_s}});
        QCOMPARE(live.text(), u"ten"_s);
    }

    void timesPastTheEndAreTheEnd()
    {
        LiveWindows live(1000);
        live.append(sound(10000));
        const LiveWindows::Window w = live.take();
        live.heard(w, {{-50, 4000, u"Raz."_s}, {4000, 30000, u"Dwa."_s}});
        // (The second reaches the end: heard again next time.)
        QCOMPARE(live.committedSegments(), (QList<Segment>{{0, 4000, u"Raz."_s}}));
        live.append(sound(1000));
        QCOMPARE(live.take().from, 4000);
    }

    void farBehind()
    {
        // A recognizer that never came back: what waited longest is never heard live.
        LiveWindows live(1000);
        live.append(sound(59000));
        live.append(sound(11000));
        const LiveWindows::Window w = live.take();
        QCOMPARE(w.from, 45000);
        QCOMPARE(w.to, 70000);
        QVERIFY(w.closing);
        QCOMPARE(w.samples.size(), 25000);
    }

    void theLongestWindow()
    {
        LiveWindows live(1000);
        live.append(sound(30000));
        const LiveWindows::Window w = live.take();
        QCOMPARE(w.from, 0);
        QCOMPARE(w.to, 25000);
        QVERIFY(w.closing);
        live.heard(w, {{0, 12000, u"Pierwsze zdanie."_s}});
        // The rest is due at once.
        QVERIFY(live.due());
        QCOMPARE(live.take().from, 24800);
    }

    void tooLittleAndReset()
    {
        LiveWindows live(16000);
        live.append(nullptr, 100);
        live.append(QList<float>(8000, 0.0f));
        QVERIFY(!live.due());
        QVERIFY(live.take().isNull());
        LiveWindows::Rules quick;
        quick.stepMs = 100;
        quick.shortestMs = 100;
        LiveWindows fast(16000, quick);
        fast.append(QList<float>(1600, 0.0f));
        QVERIFY(fast.due());
        const LiveWindows::Window w = fast.take();
        QCOMPARE(w.to, 1600);
        fast.heard(w, {{0, 100, u"Tak"_s}});
        QCOMPARE(fast.text(), u"Tak"_s);
        fast.reset();
        QCOMPARE(fast.text(), QString());
        QCOMPARE(fast.recordedMs(), 0);
        QVERIFY(!fast.due());
    }

    void tail_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("max");
        QTest::addColumn<QString>("tail");
        QTest::newRow("it fits") << u" Ala ma kota "_s << 20 << u"Ala ma kota"_s;
        QTest::newRow("from a word") << u"Ala ma kota i psa"_s << 8 << u"…i psa"_s;
        QTest::newRow("one long word") << u"Zażółćgęśląjaźń"_s << 5 << u"…ąjaźń"_s;
        QTest::newRow("nothing") << QString() << 10 << QString();
        QTest::newRow("no room") << u"tekst"_s << 0 << QString();
    }
    void tail()
    {
        QFETCH(QString, text);
        QFETCH(int, max);
        QFETCH(QString, tail);
        QCOMPARE(speech::textTail(text, max), tail);
    }

    void repeat_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("next");
        QTest::addColumn<QString>("result");
        QTest::newRow("repeated") << u"Kupić mleko."_s << u"Mleko i chleb"_s << u"i chleb"_s;
        QTest::newRow("another word") << u"Kupić mleko"_s << u"chleb"_s << u"chleb"_s;
        QTest::newRow("nothing before") << QString() << u" Tak "_s << u"Tak"_s;
        QTest::newRow("only the repeat") << u"raz dwa"_s << u"dwa,"_s << QString();
        QTest::newRow("punctuation alone") << u"raz ..."_s << u"... dwa"_s << u"... dwa"_s;
    }
    void repeat()
    {
        QFETCH(QString, before);
        QFETCH(QString, next);
        QFETCH(QString, result);
        QCOMPARE(speech::withoutRepeat(before, next), result);
    }

    void loops_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("result");
        QTest::newRow("a sentence again and again")
                << u"Dzień dobry. Dzień dobry. Dzień dobry. Dzień dobry. Potem model"_s
                << u"Dzień dobry. Potem model"_s;
        QTest::newRow("two words") << u"I to nie w nie w nie w nie w nie w czora"_s << u"I to nie w czora"_s;
        QTest::newRow("a word") << u"puię puię, puię, PUIĘ reko"_s << u"PUIĘ reko"_s;
        QTest::newRow("twice is kept") << u"nie, nie, tak"_s << u"nie, nie, tak"_s;
        QTest::newRow("nothing repeats") << u"Kupić mleko i chleb"_s << u"Kupić mleko i chleb"_s;
        QTest::newRow("spaces") << u"  raz   dwa  "_s << u"raz dwa"_s;
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("punctuation is no word") << u"… … … …"_s << u"… … … …"_s;
        QTest::newRow("a long phrase") << u"a b c d e f g h a b c d e f g h a b c d e f g h koniec"_s
                                       << u"a b c d e f g h koniec"_s;
    }
    void loops()
    {
        QFETCH(QString, text);
        QFETCH(QString, result);
        QCOMPARE(speech::withoutLoops(text), result);
    }

    void aLoopIsHeardOnce()
    {
        // Whisper stuck: the same segment again and again, a phrase inside one.
        LiveWindows live(1000);
        live.append(sound(10000));
        const LiveWindows::Window w = live.take();
        QVERIFY(w.closing);
        live.heard(w, {{0, 1000, u"Dzień dobry."_s},
                       {1000, 2000, u"Dzień dobry."_s},
                       {2000, 3000, u"Dzień dobry."_s},
                       {3000, 4000, u"Dzień dobry."_s},
                       {4000, 6000, u"Potem nie w nie w nie w nie w."_s}});
        QCOMPARE(live.committed(), u"Dzień dobry. Dzień dobry. Potem nie w."_s);
    }

    void procStat_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<bool>("ok");
        QTest::addColumn<qint64>("busy");
        QTest::addColumn<qint64>("total");
        QTest::newRow("Linux 6") << QByteArray("cpu  4705 356 584 3699 23 23 0 0 0 0\ncpu0 1 2 3 4 5 6 7 8 9 10\n")
                                 << true << qint64(5668) << qint64(9390);
        QTest::newRow("an old kernel") << QByteArray("cpu 10 0 5 85") << true << qint64(15) << qint64(100);
        QTest::newRow("not the processor") << QByteArray("intr 12345 0 0") << false << qint64(0) << qint64(0);
        QTest::newRow("words") << QByteArray("cpu a b c d") << false << qint64(0) << qint64(0);
        QTest::newRow("too short") << QByteArray("cpu 1 2") << false << qint64(0) << qint64(0);
        QTest::newRow("negative") << QByteArray("cpu 1 -2 3 4") << false << qint64(0) << qint64(0);
        QTest::newRow("empty") << QByteArray() << false << qint64(0) << qint64(0);
    }
    void procStat()
    {
        QFETCH(QByteArray, text);
        QFETCH(bool, ok);
        QFETCH(qint64, busy);
        QFETCH(qint64, total);
        const std::optional<speech::CpuTimes> t = speech::parseProcStat(text);
        QCOMPARE(t.has_value(), ok);
        if (ok)
            QCOMPARE(*t, (speech::CpuTimes{busy, total}));
    }

    void processTime_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<qint64>("ticks");
        QTest::newRow("a process") << QByteArray(
                "1234 (brulion) S 1 1234 1234 0 -1 4194560 100 0 0 0 250 50 0 0 20 0 8 0")
                                   << qint64(300);
        QTest::newRow("a name with spaces and parentheses")
                << QByteArray("77 (głos (test) 2) R 1 77 77 0 -1 0 1 0 0 0 7 3 0 0 20 0 1 0") << qint64(10);
        QTest::newRow("too short") << QByteArray("1 (x) S 1 2") << qint64(-1);
        QTest::newRow("no name") << QByteArray("garbage") << qint64(-1);
        QTest::newRow("not numbers") << QByteArray("1 (x) S 1 1 1 0 -1 0 1 0 0 0 a b") << qint64(-1);
    }
    void processTime()
    {
        QFETCH(QByteArray, text);
        QFETCH(qint64, ticks);
        QCOMPARE(speech::parseProcessTime(text), ticks);
    }

    void othersBusy()
    {
        QTemporaryDir dir;
        const auto write = [&dir](const QString &name, const QByteArray &bytes) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(bytes);
        };
        write(u"stat"_s, "cpu  700 0 300 3000 0 0 0 0\n");
        write(u"self"_s, "9 (brulion) S 1 9 9 0 -1 0 0 0 0 0 80 20 0 0\n");
        const speech::CpuSample before = speech::sampleCpu(dir.filePath(u"stat"_s), dir.filePath(u"self"_s));
        QVERIFY(before.isValid());
        // 400 ticks passed on 4 processors (one tenth of a second each): 300 busy, 100 of them ours.
        write(u"stat"_s, "cpu  900 0 400 3100 0 0 0 0\n");
        write(u"self"_s, "9 (brulion) S 1 9 9 0 -1 0 0 0 0 0 150 50 0 0\n");
        const speech::CpuSample after = speech::sampleCpu(dir.filePath(u"stat"_s), dir.filePath(u"self"_s));
        QCOMPARE(speech::othersBusy(before, after, 4), 2.0);
        // Ours alone: nobody else.
        write(u"self"_s, "9 (brulion) S 1 9 9 0 -1 0 0 0 0 0 300 100 0 0\n");
        QCOMPARE(speech::othersBusy(before, speech::sampleCpu(dir.filePath(u"stat"_s), dir.filePath(u"self"_s)), 4),
                 0.0);
        // Not known: a system that tells nothing, no time passed.
        const speech::CpuSample none = speech::sampleCpu(dir.filePath(u"nie ma"_s), dir.filePath(u"self"_s));
        QVERIFY(!none.isValid());
        QCOMPARE(speech::othersBusy(none, after, 4), -1.0);
        QCOMPARE(speech::othersBusy(after, after, 4), -1.0);
        QCOMPARE(speech::othersBusy(before, after, 0), -1.0);
        // This machine's own files, where it has them (Linux, Android).
        if (QFile::exists(u"/proc/stat"_s) && QFile::exists(u"/proc/self/stat"_s))
            QVERIFY(speech::sampleCpu().isValid());
    }

    void threads_data()
    {
        QTest::addColumn<int>("fast");
        QTest::addColumn<int>("cpus");
        QTest::addColumn<double>("busy");
        QTest::addColumn<int>("threads");
        QTest::newRow("fast cores, load not known") << 4 << 8 << -1.0 << 4;
        QTest::newRow("at most four") << 8 << 16 << 0.3 << 4;
        QTest::newRow("no fast cores told: half") << 0 << 4 << -1.0 << 2;
        QTest::newRow("a phone's four fast cores") << 4 << 8 << 1.2 << 4;
        QTest::newRow("a busy Raspberry Pi") << 4 << 4 << 3.6 << 1;
        QTest::newRow("one core taken") << 4 << 4 << 1.4 << 3;
        QTest::newRow("two fast cores") << 2 << 8 << 0.0 << 2;
        QTest::newRow("load 65 on 16 cores") << 4 << 16 << 65.0 << 1;
        QTest::newRow("one processor") << 0 << 1 << -1.0 << 1;
    }
    void threads()
    {
        QFETCH(int, fast);
        QFETCH(int, cpus);
        QFETCH(double, busy);
        QFETCH(int, threads);
        QCOMPARE(speech::speechThreads(fast, cpus, busy), threads);
    }
};

QTEST_GUILESS_MAIN(TestSpeechLive)
#include "tst_speech_live.moc"
