// The exported event log (src/app/diag/logexport.h, LogController): the header in Polish and
// English (what it contains, what it never contains, the recent syncs), the file name, the
// clipboard cap, and the whole path — a secret logged anywhere never reaches the export or the
// clipboard.
#include "logcontroller.h"
#include "logexport.h"
#include "logmodel.h"

#include "log/logger.h"
#include "log/redact.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>

using namespace Qt::StringLiterals;
using namespace brulion;
using diag::ExportInfo;
using logging::Level;
using logging::Logger;

class TestDiagLogExport : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void cleanup()
    {
        Logger::uninstall();
        logging::clearSecrets();
    }

    void header();
    void headerPolish();
    void fileName();
    void clipboardCap();
    void controllerWithoutLogger();
    void controllerExport();
};

namespace {

ExportInfo info()
{
    ExportInfo i;
    i.version = u"0.2.1"_s;
    i.device = u"Android 15, arm64"_s;
    i.qtVersion = u"6.11.2"_s;
    i.created = QDateTime(QDate(2026, 10, 18), QTime(14, 5, 12));
    i.recentSyncs = {u"#43 2026-10-18 14:03 · scheduled · No changes · 1.1 s"_s};
    return i;
}

} // namespace

void TestDiagLogExport::header()
{
    const QStringList lines = diag::exportHeader(info());
    QCOMPARE(lines.first(), u"Brulion — event log"_s);
    QCOMPARE(lines.at(1), u"Version 0.2.1 · Android 15, arm64 · Qt 6.11.2"_s);
    QVERIFY(lines.at(2).startsWith(u"Created 2026-10-18 14:05:12 (UTC"_s));
    QVERIFY(lines.at(2).endsWith(u"detail: normal"_s));
    const QString all = lines.join(u'\n');
    QVERIFY(all.contains(u"Does not contain: passwords, app passwords, tokens, cookies or note contents"_s));
    QVERIFY(all.contains(u"names and paths of the notes"_s));
    QVERIFY(all.endsWith(u"Recent syncs:\n  #43 2026-10-18 14:03 · scheduled · No changes · 1.1 s"_s));
    ExportInfo detailed = info();
    detailed.detailed = true;
    detailed.recentSyncs.clear();
    QVERIFY(diag::exportHeader(detailed).at(2).endsWith(u"detail: detailed"_s));
    QVERIFY(!diag::exportHeader(detailed).join(u'\n').contains(u"Recent syncs"_s));
}

void TestDiagLogExport::headerPolish()
{
    QTranslator translator;
    if (!translator.load(QStringLiteral(BRULION_TEST_PL_QM)))
        QSKIP("no Polish catalog in this build");
    QCoreApplication::installTranslator(&translator);
    const QString all = diag::exportHeader(info()).join(u'\n');
    QCoreApplication::removeTranslator(&translator);
    QVERIFY2(all.startsWith(u"Brulion — rejestr zdarzeń"_s), qPrintable(all));
    QVERIFY(all.contains(u"Nie zawiera"_s));
    QVERIFY(all.contains(u"haseł"_s));
}

void TestDiagLogExport::fileName()
{
    QCOMPARE(diag::suggestedFileName(QDateTime(QDate(2026, 10, 18), QTime(14, 3))),
             u"brulion-event-log-2026-10-18-1403.txt"_s);
}

void TestDiagLogExport::clipboardCap()
{
    QString text = u"Nagłówek\nDruga linia\n----\n"_s;
    for (int i = 0; i < 2000; ++i)
        text += u"2026-10-18 14:00:%1.000\tI\tbrulion.sync\twpis %2 zażółć\n\tciąg dalszy\n"_s
                        .arg(i % 60, 2, 10, QChar(u'0'))
                        .arg(i);
    QCOMPARE(diag::capForClipboard(u"krótki"_s), u"krótki"_s);
    const QString capped = diag::capForClipboard(text, 4096);
    QVERIFY(capped.toUtf8().size() <= 4096);
    QVERIFY(capped.startsWith(u"Nagłówek\nDruga linia\n----\n(older entries left out"_s));
    QVERIFY(capped.endsWith(u"wpis 1999 zażółć\n\tciąg dalszy\n"_s));
    // Never starts in the middle of an entry.
    const QString afterNote = capped.section(u'\n', 4);
    QVERIFY2(afterNote.startsWith(u"2026-10-18 "_s), qPrintable(afterNote.left(40)));
}

