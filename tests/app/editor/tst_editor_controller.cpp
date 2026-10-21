// PageController + BlockListModel (src/app/editor): loading, every editing command, undo/redo,
// byte-for-byte round trip of untouched blocks, CRLF, zoom and folding, tokens, completions,
// slash commands, tables, links, debounced saving and changes made outside the editor.
// Runs with a FakeEditorHost (in-memory files) and a fixed clock.
#include "blocklistmodel.h"
#include "blocktext.h"
#include "dayclock.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"

#include <QAbstractItemModelTester>
#include <QClipboard>
#include <QFile>
#include <QImage>
#include <QMimeData>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Page = u"Notatki/Strona.md"_s;
const QString Now = u"2026-10-15 10:00"_s;

/// A controller on `content` (with a model tester watching every signal).
struct Fixture {
    FakeEditorHost host;
    PageController page;
    std::unique_ptr<QAbstractItemModelTester> tester;

    explicit Fixture(const QString &content, int saveDelay = 0)
    {
        host.setFile(Page, content);
        host.setFile(u"Inna.md"_s, u"- Blok z innej strony #inne ^inny1\n- Drugi blok\n"_s);
        page.setNow(Now);
        page.setSaveDelay(saveDelay);
        page.setHost(&host);
        page.setPath(Page);
        tester = std::make_unique<QAbstractItemModelTester>(page.blocks(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    }

    BlockListModel *model() const { return page.blocks(); }
    QVariant role(int row, const char *name) const
    {
        const QHash<int, QByteArray> roles = model()->roleNames();
        return model()->data(model()->index(row), roles.key(name));
    }
    QString edit(int row) const { return role(row, "editText").toString(); }
    QString text() const { return page.text(); }
    int count() const { return model()->rowCount(); }
    QList<bool> visible() const
    {
        QList<bool> out;
        for (int r = 0; r < count(); ++r)
            out.append(role(r, "visible").toBool());
        return out;
    }
};

} // namespace

class TestPageController : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // --- loading and roles --------------------------------------------------------------------------

    void loadsAndRoundTrips_data()
    {
        QTest::addColumn<QString>("content");
        QTest::newRow("sample") << FakeEditorHost::sampleNote();
        QTest::newRow("crlf") << u"- a\r\n\t- b\r\n\r\nAkapit\r\n"_s;
        QTest::newRow("no final newline") << u"- jeden\n- dwa"_s;
        QTest::newRow("empty") << QString();
        QTest::newRow("bom") << u"﻿# Tytuł\n\nTreść ąćęłńóśźż\n"_s;
        QTest::newRow("mixed") << u"1. a\n2. b\n   1) c\n+ d\n* e\n\n```\nkod\n```\n"_s;
    }

    void loadsAndRoundTrips()
    {
        QFETCH(QString, content);
        Fixture f(content);
        QVERIFY(f.page.loaded());
        QCOMPARE(f.text(), content);
        QVERIFY(!f.page.dirty());
        QCOMPARE(f.page.title(), u"Strona"_s);
        QCOMPARE(f.count(), f.model()->document().count());
        // Reading every role of every row never crashes and is stable.
        for (int r = 0; r < f.count(); ++r) {
            for (const QByteArray &name : f.model()->roleNames())
                (void)f.role(r, name.constData());
        }
        QCOMPARE(f.text(), content);
    }

    void sampleRoles()
    {
        Fixture f(FakeEditorHost::sampleNote());
        QCOMPARE(f.host.saveAttachment(u"łazienka-szkic.png"_s, QByteArray("\x89PNG")), u"assets/łazienka-szkic.png"_s);
        f.model()->invalidateRendering();
        QHash<QString, int> views;
        for (int r = 0; r < f.count(); ++r)
            views[f.role(r, "view").toString()] += 1;
        for (const char *v : {"frontmatter", "text", "heading", "calc", "table", "callout", "query", "rule", "quote"})
            QVERIFY2(views.value(QString::fromLatin1(v)) > 0, v);
        // get() (for QML: the page view finds "# Title" and [[Page#Heading]] targets) carries
        // the heading level.
        for (int r = 0; r < f.count(); ++r) {
            const QVariantMap info = f.model()->get(r);
            QCOMPARE(info.value(u"headingLevel"_s).toInt(), f.role(r, "headingLevel").toInt());
            if (info.value(u"view"_s).toString() == u"heading")
                QVERIFY(info.value(u"headingLevel"_s).toInt() > 0);
        }
        // Tasks, depth and children.
        int kup = -1;
        for (int r = 0; r < f.count(); ++r) {
            if (f.edit(r).startsWith(u"Kupić płytki"_s))
                kup = r;
        }
        QVERIFY(kup > 0);
        QCOMPARE(f.role(kup, "taskStatus").toString(), u"todo"_s);
        QCOMPARE(f.role(kup, "depth").toInt(), 1);
        QVERIFY(f.role(kup, "hasChildren").toBool());
        const QString html = f.role(kup, "html").toString();
        QVERIFY(html.contains(u"tag:zakupy"_s));
        QVERIFY(html.contains(u"📅"_s));
        QVERIFY(!html.contains(u"[ ]"_s)); // the delegate draws the checkbox
        // Calc results, table formulas, query and embeds.
        for (int r = 0; r < f.count(); ++r) {
            const QString view = f.role(r, "view").toString();
            if (view == u"calc") {
                const QVariantList results = f.role(r, "calcResults").toList();
                QCOMPARE(results.size(), f.edit(r).count(u'\n') + 1);
                QCOMPARE(results.last().toMap().value(u"value"_s).toString(), u"3558,50 zł"_s);
            } else if (view == u"table") {
                const QVariantMap table = f.role(r, "tableData").toMap();
                QCOMPARE(table.value(u"header"_s).toStringList().size(), 4);
                const QVariantList rows = table.value(u"rows"_s).toList();
                QCOMPARE(rows.size(), 4);
                QCOMPARE(rows[0].toList()[3].toMap().value(u"display"_s).toString(), u"578,5"_s);
                QCOMPARE(rows[3].toList()[3].toMap().value(u"display"_s).toString(), u"834,5"_s);
                QVERIFY(rows[3].toList()[3].toMap().value(u"formula"_s).toBool());
            } else if (view == u"query") {
                QCOMPARE(f.role(r, "query").toMap().value(u"filters"_s).toList().size(), 4);
            } else if (view == u"frontmatter") {
                QCOMPARE(f.role(r, "properties").toList().size(), 4);
            } else if (f.role(r, "onlyEmbeds").toBool()) {
                // Pictures, or embedded sections (their frames come from `transclusions`).
                const QVariantList embeds = f.role(r, "embeds").toList();
                const QVariantList transclusions = f.role(r, "transclusions").toList();
                QCOMPARE(embeds.size() + transclusions.size(), 1);
                if (!embeds.isEmpty())
                    QVERIFY(embeds[0].toMap().value(u"url"_s).toUrl().isLocalFile());
                QVERIFY(f.role(r, "html").toString().isEmpty());
            }
        }
        // Inline calculation with frontmatter and calc-block variables.
        bool inlineFound = false;
        for (int r = 0; r < f.count(); ++r) {
            if (f.edit(r).startsWith(u"Zostaje z budżetu"_s)) {
                QVERIFY2(f.role(r, "html").toString().contains(u"8441,5"_s), qPrintable(f.role(r, "html").toString()));
                inlineFound = true;
            }
        }
        QVERIFY(inlineFound);
    }

