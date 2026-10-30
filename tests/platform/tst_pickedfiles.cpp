// PickCollector (src/platform/pickedfiles.h): the bookkeeping of the browser's file picker
// (platform/wasm/wasmfiles.h) driven the way Qt's QWasmLocalFileAccess::openFiles calls it — the
// count when the picker closes, then per file "accept?" and, for accepted ones, "read": several
// files with Polish names, an empty file, one above the limit (never read), all of them too
// large, a cancel, and calls after the end.
#include "pickedfiles.h"

#include <QTest>

#include <cstring>

using namespace Qt::StringLiterals;
using brulion::platform::PickCollector;
using brulion::platform::PickedFile;

namespace {

/// One file as the browser offers it.
struct Offered {
    QString name;
    QByteArray bytes;
    quint64 size = 0; ///< as reported (bytes.size() unless faking a huge file)
};

/// Qt's order of calls: closed(n), then for each file accept() and — when a buffer came back —
/// the bytes copied into it and ready().
void drive(PickCollector &collector, const QList<Offered> &files)
{
    collector.closed(int(files.size()));
    for (const Offered &f : files) {
        char *buffer = collector.accept(f.size, f.name);
        if (!buffer)
            continue;
        if (!f.bytes.isEmpty())
            std::memcpy(buffer, f.bytes.constData(), size_t(f.bytes.size()));
        collector.ready();
    }
}

Offered file(const QString &name, const QByteArray &bytes)
{
    return Offered{ name, bytes, quint64(bytes.size()) };
}

} // namespace

class TestPickedFiles : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void picks_data()
    {
        QTest::addColumn<qint64>("maxBytes");
        QTest::addColumn<int>("tooLargeIndex"); // -1: none
        QTest::newRow("no limit") << qint64(-1) << -1;
        QTest::newRow("limit above all") << qint64(1024) << -1;
        QTest::newRow("third too large") << qint64(64) << 2;
    }
    void picks()
    {
        QFETCH(qint64, maxBytes);
        QFETCH(int, tooLargeIndex);
        QList<PickedFile> got;
        int calls = 0;
        PickCollector collector(maxBytes, [&](const QList<PickedFile> &files) {
            ++calls;
            got = files;
        });
        const QList<Offered> offered{
            file(u"Zażółć gęślą jaźń.md"_s, "# Zażółć gęślą jaźń\r\n\r\n- pierwszy\r\n"_ba),
            file(u"pusty.md"_s, QByteArray()),
            file(u"duży.md"_s, QByteArray(100, 'x')),
            file(u"notatka.txt"_s, "ćma\n"_ba),
        };
        drive(collector, offered);
        QCOMPARE(calls, 1);
        QVERIFY(collector.finished());
        QCOMPARE(got.size(), offered.size());
        for (qsizetype i = 0; i < offered.size(); ++i) {
            QCOMPARE(got.at(i).name, offered.at(i).name);
            QCOMPARE(got.at(i).size, qint64(offered.at(i).size));
            const bool tooLarge = i == tooLargeIndex;
            QCOMPARE(got.at(i).tooLarge, tooLarge);
            QCOMPARE(got.at(i).data, tooLarge ? QByteArray() : offered.at(i).bytes);
        }
    }

    void emptyFileGetsABuffer()
    {
        // nullptr would make Qt skip the file: an empty file must get a real (empty) buffer.
        PickCollector collector(-1, {});
        collector.closed(1);
        QVERIFY(collector.accept(0, u"pusty.md"_s) != nullptr);
        QVERIFY(!collector.finished());
        collector.ready();
        QVERIFY(collector.finished());
    }

    void allTooLarge()
    {
        QList<PickedFile> got;
        int calls = 0;
        PickCollector collector(10, [&](const QList<PickedFile> &files) {
            ++calls;
            got = files;
        });
        Offered huge{ u"film.mp4"_s, QByteArray(), 5'000'000'000ULL };
        drive(collector, { file(u"a.md"_s, QByteArray(11, 'a')), huge });
        QCOMPARE(calls, 1);
        QCOMPARE(got.size(), 2);
        QVERIFY(got.at(0).tooLarge && got.at(1).tooLarge);
        QCOMPARE(got.at(1).size, qint64(5'000'000'000LL));
        QVERIFY(got.at(1).data.isEmpty());
    }

    void cancelled()
    {
        int calls = 0;
        QList<PickedFile> got{ PickedFile{} };
        PickCollector collector(-1, [&](const QList<PickedFile> &files) {
            ++calls;
            got = files;
        });
        collector.closed(0);
        QCOMPARE(calls, 1);
        QVERIFY(got.isEmpty());
        // Nothing after the end.
        QCOMPARE(collector.accept(3, u"a.md"_s), nullptr);
        collector.ready();
        collector.closed(2);
        QCOMPARE(calls, 1);
    }

    void malformedCount()
    {
        int calls = 0;
        PickCollector collector(-1, [&](const QList<PickedFile> &) { ++calls; });
        collector.closed(-3); // nonsense from the browser: nothing chosen
        QCOMPARE(calls, 1);
    }

    void waitsForEveryFile()
    {
        int calls = 0;
        PickCollector collector(-1, [&](const QList<PickedFile> &) { ++calls; });
        collector.closed(2);
        char *first = collector.accept(3, u"ą.md"_s);
        QVERIFY(first);
        std::memcpy(first, "abc", 3);
        collector.ready();
        QCOMPARE(calls, 0);
        // The first file's buffer stays where it was while the second one is added.
        char *second = collector.accept(2, u"ę.md"_s);
        QVERIFY(second);
        QCOMPARE(QByteArray(first, 3), "abc"_ba);
        std::memcpy(second, "de", 2);
        collector.ready();
        QCOMPARE(calls, 1);
    }
};

QTEST_GUILESS_MAIN(TestPickedFiles)
#include "tst_pickedfiles.moc"
