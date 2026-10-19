// LogModel (src/app/diag/logmodel.h): the event log page's entries — group / level / search
// filters (diacritics ignored), a time window, earlier sessions, following the Logger (new rows,
// a repeat updating the newest row), clearing, lineAt / visibleText.
#include "logmodel.h"

#include "log/logger.h"

#include <QAbstractItemModelTester>
#include <QDateTime>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using brulion::diag::LogModel;
using logging::Level;
using logging::Logger;

namespace {

qint64 g_now = 0;

logging::LoggerOptions options()
{
    logging::LoggerOptions o;
    o.installHandler = false;
    o.clock = [] { return g_now; };
    o.notifyIntervalMs = 10;
    o.sessionInfo = u"tst"_s;
    return o;
}

QStringList messages(const LogModel &model)
{
    QStringList out;
    for (int i = 0; i < model.rowCount(); ++i)
        out.append(model.data(model.index(i), LogModel::MessageRole).toString());
    return out;
}

} // namespace

class TestDiagLogModel : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        g_now = QDateTime::fromString(u"2026-10-18 14:00:00"_s, u"yyyy-MM-dd HH:mm:ss"_s).toMSecsSinceEpoch();
    }
    void cleanup() { Logger::uninstall(); }

    void filters();
    void search_data();
    void search();
    void timeWindow();
    void follows();
    void repeatUpdatesRow();
    void earlierAndClear();
    void roles();
    void withoutLogger();
};

void TestDiagLogModel::filters()
{
    Logger *logger = Logger::install(options());
    logger->append(Level::Info, u"brulion.sync", u"#1 started (edit)");
    logger->append(Level::Debug, u"brulion.net", u"#1 PROPFIND Dziennik/ -> 207 in 312 ms");
    logger->append(Level::Warning, u"brulion.sync", u"#1 PUT Dziennik/x.md failed: locked (HTTP 423)");
    logger->append(Level::Error, u"brulion.vault", u"Nie można zapisać pliku");
    logger->append(Level::Warning, u"qt.network.ssl", u"SSL handshake failed");
    LogModel model;
    model.setLogger(logger);
    QAbstractItemModelTester tester(&model);
    QCOMPARE(model.rowCount(), 6); // + session start
    QVERIFY(!model.filtered());
    QSignalSpy filterChanged(&model, &LogModel::filterChanged);
    model.setGroup(u"sync"_s);
    QCOMPARE(messages(model),
             (QStringList{u"#1 started (edit)"_s, u"#1 PUT Dziennik/x.md failed: locked (HTTP 423)"_s}));
    QVERIFY(model.filtered());
    model.setGroup(u"net"_s);
    QCOMPARE(model.rowCount(), 2); // brulion.net + qt.network
    model.setGroup(QString());
    model.setMinLevel(u"warning"_s);
    QCOMPARE(model.rowCount(), 3);
    model.setMinLevel(u"error"_s);
    QCOMPARE(messages(model), QStringList{u"Nie można zapisać pliku"_s});
    QCOMPARE(model.minLevel(), u"error"_s);
    model.setMinLevel(u"nonsense"_s); // -> everything
    QCOMPARE(model.rowCount(), 6);
    QCOMPARE(model.totalCount(), 6);
    QCOMPARE(filterChanged.count(), 6);
}

void TestDiagLogModel::search_data()
{
    QTest::addColumn<QString>("query");
    QTest::addColumn<int>("rows");
    QTest::newRow("no diacritics") << u"zazolc"_s << 1;
    QTest::newRow("upper case") << u"ŻÓŁW"_s << 1;
    QTest::newRow("category") << u"brulion.vault"_s << 1;
    QTest::newRow("spaces trimmed") << u"  gęślą  "_s << 1;
    QTest::newRow("several") << u"dziennik"_s << 2;
    QTest::newRow("nothing") << u"nie ma takiego"_s << 0;
    QTest::newRow("empty") << QString() << 5;
}

void TestDiagLogModel::search()
{
    QFETCH(QString, query);
    QFETCH(int, rows);
    Logger *logger = Logger::install(options());
    logger->append(Level::Info, u"brulion.sync", u"Zażółć gęślą jaźń");
    logger->append(Level::Info, u"brulion.sync", u"Notatka: Żółw.md");
    logger->append(Level::Info, u"brulion.vault", u"Dziennik/2026-10-18.md");
    logger->append(Level::Info, u"brulion.sync", u"PUT DZIENNIK/a.md");
    LogModel model;
    model.setLogger(logger);
    model.setSearch(query);
    QCOMPARE(model.rowCount(), rows);
}

