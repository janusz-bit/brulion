// The leftovers of interrupted own writes (`.brulion-tmp-…`, vaultutil::isLeftoverTempName()):
// removed when the vault opens — only regular files with our own names, never links or folders,
// only after a day without a change (the newest of the modification, metadata change and creation
// times), only in the folders the vault lists (not behind links it does not follow, not in ignored
// folders) and in History's folders — and never seen as notes meanwhile (listing, the watcher).
// A case-only rename's temporary name holds a note: it is never one of them.
#include "vaulttest.h"

#include "vault/history.h"
#include "vault/vaultutil_p.h"

using namespace vaulttest;

namespace {

const QString kTemp = u".brulion-tmp-0123456789abcdef"_s;
constexpr qint64 kHourMs = 60LL * 60 * 1000;

/// A file written now (its metadata and creation times are now, whatever its modification time).
void plant(const QString &abs, const QByteArray &bytes = "- przerwany zapis: zażółć gęślą jaźń\n")
{
    QVERIFY(QDir().mkpath(QFileInfo(abs).absolutePath()));
    QFile file(abs);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(file.errorString()));
    QCOMPARE(file.write(bytes), bytes.size());
}

/// The vault's clock `hours` after now (the planted files' times are now).
void clockIn(Fixture &f, qint64 hours)
{
    f.clock.now = QDateTime::currentDateTime().addMSecs(hours * kHourMs);
}

} // namespace

