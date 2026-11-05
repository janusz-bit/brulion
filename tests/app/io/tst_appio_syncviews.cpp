// Tests for the sync center's helpers for compiled QML (src/app/io/syncviews.h): IoLists'
// counting, picking and the folder picker's rows (Polish names, nested folders, fuzzy ranking),
// SyncRunInfo without a known run.
#include "syncviews.h"
#include "workspacesync.h"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::ioviews::IoLists;
using brulion::ioviews::SyncRunInfo;

class TstAppioSyncViews : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void countWhereAndColumn()
    {
        const IoLists lists;
        const QVariantList rows{
                QVariantMap{{u"path"_s, u"Dziennik/2026-10-15.md"_s}, {u"action"_s, u"delete-local"_s}},
                QVariantMap{{u"path"_s, u"Zakupy.md"_s}, {u"action"_s, u"delete-remote"_s}},
                QVariantMap{{u"path"_s, u"Żółw.md"_s}, {u"action"_s, u"delete-local"_s}},
                QVariantMap{{u"action"_s, u"delete-remote"_s}}, // no path
                QVariant(u"not a map"_s),
        };
        QCOMPARE(lists.countWhere(rows, u"action"_s, u"delete-local"_s), 2);
        QCOMPARE(lists.countWhere(rows, u"action"_s, u"delete-remote"_s), 2);
        QCOMPARE(lists.countWhere(rows, u"action"_s, u"other"_s), 0);
        QCOMPARE(lists.countWhere({}, u"action"_s, u"delete-local"_s), 0);
        QCOMPARE(lists.column(rows, u"path"_s),
                 (QStringList{u"Dziennik/2026-10-15.md"_s, u"Zakupy.md"_s, u"Żółw.md"_s, QString(), QString()}));
        QCOMPARE(lists.column({}, u"path"_s), QStringList());
    }

    void folderItems()
    {
        const IoLists lists;
        const QStringList folders{u"Projekty"_s, u"Projekty/Źródła"_s, u"Dom/Ogród"_s, u"Archiwum"_s};
        // An empty query: every folder in order.
        const QVariantList all = lists.folderItems(QString(), folders, 100);
        QCOMPARE(all.size(), 4);
        const QVariantMap second = all.at(1).toMap();
        QCOMPARE(second.value(u"title"_s).toString(), u"Źródła"_s);
        QCOMPARE(second.value(u"subtitle"_s).toString(), u"Projekty"_s);
        QCOMPARE(second.value(u"icon"_s).toString(), u"folder"_s);
        QCOMPARE(second.value(u"path"_s).toString(), u"Projekty/Źródła"_s);
        const QVariantMap top = all.at(0).toMap();
        QCOMPARE(top.value(u"title"_s).toString(), u"Projekty"_s);
        QCOMPARE(top.value(u"subtitle"_s).toString(), QString());
        // Diacritics ignored, the limit kept.
        const QVariantList zrodla = lists.folderItems(u"zrodla"_s, folders, 100);
        QVERIFY(!zrodla.isEmpty());
        QCOMPARE(zrodla.first().toMap().value(u"path"_s).toString(), u"Projekty/Źródła"_s);
        QCOMPARE(lists.folderItems(QString(), folders, 2).size(), 2);
        QVERIFY(lists.folderItems(u"qqqqq"_s, folders, 100).isEmpty());
        QVERIFY(lists.folderItems(u"a"_s, {}, 100).isEmpty());
    }

    void runInfoWithoutRun()
    {
        brulion::shell::WorkspaceSync sync;
        SyncRunInfo info;
        QSignalSpy changed(&info, &SyncRunInfo::numberChanged);
        QVERIFY(!info.known());
        info.setSync(&sync);
        info.setNumber(42); // no such run in an empty history
        QCOMPARE(changed.size(), 1);
        QVERIFY(!info.known());
        QVERIFY(info.details().isEmpty());
        QVERIFY(info.stats().isEmpty());
        QVERIFY(info.counts().isEmpty());
        QVERIFY(info.phases().isEmpty());
        QVERIFY(info.errors().isEmpty());
        QVERIFY(info.conflicts().isEmpty());
        QVERIFY(info.slowest().isEmpty());
        info.setNumber(0);
        QVERIFY(!info.known());
    }
};

QTEST_GUILESS_MAIN(TstAppioSyncViews)
#include "tst_appio_syncviews.moc"
