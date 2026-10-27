// StreamSelection (docs/UI.md "Selecting and copying"): a block selection over several days of
// the journal stream — the part each day selects (anchor and focus pages, the days between whole,
// never page properties), Shift+↑/↓ across the days' edges, a tap / Shift+click / drag into another
// day, the span ending when a day is used on its own, days destroyed and created again while
// scrolling (copied from their files meanwhile), days leaving the stream, and the copy: each day's
// blocks after its [[date]] link (plain text: the long date), the HTML rendered on request, the
// size limit, nothing edited. Offscreen QGuiApplication: a real clipboard in the process.
#include "fakeeditorhost.h"
#include "pagecontroller.h"
#include "streamselection.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTest>

#include <memory>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

constexpr int PathRole = Qt::UserRole + 1;
constexpr int DateRole = Qt::UserRole + 2;

// Top to bottom, as the journal stream lists them: today (no note yet), then earlier days.
const QString Today = u"journals/2026-10-28.md"_s;
const QString Day1 = u"journals/2026-10-27.md"_s;
const QString Day2 = u"journals/2026-10-26.md"_s;
const QString Day3 = u"journals/2026-10-24.md"_s;

/// Rows: 0 Poranek, 1 Zadania, 2 its task, 3 Wieczór.
const QString Content1 = u"- Poranek: kawa z mlekiem\n- Zadania\n\t- [ ] Zadzwonić do Łucji 📅 2026-10-28\n- Wieczór\n"_s;
/// Rows: 0 page properties, 1 Źdźbło, 2 Gęślą.
const QString Content2 = u"typ:: dziennik\n\n- Źdźbło trawy\n- Gęślą jaźń\n"_s;
/// A document. Rows: 0 frontmatter, 1 heading, 2 paragraph, 3 list item.
const QString Content3 = u"---\nbrulion: document\n---\n# Spotkanie\n\nNotatki ze spotkania 🎉.\n\n- punkt pierwszy\n"_s;

struct Stream {
    FakeEditorHost host;
    QStandardItemModel model;
    StreamSelection stream;
    /// Day1, Day2, Day3 (Today has no note and no controller).
    std::unique_ptr<PageController> pages[3];

    explicit Stream(const QString &c1 = Content1, const QString &c2 = Content2, const QString &c3 = Content3)
    {
        host.setFile(Day1, c1);
        host.setFile(Day2, c2);
        host.setFile(Day3, c3);
        model.setItemRoleNames({{PathRole, "path"}, {DateRole, "date"}});
        for (const QString &path : {Today, Day1, Day2, Day3}) {
            auto *item = new QStandardItem;
            item->setData(path, PathRole);
            item->setData(path.mid(9, 10), DateRole);
            model.appendRow(item);
        }
        stream.setHost(&host);
        stream.setModel(&model);
        for (int i = 0; i < 3; ++i)
            open(i);
    }
    PageController &page(int i) { return *pages[i]; }
    QString path(int i) const { return QStringList{Day1, Day2, Day3}.at(i); }
    void open(int i)
    {
        pages[i] = std::make_unique<PageController>();
        pages[i]->setNow(u"2026-10-28 10:00"_s);
        pages[i]->setSaveDelay(0);
        pages[i]->setHost(&host);
        pages[i]->setPath(path(i));
        stream.attach(pages[i].get());
    }
    void close(int i)
    {
        stream.detach(pages[i].get());
        pages[i].reset();
    }
    QList<int> selected(int i) const
    {
        QList<int> out;
        if (pages[i]) {
            for (const QVariant &v : pages[i]->selectedRows())
                out.append(v.toInt());
        }
        return out;
    }
    /// Select from (page a, row ra) to (page f, row rf) as a tap / Shift+click there does.
    void span(int a, int ra, int f, int rf)
    {
        page(a).selectRange(ra, ra);
        QVERIFY(stream.extendTo(&page(f), rf));
    }
    QString label(int i) { return u"[["_s + host.linkTarget(path(i), QString()) + u"]]"_s; }
};

QString clipboardText()
{
    return QGuiApplication::clipboard()->text();
}

} // namespace

