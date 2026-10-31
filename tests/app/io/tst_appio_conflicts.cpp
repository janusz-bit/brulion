// Conflict resolution in the sync center: choosing a common ancestor from the version history
// (pickMergeBase: the true ancestor beats a too-new or too-old version, unrelated texts are
// refused), merge3 hunks for BMergeView (terminators, CRLF, a missing final newline — joining the
// "same" parts with "mine" gives merge3's text), and ConflictResolver over a real Workspace with
// Syncthing conflict copies: a clean three-way merge applied, a real collision, no history at all,
// an identical copy, a copy that is an older version of the note, keeping the copy, binary files.
// Drawings ("Scal rysunki"): the union of both versions' elements written into the original (its
// previous version in the history, the copy in the trash) — Syncthing's and Brulion's copies, the
// Obsidian Excalidraw plugin's files (the Markdown around the scene kept), an identical or older
// copy, a drawing Brulion does not edit (keep one), a decision made on a version that changed, a
// plugin file whose notes around the drawing differ (never "the same" or "older"; merged, its copy
// stays), a drawing too large for the vault's memory still kept in the history; with
// tests/app/io/syncthing-conflict.sh, the conflict copies real Syncthing instances left.
#include "conflictresolver.h"
#include "workspace.h"
#include "workspacesync.h"

#include "drawing/drawingfile.h"
#include "drawing/svgwrite.h"
#include "sync/merge.h"
#include "vault/history.h"
#include "vault/vault.h"

#include <QDir>
#include <QDirIterator>
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

const QString Sketch = u"assets/Szkic ćwiczeń.svg"_s;
const QString SketchCopy = u"assets/Szkic ćwiczeń.sync-conflict-20261014-093000-ABCDEFG.svg"_s;

