// JournalDayItem (src/app/editor/journaldayitem.h): a journal day that is not edited drawn by one
// item. Which days it draws (plain blocks — text, headings, tasks, quotes, rules — and nothing it
// cannot: tables, code, pictures, embedded pages, properties, callouts, queries; a day being
// edited, selected or zoomed), the rows' geometry by BlockDelegate's formulas (outline and
// document, headings, tasks with their "Add date" room, depth), what is under a point (text,
// bullet, task box, fold arrow, number, the strip under the blocks), links, hover (the fold arrow
// of the hovered row, the link underlined in its row only), the model followed (a fold, a task, a
// change from elsewhere) without laying out texts that did not change, the sprites, and a frame
// drawn offscreen. Polish text throughout.
#include "blocklistmodel.h"
#include "fakeeditorhost.h"
#include "journaldayitem.h"
#include "pagecontroller.h"

#include <QPointer>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <memory>

using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Day = u"journals/2026-10-14.md"_s;

/// The metrics of a desktop day's editor (BlockEditor with Theme's defaults; 680 px of text).
void setMetrics(JournalDayItem &item, bool documentMode = false, bool touch = false)
{
    item.setProperty("documentMode", documentMode);
    item.setProperty("touchUi", touch);
    item.setProperty("contentX", 32.0);
    item.setProperty("contentWidth", 680.0);
    item.setProperty("gutter", documentMode ? 0.0 : 36.0);
    item.setProperty("indentStep", 26.0);
    item.setProperty("bulletColumn", touch ? 22.0 : 20.0);
    item.setProperty("arrowColumn", touch ? 18.0 : 16.0);
    item.setProperty("listIndent", 26.0);
    item.setProperty("taskBoxWidth", touch ? 26.0 : 24.0);
    item.setProperty("paragraphGap", 11.0);
    item.setProperty("listGap", touch ? 4.0 : 2.0);
    item.setProperty("headingGapBelow", 9.0);
    item.setProperty("headingGapsAbove", QVariant::fromValue(QList<qreal>{0, 27, 23, 19, 16, 16, 16}));
    item.setProperty("fontSizes", QVariant::fromValue(QList<qreal>{15, 24, 20, 17, 15, 15, 15}));
    item.setProperty("lineHeights", QVariant::fromValue(QList<qreal>{25, 34, 28, 24, 25, 25, 25}));
    item.setProperty("firstLineCenters", QVariant::fromValue(QList<qreal>{16.5, 22, 18.5, 16, 16.5, 16.5, 16.5}));
    item.setProperty("spaces", QVariant::fromValue(QList<qreal>{4, 8, 12, 16, 20, 24}));
    item.setProperty("touchTarget", touch ? 44.0 : 28.0);
    item.setProperty("iconSize", 14.0);
    item.setProperty("boxRadius", 3.0);
    item.setProperty("footerHeight", 12.0);
}

/// A day's page and its item, laid out as QML creates it (properties first, then complete).
struct Fixture {
    FakeEditorHost host;
    PageController page;
    std::unique_ptr<JournalDayItem> item;

    explicit Fixture(const QString &markdown, bool documentMode = false, bool touch = false, bool readOnly = false)
    {
        host.setFile(Day, markdown);
        host.setFile(u"Kraków.md"_s, u"- miasto\n"_s);
        page.setNow(u"2026-10-14 10:00"_s);
        page.setHost(&host);
        page.setPath(Day);
        item = std::make_unique<JournalDayItem>();
        static_cast<QQmlParserStatus *>(item.get())->classBegin();
        setMetrics(*item, documentMode, touch);
        item->setProperty("readOnly", readOnly);
        item->setWidth(744);
        item->setModel(page.blocks());
        item->componentComplete();
    }
    QVariantMap geometry(int row) const { return item->rowGeometry(row); }
    qreal value(int row, const char *key) const { return geometry(row).value(QString::fromLatin1(key)).toReal(); }
};

} // namespace

