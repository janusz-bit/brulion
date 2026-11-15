// The processor cores the model on the device computes on (src/core/ai/cpus.h): read from a
// folder laid out as Linux's /sys/devices/system/cpu, the slow cores of a phone's processor left
// out — processors as their kernels describe them (capacities from the cores' device trees, the
// clocks in kHz).

#include "ai/cpus.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace brulion;
using ai::CpuCore;

namespace {

void write(const QString &path, const QByteArray &text)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

/// A sysfs folder in `root`: core i has `cores[i]` — "capacity/kHz[/siblings]", either of the
/// first two may be "" (no file) or not a number, the third is the list of the logical processors
/// on the same physical core (none: no such file); `online` the list of the online ones ("": no
/// such file).
void layOut(const QString &root, const QStringList &cores, const QByteArray &online)
{
    if (!online.isEmpty())
        write(root + u"/online"_s, online + '\n');
    for (qsizetype i = 0; i < cores.size(); ++i) {
        const QString dir = root + u"/cpu%1"_s.arg(i);
        QVERIFY(QDir().mkpath(dir));
        const QStringList parts = cores[i].split(u'/');
        if (!parts.value(0).isEmpty())
            write(dir + u"/cpu_capacity"_s, parts.value(0).toLatin1() + '\n');
        if (!parts.value(1).isEmpty())
            write(dir + u"/cpufreq/cpuinfo_max_freq"_s, parts.value(1).toLatin1() + '\n');
        if (!parts.value(2).isEmpty())
            write(dir + u"/topology/core_cpus_list"_s, parts.value(2).toLatin1() + '\n');
    }
    // Other entries of the folder that are no cores.
    QVERIFY(QDir().mkpath(root + u"/cpufreq"_s));
    QVERIFY(QDir().mkpath(root + u"/cpuidle"_s));
    write(root + u"/possible"_s, "0-15\n");
}

QStringList times(int n, const QString &core)
{
    QStringList out;
    for (int i = 0; i < n; ++i)
        out << core;
    return out;
}

} // namespace