class TestVaultLeftovers : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void names_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("leftover");
        QTest::newRow("own write") << kTemp << true;
        QTest::newRow("own write, zeros") << u".brulion-tmp-0000000000000000"_s << true;
        QTest::newRow("Android journal append") << u".brulion-tmp-3f2504e0-4f89-41d3-9a0c-0305e82c3301"_s << true;
        QTest::newRow("backup case probe") << u".brulion-tmp-CaseProbe-aB3dE9"_s << true;
        // A case-only rename's name (base 36, at most 13 digits): a note or a folder waits there.
        QTest::newRow("case rename") << u".brulion-tmp-3w5e11264sgsg"_s << false;
        QTest::newRow("case rename, short") << u".brulion-tmp-z1"_s << false;
        QTest::newRow("15 digits") << u".brulion-tmp-0123456789abcde"_s << false;
        QTest::newRow("17 digits") << u".brulion-tmp-0123456789abcdef0"_s << false;
        QTest::newRow("upper case") << u".brulion-tmp-0123456789ABCDEF"_s << false;
        QTest::newRow("extension") << u".brulion-tmp-0123456789abcdef.md"_s << false;
        QTest::newRow("prefix inside") << u"x.brulion-tmp-0123456789abcdef"_s << false;
        QTest::newRow("test probe") << u".brulion-tmp-probe"_s << false;
        QTest::newRow("UUID, upper case") << u".brulion-tmp-3F2504E0-4F89-41D3-9A0C-0305E82C3301"_s << false;
        QTest::newRow("case probe, 5") << u".brulion-tmp-CaseProbe-aB3dE"_s << false;
        QTest::newRow("QSaveFile's") << u"Zakupy.md.aB3dE9"_s << false;
        QTest::newRow("Syncthing's") << u".syncthing.Zakupy.md.tmp"_s << false;
        QTest::newRow("a note") << u"Żółw.md"_s << false;
        QTest::newRow("Polish digits") << u".brulion-tmp-0123456789abcdeż"_s << false;
    }
    void names()
    {
        QFETCH(QString, name);
        QFETCH(bool, leftover);
        QCOMPARE(vaultutil::isLeftoverTempName(name), leftover);
    }

    // Each kind of leftover by name, old enough: removed at open; the others stay.
    void removedAtOpen_data() { names_data(); }
    void removedAtOpen()
    {
        QFETCH(QString, name);
        QFETCH(bool, leftover);
        Fixture f;
        f.write(u"Notatka.md"_s, "- treść\n");
        plant(f.root() + u'/' + name);
        clockIn(f, 25);
        QVERIFY(f.open());
        QCOMPARE(QFileInfo::exists(f.root() + u'/' + name), !leftover);
        QCOMPARE(f.read(u"Notatka.md"_s), QByteArray("- treść\n"));
        QVERIFY(f.changes().isEmpty());
    }

    // Where: every folder the vault lists (the config folder too); not the ignored folders it never
    // lists, not behind a link to a folder outside that it does not follow.
    void where()
    {
        Fixture f;
        f.write(u"Projekty/Dom/Plan.md"_s, "- plan\n");
        const QStringList removed{kTemp, u"Projekty/"_s + kTemp, u"Projekty/Dom/"_s + kTemp, u".brulion/"_s + kTemp};
        const QStringList kept{u".trash/"_s + kTemp, u".git/"_s + kTemp, u"node_modules/x/"_s + kTemp};
        for (const QString &rel : removed + kept)
            plant(f.root() + u'/' + rel);
#ifdef Q_OS_UNIX
        // A linked folder outside the vault (not followed): what lies there is not the vault's.
        QTemporaryDir outside;
        plant(outside.path() + u'/' + kTemp);
        QVERIFY(QFile::link(outside.path(), f.root() + u"/Zewnętrzny"_s));
#endif
        clockIn(f, 48);
        QVERIFY(f.open());
        for (const QString &rel : removed)
            QVERIFY2(!QFileInfo::exists(f.root() + u'/' + rel), qPrintable(rel));
        for (const QString &rel : kept)
            QVERIFY2(QFileInfo::exists(f.root() + u'/' + rel), qPrintable(rel));
#ifdef Q_OS_UNIX
        QVERIFY(QFileInfo::exists(outside.path() + u'/' + kTemp));
#endif
        QCOMPARE(f.files(), QStringList{u"Projekty/Dom/Plan.md"_s});
    }

    // How old: a day after the newest of its times. A synced or copied-in file keeps an old
    // modification time but its metadata (and creation) time is when it arrived: kept.
    void age_data()
    {
        QTest::addColumn<qint64>("hours");    // the vault's clock after the file's times
        QTest::addColumn<qint64>("oldMtime"); // hours the modification time is set back (0: as written)
        QTest::addColumn<bool>("removed");
        QTest::newRow("fresh") << qint64(0) << qint64(0) << false;
        QTest::newRow("an hour") << qint64(1) << qint64(0) << false;
        QTest::newRow("23 hours") << qint64(23) << qint64(0) << false;
        QTest::newRow("25 hours") << qint64(25) << qint64(0) << true;
        QTest::newRow("old modification time, arrived now") << qint64(1) << qint64(72) << false;
        QTest::newRow("from the future (another clock)") << qint64(-48) << qint64(0) << false;
    }
    void age()
    {
        QFETCH(qint64, hours);
        QFETCH(qint64, oldMtime);
        QFETCH(bool, removed);
        Fixture f;
        const QString abs = f.root() + u'/' + kTemp;
        plant(abs);
        if (oldMtime > 0) {
            QFile file(abs);
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(QDateTime::currentDateTime().addMSecs(-oldMtime * kHourMs),
                                     QFileDevice::FileModificationTime));
        }
        clockIn(f, hours);
        QVERIFY(f.open());
        QCOMPARE(!QFileInfo::exists(abs), removed);
    }

    // Never a link (whatever it points to) or a folder with a leftover's name; never a case-only
    // rename's note.
    void neverLinksFoldersOrNotes()
    {
        Fixture f;
        f.write(u"Ważne.md"_s, "- nie ruszać\n");
        QVERIFY(QDir().mkpath(f.root() + u"/Archiwum/"_s + kTemp));
        plant(f.root() + u"/Archiwum/"_s + kTemp + u"/notatka.md"_s);
        plant(f.root() + u"/.brulion-tmp-3w5e11264sgsg"_s, "- notatka w połowie zmiany nazwy\n");
#ifdef Q_OS_UNIX
        QVERIFY(QFile::link(f.root() + u"/Ważne.md"_s, f.root() + u'/' + kTemp));
        QTemporaryDir outside;
        plant(outside.path() + u"/cel.md"_s, "- poza notatnikiem\n");
        QVERIFY(QFile::link(outside.path() + u"/cel.md"_s, f.root() + u"/.brulion-tmp-fedcba9876543210"_s));
#endif
        clockIn(f, 100);
        QVERIFY(f.open());
        QVERIFY(QFileInfo(f.root() + u"/Archiwum/"_s + kTemp).isDir());
        QVERIFY(QFileInfo::exists(f.root() + u"/Archiwum/"_s + kTemp + u"/notatka.md"_s));
        QCOMPARE(f.read(u".brulion-tmp-3w5e11264sgsg"_s), QByteArray("- notatka w połowie zmiany nazwy\n"));
        QCOMPARE(f.read(u"Ważne.md"_s), QByteArray("- nie ruszać\n"));
#ifdef Q_OS_UNIX
        QVERIFY(QFileInfo(f.root() + u'/' + kTemp).isSymLink());
        QVERIFY(QFileInfo(f.root() + u"/.brulion-tmp-fedcba9876543210"_s).isSymLink());
        QCOMPARE(readBehind(outside.path(), u"cel.md"_s), QByteArray("- poza notatnikiem\n"));
#endif
        // (The direct call refuses them too: a fresh look right before the removal.)
        const qint64 later = f.clock.now.toMSecsSinceEpoch();
        QVERIFY(!vaultutil::removeLeftoverTemp(f.root() + u"/Archiwum/"_s + kTemp, later));
        QVERIFY(!vaultutil::removeLeftoverTemp(f.root() + u"/.brulion-tmp-3w5e11264sgsg"_s, later));
        QVERIFY(!vaultutil::removeLeftoverTemp(f.root() + u"/missing/"_s + kTemp, later));
#ifdef Q_OS_UNIX
        QVERIFY(!vaultutil::removeLeftoverTemp(f.root() + u'/' + kTemp, later));
        QVERIFY(!vaultutil::removeLeftoverTemp(f.root() + u"/.brulion-tmp-fedcba9876543210"_s, later));
        QVERIFY(QFileInfo(f.root() + u'/' + kTemp).isSymLink());
#endif
    }

    // History's folders (device state): the leftovers of its index and snapshot writes.
    void historyLeftovers()
    {
        Fixture f;
        f.write(u"Notatka.md"_s, "- treść\n");
        const QString index = f.state() + u"/history/index/"_s;
        const QString objects = f.state() + u"/history/objects/"_s;
        plant(index + kTemp, "{\"path\":\"Notatka.md\"}");
        plant(objects + u".brulion-tmp-00000000deadbeef"_s);
        plant(objects + u"keep.txt"_s);
        clockIn(f, 30);
        QVERIFY(f.open());
        QVERIFY(!QFileInfo::exists(index + kTemp));
        QVERIFY(!QFileInfo::exists(objects + u".brulion-tmp-00000000deadbeef"_s));
        QVERIFY(QFileInfo::exists(objects + u"keep.txt"_s));
        // The History keeps working afterwards.
        QVERIFY(f.vault->writeText(u"Notatka.md"_s, u"- nowa treść\n"_s));
        QCOMPARE(f.historyContents(u"Notatka.md"_s), QStringList{u"- treść\n"_s});
        // A fresh one is left alone (another instance may be writing it).
        plant(index + kTemp);
        QCOMPARE(f.vault->history()->removeLeftovers(QDateTime::currentMSecsSinceEpoch()), 0);
        QVERIFY(QFileInfo::exists(index + kTemp));
    }

    // Meanwhile they are never notes: not listed, not reported by the watcher or a rescan, not
    // removed by one (only open() removes, and only old ones).
    void ignoredMeanwhile()
    {
        Fixture f;
        f.write(u"Dziennik/2026-11-18.md"_s, "- rano\n");
        QVERIFY(f.open());
        f.vault->setWatchDelay(20);
        f.vault->startWatching();
        QTest::qWait(30);
        plant(f.root() + u"/Dziennik/"_s + kTemp);
        plant(f.root() + u'/' + kTemp);
        QTest::qWait(150);
        f.vault->processPendingEvents();
        QVERIFY(f.changes().isEmpty());
        QVERIFY(f.vault->rescan().isEmpty());
        QCOMPARE(f.files(), QStringList{u"Dziennik/2026-11-18.md"_s});
        QVERIFY(!f.vault->exists(u"Dziennik/"_s + kTemp));
        QVERIFY(QFileInfo::exists(f.root() + u"/Dziennik/"_s + kTemp));
    }
};

QTEST_GUILESS_MAIN(TestVaultLeftovers)
#include "tst_vault_leftovers.moc"
