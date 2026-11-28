// PageItem (src/app/editor/pageitem.h; docs/UI.md "One item per page (Z10 stage C)"): a page's
// blocks as one item over a Flickable, in an offscreen window with the software renderer — only the
// rows on screen and near it laid out (also after a width change), the Flickable's contentY and
// contentHeight driven by the item and its contentY read back, the anchor keeping what is on screen
// while rows above are measured or edited, "scroll to block" exact on estimated rows, rich rows
// (code, tasks) hosted by a QML delegate with the row's roles and index and released when scrolled
// away, rows with a styled tag hosted (their delegate draws the tint), folds, links and text of
// drawn rows, and a frame drawn.
#include "blocklistmodel.h"
#include "editorhost.h"
#include "fakeeditorhost.h"
#include "markdown/inlinerenderer.h"
#include "pageitem.h"
#include "renderedtextitem.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlParserStatus>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTest>

#include <cmath>
#include <memory>

using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

constexpr int ViewWidth = 600;
constexpr int ViewHeight = 400;

QString outline(int blocks)
{
    QStringList lines;
    for (int k = 0; k < blocks; ++k) {
        if (k % 7 == 3)
            lines << u"  - zagnieżdżony punkt %1: źdźbło trawy i gęś"_s.arg(k);
        else if (k % 11 == 5)
            lines << u"- %1 Długi punkt, który zawija się na kilka linii: zażółć gęślą jaźń, pchnąć w tę łódź jeża "
                     "lub ośm skrzyń fig, a potem jeszcze raz to samo, żeby na pewno był dłuższy niż jedna linia "
                     "w kolumnie tekstu."_s.arg(k);
        else
            lines << u"- punkt %1 o [[Stronie]] i #tagu"_s.arg(k);
    }
    return lines.join(u'\n') + u'\n';
}

} // namespace

class TstEditorPageItem : public QObject {
    Q_OBJECT

    QQmlEngine *m_engine = nullptr;
    std::unique_ptr<QQuickWindow> m_window;
    std::unique_ptr<BlockListModel> m_model;
    QQuickItem *m_flick = nullptr;
    PageItem *m_page = nullptr;
    QQmlComponent *m_delegate = nullptr;

    void setUpPage(const QString &content)
    {
        m_window = std::make_unique<QQuickWindow>();
        m_window->resize(ViewWidth, ViewHeight);
        m_window->setColor(Qt::white);
        QQmlComponent flickable(m_engine);
        flickable.setData("import QtQuick\nFlickable { width: 600; height: 400; contentWidth: width }", {});
        m_flick = qobject_cast<QQuickItem *>(flickable.create());
        QVERIFY2(m_flick, qPrintable(flickable.errorString()));
        m_flick->setParentItem(m_window->contentItem());
        m_delegate = new QQmlComponent(m_engine, m_window.get());
        // A hosted row: the roles the editor's BlockDelegate takes, a code block taller than text.
        m_delegate->setData("import QtQuick\nItem { required property int index; required property bool shown;"
                            " required property string view; required property string taskStatus;"
                            " required property bool isEditing; property var model;"
                            " width: 600; height: view === \"code\" ? 120 : 40 }",
                            {});
        QVERIFY2(m_delegate->isReady(), qPrintable(m_delegate->errorString()));
        m_model = std::make_unique<BlockListModel>();
        m_model->resetContent(content);
        m_page = new PageItem(m_window->contentItem());
        m_page->setSize(QSizeF(ViewWidth, ViewHeight));
        m_page->setContentX(20);
        m_page->setContentWidth(560);
        m_page->setFontSizes({15, 26, 22, 18, 16, 15, 15});
        m_page->setLineHeights({24, 36, 30, 26, 24, 24, 24});
        m_page->setFirstLineCenters({13, 19, 16, 14, 13, 13, 13});
        m_page->setDelegate(m_delegate);
        m_page->setFlickable(m_flick);
        m_page->setModel(m_model.get());
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        m_page->forceLayout();
    }
    void scrollTo(qreal y)
    {
        m_flick->setProperty("contentY", y);
        m_page->forceLayout();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        m_engine = new QQmlEngine(this);
    }
    void cleanup()
    {
        delete m_page;
        m_page = nullptr;
        m_window.reset();
        m_model.reset();
        m_flick = nullptr;
    }

