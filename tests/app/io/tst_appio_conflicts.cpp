// Conflict resolution in the sync center: choosing a common ancestor from the version history
// (pickMergeBase: the true ancestor beats a too-new or too-old version, unrelated texts are
// refused), merge3 hunks for BMergeView (terminators, CRLF, a missing final newline — joining the
// "same" parts with "mine" gives merge3's text), and ConflictResolver over a real Workspace with
// Syncthing conflict copies: a clean three-way merge applied, a real collision, no history at all,
// an identical copy, a copy that is an older version of the note, keeping the copy, binary files.
#include "conflictresolver.h"
#include "workspace.h"
#include "workspacesync.h"

#include "sync/merge.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::ioviews::ConflictResolver;

namespace {

bool writeRaw(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(bytes);
    return true;
}

QString joinedWithMine(const QVariantList &hunks)
{
    QString out;
    for (const QVariant &v : hunks) {
        const QVariantMap h = v.toMap();
        out += h.value(u"kind"_s).toString() == u"same" ? h.value(u"text"_s).toString() : h.value(u"ours"_s).toString();
    }
    return out;
}

const QString Copy = u"Zakupy.sync-conflict-20261014-093000-ABCDEFG.md"_s;

} // namespace

class TestAppIoConflicts : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QDateTime m_now{ QDate(2026, 10, 14), QTime(9, 0) };

    std::unique_ptr<brulion::shell::Workspace> open(const QHash<QString, QByteArray> &files)
    {
        const QString root = m_tmp.filePath(u"vault-%1"_s.arg(++m_n));
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            writeRaw(root + u'/' + it.key(), it.value());
        auto ws = std::make_unique<brulion::shell::Workspace>();
        ws->setClock([this] { return m_now; });
        ws->sync()->setSyncthingDiscovery(false);
        QString error;
        if (!ws->open(root, root + u"-state"_s, &error))
            qWarning() << error;
        ws->finishIndexing();
        ws->vault()->setClock([this] { return m_now; });
        return ws;
    }

    /// The original edited on this device (`base` -> `ours`, so History keeps `base`) and a
    /// Syncthing conflict copy with `theirs`.
    std::unique_ptr<brulion::shell::Workspace> conflict(const QString &base, const QString &ours, const QString &theirs)
    {
        auto ws = open({ { u"Zakupy.md"_s, base.toUtf8() } });
        m_now = m_now.addSecs(600);
        ws->write(u"Zakupy.md"_s, ours);
        ws->vault()->writeText(Copy, theirs, brulion::ChangeSource::External);
        return ws;
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }
    void init() { m_now = QDateTime(QDate(2026, 10, 14), QTime(9, 0)); }

    // ---- Choosing the base ------------------------------------------------------------------------

    void pickBase()
    {
        const QString ancestor = u"- mleko\n- chleb\n- masło\n"_s;
        const QString ours = u"- mleko\n- chleb\n- masło\n- jajka\n- ser żółty\n"_s;
        const QString theirs = u"- mleko\n- chleb razowy\n- masło\n"_s;
        const QString tooNew = u"- mleko\n- chleb\n- masło\n- jajka\n"_s; // with one of my later additions
        const QString tooOld = u"- mleko\n"_s;
        const QString unrelated = u"# Zupełnie inna notatka\n"_s;
        QCOMPARE(brulion::ioviews::pickMergeBase({ tooNew, ancestor, tooOld }, ours, theirs), std::optional<qsizetype>(1));
        QCOMPARE(brulion::ioviews::pickMergeBase({ unrelated, ancestor }, ours, theirs), std::optional<qsizetype>(1));
        QCOMPARE(brulion::ioviews::pickMergeBase({ unrelated }, ours, theirs), std::nullopt);
        QCOMPARE(brulion::ioviews::pickMergeBase({}, ours, theirs), std::nullopt);
        QCOMPARE(brulion::ioviews::pickMergeBase({ u"\n\n"_s }, ours, theirs), std::nullopt);
        // Equally plausible: the newer one.
        QCOMPARE(brulion::ioviews::pickMergeBase({ ancestor, ancestor }, ours, theirs), std::optional<qsizetype>(0));
    }

    void hunksForView_data()
    {
        QTest::addColumn<QString>("base");
        QTest::addColumn<QString>("ours");
        QTest::addColumn<QString>("theirs");
        QTest::addColumn<int>("collisions");
        QTest::newRow("clean") << u"a\nb\nc\n"_s << u"a\nb\nc\nd\n"_s << u"a\nB\nc\n"_s << 0;
        QTest::newRow("collision") << u"a\nchleb\nc\n"_s << u"a\nchleb żytni\nc\n"_s << u"a\nchleb razowy\nc\n"_s << 1;
        QTest::newRow("crlf") << u"a\r\nb\r\nc\r\n"_s << u"a\r\nż\r\nc\r\n"_s << u"a\r\nź\r\nc\r\n"_s << 1;
        QTest::newRow("no final newline") << u"a\nb"_s << u"a\nb\nc"_s << u"x\nb"_s << 0;
        QTest::newRow("collision at the end") << u"a\nb"_s << u"a\nmoje"_s << u"a\nich\n"_s << 1;
        QTest::newRow("deleted on one side") << u"a\nb\nc\n"_s << u"a\nc\n"_s << u"a\nb\nc\nd\n"_s << 0;
    }

    void hunksForView()
    {
        QFETCH(QString, base);
        QFETCH(QString, ours);
        QFETCH(QString, theirs);
        QFETCH(int, collisions);
        const brulion::sync::MergeResult r = brulion::sync::merge3(base, ours, theirs);
        const QVariantList hunks = brulion::ioviews::mergeHunksForView(r, ours, theirs);
        int n = 0;
        for (const QVariant &v : hunks) {
            const QVariantMap h = v.toMap();
            if (h.value(u"kind"_s).toString() == u"conflict")
                ++n;
            else
                QVERIFY(h.value(u"lines"_s).toInt() > 0);
        }
        QCOMPARE(n, collisions);
        QCOMPARE(r.clean, collisions == 0);
        // merge3 keeps "mine" in collisions: the hunks rebuild its text byte for byte.
        QCOMPARE(joinedWithMine(hunks), r.text);
    }

    // ---- ConflictResolver ---------------------------------------------------------------------------

    void cleanThreeWayMerge()
    {
        auto ws = conflict(u"- mleko\n- chleb\n- masło\n"_s, u"- mleko\n- chleb\n- masło\n- jajka\n"_s,
                           u"- mleko\n- chleb razowy\n- masło\n"_s);
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(Copy);
        QVERIFY(r.loaded());
        QVERIFY(r.isText());
        QCOMPARE(r.conflict().value(u"original"_s).toString(), u"Zakupy.md"_s);
        QCOMPARE(r.conflict().value(u"source"_s).toString(), u"syncthing"_s);
        QVERIFY(r.conflict().value(u"originalExists"_s).toBool());
        QVERIFY(r.hasBase());
        QCOMPARE(r.base(), u"- mleko\n- chleb\n- masło\n"_s);
        QCOMPARE(r.baseWhen(), u"2026-10-14 09:10"_s);
        QVERIFY(r.clean());
        QCOMPARE(r.collisionCount(), 0);
        QCOMPARE(r.mergedText(), u"- mleko\n- chleb razowy\n- masło\n- jajka\n"_s);
        QVERIFY(!r.identical());
        QVERIFY(!r.oursSizeText().isEmpty());

        QSignalSpy resolved(&r, &ConflictResolver::resolved);
        QVERIFY(r.applyMerged(r.mergedText()));
        QCOMPARE(resolved.size(), 1);
        QVERIFY(!r.loaded());
        QCOMPARE(ws->readFile(u"Zakupy.md"_s), u"- mleko\n- chleb razowy\n- masło\n- jajka\n"_s);
        QVERIFY(!ws->exists(Copy));
        QCOMPARE(ws->sync()->conflictCount(), 0);
        // Mine is in the history, the copy in the trash.
        QVERIFY(!ws->history(u"Zakupy.md"_s).isEmpty());
        QVERIFY(ws->trashCount() >= 1);
    }

    void collision()
    {
        auto ws = conflict(u"- mleko\n- chleb\n- masło\n"_s, u"- mleko\n- chleb żytni\n- masło\n"_s,
                           u"- mleko\n- chleb razowy\n- masło\n- ser\n"_s);
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(Copy);
        QVERIFY(r.hasBase());
        QVERIFY(!r.clean());
        QCOMPARE(r.collisionCount(), 1);
        bool found = false;
        for (const QVariant &v : r.hunks()) {
            const QVariantMap h = v.toMap();
            if (h.value(u"kind"_s).toString() == u"conflict") {
                QCOMPARE(h.value(u"ours"_s).toString(), u"- chleb żytni\n"_s);
                QCOMPARE(h.value(u"theirs"_s).toString(), u"- chleb razowy\n"_s);
                found = true;
            }
        }
        QVERIFY(found);
        // The user's resolution (as BMergeView produces it) is written.
        QVERIFY(r.applyMerged(u"- mleko\n- chleb żytni\n- chleb razowy\n- masło\n- ser\n"_s));
        QCOMPARE(ws->readFile(u"Zakupy.md"_s), u"- mleko\n- chleb żytni\n- chleb razowy\n- masło\n- ser\n"_s);
    }

    void noHistoryKeepCopy()
    {
        auto ws = open({ { u"Zakupy.md"_s, "- moja wersja\n" }, { Copy, "- ich wersja\n" } });
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(Copy);
        QVERIFY(r.loaded());
        QVERIFY(!r.hasBase());
        QVERIFY(r.hunks().isEmpty()); // BMergeView diffs the two versions itself
        QCOMPARE(r.ours(), u"- moja wersja\n"_s);
        QCOMPARE(r.theirs(), u"- ich wersja\n"_s);
        QVERIFY(r.keepCopy());
        QCOMPARE(ws->readFile(u"Zakupy.md"_s), u"- ich wersja\n"_s);
        QVERIFY(!ws->exists(Copy));
        // "Mine" can be restored from the history.
        const QVariantList versions = ws->history(u"Zakupy.md"_s);
        QVERIFY(!versions.isEmpty());
        QCOMPARE(ws->historyText(versions.first().toMap().value(u"id"_s).toString()), u"- moja wersja\n"_s);
    }

    void identicalAndKeepMine()
    {
        auto ws = open({ { u"Zakupy.md"_s, "- to samo\r\n" }, { Copy, "- to samo\n" } });
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(Copy);
        QVERIFY(r.identical()); // line endings do not count
        QVERIFY(r.keepMine());
        QCOMPARE(ws->readFile(u"Zakupy.md"_s), u"- to samo\r\n"_s);
        QVERIFY(!ws->exists(Copy));
    }

    void copyIsAnOlderVersion()
    {
        auto ws = conflict(u"- mleko\n"_s, u"- mleko\n- chleb\n"_s, u"- mleko\n"_s);
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(Copy);
        QVERIFY(r.copyIsOlder());
    }

    void binaryKeepCopy()
    {
        const QString photo = u"assets/zdjęcie.png"_s;
        const QString photoCopy = u"assets/zdjęcie.sync-conflict-20261014-093000-ABCDEFG.png"_s;
        auto ws = open({ { photo, QByteArray("\x89PNG-moje", 9) }, { photoCopy, QByteArray("\x89PNG-ich!", 9) } });
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(photoCopy);
        QVERIFY(r.loaded());
        QVERIFY(!r.isText());
        QVERIFY(!r.identical());
        QVERIFY(r.ours().isEmpty());
        QVERIFY(r.keepCopy());
        QCOMPARE(ws->vault()->readBinary(photo), QByteArray("\x89PNG-ich!", 9));
        QVERIFY(!ws->exists(photoCopy));
    }

    void resolvedElsewhere()
    {
        auto ws = open({ { u"Zakupy.md"_s, "- a\n" }, { Copy, "- b\n" } });
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(Copy);
        QVERIFY(r.loaded());
        QVERIFY(ws->sync()->discardConflictCopy(Copy)); // another view did it
        QVERIFY(!r.loaded());
        QVERIFY(r.copy().isEmpty());
        // Unknown copies load nothing and refuse the actions.
        r.setCopy(u"Zakupy.md"_s);
        QVERIFY(!r.loaded());
        QVERIFY(!r.keepMine());
        QVERIFY(!r.keepCopy());
        QVERIFY(!r.applyMerged(u"x"_s));
        QCOMPARE(ws->readFile(u"Zakupy.md"_s), u"- a\n"_s);
    }
};

QTEST_GUILESS_MAIN(TestAppIoConflicts)
#include "tst_appio_conflicts.moc"