class TestAiCpus : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void computeCores_data()
    {
        QTest::addColumn<QStringList>("cores");
        QTest::addColumn<QByteArray>("online");
        QTest::addColumn<QList<int>>("expected");

        // Phones: the slow cluster (Cortex-A5xx) is left out, fast cores of two kinds stay.
        QTest::newRow("Pixel 9 Pro (Tensor G4: 4 × A520, 3 × A720, X4)")
                << times(4, u"250/1950000"_s) + times(3, u"620/2600000"_s) + QStringList{u"1024/3105000"_s}
                << QByteArray("0-7") << QList<int>{4, 5, 6, 7};
        QTest::newRow("Snapdragon 8 Gen 3 (2 × A520, 5 × A720, X4)")
                << times(2, u"380/2265600"_s) + times(3, u"900/3148800"_s) + times(2, u"860/2956800"_s)
                           + QStringList{u"1024/3302400"_s}
                << QByteArray("0-7") << QList<int>{2, 3, 4, 5, 6, 7};
        QTest::newRow("Helio G99 (6 × A55, 2 × A76): two threads, not four")
                << times(6, u"400/2000000"_s) + times(2, u"1024/2200000"_s) << QByteArray("0-7") << QList<int>{6, 7};
        QTest::newRow("Exynos 2400 (10 cores: 4 × A520, 5 × A720, X4)")
                << times(4, u"280/1960000"_s) + times(3, u"640/2600000"_s) + times(2, u"720/2900000"_s)
                           + QStringList{u"1024/3210000"_s}
                << QByteArray("0-9") << QList<int>{4, 5, 6, 7, 8, 9};
        // No slow cores: every core computes.
        QTest::newRow("Snapdragon 8 Elite (6 + 2 Oryon)") << times(6, u"830/3532800"_s) + times(2, u"1024/4320000"_s)
                                                          << QByteArray("0-7") << QList<int>{0, 1, 2, 3, 4, 5, 6, 7};
        QTest::newRow("Dimensity 9300 (4 × A720, 4 × X4)")
                << times(4, u"700/2000000"_s) + times(3, u"960/2850000"_s) + QStringList{u"1024/3250000"_s}
                << QByteArray("0-7") << QList<int>{0, 1, 2, 3, 4, 5, 6, 7};
        QTest::newRow("Raspberry Pi 4 (4 × A72)")
                << times(4, u"1024/1800000"_s) << QByteArray("0-3") << QList<int>{0, 1, 2, 3};
        QTest::newRow("no clocks either") << times(4, u"1024/"_s) << QByteArray("0-3") << QList<int>{0, 1, 2, 3};
        // A kernel that does not know the cores' capacities says 1024 for each: their clocks tell.
        QTest::newRow("every capacity 1024, the clocks of a Tensor G4")
                << times(4, u"1024/1950000"_s) + times(3, u"1024/2600000"_s) + QStringList{u"1024/3105000"_s}
                << QByteArray("0-7") << QList<int>{4, 5, 6, 7};
        QTest::newRow("every capacity 1024, the clocks of a Snapdragon 8 Elite")
                << times(6, u"1024/3532800"_s) + times(2, u"1024/4320000"_s) << QByteArray("0-7")
                << QList<int>{0, 1, 2, 3, 4, 5, 6, 7};
        QTest::newRow("every capacity 1024, clocks a little apart")
                << times(4, u"1024/2000000"_s) + times(4, u"1024/2200000"_s) << QByteArray("0-7")
                << QList<int>{0, 1, 2, 3, 4, 5, 6, 7};
        // A PC's processor (a kernel that has capacities: 1024 each): one thread per physical
        // core, not on its SMT sibling, no efficiency cores (their clocks tell).
        QStringList raptorLake;
        for (int i = 0; i < 12; ++i)
            raptorLake << u"1024/4600000/%1-%2"_s.arg(i & ~1).arg(i | 1);
        for (int i = 12; i < 16; ++i)
            raptorLake << u"1024/3400000/%1"_s.arg(i);
        QTest::newRow("i5-13450HX (6 P-cores with SMT, 4 E-cores)")
                << raptorLake << QByteArray("0-15") << QList<int>{0, 2, 4, 6, 8, 10};
        QStringList ryzen;
        for (int i = 0; i < 16; ++i)
            ryzen << u"1024/5100000/%1,%2"_s.arg(i % 8).arg(i % 8 + 8);
        QTest::newRow("Ryzen 7 (8 cores, siblings 8 apart)")
                << ryzen << QByteArray("0-15") << QList<int>{0, 1, 2, 3, 4, 5, 6, 7};
        QTest::newRow("a sibling offline: the other one computes")
                << QStringList{u"1024/4000000/0-1"_s, u"1024/4000000/0-1"_s, u"1024/4000000/2-3"_s,
                               u"1024/4000000/2-3"_s}
                << QByteArray("1-3") << QList<int>{1, 2};
        QTest::newRow("a sibling list that is none: each its own core")
                << times(4, u"1024/4000000/two"_s) << QByteArray("0-3") << QList<int>{0, 1, 2, 3};
        // At most eight, the fastest.
        QTest::newRow("a 16-core server")
                << times(16, u"1024/3000000"_s) << QByteArray("0-15") << QList<int>{0, 1, 2, 3, 4, 5, 6, 7};
        QTest::newRow("12 cores, 4 slow")
                << times(4, u"300/2000000"_s) + times(4, u"900/2800000"_s) + times(4, u"1024/3200000"_s)
                << QByteArray("0-11") << QList<int>{4, 5, 6, 7, 8, 9, 10, 11};
        // Only the online cores.
        QTest::newRow("two fast cores offline")
                << times(4, u"250/1950000"_s) + times(3, u"620/2600000"_s) + QStringList{u"1024/3105000"_s}
                << QByteArray("0-4,7") << QList<int>{4, 7};
        QTest::newRow("no list of the online cores: the cores' folders")
                << times(2, u"400/2000000"_s) + times(2, u"1024/2400000"_s) << QByteArray() << QList<int>{2, 3};
        QTest::newRow("a list that is none: the cores' folders")
                << times(2, u"400/2000000"_s) + times(2, u"1024/2400000"_s) << QByteArray("zero to three")
                << QList<int>{2, 3};
        // A one-core gap is no cluster of fast cores: all of them.
        QTest::newRow("one fast core") << times(3, u"400/1800000"_s) + QStringList{u"1024/2800000"_s}
                                       << QByteArray("0-3") << QList<int>{0, 1, 2, 3};
        // Not told how fast the cores are (a PC with an older kernel: SMT siblings, efficiency
        // cores unknown): none chosen — the runner's own rule.
        QTest::newRow("a PC's processor (no capacities)")
                << times(16, u"/4600000"_s) << QByteArray("0-15") << QList<int>{};
        QTest::newRow("one core without its capacity")
                << times(3, u"1024/2000000"_s) + QStringList{u"/2000000"_s} << QByteArray("0-3") << QList<int>{};
        QTest::newRow("a capacity that is no number")
                << times(3, u"1024/2000000"_s) + QStringList{u"fast/2000000"_s} << QByteArray("0-3") << QList<int>{};
        QTest::newRow("no cores") << QStringList{} << QByteArray() << QList<int>{};
    }

    void computeCores()
    {
        QFETCH(QStringList, cores);
        QFETCH(QByteArray, online);
        QFETCH(QList<int>, expected);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.filePath(u"devices/system/cpu"_s);
        layOut(root, cores, online);
        QCOMPARE(ai::computeCores(ai::cpuCores(root)), expected);
    }

    void readsTheCores()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(u"cpu"_s);
        layOut(root, {u"250/1950000"_s, u" 620 /2600000"_s, u"1024/"_s, u"/3105000"_s}, "0-3");
        const QList<CpuCore> cores = ai::cpuCores(root);
        QCOMPARE(cores.size(), 4);
        QCOMPARE(cores[0], (CpuCore{0, 250, 1950000}));
        QCOMPARE(cores[1], (CpuCore{1, 620, 2600000}));
        QCOMPARE(cores[2], (CpuCore{2, 1024, 0}));
        QCOMPARE(cores[3], (CpuCore{3, 0, 3105000}));
        // The cores sharing one physical core: by the list's newer name or its older one.
        write(root + u"/cpu0/topology/core_cpus_list"_s, "0-1\n");
        write(root + u"/cpu1/topology/thread_siblings_list"_s, "0-1\n");
        write(root + u"/cpu2/topology/core_cpus_list"_s, "2\n");
        write(root + u"/cpu3/topology/core_cpus_list"_s, "0-1\n"); // not its own: not told
        QCOMPARE(ai::cpuCores(root)[0].firstSibling, 0);
        QCOMPARE(ai::cpuCores(root)[1].firstSibling, 0);
        QCOMPARE(ai::cpuCores(root)[2].firstSibling, 2);
        QCOMPARE(ai::cpuCores(root)[3].firstSibling, -1);

        // No such folder (Windows, macOS, the browser): no cores.
        QVERIFY(ai::cpuCores(dir.filePath(u"nonexistent"_s)).isEmpty());
        // A list of online cores that names a core without a folder: one known by its number only.
        write(root + u"/online"_s, "0-4\n");
        QCOMPARE(ai::cpuCores(root).size(), 5);
        QCOMPARE(ai::cpuCores(root).last(), (CpuCore{4, 0, 0}));

        // This machine's own: whatever it says, the cores chosen are among its online ones.
        const QList<CpuCore> here = ai::cpuCores();
        const QList<int> chosen = ai::computeCores(here);
        QVERIFY(chosen.size() <= ai::MaxComputeThreads);
        for (const int core : chosen)
            QVERIFY(std::ranges::any_of(here, [&](const CpuCore &c) { return c.index == core; }));
        qInfo("this machine: %lld cores, computing on %s", qlonglong(here.size()),
              chosen.isEmpty() ? "(not chosen)" : qPrintable(ai::coreList(chosen)));
    }

    void coreList()
    {
        QCOMPARE(ai::coreList({}), QString());
        QCOMPARE(ai::coreList({3}), u"3"_s);
        QCOMPARE(ai::coreList({4, 5, 6, 7}), u"4-7"_s);
        QCOMPARE(ai::coreList({0, 2, 4, 5}), u"0,2,4-5"_s);
        QCOMPARE(ai::coreList({0, 1, 3, 4, 5, 9}), u"0-1,3-5,9"_s);
    }
};

QTEST_GUILESS_MAIN(TestAiCpus)
#include "tst_ai_cpus.moc"