    /// RenderedTextLayout without an item lays out and hit-tests exactly as RenderedTextItem (the
    /// code the two share).
    void sharedLayoutMatchesTheItem_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<qreal>("width");
        QTest::addColumn<int>("maxLines");
        const QString text = u"Zażółć gęślą jaźń — w domu i w ogrodzie, z **pogrubieniem**, `kodem`, #tagiem, "
                             "[[Dom]] i ==zakreśleniem== 📅 2026-10-20, bo tekst musi się zawinąć."_s;
        QTest::newRow("wide") << text << 900.0 << 0;
        QTest::newRow("narrow") << text << 180.0 << 0;
        QTest::newRow("cut after two lines") << text << 180.0 << 2;
        QTest::newRow("one word") << u"źdźbło"_s << 300.0 << 0;
        QTest::newRow("empty") << QString() << 300.0 << 0;
    }
    void sharedLayoutMatchesTheItem()
    {
        QFETCH(QString, markdown);
        QFETCH(qreal, width);
        QFETCH(int, maxLines);
        brulion::markdown::RenderContext context;
        context.today = QDate(2026, 10, 14);
        context.dateLinks = true;
        const auto runs = std::make_shared<const brulion::markdown::RenderedText>(
                brulion::markdown::InlineRenderer(context).renderRuns(markdown));
        QFont font;
        font.setPixelSize(15);
        RenderedTextItem it;
        static_cast<QQmlParserStatus *>(&it)->classBegin();
        it.setRenderedText({runs});
        it.setFont(font);
        it.setLineHeight(26);
        it.setWidth(width);
        it.setMaximumLineCount(maxLines);
        it.componentComplete();
        RenderedTextLayout layout;
        RenderedTextLayout::Style style;
        style.font = font;
        style.lineHeight = 26;
        style.maximumLineCount = maxLines;
        layout.setStyle(style);
        layout.layout(*runs, width);
        QCOMPARE(layout.laidOutHeight(), it.implicitHeight());
        QCOMPARE(layout.lines(), it.lineCount());
        QCOMPARE(layout.isTruncated(), it.truncated());
        QCOMPARE(layout.plainText(), it.plainText());
        QVERIFY(layout.shapes() == it.shapes());
        QVERIFY(layout.icons() == it.iconShapes());
        for (qreal y = 2; y < it.implicitHeight(); y += 13) {
            for (qreal x = 0; x < width; x += 7) {
                QCOMPARE(layout.linkAt(x, y), it.linkAt(x, y));
                QCOMPARE(layout.positionAt(x, y), it.positionAt(x, y));
            }
        }
        QVERIFY(layout.keepsLayoutAt(width));
    }

    /// 3000 rows: those on screen and in the margins are laid out, not the page.
    void laysOutOnlyWhatIsShown()
    {
        setUpPage(outline(3000));
        QCOMPARE(m_model->count(), 3000);
        QVERIFY(m_page->drawnRowCount() >= 8);
        QVERIFY2(m_page->drawnRowCount() <= ViewHeight / 24 + 1, qPrintable(QString::number(m_page->drawnRowCount())));
        QCOMPARE(m_page->firstShownRow(), 0);
        QVERIFY2(m_page->layoutCount() < 60, qPrintable(QString::number(m_page->layoutCount())));
        QVERIFY(m_page->measuredRows() < 80);
        QVERIFY(m_page->contentHeight() > 3000 * 24);
        // The Flickable scrolls the estimated page.
        QCOMPARE(m_flick->property("contentHeight").toReal(), m_page->contentHeight());
        QCOMPARE(m_flick->property("contentY").toReal(), 0.0);
        // (Polish typography: a no-break space after the one-letter words.)
        QCOMPARE(m_page->rowText(0).left(17), u"punkt 0 o\u00a0Stronie"_s);
        QVERIFY(m_page->rowLineCount(5) >= 2);   // a long row, wrapped
        QCOMPARE(m_page->rowLineCount(2999), 0); // never laid out
    }

    /// The Flickable's contentY moves the item; rows are laid out on the way only.
    void scrolls()
    {
        setUpPage(outline(3000));
        const int before = m_page->layoutCount();
        m_flick->setProperty("contentY", 30000);
        QCOMPARE(m_page->viewY(), 30000.0); // the anchor follows the Flickable
        m_page->forceLayout();
        // (Rows measured above the top edge move the page's coordinates, not the screen: the
        // Flickable is told.)
        QCOMPARE(m_flick->property("contentY").toReal(), m_page->viewY());
        QVERIFY(m_page->firstShownRow() > 500);
        QVERIFY(m_page->layoutCount() - before < 60);
        const int top = m_page->firstShownRow();
        QVERIFY(m_page->rowRect(top).y() <= 0);
        QVERIFY(m_page->rowRect(top).bottom() > 0);
        QCOMPARE(m_page->rowAt(10, 1), top);
        scrollTo(0);
        QCOMPARE(m_page->firstShownRow(), 0);
        QCOMPARE(m_page->rowAt(10, -5), -1);
    }

    /// A new width lays out the rows near the view, not the 3000 the page has.
    void widthChangeLaysOutWhatIsShown()
    {
        setUpPage(outline(3000));
        scrollTo(20000);
        const int before = m_page->layoutCount();
        m_page->setContentWidth(400);
        m_page->forceLayout();
        const int after = m_page->layoutCount() - before;
        QVERIFY2(after > 0 && after < 70, qPrintable(QString::number(after)));
        const int top = m_page->firstShownRow();
        // The narrower column wraps the long rows onto more lines.
        int longRow = -1;
        for (int row = top; row <= m_page->lastShownRow() && longRow < 0; ++row) {
            if (m_page->rowText(row).contains(u"Długi punkt"))
                longRow = row;
        }
        QVERIFY(longRow >= 0);
        QVERIFY(m_page->rowLineCount(longRow) >= 3);
        // Back to the first width: the short rows kept their layout (no line broke).
        const int again = m_page->layoutCount();
        m_page->setContentWidth(560);
        m_page->forceLayout();
        QVERIFY(m_page->layoutCount() - again < 70);
    }

    /// Rows above the view measured, edited, made longer: the row at the top stays where it is.
    void anchorKeepsTheScreen()
    {
        setUpPage(outline(3000));
        scrollTo(40007);
        const int top = m_page->firstShownRow();
        const qreal screen = m_page->rowRect(top).y();
        const quint64 uid = m_model->uidAt(top);
        // Another device made rows far above longer and added some (estimated rows grow).
        QStringList lines = outline(3000).split(u'\n');
        for (int k = 100; k < 140; ++k)
            lines[k] += u" i jeszcze dłuższy tekst, który się zawija na wiele linii: "_s.repeated(4);
        lines.insert(50, u"- nowy blok ą"_s);
        m_model->replaceContent(lines.join(u'\n'));
        m_page->forceLayout();
        const int now = m_model->rowOfUid(uid);
        QCOMPARE(now, top + 1);
        QCOMPARE(m_page->rowRect(now).y(), screen);
        // The rows just above the view (the margin) are measured on the way up: still in place.
        scrollTo(m_page->viewY() - 150);
        const int up = m_page->firstShownRow();
        const qreal upScreen = m_page->rowRect(up).y();
        m_page->forceLayout();
        QCOMPARE(m_page->rowRect(up).y(), upScreen);
        QCOMPARE(m_flick->property("contentY").toReal(), m_page->viewY());
    }

    /// While the Flickable moves (a fling) a height change above the view is no write of its
    /// contentY — that would stop the fling —: the difference waits as a slack and is folded in
    /// when the movement ends.
    void slackWhileMoving()
    {
        setUpPage(outline(3000));
        scrollTo(40000);
        QVERIFY(QMetaObject::invokeMethod(m_flick, "flick", Q_ARG(qreal, 0), Q_ARG(qreal, -800)));
        QTRY_VERIFY(m_flick->property("moving").toBool());
        // Rows far above made longer by another device: the view keeps its rows on screen.
        const int top = m_page->firstShownRow();
        const quint64 uid = m_model->uidAt(top);
        const qreal screen = m_page->rowRect(top).y();
        QStringList lines = outline(3000).split(u'\n');
        for (int k = 10; k < 60; ++k)
            lines[k] += u" dłuższy tekst o źdźbłach, który się zawija "_s.repeated(5);
        m_model->replaceContent(lines.join(u'\n'));
        m_page->forceLayout();
        QCOMPARE(m_page->rowRect(m_model->rowOfUid(uid)).y(), screen);
        QVERIFY2(m_flick->property("moving").toBool(), "the fling was stopped");
        const qreal slack = m_page->viewY() - m_flick->property("contentY").toReal();
        QVERIFY2(slack > 100, qPrintable(QString::number(slack)));
        // The fling scrolls the item on (the slack kept), and at its end the slack is folded in.
        const qreal before = m_page->viewY();
        QTRY_VERIFY(std::abs(m_page->viewY() - before) > 1);
        QVERIFY(QMetaObject::invokeMethod(m_flick, "cancelFlick"));
        QTRY_VERIFY(!m_flick->property("moving").toBool());
        m_page->forceLayout();
        QCOMPARE(m_flick->property("contentY").toReal(), m_page->viewY());
    }

    /// "Scroll to block" on estimated rows: exactly where it was asked for.
    void positionAtRow()
    {
        setUpPage(outline(3000));
        m_page->positionAtRow(2000, 1); // ListView.Center
        m_page->forceLayout();
        const QRectF r = m_page->rowRect(2000);
        QCOMPARE(r.y() + r.height() / 2, ViewHeight / 2.0);
        // A long row, its estimate off: centred with its measured height.
        int longRow = 2000;
        while (longRow % 11 != 5)
            ++longRow;
        m_page->positionAtRow(longRow, 1);
        m_page->forceLayout();
        const QRectF l = m_page->rowRect(longRow);
        QVERIFY(m_page->rowLineCount(longRow) >= 2);
        QCOMPARE(l.y() + l.height() / 2, ViewHeight / 2.0);
        m_page->positionAtRow(2500, 0); // Beginning
        m_page->forceLayout();
        QCOMPARE(m_page->rowRect(2500).y(), 0.0);
        QCOMPARE(m_page->firstShownRow(), 2500);
        m_page->ensureRowVisible(2501); // shown whole: nothing moves
        m_page->forceLayout();
        QCOMPARE(m_page->rowRect(2500).y(), 0.0);
        m_page->positionAtBeginning();
        m_page->forceLayout();
        QCOMPARE(m_page->viewY(), 0.0);
    }

    /// Rows the item does not draw (code, tasks) are hosted by the delegate, with their roles and
    /// index, at their place; released when scrolled away.
    void hostsRichRows()
    {
        QStringList lines;
        for (int k = 0; k < 400; ++k)
            lines << u"- punkt %1"_s.arg(k);
        lines[3] = u"- [ ] zadanie do zrobienia"_s;
        lines.insert(5, u"- ```python\n  print(\"źdźbło\")\n  ```"_s);
        setUpPage(lines.join(u'\n') + u'\n');
        QCOMPARE(m_model->viewAt(5), u"code"_s);
        QQuickItem *code = m_page->rowItem(5);
        QVERIFY(code);
        QCOMPARE(code->property("index").toInt(), 5);
        QCOMPARE(code->property("view").toString(), u"code"_s);
        QCOMPARE(code->height(), 120.0);
        QCOMPARE(m_page->rowRect(5).height(), 120.0);
        QCOMPARE(code->y(), m_page->rowRect(5).y());
        QQuickItem *task = m_page->rowItem(3);
        QVERIFY(task);
        QCOMPARE(task->property("taskStatus").toString(), u"todo"_s);
        QVERIFY(!m_page->rowItem(0));
        QCOMPARE(m_page->hostedRowCount(), 2);
        // Rows inserted above: the delegate's index follows its row.
        m_model->replaceContent(u"- nowy pierwszy\n"_s + lines.join(u'\n') + u'\n');
        m_page->forceLayout();
        QCOMPARE(m_page->rowItem(6), code);
        QCOMPARE(code->property("index").toInt(), 6);
        // A role changed: the delegate is told.
        scrollTo(m_page->rowRect(300).y() + m_page->viewY());
        QVERIFY(!m_page->rowItem(6));
        QCOMPARE(m_page->hostedRowCount(), 0);
        // A test may ask for a drawn row to be hosted.
        m_page->hostRow(m_page->firstShownRow());
        m_page->forceLayout();
        QVERIFY(m_page->rowItem(m_page->firstShownRow()));
    }

    /// A row with a styled tag (b.app.tagStyle) is hosted: the editor's delegate draws its tint
    /// (BlockPartTagTint); the item draws it again once the style is gone.
    void hostsTintedRows()
    {
        setUpPage(u"- zwykły punkt\n- zrobić #pilne\n- drugi zwykły\n"_s);
        FakeEditorHost host;
        m_model->setHost(&host);
        connect(&host, &brulion::EditorHost::tagStylesChanged, m_model.get(), &BlockListModel::followTagStyles);
        m_page->forceLayout();
        QCOMPARE(m_page->hostedRowCount(), 0);
        host.setTagStyle(u"pilne"_s, u"danger"_s);
        m_page->forceLayout();
        QVERIFY(m_page->rowItem(1));
        QVERIFY(!m_page->rowItem(0) && !m_page->rowItem(2));
        QCOMPARE(m_page->hostedRowCount(), 1);
        host.setTagStyle(u"pilne"_s, QString());
        m_page->forceLayout();
        QVERIFY(!m_page->rowItem(1));
        QCOMPARE(m_page->hostedRowCount(), 0);
    }

    /// A fold hides rows (no height, not drawn); the anchor stays in place.
    void folds()
    {
        QStringList lines;
        for (int k = 0; k < 100; ++k) {
            lines << u"- rodzic %1"_s.arg(k);
            for (int c = 0; c < 3; ++c)
                lines << u"  - dziecko %1.%2"_s.arg(k).arg(c);
        }
        setUpPage(lines.join(u'\n') + u'\n');
        const qreal total = m_page->contentHeight();
        m_model->setCollapsed(0, true);
        m_page->forceLayout();
        QVERIFY(m_page->contentHeight() < total);
        QCOMPARE(m_page->rowRect(1).height(), 0.0);
        QCOMPARE(m_page->rowAt(10, m_page->rowRect(4).y() + 2), 4);
        m_model->setCollapsed(0, false);
        m_page->forceLayout();
        QVERIFY(m_page->rowRect(1).height() > 0);
    }

    /// The drawn rows' links and a frame with their text and bullets.
    void linksAndFrame()
    {
        setUpPage(outline(50));
        const QRectF r = m_page->rowRect(0);
        QString link;
        for (qreal x = 0; x < ViewWidth && link.isEmpty(); x += 2)
            link = m_page->linkAt(x, r.y() + r.height() / 2);
        QVERIFY2(link.startsWith(u"page:"), qPrintable(link));
        m_page->setPointer(-1, -1);
        QCOMPARE(m_page->hoveredLink(), QString());
        const QImage frame = m_window->grabWindow();
        QVERIFY(!frame.isNull());
        // Dark text on the first row, right of the text column's start.
        bool dark = false;
        for (int x = 60; x < 300 && !dark; ++x) {
            for (int y = int(r.y()); y < int(r.bottom()) && !dark; ++y)
                dark = qGray(frame.pixel(x, y)) < 110;
        }
        QVERIFY(dark);
    }
};

QTEST_MAIN(TstEditorPageItem)
#include "tst_editor_pageitem.moc"
