// The Logger (log/logger.h): the chained message handler (the previous handler gets the
// redacted text), the ring, collapsing repeats, rate limits, concurrent writers, verbosity
// rules (QT_LOGGING_RULES still wins), coalesced notifications, export, clear, the rotating
// file with session lines, an unwritable folder, earlier sessions, and an end-to-end check that
// a secret never reaches the ring, the files, the export or stderr.
#include "filesystemtraits.h"
#include "log/categories.h"
#include "log/logger.h"
#include "log/redact.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUrl>

#include <atomic>
#include <memory>
#include <vector>

using namespace Qt::StringLiterals;
using namespace brulion::logging;

Q_LOGGING_CATEGORY(lcTestQuiet, "brulion.testquiet", QtWarningMsg)

namespace {

std::atomic<qint64> g_now{0};
QMutex g_capturedMutex;
QStringList g_captured;

void captureHandler(QtMsgType, const QMessageLogContext &, const QString &message)
{
    QMutexLocker locker(&g_capturedMutex);
    g_captured.append(message);
}

QString capturedText()
{
    QMutexLocker locker(&g_capturedMutex);
    return g_captured.join(u'\n');
}

LoggerOptions memoryOptions()
{
    LoggerOptions o;
    o.installHandler = false;
    o.clock = [] { return g_now.load(); };
    o.sessionInfo = u"tst"_s;
    return o;
}

QString readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QStringList messages(const QList<Entry> &entries)
{
    QStringList out;
    for (const Entry &e : entries)
        out.append(e.message);
    return out;
}

} // namespace

class TestLogLogger : public QObject {
    Q_OBJECT

public:
    static void initMain()
    {
        // Developers' rules beat the verbosity switch (Qt's rule order): checked in verbosity().
        qputenv("QT_LOGGING_RULES", "brulion.index.debug=true");
    }

private Q_SLOTS:
    void init();
    void cleanup();

    void handlerChain();
    void ring();
    void collapse();
    void rateLimit();
    void concurrentWriters();
    void verbosity();
    void appendedCoalesced();
    void exportCap();
    void clearLog();
    void fileRotation();
    void sessionLines();
    void unclosedSession();
    void unwritableFolder();
    void earlierSessions();
    void secretNeverLeaks();
};

void TestLogLogger::init()
{
    g_now = QDateTime::fromString(u"2026-10-18 14:00:00"_s, u"yyyy-MM-dd HH:mm:ss"_s).toMSecsSinceEpoch();
    QMutexLocker locker(&g_capturedMutex);
    g_captured.clear();
}

void TestLogLogger::cleanup()
{
    Logger::uninstall();
    clearSecrets();
    QLoggingCategory::setFilterRules(QString());
}

void TestLogLogger::handlerChain()
{
    const QtMessageHandler qtest = qInstallMessageHandler(captureHandler);
    LoggerOptions o = memoryOptions();
    o.installHandler = true;
    Logger *logger = Logger::install(o);
    QVERIFY(logger);
    QCOMPARE(Logger::instance(), logger);
    QCOMPARE(Logger::install(o), logger); // once
    qCWarning(lcSync).noquote() << u"PUT https://jan:tajne@chmura.example.pl/x.md: Authorization: Basic amFuOnRham5l"_s;
    qInfo("zażółć gęślą jaźń");
    const QList<Entry> entries = logger->entries();
    QCOMPARE(entries.last().message, u"zażółć gęślą jaźń"_s);
    QCOMPARE(entries.last().category, u"default"_s);
    const Entry &warning = entries.at(entries.size() - 2);
    QCOMPARE(warning.level, Level::Warning);
    QCOMPARE(warning.category, u"brulion.sync"_s);
    QCOMPARE(warning.message, u"PUT https://***@chmura.example.pl/x.md: Authorization: ***"_s);
    QVERIFY(capturedText().contains(u"PUT https://***@chmura.example.pl/x.md: Authorization: ***"_s));
    QVERIFY(!capturedText().contains(u"tajne"_s));
    QCOMPARE(logger->problemCount(), 1);
    Logger::uninstall();
    QCOMPARE(Logger::instance(), nullptr);
    // The capture handler is back in place.
    qInfo("after");
    QVERIFY(capturedText().endsWith(u"after"_s));
    qInstallMessageHandler(qtest);
}

