// SourceTextSync: source mode's TextEdit (here its QTextDocument, with the source mode's
// MarkdownHighlighter on it as in SourceEditor.qml) handing its edits to PageController one at a
// time — random edits, undo and redo, formats, every kind of line ending, read-only pages, text
// loaded from the controller, the safety net, and what a keystroke costs on a long page.
#include "blocktext.h"
#include "editorinput.h"
#include "fakeeditorhost.h"
#include "markdownhighlighter.h"
#include "pagecontroller.h"
#include "sourcetextsync.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Page = u"Notatki/Źródło.md"_s;

/// A page in source mode with its "TextEdit": the document, the highlighter and the sync, wired as
/// SourceEditor.qml wires them (the controller's own changes are loaded into the document).
struct Fixture {
    FakeEditorHost host;
    PageController page;
    QTextDocument doc;
    MarkdownHighlighter highlighter;
    SourceTextSync sync;

    explicit Fixture(const QString &content, int saveDelay = 0, bool reloads = true, bool highlighted = true)
    {
        host.setFile(Page, content);
        page.setNow(u"2026-11-16 10:00"_s);
        page.setSaveDelay(saveDelay);
        page.setHost(&host);
        page.setPath(Page);
        page.setSourceMode(true);
        highlighter.setMode(u"source"_s);
        if (highlighted)
            highlighter.attach(&doc);
        sync.setController(&page);
        sync.setDocument(&doc);
        doc.setPlainText(page.sourceText());
        sync.setActive(true);
        if (reloads) {
            QObject::connect(&page, &PageController::sourceTextChanged, &sync, [this] {
                if (page.sourceMode() && !sync.pushing())
                    load();
            });
        }
    }

    void load()
    {
        sync.setActive(false);
        doc.setPlainText(page.sourceText());
        sync.setActive(true);
    }

    QString shown() const { return documentPlainText(&doc); }
    int size() const { return doc.characterCount() - 1; }
    QTextCursor at(int from, int to)
    {
        QTextCursor c(&doc);
        c.setPosition(from);
        c.setPosition(to, QTextCursor::KeepAnchor);
        return c;
    }
    void type(int position, const QString &text) { at(position, position).insertText(text); }
};

QString pageOf(int lines, const QString &eol)
{
    QString text = u"---\ntytuł: Zażółć\n---\n"_s;
    text.replace(u"\n"_s, eol);
    for (int i = 0; i < lines; ++i)
        text += u"- Wiersz %1: gęślą jaźń [[Strona %2]] #tag ⏰ jutro 9:00"_s.arg(i).arg(i % 7) + eol;
    return text;
}

} // namespace

