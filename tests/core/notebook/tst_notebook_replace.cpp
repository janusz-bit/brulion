// Text replaced in the whole notebook (notebook/replace.h): what a plan lists and leaves out, its
// contexts and links, applying (bytes outside the hits, excluded hits, a file changed since the plan,
// History), undoing, and the scan on threads and in slices.
#include "notebook/replace.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::notebook;

namespace {

struct Notebook {
    QTemporaryDir root;
    QTemporaryDir state;
    Vault vault;

    Notebook() : vault(root.path(), state.path()) { }
    QByteArray bytes(const QString &path) const
    {
        QFile f(root.filePath(path));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    void write(const QString &path, const QByteArray &data) const
    {
        QDir().mkpath(QFileInfo(root.filePath(path)).absolutePath());
        QFile f(root.filePath(path));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }
    void open()
    {
        QVERIFY(vault.open());
    }
};

QStringList pathsOf(const ReplacePlan &plan)
{
    QStringList out;
    for (const ReplaceFile &file : plan.files)
        out.append(file.path);
    return out;
}

} // namespace

class TestNotebookReplace : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void noteOrNot_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("note");
        QTest::newRow("note") << u"Żółw.md"_s << true;
        QTest::newRow("in a folder") << u"Projekty/Dom.md"_s << true;
        QTest::newRow("markdown extension") << u"a.markdown"_s << true;
        QTest::newRow("drawing") << u"Szkic.excalidraw.md"_s << false;
        QTest::newRow("drawing, upper case") << u"Szkic.EXCALIDRAW.MD"_s << false;
        QTest::newRow("svg") << u"assets/Rysunek.svg"_s << false;
        QTest::newRow("picture") << u"assets/a.png"_s << false;
        QTest::newRow("hidden file") << u".ukryta.md"_s << false;
        QTest::newRow("hidden folder") << u"a/.b/c.md"_s << false;
        QTest::newRow("syncthing conflict") << u"a.sync-conflict-20261014-101010-ABCDEFG.md"_s << false;
    }
    void noteOrNot()
    {
        QFETCH(QString, path);
        QFETCH(bool, note);
        QCOMPARE(isReplaceableNote(path), note);
    }

    void plansOnlyNotes()
    {
        Notebook nb;
        nb.write(u"A.md"_s, "żółw\n");
        nb.write(u"B/Dom.md"_s, "ZOLW i zolw\n");
        nb.write(u"Bez.md"_s, "nic\n");
        nb.write(u".trash/Stary.md"_s, "żółw\n");
        nb.write(u".obsidian/x.md"_s, "żółw\n");
        nb.write(u"Szkic.excalidraw.md"_s, "---\nexcalidraw-plugin: parsed\n---\nżółw\n");
        nb.write(u"Rysunek po cichu.md"_s, "---\nexcalidraw-plugin: parsed\n---\nżółw\n");
        nb.write(u"assets/r.svg"_s, "<svg>żółw</svg>");
        nb.write(u"notatka.txt"_s, "żółw\n");
        nb.write(u"Windows.md"_s, QByteArray("\xBF\xF3\xB3w zolw\n")); // windows-1250: left alone
        nb.open();
        const ReplacePlan plan = planReplace(nb.vault, u"zolw"_s, {});
        QCOMPARE(pathsOf(plan), (QStringList{u"A.md"_s, u"B/Dom.md"_s}));
        QCOMPARE(plan.totalHits, 3);
        QCOMPARE(plan.previewHits, 3);
        QCOMPARE(plan.skipped, (QList<VaultPath>{u"Windows.md"_s}));
        QCOMPARE(plan.files.at(1).hitCount, 2);
    }

    void contexts()
    {
        const QString text = u"pierwsza\r\n- druga żółw koniec\r\nżółw\n"_s;
        const ReplaceFile file = planFile(u"a.md"_s, text, u"zolw", {}, 50);
        QCOMPARE(file.hitCount, 2);
        QCOMPARE(file.hits.at(0).index, 0);
        QCOMPARE(file.hits.at(0).line, 1);
        QCOMPARE(file.hits.at(0).before, u"- druga "_s);
        QCOMPARE(file.hits.at(0).matched, u"żółw"_s);
        QCOMPARE(file.hits.at(0).after, u" koniec"_s);
        QCOMPARE(file.hits.at(1).line, 2);
        QCOMPARE(file.hits.at(1).before, QString());
        QCOMPARE(file.hits.at(1).after, QString());
        QCOMPARE(text.sliced(file.hits.at(1).start, file.hits.at(1).length), u"żółw"_s);
    }

    void contextIsCut()
    {
        const QString pad = QString(100, u'x');
        const QString emoji = QString(100, u'y') + u"😀"_s + QString(39, u'x');
        const ReplaceFile file = planFile(u"a.md"_s, QString(pad + u"kot"_s + pad + u"\n"_s + emoji + u"kot"_s), u"kot", {}, 50);
        QCOMPARE(file.hits.at(0).before.size(), 41);
        QVERIFY(file.hits.at(0).before.startsWith(u'…'));
        QVERIFY(file.hits.at(0).after.endsWith(u'…'));
        // The cut does not fall inside the emoji.
        QVERIFY(!file.hits.at(1).before.at(1).isLowSurrogate());
    }

    void previewIsLimited()
    {
        const QString many = QString(u"a "_s).repeated(100);
        const ReplaceFile file = planFile(u"a.md"_s, many, u"a", {}, 10);
        QCOMPARE(file.hitCount, 100);
        QCOMPARE(file.hits.size(), 10);

        Notebook nb;
        for (int i = 0; i < 5; ++i)
            nb.write(u"n%1.md"_s.arg(i), many.toUtf8());
        nb.open();
        const ReplacePlan plan = planReplace(nb.vault, u"a"_s, {}, {.previewPerFile = 10, .previewTotal = 25});
        QCOMPARE(plan.totalHits, 500);
        QCOMPARE(plan.previewHits, 25);
        QCOMPARE(plan.files.size(), 5); // listed and counted without a preview too
        QCOMPARE(plan.files.at(4).hitCount, 100);
        QCOMPARE(plan.files.at(4).hits.size(), 0);
    }

    void hitsInLinks_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<bool>("inLink");
        QTest::newRow("plain") << u"kot w domu"_s << false;
        QTest::newRow("wiki link") << u"zobacz [[kot]]"_s << true;
        QTest::newRow("wiki link with a path") << u"[[Zwierzęta/kot|mruczek]]"_s << true;
        QTest::newRow("display text is the link's too") << u"[[Strona|kot]]"_s << true;
        QTest::newRow("embed") << u"![[kot.png]]"_s << true;
        QTest::newRow("after a link") << u"[[a]] kot"_s << false;
        QTest::newRow("before a link") << u"kot [[a]]"_s << false;
        QTest::newRow("markdown target") << u"[x](kot.md)"_s << true;
        QTest::newRow("markdown text") << u"[kot](x.md)"_s << false;
        QTest::newRow("after a markdown link") << u"[x](y.md) kot"_s << false;
    }
    void hitsInLinks()
    {
        QFETCH(QString, line);
        QFETCH(bool, inLink);
        const ReplaceFile file = planFile(u"a.md"_s, QString(u"wstęp\n"_s + line + u"\n[[inna]]"_s), u"kot", {}, 50);
        QCOMPARE(file.hits.size(), 1);
        QCOMPARE(file.hits.at(0).inLink, inLink);
    }

    void applyKeepsEveryOtherByte()
    {
        Notebook nb;
        const QByteArray bom = "\xEF\xBB\xBF";
        nb.write(u"Crlf.md"_s, bom + QStringLiteral(u"- żółw\r\n\tZOLW i ą\r\n\r\nkoniec bez końca linii").toUtf8());
        nb.write(u"Lf.md"_s, QStringLiteral(u"zolw\n").toUtf8());
        nb.open();
        const ReplacePlan plan = planReplace(nb.vault, u"zolw"_s, {});
        const QList<ReplacedFile> results = applyReplace(nb.vault, plan, u"kot"_s, {{u"Crlf.md"_s, {}}});
        QCOMPARE(results.size(), 1);
        QCOMPARE(results.at(0).status, ReplaceStatus::Written);
        QCOMPARE(results.at(0).replaced, 2);
        QCOMPARE(nb.bytes(u"Crlf.md"_s),
                 bom + QStringLiteral(u"- kot\r\n\tkot i ą\r\n\r\nkoniec bez końca linii").toUtf8());
        QCOMPARE(nb.bytes(u"Lf.md"_s), QStringLiteral(u"zolw\n").toUtf8()); // not chosen
    }

    void excludedHitsStay()
    {
        Notebook nb;
        nb.write(u"a.md"_s, "a a a a\n");
        nb.open();
        const ReplacePlan plan = planReplace(nb.vault, u"a"_s, {});
        const QList<ReplacedFile> results = applyReplace(nb.vault, plan, u"b"_s, {{u"a.md"_s, {1, 3}}});
        QCOMPARE(results.at(0).replaced, 2);
        QCOMPARE(nb.bytes(u"a.md"_s), "b a b a\n");
    }

    void allExcludedWritesNothing()
    {
        Notebook nb;
        nb.write(u"a.md"_s, "a\n");
        nb.open();
        const ReplacePlan plan = planReplace(nb.vault, u"a"_s, {});
        const QList<ReplacedFile> results = applyReplace(nb.vault, plan, u"b"_s, {{u"a.md"_s, {0}}});
        QCOMPARE(results.at(0).status, ReplaceStatus::Written);
        QCOMPARE(results.at(0).replaced, 0);
        QCOMPARE(nb.bytes(u"a.md"_s), "a\n");
    }

    void stalePlanIsRefused()
    {
        Notebook nb;
        nb.write(u"Zmieniona.md"_s, "kot\n");
        nb.write(u"Usunięta.md"_s, "kot\n");
        nb.write(u"Bez zmian.md"_s, "kot\n");
        nb.open();
        const ReplacePlan plan = planReplace(nb.vault, u"kot"_s, {});
        QCOMPARE(plan.files.size(), 3);
        // Another program writes between the plan and applying; the vault has not seen it yet.
        nb.write(u"Zmieniona.md"_s, "nowy tekst, kot gdzie indziej\n");
        QFile::remove(nb.root.filePath(u"Usunięta.md"_s));
        QList<ReplaceChoice> choices;
        for (const ReplaceFile &file : plan.files)
            choices.append({file.path, {}});
        const QList<ReplacedFile> results = applyReplace(nb.vault, plan, u"pies"_s, choices);
        QHash<QString, ReplaceStatus> status;
        for (const ReplacedFile &r : results)
            status.insert(r.path, r.status);
        QCOMPARE(status.value(u"Zmieniona.md"_s), ReplaceStatus::Changed);
        QCOMPARE(status.value(u"Usunięta.md"_s), ReplaceStatus::Changed);
        QCOMPARE(status.value(u"Bez zmian.md"_s), ReplaceStatus::Written);
        QCOMPARE(nb.bytes(u"Zmieniona.md"_s), "nowy tekst, kot gdzie indziej\n");
        QVERIFY(!QFile::exists(nb.root.filePath(u"Usunięta.md"_s)));
        QCOMPARE(nb.bytes(u"Bez zmian.md"_s), "pies\n");
    }

    void historyKeepsTheOldText()
    {
        Notebook nb;
        nb.write(u"a.md"_s, "kot\n");
        nb.open();
        // An ordinary save a moment ago: its throttle must not swallow the replace's snapshot.
        QVERIFY(nb.vault.writeText(u"a.md"_s, u"kot i kot\n"_s));
        const ReplacePlan plan = planReplace(nb.vault, u"kot"_s, {});
        applyReplace(nb.vault, plan, u"pies"_s, {{u"a.md"_s, {}}});
        const QList<HistoryEntry> entries = nb.vault.history()->list(u"a.md"_s);
        QVERIFY(!entries.isEmpty());
        QCOMPARE(entries.first().reason, u"replace"_s);
        QCOMPARE(nb.vault.history()->read(entries.first().id), u"kot i kot\n"_s);
        QCOMPARE(nb.vault.historyReason(), QString()); // the vault's own reason back
    }

    void undoOnlyWhatStillHolds()
    {
        Notebook nb;
        const QByteArray bom = "\xEF\xBB\xBF";
        nb.write(u"Tak.md"_s, bom + "kot\r\n");
        nb.write(u"Nie.md"_s, "kot\n");
        nb.write(u"Bez pliku.md"_s, "kot\n");
        nb.open();
        const ReplacePlan plan = planReplace(nb.vault, u"kot"_s, {});
        QList<ReplaceChoice> choices;
        for (const ReplaceFile &file : plan.files)
            choices.append({file.path, {}});
        const QList<ReplacedFile> results = applyReplace(nb.vault, plan, u"pies"_s, choices);
        QVERIFY(nb.vault.writeText(u"Nie.md"_s, u"pies i coś nowego\n"_s));
        QVERIFY(nb.vault.remove(u"Bez pliku.md"_s));
        for (const ReplacedFile &r : results) {
            const UndoReplaceStatus undone = undoReplacedFile(nb.vault, r);
            if (r.path == u"Tak.md")
                QCOMPARE(undone, UndoReplaceStatus::Restored);
            else
                QCOMPARE(undone, UndoReplaceStatus::Changed);
        }
        QCOMPARE(nb.bytes(u"Tak.md"_s), bom + "kot\r\n");
        QCOMPARE(nb.bytes(u"Nie.md"_s), "pies i coś nowego\n");
        QVERIFY(!QFile::exists(nb.root.filePath(u"Bez pliku.md"_s)));
    }

    void undoDoesNotBringBackADeletedNote()
    {
        Notebook nb;
        nb.write(u"Skasowana.md"_s, "kot\n");
        nb.open();
        const ReplacePlan plan = planReplace(nb.vault, u"kot"_s, {});
        const QList<ReplacedFile> results = applyReplace(nb.vault, plan, u"pies"_s, {{u"Skasowana.md"_s, {}}});
        QCOMPARE(results.at(0).status, ReplaceStatus::Written);
        // Another program deletes it; the vault's cached listing has not seen that yet.
        QVERIFY(QFile::remove(nb.root.filePath(u"Skasowana.md"_s)));
        QVERIFY(nb.vault.exists(u"Skasowana.md"_s));
        QCOMPARE(undoReplacedFile(nb.vault, results.at(0)), UndoReplaceStatus::Changed);
        QVERIFY(!QFile::exists(nb.root.filePath(u"Skasowana.md"_s)));
    }

    void identicalTextReplacesNothing()
    {
        Notebook nb;
        nb.write(u"a.md"_s, "kot kot\n");
        nb.open();
        const ReplacePlan plan = planReplace(nb.vault, u"kot"_s, {});
        const QList<ReplacedFile> results = applyReplace(nb.vault, plan, u"kot"_s, {{u"a.md"_s, {}}});
        QCOMPARE(results.at(0).status, ReplaceStatus::Written);
        QCOMPARE(results.at(0).replaced, 0);
        QVERIFY(results.at(0).before.isEmpty());
        QVERIFY(nb.vault.history()->list(u"a.md"_s).isEmpty());
    }

    void scanOnThreads()
    {
        Notebook nb;
        for (int i = 0; i < 60; ++i)
            nb.write(u"n%1.md"_s.arg(i, 3, 10, QChar(u'0')), i % 3 == 0 ? "żółw\n" : "nic\n");
        nb.open();
        QThreadPool threads;
        threads.setMaxThreadCount(3);
        WorkerPool pool;
        pool.threads = 3;
        pool.start = [&threads](std::function<void()> job) { threads.start(std::move(job)); };
        ReplaceScan scan(nb.vault, pool, u"zolw"_s, {});
        QCOMPARE(scan.total(), 60);
        QTRY_VERIFY_WITH_TIMEOUT((scan.step(10), scan.isDone()), 20000);
        const ReplacePlan plan = scan.plan();
        QCOMPARE(plan.files.size(), 20);
        QCOMPARE(plan.totalHits, 20);
        QVERIFY(std::is_sorted(plan.files.cbegin(), plan.files.cend(),
                               [](const ReplaceFile &a, const ReplaceFile &b) { return a.path < b.path; }));
        QCOMPARE(scan.takeMerged().size(), 20);
        QVERIFY(scan.takeMerged().isEmpty());
        threads.waitForDone();
    }

    void scanDroppedWhileWorking()
    {
        Notebook nb;
        for (int i = 0; i < 40; ++i)
            nb.write(u"n%1.md"_s.arg(i), "żółw\n");
        nb.open();
        QThreadPool threads;
        threads.setMaxThreadCount(2);
        WorkerPool pool;
        pool.threads = 2;
        pool.start = [&threads](std::function<void()> job) { threads.start(std::move(job)); };
        {
            ReplaceScan scan(nb.vault, pool, u"zolw"_s, {});
        }
        threads.waitForDone();
    }

    void scanInSlices()
    {
        Notebook nb;
        for (int i = 0; i < 10; ++i)
            nb.write(u"n%1.md"_s.arg(i), "żółw\n");
        nb.open();
        ReplaceScan scan(nb.vault, {}, u"zolw"_s, {});
        ReplaceScan::Step step = ReplaceScan::Step::More;
        int steps = 0;
        while (step != ReplaceScan::Step::Done && steps++ < 100)
            step = scan.step(0); // one note per slice with no budget
        QCOMPARE(steps, 10);
        QCOMPARE(scan.done(), 10);
        QCOMPARE(scan.plan().files.size(), 10);
    }

    void emptyQueryFindsNothing()
    {
        Notebook nb;
        nb.write(u"a.md"_s, "kot\n");
        nb.open();
        const ReplacePlan plan = planReplace(nb.vault, QString(), {});
        QCOMPARE(plan.totalHits, 0);
        QVERIFY(plan.files.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestNotebookReplace)
#include "tst_notebook_replace.moc"