QByteArray fixture(const QString &name)
{
    QFile f(QStringLiteral(BRULION_TEST_DATA "/drawing/") + name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", qPrintable(name));
    return f.readAll();
}

/// A freehand line `id` at a height of its own (the same id is the same line in every file).
brulion::drawing::Stroke line(const QString &id)
{
    brulion::drawing::Stroke s;
    s.id = id;
    const double y = 40 + 50 * (id.back().unicode() % 10);
    s.points = {{100, y, 0.5}, {200, y + 8, 0.5}, {300, y, 0.5}};
    return s;
}

/// A drawing of lines as Brulion writes it.
QByteArray drawingFile(const QStringList &ids)
{
    brulion::drawing::Drawing d;
    for (const QString &id : ids)
        d.strokes.append(line(id));
    return brulion::drawing::toSvg(d);
}

/// The ids of the elements of the drawing in `bytes` ("<no drawing>": none).
QStringList idsOf(const QByteArray &bytes)
{
    const std::optional<brulion::drawing::DrawingFile> file = brulion::drawing::readDrawingFile(bytes);
    if (!file)
        return {u"<no drawing>"_s};
    QStringList ids;
    for (const brulion::drawing::Stroke &s : file->drawing.strokes)
        ids.append(s.id);
    return ids;
}

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
        // History keeps text only: the replaced picture is in the trash, restorable.
        bool trashed = false;
        for (const QVariant &v : ws->trashItems()) {
            const QVariantMap item = v.toMap();
            if (item.value(u"original"_s).toString() == photo) {
                trashed = true;
                QCOMPARE(ws->vault()->readBinary(item.value(u"trashPath"_s).toString()), QByteArray("\x89PNG-moje", 9));
            }
        }
        QVERIFY(trashed);
    }

    void replacedTextKeptWithinHistoryThrottle()
    {
        // The note was edited twice within History's 5-minute throttle: the second version has no
        // snapshot yet. Resolving the conflict replaces it — it must still land in the history.
        auto ws = conflict(u"- mleko\n"_s, u"- mleko\n- chleb\n"_s, u"- mleko\n- ser\n"_s);
        m_now = m_now.addSecs(60);
        QVERIFY(ws->write(u"Zakupy.md"_s, u"- mleko\n- chleb\n- ostatnia zmiana żółtego sera\n"_s));
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        m_now = m_now.addSecs(60);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(Copy);
        QVERIFY(r.keepCopy());
        QCOMPARE(ws->readFile(u"Zakupy.md"_s), u"- mleko\n- ser\n"_s);
        const QVariantList versions = ws->history(u"Zakupy.md"_s);
        QVERIFY(!versions.isEmpty());
        QCOMPARE(ws->historyText(versions.first().toMap().value(u"id"_s).toString()),
                 u"- mleko\n- chleb\n- ostatnia zmiana żółtego sera\n"_s);
    }

    void staleDecisionRefused()
    {
        // The resolver shows the texts of a moment ago; the note changes meanwhile (another
        // pane, a sync). Applying the old decision would drop that change: refused, reloaded.
        auto ws = conflict(u"- mleko\n- chleb\n- masło\n"_s, u"- mleko\n- chleb\n- masło\n- jajka\n"_s,
                           u"- mleko\n- chleb razowy\n- masło\n"_s);
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(Copy);
        QVERIFY(r.clean());
        const QString merged = r.mergedText();
        QVERIFY(ws->write(u"Zakupy.md"_s, u"- mleko\n- chleb\n- masło\n- jajka\n- nowa linia z drugiego okna\n"_s));
        QSignalSpy failed(&r, &ConflictResolver::failed);
        QVERIFY(!r.applyMerged(merged));
        QCOMPARE(failed.size(), 1);
        QCOMPARE(ws->readFile(u"Zakupy.md"_s), u"- mleko\n- chleb\n- masło\n- jajka\n- nowa linia z drugiego okna\n"_s);
        QVERIFY(ws->exists(Copy));
        // Reloaded: the decision is made again on the current texts.
        QCOMPARE(r.ours(), u"- mleko\n- chleb\n- masło\n- jajka\n- nowa linia z drugiego okna\n"_s);
        // "Keep the copy" on a copy that changed after it was shown (Syncthing updated it).
        QVERIFY(ws->vault()->writeText(Copy, u"- mleko\n- chleb razowy\n- masło\n- miód\n"_s, brulion::ChangeSource::External));
        QVERIFY(!r.keepCopy());
        QCOMPARE(failed.size(), 2);
        QCOMPARE(ws->readFile(u"Zakupy.md"_s), u"- mleko\n- chleb\n- masło\n- jajka\n- nowa linia z drugiego okna\n"_s);
        QCOMPARE(r.theirs(), u"- mleko\n- chleb razowy\n- masło\n- miód\n"_s);
        QVERIFY(r.applyMerged(r.clean() ? r.mergedText() : r.ours()));
        QVERIFY(!ws->exists(Copy));
    }

    // ---- Drawings ------------------------------------------------------------------------------------

    void drawingConflictMerges_data()
    {
        QTest::addColumn<QString>("copy");
        QTest::addColumn<QString>("source");
        QTest::newRow("Syncthing") << SketchCopy << u"syncthing"_s;
        QTest::newRow("Brulion (Nextcloud)")
                << u"assets/Szkic ćwiczeń.conflict-Telefon-Łucji-20261014-091500.svg"_s << u"brulion"_s;
    }
    void drawingConflictMerges()
    {
        QFETCH(QString, copy);
        QFETCH(QString, source);
        const QByteArray original = drawingFile({u"a"_s, u"b"_s, u"c"_s});
        const QByteArray copied = drawingFile({u"a"_s, u"b"_s, u"d"_s});
        auto ws = open({{Sketch, original}, {copy, copied}});
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(copy);
        QVERIFY(r.loaded());
        QCOMPARE(r.conflict().value(u"source"_s).toString(), source);
        QCOMPARE(r.conflict().value(u"original"_s).toString(), Sketch);
        QVERIFY(r.isDrawing());
        QVERIFY(!r.isText()); // never merged line by line
        QVERIFY(r.hunks().isEmpty());
        QVERIFY(r.canMergeDrawings());
        QVERIFY(!r.identical());
        QVERIFY(!r.copyIsOlder());
        QCOMPARE(r.oursBytes(), original);
        QCOMPARE(r.theirsBytes(), copied);
        const QVariantMap counts = r.drawingCounts();
        QCOMPARE(counts.value(u"ours"_s).toInt(), 3);
        QCOMPARE(counts.value(u"theirs"_s).toInt(), 3);
        QCOMPARE(counts.value(u"common"_s).toInt(), 2);
        QCOMPARE(counts.value(u"onlyOurs"_s).toInt(), 1);
        QCOMPARE(counts.value(u"onlyTheirs"_s).toInt(), 1);
        QCOMPARE(counts.value(u"merged"_s).toInt(), 4);
        QCOMPARE(idsOf(r.mergedBytes()), (QStringList{u"a"_s, u"b"_s, u"c"_s, u"d"_s}));

        QSignalSpy resolved(&r, &ConflictResolver::resolved);
        QVERIFY(r.mergeDrawings());
        QCOMPARE(resolved.size(), 1);
        QVERIFY(!r.loaded());
        // The original has the lines of both, the copy is in the trash, the previous version in the
        // history.
        QCOMPARE(idsOf(ws->vault()->readBinary(Sketch)), (QStringList{u"a"_s, u"b"_s, u"c"_s, u"d"_s}));
        QVERIFY(!ws->exists(copy));
        QCOMPARE(ws->sync()->conflictCount(), 0);
        bool trashed = false;
        for (const QVariant &v : ws->trashItems())
            trashed = trashed || v.toMap().value(u"original"_s).toString() == copy;
        QVERIFY(trashed);
        const QVariantList versions = ws->history(Sketch);
        QVERIFY(!versions.isEmpty());
        QCOMPARE(ws->historyText(versions.first().toMap().value(u"id"_s).toString()), QString::fromUtf8(original));
    }

    void drawingCopyIsOlder()
    {
        auto ws = open({{Sketch, drawingFile({u"a"_s, u"b"_s, u"c"_s})}, {SketchCopy, drawingFile({u"a"_s, u"b"_s})}});
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(SketchCopy);
        QVERIFY(r.isDrawing());
        QVERIFY(!r.identical());
        QVERIFY(r.copyIsOlder());
        QVERIFY(r.canMergeDrawings());
        QVERIFY(r.keepMine());
        QVERIFY(!ws->exists(SketchCopy));
        QCOMPARE(idsOf(ws->vault()->readBinary(Sketch)), (QStringList{u"a"_s, u"b"_s, u"c"_s}));
    }

    void identicalDrawings()
    {
        // The same lines, the picture written by another program (other bytes).
        const QByteArray original = drawingFile({u"a"_s, u"ż"_s});
        QByteArray copied = original;
        copied.replace("</svg>", "<!-- zapisane gdzie indziej --></svg>");
        QVERIFY(copied != original);
        auto ws = open({{Sketch, original}, {SketchCopy, copied}});
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(SketchCopy);
        QVERIFY(r.isDrawing());
        QVERIFY(r.identical());
        QVERIFY(!r.copyIsOlder());
        QVERIFY(r.keepMine());
        QCOMPARE(ws->vault()->readBinary(Sketch), original);
    }

    void drawingWithAFrameIsNotMerged()
    {
        // A copy with a frame from excalidraw.com (an element Brulion does not draw): shown, not
        // merged — keep one of them, the exact bytes.
        const QByteArray original = drawingFile({u"a"_s});
        const QByteArray framed = fixture(u"excalidraw_frame.svg"_s);
        auto ws = open({{Sketch, original}, {SketchCopy, framed}});
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(SketchCopy);
        QVERIFY(r.isDrawing());
        QVERIFY(!r.isText());
        QVERIFY(!r.canMergeDrawings());
        QVERIFY(r.mergedBytes().isEmpty());
        QVERIFY(!r.identical());
        QVERIFY(!r.copyIsOlder());
        QVERIFY(!r.mergeDrawings());
        QVERIFY(!r.applyMerged(QString::fromUtf8(framed))); // never a text merge of a drawing
        QVERIFY(r.keepCopy());
        QCOMPARE(ws->vault()->readBinary(Sketch), framed);
        QVERIFY(!ws->exists(SketchCopy));
        const QVariantList versions = ws->history(Sketch);
        QVERIFY(!versions.isEmpty());
        QCOMPARE(ws->historyText(versions.first().toMap().value(u"id"_s).toString()), QString::fromUtf8(original));
    }

    void pictureAgainstADrawing()
    {
        // A plain SVG (no scene) against a drawing: shown as drawings, nothing to merge.
        auto ws = open({{Sketch, drawingFile({u"a"_s})},
                        {SketchCopy, "<svg xmlns=\"http://www.w3.org/2000/svg\"><circle r=\"4\"/></svg>"}});
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(SketchCopy);
        QVERIFY(r.isDrawing());
        QVERIFY(!r.canMergeDrawings());
        QVERIFY(!r.mergeDrawings());
        // Two plain pictures are text, as before.
        auto plain = open({{u"logo.svg"_s, "<svg xmlns=\"http://www.w3.org/2000/svg\"/>\n"},
                           {u"logo.sync-conflict-20261014-093000-ABCDEFG.svg"_s,
                            "<svg xmlns=\"http://www.w3.org/2000/svg\"><rect/></svg>\n"}});
        QTRY_COMPARE(plain->sync()->conflictCount(), 1);
        ConflictResolver t;
        t.setWorkspace(plain.get());
        t.setCopy(u"logo.sync-conflict-20261014-093000-ABCDEFG.svg"_s);
        QVERIFY(!t.isDrawing());
        QVERIFY(t.isText());
    }

    void deletedDrawingRestoredFromTheCopy()
    {
        auto ws = open({{SketchCopy, drawingFile({u"a"_s, u"d"_s})}});
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(SketchCopy);
        QVERIFY(r.isDrawing());
        QVERIFY(!r.conflict().value(u"originalExists"_s).toBool());
        QVERIFY(!r.canMergeDrawings());
        QVERIFY(r.keepCopy());
        QCOMPARE(idsOf(ws->vault()->readBinary(Sketch)), (QStringList{u"a"_s, u"d"_s}));
        QVERIFY(!ws->exists(SketchCopy));
    }

    void staleDrawingDecisionRefused()
    {
        auto ws = open({{Sketch, drawingFile({u"a"_s, u"b"_s})}, {SketchCopy, drawingFile({u"a"_s, u"c"_s})}});
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(SketchCopy);
        QVERIFY(r.canMergeDrawings());
        QCOMPARE(r.drawingCounts().value(u"merged"_s).toInt(), 3);
        // Syncthing brings a newer copy meanwhile.
        QVERIFY(ws->vault()->writeBinary(SketchCopy, drawingFile({u"a"_s, u"c"_s, u"e"_s}),
                                         brulion::ChangeSource::External));
        QSignalSpy failed(&r, &ConflictResolver::failed);
        QVERIFY(!r.mergeDrawings());
        QCOMPARE(failed.size(), 1);
        QCOMPARE(idsOf(ws->vault()->readBinary(Sketch)), (QStringList{u"a"_s, u"b"_s}));
        QVERIFY(ws->exists(SketchCopy));
        // Reloaded: the decision is made again on what is there now.
        QCOMPARE(r.drawingCounts().value(u"merged"_s).toInt(), 4);
        // The original changed under "Keep the copy": refused as well.
        QVERIFY(ws->vault()->writeBinary(Sketch, drawingFile({u"a"_s, u"b"_s, u"f"_s}),
                                         brulion::ChangeSource::External));
        QVERIFY(!r.keepCopy());
        QCOMPARE(failed.size(), 2);
        QCOMPARE(idsOf(ws->vault()->readBinary(Sketch)), (QStringList{u"a"_s, u"b"_s, u"f"_s}));
        QVERIFY(r.mergeDrawings());
        QCOMPARE(idsOf(ws->vault()->readBinary(Sketch)), (QStringList{u"a"_s, u"b"_s, u"f"_s, u"c"_s, u"e"_s}));
    }

    void pluginDrawingConflictMerges()
    {
        // A Syncthing copy of an Obsidian Excalidraw plugin file: one line drawn on each device.
        const QString original = u"Excalidraw/Szkic.excalidraw.md"_s;
        const QString copy = u"Excalidraw/Szkic.excalidraw.sync-conflict-20261014-093000-ABCDEFG.md"_s;
        const QByteArray base = fixture(u"obsidian_freedraw.excalidraw.md"_s);
        const std::optional<brulion::drawing::DrawingFile> file = brulion::drawing::readDrawingFile(base);
        QVERIFY(file);
        brulion::drawing::Drawing mine = file->drawing;
        mine.strokes.append(line(u"tutaj"_s));
        brulion::drawing::Drawing theirs = file->drawing;
        theirs.strokes.append(line(u"tam-ż"_s));
        auto ws = open({{original, brulion::drawing::writeDrawingFile(*file, mine, 1761650000000)},
                        {copy, brulion::drawing::writeDrawingFile(*file, theirs, 1761650000000)}});
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(copy);
        QVERIFY(r.isDrawing());
        QVERIFY(!r.isText());
        QVERIFY(r.canMergeDrawings());
        QVERIFY(r.mergeDrawings());
        const QByteArray merged = ws->vault()->readBinary(original);
        const std::optional<brulion::drawing::DrawingFile> back = brulion::drawing::readDrawingFile(merged);
        QVERIFY(back);
        QCOMPARE(back->format, brulion::drawing::FileFormat::ExcalidrawMarkdown);
        QStringList expected;
        for (const brulion::drawing::Stroke &s : file->drawing.strokes)
            expected.append(s.id);
        expected << u"tutaj"_s << u"tam-ż"_s;
        QCOMPARE(idsOf(merged), expected);
        // The frontmatter and the back of the card byte for byte.
        QVERIFY(merged.startsWith(base.first(base.indexOf("## Drawing"))));
        QVERIFY(!ws->exists(copy));
    }

    void pluginDrawingOnlyTheNotesDiffer_data()
    {
        // The copy adds nothing to the drawing, but its notes around it (the back of the card)
        // were changed in Obsidian on the other device: neither "the same" nor "older" — removing
        // it would lose those notes without a word. With a paper of its own (Brulion's dark one)
        // the copy is not contained in the drawing, but a merge would add nothing to it either.
        QTest::addColumn<bool>("drawnHere");
        QTest::addColumn<bool>("otherPaper");
        QTest::newRow("the same drawing") << false << false;
        QTest::newRow("a line more here") << true << false;
        QTest::newRow("another paper there") << false << true;
        QTest::newRow("a line more here, another paper there") << true << true;
    }
    void pluginDrawingOnlyTheNotesDiffer()
    {
        QFETCH(bool, drawnHere);
        QFETCH(bool, otherPaper);
        const QString original = u"Excalidraw/Szkic.excalidraw.md"_s;
        const QString copy = u"Excalidraw/Szkic.excalidraw.sync-conflict-20261014-093000-ABCDEFG.md"_s;
        const QByteArray base = fixture(u"obsidian_freedraw.excalidraw.md"_s);
        const std::optional<brulion::drawing::DrawingFile> file = brulion::drawing::readDrawingFile(base);
        QVERIFY(file);
        brulion::drawing::Drawing mine = file->drawing;
        if (drawnHere)
            mine.strokes.append(line(u"tutaj"_s));
        brulion::drawing::Drawing theirs = file->drawing;
        theirs.theme = brulion::drawing::PaperTheme::Dark;
        QByteArray noted = otherPaper ? brulion::drawing::writeDrawingFile(*file, theirs, 1761650000000) : base;
        noted.replace("Notatka z tyłu: Zażółć gęślą jaźń", "Notatka z tyłu: WAŻNE zmiany z laptopa, ąę");
        QVERIFY(noted != base);
        auto ws = open({{original, brulion::drawing::writeDrawingFile(*file, mine, 1761650000000)}, {copy, noted}});
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(copy);
        QVERIFY(r.isDrawing());
        QVERIFY(!r.identical());
        QVERIFY(!r.copyIsOlder());
        QVERIFY(r.notesDiffer());
        QVERIFY(r.notesOnly());
        QVERIFY(!r.canMergeDrawings());
        QVERIFY(!r.mergeDrawings());
        QVERIFY(r.oursNotes().contains(u"Notatka z tyłu: Zażółć gęślą jaźń"_s));
        QVERIFY(r.theirsNotes().contains(u"Notatka z tyłu: WAŻNE zmiany z laptopa, ąę"_s));
        QVERIFY(!r.theirsNotes().contains(
                u"N4KAkARALgngDgUwgLgAQQQDwMYEMA2AlgCYBOuA7hADTgQBuCpAzoQPYB2KqATLZMzYBXUtiRoIACyhQ4zZAHoFAc"_s));
        // Told, the user removes the copy (it goes to the trash).
        QVERIFY(r.keepMine());
        QVERIFY(!ws->exists(copy));
        QCOMPARE(idsOf(ws->vault()->readBinary(original)), idsOf(brulion::drawing::writeDrawingFile(*file, mine)));
    }

    void pluginDrawingNotesDifferMergeKeepsTheCopy_data()
    {
        // (Another paper there: the merge keeps the original's, so the copy is never contained in
        // the merged drawing — the notes are all that is left to do all the same.)
        QTest::addColumn<bool>("otherPaper");
        QTest::newRow("the same paper") << false;
        QTest::newRow("another paper there") << true;
    }
    void pluginDrawingNotesDifferMergeKeepsTheCopy()
    {
        // Both devices drew a line, and the other one changed the back of the card as well (what
        // Nextcloud leaves as a conflict copy instead of merging): "Scal rysunki" merges the
        // elements into the original and keeps the copy, whose notes would be lost with it.
        QFETCH(bool, otherPaper);
        const QString original = u"Excalidraw/Szkic.excalidraw.md"_s;
        const QString copy = u"Excalidraw/Szkic.excalidraw.sync-conflict-20261014-093000-ABCDEFG.md"_s;
        const QByteArray base = fixture(u"obsidian_freedraw.excalidraw.md"_s);
        const std::optional<brulion::drawing::DrawingFile> file = brulion::drawing::readDrawingFile(base);
        QVERIFY(file);
        brulion::drawing::Drawing mine = file->drawing;
        mine.strokes.append(line(u"tutaj"_s));
        brulion::drawing::Drawing theirs = file->drawing;
        theirs.strokes.append(line(u"tam-ż"_s));
        if (otherPaper)
            theirs.theme = brulion::drawing::PaperTheme::Dark;
        const QByteArray ours = brulion::drawing::writeDrawingFile(*file, mine, 1761650000000);
        QByteArray copied = brulion::drawing::writeDrawingFile(*file, theirs, 1761650000000);
        copied.replace("Notatka z tyłu: Zażółć gęślą jaźń", "Notatka z tyłu: WAŻNE zmiany z laptopa");
        auto ws = open({{original, ours}, {copy, copied}});
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(copy);
        QVERIFY(r.isDrawing());
        QVERIFY(r.notesDiffer());
        QVERIFY(!r.notesOnly());
        QVERIFY(!r.identical());
        QVERIFY(!r.copyIsOlder());
        QVERIFY(r.canMergeDrawings());
        QSignalSpy resolved(&r, &ConflictResolver::resolved);
        QSignalSpy updated(&r, &ConflictResolver::updated);
        QVERIFY(r.mergeDrawings());
        QCOMPARE(resolved.size(), 0);
        QCOMPARE(updated.size(), 1);
        // The elements of both in the original, its own notes; the copy with its notes still there.
        const QByteArray merged = ws->vault()->readBinary(original);
        QStringList expected;
        for (const brulion::drawing::Stroke &s : file->drawing.strokes)
            expected.append(s.id);
        expected << u"tutaj"_s << u"tam-ż"_s;
        QCOMPARE(idsOf(merged), expected);
        QVERIFY(merged.contains("Notatka z tyłu: Zażółć gęślą jaźń"));
        QVERIFY(ws->exists(copy));
        QCOMPARE(ws->vault()->readBinary(copy), copied);
        QCOMPARE(ws->sync()->conflictCount(), 1);
        const QVariantList versions = ws->history(original);
        QVERIFY(!versions.isEmpty());
        QCOMPARE(ws->historyText(versions.first().toMap().value(u"id"_s).toString()), QString::fromUtf8(ours));
        // Reloaded: only the notes differ now; the copy goes when the user says so.
        QVERIFY(r.loaded());
        QVERIFY(r.notesOnly());
        QVERIFY(!r.canMergeDrawings());
        QVERIFY(r.theirsNotes().contains(u"WAŻNE zmiany z laptopa"_s));
        QVERIFY(r.keepMine());
        QCOMPARE(resolved.size(), 1);
        QVERIFY(!ws->exists(copy));
    }

    void largeDrawingKeptInHistory_data()
    {
        QTest::addColumn<bool>("merge");
        QTest::newRow("keep the copy") << false;
        QTest::newRow("merge drawings") << true;
    }
    void largeDrawingKeptInHistory()
    {
        // A drawing with pasted photos beyond what the vault holds in memory (16 MB): the version
        // replaced is in the history all the same, as the dialog and the toast say.
        QFETCH(bool, merge);
        QByteArray original = drawingFile({u"a"_s, u"b"_s});
        const QByteArray padding = QByteArray("<!-- ") + QByteArray(17 * 1024 * 1024, 'x') + QByteArray(" --></svg>");
        original.replace("</svg>", padding);
        QVERIFY(original.size() > 17 * 1024 * 1024);
        const QByteArray copied = drawingFile({u"a"_s, u"c"_s});
        auto ws = open({{Sketch, original}, {SketchCopy, copied}});
        QTRY_COMPARE(ws->sync()->conflictCount(), 1);
        ConflictResolver r;
        r.setWorkspace(ws.get());
        r.setCopy(SketchCopy);
        QVERIFY(r.isDrawing());
        QCOMPARE(r.canMergeDrawings(), true);
        QVERIFY(merge ? r.mergeDrawings() : r.keepCopy());
        QCOMPARE(idsOf(ws->vault()->readBinary(Sketch)),
                 merge ? (QStringList{u"a"_s, u"b"_s, u"c"_s}) : (QStringList{u"a"_s, u"c"_s}));
        QVERIFY(!ws->exists(SketchCopy));
        const QVariantList versions = ws->history(Sketch);
        QCOMPARE(versions.size(), 1);
        bool ok = false;
        const QByteArray kept =
                ws->vault()->history()->readBytes(versions.first().toMap().value(u"id"_s).toString(), &ok);
        QVERIFY(ok);
        QVERIFY(kept == original);
    }

    void realSyncthingConflicts()
    {
        // tests/app/io/syncthing-conflict.sh: a folder where two real Syncthing instances left their
        // own conflict copies of a Brulion drawing and of an Obsidian one, both changed on both
        // devices. Each is recognised, merged ("Scal rysunki": the elements of both versions) and
        // its copy goes to the trash.
        const QString dir = qEnvironmentVariable("BRULION_SYNCTHING_CONFLICT_DIR");
        if (dir.isEmpty())
            QSKIP("run by tests/app/io/syncthing-conflict.sh (two Syncthing instances)");
        QHash<QString, QByteArray> files;
        QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = QDir(dir).relativeFilePath(it.next());
            if (path.startsWith(u".st"_s))
                continue; // (.stfolder, .stversions)
            QFile f(it.filePath());
            QVERIFY(f.open(QIODevice::ReadOnly));
            files.insert(path, f.readAll());
        }
        qInfo().noquote() << "files:" << QStringList(files.keyBegin(), files.keyEnd()).join(u", "_s);
        auto ws = open(files);
        QTRY_COMPARE(ws->sync()->conflictCount(), 2);
        const QVariantList conflicts = ws->sync()->conflicts();
        for (const QVariant &v : conflicts) {
            const QVariantMap c = v.toMap();
            const QString copy = c.value(u"copy"_s).toString();
            const QString original = c.value(u"original"_s).toString();
            QVERIFY2(copy.contains(u".sync-conflict-"_s), qPrintable(copy));
            QCOMPARE(c.value(u"source"_s).toString(), u"syncthing"_s);
            QVERIFY(c.value(u"drawing"_s).toBool());
            ConflictResolver r;
            r.setWorkspace(ws.get());
            r.setCopy(copy);
            QVERIFY(r.isDrawing());
            QVERIFY(!r.identical());
            QVERIFY(!r.copyIsOlder());
            QVERIFY(!r.notesDiffer());
            QVERIFY(r.canMergeDrawings());
            QStringList both = idsOf(files.value(original)) + idsOf(files.value(copy));
            both.removeDuplicates();
            both.sort();
            QVERIFY(r.mergeDrawings());
            QStringList merged = idsOf(ws->vault()->readBinary(original));
            QCOMPARE(merged.size(), both.size());
            merged.sort();
            QCOMPARE(merged, both);
            QVERIFY(!ws->exists(copy));
            qInfo().noquote() << "merged" << original << "with" << copy << "->" << merged.join(u' ');
        }
        QCOMPARE(ws->sync()->conflictCount(), 0);
    }

    void listMarksDrawings()
    {
        auto ws = open({{Sketch, drawingFile({u"a"_s})},
                        {SketchCopy, drawingFile({u"b"_s})},
                        {u"Zakupy.md"_s, "- a\n"},
                        {Copy, "- b\n"}});
        QTRY_COMPARE(ws->sync()->conflictCount(), 2);
        for (const QVariant &v : ws->sync()->conflicts()) {
            const QVariantMap c = v.toMap();
            const bool drawing = c.value(u"original"_s).toString() == Sketch;
            QCOMPARE(c.value(u"drawing"_s).toBool(), drawing);
            QCOMPARE(c.value(u"text"_s).toBool(), true); // (both are text files to sync)
        }
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