void TestDiagLogExport::controllerWithoutLogger()
{
    diag::LogController logs;
    QVERIFY(!logs.available());
    QVERIFY(!logs.copy());
    QCOMPARE(logs.exportText(), QString());
    QVERIFY(!logs.detailed());
    logs.setDetailed(true); // nothing to switch
    QVERIFY(!logs.detailed());
    QVERIFY(logs.actions().contains(u"copy"_s));
}

void TestDiagLogExport::controllerExport()
{
    QTemporaryDir dir;
    logging::LoggerOptions o;
    o.installHandler = false;
    o.directory = dir.path();
    o.sessionInfo = u"tst"_s;
    Logger *logger = Logger::install(o);
    const QString secret = u"Zażółć-Gęślą-Jaźń-42!"_s;
    logging::addSecret(secret);
    logger->append(Level::Warning, u"brulion.sync", u"PUT https://jan:%1@chmura.example.pl/x.md failed"_s.arg(secret));
    logger->append(Level::Info, u"brulion.net", u"Authorization: Basic amFuOnRham5l");
    diag::LogController logs;
    QVERIFY(logs.available());
    QCOMPARE(logs.problemCount(), 1);
    logs.setClock([] { return QDateTime(QDate(2026, 10, 18), QTime(14, 3)); });
    logs.setRecentSyncs([] { return QStringList{u"#7 ok"_s}; });
    QString persisted;
    logs.setPersistVerbosity([&persisted](const QString &v) { persisted = v; });
    QSignalSpy detailedChanged(&logs, &diag::LogController::detailedChanged);
    logs.setDetailed(true);
    QVERIFY(logs.detailed());
    QCOMPARE(persisted, u"detailed"_s);
    QCOMPARE(logger->maxFileBytes(), 3 * 1024 * 1024);
    QCOMPARE(detailedChanged.count(), 1);
    logs.setDetailed(false);
    QCOMPARE(persisted, u"normal"_s);
    QCOMPARE(logger->maxFileBytes(), 1024 * 1024);
    QCOMPARE(logs.suggestedFileName(), u"brulion-event-log-2026-10-18-1403.txt"_s);
    QVERIFY(logs.suggestedSaveFile().toLocalFile().endsWith(u"/brulion-event-log-2026-10-18-1403.txt"_s));

    const QString text = logs.exportText();
    QVERIFY(text.startsWith(u"Brulion — event log\n"_s));
    QVERIFY(text.contains(u"  #7 ok\n----\n"_s));
    QVERIFY(text.contains(u"https://***@chmura.example.pl/x.md"_s));
    QVERIFY(!text.contains(secret));
    QVERIFY(!text.contains(u"amFuOnRham5l"_s));

    // Saving to a file: the same redacted text.
    const QUrl target = QUrl::fromLocalFile(dir.filePath(u"eksport.txt"_s));
    QSignalSpy done(&logs, &diag::LogController::done);
    QVERIFY(logs.saveTo(target));
    QCOMPARE(done.count(), 1);
    QFile saved(target.toLocalFile());
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const QString savedText = QString::fromUtf8(saved.readAll());
    QVERIFY(!savedText.contains(secret));
    QVERIFY(savedText.contains(u"Does not contain"_s));
    QSignalSpy failed(&logs, &diag::LogController::failed);
    QFile blocker(dir.filePath(u"plik"_s)); // a file where a folder would have to be
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    QVERIFY(!logs.saveTo(QUrl::fromLocalFile(dir.filePath(u"plik/eksport.txt"_s))));
    QCOMPARE(failed.count(), 1);

    // The clipboard: everything, or what a model shows.
    QVERIFY(logs.copy());
    const QString clip = QGuiApplication::clipboard()->text();
    if (!clip.isEmpty()) { // offscreen clipboards may be unavailable
        QVERIFY(!clip.contains(secret));
        QVERIFY(clip.contains(u"----\n"_s));
    }
    diag::LogModel model;
    model.setLogger(logger);
    model.setGroup(u"net"_s);
    QVERIFY(logs.copy(&model));
    const QString modelClip = QGuiApplication::clipboard()->text();
    if (!modelClip.isEmpty()) {
        QVERIFY(modelClip.contains(u"Authorization: ***"_s));
        QVERIFY(!modelClip.contains(u"chmura.example.pl"_s));
    }

    QSignalSpy counts(&logs, &diag::LogController::countsChanged);
    logs.clear();
    QTRY_VERIFY(counts.count() >= 1);
    QCOMPARE(logs.problemCount(), 0);
    QVERIFY(!logs.exportText().contains(u"chmura"_s));
}

QTEST_MAIN(TestDiagLogExport)
#include "tst_diag_logexport.moc"