class TestStreamSelection : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void spanRanges_data()
    {
        QTest::addColumn<int>("anchorPage");
        QTest::addColumn<int>("anchorRow");
        QTest::addColumn<int>("focusPage");
        QTest::addColumn<int>("focusRow");
        QTest::addColumn<QList<int>>("rows1");
        QTest::addColumn<QList<int>>("rows2");
        QTest::addColumn<QList<int>>("rows3");
        QTest::addColumn<int>("pageCount");
        // Down: the anchor to its page's end, the focus page from its first block (after the
        // page properties) to the focus — with the focus block's subtree.
        QTest::newRow("down, adjacent days") << 0 << 1 << 1 << 2 << QList<int>{1, 2, 3} << QList<int>{1, 2} << QList<int>{} << 2;
        QTest::newRow("down, a day between whole") << 0 << 3 << 2 << 2 << QList<int>{3} << QList<int>{1, 2} << QList<int>{1, 2} << 3;
        QTest::newRow("down to a parent") << 1 << 2 << 2 << 3 << QList<int>{} << QList<int>{2} << QList<int>{1, 2, 3} << 2;
        // Up: the anchor page from its first block to the anchor, the focus page from the focus on.
        QTest::newRow("up, a day between whole") << 2 << 2 << 0 << 2 << QList<int>{2, 3} << QList<int>{1, 2} << QList<int>{1, 2} << 3;
        QTest::newRow("up from a heading") << 2 << 1 << 1 << 2 << QList<int>{} << QList<int>{2} << QList<int>{1} << 2;
        QTest::newRow("up to a parent") << 1 << 1 << 0 << 1 << QList<int>{1, 2, 3} << QList<int>{1} << QList<int>{} << 2;
        // An end on the page properties / frontmatter: the first block instead.
        QTest::newRow("focus on page properties") << 0 << 3 << 1 << 0 << QList<int>{3} << QList<int>{1} << QList<int>{} << 2;
        QTest::newRow("anchor on frontmatter") << 2 << 0 << 1 << 2 << QList<int>{} << QList<int>{2} << QList<int>{1} << 2;
    }
    void spanRanges()
    {
        QFETCH(int, anchorPage);
        QFETCH(int, anchorRow);
        QFETCH(int, focusPage);
        QFETCH(int, focusRow);
        QFETCH(QList<int>, rows1);
        QFETCH(QList<int>, rows2);
        QFETCH(QList<int>, rows3);
        QFETCH(int, pageCount);
        Stream s;
        // (The frontmatter is selected alone like a block: then the span starts from it.)
        s.span(anchorPage, anchorRow, focusPage, focusRow);
        QVERIFY(s.stream.active());
        QCOMPARE(s.stream.anchorPath(), s.path(anchorPage));
        QCOMPARE(s.stream.focusPath(), s.path(focusPage));
        QCOMPARE(s.stream.pageCount(), pageCount);
        QCOMPARE(s.selected(0), rows1);
        QCOMPARE(s.selected(1), rows2);
        QCOMPARE(s.selected(2), rows3);
        int count = 0;
        for (int i = 0; i < 3; ++i) {
            QVERIFY(!s.page(i).canUndo()); // selecting edits nothing
            QCOMPARE(s.page(i).editingRow(), -1);
            count += s.page(i).selectionCount();
        }
        QCOMPARE(s.stream.count(), count);
        QVERIFY(s.stream.toolsController() == &s.page(focusPage));
    }

    void foldedChildrenAreCountedAsShownAndCopied()
    {
        Stream s;
        s.page(0).setFolded(1, true); // Zadania with its task
        s.span(0, 1, 1, 1);
        QCOMPARE(s.selected(0), QList<int>({1, 2, 3}));
        QCOMPARE(s.stream.count(), 2 + 1); // Zadania, Wieczór; Źdźbło
        QVERIFY(s.stream.markdown().contains(u"Zadzwonić do Łucji"_s));
    }

    void copyAcrossDays()
    {
        Stream s;
        s.span(0, 3, 2, 3);
        const QString expected = s.label(0) + u"\n\n- Wieczór\n\n"_s + s.label(1) + u"\n\n- Źdźbło trawy\n- Gęślą jaźń\n\n"_s
                                 + s.label(2) + u"\n\n# Spotkanie\n\nNotatki ze spotkania 🎉.\n\n- punkt pierwszy"_s;
        QVERIFY(s.label(0).contains(u"2026-10-27"_s));
        QCOMPARE(s.stream.markdown(), expected);
        s.host.renderCalls = 0;
        const QVariantMap r = s.stream.copy();
        QVERIFY(r.value(u"ok"_s).toBool());
        QCOMPARE(r.value(u"count"_s).toInt(), 1 + 2 + 3);
        QCOMPARE(r.value(u"pages"_s).toInt(), 3);
        QCOMPARE(clipboardText(), expected);
        // The HTML flavour: offered, rendered once when asked for.
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        QVERIFY(mime->hasHtml());
        QCOMPARE(s.host.renderCalls, 0);
        QCOMPARE(mime->html(), u"<pre>"_s + expected.toHtmlEscaped() + u"</pre>"_s);
        mime->html();
        QCOMPARE(s.host.renderCalls, 1);
        // A paste of it in a day is this app's own copy: the Markdown, never converted back.
        QCOMPARE(s.page(0).clipboardText(), expected);
        // Plain text: the long dates.
        QVERIFY(s.stream.copy(true).value(u"ok"_s).toBool());
        QCOMPARE(clipboardText(), u"wtorek, 27 października 2026\n• Wieczór\n\n"
                                  u"poniedziałek, 26 października 2026\n• Źdźbło trawy\n• Gęślą jaźń\n\n"
                                  u"sobota, 24 października 2026\nSpotkanie\n\nNotatki ze spotkania 🎉.\n\n• punkt pierwszy"_s);
        QVERIFY(!QGuiApplication::clipboard()->mimeData()->hasHtml());
        s.stream.setLocale(u"en"_s);
        QVERIFY(s.stream.plainText().startsWith(u"Tuesday, October 27, 2026\n• Wieczór\n\nMonday, October 26, 2026\n"_s));
        // Copying edits nothing and keeps the span.
        QVERIFY(s.stream.active());
        for (int i = 0; i < 3; ++i)
            QVERIFY(!s.page(i).canUndo());
        QCOMPARE(s.host.file(Day1), Content1);
        QCOMPARE(s.host.file(Day2), Content2);
        QCOMPARE(s.host.file(Day3), Content3);
    }

    void noSpanNothingToCopy()
    {
        Stream s;
        QCOMPARE(s.stream.copy().value(u"reason"_s).toString(), u"empty"_s);
        QCOMPARE(s.stream.markdown(), QString());
        // No other day holds a selection: a Shift+click selects in its own day.
        QVERIFY(!s.stream.extendTo(&s.page(1), 1));
        QVERIFY(!s.stream.active());
        QVERIFY(!s.stream.othersSelected(&s.page(1)));
        s.page(0).selectRange(0, 0);
        QVERIFY(s.stream.othersSelected(&s.page(1)));
        QVERIFY(!s.stream.othersSelected(&s.page(0)));
    }

    void detachedDaysStillCopy()
    {
        Stream s;
        s.span(0, 3, 2, 3);
        const QString markdown = s.stream.markdown();
        const QString plain = s.stream.plainText();
        const int count = s.stream.count();
        // Scrolled far away: the anchor day and the day between are destroyed.
        s.close(1);
        s.close(0);
        QVERIFY(s.stream.active());
        QCOMPARE(s.stream.markdown(), markdown);
        QCOMPARE(s.stream.plainText(), plain);
        QCOMPARE(s.stream.count(), count);
        QVERIFY(s.stream.toolsController() == &s.page(2));
        // The focus day goes too: nothing shows the tools; still copied from the files.
        s.close(2);
        QVERIFY(s.stream.toolsController() == nullptr);
        QVERIFY(s.stream.copy().value(u"ok"_s).toBool());
        QCOMPARE(clipboardText(), markdown);
        // Created again (scrolled back): each day takes its part once loaded.
        s.open(1);
        QCOMPARE(s.selected(1), QList<int>({1, 2}));
        s.open(0);
        QCOMPARE(s.selected(0), QList<int>({3}));
        QVERIFY(s.stream.toolsController() == &s.page(0));
        s.open(2);
        QCOMPARE(s.selected(2), QList<int>({1, 2, 3}));
        QVERIFY(s.stream.toolsController() == &s.page(2));
        QVERIFY(s.stream.active());
        QCOMPARE(s.stream.markdown(), markdown);
    }

    void fileChangedWhileAway()
    {
        Stream s;
        s.span(0, 3, 2, 3);
        s.close(2);
        // Sync shortens the focus day: its rows are clamped to what is there.
        s.host.externalWrite(Day3, u"- tylko jeden punkt\n"_s);
        QVERIFY(s.stream.markdown().endsWith(s.label(2) + u"\n\n- tylko jeden punkt"_s));
        s.open(2);
        QCOMPARE(s.selected(2), QList<int>({0}));
    }

    void dayUsedOnItsOwnEndsTheSpan_data()
    {
        QTest::addColumn<QString>("action");
        QTest::addColumn<int>("day");
        QTest::newRow("edit") << u"edit"_s << 1;
        QTest::newRow("select") << u"select"_s << 1;
        // (A day selected whole already changes nothing: the editor ends the span first.)
        QTest::newRow("select all") << u"selectAll"_s << 0;
        QTest::newRow("pick") << u"pick"_s << 1;
        QTest::newRow("fold") << u"fold"_s << 0;
    }
    void dayUsedOnItsOwnEndsTheSpan()
    {
        QFETCH(QString, action);
        QFETCH(int, day);
        Stream s;
        s.span(0, 1, 2, 1);
        QSignalSpy changed(&s.stream, &StreamSelection::changed);
        PageController &page = s.page(day);
        if (action == u"edit")
            page.startEditing(1, 0);
        else if (action == u"select")
            page.selectRange(2, 2);
        else if (action == u"selectAll")
            page.selectAll();
        else if (action == u"pick")
            page.toggleSelected(1);
        else
            page.setFolded(1, true); // the selected rows shown change
        QVERIFY(!s.stream.active());
        QVERIFY(changed.count() >= 1);
        for (int i = 0; i < 3; ++i) {
            if (i != day)
                QCOMPARE(s.selected(i), QList<int>());
        }
        QVERIFY(s.stream.toolsController() == nullptr);
        QCOMPARE(s.stream.count(), 0);
        if (action == u"edit")
            QCOMPARE(page.editingRow(), 1);
        else
            QVERIFY(!s.selected(day).isEmpty());
    }

    void clearClearsEveryDay()
    {
        Stream s;
        s.span(2, 2, 0, 0);
        s.close(1);
        s.stream.clear();
        QVERIFY(!s.stream.active());
        QCOMPARE(s.selected(0), QList<int>());
        QCOMPARE(s.selected(2), QList<int>());
        s.open(1);
        QCOMPARE(s.selected(1), QList<int>());
        QCOMPARE(s.stream.copy().value(u"reason"_s).toString(), u"empty"_s);
    }

    void backInTheAnchorDayIsThatDayAlone()
    {
        Stream s;
        s.span(0, 1, 2, 2);
        QVERIFY(s.stream.extendTo(&s.page(0), 3)); // Shift+click back in the anchor day
        QVERIFY(!s.stream.active());
        QCOMPARE(s.selected(0), QList<int>({1, 2, 3}));
        QCOMPARE(s.selected(1), QList<int>());
        QCOMPARE(s.selected(2), QList<int>());
        QCOMPARE(s.page(0).selectionAnchorRow(), 1);
        // From there again into another day: the same anchor.
        QVERIFY(s.stream.extendTo(&s.page(1), 1));
        QCOMPARE(s.stream.anchorRow(), 1);
        QCOMPARE(s.selected(0), QList<int>({1, 2, 3}));
        QCOMPARE(s.selected(1), QList<int>({1}));
    }

    void fromAnEditedBlock()
    {
        Stream s;
        s.page(1).startEditing(2, 0);
        QVERIFY(s.stream.extendTo(&s.page(2), 1)); // Shift+click in the next day
        QCOMPARE(s.page(1).editingRow(), -1);
        QCOMPARE(s.selected(1), QList<int>({2}));
        QCOMPARE(s.selected(2), QList<int>({1}));
        // Shift+↓ on the last line of the last block of a day.
        s.stream.clear();
        s.page(0).startEditing(3, 0);
        QVERIFY(s.stream.extendBy(&s.page(0), 1));
        QCOMPARE(s.page(0).editingRow(), -1);
        QCOMPARE(s.selected(0), QList<int>({3}));
        QCOMPARE(s.selected(1), QList<int>({1}));
    }

    void dragIntoAnotherDay()
    {
        Stream s;
        // Pressed on Poranek, dragged into the second day and on into the third.
        QVERIFY(s.stream.extendFrom(&s.page(0), 0, &s.page(1), 2));
        QCOMPARE(s.selected(0), QList<int>({0, 1, 2, 3}));
        QCOMPARE(s.selected(1), QList<int>({1, 2}));
        QVERIFY(s.stream.extendFrom(&s.page(0), 0, &s.page(2), 2));
        QCOMPARE(s.selected(1), QList<int>({1, 2}));
        QCOMPARE(s.selected(2), QList<int>({1, 2}));
        // Back up into the second day: the third leaves the span.
        QVERIFY(s.stream.extendFrom(&s.page(0), 0, &s.page(1), 1));
        QCOMPARE(s.selected(1), QList<int>({1}));
        QCOMPARE(s.selected(2), QList<int>());
        // Within the pressed day: not the stream's (the day selects itself).
        QVERIFY(!s.stream.extendFrom(&s.page(0), 0, &s.page(0), 2));
    }

    void keysAcrossTheDaysEdges()
    {
        Stream s;
        QSignalSpy moved(&s.stream, &StreamSelection::focusMoved);
        s.page(0).selectRange(3, 3); // Wieczór, the day's last block
        QVERIFY(!s.page(0).extendSelection(1)); // the day's own step: nowhere to go
        QVERIFY(s.stream.extendBy(&s.page(0), 1));
        QCOMPARE(s.selected(0), QList<int>({3}));
        QCOMPARE(s.selected(1), QList<int>({1})); // past the page properties
        QCOMPARE(moved.count(), 1);
        QVERIFY(moved.last().at(0).value<PageController *>() == &s.page(1));
        QCOMPARE(moved.last().at(1).toInt(), 1);
        QVERIFY(moved.last().at(2).toBool()); // keys: scrolled into view
        QVERIFY(s.stream.extendBy(&s.page(0), 1));
        QCOMPARE(s.selected(1), QList<int>({1, 2}));
        QVERIFY(s.stream.extendBy(&s.page(1), 1));
        QCOMPARE(s.selected(2), QList<int>({1})); // the document's heading
        QCOMPARE(s.stream.pageCount(), 3);
        // Back up: the third day leaves, then the second shrinks.
        QVERIFY(s.stream.extendBy(&s.page(2), -1));
        QCOMPARE(s.selected(2), QList<int>());
        QCOMPARE(s.selected(1), QList<int>({1, 2}));
        QCOMPARE(s.stream.focusRow(), 2);
        QVERIFY(s.stream.extendBy(&s.page(1), -1));
        QCOMPARE(s.selected(1), QList<int>({1}));
        // Into the anchor day again: that day alone.
        QVERIFY(s.stream.extendBy(&s.page(1), -1));
        QVERIFY(!s.stream.active());
        QCOMPARE(s.selected(0), QList<int>({3}));
        QCOMPARE(s.selected(1), QList<int>());
        // The stream's ends: today has no note, nothing above it.
        s.page(0).selectRange(0, 0);
        QVERIFY(!s.stream.extendBy(&s.page(0), -1));
        s.page(2).selectRange(3, 3);
        QVERIFY(!s.stream.extendBy(&s.page(2), 1));
        QVERIFY(!s.stream.active());
        // Nothing selected or edited: no span.
        s.page(2).clearSelection();
        QVERIFY(!s.stream.extendBy(&s.page(2), -1));
    }

    void focusDayAwayKeysScrollThereFirst()
    {
        Stream s;
        s.span(0, 3, 2, 1);
        s.close(2);
        QSignalSpy reveal(&s.stream, &StreamSelection::revealRequested);
        QVERIFY(s.stream.extendBy(&s.page(0), 1));
        QCOMPARE(reveal.count(), 1);
        QCOMPARE(reveal.last().at(0).toString(), Day3);
        QCOMPARE(s.stream.focusRow(), 1); // unchanged until the day exists
    }

    void daysLeavingTheStream()
    {
        Stream s;
        s.span(0, 3, 2, 3);
        // The day between leaves the list (its note deleted): the span goes on without it.
        s.model.removeRow(2); // Day2
        QVERIFY(s.stream.active());
        QCOMPARE(s.stream.pageCount(), 2);
        QVERIFY(!s.stream.markdown().contains(u"Źdźbło"_s));
        QCOMPARE(s.selected(1), QList<int>()); // no longer in the stream's span
        // An end of the span leaves: the span ends.
        s.model.removeRow(2); // Day3
        QVERIFY(!s.stream.active());
        QCOMPARE(s.selected(0), QList<int>());
    }

    void tooLargeIsRefused()
    {
        QString big;
        const QString line = u"- "_s + QString(200, u'ż') + u'\n'; // 403 bytes in UTF-8
        for (qsizetype n = 0; n < PageController::ClipboardLimit / 403 + 10; ++n)
            big += line;
        Stream s(Content1, big, Content3);
        s.span(0, 3, 2, 1);
        s.close(1); // the big day between, from its file
        QGuiApplication::clipboard()->setText(u"poprzednie"_s);
        const QVariantMap r = s.stream.copy();
        QVERIFY(!r.value(u"ok"_s).toBool());
        QCOMPARE(r.value(u"reason"_s).toString(), u"tooLarge"_s);
        QCOMPARE(clipboardText(), u"poprzednie"_s);
        QVERIFY(s.stream.active());
    }
};

QTEST_MAIN(TestStreamSelection)
#include "tst_editor_streamselection.moc"