class TestJournalDayItem : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void drawable_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<bool>("drawable");
        QTest::newRow("outline with links and tags")
                << u"- Kawa z Basią w [[Kraków]] #dom\n  - zażółć gęślą jaźń\n"_s << true;
        QTest::newRow("tasks with dates")
                << u"- [ ] Zadzwonić do hydraulika 📅 2026-10-20\n- [x] Zamówić farbę\n- [-] Odwołane\n"_s << true;
        QTest::newRow("heading and items") << u"## Plan dnia\n- spacer nad rzeką\n- **czytanie**\n"_s << true;
        QTest::newRow("quote") << u"- > Cytat z książki o historii Krakowa\n"_s << true;
        QTest::newRow("rule") << u"Pierwszy akapit.\n\n***\n\nDrugi akapit.\n"_s << true;
        QTest::newRow("note mode only") << u"---\nbrulion: document\n---\nAkapit dnia.\n"_s << true;
        QTest::newRow("empty day") << QString() << true;
        QTest::newRow("table") << u"- | Rzecz | Ilość |\n  |---|---:|\n  | farba | 2 |\n"_s << false;
        QTest::newRow("code") << u"- ```python\n  def suma(a, b):\n      return a + b\n  ```\n"_s << false;
        QTest::newRow("query") << u"- ```query\n  task:open\n  ```\n"_s << false;
        QTest::newRow("picture") << u"- Szkic: ![[assets/plan.png]]\n"_s << false;
        QTest::newRow("embedded page") << u"- ![[Kraków]]\n"_s << false;
        QTest::newRow("callout") << u"> [!info] Uwaga\n> treść\n"_s << false;
        QTest::newRow("properties") << u"---\ntags: [praca]\n---\n- treść\n"_s << false;
        QTest::newRow("formula block") << u"```math\nx^2\n```\n"_s << false;
    }
    void drawable()
    {
        QFETCH(QString, markdown);
        QFETCH(bool, drawable);
        Fixture f(markdown);
        QCOMPARE(f.item->drawable(), drawable);
        if (!drawable) {
            // Nothing of the rows is kept: the editor's rows take over.
            QCOMPARE(f.item->rows().size(), size_t(0));
            QCOMPARE(f.item->childItems().size(), 0);
        }
    }

    void blockedAndEditing()
    {
        Fixture f(u"- pierwszy\n- drugi\n"_s);
        QVERIFY(f.item->drawable());
        QSignalSpy spy(f.item.get(), &JournalDayItem::drawableChanged);
        f.item->setBlocked(true);
        QVERIFY(!f.item->drawable());
        QCOMPARE(spy.count(), 1);
        f.item->setBlocked(false);
        QVERIFY(f.item->drawable());
        // A block being edited is the editor's.
        f.page.startEditing(1, -1);
        QVERIFY(!f.item->drawable());
        f.page.stopEditing();
        QVERIFY(f.item->drawable());
        // So is a selected one.
        f.page.selectRange(0, 1);
        QVERIFY(!f.item->drawable());
    }

    /// A block with a styled tag (b.app.tagStyle): the editor's rows draw its tint
    /// (BlockPartTagTint), so the day is theirs while a style tints one of its blocks.
    void tintedBlockIsTheEditors()
    {
        Fixture f(u"- zwykły punkt\n- zrobić #pilne\n"_s);
        QVERIFY(f.item->drawable());
        f.host.setTagStyle(u"pilne"_s, u"danger"_s);
        QVERIFY(!f.item->drawable());
        f.host.setTagStyle(u"pilne"_s, QString());
        QVERIFY(f.item->drawable());
    }

    // The text items of rows no longer drawn leave the scene at once and are deleted later: QML
    // may hold one (textItemOf(), a self-test's compiled locals, read by the garbage collector
    // until its function returns) — a tap that made the day editable deleted it under the
    // span self-test, and a collection while the editor's rows were made read the freed item.
    void textsDeletedLater()
    {
        Fixture f(u"- pierwszy z listą\n- drugi\n"_s);
        QVERIFY(f.item->drawable());
        QPointer<RenderedTextItem> text = f.item->textItemOf(0);
        QVERIFY(text);
        QCOMPARE(text->parentItem(), f.item.get());
        f.page.startEditing(1, -1); // the editor's rows take over
        QVERIFY(!f.item->drawable());
        QVERIFY(f.item->textItemOf(0) == nullptr);
        QVERIFY(text); // not yet
        QCOMPARE(text->parentItem(), nullptr);
        QVERIFY(!text->isVisible());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!text);
    }

    void outlineGeometry()
    {
        // contentX 32 + gutter 36: the text column at 68; a level 26 px in; bullets centred 10 px
        // left of their level's column.
        Fixture f(u"- Pierwszy blok dnia\n  - [ ] zadanie bez daty\n  - [x] zrobione\n- ## Nagłówek\n"_s);
        QVERIFY(f.item->drawable());
        QCOMPARE(f.item->rows().size(), size_t(4));
        // Row 0: a list item at depth 0, two pixels of air above and below one line.
        QCOMPARE(f.value(0, "y"), 0.0);
        QCOMPARE(f.value(0, "topPad"), 2.0);
        QCOMPARE(f.value(0, "levelX"), 68.0);
        QCOMPARE(f.value(0, "bulletX"), 58.0);
        QCOMPARE(f.value(0, "textX"), 68.0);
        QCOMPARE(f.value(0, "textWidth"), 712.0 - 68.0);
        QCOMPARE(f.value(0, "height"), 2.0 + 25.0 + 2.0);
        QCOMPARE(f.value(0, "firstLineCenter"), 2.0 + 16.5);
        // Row 1: an open task without a date: after its box, the "Add date" room at the end.
        QCOMPARE(f.value(1, "y"), 29.0);
        QCOMPARE(f.value(1, "levelX"), 94.0);
        QCOMPARE(f.value(1, "textX"), 94.0 + 24.0);
        QCOMPARE(f.value(1, "dateButtonRoom"), 28.0);
        QCOMPARE(f.value(1, "textWidth"), 712.0 - 28.0 - 118.0);
        // Row 2: done: no room for a date.
        QCOMPARE(f.value(2, "dateButtonRoom"), 0.0);
        QCOMPARE(f.value(2, "textWidth"), 712.0 - 118.0);
        // Row 3: a level-2 heading after other rows: 16 px above, 4 px below, its own line height.
        QCOMPARE(f.value(3, "topPad"), 16.0);
        QCOMPARE(f.value(3, "height"), 16.0 + 28.0 + 4.0);
        QCOMPARE(f.value(3, "firstLineCenter"), 16.0 + 18.5);
        QCOMPARE(f.item->contentHeight(), 29.0 * 3 + 48.0);
        QCOMPARE(f.item->implicitHeight(), f.item->contentHeight());
        // Reading mode: no "Add date" room.
        Fixture reading(u"- [ ] zadanie bez daty\n"_s, false, false, true);
        QCOMPARE(reading.value(0, "dateButtonRoom"), 0.0);
    }

    void wrapsLongText()
    {
        QString words;
        for (int i = 0; i < 40; ++i)
            words += u"zażółć gęślą jaźń "_s;
        Fixture f(u"- "_s + words.trimmed() + u"\n- krótki\n"_s);
        const int lines = f.geometry(0).value(u"lines"_s).toInt();
        QVERIFY2(lines >= 3, qPrintable(QString::number(lines)));
        QCOMPARE(f.value(0, "height"), 2.0 + lines * 25.0 + 2.0);
        QCOMPARE(f.value(1, "y"), f.value(0, "height"));
        // A narrower column breaks it into more lines (the editor's contentWidth).
        f.item->setProperty("contentWidth", 300.0);
        QVERIFY(f.geometry(0).value(u"lines"_s).toInt() > lines);
        QCOMPARE(f.value(1, "y"), f.value(0, "height"));
    }

    void documentGeometry()
    {
        // Document: paragraphs at the text column, gaps by the block before; numbers right-aligned in
        // the marker column.
        Fixture f(u"---\nbrulion: document\n---\n# Dzień\n\nAkapit pierwszy.\n\n1. jeden\n2. dwa\n"_s, true);
        QVERIFY(f.item->drawable());
        int heading = -1, paragraph = -1, first = -1, second = -1;
        for (int i = 0; i < int(f.item->rows().size()); ++i) {
            const JournalDayItem::Row &r = f.item->rows()[size_t(i)];
            if (!r.shown)
                continue;
            if (r.view == u"heading")
                heading = i;
            else if (r.kind == u"paragraph")
                paragraph = i;
            else if (r.kind == u"listItem")
                (first < 0 ? first : second) = i;
        }
        QVERIFY(heading >= 0 && paragraph > heading && first > paragraph && second > first);
        QCOMPARE(f.value(heading, "topPad"), 0.0);   // the first shown block
        QCOMPARE(f.value(paragraph, "topPad"), 9.0); // under a heading
        QCOMPARE(f.value(first, "topPad"), 11.0);    // a paragraph's gap
        QCOMPARE(f.value(second, "topPad"), 2.0);    // list items close together
        QCOMPARE(f.value(first, "levelX"), 32.0 + 26.0);
        const JournalDayItem::Row &one = f.item->rows()[size_t(first)];
        QVERIFY(one.numberItem);
        QCOMPARE(one.numberItem->plainText(), u"1."_s);
        QCOMPARE(one.numberItem->x() + one.numberItem->width() + std::round(26 * 0.25), 58.0);
        // A document's items have no bullet: the number is the marker.
        const QVariantMap g = f.geometry(first);
        QCOMPARE(f.item->partAt(57, g.value(u"y"_s).toReal() + g.value(u"firstLineCenter"_s).toReal()), u"number"_s);
    }

    void parts()
    {
        Fixture f(u"- Rodzic z [[Kraków]]\n  - dziecko\n- [ ] zadanie\n"_s);
        const qreal cy0 = f.value(0, "firstLineCenter");
        // The bullet (24 px wide, its right edge at the text column), the text, the margin left of
        // both.
        QCOMPARE(f.item->partAt(58, cy0), u"bullet"_s);
        QCOMPARE(f.item->partAt(80, cy0), u"text"_s);
        QCOMPARE(f.item->partAt(5, cy0), u"row"_s);
        // The fold arrow shows only on hover (mouse): then it is there to click.
        QCOMPARE(f.item->partAt(36, cy0), u"row"_s);
        f.item->setHover(80, cy0);
        QCOMPARE(f.item->hoveredRow(), 0);
        QCOMPARE(f.item->partAt(36, cy0), u"arrow"_s);
        f.item->clearHover();
        // The task box after the bullet.
        const qreal cy2 = f.value(2, "y") + f.value(2, "firstLineCenter");
        QCOMPARE(f.item->partAt(68 + 10, cy2), u"taskBox"_s);
        // The strip under the blocks, nothing below it.
        QCOMPARE(f.item->partAt(100, f.item->contentHeight() + 2), u"footer"_s);
        QCOMPARE(f.item->partAt(100, f.item->contentHeight() + 40), QString());
        QCOMPARE(f.item->rowAt(f.item->contentHeight() + 2), -1);
        // A link under the pointer.
        const RenderedTextItem *text = f.item->textItemOf(0);
        QVERIFY(text);
        const QRectF link = text->rectForSource(int(u"Rodzic z "_s.size()) + 2, int(u"Rodzic z "_s.size()) + 4);
        QVERIFY(!link.isEmpty());
        QCOMPARE(f.item->linkAt(text->x() + link.center().x(), text->y() + link.center().y()), u"page:Krak%C3%B3w"_s);
        QCOMPARE(f.item->linkAt(text->x() + 2, text->y() + 2), QString());
    }

    void touchParts()
    {
        // Touch: fold arrows always shown, 44 px targets.
        Fixture f(u"- Rodzic\n  - dziecko\n"_s, false, true);
        const qreal cy = f.value(0, "firstLineCenter");
        bool arrow = false;
        for (const JournalDayItem::Decoration &d : f.item->decorations())
            arrow = arrow || d.sprite.startsWith(u"icon|chevron-right"_s);
        QVERIFY(arrow);
        QCOMPARE(f.item->partAt(f.value(0, "bulletX"), cy), u"bullet"_s);
        // The menu of the bullet opens under its 44 px hit area.
        QCOMPARE(f.item->menuPoint(0, u"bullet"_s), QPointF(f.value(0, "bulletX"), cy + 22));
    }

    void hover()
    {
        Fixture f(u"- Rodzic z [[Kraków]]\n  - dziecko z [[Kraków]]\n"_s);
        const int layouts = f.item->textLayouts();
        const auto chevrons = [&f] {
            int n = 0;
            for (const JournalDayItem::Decoration &d : f.item->decorations())
                n += d.sprite.startsWith(u"icon|chevron-right"_s) ? 1 : 0;
            return n;
        };
        QCOMPARE(chevrons(), 0);
        f.item->setHover(100, f.value(0, "firstLineCenter"));
        QCOMPARE(chevrons(), 1);
        // The link is underlined in the hovered row only.
        f.item->setHoveredLink(u"page:Krak%C3%B3w"_s);
        QCOMPARE(f.item->textItemOf(0)->hoveredLink(), u"page:Krak%C3%B3w"_s);
        QCOMPARE(f.item->textItemOf(1)->hoveredLink(), QString());
        f.item->setHover(100, f.value(1, "y") + 5);
        QCOMPARE(f.item->textItemOf(0)->hoveredLink(), QString());
        QCOMPARE(f.item->textItemOf(1)->hoveredLink(), u"page:Krak%C3%B3w"_s);
        f.item->clearHover();
        QCOMPARE(chevrons(), 0);
        QCOMPARE(f.item->textItemOf(1)->hoveredLink(), QString());
        // Hovering lays no text out again.
        QCOMPARE(f.item->textLayouts(), layouts);
    }

    void followsTheModel()
    {
        Fixture f(u"- Rodzic\n  - dziecko pierwsze\n  - dziecko drugie\n- [ ] zadanie\n"_s);
        const qreal full = f.item->contentHeight();
        const int layouts = f.item->textLayouts();
        // A fold: the children are hidden, the day is shorter, no text laid out again.
        QSignalSpy heights(f.item.get(), &JournalDayItem::contentHeightChanged);
        f.page.toggleFold(0);
        QVERIFY(heights.count() >= 1);
        QCOMPARE(f.item->contentHeight(), full - 2 * 29.0);
        QCOMPARE(f.item->shownRows(), 2);
        QCOMPARE(f.item->textLayouts(), layouts);
        // The folded bullet's halo.
        bool halo = false;
        for (const JournalDayItem::Decoration &d : f.item->decorations())
            halo = halo || d.sprite.startsWith(u"dot|16|"_s);
        QVERIFY(halo);
        f.page.toggleFold(0);
        QCOMPARE(f.item->contentHeight(), full);
        // The task box clicked: done (the text in the faint colour, the box filled).
        f.page.toggleTask(3);
        QCOMPARE(f.item->rows()[3].taskStatus, u"done"_s);
        bool doneBox = false;
        for (const JournalDayItem::Decoration &d : f.item->decorations())
            doneBox = doneBox || d.sprite.startsWith(u"box|done|"_s);
        QVERIFY(doneBox);
        QTRY_VERIFY(f.host.file(Day).contains(u"- [x] zadanie"_s));
        // A change from elsewhere: the new block is drawn; one that the item cannot draw ends it.
        QTRY_VERIFY(!f.page.dirty()); // (the task saved: another change merges into a saved page)
        f.host.externalWrite(Day, u"- Rodzic\n- nowy blok z innego urządzenia\n"_s);
        QVERIFY(f.item->drawable());
        QCOMPARE(f.item->shownRows(), 2);
        // (A one-letter word keeps its next word on its line: a no-break space, as the rich text.)
        QCOMPARE(f.item->textItemOf(1)->plainText(), u"nowy blok z\u00a0innego urządzenia"_s);
        f.host.externalWrite(Day, u"- Rodzic\n- | a | b |\n  |---|---|\n  | 1 | 2 |\n"_s);
        QVERIFY(!f.item->drawable());
    }

    void metricsLayOutAgain()
    {
        Fixture f(u"- zażółć gęślą jaźń\n"_s);
        const qreal before = f.value(0, "height");
        f.item->setProperty("lineHeights", QVariant::fromValue(QList<qreal>{30, 34, 28, 24, 30, 30, 30}));
        QCOMPARE(f.value(0, "height"), 2.0 + 30.0 + 2.0);
        QVERIFY(f.value(0, "height") != before);
        f.item->setProperty("touchUi", true);
        QCOMPARE(f.value(0, "topPad"), 4.0);
    }

    void finiteGeometry_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<bool>("touch");
        const QString day =
                u"- Spotkanie z zespołem o synchronizacji #praca\n- [ ] Wysłać notatki 📅 2026-10-16 🔁 co "
                u"tydzień\n- Lista zakupów:\n  - mleko\n  - chleb\n    - razowy\n- Link do [[Kraków]] i #pomysł\n"_s;
        QTest::newRow("chips, nesting") << day << false;
        QTest::newRow("chips, nesting, touch") << day << true;
    }
    void finiteGeometry()
    {
        // Every rectangle drawn is finite (a NaN or an infinite one makes the software renderer
        // paint nothing more).
        QFETCH(QString, markdown);
        QFETCH(bool, touch);
        Fixture f(markdown, false, touch);
        QVERIFY(f.item->drawable());
        for (const JournalDayItem::Decoration &d : f.item->decorations()) {
            const QRectF r = d.rect;
            QVERIFY2(std::isfinite(r.x()) && std::isfinite(r.y()) && std::isfinite(r.width())
                             && std::isfinite(r.height()) && r.width() < 2000 && r.height() < 2000,
                     qPrintable(
                             u"%1: %2,%3 %4x%5"_s.arg(d.sprite).arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height())));
        }
        for (QQuickItem *child : f.item->childItems()) {
            const QRectF r(child->position(), child->size());
            QVERIFY2(std::isfinite(r.x()) && std::isfinite(r.y()) && r.width() < 2000 && r.height() < 2000,
                     qPrintable(u"%1,%2 %3x%4"_s.arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height())));
        }
    }

    void sprites()
    {
        QVERIFY(!JournalDayItem::spriteImage(u"dot|6|ff336699"_s, 1).isNull());
        const QImage box = JournalDayItem::spriteImage(u"box|todo|16|4|0|ff888888|1.5|ffffffff|ff666666"_s, 2);
        QCOMPARE(box.size(), QSize(32, 32));
        // The border is drawn, the inside is empty.
        QVERIFY(qAlpha(box.pixel(1, 16)) > 0);
        QCOMPARE(qAlpha(box.pixel(16, 16)), 0);
        const QImage done = JournalDayItem::spriteImage(u"box|done|16|4|ff22aa55|ff22aa55|0|ffffffff|ff666666"_s, 1);
        QVERIFY(qAlpha(done.pixel(2, 8)) > 0);
        QVERIFY(JournalDayItem::spriteImage(u"nonsense"_s, 1).isNull());
    }

    void drawsAFrame()
    {
        QQuickWindow window;
        window.resize(760, 200);
        window.setColor(Qt::white);
        FakeEditorHost host;
        host.setFile(Day, u"- Rodzic\n  - dziecko [[Kraków]]\n- [x] zrobione\n"_s);
        PageController page;
        page.setHost(&host);
        page.setPath(Day);
        auto *item = new JournalDayItem(window.contentItem());
        static_cast<QQmlParserStatus *>(item)->classBegin();
        setMetrics(*item);
        item->setProperty("textColor", QColor(Qt::black));
        item->setProperty("mutedColor", QColor(Qt::black));
        item->setProperty("guideColor", QColor(Qt::black));
        item->setProperty("successColor", QColor(0, 160, 0));
        item->setWidth(760);
        item->setModel(page.blocks());
        item->componentComplete();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const QImage frame = window.grabWindow();
        QVERIFY(!frame.isNull());
        const qreal dpr = frame.devicePixelRatio();
        const auto at = [&](qreal x, qreal y) { return frame.pixelColor(int(x * dpr), int(y * dpr)); };
        // The first bullet's dot, the guide line through the child's row, the done task's green box.
        const qreal cy = item->rowGeometry(0).value(u"firstLineCenter"_s).toReal();
        QVERIFY2(qGray(at(58, cy).rgb()) < 128, qPrintable(at(58, cy).name()));
        const QVariantMap child = item->rowGeometry(1);
        QVERIFY2(qGray(at(58, child.value(u"y"_s).toReal() + 3).rgb()) < 200, qPrintable(at(58, 3).name()));
        const QVariantMap task = item->rowGeometry(2);
        const QColor box = at(68 + 2 + 8, task.value(u"y"_s).toReal() + task.value(u"firstLineCenter"_s).toReal() - 5);
        QVERIFY2(box.green() > box.red() + 40, qPrintable(box.name()));
        // The text is drawn too (dark pixels on the first line).
        bool dark = false;
        for (int x = 70; x < 140 && !dark; ++x)
            dark = qGray(at(x, cy).rgb()) < 100 || qGray(at(x, cy - 3).rgb()) < 100;
        QVERIFY(dark);
    }
};

QTEST_MAIN(TestJournalDayItem)
#include "tst_editor_journalday.moc"