void TestLogLogger::ring()
{
    LoggerOptions o = memoryOptions();
    o.capacity = 16;
    Logger *logger = Logger::install(o);
    QCOMPARE(logger->entries().size(), 1); // session start
    QVERIFY(logger->entries().first().message.startsWith(u"=== session start: tst, pid "_s));
    for (int i = 1; i <= 40; ++i) {
        g_now += 1000;
        logger->append(Level::Warning, u"brulion.sync", u"wpis %1"_s.arg(i));
    }
    const QList<Entry> entries = logger->entries();
    QCOMPARE(entries.size(), 16);
    QCOMPARE(entries.first().message, u"wpis 25"_s);
    QCOMPARE(entries.last().message, u"wpis 40"_s);
    QCOMPARE(logger->lastSeq(), quint64(41));
    for (qsizetype i = 1; i < entries.size(); ++i)
        QCOMPARE(entries.at(i).seq, entries.at(i - 1).seq + 1);
    const QList<Entry> after = logger->entries(38);
    QCOMPARE(messages(after), (QStringList{u"wpis 38"_s, u"wpis 39"_s, u"wpis 40"_s}));
    QVERIFY(logger->entries(41).isEmpty());
}

void TestLogLogger::collapse()
{
    QTemporaryDir dir;
    LoggerOptions o = memoryOptions();
    o.directory = dir.path();
    o.repeatWindowMs = 10'000;
    Logger *logger = Logger::install(o);
    for (int i = 0; i < 5; ++i) {
        g_now += 1000;
        logger->append(Level::Warning, u"brulion.sync", u"Brak połączenia z serwerem");
    }
    QList<Entry> entries = logger->entries();
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.last().repeat, 5);
    QCOMPARE(logger->problemCount(), 1);
    // Another category or level is a new entry.
    logger->append(Level::Info, u"brulion.sync", u"Brak połączenia z serwerem");
    QCOMPARE(logger->entries().size(), 3);
    // After the window the same text starts a new entry.
    g_now += 20'000;
    logger->append(Level::Info, u"brulion.sync", u"Brak połączenia z serwerem");
    entries = logger->entries();
    QCOMPARE(entries.size(), 4);
    QCOMPARE(entries.last().repeat, 1);
    QVERIFY(logger->flush());
    const QString text = readFile(dir.filePath(Logger::fileName()));
    QCOMPARE(text.count(u"Brak połączenia z serwerem"_s), 3);
    QVERIFY2(text.contains(u"\tW\tbrulion.sync\t(previous message repeated 4 more times)"_s), qPrintable(text));
}

void TestLogLogger::rateLimit()
{
    LoggerOptions o = memoryOptions();
    o.burst = 10;
    o.perSecond = 2;
    Logger *logger = Logger::install(o);
    for (int i = 0; i < 30; ++i)
        logger->append(Level::Debug, u"brulion.net", u"żądanie %1"_s.arg(i));
    for (int i = 0; i < 30; ++i)
        logger->append(Level::Warning, u"brulion.net", u"błąd %1"_s.arg(i)); // never dropped
    QList<Entry> entries = logger->entries();
    int debug = 0;
    int warnings = 0;
    for (const Entry &e : entries) {
        debug += e.level == Level::Debug ? 1 : 0;
        warnings += e.level == Level::Warning ? 1 : 0;
    }
    QCOMPARE(debug, 10);
    QCOMPARE(warnings, 30);
    // Another category has its own bucket.
    logger->append(Level::Debug, u"brulion.sync", u"inna kategoria");
    QCOMPARE(logger->entries().last().message, u"inna kategoria"_s);
    // Tokens come back with time; the next admitted line is preceded by the note.
    g_now += 5000;
    logger->append(Level::Debug, u"brulion.net", u"znowu");
    entries = logger->entries();
    QCOMPARE(entries.last().message, u"znowu"_s);
    QCOMPARE(entries.at(entries.size() - 2).message, u"(dropped 20 lines of brulion.net: too many at once)"_s);
}