void TestDiagLogModel::timeWindow()
{
    Logger *logger = Logger::install(options());
    const qint64 start = g_now;
    for (int i = 0; i < 10; ++i) {
        g_now = start + i * 60'000;
        logger->append(Level::Warning, u"brulion.sync", u"minuta %1"_s.arg(i));
    }
    LogModel model;
    model.setLogger(logger);
    model.setFromMs(start + 3 * 60'000);
    model.setToMs(start + 5 * 60'000);
    QCOMPARE(messages(model), (QStringList{u"minuta 3"_s, u"minuta 4"_s, u"minuta 5"_s}));
    model.setFromMs(0);
    model.setToMs(0);
    QCOMPARE(model.rowCount(), 11);
}

void TestDiagLogModel::follows()
{
    Logger *logger = Logger::install(options());
    LogModel model;
    model.setLogger(logger);
    QAbstractItemModelTester tester(&model);
    model.setGroup(u"sync"_s);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    logger->append(Level::Info, u"brulion.sync", u"nowy");
    logger->append(Level::Info, u"brulion.app", u"inny obszar");
    QTRY_COMPARE(model.rowCount(), 1);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(model.totalCount(), 3);
    QCOMPARE(model.lineAt(0).section(u'\t', 1), u"I\tbrulion.sync\tnowy"_s);
    QCOMPARE(model.lineAt(5), QString());
}

void TestDiagLogModel::repeatUpdatesRow()
{
    Logger *logger = Logger::install(options());
    logger->append(Level::Warning, u"brulion.sync", u"Brak sieci");
    LogModel model;
    model.setLogger(logger);
    QAbstractItemModelTester tester(&model);
    QCOMPARE(model.rowCount(), 2);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    g_now += 1000;
    logger->append(Level::Warning, u"brulion.sync", u"Brak sieci");
    g_now += 1000;
    logger->append(Level::Warning, u"brulion.sync", u"Brak sieci");
    QTRY_COMPARE(model.data(model.index(1), LogModel::RepeatRole).toInt(), 3);
    QVERIFY(changed.count() >= 1);
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(model.visibleText().contains(u"(previous message repeated 2 more times)"_s));
}

void TestDiagLogModel::earlierAndClear()
{
    QTemporaryDir dir;
    QFile file(dir.filePath(Logger::fileName()));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("2026-10-17 08:00:00.000\tW\tbrulion.sync\twczoraj: timeout\n"
               "2026-10-17 08:00:01.000\tI\tbrulion.app\t=== session end ===\n");
    file.close();
    logging::LoggerOptions o = options();
    o.directory = dir.path();
    Logger *logger = Logger::install(o);
    LogModel model;
    model.setLogger(logger);
    QAbstractItemModelTester tester(&model);
    QCOMPARE(model.rowCount(), 1);
    model.setIncludeEarlier(true);
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.data(model.index(0), LogModel::EarlierRole).toBool());
    QCOMPARE(model.data(model.index(0), LogModel::MessageRole).toString(), u"wczoraj: timeout"_s);
    QVERIFY(!model.data(model.index(2), LogModel::EarlierRole).toBool());
    logger->clear();
    QTRY_COMPARE(messages(model), QStringList{u"Log cleared"_s});
    QVERIFY(!model.includeEarlier());
}

void TestDiagLogModel::roles()
{
    Logger *logger = Logger::install(options());
    logger->append(Level::Error, u"brulion.sync", u"tekst\ndruga linia");
    LogModel model;
    model.setLogger(logger);
    const QModelIndex i = model.index(1);
    QCOMPARE(model.data(i, LogModel::LevelRole).toString(), u"error"_s);
    QCOMPARE(model.data(i, LogModel::LetterRole).toString(), u"E"_s);
    QCOMPARE(model.data(i, LogModel::GroupRole).toString(), u"sync"_s);
    QCOMPARE(model.data(i, LogModel::CategoryRole).toString(), u"brulion.sync"_s);
    QCOMPARE(model.data(i, LogModel::TimeTextRole).toString(), u"14:00:00.000"_s);
    QCOMPARE(model.data(i, LogModel::DayRole).toString(), u"2026-10-18"_s);
    QCOMPARE(model.data(i, LogModel::SeqRole).toULongLong(), quint64(2));
    QCOMPARE(model.data(i, Qt::DisplayRole).toString(), u"tekst\ndruga linia"_s);
    QVERIFY(!model.data(model.index(7), LogModel::MessageRole).isValid());
    const QHash<int, QByteArray> names = model.roleNames();
    QCOMPARE(names.value(LogModel::TimeTextRole), QByteArray("timeText"));
    QCOMPARE(model.lineAt(1), u"2026-10-18 14:00:00.000\tE\tbrulion.sync\ttekst\n\tdruga linia"_s);
}

void TestDiagLogModel::withoutLogger()
{
    LogModel model; // no Logger installed
    QCOMPARE(model.rowCount(), 0);
    model.setSearch(u"x"_s);
    model.reload();
    QCOMPARE(model.visibleText(), QString());
}

QTEST_MAIN(TestDiagLogModel)
#include "tst_diag_logmodel.moc"
