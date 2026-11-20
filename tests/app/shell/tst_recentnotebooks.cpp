// RecentNotebooks (recentnotebooks.h): the start screen's recent notebooks — the rows from
// AppSettings, the folders as shown ("~/…"), when each was last opened in both languages, the
// folders checked off the GUI thread (available, missing, unreachable after the timeout and
// back once it answers), a vanished temporary folder dropped (at the start and when a check
// finds it) while another missing one stays marked, "Remove from list" taking out just its row,
// and malformed stored entries.
#include "appsettings.h"
#include "recentnotebooks.h"

#include <QAbstractItemModelTester>
#include <QDir>
#include <QSemaphore>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using brulion::shell::AppSettings;
using brulion::shell::RecentNotebooks;
using brulion::dates::UiLocale;

Q_DECLARE_METATYPE(UiLocale)

namespace {

QString roleText(const RecentNotebooks &m, int row, int role)
{
    return m.data(m.index(row), role).toString();
}

} // namespace

class TestRecentNotebooks : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;

    std::unique_ptr<AppSettings> settings()
    {
        return std::make_unique<AppSettings>(m_tmp.filePath(u"s%1.ini"_s.arg(++m_n)), m_tmp.path());
    }
    /// A notebook folder that exists (under the test's folder).
    QString folder(const QString &name)
    {
        const QString path = QDir::cleanPath(m_tmp.filePath(name));
        QDir().mkpath(path);
        return path;
    }
    /// A folder that is no temporary one and does not exist (the test's folder is temporary).
    static QString missingOutsideTemp(const QString &name)
    {
        return u"/brulion-test-missing-%1/%2"_s.arg(QCoreApplication::applicationPid()).arg(name);
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    void isUnder_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QStringList>("roots");
        QTest::addColumn<bool>("under");
        const QStringList tmp{u"/tmp"_s, u"/var/tmp"_s};
        QTest::newRow("inside") << u"/tmp/claude-1000/scratchpad/vault"_s << tmp << true;
        QTest::newRow("the root itself") << u"/tmp"_s << tmp << true;
        QTest::newRow("trailing slash") << u"/tmp/zeszyt/"_s << tmp << true;
        QTest::newRow("dot dot out") << u"/tmp/../home/ala/Notatki"_s << tmp << false;
        QTest::newRow("prefix only") << u"/tmpfiles/zeszyt"_s << tmp << false;
        QTest::newRow("var tmp") << u"/var/tmp/żółw"_s << tmp << true;
        QTest::newRow("home") << u"/home/ala/Notatki"_s << tmp << false;
        QTest::newRow("empty path") << QString() << tmp << false;
        QTest::newRow("no roots") << u"/tmp/x"_s << QStringList() << false;
        QTest::newRow("the whole disk is no root") << u"/home/ala"_s << QStringList{u"/"_s} << false;
        // Windows: its separators, and names without regard to case.
        if (QDir::separator() == u'\\')
            QTest::newRow("backslashes") << u"C:\\Users\\Ala\\AppData\\Local\\Temp\\zeszyt"_s
                                         << QStringList{u"c:/users/ala/AppData/Local/Temp"_s} << true;
    }
    void isUnder()
    {
        QFETCH(QString, path);
        QFETCH(QStringList, roots);
        QFETCH(bool, under);
        QCOMPARE(RecentNotebooks::isUnder(path, roots), under);
    }

    void defaultTemporaryRoots()
    {
        const QStringList roots = RecentNotebooks::defaultTemporaryRoots();
        QVERIFY(RecentNotebooks::isUnder(QDir::tempPath() + u"/zeszyt"_s, roots));
        QVERIFY(!RecentNotebooks::isUnder(QDir::homePath() + u"/Dokumenty/zeszyt"_s, roots)
                || RecentNotebooks::isUnder(QDir::homePath(), roots)); // (a home inside /tmp: a test's)
    }

    void displayPath_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("home");
        QTest::addColumn<QString>("shown");
        QTest::newRow("in home") << u"/home/ala/Dokumenty/Żółty zeszyt"_s << u"/home/ala"_s
                                 << u"~/Dokumenty/Żółty zeszyt"_s;
        QTest::newRow("home itself") << u"/home/ala"_s << u"/home/ala/"_s << u"~"_s;
        QTest::newRow("a neighbour") << u"/home/alan/Notatki"_s << u"/home/ala"_s << u"/home/alan/Notatki"_s;
        QTest::newRow("outside") << u"/media/ala/PENDRIVE/Zeszyt"_s << u"/home/ala"_s
                                 << u"/media/ala/PENDRIVE/Zeszyt"_s;
        QTest::newRow("no home") << u"/home/ala/Notatki"_s << QString() << u"/home/ala/Notatki"_s;
        QTest::newRow("home is the root") << u"/Notatki"_s << u"/"_s << u"/Notatki"_s;
        QTest::newRow("cleaned") << u"/home/ala//Notatki/./"_s << u"/home/ala"_s << u"~/Notatki"_s;
    }
    void displayPath()
    {
        QFETCH(QString, path);
        QFETCH(QString, home);
        QFETCH(QString, shown);
        QCOMPARE(RecentNotebooks::displayPath(path, home), QDir::toNativeSeparators(shown));
    }

    void lastOpenedText_data()
    {
        QTest::addColumn<QDateTime>("when");
        QTest::addColumn<UiLocale>("locale");
        QTest::addColumn<QString>("text");
        const QDate today(2026, 11, 20);
        QTest::newRow("today en") << QDateTime(today, QTime(8, 12)) << UiLocale::English << u"Today 08:12"_s;
        QTest::newRow("today pl") << QDateTime(today, QTime(8, 12)) << UiLocale::Polish << u"Dziś 08:12"_s;
        QTest::newRow("yesterday en") << QDateTime(today.addDays(-1), QTime(17, 40, 59)) << UiLocale::English
                                      << u"Yesterday 17:40"_s;
        QTest::newRow("yesterday pl") << QDateTime(today.addDays(-1), QTime(17, 40)) << UiLocale::Polish
                                      << u"Wczoraj 17:40"_s;
        QTest::newRow("this year en") << QDateTime(QDate(2026, 11, 13), QTime(9, 0)) << UiLocale::English
                                      << u"Fri, Nov 13"_s;
        QTest::newRow("this year pl") << QDateTime(QDate(2026, 11, 13), QTime(9, 0)) << UiLocale::Polish
                                      << u"Pt 13 lis"_s;
        QTest::newRow("last year en") << QDateTime(QDate(2025, 10, 17), QTime(9, 0)) << UiLocale::English
                                      << u"Oct 17, 2025"_s;
        QTest::newRow("last year pl") << QDateTime(QDate(2025, 10, 17), QTime(9, 0)) << UiLocale::Polish
                                      << u"17 paź 2025"_s;
        QTest::newRow("tomorrow (another clock)") << QDateTime(today.addDays(1), QTime(7, 0)) << UiLocale::Polish
                                                  << u"Sob 21 lis"_s;
        QTest::newRow("invalid") << QDateTime() << UiLocale::English << QString();
    }
    void lastOpenedText()
    {
        QFETCH(QDateTime, when);
        QFETCH(UiLocale, locale);
        QFETCH(QString, text);
        QCOMPARE(RecentNotebooks::lastOpenedText(when, QDateTime(QDate(2026, 11, 20), QTime(18, 30)), locale), text);
    }

    void probe()
    {
        QCOMPARE(RecentNotebooks::probe(folder(u"jest"_s)), u"available"_s);
        QCOMPARE(RecentNotebooks::probe(m_tmp.filePath(u"nie ma"_s)), u"missing"_s);
        QFile file(m_tmp.filePath(u"plik.md"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QCOMPARE(RecentNotebooks::probe(file.fileName()), u"missing"_s); // a file is no notebook
        QCOMPARE(RecentNotebooks::probe(QString()), u"missing"_s);
    }

    void rowsFollowTheSettings()
    {
        auto s = settings();
        const QString older = folder(u"Praca/Notatki zespołu"_s);
        const QString newer = folder(u"Dokumenty/Brulion"_s);
        s->addRecentVault(older);
        s->addRecentVault(newer);
        RecentNotebooks m(s.get());
        QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
        m.setClock([] { return QDateTime(QDate::currentDate(), QTime(23, 59)); });
        m.setLanguage(u"pl"_s);
        QCOMPARE(m.count(), 2);
        // Nothing is checked before the start screen asks.
        QCOMPARE(m.availability(newer), QString());
        QVERIFY(m.checking());
        QTest::qWait(20);
        QCOMPARE(m.availability(newer), QString());
        m.refresh();
        QCOMPARE(roleText(m, 0, RecentNotebooks::PathRole), newer);
        QCOMPARE(roleText(m, 0, RecentNotebooks::NameRole), u"Brulion"_s);
        QCOMPARE(roleText(m, 1, RecentNotebooks::NameRole), u"Notatki zespołu"_s);
        QVERIFY(roleText(m, 0, RecentNotebooks::LastOpenedTextRole).startsWith(u"Dziś "_s));
        QVERIFY(m.data(m.index(0), RecentNotebooks::TemporaryRole).toBool()); // (under the test's temp folder)
        const QHash<int, QByteArray> roles = m.roleNames();
        QCOMPARE(roles.value(RecentNotebooks::AvailabilityRole), "availability");
        QCOMPARE(roles.value(RecentNotebooks::LastOpenedTextRole), "lastOpenedText");
        QTRY_COMPARE(m.availability(newer), u"available"_s);
        QTRY_COMPARE(m.availability(older), u"available"_s);
        QVERIFY(!m.checking());
        QCOMPARE(m.unavailableCount(), 0);
        QCOMPARE(m.availability(u"/nie/ma/takiego"_s), QString());
        QCOMPARE(m.indexOf(newer + u"/"_s), 0);

        // Another open moves a notebook to the top; the language switches the texts.
        QSignalSpy count(&m, &RecentNotebooks::countChanged);
        s->addRecentVault(older);
        QCOMPARE(roleText(m, 0, RecentNotebooks::PathRole), older);
        QCOMPARE(count.size(), 0);
        QCOMPARE(m.availability(older), u"available"_s); // known: not checked again from scratch
        m.setLanguage(u"en"_s);
        QVERIFY(roleText(m, 0, RecentNotebooks::LastOpenedTextRole).startsWith(u"Today "_s));
        m.setHome(QDir::cleanPath(m_tmp.path()));
        QCOMPARE(roleText(m, 0, RecentNotebooks::DisplayPathRole), QDir::toNativeSeparators(u"~/Praca/Notatki zespołu"_s));
    }

    void missingFolderIsMarkedAndRemovedOnRequest()
    {
        auto s = settings();
        const QString kept = folder(u"istniejący"_s);
        const QString usb = missingOutsideTemp(u"Zeszyt na wyjazd"_s);
        const QString last = folder(u"ostatni"_s);
        s->addRecentVault(kept);
        s->addRecentVault(usb);
        s->addRecentVault(last);
        RecentNotebooks m(s.get());
        QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
        m.refresh();
        QVERIFY(!m.isTemporary(usb));
        QTRY_VERIFY(!m.checking());
        QCOMPARE(m.availability(usb), u"missing"_s);
        QCOMPARE(m.availability(kept), u"available"_s);
        QCOMPARE(m.unavailableCount(), 1);
        QCOMPARE(m.count(), 3); // marked, not dropped
        QCOMPARE(m.dropVanishedTemporary(), 0);
        QCOMPARE(m.count(), 3);

        // "Remove from list": only its row goes (the others keep their place and state).
        QSignalSpy removed(&m, &QAbstractItemModel::rowsRemoved);
        QSignalSpy reset(&m, &QAbstractItemModel::modelReset);
        m.remove(usb);
        QCOMPARE(removed.size(), 1);
        QCOMPARE(removed.first().at(1).toInt(), 1);
        QCOMPARE(reset.size(), 0);
        QCOMPARE(m.count(), 2);
        QCOMPARE(s->recentVaults().size(), 2);
        QCOMPARE(m.availability(kept), u"available"_s);
        QCOMPARE(m.unavailableCount(), 0);
        m.remove(u"/nie/ma/na/liście"_s); // nothing happens
        QCOMPARE(m.count(), 2);
    }

    void vanishedTemporaryFolderLeaves()
    {
        auto s = settings();
        const QString kept = folder(u"zostaje"_s);
        const QString gone = folder(u"znika"_s);
        s->addRecentVault(kept);
        s->addRecentVault(gone);
        {
            // At the start: the folder of a test run is gone.
            RecentNotebooks m(s.get());
            m.refresh();
            QTRY_VERIFY(!m.checking());
            QVERIFY(QDir(gone).removeRecursively());
            QVERIFY(m.isTemporary(gone));
            QCOMPARE(m.dropVanishedTemporary(), 1);
            QCOMPARE(m.count(), 1);
            QCOMPARE(roleText(m, 0, RecentNotebooks::PathRole), kept);
        }
        // Found by a check: it leaves the list instead of being marked.
        const QString later = folder(u"znika później"_s);
        s->addRecentVault(later);
        RecentNotebooks m(s.get());
        m.refresh();
        QTRY_VERIFY(!m.checking());
        QCOMPARE(m.count(), 2);
        QVERIFY(QDir(later).removeRecursively());
        m.refresh();
        QTRY_COMPARE(m.count(), 1);
        QCOMPARE(s->recentVaults().size(), 1);
        // markMissing() (an open that found it gone) does the same.
        const QString third = folder(u"trzeci"_s);
        s->addRecentVault(third);
        QVERIFY(QDir(third).removeRecursively());
        m.markMissing(third);
        QCOMPARE(m.indexOf(third), -1);
    }

    void folderThatDoesNotAnswerIsUnreachable()
    {
        auto s = settings();
        const QString share = missingOutsideTemp(u"udział sieciowy"_s);
        const QString local = folder(u"lokalny"_s);
        s->addRecentVault(local);
        s->addRecentVault(share);
        // A network share that hangs: its check blocks until released.
        auto gate = std::make_shared<QSemaphore>(0);
        RecentNotebooks m(s.get());
        m.setTimeout(50);
        m.setProbe([gate, share](const QString &path) {
            if (path == share) {
                gate->acquire();
                return u"available"_s;
            }
            return RecentNotebooks::probe(path);
        });
        m.refresh();
        QTRY_COMPARE(m.availability(share), u"unreachable"_s);
        // (A slow machine may count the local folder out too until its answer comes.)
        QTRY_COMPARE(m.availability(local), u"available"_s);
        QCOMPARE(m.unavailableCount(), 1);
        // Checked again while the first check still hangs: no second thread for it.
        m.refresh();
        QTest::qWait(100);
        QCOMPARE(m.availability(share), u"unreachable"_s);
        // It answers at last.
        gate->release(4);
        QTRY_COMPARE(m.availability(share), u"available"_s);
        QCOMPARE(m.unavailableCount(), 0);
    }

    void assumedAvailableIsNotChecked()
    {
        auto s = settings();
        const QString phone = missingOutsideTemp(u"storage/emulated/0/Brulion"_s);
        s->addRecentVault(phone);
        RecentNotebooks m(s.get());
        m.setAssumeAvailable([phone](const QString &path) { return path == phone; });
        m.refresh();
        QCOMPARE(m.availability(phone), u"available"_s); // at once, no thread
        QVERIFY(!m.checking());
        QTest::qWait(50);
        QCOMPARE(m.availability(phone), u"available"_s);
    }

    void modelOutlivesNothing()
    {
        // The model goes while its checks run (onboarding left at once): their answers go nowhere.
        auto s = settings();
        s->addRecentVault(folder(u"szybki"_s));
        auto gate = std::make_shared<QSemaphore>(0);
        {
            RecentNotebooks m(s.get());
            m.setProbe([gate](const QString &) {
                gate->acquire();
                return u"available"_s;
            });
            m.refresh();
        }
        gate->release(4);
        QTest::qWait(50);
    }

    void malformedStoredEntries()
    {
        const QString file = m_tmp.filePath(u"zepsute.ini"_s);
        {
            QSettings raw(file, QSettings::IniFormat);
            raw.setValue(u"vaults/recent"_s,
                         QVariantList{QVariantMap{{u"path"_s, u""_s}}, u"tylko tekst"_s, 42,
                                      QVariantMap{{u"path"_s, u"/home/ala/Notatki/"_s}, {u"lastOpened"_s, u"wczoraj"_s}},
                                      QVariantMap{{u"path"_s, u"/"_s}}});
        }
        AppSettings s(file, m_tmp.path());
        RecentNotebooks m(&s);
        QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(m.count(), 2);
        QCOMPARE(roleText(m, 0, RecentNotebooks::PathRole), u"/home/ala/Notatki"_s);
        QCOMPARE(roleText(m, 0, RecentNotebooks::NameRole), u"Notatki"_s);
        QCOMPARE(roleText(m, 0, RecentNotebooks::LastOpenedTextRole), QString()); // not a date
        QCOMPARE(roleText(m, 1, RecentNotebooks::NameRole), QDir::toNativeSeparators(u"/"_s)); // no folder name
        QCOMPARE(m.data(m.index(5), RecentNotebooks::PathRole), QVariant());
    }
};

QTEST_GUILESS_MAIN(TestRecentNotebooks)
#include "tst_recentnotebooks.moc"