void TestLogLogger::concurrentWriters()
{
    LoggerOptions o = memoryOptions();
    o.capacity = 10'000;
    o.clock = {};
    Logger *logger = Logger::install(o);
    std::vector<std::unique_ptr<QThread>> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back(QThread::create([logger, t] {
            for (int i = 0; i < 1000; ++i)
                logger->append(Level::Warning, u"brulion.sync", u"wątek %1 wpis %2"_s.arg(t).arg(i));
        }));
        threads.back()->start();
    }
    for (auto &thread : threads)
        QVERIFY(thread->wait(60'000 * BRULION_TEST_SLOWDOWN));
    const QList<Entry> entries = logger->entries();
    QCOMPARE(entries.size(), 8001);
    for (qsizetype i = 1; i < entries.size(); ++i)
        QVERIFY(entries.at(i).seq > entries.at(i - 1).seq);
    QCOMPARE(logger->problemCount(), 8000);
}

void TestLogLogger::verbosity()
{
    Logger *logger = Logger::install(memoryOptions());
    QSignalSpy changed(logger, &Logger::verbosityChanged);
    logger->setVerbosity(Verbosity::Normal);
    QVERIFY(!lcSync().isDebugEnabled());
    QVERIFY(!lcNet().isDebugEnabled());
    QVERIFY(lcSync().isInfoEnabled());
    QVERIFY(lcIndex().isDebugEnabled()); // QT_LOGGING_RULES (initMain) wins
    QCOMPARE(changed.count(), 0);
    logger->setVerbosity(Verbosity::Detailed);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(logger->verbosity(), Verbosity::Detailed);
    QVERIFY(lcSync().isDebugEnabled());
    QVERIFY(lcNet().isDebugEnabled());
    QVERIFY(!lcTestQuiet().isDebugEnabled()); // categories with a higher threshold stay quiet
    QVERIFY(!lcTestQuiet().isInfoEnabled());
    logger->setVerbosity(Verbosity::Normal);
    QVERIFY(!lcNet().isDebugEnabled());
    QCOMPARE(verbosityFromName(u"detailed"), Verbosity::Detailed);
    QCOMPARE(verbosityFromName(verbosityName(Verbosity::Normal)), Verbosity::Normal);
    QCOMPARE(verbosityFromName(u"loud"), std::nullopt);
}

void TestLogLogger::appendedCoalesced()
{
    LoggerOptions o = memoryOptions();
    o.notifyIntervalMs = 200;
    Logger *logger = Logger::install(o);
    QSignalSpy spy(logger, &Logger::appended);
    for (int i = 0; i < 100; ++i)
        logger->append(Level::Warning, u"brulion.app", u"wpis %1"_s.arg(i));
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 2000 * BRULION_TEST_SLOWDOWN);
    QTest::qWait(300);
    QVERIFY2(spy.count() <= 2, qPrintable(QString::number(spy.count())));
    QCOMPARE(spy.last().first().toULongLong(), logger->lastSeq());
    // Nothing more without new entries (no timer while idle).
    const auto before = spy.count();
    QTest::qWait(500);
    QCOMPARE(spy.count(), before);
}

void TestLogLogger::exportCap()
{
    Logger *logger = Logger::install(memoryOptions());
    for (int i = 0; i < 200; ++i) {
        g_now += 10;
        logger->append(Level::Warning, u"brulion.sync", u"wiersz %1 zażółć\ndruga linia"_s.arg(i));
    }
    const QStringList header{u"Brulion — rejestr zdarzeń"_s, u"Nie zawiera haseł."_s};
    const QString full = logger->exportText(header);
    QVERIFY(full.startsWith(u"Brulion — rejestr zdarzeń\nNie zawiera haseł.\n----\n"_s));
    QVERIFY(full.contains(u"=== session start"_s));
    QVERIFY(!full.contains(u"(older entries left out)"_s));
    const QString capped = logger->exportText(header, 2000);
    QVERIFY(capped.toUtf8().size() <= 2000 + 40);
    QVERIFY(capped.contains(u"(older entries left out)\n2026-"_s));
    QVERIFY(capped.trimmed().endsWith(u"wiersz 199 zażółć\n\tdruga linia"_s));
    QVERIFY(!capped.contains(u"wiersz 0 "_s));
}