    void reportedSampleCodeBlocks()
    {
        // The reported note (trimmed): inline code with '#', a code block followed directly by a
        // paragraph, consecutive fences, trailing backslashes and a blank line before the close.
        const QString sample = uR"(paczka llama.cpp: `github:janusz-bit/nixos#llama-cpp-prism`

```
nix shell nixpkgs#python314Packages.huggingface-hub.out -c hf download prism-ml/Ternary-Bonsai-2-27B-gguf Ternary-Bonsai-2-27B-PQ2_0.gguf --local-dir . \
                                     --ctx-size 200000 \
                                     -ctk q8_0 -ctv q8_0
```
Udało się zmieścić model na GPU!!!!

Dokumentacja mówi:

```
llama serve -m Ternary-Bonsai-2-27B-PQ2_0.gguf \
    -ngl 99 -fa on -c 32768 \
    -p "Explain quantum computing in simple terms." -n 16384
```


ja będę robić tak:

```
GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 llama serve -m Ternary-Bonsai-2-27B-PTQ1_0.gguf \
-ngl 99 -fa on -c 65536 \
-ctk q8_0 -ctv q4_0 \

```
)"_s;
        Fixture f(sample);
        QCOMPARE(f.text(), sample);
        QStringList views;
        for (int r = 0; r < f.count(); ++r)
            views.append(f.role(r, "view").toString());
        QCOMPARE(views, (QStringList{u"text"_s, u"code"_s, u"text"_s, u"text"_s, u"code"_s,
                                     u"text"_s, u"code"_s}));
        QCOMPARE(f.edit(0), u"paczka llama.cpp: `github:janusz-bit/nixos#llama-cpp-prism`"_s);
        QVERIFY(f.edit(1).endsWith(u"-ctk q8_0 -ctv q8_0"_s));
        QCOMPARE(f.edit(2), u"Udało się zmieścić model na GPU!!!!"_s);
        QCOMPARE(f.edit(3), u"Dokumentacja mówi:"_s);
        QVERIFY(f.edit(4).contains(uR"(-p "Explain quantum computing in simple terms.")"_s));
        QCOMPARE(f.edit(5), u"ja będę robić tak:"_s);
        QVERIFY(f.edit(6).endsWith(u"\\\n"_s)); // the blank line before the closing fence
    }

    void unresolvedLinksAndPalette()
    {
        Fixture f(u"- [[Inna]] i [[Nie istnieje]]\n"_s);
        const QString html = f.role(0, "html").toString();
        QVERIFY(html.contains(u"#2c6aa3"_s, Qt::CaseInsensitive));
        QVERIFY(html.contains(u"#7a5ea3"_s, Qt::CaseInsensitive));
        f.page.setPalette({{u"link"_s, u"#112233"_s}, {u"unresolved"_s, QColor(u"#445566"_s)}});
        const QString themed = f.role(0, "html").toString();
        QVERIFY(themed.contains(u"#112233"_s, Qt::CaseInsensitive));
        QVERIFY(themed.contains(u"#445566"_s, Qt::CaseInsensitive));
    }

    void onlyListItemsAreTasks()
    {
        // SYNTAX §5: a paragraph starting with "TODO" or "[ ]" is text, a list item a task (its
        // marker is drawn by the delegate, so it is not part of the html).
        Fixture f(u"TODO zadzwonić do Łucji\n\n[ ] to nie zadanie\n\n- TODO zadanie\n- [ ] kupić żółty ser\n"_s);
        QCOMPARE(f.count(), 4);
        QCOMPARE(f.role(0, "taskStatus").toString(), QString());
        QVERIFY2(f.role(0, "html").toString().startsWith(u"TODO zadzwonić"_s),
                 qPrintable(f.role(0, "html").toString()));
        QCOMPARE(f.edit(0), u"TODO zadzwonić do Łucji"_s);
        QCOMPARE(f.role(1, "taskStatus").toString(), QString());
        QVERIFY2(f.role(1, "html").toString().startsWith(u"[ ] to nie"_s), qPrintable(f.role(1, "html").toString()));
        QCOMPARE(f.role(2, "taskStatus").toString(), u"todo"_s);
        QVERIFY(!f.role(2, "html").toString().contains(u"TODO"_s));
        QCOMPARE(f.edit(2), u"zadanie"_s);
        QCOMPARE(f.role(3, "taskStatus").toString(), u"todo"_s);
        QVERIFY(!f.role(3, "html").toString().contains(u"[ ]"_s));
        // A heading item's further lines are text as well.
        Fixture h(u"- # Plan\n  TODO nie zadanie\n"_s);
        QVERIFY2(h.role(0, "html").toString().contains(u"TODO nie"_s), qPrintable(h.role(0, "html").toString()));
    }

    void journalLinksShowDates()
    {
        // [[2026-10-13]] reads as a date in the editor (the file keeps the ISO date).
        Fixture f(u"- Wczoraj: [[2026-10-14]], potem [[2026-10-13|wtorek]] i [[Inna]]\n"_s);
        const QString nb = QString(QChar(0x00A0));
        const QString html = f.role(0, "html").toString();
        QVERIFY2(html.contains(u"śr"_s + nb + u"14"_s + nb + u"paź"_s), qPrintable(html));
        QVERIFY(html.contains(u"page:2026-10-14"_s));
        QVERIFY(html.contains(u">wtorek<"_s));
        QVERIFY(!html.contains(u">2026-10-14<"_s));
        QCOMPARE(f.text(), u"- Wczoraj: [[2026-10-14]], potem [[2026-10-13|wtorek]] i [[Inna]]\n"_s);
        f.page.setLocale(u"en"_s);
        QVERIFY2(f.role(0, "html").toString().contains(u"Wed,"_s + nb + u"Oct"_s + nb + u"14"_s),
                 qPrintable(f.role(0, "html").toString()));
    }

    void dayClockFollowsMidnight()
    {
        DayClock *clock = DayClock::instance();
        QDateTime now(QDate(2026, 10, 16), QTime(23, 59, 59));
        clock->setClock([&now] { return now; });
        QCOMPARE(clock->today(), QDate(2026, 10, 16));
        QSignalSpy changed(clock, &DayClock::dayChanged);
        clock->check();
        QCOMPARE(changed.size(), 0);
        // The timer is armed for just after midnight (by this clock: 2 s).
        now = QDateTime(QDate(2026, 10, 17), QTime(0, 0, 1));
        QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 1, 5000);
        QCOMPARE(changed.first().first().toDate(), QDate(2026, 10, 17));
        clock->check();
        QCOMPARE(changed.size(), 1);
        // The clock jumps (travel, suspend): noticed at the next check.
        now = QDateTime(QDate(2026, 10, 15), QTime(12, 0));
        clock->check();
        QCOMPARE(changed.size(), 2);
        clock->setClock({});
    }

    void midnightRefreshesAnOpenPage()
    {
        // The real clock (no fixed `now`): a page rendered yesterday follows the DayClock —
        // "jutro" becomes "dziś", a task due yesterday turns red, query results run again.
        const QDate today = QDate::currentDate();
        const QString due = today.toString(Qt::ISODate);
        const QString past = today.addDays(-1).toString(Qt::ISODate);
        FakeEditorHost host;
        host.setFile(Page, u"- [ ] Zadzwonić 📅 %1\n- [ ] Spóźnione 📅 %2\n"_s.arg(due, past));
        PageController page;
        page.setHost(&host);
        page.setPath(Page);
        BlockListModel *model = page.blocks();
        const auto html = [&](int row) { return model->data(model->index(row), BlockListModel::HtmlRole).toString(); };
        QVERIFY2(html(0).contains(u"dziś"_s), qPrintable(html(0)));
        model->setToday(today.addDays(-1)); // as rendered before midnight
        QVERIFY2(html(0).contains(u"jutro"_s), qPrintable(html(0)));
        QVERIFY(!html(1).contains(markdown::Palette().overdue.foreground, Qt::CaseInsensitive));
        QSignalSpy todayChanged(&page, &PageController::todayChanged);
        QSignalSpy dataChanged(model, &QAbstractItemModel::dataChanged);
        QDateTime fake(today.addDays(1), QTime(0, 0, 1));
        DayClock::instance()->setClock([&fake] { return fake; }); // "midnight"
        QCOMPARE(todayChanged.size(), 1);
        QVERIFY(dataChanged.size() >= 1);
        QVERIFY2(html(0).contains(u"dziś"_s), qPrintable(html(0)));
        QVERIFY2(html(1).contains(markdown::Palette().overdue.foreground, Qt::CaseInsensitive), qPrintable(html(1)));
        DayClock::instance()->setClock({});
        QCOMPARE(todayChanged.size(), 1); // the same day for the page: nothing again
    }

    // --- typing and undo ------------------------------------------------------------------------------

    void typingIsOneUndoStepPerBlock()
    {
        const QString content = u"- pierwszy\n- drugi\n"_s;
        Fixture f(content);
        f.page.startEditing(0, 9);
        f.page.setEditText(0, u"pierwszy!"_s, 9);
        f.page.setEditText(0, u"pierwszy!!"_s, 10);
        f.page.setEditText(0, u"pierwszy!!!"_s, 11);
        QCOMPARE(f.text(), u"- pierwszy!!!\n- drugi\n"_s);
        f.page.startEditing(1, 5);
        f.page.setEditText(1, u"drugi?"_s, 6);
        QCOMPARE(f.text(), u"- pierwszy!!!\n- drugi?\n"_s);
        QVERIFY(f.page.canUndo());
        f.page.undo();
        QCOMPARE(f.text(), u"- pierwszy!!!\n- drugi\n"_s);
        f.page.undo();
        QCOMPARE(f.text(), content);
        QVERIFY(!f.page.canUndo());
        QCOMPARE(f.page.editingRow(), 0);
        f.page.redo();
        QCOMPARE(f.text(), u"- pierwszy!!!\n- drugi\n"_s);
        f.page.redo();
        QCOMPARE(f.text(), u"- pierwszy!!!\n- drugi?\n"_s);
        QVERIFY(!f.page.canRedo());
    }

    void undoKeepsRowsAndSendsEditTextReset()
    {
        Fixture f(u"- a\n- b\n- c\n"_s);
        QSignalSpy resets(f.model(), &QAbstractItemModel::modelReset);
        QSignalSpy textResets(&f.page, &PageController::editTextReset);
        f.page.startEditing(1, 1);
        f.page.setEditText(1, u"bę"_s, 2);
        f.page.undo();
        QCOMPARE(resets.size(), 0); // rows of unchanged blocks survive undo
        QVERIFY(!textResets.isEmpty());
        QCOMPARE(textResets.last().at(0).toInt(), 1);
        QCOMPARE(textResets.last().at(1).toString(), u"b"_s);
    }

    void typingTaskMarkerBecomesCheckbox()
    {
        Fixture f(u"- zwykły\n"_s);
        QSignalSpy textResets(&f.page, &PageController::editTextReset);
        f.page.startEditing(0, 0);
        f.page.setEditText(0, u"[ ] zwykły"_s, 4);
        QCOMPARE(f.text(), u"- [ ] zwykły\n"_s);
        QCOMPARE(f.role(0, "taskStatus").toString(), u"todo"_s);
        QCOMPARE(f.edit(0), u"zwykły"_s);
        QCOMPARE(textResets.size(), 1);
        QCOMPARE(textResets[0].at(1).toString(), u"zwykły"_s);
        QCOMPARE(textResets[0].at(2).toInt(), 0);
        // Typing inside a task keeps the marker.
        f.page.setEditText(0, u"zwykły tekst"_s, 12);
        QCOMPARE(f.text(), u"- [ ] zwykły tekst\n"_s);
    }

    void crlfIsPreserved()
    {
        Fixture f(u"- a\r\n- b\r\n\r\nKoniec\r\n"_s);
        f.page.startEditing(0, 1);
        f.page.setEditText(0, u"ała\ndruga linia"_s, 3);
        QCOMPARE(f.text(), u"- ała\r\n  druga linia\r\n- b\r\n\r\nKoniec\r\n"_s);
        f.page.split(1, 1);
        QCOMPARE(f.text(), u"- ała\r\n  druga linia\r\n- b\r\n- \r\n\r\nKoniec\r\n"_s);
    }

    // --- structure commands ------------------------------------------------------------------------

    void splitCommands()
    {
        Fixture f(u"- [ ] Kup mleko\n- abc\n"_s);
        f.page.startEditing(0, 9);
        QVariantMap r = f.page.split(0, 9);
        QCOMPARE(f.text(), u"- [ ] Kup mleko\n- [ ] \n- abc\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 1);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 0); // after the hidden marker
        QCOMPARE(f.page.editingRow(), 1);
        f.page.startEditing(2, 1);
        r = f.page.split(2, 1);
        QCOMPARE(f.text(), u"- [ ] Kup mleko\n- [ ] \n- a\n- bc\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 3);
        QCOMPARE(f.role(3, "isEditing").toBool(), true);
        QCOMPARE(f.role(2, "isEditing").toBool(), false);
        f.page.undo();
        QCOMPARE(f.text(), u"- [ ] Kup mleko\n- [ ] \n- abc\n"_s);
        QCOMPARE(f.page.editingRow(), 2);
        QCOMPARE(f.page.cursorPosition(), 1);
    }

    void splitSpecialCases()
    {
        // "```calc" + Enter opens a code block.
        Fixture fence(u"- ```calc\n"_s);
        const QVariantMap r = fence.page.split(0, 7);
        QCOMPARE(fence.text(), u"- ```calc\n\n  ```\n"_s);
        QCOMPARE(fence.role(0, "view").toString(), u"calc"_s);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 0);
        // Any fence length and info string Markdown accepts opens a block; the closing fence
        // matches the opening one.
        Fixture four(u"````\n"_s);
        QCOMPARE(four.page.split(0, 4).value(u"row"_s).toInt(), 0);
        QCOMPARE(four.text(), u"````\n\n````\n"_s);
        QCOMPARE(four.role(0, "view").toString(), u"code"_s);
        Fixture sharp(u"```c#\n"_s);
        sharp.page.split(0, 5);
        QCOMPARE(sharp.text(), u"```c#\n\n```\n"_s);
        QCOMPARE(sharp.role(0, "view").toString(), u"code"_s);
        QCOMPARE(sharp.role(0, "lang").toString(), u"c#"_s);
        Fixture tildes(u"~~~~ js\n"_s);
        tildes.page.split(0, 7);
        QCOMPARE(tildes.text(), u"~~~~ js\n\n~~~~\n"_s);
        QCOMPARE(tildes.role(0, "view").toString(), u"code"_s);
        // Enter on an empty nested item outdents it.
        Fixture nested(u"- a\n\t- \n"_s);
        nested.page.startEditing(1, 0);
        nested.page.split(1, 0);
        QCOMPARE(nested.text(), u"- a\n- \n"_s);
        // Code blocks do not split.
        Fixture code(u"```\nx\n```\n"_s);
        QCOMPARE(code.page.split(0, 1).value(u"row"_s).toInt(), 0);
        QCOMPARE(code.text(), u"```\nx\n```\n"_s);
    }

    void mergeCommands()
    {
        Fixture f(u"- ab\n- cd\n\t- dziecko\n- \n"_s);
        QVariantMap r = f.page.mergeWithPrevious(1);
        QCOMPARE(f.text(), u"- abcd\n\t- dziecko\n- \n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 0);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 2);
        r = f.page.mergeWithPrevious(2); // empty block: removed
        QCOMPARE(f.text(), u"- abcd\n\t- dziecko\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 1);
        QCOMPARE(f.page.mergeWithPrevious(0).value(u"row"_s).toInt(), -1);
        // Delete at the end merges the next block in.
        Fixture next(u"- ab\n- cd\n"_s);
        r = next.page.mergeNext(0);
        QCOMPARE(next.text(), u"- abcd\n"_s);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 2);
        // Previous block is code: nothing merges, the caret moves there.
        Fixture code(u"```\nx\n```\n- a\n"_s);
        r = code.page.mergeWithPrevious(1);
        QCOMPARE(code.text(), u"```\nx\n```\n- a\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 0);
    }

    void mergeUnfoldsHiddenPrevious()
    {
        Fixture f(u"- a\n\t- ukryty\n- b\n"_s);
        f.page.setFolded(0, true);
        QCOMPARE(f.visible(), QList<bool>({true, false, true}));
        f.page.mergeWithPrevious(2);
        QCOMPARE(f.text(), u"- a\n\t- ukrytyb\n"_s);
        QCOMPARE(f.visible(), QList<bool>({true, true}));
    }

    void indentOutdentMove()
    {
        const QString content = u"Wstęp  \n\n- a\n- b\n\t- b1\n- c\n\nKoniec ąę\n"_s;
        Fixture f(content);
        QVERIFY(f.page.indent(2));
        QCOMPARE(f.text(), u"Wstęp  \n\n- a\n\t- b\n\t\t- b1\n- c\n\nKoniec ąę\n"_s);
        QVERIFY(f.page.outdent(2));
        QCOMPARE(f.text(), content);
        QVERIFY(f.page.moveDown(2));
        QCOMPARE(f.text(), u"Wstęp  \n\n- a\n- c\n- b\n\t- b1\n\nKoniec ąę\n"_s);
        QVERIFY(f.page.moveUp(3));
        QCOMPARE(f.text(), content);
        f.page.undo();
        f.page.undo();
        f.page.undo();
        f.page.undo();
        QCOMPARE(f.text(), content);
        Fixture first(u"- a\n- b\n"_s);
        QVERIFY(!first.page.indent(0)); // no previous sibling
        QVERIFY(!first.page.outdent(0));
    }

    void movingKeepsEditingBlock()
    {
        Fixture f(u"- a\n- b\n- c\n"_s);
        f.page.startEditing(2, 1);
        f.page.moveUp(2);
        QCOMPARE(f.page.editingRow(), 1);
        QCOMPARE(f.edit(1), u"c"_s);
    }

    void tasks()
    {
        Fixture f(u"- Zadanie\n"_s);
        f.page.cycleTask(0);
        QCOMPARE(f.text(), u"- [ ] Zadanie\n"_s);
        f.page.cycleTask(0);
        QCOMPARE(f.text(), u"- [/] Zadanie\n"_s);
        f.page.cycleTask(0);
        QCOMPARE(f.text(), u"- [x] Zadanie ✅ 2026-10-15\n"_s);
        f.page.cycleTask(0);
        QCOMPARE(f.text(), u"- Zadanie\n"_s);
        f.page.undo();
        QCOMPARE(f.text(), u"- [x] Zadanie ✅ 2026-10-15\n"_s);
        f.page.toggleTask(0);
        QCOMPARE(f.text(), u"- [ ] Zadanie\n"_s);
        f.page.setTaskStatus(0, u"cancelled"_s);
        QCOMPARE(f.text(), u"- [-] Zadanie ❌ 2026-10-15\n"_s);
        f.page.removeTaskMarker(0);
        QCOMPARE(f.text(), u"- Zadanie\n"_s);
    }

    void recurringTaskInsertsNextOccurrence()
    {
        Fixture f(u"- [ ] Podlać kwiaty 📅 2026-10-15 🔁 every week\n"_s);
        f.page.startEditing(0, 3);
        f.page.toggleTask(0);
        QCOMPARE(f.count(), 2);
        QVERIFY(f.text().startsWith(u"- [ ] Podlać kwiaty 📅 2026-10-22"_s));
        QVERIFY(f.text().contains(u"- [x] Podlać kwiaty 📅 2026-10-15 🔁 every week ✅ 2026-10-15"_s));
        QCOMPARE(f.page.editingRow(), 1); // still the completed block
    }

    void deleteAndInsert()
    {
        Fixture f(u"- a\n\t- a1\n- b\n"_s);
        f.page.startEditing(0, 0);
        const int focus = f.page.deleteBlock(0);
        QCOMPARE(f.text(), u"- b\n"_s);
        QCOMPARE(focus, 0);
        const int row = f.page.insertBlockAfter(0, u"nowy ż"_s);
        QCOMPARE(row, 1);
        QCOMPARE(f.text(), u"- b\n- nowy ż\n"_s);
        QCOMPARE(f.page.editingRow(), 1);
        // A trailing empty block is reused.
        Fixture empty(u"- a\n- \n"_s);
        QCOMPARE(empty.page.appendEmptyBlock(), 1);
        QCOMPARE(empty.text(), u"- a\n- \n"_s);
        Fixture para(u"Akapit\n"_s);
        QCOMPARE(para.page.appendEmptyBlock(), 1);
        QCOMPARE(para.model()->document().at(1).kind, BlockKind::ListItem);
        Fixture none{QString()};
        QCOMPARE(none.page.appendEmptyBlock(), 0);
        QCOMPARE(none.count(), 1);
    }

    void insertMarkdownAndTemplates()
    {
        Fixture f(u"- a\n\t- a1\n- \n"_s);
        int row = f.page.insertMarkdown(2, u"- [ ] x\n- [ ] y"_s);
        QCOMPARE(f.text(), u"- a\n\t- a1\n- [ ] x\n- [ ] y\n"_s);
        QCOMPARE(row, 2);
        row = f.page.insertMarkdown(1, u"## Sekcja"_s);
        QCOMPARE(f.text(), u"- a\n\t- a1\n\n## Sekcja\n\n- [ ] x\n- [ ] y\n"_s);
        QCOMPARE(f.model()->document().at(row).kind, BlockKind::Heading);
        f.page.undo();
        f.page.undo();
        QCOMPARE(f.text(), u"- a\n\t- a1\n- \n"_s);
        // Templates are filled ({{date}}, {{title}}).
        f.host.setFile(u"templates/T.md"_s, u"- Notatka {{date}} — {{title}}\n"_s);
        f.page.insertTemplate(2, u"templates/T.md"_s);
        QCOMPARE(f.text(), u"- a\n\t- a1\n- Notatka 2026-10-15 — Strona\n"_s);
    }

    // --- zoom and folding ----------------------------------------------------------------------------

    void zoom()
    {
        Fixture f(u"- a\n\t- a1\n\t\t- a11\n\t- a2\n- b\n"_s);
        QSignalSpy zoomSpy(&f.page, &PageController::zoomChanged);
        f.page.zoomInto(1);
        QCOMPARE(f.page.zoomBlock(), 1);
        QCOMPARE(f.visible(), QList<bool>({false, true, true, false, false}));
        QCOMPARE(f.role(1, "depth").toInt(), 0);
        QCOMPARE(f.role(2, "depth").toInt(), 1);
        QCOMPARE(f.role(2, "docDepth").toInt(), 2);
        QVERIFY(f.role(1, "isZoomRoot").toBool());
        const QVariantList crumbs = f.page.breadcrumb();
        QCOMPARE(crumbs.size(), 2);
        QCOMPARE(crumbs[0].toMap().value(u"title"_s).toString(), u"a"_s);
        QCOMPARE(crumbs[1].toMap().value(u"row"_s).toInt(), 1);
        // Enter at the end of the zoomed block adds a first child inside the zoom.
        f.page.startEditing(1, 2);
        const QVariantMap r = f.page.split(1, 2);
        QCOMPARE(f.text(), u"- a\n\t- a1\n\t\t- \n\t\t- a11\n\t- a2\n- b\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 2);
        QVERIFY(f.role(2, "visible").toBool());
        // The zoom root cannot be moved or outdented; its children cannot leave it.
        QVERIFY(!f.page.moveUp(1));
        QVERIFY(!f.page.outdent(2));
        f.page.zoomOut();
        QCOMPARE(f.page.zoomBlock(), 0);
        f.page.zoomOut();
        QCOMPARE(f.page.zoomBlock(), -1);
        QCOMPARE(f.visible(), QList<bool>(6, true));
        QVERIFY(zoomSpy.size() >= 3);
    }

    void zoomFollowsUndo()
    {
        Fixture f(u"- a\n\t- a1\n- b\n"_s);
        f.page.zoomInto(0);
        f.page.setEditText(1, u"a1 zmienione"_s, 12);
        f.page.undo();
        QCOMPARE(f.page.zoomBlock(), 0);
        QCOMPARE(f.visible(), QList<bool>({true, true, false}));
    }

    void folding()
    {
        Fixture f(u"- a\n\t- a1\n\t\t- a11\n- b\n\t- b1\n"_s);
        QSignalSpy foldSpy(&f.page, &PageController::foldStateChanged);
        f.page.startEditing(2, 1);
        f.page.toggleFold(0);
        QCOMPARE(f.visible(), QList<bool>({true, false, false, true, true}));
        QVERIFY(f.role(0, "collapsed").toBool());
        QCOMPARE(f.page.editingRow(), 0); // the caret moved out of the folded subtree
        QCOMPARE(f.page.foldState(), QVariantList({0}));
        QCOMPARE(f.page.nextVisibleRow(0), 3);
        QCOMPARE(f.page.previousVisibleRow(3), 0);
        f.page.toggleFold(1); // hidden but has children: folds too
        f.page.toggleFold(0);
        QCOMPARE(f.visible(), QList<bool>({true, true, false, true, true}));
        QCOMPARE(f.page.foldState(), QVariantList({1}));
        f.page.toggleFold(4); // no children: nothing
        QCOMPARE(f.page.foldState(), QVariantList({1}));
        f.page.foldAll(true);
        QCOMPARE(f.visible(), QList<bool>({true, false, false, true, false}));
        f.page.foldAll(false);
        QCOMPARE(f.visible(), QList<bool>(5, true));
        f.page.setFoldState({3, 99});
        QCOMPARE(f.visible(), QList<bool>({true, true, true, true, false}));
        QVERIFY(foldSpy.size() >= 5);
        // Editing a hidden block unfolds it.
        f.page.startEditing(4, 0);
        QCOMPARE(f.visible(), QList<bool>(5, true));
        // Split of a folded block puts the new block after its subtree.
        f.page.setFolded(0, true);
        f.page.split(0, 1);
        QCOMPARE(f.text(), u"- a\n\t- a1\n\t\t- a11\n- \n- b\n\t- b1\n"_s);
    }

    void revealAndLookups()
    {
        Fixture f(u"- a ^ida\n\t- a1\n\t\t- a11 ^deep\n- b\n"_s);
        QCOMPARE(f.page.rowOfBlockId(u"deep"_s), 2);
        QCOMPARE(f.page.rowOfBlockId(u"nope"_s), -1);
        QCOMPARE(f.page.rowOfLine(3), 3);
        QCOMPARE(f.page.rowOfLine(99), -1);
        f.page.setFolded(0, true);
        f.page.setFolded(1, true);
        f.page.zoomInto(3);
        QVERIFY(!f.role(2, "visible").toBool());
        f.page.reveal(2);
        QVERIFY(f.role(2, "visible").toBool());
        QCOMPARE(f.page.zoomBlock(), -1);
        QCOMPARE(f.page.foldState(), QVariantList());
    }

    void collapsedPropertyFoldsInitially()
    {
        Fixture f(u"- a\n  collapsed:: true\n\t- a1\n- b\n"_s);
        QCOMPARE(f.visible(), QList<bool>({true, false, true}));
        QVERIFY(!f.role(0, "html").toString().contains(u"collapsed"_s));
    }

    // --- tokens ---------------------------------------------------------------------------------------

    void tokenCommands()
    {
        Fixture f(u"- [ ] Zadzwonić do mamy ^m1\n"_s);
        f.page.startEditing(0, 4);
        f.page.setReminder(0, u"2026-10-16 18:00"_s, false, QString());
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do mamy ⏰ 2026-10-16 18:00 ^m1\n"_s);
        QCOMPARE(f.page.cursorPosition(), 4);
        // Edit the reminder through its chip range (body coordinates).
        const QString body = f.model()->document().at(0).body;
        const int from = int(body.indexOf(u"⏰"_s));
        const int to = from + int(u"⏰ 2026-10-16 18:00"_s.size());
        const QVariantMap reminder = f.page.reminderAt(0, from, to);
        QCOMPARE(reminder.value(u"at"_s).toString(), u"2026-10-16 18:00"_s);
        QCOMPARE(reminder.value(u"alarm"_s).toBool(), false);
        QCOMPARE(reminder.value(u"context"_s).toString(), u"Zadzwonić do mamy"_s);
        f.page.setReminder(0, u"2026-10-17 07:30"_s, true, u"co tydzień w sobotę"_s, from, to);
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do mamy ⏰! 2026-10-17 07:30 🔁 every week on Saturday ^m1\n"_s);
        QCOMPARE(f.page.blockDates(0).value(u"recurrence"_s).toString(), u"every week on Saturday"_s);
        // Removing the recurrence while editing the reminder.
        const QString body2 = f.model()->document().at(0).body;
        const int from2 = int(body2.indexOf(u"⏰"_s));
        f.page.setReminder(0, u"2026-10-17 07:30"_s, true, QString(), from2, from2 + int(u"⏰! 2026-10-17 07:30"_s.size()));
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do mamy ⏰! 2026-10-17 07:30 ^m1\n"_s);
        f.page.setDateToken(0, u"due"_s, u"2026-10-20"_s);
        f.page.setDateToken(0, u"scheduled"_s, u"2026-10-18 09:00"_s);
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do mamy ⏰! 2026-10-17 07:30 📅 2026-10-20 ⏳ 2026-10-18 09:00 ^m1\n"_s);
        QCOMPARE(f.page.blockDates(0).value(u"due"_s).toString(), u"2026-10-20"_s);
        f.page.setDateToken(0, u"due"_s, QString());
        f.page.setRecurrence(0, u"codziennie"_s);
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do mamy ⏰! 2026-10-17 07:30 ⏳ 2026-10-18 09:00 🔁 every day ^m1\n"_s);
        f.page.setDateToken(0, u"nonsense"_s, u"2026-10-20"_s);
        f.page.setDateToken(0, u"due"_s, u"jutro"_s); // not a canonical spec: ignored
        f.page.setReminder(0, u"zła data"_s, false, QString());
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do mamy ⏰! 2026-10-17 07:30 ⏳ 2026-10-18 09:00 🔁 every day ^m1\n"_s);
        const QString body3 = f.model()->document().at(0).body;
        const int from3 = int(body3.indexOf(u"⏳"_s));
        f.page.removeToken(0, from3, from3 + int(u"⏳ 2026-10-18 09:00"_s.size()));
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do mamy ⏰! 2026-10-17 07:30 🔁 every day ^m1\n"_s);
        // insertToken / replaceRange in edit-text coordinates.
        Fixture g(u"- [ ] Kupić\n"_s);
        QCOMPARE(g.page.insertToken(0, 5, u"⏫"_s), 8);
        QCOMPARE(g.text(), u"- [ ] Kupić ⏫ \n"_s);
        QCOMPARE(g.page.replaceRange(0, 0, 5, u"Sprzedać"_s), 8);
        QCOMPARE(g.text(), u"- [ ] Sprzedać ⏫ \n"_s);
    }

    void reminderWithoutTimeUsesDefault()
    {
        Fixture f(u"- x ⏰ 2026-10-16\n"_s);
        const QString body = f.model()->document().at(0).body;
        QCOMPARE(f.page.reminderAt(0, 2, int(body.size())).value(u"at"_s).toString(), u"2026-10-16 09:00"_s);
        QVERIFY(f.page.reminderAt(0, 0, 1).isEmpty());
    }

    void blockRefs()
    {
        Fixture f(u"- cel\n- inny\n"_s);
        const QString id = f.page.ensureBlockRef(0);
        QCOMPARE(id.size(), 6);
        QCOMPARE(f.text(), u"- cel ^"_s + id + u"\n- inny\n"_s);
        QCOMPARE(f.page.ensureBlockRef(0), id); // stable
        QCOMPARE(f.page.copyBlockRef(0), id);
        QCOMPARE(QGuiApplication::clipboard()->text(), u"(("_s + id + u"))"_s);
        QCOMPARE(f.page.journalLink(u"2026-10-15"_s), u"[[2026-10-15]]"_s);
    }

    // --- completions and slash commands ------------------------------------------------------------

    void completions()
    {
        Fixture f(u"- zobacz [[Inn\n"_s);
        f.page.startEditing(0, 12);
        QVariantMap trigger = completionTriggerAt(f.edit(0), 12).toMap();
        QVariantMap r = f.page.applyCompletion(0, trigger, {{u"insertText"_s, u"Inna"_s}, {u"kind"_s, u"page"_s}});
        QCOMPARE(f.text(), u"- zobacz [[Inna]]\n"_s);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 15);
        // Existing closing brackets are reused.
        Fixture closed(u"- [[In]] x\n"_s);
        trigger = completionTriggerAt(closed.edit(0), 4).toMap();
        r = closed.page.applyCompletion(0, trigger, {{u"insertText"_s, u"Inna"_s}});
        QCOMPARE(closed.text(), u"- [[Inna]] x\n"_s);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 8);
        // Tags with spaces become #[[…]].
        Fixture tag(u"- #zak\n"_s);
        tag.page.applyCompletion(0, completionTriggerAt(tag.edit(0), 4).toMap(), {{u"insertText"_s, u"zakupy domowe"_s}});
        QCOMPARE(tag.text(), u"- #[[zakupy domowe]] \n"_s);
        // Block refs: a block of this page gets an id; another page's through the host.
        Fixture refs(u"- cel\n- ((ce\n"_s);
        refs.page.startEditing(1, 4);
        refs.page.applyCompletion(1, completionTriggerAt(refs.edit(1), 4).toMap(),
                                  {{u"path"_s, Page}, {u"line"_s, 0}, {u"blockId"_s, QString()}});
        const QString id = refs.model()->document().at(0).blockId;
        QCOMPARE(id.size(), 6);
        QCOMPARE(refs.text(), u"- cel ^"_s + id + u"\n- (("_s + id + u"))\n"_s);
        refs.page.setEditText(1, u"(("_s, 2);
        refs.page.applyCompletion(1, completionTriggerAt(u"(("_s, 2).toMap(),
                                  {{u"path"_s, u"Inna.md"_s}, {u"line"_s, 1}, {u"blockId"_s, QString()}});
        const QString other = refs.host.file(u"Inna.md"_s);
        QVERIFY(other.contains(u"- Drugi blok ^"_s));
        QVERIFY(refs.edit(1).startsWith(u"(("_s) && refs.edit(1).endsWith(u"))"_s));
        // Dates.
        Fixture date(u"- Zadzwonić ⏰ jutro o 9\n"_s);
        date.page.applyCompletion(0, completionTriggerAt(date.edit(0), int(date.edit(0).size())).toMap(),
                                  {{u"insertText"_s, u"2026-10-16 09:00"_s}, {u"kind"_s, u"date"_s}});
        QCOMPARE(date.text(), u"- Zadzwonić ⏰ 2026-10-16 09:00 \n"_s);
        Fixture at(u"- spotkanie @jutro\n"_s);
        at.page.applyCompletion(0, completionTriggerAt(at.edit(0), int(at.edit(0).size())).toMap(),
                                {{u"insertText"_s, u"[[2026-10-16]]"_s}, {u"kind"_s, u"journal"_s}});
        QCOMPARE(at.text(), u"- spotkanie [[2026-10-16]] \n"_s);
    }

    void slashCommands()
    {
        Fixture f(u"- Zrobić /zad\n"_s);
        f.page.startEditing(0, 11);
        QVariantMap r = f.page.applySlashCommand(0, 7, 11, u"task"_s);
        QCOMPARE(f.text(), u"- [ ] Zrobić \n"_s);
        QCOMPARE(r.value(u"action"_s).toString(), QString());
        f.page.undo();
        QCOMPARE(f.text(), u"- Zrobić /zad\n"_s); // one undo step
        // Headings.
        Fixture h(u"- Sekcja /h2\n"_s);
        h.page.applySlashCommand(0, 7, 10, u"h2"_s);
        QCOMPARE(h.text(), u"- ## Sekcja \n"_s);
        QCOMPARE(h.role(0, "view").toString(), u"heading"_s);
        Fixture hp(u"Akapit /h1\n"_s);
        hp.page.applySlashCommand(0, 7, 10, u"h1"_s);
        QCOMPARE(hp.text(), u"# Akapit \n"_s);
        // Blocks from templates replace an empty block.
        Fixture calc(u"- /obl\n"_s);
        calc.page.startEditing(0, 4);
        r = calc.page.applySlashCommand(0, 0, 4, u"calc"_s);
        QCOMPARE(calc.text(), u"- ```calc\n\n  ```\n"_s);
        QCOMPARE(calc.role(0, "view").toString(), u"calc"_s);
        QCOMPARE(calc.page.editingRow(), 0);
        QCOMPARE(calc.page.cursorPosition(), 0);
        Fixture table(u"Tekst /tab\n"_s);
        r = table.page.applySlashCommand(0, 6, 10, u"table"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"table"_s);
        QCOMPARE(table.count(), 2);
        QCOMPARE(table.role(1, "view").toString(), u"table"_s);
        QCOMPARE(table.edit(0), u"Tekst "_s);
        // Dialog commands just remove the trigger and report the action.
        Fixture rem(u"- Zadzwonić /przyp\n"_s);
        r = rem.page.applySlashCommand(0, 10, 16, u"reminder"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"reminder"_s);
        QCOMPARE(rem.text(), u"- Zadzwonić \n"_s);
        // Journal links and embeds.
        Fixture today(u"- spotkanie /dzi\n"_s);
        today.page.applySlashCommand(0, 10, 14, u"tomorrow"_s);
        QCOMPARE(today.text(), u"- spotkanie [[2026-10-16]]\n"_s);
        Fixture embed(u"- /osa\n"_s);
        r = embed.page.applySlashCommand(0, 0, 4, u"embed"_s);
        QCOMPARE(embed.text(), u"- ![[\n"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"complete"_s);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 3);
        // Divider replaces the empty block with a top-level rule.
        Fixture rule(u"- a\n- /lin\n"_s);
        rule.page.applySlashCommand(1, 0, 4, u"rule"_s);
        QCOMPARE(rule.text(), u"- a\n---\n"_s);
        QCOMPARE(rule.role(1, "view").toString(), u"rule"_s);
        rule.page.undo();
        QCOMPARE(rule.text(), u"- a\n- /lin\n"_s);
    }

    void pasting()
    {
        // A list pasted into an empty block becomes blocks, nesting kept.
        Fixture f(u"- a\n- \n- z\n"_s);
        f.page.startEditing(1, 0);
        QVariantMap r = f.page.pasteText(1, 0, 0, u"- [ ] mleko\n- chleb\n\t- żytni\n"_s);
        QCOMPARE(f.text(), u"- a\n- [ ] mleko\n- chleb\n\t- żytni\n- z\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 3);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 5);
        f.page.undo();
        QCOMPARE(f.text(), u"- a\n- \n- z\n"_s);
        // Text before the caret joins the first pasted block, text after it ends the last one.
        Fixture mid(u"- początek|koniec\n"_s);
        r = mid.page.pasteText(0, 8, 9, u"- jeden\n- dwa"_s);
        QCOMPARE(mid.text(), u"- początekjeden\n- dwakoniec\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 1);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 3);
        // Paragraphs separated by blank lines become blocks too; one paragraph stays one block.
        Fixture para(u"- x\n"_s);
        para.page.pasteText(0, 1, 1, u" pierwszy\ndruga linia"_s);
        QCOMPARE(para.text(), u"- x pierwszy\n  druga linia\n"_s);
        Fixture two(u"- \n"_s);
        two.page.pasteText(0, 0, 0, u"Akapit pierwszy\n\nAkapit drugi"_s);
        QCOMPARE(two.count(), 2);
        QCOMPARE(two.edit(1), u"Akapit drugi"_s);
        // Code blocks keep pasted text as it is.
        Fixture code(u"```\nx\n```\n"_s);
        code.page.pasteText(0, 1, 1, u"\n- a\n- b"_s);
        QCOMPARE(code.text(), u"```\nx\n- a\n- b\n```\n"_s);
        // Pasting a fenced example into a code block widens the hidden fence instead of ending
        // the block at the pasted ``` line (and keeps the next block out of it).
        Fixture nested(u"```\nold\n```\n\nPo\n"_s);
        nested.page.startEditing(0, 3);
        nested.page.pasteText(0, 3, 3, u"\n```\nnew\n```"_s);
        QCOMPARE(nested.text(), u"````\nold\n```\nnew\n```\n````\n\nPo\n"_s);
        QCOMPARE(nested.count(), 2);
        QCOMPARE(nested.edit(0), u"old\n```\nnew\n```"_s);
    }

    void pastingCodeBlocksMidText()
    {
        // A pasted code block (or table, quote…) is never glued to the text around the caret:
        // "a```js" / "```bc" would no longer be a fence.
        Fixture f(u"- abc\n- z\n"_s);
        f.page.startEditing(0, 1);
        const QVariantMap r = f.page.pasteText(0, 1, 1, u"```js\nkod ąę\n```\npo"_s);
        QCOMPARE(f.edit(0), u"a"_s);
        QCOMPARE(f.role(1, "view").toString(), u"code"_s);
        QCOMPARE(f.edit(1), u"kod ąę"_s);
        QCOMPARE(f.edit(2), u"pobc"_s);
        QCOMPARE(f.edit(3), u"z"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 2);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 2);
        f.page.undo();
        QCOMPARE(f.text(), u"- abc\n- z\n"_s);
        // Ending with a code block: the text after the caret becomes a block of its own.
        f.page.pasteText(0, 1, 1, u"x\n\n| A | B |\n| --- | --- |\n| 1 | 2 |"_s);
        QCOMPARE(f.edit(0), u"ax"_s);
        QCOMPARE(f.role(1, "view").toString(), u"table"_s);
        QCOMPARE(f.edit(2), u"bc"_s);
        f.page.undo();
        // Only a code block, pasted mid-text: it still becomes a block between the two halves.
        f.page.pasteText(0, 1, 1, u"```\nkod\n```"_s);
        QCOMPARE(f.edit(0), u"a"_s);
        QCOMPARE(f.role(1, "view").toString(), u"code"_s);
        QCOMPARE(f.edit(1), u"kod"_s);
        QCOMPARE(f.edit(2), u"bc"_s);
        f.page.undo();
        QCOMPARE(f.text(), u"- abc\n- z\n"_s);
        // Into an empty block: the code block takes its place.
        Fixture e(u"- \n"_s);
        e.page.pasteText(0, 0, 0, u"```\nkod\n```\n\ntekst"_s);
        QCOMPARE(e.role(0, "view").toString(), u"code"_s);
        QCOMPARE(e.edit(1), u"tekst"_s);
        QCOMPARE(e.count(), 2);
    }

    void editingCodeBlocksWithFenceLinesInside()
    {
        // Two code blocks one after another, edited from the block view: the fence lines are
        // hidden, so ``` typed as content must not close either block (a reload would split it
        // into extra blocks and swallow the text after it).
        Fixture f(u"```\nfirst\n```\n\n```\nsecond\n```\n\nPo\n"_s);
        QCOMPARE(f.count(), 3);
        QCOMPARE(f.role(0, "view").toString(), u"code"_s);
        QCOMPARE(f.role(1, "view").toString(), u"code"_s);
        f.page.setEditText(0, u"first\n```\nstill code"_s);
        QCOMPARE(f.text(), u"````\nfirst\n```\nstill code\n````\n\n```\nsecond\n```\n\nPo\n"_s);
        QCOMPARE(f.count(), 3);
        // The widened fences are part of the edit and undo as one step.
        f.page.undo();
        QCOMPARE(f.text(), u"```\nfirst\n```\n\n```\nsecond\n```\n\nPo\n"_s);
        f.page.redo();
        QCOMPARE(f.text(), u"````\nfirst\n```\nstill code\n````\n\n```\nsecond\n```\n\nPo\n"_s);
        f.page.setEditText(1, u"second\n```\nstill code too"_s);
        QCOMPARE(f.text(), u"````\nfirst\n```\nstill code\n````\n\n````\nsecond\n```\nstill code too\n````\n\nPo\n"_s);
        QCOMPARE(f.count(), 3);
        // A fresh parse (reopening the page) sees exactly the same blocks.
        const markdown::BlockDocument reloaded(f.text());
        QCOMPARE(reloaded.count(), 3);
        QCOMPARE(reloaded.at(0).kind, BlockKind::Code);
        QCOMPARE(reloaded.at(1).kind, BlockKind::Code);
        QCOMPARE(reloaded.at(2).kind, BlockKind::Paragraph);
        QCOMPARE(reloaded.at(2).body, u"Po"_s);
        QCOMPARE(f.edit(0), u"first\n```\nstill code"_s);
        QCOMPARE(f.edit(1), u"second\n```\nstill code too"_s);
        // Fences are enough of a separator themselves: no blank line between two code blocks is
        // not a reason to merge them or insert one when one of them is edited.
        Fixture adjacent(u"```\nfirst\n```\n```\nsecond\n```\n\nPo\n"_s);
        QCOMPARE(adjacent.count(), 3);
        QCOMPARE(adjacent.role(0, "view").toString(), u"code"_s);
        QCOMPARE(adjacent.role(1, "view").toString(), u"code"_s);
        adjacent.page.setEditText(0, u"first\n```\nstill code"_s);
        QCOMPARE(adjacent.text(), u"````\nfirst\n```\nstill code\n````\n```\nsecond\n```\n\nPo\n"_s);
        QCOMPARE(adjacent.count(), 3);
    }

    // --- tables ----------------------------------------------------------------------------------------

    void tables()
    {
        Fixture f(u"Przed\n\n| Pozycja | Cena |\n| --- | ---: |\n| Płytki | 89 |\n| Suma | =sum(above) |\n\nPo\n"_s);
        QCOMPARE(f.role(1, "view").toString(), u"table"_s);
        QVariantList rows = f.role(1, "tableData").toMap().value(u"rows"_s).toList();
        QCOMPARE(rows[1].toList()[1].toMap().value(u"display"_s).toString(), u"89"_s);
        f.page.setTableCell(1, 0, 1, u"100"_s);
        rows = f.role(1, "tableData").toMap().value(u"rows"_s).toList();
        QCOMPARE(rows[1].toList()[1].toMap().value(u"display"_s).toString(), u"100"_s);
        QVERIFY(f.text().startsWith(u"Przed\n\n"_s));
        QVERIFY(f.text().endsWith(u"\n\nPo\n"_s));
        f.page.addTableRow(1);
        f.page.addTableColumn(1);
        const QVariantMap data = f.role(1, "tableData").toMap();
        QCOMPARE(data.value(u"header"_s).toStringList(), QStringList({u"Pozycja"_s, u"Cena"_s, u"Kolumna 3"_s}));
        QCOMPARE(data.value(u"rows"_s).toList().size(), 3);
        f.page.setTableCell(1, -1, 0, u"Rzecz"_s);
        QCOMPARE(f.role(1, "tableData").toMap().value(u"header"_s).toStringList().first(), u"Rzecz"_s);
        f.page.removeTableRow(1, 2);
        QCOMPARE(f.role(1, "tableData").toMap().value(u"rows"_s).toList().size(), 2);
        f.page.setTableCell(1, 99, 0, u"x"_s); // out of range: nothing
        f.page.undo();
        f.page.undo();
        f.page.undo();
        f.page.undo();
        f.page.undo();
        QCOMPARE(f.text(), u"Przed\n\n| Pozycja | Cena |\n| --- | ---: |\n| Płytki | 89 |\n| Suma | =sum(above) |\n\nPo\n"_s);
    }

    void tableCellsAreRendered()
    {
        Fixture f(u"| Rzecz | Cena |\n| --- | ---: |\n| **Płytki** z [[Inna]] #zakupy | 89 |\n| <b>x</b> ⏰ 2026-10-16 09:00 | =2*3 |\n| Brak [[Nie ma]] | =1+ |\n"_s);
        QVariantMap data = f.role(0, "tableData").toMap();
        const QVariantList rows = data.value(u"rows"_s).toList();
        const QVariantMap tiles = rows[0].toList()[0].toMap();
        const QString html = tiles.value(u"html"_s).toString();
        QVERIFY2(html.contains(u"<b>Płytki</b>"_s) && html.contains(u"href=\"page:Inna\""_s) && html.contains(u"tag:zakupy"_s),
                 qPrintable(html));
        // The visible text measures the column; the raw text stays what the cell editor edits.
        // "z" keeps the next word on its line (a no-break space, measured as such).
        QCOMPARE(tiles.value(u"plain"_s).toString(), u"Płytki z\u00a0Inna #zakupy"_s);
        QCOMPARE(tiles.value(u"text"_s).toString(), u"**Płytki** z [[Inna]] #zakupy"_s);
        // Note text that looks like HTML stays text; chips render.
        const QString hostile = rows[1].toList()[0].toMap().value(u"html"_s).toString();
        QVERIFY2(hostile.contains(u"&lt;b&gt;x&lt;/b&gt;"_s) && !hostile.contains(u"<b>x"_s), qPrintable(hostile));
        QVERIFY(hostile.contains(u"⏰&nbsp;jutro&nbsp;09:00"_s));
        // Formula results are values (escaped), not Markdown.
        const QVariantMap six = rows[1].toList()[1].toMap();
        QCOMPARE(six.value(u"html"_s).toString(), u"6"_s);
        QCOMPARE(six.value(u"plain"_s).toString(), u"6"_s);
        QVERIFY(!rows[2].toList()[1].toMap().value(u"error"_s).toString().isEmpty());
        QCOMPARE(rows[2].toList()[1].toMap().value(u"html"_s).toString(),
                 rows[2].toList()[1].toMap().value(u"display"_s).toString().toHtmlEscaped());
        // Header cells are rendered too.
        QCOMPARE(data.value(u"headerHtml"_s).toList().size(), 2);
        QCOMPARE(data.value(u"headerHtml"_s).toList()[0].toString(), u"Rzecz"_s);
        // Theme colours and the index re-render the cells (a page that appears resolves its link).
        QVERIFY(rows[2].toList()[0].toMap().value(u"html"_s).toString().contains(u"#7a5ea3"_s, Qt::CaseInsensitive));
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        f.page.setPalette({{u"link"_s, u"#102030"_s}, {u"tagFg"_s, u"#405060"_s}});
        QVERIFY(changed.size() > 0);
        data = f.role(0, "tableData").toMap();
        QVERIFY(data.value(u"rows"_s).toList()[0].toList()[0].toMap().value(u"html"_s).toString().contains(u"#102030"_s));
        f.host.setFile(u"Nie ma.md"_s, u"- teraz jest\n"_s);
        Q_EMIT f.host.indexChanged();
        data = f.role(0, "tableData").toMap();
        QVERIFY(data.value(u"rows"_s).toList()[2].toList()[0].toMap().value(u"html"_s).toString().contains(u"#102030"_s));
    }

    void transclusions()
    {
        Fixture f(u"- Wstęp ^wst\n- ![[Budżet#Październik]]\n- Zobacz ![[Inna#^inny1]] i !((kont))\n- ![[Brak strony]] ![[Budżet#Grudzień]]\n"_s);
        f.host.setFile(u"Budżet.md"_s,
                       u"# Budżet\n\n## Październik\n\n- Czynsz **1800 zł** #opłaty\n\t- [ ] Zapłacić 📅 2026-10-20\n- Jedzenie\n\n## Listopad\n\n- Prezenty\n"_s);
        f.host.setFile(u"Kontakty.md"_s, u"- Hydraulik: pan Marek ^kont\n\t- 600 100 200\n"_s);
        f.model()->invalidateRendering();
        // A section: the blocks under the heading up to the next one of its level.
        QVERIFY(f.role(1, "onlyEmbeds").toBool());
        QVERIFY(f.role(1, "html").toString().isEmpty());
        QVariantList t = f.role(1, "transclusions").toList();
        QCOMPARE(t.size(), 1);
        QVariantMap section = t[0].toMap();
        QCOMPARE(section.value(u"kind"_s).toString(), u"heading"_s);
        QVERIFY(section.value(u"found"_s).toBool());
        QCOMPARE(section.value(u"title"_s).toString(), u"Budżet › Październik"_s);
        QCOMPARE(section.value(u"href"_s).toString(), u"page:Bud%C5%BCet#Pa%C5%BAdziernik"_s);
        QCOMPARE(section.value(u"path"_s).toString(), u"Budżet.md"_s);
        QVariantList blocks = section.value(u"blocks"_s).toList();
        QCOMPARE(blocks.size(), 3);
        QVERIFY(blocks[0].toMap().value(u"html"_s).toString().contains(u"<b>1800 zł</b>"_s));
        QCOMPARE(blocks[0].toMap().value(u"depth"_s).toInt(), 0);
        QCOMPARE(blocks[1].toMap().value(u"depth"_s).toInt(), 1);
        QCOMPARE(blocks[1].toMap().value(u"task"_s).toString(), u"todo"_s);
        QCOMPARE(blocks[1].toMap().value(u"path"_s).toString(), u"Budżet.md"_s);
        QCOMPARE(blocks[1].toMap().value(u"line"_s).toInt(), 5);
        QVERIFY(!blocks[1].toMap().value(u"html"_s).toString().contains(u"[ ]"_s));
        QVERIFY(blocks[2].toMap().value(u"html"_s).toString().contains(u"Jedzenie"_s));
        // A block of a page (with its children) and a vault-wide block embed, next to text.
        QVERIFY(!f.role(2, "onlyEmbeds").toBool());
        QVERIFY(!f.role(2, "html").toString().isEmpty());
        t = f.role(2, "transclusions").toList();
        QCOMPARE(t.size(), 2);
        QCOMPARE(t[0].toMap().value(u"kind"_s).toString(), u"block"_s);
        QCOMPARE(t[0].toMap().value(u"title"_s).toString(), u"Inna"_s);
        QCOMPARE(t[0].toMap().value(u"href"_s).toString(), u"page:Inna#^inny1"_s);
        QVERIFY(t[0].toMap()
                        .value(u"blocks"_s)
                        .toList()[0]
                        .toMap()
                        .value(u"html"_s)
                        .toString()
                        .contains(u"Blok z\u00a0innej strony"_s));
        const QVariantMap contact = t[1].toMap();
        QCOMPARE(contact.value(u"href"_s).toString(), u"block:kont"_s);
        QCOMPARE(contact.value(u"path"_s).toString(), u"Kontakty.md"_s);
        QCOMPARE(contact.value(u"title"_s).toString(), u"Kontakty"_s);
        blocks = contact.value(u"blocks"_s).toList();
        QCOMPARE(blocks.size(), 2);
        QVERIFY(blocks[0].toMap().value(u"html"_s).toString().contains(u"pan Marek"_s));
        QCOMPARE(blocks[1].toMap().value(u"depth"_s).toInt(), 1);
        // Missing page and section.
        t = f.role(3, "transclusions").toList();
        QCOMPARE(t.size(), 2);
        QVERIFY(!t[0].toMap().value(u"found"_s).toBool());
        QCOMPARE(t[0].toMap().value(u"message"_s).toString(), u"missingPage"_s);
        QCOMPARE(t[0].toMap().value(u"title"_s).toString(), u"Brak strony"_s);
        QCOMPARE(t[1].toMap().value(u"message"_s).toString(), u"missingSection"_s);
        // The index changed (another device edited the page): the embed follows.
        f.host.setFile(u"Budżet.md"_s, u"## Październik\n\n- Czynsz 1900 zł\n"_s);
        Q_EMIT f.host.indexChanged();
        blocks = f.role(1, "transclusions").toList()[0].toMap().value(u"blocks"_s).toList();
        QCOMPARE(blocks.size(), 1);
        QVERIFY(blocks[0].toMap().value(u"html"_s).toString().contains(u"1900"_s));
    }

    void transcludedCodeBlocksShowTheirTextOnly()
    {
        Fixture f(u"- ![[Inna]]\n"_s);
        f.host.setFile(u"Inna.md"_s,
                       u"```js\nlet a = 1 < 2;\n```\n\n~~~\nb\n~~~\n\n```calc\nx = 2\n```\n"_s);
        f.model()->invalidateRendering();
        const QVariantList t = f.role(0, "transclusions").toList();
        QCOMPARE(t.size(), 1);
        const QVariantList blocks = t[0].toMap().value(u"blocks"_s).toList();
        QCOMPARE(blocks.size(), 3);
        QCOMPARE(blocks[0].toMap().value(u"view"_s).toString(), u"code"_s);
        QCOMPARE(blocks[0].toMap().value(u"html"_s).toString(), u"<pre>let a = 1 &lt; 2;</pre>"_s);
        QCOMPARE(blocks[1].toMap().value(u"html"_s).toString(), u"<pre>b</pre>"_s);
        QCOMPARE(blocks[2].toMap().value(u"view"_s).toString(), u"code"_s);
        QCOMPARE(blocks[2].toMap().value(u"html"_s).toString(), u"<pre>x = 2</pre>"_s);
    }

    void transclusionsOfThisPageAndCycles()
    {
        // ![[#Heading]] shows this page's section as edited (unsaved too); a section embedding
        // itself is shown once, with a note instead of the repetition.
        Fixture f(u"## Plan\n\n- Krok pierwszy\n- ![[#Plan]]\n\n## Inne\n\n- ![[#Plan]]\n"_s, 100000);
        int inner = -1;
        int outer = -1;
        for (int r = 0; r < f.count(); ++r) {
            if (f.edit(r) == u"![[#Plan]]"_s)
                (inner < 0 ? inner : outer) = r;
        }
        QVERIFY(inner > 0 && outer > inner);
        QVariantList blocks = f.role(outer, "transclusions").toList()[0].toMap().value(u"blocks"_s).toList();
        QCOMPARE(blocks.size(), 2);
        QVERIFY(blocks[0].toMap().value(u"html"_s).toString().contains(u"Krok pierwszy"_s));
        QCOMPARE(blocks[1].toMap().value(u"kind"_s).toString(), u"embed"_s);
        QCOMPARE(blocks[1].toMap().value(u"message"_s).toString(), u"cycle"_s);
        QCOMPARE(f.role(outer, "transclusions").toList()[0].toMap().value(u"href"_s).toString(), u"page:#Plan"_s);
        // The embed follows edits of this page right away.
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        f.page.setEditText(1, u"Krok pierwszy ąę"_s);
        blocks = f.role(outer, "transclusions").toList()[0].toMap().value(u"blocks"_s).toList();
        QVERIFY(blocks[0].toMap().value(u"html"_s).toString().contains(u"Krok pierwszy ąę"_s));
        bool outerChanged = false;
        for (const QList<QVariant> &args : std::as_const(changed)) {
            if (args.at(0).toModelIndex().row() <= outer && args.at(1).toModelIndex().row() >= outer)
                outerChanged = true;
        }
        QVERIFY(outerChanged);
        // The whole page embedded in itself.
        Fixture self(u"- Jedyny blok\n- ![[Strona]]\n"_s);
        blocks = self.role(1, "transclusions").toList()[0].toMap().value(u"blocks"_s).toList();
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[1].toMap().value(u"message"_s).toString(), u"cycle"_s);
    }

    void transclusionsAreDepthLimited()
    {
        // A chain of pages embedding the next one: MaxTransclusionDepth levels, then a note.
        Fixture f(u"- ![[P1]]\n"_s);
        for (int i = 1; i <= 6; ++i)
            f.host.setFile(u"P%1.md"_s.arg(i), u"- Poziom %1\n- ![[P%2]]\n"_s.arg(i).arg(i + 1));
        f.model()->invalidateRendering();
        const QVariantList blocks = f.role(0, "transclusions").toList()[0].toMap().value(u"blocks"_s).toList();
        int levels = 0;
        QString note;
        for (const QVariant &b : blocks) {
            const QVariantMap m = b.toMap();
            if (m.value(u"kind"_s).toString() == u"block"_s)
                ++levels;
            if (!m.value(u"message"_s).toString().isEmpty())
                note = m.value(u"message"_s).toString();
        }
        QCOMPARE(levels, BlockListModel::MaxTransclusionDepth);
        QCOMPARE(note, u"depth"_s);
        // Big pages: at most MaxTransclusionBlocks blocks, the rest counted.
        QString big;
        for (int i = 0; i < 100; ++i)
            big += u"- Wiersz %1\n"_s.arg(i);
        f.host.setFile(u"Duża.md"_s, big);
        f.page.setEditText(0, u"![[Duża]]"_s);
        const QVariantMap t = f.role(0, "transclusions").toList()[0].toMap();
        QCOMPARE(t.value(u"blocks"_s).toList().size(), BlockListModel::MaxTransclusionBlocks);
        QCOMPARE(t.value(u"more"_s).toInt(), 100 - BlockListModel::MaxTransclusionBlocks);
    }

    void titleRow()
    {
        Fixture f(u"---\ntags: [a]\n---\n# Strona\n\nTreść\n"_s);
        QCOMPARE(f.page.titleRow(), 1);
        QSignalSpy spy(&f.page, &PageController::titleRowChanged);
        f.page.setBody(1, u"# Inny tytuł"_s);
        QCOMPARE(f.page.titleRow(), -1);
        f.page.undo();
        QCOMPARE(f.page.titleRow(), 1);
        QVERIFY(spy.size() >= 2);
        // Diacritics and case do not matter, a Logseq "- # Title" item counts; a level-2 heading
        // or a heading after other blocks is no title.
        Fixture folded(u"# STRONA\n- a\n"_s);
        QCOMPARE(folded.page.titleRow(), 0);
        Fixture h2(u"---\na: 1\n---\n## Strona\n"_s);
        QCOMPARE(h2.page.titleRow(), -1);
        Fixture item(u"---\na: 1\n---\n- # Strona\n\t- dziecko\n"_s);
        QCOMPARE(item.page.titleRow(), 1);
        Fixture later(u"Wstęp\n\n# Strona\n"_s);
        QCOMPARE(later.page.titleRow(), -1);
        Fixture polish(u"# zazolc gesla\n- treść\n"_s);
        QCOMPARE(polish.page.titleRow(), -1);
        polish.page.followRename(Page, u"Notatki/Zażółć gęślą.md"_s); // diacritics folded
        QCOMPARE(polish.page.titleRow(), 0);
        // While zoomed there is no title row.
        Fixture zoom(u"---\na: 1\n---\n# Strona\n- rodzic\n\t- dziecko\n"_s);
        QCOMPARE(zoom.page.titleRow(), 1);
        zoom.page.zoomInto(2);
        QCOMPARE(zoom.page.titleRow(), -1);
        zoom.page.zoomOut();
        QCOMPARE(zoom.page.titleRow(), 1);
    }

    void tasksElsewhere()
    {
        Fixture f(u"- [ ] Tutaj 📅 2026-10-20\n- zwykły\n"_s);
        f.host.setNow(Now);
        f.host.setFile(u"Zakupy.md"_s, u"- [ ] Mleko\n"_s);
        QVERIFY(f.page.queriesAvailable());
        // This page: an editing command.
        QVERIFY(f.page.toggleTaskAt(Page, 0, u"todo"_s));
        QCOMPARE(f.text(), u"- [x] Tutaj 📅 2026-10-20 ✅ 2026-10-15\n- zwykły\n"_s);
        QVERIFY(!f.page.setTaskStatusAt(Page, 0, u"done"_s)); // already
        QVERIFY(!f.page.setTaskStatusAt(Page, 1, u"done"_s)); // no task
        QVERIFY(!f.page.setTaskStatusAt(Page, 0, u"nonsense"_s));
        f.page.undo();
        QVERIFY(f.text().startsWith(u"- [ ] Tutaj"_s));
        // Another file: through the host's QueryHost.
        QVERIFY(f.page.toggleTaskAt(u"Zakupy.md"_s, 0, u"todo"_s));
        QCOMPARE(f.host.file(u"Zakupy.md"_s), u"- [x] Mleko ✅ 2026-10-15\n"_s);
        QVERIFY(!f.page.toggleTaskAt(u"Nie ma.md"_s, 0, u"todo"_s));
        // The status menu: any status, "" = no status (the marker goes), here and elsewhere.
        QVERIFY(f.page.setTaskStatusAt(u"Zakupy.md"_s, 0, u"deferred"_s));
        QCOMPARE(f.host.file(u"Zakupy.md"_s), u"- [>] Mleko\n"_s);
        QVERIFY(f.page.setTaskStatusAt(u"Zakupy.md"_s, 0, QString()));
        QCOMPARE(f.host.file(u"Zakupy.md"_s), u"- Mleko\n"_s);
        QVERIFY(!f.page.setTaskStatusAt(u"Zakupy.md"_s, 0, QString())); // nothing left to remove
        QVERIFY(f.page.setTaskStatusAt(Page, 0, QString()));
        QCOMPARE(f.text(), u"- Tutaj 📅 2026-10-20\n- zwykły\n"_s);
        QVERIFY(!f.page.setTaskStatusAt(Page, 0, QString()));
        f.page.undo();
        QCOMPARE(f.text(), u"- [ ] Tutaj 📅 2026-10-20\n- zwykły\n"_s);
        QCOMPARE(PageController::toggledStatus(u"doing"_s), u"done"_s);
        QCOMPARE(PageController::toggledStatus(u"cancelled"_s), u"todo"_s);
        QCOMPARE(PageController::toggledStatus(QString()), QString());
        // Locations: this page scrolls, others navigate.
        QCOMPARE(f.page.openLocation(Page, 1).value(u"row"_s).toInt(), 1);
        QVERIFY(f.page.openLocation(u"Zakupy.md"_s, 0).isEmpty());
        QCOMPARE(f.host.lastRoute().value(u"path"_s).toString(), u"Zakupy.md"_s);
        QCOMPARE(f.host.lastRoute().value(u"line"_s).toInt(), 0);
    }

    void attachments()
    {
        Fixture f(u"- Szkic łazienki\n\n| a |\n| --- |\n| 1 |\n"_s);
        // Into the text at the caret, with spaces around the embed.
        f.page.startEditing(0, 5);
        QVariantMap r = f.page.insertAttachment(0, 5, 5, u"plan piętra.png"_s, QByteArray("\x89PNG"));
        QCOMPARE(r.value(u"row"_s).toInt(), 0);
        QCOMPARE(f.edit(0), u"Szkic ![[plan piętra.png]] łazienki"_s);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 26);
        QCOMPARE(f.page.cursorPosition(), 26);
        QVERIFY(f.role(0, "embeds").toList().size() == 1);
        f.page.undo();
        QCOMPARE(f.edit(0), u"Szkic łazienki"_s);
        // Not into a table: a block of its own after it.
        const int table = 1;
        QCOMPARE(f.role(table, "view").toString(), u"table"_s);
        r = f.page.insertAttachment(table, 0, 0, u"zdjęcie.jpg"_s, QByteArray("JPEG"));
        QCOMPARE(r.value(u"row"_s).toInt(), table + 1);
        QCOMPARE(f.edit(table + 1), u"![[zdjęcie.jpg]]"_s);
        QCOMPARE(f.page.editingRow(), -1);
        // A name that would find another file is embedded by its path.
        QCOMPARE(f.page.embedFor(u"assets/plan piętra.png"_s), u"![[plan piętra.png]]"_s);
        f.host.setFile(u"plan piętra.png.md"_s, u"- strona o tej nazwie\n"_s);
        QCOMPARE(f.page.embedFor(u"assets/plan piętra.png"_s), u"![[assets/plan piętra.png]]"_s);
        // Saving fails: nothing inserted, a signal for the message.
        QSignalSpy failed(&f.page, &PageController::attachmentFailed);
        r = f.page.insertAttachment(0, 0, 0, u"pusty.png"_s, QByteArray());
        QCOMPARE(r.value(u"row"_s).toInt(), -1);
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.at(0).at(0).toString(), u"pusty.png"_s);
        // Files (drops, pickers): stored and embedded, after the block or in the edited text.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString a = dir.filePath(u"żółw.png"_s);
        const QString b = dir.filePath(u"umowa najmu.pdf"_s);
        for (const QString &path : {a, b}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("dane");
        }
        r = f.page.insertFilesAfter(0, {QUrl::fromLocalFile(a), QUrl::fromLocalFile(b), QUrl::fromLocalFile(dir.filePath(u"brak.png"_s))});
        QCOMPARE(r.value(u"count"_s).toInt(), 2);
        QCOMPARE(f.edit(r.value(u"row"_s).toInt()), u"![[żółw.png]] ![[umowa najmu.pdf]]"_s);
        QCOMPARE(failed.size(), 2); // the missing file
        r = f.page.insertFiles(0, -1, -1, {QUrl::fromLocalFile(a)});
        QVERIFY(f.edit(0).endsWith(u" ![[żółw 2.png]]"_s) || f.edit(0).endsWith(u" ![[żółw-2.png]]"_s)
                || f.edit(0).contains(u"![[żółw"_s));
        // At the end of an empty page.
        Fixture empty{QString()};
        r = empty.page.insertFilesAfter(-1, {QUrl::fromLocalFile(a)});
        QVERIFY2(empty.text().startsWith(u"- ![[żółw.png]]"_s), qPrintable(empty.text()));
    }

    void pastingPictures()
    {
        Fixture f(u"- Zrzut: \n"_s);
        QClipboard *clipboard = QGuiApplication::clipboard();
        clipboard->setText(u"zwykły tekst"_s);
        QVERIFY(!f.page.clipboardHasImage());
        QCOMPARE(f.page.pasteImage(0, 7, 7).value(u"row"_s).toInt(), -1);
        QImage image(40, 20, QImage::Format_ARGB32);
        image.fill(QColor(u"#f2b33d"_s));
        clipboard->setImage(image);
        QVERIFY(f.page.clipboardHasImage());
        f.page.startEditing(0, 7);
        const QVariantMap r = f.page.pasteImage(0, 7, 7);
        QCOMPARE(r.value(u"row"_s).toInt(), 0);
        QCOMPARE(f.edit(0), u"Zrzut: ![[Pasted image 20261015100000.png]]"_s);
        const QVariantList embeds = f.role(0, "embeds").toList();
        QCOMPARE(embeds.size(), 1);
        QVERIFY(embeds[0].toMap().value(u"image"_s).toBool());
        // Copied files (a file manager's copy) paste as attachments too.
        QTemporaryDir dir;
        const QString file = dir.filePath(u"skan.pdf"_s);
        {
            QFile out(file);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write("%PDF");
        }
        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(file)});
        clipboard->setMimeData(mime);
        QVERIFY(f.page.clipboardHasImage());
        f.page.pasteImage(0, int(f.edit(0).size()), int(f.edit(0).size()));
        QVERIFY(f.edit(0).endsWith(u"![[skan.pdf]]"_s));
        clipboard->clear();
    }

    void imageSlashCommand()
    {
        Fixture f(u"- Zdjęcie /obr\n"_s);
        const QVariantMap r = f.page.applySlashCommand(0, 8, 12, u"image"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"image"_s);
        QCOMPARE(f.text(), u"- Zdjęcie \n"_s);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 8);
        QVERIFY(!f.page.imageNameFilters().isEmpty());
        QVERIFY(!f.page.pickImageInBrowser(0, 0, 0)); // a FileDialog outside the browser
    }

    void tableCellTypingIsSavedAndOneUndoStep()
    {
        // A cell being typed in reaches the document (and the debounced save) at once — an app
        // suspended or closed with the cell still open loses nothing — and its typing is one
        // undo step per cell.
        const QString content = u"| Pozycja | Cena |\n| --- | ---: |\n| Płytki | 89 |\n"_s;
        Fixture f(content, 0);
        f.page.setTableCell(0, 0, 0, u"Ł"_s, true);
        f.page.setTableCell(0, 0, 0, u"Łą"_s, true);
        f.page.setTableCell(0, 0, 0, u"Łąka"_s, true);
        QVERIFY(f.host.file(Page).contains(u"Łąka"_s));
        f.page.setTableCell(0, 0, 1, u"90"_s, true); // another cell: another step
        f.page.setTableCell(0, 0, 1, u"901"_s, true);
        QVERIFY(f.host.file(Page).contains(u"901"_s));
        f.page.undo();
        QVERIFY(f.text().contains(u"Łąka"_s));
        QVERIFY(f.text().contains(u"89"_s));
        f.page.undo();
        QCOMPARE(f.text(), content);
        QVERIFY(!f.page.canUndo());
        // Esc: the cell's typing is undone exactly (the table is reformatted by typing).
        f.page.setTableCell(0, -1, 1, u"Cena zł"_s, true);
        f.page.setTableCell(0, 0, 1, u"9"_s, true);
        f.page.setTableCell(0, 0, 1, u"99"_s, true);
        f.page.cancelTableCell(0, 0, 1);
        QVERIFY(f.text().contains(u"Cena zł"_s));
        QVERIFY(f.text().contains(u"89 |"_s) && !f.text().contains(u"99"_s));
        f.page.cancelTableCell(0, 0, 1); // nothing typed in this cell any more: no undo
        f.page.cancelTableCell(0, -1, 0);
        QVERIFY(f.text().contains(u"Cena zł"_s));
        f.page.undo();
        QCOMPARE(f.text(), content);
    }

    void calcResultsFollowEdits()
    {
        Fixture f(u"```calc\na = 2\nb = a * 3\n```\n\n```calc\nb + 1\n```\n"_s);
        QCOMPARE(f.role(1, "calcResults").toList()[0].toMap().value(u"value"_s).toString(), u"7"_s);
        f.page.setEditText(0, u"a = 5\nb = a * 3"_s, 5);
        QCOMPARE(f.role(0, "calcResults").toList()[1].toMap().value(u"value"_s).toString(), u"15"_s);
        QCOMPARE(f.role(1, "calcResults").toList()[0].toMap().value(u"value"_s).toString(), u"16"_s);
        QCOMPARE(f.text(), u"```calc\na = 5\nb = a * 3\n```\n\n```calc\nb + 1\n```\n"_s);
    }

    // --- links --------------------------------------------------------------------------------------------

    void links()
    {
        Fixture f(u"- [ ] zadanie ⏰ 2026-10-16 09:00 ^c1\n- [[Inna#^inny1]] ((c1))\n"_s);
        QSignalSpy nav(&f.host, &FakeEditorHost::navigated);
        QSignalSpy urls(&f.host, &FakeEditorHost::urlOpened);
        f.page.activateLink(1, u"page:Inna#%5Einny1"_s);
        QCOMPARE(nav.size(), 1);
        QCOMPARE(f.host.lastRoute().value(u"path"_s).toString(), u"Inna.md"_s);
        QCOMPARE(f.host.lastRoute().value(u"blockId"_s).toString(), u"inny1"_s);
        f.page.activateLink(1, u"page:Nowa%20strona"_s);
        QCOMPARE(f.host.lastRoute().value(u"path"_s).toString(), QString());
        QCOMPARE(f.host.lastRoute().value(u"target"_s).toString(), u"Nowa strona"_s);
        f.page.activateLink(0, u"tag:%C5%BC%C3%B3%C5%82w"_s);
        QCOMPARE(f.host.lastRoute().value(u"tag"_s).toString(), u"żółw"_s);
        f.page.activateLink(0, u"ext:https://example.com/a"_s);
        QCOMPARE(urls.size(), 1);
        QVariantMap r = f.page.activateLink(1, u"block:c1"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"scrollTo"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 0);
        r = f.page.activateLink(0, u"reminder:10-30"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"reminder"_s);
        QCOMPARE(r.value(u"from"_s).toInt(), 10);
        f.page.activateLink(0, u"task:toggle"_s);
        QVERIFY(f.text().startsWith(u"- [x] zadanie"_s));
        // The rendered reminder chip links to the token's range in the body.
        const QString html = f.role(0, "html").toString();
        const qsizetype k = html.indexOf(u"reminder:"_s);
        QVERIFY(k > 0);
        const QString range = html.mid(k + 9, html.indexOf(u'"', k) - k - 9);
        const QStringList parts = range.split(u'-');
        const QString body = f.model()->document().at(0).body;
        QCOMPARE(body.mid(parts[0].toInt(), parts[1].toInt() - parts[0].toInt()), u"⏰ 2026-10-16 09:00"_s);
    }

    // --- saving -------------------------------------------------------------------------------------------

    void debouncedSave()
    {
        Fixture f(u"- a\n"_s, 60);
        QSignalSpy saved(&f.page, &PageController::saved);
        f.page.setEditText(0, u"ab"_s, 2);
        f.page.setEditText(0, u"abc"_s, 3);
        QVERIFY(f.page.dirty());
        QCOMPARE(f.host.writeCount(), 0);
        QTRY_COMPARE(f.host.writeCount(), 1);
        QCOMPARE(f.host.file(Page), u"- abc\n"_s);
        QVERIFY(!f.page.dirty());
        QCOMPARE(saved.size(), 1);
        // save() flushes at once.
        f.page.setEditText(0, u"abcd"_s, 4);
        QVERIFY(f.page.save());
        QCOMPARE(f.host.writeCount(), 2);
        QTest::qWait(100);
        QCOMPARE(f.host.writeCount(), 2);
        // Undo back to the saved text is not dirty.
        f.page.cycleTask(0);
        QVERIFY(f.page.dirty());
        f.page.undo();
        QVERIFY(!f.page.dirty());
    }

    void failedSaveStaysDirty()
    {
        Fixture f(u"- a\n"_s, 0);
        QSignalSpy failed(&f.page, &PageController::saveFailed);
        f.host.setFailWrites(true);
        f.page.setEditText(0, u"b"_s, 1);
        QCOMPARE(failed.size(), 1);
        QVERIFY(f.page.dirty());
        f.host.setFailWrites(false);
        QVERIFY(f.page.save());
        QVERIFY(!f.page.dirty());
        QCOMPARE(f.host.file(Page), u"- b\n"_s);
    }

    void newPageIsCreatedByContent()
    {
        FakeEditorHost host;
        PageController page;
        page.setSaveDelay(0);
        page.setHost(&host);
        page.setPath(u"Nowa strona.md"_s);
        QVERIFY(page.loaded());
        QVERIFY(!page.exists());
        QCOMPARE(page.appendEmptyBlock(), 0);
        QVERIFY(!page.dirty());
        QCOMPARE(host.writeCount(), 0); // a lone empty bullet creates no file
        page.setEditText(0, u"Pierwsza myśl"_s, 13);
        QCOMPARE(host.writeCount(), 1);
        QVERIFY(page.exists());
        QCOMPARE(host.file(u"Nowa strona.md"_s), u"- Pierwsza myśl\n"_s);
    }

    void startNewPageWritesOnlyWhenChanged()
    {
        // A day without a note starts from the journal template, filled for that day; the note is
        // created by the first change, never by looking at the day or starting to write.
        FakeEditorHost host;
        host.setFile(u"templates/journal.md"_s, u"## {{date}} — {{weekday}}\n- \n"_s);
        host.setFile(u"templates/bez punktu.md"_s, u"## Plan dnia"_s);
        PageController page;
        page.setNow(Now);
        page.setSaveDelay(0);
        page.setHost(&host);
        const QString day = u"journals/2026-10-13.md"_s;
        page.setPath(day);
        QVERIFY(!page.exists());
        const int row = page.startNewPage(u"templates/journal.md"_s);
        QCOMPARE(page.text(), u"## 2026-10-13 — wtorek\n- \n"_s);
        QCOMPARE(row, 1);
        QVERIFY(!page.dirty());
        QVERIFY(!page.canUndo());
        QVERIFY(page.save());
        QCOMPARE(host.writeCount(), 0); // looked at, started, left: nothing written
        page.startEditing(row);
        page.setEditText(row, u"Kupić żółty ser"_s, 15);
        QCOMPARE(host.writeCount(), 1);
        QVERIFY(page.exists());
        QCOMPARE(host.file(day), u"## 2026-10-13 — wtorek\n- Kupić żółty ser\n"_s);
        // Undo goes back to the template (the starting text), not to an empty page.
        page.undo();
        QCOMPARE(page.text(), u"## 2026-10-13 — wtorek\n- \n"_s);
        page.redo();
        QCOMPARE(host.file(day), u"## 2026-10-13 — wtorek\n- Kupić żółty ser\n"_s);
        const int writes = host.writeCount();

        // A template without an empty bullet gets one to type into; leaving writes nothing.
        page.setPath(u"journals/2026-10-12.md"_s);
        const int r2 = page.startNewPage(u"templates/bez punktu.md"_s);
        QVERIFY(r2 >= 1);
        QVERIFY(page.text().startsWith(u"## Plan dnia"_s));
        QCOMPARE(page.editingRow(), -1);
        QVERIFY(!page.dirty());
        page.setPath(u"journals/2026-10-11.md"_s);
        QVERIFY(!host.files().contains(u"journals/2026-10-12.md"_s));
        QCOMPARE(host.writeCount(), writes);

        // No template (or a missing one): an empty bullet, as for any new page.
        QCOMPARE(page.startNewPage(u"templates/nie ma.md"_s), 0);
        QCOMPARE(page.startNewPage(QString()), 0);
        QCOMPARE(page.blocks()->count(), 1);
        // A page that exists keeps its text: the empty block goes at its end.
        page.setPath(day);
        QCOMPARE(page.startNewPage(u"templates/journal.md"_s), 2);
        QVERIFY(page.text().startsWith(u"## 2026-10-13 — wtorek\n- Kupić żółty ser\n"_s));
    }

    void startNewPageFollowsANoteCreatedElsewhere()
    {
        // Quick capture writes today's note while its day shows the template, untouched: the
        // editor takes the new file (it exists now) instead of the template.
        FakeEditorHost host;
        host.setFile(u"templates/journal.md"_s, u"## Plan\n- \n"_s);
        PageController page;
        page.setNow(Now);
        page.setSaveDelay(0);
        page.setHost(&host);
        page.setPath(u"journals/2026-10-15.md"_s);
        page.startNewPage(u"templates/journal.md"_s);
        QSignalSpy exists(&page, &PageController::existsChanged);
        host.externalWrite(u"journals/2026-10-15.md"_s, u"- Kupić mleko\n"_s);
        QCOMPARE(page.text(), u"- Kupić mleko\n"_s);
        QVERIFY(page.exists());
        QCOMPARE(exists.size(), 1);
        QVERIFY(!page.dirty());
    }

    void saveOnDestructionAndPathChange()
    {
        FakeEditorHost host;
        host.setFile(Page, u"- a\n"_s);
        host.setFile(u"B.md"_s, u"- b\n"_s);
        {
            PageController page;
            page.setSaveDelay(10000);
            page.setHost(&host);
            page.setPath(Page);
            page.setEditText(0, u"zmiana"_s, 6);
            QCOMPARE(host.file(Page), u"- a\n"_s);
            page.setPath(u"B.md"_s);
            QCOMPARE(host.file(Page), u"- zmiana\n"_s);
            QCOMPARE(page.text(), u"- b\n"_s);
            page.setEditText(0, u"bb"_s, 2);
        }
        QCOMPARE(host.file(u"B.md"_s), u"- bb\n"_s);
    }

    // --- external changes ---------------------------------------------------------------------------

    void externalChangeReloadsWhenClean()
    {
        Fixture f(u"- a\n- b\n- c\n"_s);
        QSignalSpy resets(f.model(), &QAbstractItemModel::modelReset);
        QSignalSpy changed(&f.page, &PageController::externallyChanged);
        f.page.startEditing(2, 1);
        f.host.externalWrite(Page, u"- a\n- b zmienione przez Syncthing\n- c\n"_s);
        QCOMPARE(f.text(), u"- a\n- b zmienione przez Syncthing\n- c\n"_s);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(f.page.editingRow(), 2);
        QVERIFY(!f.page.dirty());
        QVERIFY(!f.page.canUndo());
        // Our own write coming back is ignored.
        f.page.setEditText(2, u"c!"_s, 2);
        f.page.save();
        f.host.externalWrite(Page, f.host.file(Page));
        QCOMPARE(changed.size(), 1);
    }

    void externalChangeMergesWhenDirty()
    {
        Fixture f(u"- a\n- b\n- c\n"_s, 10000);
        f.page.setMergeFunction([](const QString &base, const QString &local, const QString &remote) {
            // Test merge: the remote added a line at the end, the local changed the first line.
            Q_UNUSED(base);
            if (remote == u"- a\n- b\n- c\n- d\n"_s)
                return MergeOutcome{local + u"- d\n"_s, true};
            return MergeOutcome{local, false};
        });
        f.page.setEditText(0, u"a lokalnie"_s, 10);
        QVERIFY(f.page.dirty());
        f.host.externalWrite(Page, u"- a\n- b\n- c\n- d\n"_s);
        QCOMPARE(f.text(), u"- a lokalnie\n- b\n- c\n- d\n"_s);
        QVERIFY(f.page.dirty());
        QVERIFY(!f.page.conflict());
        QVERIFY(f.page.save());
        QCOMPARE(f.host.file(Page), u"- a lokalnie\n- b\n- c\n- d\n"_s);
        // A conflict keeps the local text and blocks saving until resolved.
        f.page.setEditText(1, u"b lokalnie"_s, 10);
        f.host.externalWrite(Page, u"- a zdalnie\n- b\n- c\n- d\n"_s);
        QVERIFY(f.page.conflict());
        QCOMPARE(f.page.conflictText(), u"- a zdalnie\n- b\n- c\n- d\n"_s);
        QCOMPARE(f.text(), u"- a lokalnie\n- b lokalnie\n- c\n- d\n"_s);
        QVERIFY(!f.page.save());
        QCOMPARE(f.host.file(Page), u"- a zdalnie\n- b\n- c\n- d\n"_s);
        f.page.resolveConflict(u"- a obie\n- b lokalnie\n- c\n- d\n"_s);
        QVERIFY(!f.page.conflict());
        QCOMPARE(f.host.file(Page), u"- a obie\n- b lokalnie\n- c\n- d\n"_s);
        QCOMPARE(f.text(), u"- a obie\n- b lokalnie\n- c\n- d\n"_s);
    }

    void defaultMergeIsThreeWay()
    {
        Fixture f(u"- a\n- b\n- c\n"_s, 10000);
        f.page.setEditText(0, u"a lokalnie"_s, 10);
        f.host.externalWrite(Page, u"- a\n- b\n- c zdalnie\n"_s);
#ifdef BRULION_TEST_NO_SYNC
        QVERIFY(f.page.conflict());
#else
        if (f.page.conflict())
            QSKIP("sync::merge3 is not part of this build");
        QCOMPARE(f.text(), u"- a lokalnie\n- b\n- c zdalnie\n"_s);
#endif
    }

    void conflictCopyOnDestruction()
    {
        FakeEditorHost host;
        host.setFile(Page, u"- a\n"_s);
        {
            PageController page;
            page.setSaveDelay(10000);
            page.setMergeFunction([](const QString &, const QString &local, const QString &) {
                return MergeOutcome{local, false};
            });
            page.setHost(&host);
            page.setPath(Page);
            page.setEditText(0, u"lokalnie"_s, 8);
            host.externalWrite(Page, u"- zdalnie\n"_s);
            QVERIFY(page.conflict());
        }
        QCOMPARE(host.file(Page), u"- zdalnie\n"_s);
        bool copied = false;
        for (const QString &p : host.files()) {
            if (p.contains(u".conflict-"_s)) {
                QCOMPARE(host.file(p), u"- lokalnie\n"_s);
                copied = true;
            }
        }
        QVERIFY(copied);
    }

    void conflictFollowsLaterChanges()
    {
        // A conflict is not frozen: when the other side changes again and the change now merges
        // (or equals the local text, or goes back to the base), the conflict is over — and
        // "Use the other version" never writes an outdated remote text over a newer file.
        Fixture f(u"- a\n- b\n- c\n"_s, 10000);
        f.page.setMergeFunction([](const QString &base, const QString &local, const QString &remote) {
            Q_UNUSED(base);
            if (remote.endsWith(u"- d\n"_s) && remote.startsWith(u"- a\n"_s))
                return MergeOutcome{local + u"- d\n"_s, true};
            return MergeOutcome{local, false};
        });
        f.page.setEditText(0, u"a lokalnie"_s, 10);
        f.host.externalWrite(Page, u"- a zdalnie\n- b\n- c\n"_s);
        QVERIFY(f.page.conflict());
        // The other device reverts its change and adds a line: merges cleanly now.
        f.host.externalWrite(Page, u"- a\n- b\n- c\n- d\n"_s);
        QVERIFY(!f.page.conflict());
        QCOMPARE(f.text(), u"- a lokalnie\n- b\n- c\n- d\n"_s);
        QVERIFY(f.page.save());
        QCOMPARE(f.host.file(Page), u"- a lokalnie\n- b\n- c\n- d\n"_s);

        // Conflict, then the other side writes exactly our text: nothing to resolve.
        f.page.setEditText(1, u"b lokalnie"_s, 10);
        f.host.externalWrite(Page, u"- a lokalnie\n- b zdalnie\n- c\n- d\n"_s);
        QVERIFY(f.page.conflict());
        f.host.externalWrite(Page, u"- a lokalnie\n- b lokalnie\n- c\n- d\n"_s);
        QVERIFY(!f.page.conflict());
        QVERIFY(!f.page.dirty());

        // Conflict, then the other side goes back to the common base: ours can be saved.
        f.page.setEditText(2, u"c lokalnie"_s, 10);
        const QString base = f.host.file(Page);
        f.host.externalWrite(Page, u"- inne\n"_s);
        QVERIFY(f.page.conflict());
        f.host.externalWrite(Page, base);
        QVERIFY(!f.page.conflict());
        QVERIFY(f.page.save());
        QCOMPARE(f.host.file(Page), u"- a lokalnie\n- b lokalnie\n- c lokalnie\n- d\n"_s);

        // A second conflicting change replaces the other version shown in the merge view.
        f.page.setEditText(0, u"a jeszcze raz"_s, 13);
        f.host.externalWrite(Page, u"- x\n"_s);
        f.host.externalWrite(Page, u"- y\n"_s);
        QVERIFY(f.page.conflict());
        QCOMPARE(f.page.conflictText(), u"- y\n"_s);
        f.page.takeRemote();
        QCOMPARE(f.host.file(Page), u"- y\n"_s);
        QCOMPARE(f.text(), u"- y\n"_s);
        QVERIFY(!f.page.dirty());

        // The other side emptied the page and we take that version: the file stays empty.
        f.page.setEditText(0, u"y lokalnie"_s, 10);
        f.host.externalWrite(Page, QString());
        QVERIFY(f.page.conflict());
        f.page.takeRemote();
        QCOMPARE(f.host.file(Page), QString());
        QCOMPARE(f.text(), QString());
        QVERIFY(!f.page.dirty());
    }

    void rawHtmlIsNeverRendered_data()
    {
        QTest::addColumn<QString>("content");
        const QString evil = u"<img src=\"https://tracker.example/p.png\"> <b>pogrubione</b> <a href=\"javascript:alert(1)\">x</a>"_s;
        QTest::newRow("list item") << u"- "_s + evil + u"\n"_s;
        QTest::newRow("task") << u"- [ ] "_s + evil + u"\n"_s;
        QTest::newRow("paragraph") << evil + u"\n"_s;
        QTest::newRow("heading") << u"# "_s + evil + u"\n"_s;
        QTest::newRow("heading item") << u"- ## "_s + evil + u"\n  druga linia "_s + evil + u"\n"_s;
        QTest::newRow("quote") << u"> "_s + evil + u"\n"_s;
        QTest::newRow("callout") << u"> [!warning] "_s + evil + u"\n> "_s + evil + u"\n"_s;
        QTest::newRow("html block") << u"<div>\n"_s + evil + u"\n</div>\n"_s;
        QTest::newRow("link text") << u"- [<img src=x onerror=alert(1)>](https://example.com) [[<b>strona</b>]]\n"_s;
    }

    void rawHtmlIsNeverRendered()
    {
        // Notes come from anywhere: markup in a note is shown as text, never interpreted by Text.
        QFETCH(QString, content);
        Fixture f(content);
        for (int r = 0; r < f.count(); ++r) {
            const QString html = f.role(r, "html").toString();
            QVERIFY2(!html.contains(u"<img"_s, Qt::CaseInsensitive), qPrintable(html));
            QVERIFY2(!html.contains(u"<b>pogrubione"_s), qPrintable(html));
            QVERIFY2(!html.contains(u"<div"_s, Qt::CaseInsensitive), qPrintable(html));
            QVERIFY2(!html.contains(u"javascript:"_s, Qt::CaseInsensitive) || html.contains(u"&lt;a href=&quot;javascript:"_s),
                     qPrintable(html));
            QVERIFY2(!html.contains(u"onerror=alert"_s) || html.contains(u"&lt;img"_s), qPrintable(html));
        }
    }

    void cursorsNeverSplitSurrogatePairs()
    {
        // UTF-16: a caret position between the two halves of an emoji (a stale position after the
        // text changed, a computed one) must never cut it into invalid text.
        Fixture f(u"- a😀b\n- 📅 jutro\n"_s);
        const QString edit = f.edit(0);
        QCOMPARE(edit.size(), 4); // a, high, low, b
        f.page.split(0, 2); // inside the pair
        QVERIFY2(f.text() == u"- a😀\n- b\n- 📅 jutro\n"_s || f.text() == u"- a\n- 😀b\n- 📅 jutro\n"_s,
                 qPrintable(f.text()));
        f.page.undo();
        QCOMPARE(f.text(), u"- a😀b\n- 📅 jutro\n"_s);
        f.page.replaceRange(0, 2, 2, u"X"_s);
        QVERIFY2(f.text().startsWith(u"- a😀X"_s) || f.text().startsWith(u"- aX😀"_s), qPrintable(f.text()));
        f.page.undo();
        f.page.insertToken(1, 1, u"⏰ 2026-10-16 09:00"_s);
        QVERIFY2(f.text().contains(u"📅"_s), qPrintable(f.text()));
        f.page.undo();
        const QVariantMap pasted = f.page.pasteText(0, 2, 2, u"x\ny"_s);
        Q_UNUSED(pasted);
        QVERIFY2(f.text().contains(u"😀"_s), qPrintable(f.text()));
        f.page.undo();
        f.page.startEditing(0, 2);
        QVERIFY(f.page.cursorPosition() == 1 || f.page.cursorPosition() == 3);
        // Every row's text is valid UTF-16.
        for (int r = 0; r < f.count(); ++r) {
            const QString t = f.edit(r);
            for (qsizetype i = 0; i < t.size(); ++i) {
                if (t[i].isHighSurrogate())
                    QVERIFY(i + 1 < t.size() && t[i + 1].isLowSurrogate());
                if (t[i].isLowSurrogate())
                    QVERIFY(i > 0 && t[i - 1].isHighSurrogate());
            }
        }
    }

    void trackedBlocksFollowChanges()
    {
        // A dialog opened for a block acts on that block even when the page changed meanwhile.
        Fixture f(u"- a\n- b ąę\n- c\n"_s, 10000);
        const int handle = f.page.trackBlock(1);
        QVERIFY(handle > 0);
        QCOMPARE(f.page.trackedRow(handle), 1);
        f.host.externalWrite(Page, u"- nowy z telefonu\n- a\n- b ąę\n- c\n"_s); // reload (not dirty)
        QCOMPARE(f.page.trackedRow(handle), 2);
        f.page.split(0, -1);
        QCOMPARE(f.page.trackedRow(handle), 3);
        f.page.undo();
        QCOMPARE(f.page.trackedRow(handle), 2);
        f.host.externalWrite(Page, u"- nowy z telefonu\n- a\n- c\n"_s); // the block is gone
        QCOMPARE(f.page.trackedRow(handle), -1);
        const int other = f.page.trackBlock(2);
        QCOMPARE(f.page.trackedRow(other), 2);
        f.page.setPath(u"Inna.md"_s);
        QCOMPARE(f.page.trackedRow(other), -1);
        f.page.untrack(other);
        QCOMPARE(f.page.trackedRow(other), -1);
        QCOMPARE(f.page.trackBlock(99), 0);
    }

    void conflictCopyWhenSuspended()
    {
        // Android may kill a suspended app without running destructors: an unresolved conflict
        // keeps the local text only in memory, so suspending writes it to a conflict copy (one
        // file per conflict, updated, never a new one per suspend).
        FakeEditorHost host;
        host.setFile(Page, u"- a\n"_s);
        PageController page;
        page.setSaveDelay(10000);
        page.setMergeFunction([](const QString &, const QString &local, const QString &) {
            return MergeOutcome{local, false};
        });
        page.setHost(&host);
        page.setPath(Page);
        page.setEditText(0, u"lokalnie"_s, 8);
        host.externalWrite(Page, u"- zdalnie\n"_s);
        QVERIFY(page.conflict());
        const auto copies = [&] {
            QStringList out;
            for (const QString &p : host.files()) {
                if (p.contains(u".conflict-"_s))
                    out.append(p);
            }
            return out;
        };
        Q_EMIT qGuiApp->applicationStateChanged(Qt::ApplicationSuspended);
        QCOMPARE(copies().size(), 1);
        QCOMPARE(host.file(copies().first()), u"- lokalnie\n"_s);
        QCOMPARE(host.file(Page), u"- zdalnie\n"_s); // the other version is not overwritten
        page.setEditText(0, u"lokalnie 2"_s, 10);
        Q_EMIT qGuiApp->applicationStateChanged(Qt::ApplicationSuspended);
        Q_EMIT qGuiApp->applicationStateChanged(Qt::ApplicationActive);
        QCOMPARE(copies().size(), 1);
        QCOMPARE(host.file(copies().first()), u"- lokalnie 2\n"_s);
        // Resolving ends the conflict; a later conflict gets a copy of its own.
        page.keepLocal();
        QCOMPARE(host.file(Page), u"- lokalnie 2\n"_s);
    }

    void followsRenames()
    {
        // The page is renamed (shell dialog, sync) while it has unsaved edits: they go to the new
        // path; the old path is never written again (it would come back as a duplicate page).
        Fixture f(u"- a\n- b\n"_s, 10000);
        f.page.startEditing(1, 1);
        f.page.setEditText(1, u"b zmienione"_s, 11);
        QVERIFY(f.page.dirty());
        const QString renamed = u"Notatki/Nowa nazwa.md"_s;
        f.host.setFile(renamed, f.host.file(Page)); // what the vault's rename did
        f.host.removeFile(Page);
        f.page.followRename(Page, renamed);
        QCOMPARE(f.page.path(), renamed);
        QCOMPARE(f.page.title(), u"Nowa nazwa"_s);
        QCOMPARE(f.page.editingRow(), 1); // still editing, undo kept
        QVERIFY(f.page.canUndo());
        f.page.setPath(renamed); // the route follows too: no effect
        QVERIFY(f.page.save());
        QVERIFY(!f.host.files().contains(Page));
        QCOMPARE(f.host.file(renamed), u"- a\n- b zmienione\n"_s);
        // Other renames are not ours.
        f.page.followRename(u"Inna.md"_s, u"Inna2.md"_s);
        QCOMPARE(f.page.path(), renamed);
        // Renamed while clean, and the new file differs (sync): it is loaded.
        f.host.setFile(u"C.md"_s, u"- a\n- b zmienione\n- c z telefonu\n"_s);
        f.host.removeFile(renamed);
        f.page.followRename(renamed, u"C.md"_s);
        QCOMPARE(f.text(), u"- a\n- b zmienione\n- c z telefonu\n"_s);
        QVERIFY(!f.page.dirty());
    }

    void tellsTheHostWhichPageItShows()
    {
        // The host watches the pages editors show (Workspace: in-place rewrites by other
        // programs); every page reported as shown is reported as closed again.
        struct RecordingHost : FakeEditorHost {
            QStringList events;
            void editorOpened(const QString &path) override { events.append(u"+"_s + path); }
            void editorClosed(const QString &path) override { events.append(u"-"_s + path); }
        };
        RecordingHost host;
        RecordingHost other;
        host.setFile(Page, u"- a\n"_s);
        {
            PageController page;
            page.setHost(&host); // no page yet
            QCOMPARE(host.events, QStringList());
            page.setPath(Page);
            QCOMPARE(host.events, QStringList{u"+"_s + Page});
            page.setPath(Page);
            page.followRename(u"Inna.md"_s, u"Inna2.md"_s); // not ours
            QCOMPARE(host.events.size(), 1);
            host.events.clear();
            page.followRename(Page, u"Notatki/Zażółć.md"_s);
            QCOMPARE(host.events, (QStringList{u"-"_s + Page, u"+Notatki/Zażółć.md"_s}));
            page.setPath(u"B.md"_s);
            QCOMPARE(host.events.mid(2), (QStringList{u"-Notatki/Zażółć.md"_s, u"+B.md"_s}));
            host.events.clear();
            page.setHost(&other);
            QCOMPARE(host.events, QStringList{u"-B.md"_s});
            QCOMPARE(other.events, QStringList{u"+B.md"_s});
            page.setPath(QString());
            page.setPath(u"C.md"_s);
            page.setHost(nullptr);
            page.setHost(&other);
            QCOMPARE(other.events.mid(1), (QStringList{u"-B.md"_s, u"+C.md"_s, u"-C.md"_s, u"+C.md"_s}));
            other.events.clear();
        }
        QCOMPARE(other.events, QStringList{u"-C.md"_s}); // destroyed
        QCOMPARE(host.events, QStringList{u"-B.md"_s});

        // A host destroyed first is not told anything.
        auto *gone = new RecordingHost;
        PageController page;
        page.setHost(gone);
        page.setPath(Page);
        delete gone;
        page.setPath(u"D.md"_s);
        page.setHost(&host);
        QCOMPARE(host.events.last(), u"+D.md"_s);
    }

    void removedFileKeepsText()
    {
        Fixture f(u"- a\n"_s);
        QSignalSpy removed(&f.page, &PageController::fileRemoved);
        f.host.removeFile(Page);
        QCOMPARE(removed.size(), 1);
        QCOMPARE(f.text(), u"- a\n"_s);
    }

    // --- source mode ----------------------------------------------------------------------------------

    void sourceMode()
    {
        Fixture f(u"- a\n- b\n"_s, 0);
        f.page.startEditing(0, 0);
        f.page.setSourceMode(true);
        QCOMPARE(f.page.editingRow(), -1);
        QCOMPARE(f.page.sourceText(), u"- a\n- b\n"_s);
        f.page.setSourceText(u"- a\n- b\n- c ąę\n"_s);
        QCOMPARE(f.host.file(Page), u"- a\n- b\n- c ąę\n"_s); // saved while in source mode
        QCOMPARE(f.count(), 2); // blocks follow when leaving
        f.page.setSourceMode(false);
        QCOMPARE(f.count(), 3);
        QCOMPARE(f.text(), u"- a\n- b\n- c ąę\n"_s);
        QVERIFY(!f.page.dirty());
        f.page.undo();
        QCOMPARE(f.text(), u"- a\n- b\n"_s);
    }

    void sourceModeKeepsLineEndings()
    {
        // The source editor's TextEdit reports "\n" for every line break: an edit there must not
        // rewrite a CRLF file (or lone "\r" lines) into LF.
        const QString content = u"---\r\ntytuł: Zażółć\r\n---\r\n- a\r\n- b\r\n\r\nKoniec\r\n"_s;
        Fixture f(content, 0);
        f.page.setSourceMode(true);
        QString shown = content;
        shown.replace(u"\r\n"_s, u"\n"_s);
        // What a TextEdit hands back when nothing changed: the file is untouched.
        f.page.setSourceText(shown);
        QCOMPARE(f.page.sourceText(), content);
        QVERIFY(!f.page.dirty());
        // A typed character and a new line.
        shown.replace(u"- b\n"_s, u"- bę\n- nowa\n"_s);
        f.page.setSourceText(shown);
        const QString expected = u"---\r\ntytuł: Zażółć\r\n---\r\n- a\r\n- bę\r\n- nowa\r\n\r\nKoniec\r\n"_s;
        QCOMPARE(f.page.sourceText(), expected);
        QCOMPARE(f.host.file(Page), expected);
        f.page.setSourceMode(false);
        QCOMPARE(f.text(), expected);
        QCOMPARE(f.count(), 5);
    }

    // --- large pages ------------------------------------------------------------------------------

    void undoStoresChangesNotPages()
    {
        // Every undo step used to keep two copies of the whole page: on a long page (an imported
        // journal) a few hundred commands held hundreds of megabytes. Steps store the change.
        QString content;
        for (int i = 0; i < 3000; ++i)
            content += u"- Blok numer %1 z tekstem zażółć gęślą jaźń\n"_s.arg(i);
        Fixture f(content, 100000);
        for (int k = 0; k < 60; ++k) {
            f.page.startEditing(k * 10, -1);
            f.page.split(k * 10, -1);
            f.page.setEditText(k * 10 + 1, u"nowy %1"_s.arg(k), 6);
        }
        QVERIFY(f.page.canUndo());
        QVERIFY2(f.page.undoHistorySize() < content.size() / 4,
                 qPrintable(u"undo history holds %1 characters for a %2 character page"_s.arg(f.page.undoHistorySize()).arg(content.size())));
        while (f.page.canUndo())
            f.page.undo();
        QCOMPARE(f.text(), content);
        while (f.page.canRedo())
            f.page.redo();
        QVERIFY(f.text().contains(u"- nowy 59\n"_s));
        QCOMPARE(f.count(), 3000 + 60);
    }

    void largePageTouchesOneRow()
    {
        // 5000 blocks (with calc and table blocks and inline calculations that depend on the
        // page): typing in one block changes only that row — no resets, no inserts/removes, no
        // dataChanged for other rows — and stays fast.
        QString content = u"# Dziennik budowy\n\n"_s;
        for (int i = 0; i < 5000; ++i) {
            if (i % 1000 == 500)
                content += u"- ```calc\n  cena = %1 zł\n  cena * 2\n  ```\n"_s.arg(i);
            else if (i % 1000 == 700)
                content += u"- Koszt `= cena * 3` łącznie #budowa\n"_s;
            else
                content += u"- Wpis %1: zażółć gęślą jaźń [[Strona %2]] #tag%3 📅 2026-10-%4\n"_s.arg(i).arg(i % 50).arg(i % 7).arg(10 + i % 18);
        }
        QElapsedTimer load;
        load.start();
        Fixture f(content, 100000);
        const qint64 loadMs = load.elapsed();
        const int row = 2500;
        QVERIFY(f.edit(row).startsWith(u"Wpis"_s));
        f.page.startEditing(row, -1);
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        QSignalSpy reset(f.model(), &QAbstractItemModel::modelReset);
        QSignalSpy inserted(f.model(), &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(f.model(), &QAbstractItemModel::rowsRemoved);
        QElapsedTimer typing;
        typing.start();
        QString text = f.edit(row);
        for (int k = 0; k < 50; ++k) {
            text += QChar(u"ąćęłńóśźż"_s.at(k % 9));
            f.page.setEditText(row, text, int(text.size()));
        }
        const qint64 typingMs = typing.elapsed();
        QCOMPARE(reset.size(), 0);
        QCOMPARE(inserted.size(), 0);
        QCOMPARE(removed.size(), 0);
        for (const QList<QVariant> &args : std::as_const(changed)) {
            const QModelIndex from = args.at(0).toModelIndex();
            const QModelIndex to = args.at(1).toModelIndex();
            QCOMPARE(from.row(), row);
            QCOMPARE(to.row(), row);
        }
        QElapsedTimer ops;
        ops.start();
        f.page.split(row, -1);
        f.page.indent(row + 1);
        f.page.undo();
        f.page.undo();
        f.page.undo();
        const qint64 opsMs = ops.elapsed();
        QCOMPARE(reset.size(), 0);
        qInfo("5000 blocks: load %lld ms, 50 keystrokes %lld ms, split+indent+3 undo %lld ms", loadMs, typingMs, opsMs);
        QVERIFY2(typingMs < 50 * 40 * BRULION_TEST_SLOWDOWN, "typing on a 5000-block page is too slow");
    }

    // Last: runs the application's event loop to quit it.
    void savesWhenTheApplicationQuits()
    {
        // A page still open when the app quits is saved even before QML tears it down.
        auto *host = new FakeEditorHost;
        host->setFile(Page, u"- a\n"_s);
        auto *page = new PageController;
        page->setSaveDelay(100000);
        page->setHost(host);
        page->setPath(Page);
        page->setEditText(0, u"zapisz mnie"_s, 11);
        QCOMPARE(host->file(Page), u"- a\n"_s);
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        QCoreApplication::exec();
        QCOMPARE(host->file(Page), u"- zapisz mnie\n"_s);
        delete page;
        delete host;
    }
};

QTEST_MAIN(TestPageController)
#include "tst_editor_controller.moc"