class TstEditorSourceSync : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void randomEditsKeepTheTextInStep_data()
    {
        QTest::addColumn<QString>("eol");
        QTest::addColumn<QString>("mixed");
        QTest::addColumn<quint32>("seed");
        QTest::newRow("lf") << u"\n"_s << QString() << 1u;
        QTest::newRow("crlf") << u"\r\n"_s << QString() << 2u;
        QTest::newRow("lf with crlf lines") << u"\n"_s << u"\r\n"_s << 3u;
        QTest::newRow("lone cr") << u"\r"_s << QString() << 4u;
        QTest::newRow("lf with lone cr lines") << u"\n"_s << u"\r"_s << 5u;
        QTest::newRow("crlf, another seed") << u"\r\n"_s << QString() << 6u;
    }

    void randomEditsKeepTheTextInStep()
    {
        // Every edit a TextEdit makes — typing, pastes with line breaks, deletions across lines,
        // undo and redo (several steps at once), edits of one undo step, replacing everything,
        // formats and line heights — leaves the controller's text showing exactly the document's,
        // without one whole text handed over.
        QFETCH(QString, eol);
        QFETCH(QString, mixed);
        QFETCH(quint32, seed);
        QString content = pageOf(30, eol);
        if (!mixed.isEmpty()) {
            for (int i = 0; i < 5; ++i)
                content += u"- inny koniec %1"_s.arg(i) + mixed;
        }
        content += u"- ```\n  kod\n  ```"_s;
        Fixture f(content);
        QRandomGenerator rng(seed);
        const QStringList pieces{u"a"_s,  u"ż"_s,    u"\n"_s,   u"zażółć\ngęślą"_s,     u"```\n"_s,   u" "_s, u" "_s,
                                 u"😀"_s, u"\r\n"_s, u"x\ry"_s, u"- [ ] zadanie\n\n"_s, u"[[link]]"_s};
        for (int step = 0; step < 400; ++step) {
            const int size = f.size();
            QTextCursor c = f.at(rng.bounded(size + 1), rng.bounded(size + 1));
            const QString before = f.page.sourceText();
            const int op = rng.bounded(11);
            switch (op) {
            case 0:
            case 1:
            case 2: c.insertText(pieces.at(rng.bounded(pieces.size()))); break;
            case 3: c.removeSelectedText(); break;
            case 4: c.insertBlock(); break;
            case 5:
                if (f.doc.isUndoAvailable())
                    f.doc.undo();
                break;
            case 6:
                if (f.doc.isRedoAvailable())
                    f.doc.redo();
                break;
            case 7: {
                QTextBlockFormat format;
                format.setLineHeight(100 + rng.bounded(60), QTextBlockFormat::ProportionalHeight);
                c.mergeBlockFormat(format);
                break;
            }
            case 8: {
                // The find bar's Replace all: several edits as one undo step.
                c.beginEditBlock();
                for (int k = 0; k < 3; ++k) {
                    const int s = f.size();
                    const int x = rng.bounded(s + 1);
                    f.at(x, std::min(s, x + rng.bounded(5))).insertText(pieces.at(rng.bounded(pieces.size())));
                }
                c.endEditBlock();
                break;
            }
            case 9: {
                QTextCharFormat format;
                format.setFontItalic(true);
                c.mergeCharFormat(format);
                break;
            }
            case 10:
                c.select(QTextCursor::Document);
                c.insertText(pageOf(3, u"\n"_s));
                break;
            }
            QCOMPARE(shownText(f.page.sourceText()), f.shown());
            if (op == 7 || op == 9) // formats only: the file keeps every byte (its line endings too)
                QCOMPARE(f.page.sourceText(), before);
        }
        f.sync.verify();
        QCOMPARE(f.sync.differences(), 0);
        QCOMPARE(f.sync.wholeTexts(), 0);
        QVERIFY(!f.sync.handsOverWholeText());
        QVERIFY(f.sync.edits() > 300);
        // What is saved is the controller's text, and the blocks take it when leaving.
        QVERIFY(f.page.save());
        QCOMPARE(f.host.file(Page), f.page.sourceText());
    }

    void lineEndingsAreKept()
    {
        // A CRLF file stays CRLF: typed text and a new line between untouched lines.
        const QString content = u"---\r\ntytuł: Zażółć\r\n---\r\n- a\r\n- b\r\n\r\nKoniec\r\n"_s;
        Fixture f(content);
        const int b = int(f.shown().indexOf(u"- b"_s)) + 3;
        f.type(b, u"ę"_s);
        f.type(b + 1, u"\n- nowa"_s);
        const QString expected = u"---\r\ntytuł: Zażółć\r\n---\r\n- a\r\n- bę\r\n- nowa\r\n\r\nKoniec\r\n"_s;
        QCOMPARE(f.page.sourceText(), expected);
        QCOMPARE(f.host.file(Page), expected);
        QCOMPARE(f.sync.wholeTexts(), 0);
        f.page.setSourceMode(false);
        QCOMPARE(f.page.text(), expected);
    }

    void formatsChangeNothing()
    {
        // Highlighting, line heights and character formats are no edits: nothing saved, no echo —
        // also over lines with other terminators than the document's (Qt reports the whole range).
        const QString content = pageOf(20, u"\r\n"_s) + u"- LF\n- CR\r- ąę"_s;
        Fixture f(content);
        QSignalSpy changed(&f.page, &PageController::sourceTextChanged);
        QTextCursor all = f.at(0, f.size());
        QTextBlockFormat block;
        block.setLineHeight(150, QTextBlockFormat::ProportionalHeight);
        all.mergeBlockFormat(block);
        QTextCharFormat chars;
        chars.setFontWeight(QFont::Bold);
        f.at(3, 40).mergeCharFormat(chars);
        f.highlighter.rehighlight();
        QCOMPARE(changed.size(), 0);
        QVERIFY(!f.page.dirty());
        QCOMPARE(f.host.file(Page), content);
        QCOMPARE(f.page.sourceText(), content);
    }

    void textLoadedFromTheControllerIsNoEdit()
    {
        // A file changed elsewhere while the page is clean: the controller reloads, the document
        // is filled again (the sync inactive meanwhile), typing goes on in the new text.
        Fixture f(u"- a\n- b\n"_s);
        f.host.externalWrite(Page, u"- a\n- b zmienione przez Syncthing\n- c\n"_s);
        QCOMPARE(f.shown(), u"- a\n- b zmienione przez Syncthing\n- c\n"_s);
        f.type(f.size(), u"- dopisane ąę"_s);
        QCOMPARE(f.page.sourceText(), u"- a\n- b zmienione przez Syncthing\n- c\n- dopisane ąę"_s);
        QCOMPARE(f.host.file(Page), f.page.sourceText());
        QCOMPARE(f.sync.wholeTexts(), 0);
    }

    void misplacedEditHandsOverWholeTexts()
    {
        // An edit the controller cannot place (its text shorter than the document's, as if an
        // edit had been lost): the TextEdit's whole text is handed over, now and for the rest of
        // the session — as before SourceTextSync.
        Fixture f(u"- a\n- b\n"_s, 0, false);
        QVERIFY(f.page.editSourceText(0, 4, QString()) == PageController::SourceEditResult::Applied);
        f.type(f.size(), u"- c ąę"_s);
        QVERIFY(f.sync.handsOverWholeText());
        QCOMPARE(f.sync.wholeTexts(), 1);
        QCOMPARE(f.page.sourceText(), u"- a\n- b\n- c ąę"_s);
        f.type(0, u"#"_s);
        QCOMPARE(f.sync.wholeTexts(), 2);
        QCOMPARE(f.page.sourceText(), u"#- a\n- b\n- c ąę"_s);
        QCOMPARE(f.host.file(Page), f.page.sourceText());
        // Text loaded from the controller starts a new session: edits one by one again.
        f.load();
        QVERIFY(!f.sync.handsOverWholeText());
        f.type(1, u"!"_s);
        QCOMPARE(f.sync.wholeTexts(), 2);
        QCOMPARE(f.page.sourceText(), u"#!- a\n- b\n- c ąę"_s);
    }

    void differencesAreRepaired()
    {
        // The safety net: a controller text that no longer matches the TextEdit's (none is known
        // to happen) is found once typing pauses or before a save, logged and repaired.
        // (A save delay longer than the sync's: the pause comes first.)
        Fixture f(u"- a\n- b\n"_s, 4 * SourceTextSync::VerifyDelayMs, false);
        f.type(3, u"ą"_s);
        // A wrong report, as if Qt had placed an edit elsewhere.
        QVERIFY(f.page.editSourceText(0, 0, u"X"_s) == PageController::SourceEditResult::Applied);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"SourceTextSync: .* differs"_s));
        QTRY_COMPARE(f.sync.differences(), 1); // after SourceTextSync::VerifyDelayMs
        QCOMPARE(f.page.sourceText(), u"- aą\n- b\n"_s);
        QVERIFY(f.page.save());
        QCOMPARE(f.host.file(Page), u"- aą\n- b\n"_s);
        QVERIFY(f.sync.handsOverWholeText());

        // Before a save that comes first (no pause in between).
        Fixture g(u"- a\n- b\n"_s, 100000, false);
        g.type(3, u"ę"_s);
        QVERIFY(g.page.editSourceText(0, 0, u"Y"_s) == PageController::SourceEditResult::Applied);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"SourceTextSync: .* differs"_s));
        QVERIFY(g.page.save());
        QCOMPARE(g.sync.differences(), 1);
        QCOMPARE(g.host.file(Page), u"- aę\n- b\n"_s);

        // And before leaving source mode with it.
        Fixture h(u"- a\n- b\n"_s, 100000, false);
        h.type(7, u"ó"_s);
        QVERIFY(h.page.editSourceText(0, 1, QString()) == PageController::SourceEditResult::Applied);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"SourceTextSync: .* differs"_s));
        h.page.setSourceMode(false);
        QCOMPARE(h.sync.differences(), 1);
        QCOMPARE(h.page.text(), u"- a\n- bó\n"_s);
    }

    void outsideSourceModeNothingIsHandedOver()
    {
        Fixture f(u"- a\n"_s, 0, false);
        f.page.setSourceMode(false);
        QVERIFY(f.page.editSourceText(0, 0, u"x"_s) == PageController::SourceEditResult::Unfit);
        QCOMPARE(f.page.text(), u"- a\n"_s);
    }

    void keystrokeCostsTheEditNotThePage_data()
    {
        QTest::addColumn<bool>("highlighted");
        QTest::newRow("plain") << false;
        QTest::newRow("highlighted") << true;
    }

    void keystrokeCostsTheEditNotThePage()
    {
        QFETCH(bool, highlighted);
        // A 500 KB page: a keystroke used to hand over the whole text (copied, compared, its line
        // endings mapped back: O(page)); now it hands over the edit. Both ways are timed by what
        // they do themselves, not the document's own work (insertion, layout, highlighting: 3-4 ms
        // a keystroke here in a Debug build, the same both ways), so that a busy machine (CI runs
        // tests in parallel) does not hide the difference in that work's noise.
        Fixture f(pageOf(9000, u"\r\n"_s), 100000, false, highlighted);
        QVERIFY(f.page.sourceText().size() > 500'000);
        // The sync's slot alone: a clock started by a slot connected before it and read by one
        // connected after it (slots run in the order they were connected; the highlighter's,
        // connected first, makes its changes of formats before the clock starts).
        f.sync.setDocument(nullptr);
        QElapsedTimer clock;
        qint64 edits = 0;
        QObject::connect(&f.doc, &QTextDocument::contentsChange, &f.sync, [&clock] { clock.start(); });
        f.sync.setDocument(&f.doc);
        QObject::connect(&f.doc, &QTextDocument::contentsChange, &f.sync,
                         [&clock, &edits] { edits += clock.nsecsElapsed(); });
        const int middle = f.size() / 2;
        constexpr int Keys = 60;
        QElapsedTimer timer;
        timer.start();
        for (int k = 0; k < Keys; ++k)
            f.type(middle + k, u"ż"_s);
        const qint64 all = timer.nsecsElapsed();
        QCOMPARE(f.sync.wholeTexts(), 0);
        QVERIFY(f.sync.edits() >= Keys);
        QCOMPARE(shownText(f.page.sourceText()), f.shown());

        // The old way (SourceEditor.qml before: the TextEdit's whole text on every change; once a
        // keystroke here, though a change of formats paid for it again).
        Fixture old(pageOf(9000, u"\r\n"_s), 100000, false, highlighted);
        old.sync.setActive(false);
        QString last = old.shown();
        qint64 whole = 0;
        for (int k = 0; k < Keys; ++k) {
            old.type(middle + k, u"ż"_s);
            timer.restart();
            const QString text = documentPlainText(&old.doc);
            if (text != last) {
                last = text;
                old.page.setSourceText(text);
            }
            whole += timer.nsecsElapsed();
        }
        QCOMPARE(old.page.sourceText(), f.page.sourceText());
        qInfo("500 KB page, %d keystrokes: %.2f ms each in all, handing over %.3f ms each by edits, "
              "%.3f ms each by whole texts",
              Keys, double(all) / Keys / 1e6, double(edits) / Keys / 1e6, double(whole) / Keys / 1e6);
        // (Measured, Debug on x86_64: about 0.1 ms against 4.2 ms.)
        QVERIFY2(edits * 3 < whole, "a keystroke in source mode costs about as much as the whole page");
    }
};

QTEST_MAIN(TstEditorSourceSync)
#include "tst_editor_sourcesync.moc"