void TestLogLogger::clearLog()
{
    QTemporaryDir dir;
    LoggerOptions o = memoryOptions();
    o.directory = dir.path();
    Logger *logger = Logger::install(o);
    logger->append(Level::Error, u"brulion.sync", u"błąd");
    QSignalSpy cleared(logger, &Logger::cleared);
    logger->clear();
    QCOMPARE(cleared.count(), 1);
    QCOMPARE(messages(logger->entries()), QStringList{u"Log cleared"_s});
    QCOMPARE(logger->problemCount(), 0);
    logger->flush();
    const QString text = readFile(dir.filePath(Logger::fileName()));
    QVERIFY(!text.contains(u"błąd"_s));
    QVERIFY(text.contains(u"Log cleared"_s));
}

void TestLogLogger::fileRotation()
{
    QTemporaryDir dir;
    LoggerOptions o = memoryOptions();
    o.directory = dir.path() + u"/logs"_s;
    o.maxFileBytes = 4096;
    Logger *logger = Logger::install(o);
    QCOMPARE(logger->directory(), o.directory);
    for (int i = 0; i < 300; ++i) {
        g_now += 1000;
        logger->append(Level::Warning, u"brulion.sync", u"linia %1 — zażółć gęślą jaźń, trochę dłuższy tekst"_s.arg(i));
        logger->flush();
    }
    const QStringList files = logger->files();
    QCOMPARE(files.size(), 2);
    QVERIFY(files.first().endsWith(Logger::rotatedFileName()));
    for (const QString &f : files)
        QVERIFY2(QFileInfo(f).size() < 4096 + 512, qPrintable(f));
    if (brulion::test::hasUnixPermissions()) {
        QCOMPARE(QFileInfo(o.directory).permissions() & (QFileDevice::ReadOther | QFileDevice::ReadGroup),
                 QFileDevice::Permissions());
    }
    // The newest line is in brulion.log, readable as UTF-8.
    QVERIFY(readFile(files.last()).contains(u"linia 299 — zażółć gęślą jaźń"_s));
    QVERIFY(!readFile(files.first()).contains(u"linia 0 —"_s)); // rotated away twice
    logger->setMaxFileBytes(3 * 1024 * 1024);
    QCOMPARE(logger->maxFileBytes(), 3 * 1024 * 1024);
}

void TestLogLogger::sessionLines()
{
    QTemporaryDir dir;
    LoggerOptions o = memoryOptions();
    o.directory = dir.path();
    o.sessionInfo = u"Brulion 9.9 (test)"_s;
    Logger *logger = Logger::install(o);
    logger->append(Level::Info, u"brulion.app", u"praca");
    logger->endSession();
    logger->endSession(); // once
    Logger::uninstall();
    const QString text = readFile(dir.filePath(Logger::fileName()));
    QVERIFY(text.contains(u"\tI\tbrulion.app\t=== session start: Brulion 9.9 (test), pid "_s));
    QVERIFY(text.contains(u", UTC"_s));
    QCOMPARE(text.count(u"=== session end ==="_s), 1);
    // A clean previous session: no note at the next start.
    Logger *second = Logger::install(o);
    QVERIFY(second->hasEarlierEntries());
    QVERIFY(!messages(second->entries()).join(u'\n').contains(u"Previous session"_s));
}

void TestLogLogger::unclosedSession()
{
    QTemporaryDir dir;
    QFile file(dir.filePath(Logger::fileName()));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("2026-10-17 09:00:00.000\tI\tbrulion.app\t=== session end ===\n"
               "2026-10-17 10:00:00.000\tI\tbrulion.app\t=== session start: Brulion (old), pid 1, UTC+02:00 ===\n"
               "2026-10-17 10:05:00.250\tW\tbrulion.sync\t#7 no answer from the server for 20 s\n");
    file.close();
    LoggerOptions o = memoryOptions();
    o.directory = dir.path();
    Logger *logger = Logger::install(o);
    const QString all = messages(logger->entries()).join(u'\n');
    QVERIFY2(all.contains(u"Previous session (started 2026-10-17 10:00:00.000) ended without closing"_s),
             qPrintable(all));
    QVERIFY(all.contains(u"its last entry: 2026-10-17 10:05:00.250"_s));
}

void TestLogLogger::unwritableFolder()
{
    QTemporaryDir dir;
    // A file where the folder should be: the sink disables itself, the ring keeps working.
    QFile blocker(dir.filePath(u"logs"_s));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    LoggerOptions o = memoryOptions();
    o.directory = blocker.fileName();
    Logger *logger = Logger::install(o);
    QVERIFY(logger->directory().isEmpty());
    QVERIFY(logger->files().isEmpty());
    logger->append(Level::Warning, u"brulion.sync", u"nadal działa");
    QVERIFY(!logger->flush());
    QCOMPARE(logger->entries().last().message, u"nadal działa"_s);
    QVERIFY(logger->exportText({u"h"_s}).contains(u"nadal działa"_s));
}

void TestLogLogger::earlierSessions()
{
    QTemporaryDir dir;
    QFile rotated(dir.filePath(Logger::rotatedFileName()));
    QVERIFY(rotated.open(QIODevice::WriteOnly));
    rotated.write("2026-10-16 08:00:00.000\tI\tbrulion.app\tnajstarszy\n");
    rotated.close();
    QFile file(dir.filePath(Logger::fileName()));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("2026-10-17 08:00:00.000\tW\tbrulion.sync\twczorajszy błąd\n"
               "2026-10-17 08:00:01.000\tI\tbrulion.app\t=== session end ===\n");
    file.close();
    LoggerOptions o = memoryOptions();
    o.directory = dir.path();
    Logger *logger = Logger::install(o);
    logger->append(Level::Info, u"brulion.app", u"dzisiaj");
    const QList<Entry> earlier = logger->earlierEntries();
    QCOMPARE(messages(earlier), (QStringList{u"najstarszy"_s, u"wczorajszy błąd"_s, u"=== session end ==="_s}));
    QCOMPARE(earlier.at(1).level, Level::Warning);
    QCOMPARE(earlier.first().seq, quint64(0));
    QCOMPARE(logger->earlierEntries(1).size(), 1);
    QVERIFY(logger->hasEarlierEntries());
    logger->clear();
    QVERIFY(!logger->hasEarlierEntries());
    QVERIFY(logger->earlierEntries().isEmpty());
}

void TestLogLogger::secretNeverLeaks()
{
    const QString secret = u"Zażółć-Gęślą-Jaźń-42!"_s;
    const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(secret));
    const QString token = QString::fromLatin1((u"jan:"_s + secret).toUtf8().toBase64());
    QTemporaryDir dir;
    const QtMessageHandler qtest = qInstallMessageHandler(captureHandler);
    LoggerOptions o = memoryOptions();
    o.installHandler = true;
    o.directory = dir.path();
    Logger *logger = Logger::install(o);
    logger->setVerbosity(Verbosity::Detailed);
    addSecret(secret);
    addSecret(token);
    qCWarning(lcSync).noquote() << u"PUT https://jan:%1@chmura.example.pl/x.md failed"_s.arg(encoded);
    qCDebug(lcNet).noquote() << u"headers: Authorization: Basic "_s + token;
    qWarning() << "hasło" << secret;
    qCInfo(lcApp).noquote() << u"{\"password\":\"%1\"}"_s.arg(secret);
    logger->append(Level::Error, u"brulion.sync", u"direct " + secret);
    removeSecret(secret); // gone from the registry: the stored text stays redacted
    removeSecret(token);
    logger->flush();
    const QString ring = messages(logger->entries()).join(u'\n');
    QString files;
    for (const QString &f : logger->files())
        files += readFile(f);
    const QString exported = logger->exportText({u"header"_s});
    const QString stderrText = capturedText();
    qInstallMessageHandler(qtest);
    for (const QString &where : {ring, files, exported, stderrText}) {
        QVERIFY2(!where.contains(secret), qPrintable(where));
        QVERIFY2(!where.contains(encoded), qPrintable(where));
        QVERIFY2(!where.contains(token), qPrintable(where));
        QVERIFY2(!where.contains(u"Authorization: Basic"_s), qPrintable(where));
        QVERIFY2(where.contains(u"https://***@chmura.example.pl/x.md"_s), qPrintable(where));
    }
    QVERIFY(ring.contains(u"direct ***"_s));
}

QTEST_GUILESS_MAIN(TestLogLogger)
#include "tst_log_logger.moc"
