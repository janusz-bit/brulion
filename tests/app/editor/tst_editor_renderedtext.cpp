// RenderedTextItem (src/app/editor/renderedtextitem.h): a block's rendered text laid out from
// RenderedText runs and drawn through the scene graph, compared with what the rich-text path does
// with the same HTML — a QTextDocument with the fixed line height (the lines, where they break, the
// baselines) and RichTextPills (the shapes of chips, tags, code and highlights) —; its height known
// as soon as it is complete and laid out again only for a change that needs it (not for the hovered
// link or the find bar), links under a point, source positions, the cut after maximumLineCount
// lines, Polish text with diacritics and no-break spaces, emoji, and a frame drawn offscreen.
#include "markdown/inlinerenderer.h"
#include "renderedtextitem.h"
#include "richtextpills.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <memory>

using namespace brulion::editor;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

constexpr qreal LineHeight = 26;

RenderContext context()
{
    RenderContext c;
    c.today = QDate(2026, 10, 14);
    c.dateLinks = true;
    c.pageIcon = [](const QString &target) {
        return target == u"Dom"_s ? PageIcon{PageIcon::Kind::App, u"home"_s} : PageIcon{};
    };
    return c;
}

QFont font()
{
    QFont f;
    f.setPixelSize(15);
    return f;
}

/// An item laid out as QML would create it: properties first, then complete.
std::unique_ptr<RenderedTextItem> item(const QString &markdown, qreal width, int maxLines = 0)
{
    auto out = std::make_unique<RenderedTextItem>();
    static_cast<QQmlParserStatus *>(out.get())->classBegin();
    const InlineRenderer renderer(context());
    out->setRenderedText({std::make_shared<const RenderedText>(renderer.renderRuns(markdown))});
    out->setFont(font());
    out->setLineHeight(LineHeight);
    out->setWidth(width);
    out->setMaximumLineCount(maxLines);
    out->componentComplete();
    return out;
}

/// The rich-text path's document: the HTML with the font, width and fixed line height.
std::unique_ptr<QTextDocument> document(const QString &html, qreal width)
{
    auto doc = std::make_unique<QTextDocument>();
    doc->setDefaultFont(font());
    doc->setDocumentMargin(0);
    doc->setTextWidth(width);
    doc->setHtml(html);
    QTextBlockFormat f;
    f.setLineHeight(LineHeight, QTextBlockFormat::FixedHeight);
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        QTextCursor(b).mergeBlockFormat(f);
    (void)doc->documentLayout()->documentSize();
    return doc;
}

QString longText()
{
    return u"Zażółć gęślą jaźń — w domu i w ogrodzie, z **pogrubieniem**, `kodem`, #tagiem, [[Dom]] i "
           "linkiem [strona](https://example.com/ścieżka) oraz ==zakreśleniem== 📅 2026-10-20 i dalej, "
           "bo tekst musi się zawinąć kilka razy na wąskiej szerokości."_s;
}

} // namespace

class TestRenderedTextItem : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void linesMatchTheRichText_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<qreal>("width");
        QTest::newRow("one line") << u"Krótki tekst"_s << 600.0;
        QTest::newRow("wrapped") << longText() << 260.0;
        QTest::newRow("wide") << longText() << 900.0;
        QTest::newRow("line breaks") << u"pierwsza\ndruga linia\n\ntrzecia"_s << 400.0;
        QTest::newRow("chips") << u"[ ] zadanie 📅 2026-10-20 ⏰ 2026-10-14 09:00 🔁 co tydzień"_s << 200.0;
        QTest::newRow("long word") << u"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_s << 120.0;
        QTest::newRow("emoji") << u"🙂 uśmiech 👍🏽 i 👨‍👩‍👧 rodzina"_s << 120.0;
    }
    void linesMatchTheRichText()
    {
        QFETCH(QString, markdown);
        QFETCH(qreal, width);
        const auto it = item(markdown, width);
        const QString html = InlineRenderer(context()).render(markdown);
        const auto doc = document(html, width);
        const QTextLayout *expected = doc->begin().layout();
        const QTextLayout *actual = it->textLayout();
        QVERIFY(actual);
        QCOMPARE(doc->blockCount(), 1);
        QCOMPARE(actual->lineCount(), expected->lineCount());
        for (int k = 0; k < expected->lineCount(); ++k) {
            const QTextLine e = expected->lineAt(k);
            const QTextLine a = actual->lineAt(k);
            QCOMPARE(a.textStart(), e.textStart());
            QCOMPARE(a.textLength(), e.textLength());
            QCOMPARE(a.y(), e.y()); // the baseline where FixedHeight puts it
            QVERIFY2(std::abs(a.naturalTextWidth() - e.naturalTextWidth()) < 0.5, qPrintable(QString::number(k)));
        }
        // Whole lines of the fixed height.
        QCOMPARE(it->implicitHeight(), expected->lineCount() * LineHeight);
        // The text Qt shows (no-break spaces kept: toRawText()).
        QString text = doc->toRawText();
        text.replace(QChar::LineSeparator, u'\n');
        QCOMPARE(it->plainText(), text);
    }

    void shapesMatchRichTextPills_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<qreal>("width");
        QTest::newRow("tag") << u"tekst #tag dalej"_s << 600.0;
        QTest::newRow("code and mark") << u"`kod` i ==żółte **gruby** dalej=="_s << 600.0;
        QTest::newRow("wrapped chip") << u"zadanie 🔁 co tydzień w poniedziałek i środę aż do końca roku"_s << 160.0;
        QTest::newRow("nested tag") << u"#praca/raporty/tygodniowe"_s << 600.0;
        QTest::newRow("long") << longText() << 260.0;
    }
    void shapesMatchRichTextPills()
    {
        QFETCH(QString, markdown);
        QFETCH(qreal, width);
        const auto it = item(markdown, width);
        const auto doc = document(InlineRenderer(context()).render(markdown), width);
        RichTextPills pills;
        pills.setLineHeight(LineHeight);
        pills.attach(doc.get());
        QList<RichTextPills::Pill> expected;
        for (const RichTextPills::Pill &p : pills.pillList()) {
            if (p.icon.isEmpty() && p.color.alpha() > 0)
                expected.append(p);
        }
        const QList<RenderedTextItem::Shape> &shapes = it->shapes();
        QCOMPARE(shapes.size(), expected.size());
        for (qsizetype k = 0; k < shapes.size(); ++k) {
            const QRectF a = shapes[k].rect;
            const QRectF e = expected[k].rect;
            QVERIFY2(std::abs(a.x() - e.x()) < 0.5 && std::abs(a.y() - e.y()) < 0.5 && std::abs(a.width() - e.width()) < 0.5
                             && std::abs(a.height() - e.height()) < 0.5,
                     qPrintable(u"%1: %2,%3 %4×%5 vs %6,%7 %8×%9"_s.arg(k)
                                        .arg(a.x())
                                        .arg(a.y())
                                        .arg(a.width())
                                        .arg(a.height())
                                        .arg(e.x())
                                        .arg(e.y())
                                        .arg(e.width())
                                        .arg(e.height())));
            QCOMPARE(shapes[k].color, expected[k].color);
            QCOMPARE(shapes[k].kind == RenderedTextItem::Shape::Kind::Pill, expected[k].round);
        }
    }

    void heightKnownWhenComplete()
    {
        auto it = std::make_unique<RenderedTextItem>();
        static_cast<QQmlParserStatus *>(it.get())->classBegin();
        it->setHtml(InlineRenderer(context()).render(longText()));
        it->setFont(font());
        it->setLineHeight(LineHeight);
        it->setWidth(260);
        QCOMPARE(it->layoutCount(), 0); // nothing laid out before it is complete
        it->componentComplete();
        QCOMPARE(it->layoutCount(), 1);
        QVERIFY(it->implicitHeight() >= 3 * LineHeight);
        QCOMPARE(std::fmod(it->implicitHeight(), LineHeight), 0.0);
        // Changes that need no layout lay nothing out.
        it->setHoveredLink(u"page:Dom"_s);
        it->setFindQuery(u"dom"_s);
        it->setColor(Qt::red);
        it->setWidth(260);
        it->setFont(font());
        QCOMPARE(it->layoutCount(), 1);
        QVERIFY(!it->overlays().isEmpty());
        // A new width does.
        QSignalSpy heights(it.get(), &QQuickItem::implicitHeightChanged);
        it->setWidth(900);
        QCOMPARE(it->layoutCount(), 2);
        QCOMPARE(heights.size(), 1);
        QVERIFY(it->implicitHeight() < 3 * LineHeight);
        // Empty text: no lines, no height.
        const auto empty = item(QString(), 300);
        QCOMPARE(empty->lineCount(), 0);
        QCOMPARE(empty->implicitHeight(), 0.0);
    }

    void linksUnderAPoint()
    {
        const QString markdown = u"Ala [[Dom]] i #tag oraz https://example.com/a koniec"_s;
        const auto it = item(markdown, 600);
        const QString text = it->plainText();
        const QTextLine line = it->textLayout()->lineAt(0);
        const auto xOf = [&](QStringView piece) {
            const qsizetype at = text.indexOf(piece);
            return (line.cursorToX(int(at)) + line.cursorToX(int(at + piece.size()))) / 2;
        };
        const qreal y = LineHeight / 2;
        QCOMPARE(it->linkAt(xOf(u"Dom"), y), u"page:Dom"_s);
        QCOMPARE(it->linkAt(xOf(u"#tag"), y), u"tag:tag"_s);
        QCOMPARE(it->linkAt(xOf(u"example"), y), u"ext:https://example.com/a"_s);
        QCOMPARE(it->linkAt(xOf(u"Ala"), y), QString());
        QCOMPARE(it->linkAt(xOf(u"koniec"), y), QString());
        // Past the line's text, above and below the lines: none.
        QCOMPARE(it->linkAt(590, y), QString());
        QCOMPARE(it->linkAt(xOf(u"Dom"), -1), QString());
        QCOMPARE(it->linkAt(xOf(u"Dom"), LineHeight + 1), QString());
        // The page icon in front of [[Dom]] is a link to it, with its place for the icon.
        QCOMPARE(it->iconShapes().size(), 1);
        const RichTextPill icon = it->iconShapes().first();
        QCOMPARE(icon.icon, u"home"_s);
        QCOMPARE(it->linkAt(icon.x + icon.width / 2, y), u"page:Dom"_s);
        // Source positions: verbatim text exactly, the link at its start; the rectangle of a range.
        QCOMPARE(it->positionAt(xOf(u"Ala") - 2, y), 1);
        QCOMPARE(it->positionAt(line.cursorToX(int(text.indexOf(u"koniec"))), y), markdown.indexOf(u"koniec"));
        QCOMPARE(it->positionAt(0, 5 * LineHeight), 0);
        const QRectF rect = it->rectForSource(int(markdown.indexOf(u"oraz")), int(markdown.indexOf(u"oraz") + 4));
        QCOMPARE(rect.height(), LineHeight);
        QVERIFY(std::abs(rect.x() - line.cursorToX(int(text.indexOf(u"oraz")))) < 0.01);
        QVERIFY(rect.width() > 10);
    }

    void hoverAndFindAreOverlays()
    {
        const auto it = item(u"[[Dom]] i [[Inna|inna strona]] oraz #tag"_s, 600);
        QVERIFY(it->overlays().isEmpty());
        it->setHoveredLink(u"page:Inna"_s);
        QCOMPARE(it->overlays().size(), 1);
        QCOMPARE(it->overlays().first().kind, RenderedTextItem::Shape::Kind::Underline);
        // A whole tag is never underlined (withHoveredLink()).
        it->setHoveredLink(u"tag:tag"_s);
        QVERIFY(it->overlays().isEmpty());
        it->setHoveredLink(QString());
        it->setFindQuery(u"strona"_s);
        QCOMPARE(it->overlays().size(), 1);
        QCOMPARE(it->overlays().first().kind, RenderedTextItem::Shape::Kind::Pill); // in a link
        it->setFindQuery(u"oraz"_s);
        QCOMPARE(it->overlays().first().kind, RenderedTextItem::Shape::Kind::Find);
        it->setFindQuery(u"ORAZ"_s);
        it->setFindMatchCase(true);
        QVERIFY(it->overlays().isEmpty());
        QCOMPARE(it->layoutCount(), 1);
    }

    void elision()
    {
        const auto it = item(longText(), 260, 2);
        QCOMPARE(it->lineCount(), 2);
        QVERIFY(it->truncated());
        QVERIFY(it->plainText().endsWith(u'…'));
        QCOMPARE(it->implicitHeight(), 2 * LineHeight);
        QVERIFY(!it->plainText().contains(u' ' + QString(u'…')));
        // "…" is no link: nothing past the cut is.
        const QTextLine last = it->textLayout()->lineAt(1);
        QCOMPARE(it->linkAt(last.naturalTextRect().right() - 1, LineHeight * 1.5), QString());
        // Enough lines: not cut.
        it->setMaximumLineCount(50);
        QVERIFY(!it->truncated());
        QVERIFY(!it->plainText().endsWith(u'…'));
        // A surrogate pair is never cut in half.
        const auto emoji = item(QString(u"📅"_s).repeated(200), 100, 1);
        QVERIFY(emoji->truncated());
        const QString shown = emoji->plainText();
        QVERIFY(!shown.at(shown.size() - 2).isHighSurrogate());
    }

    void polishTypography()
    {
        // "w domu", "i dalej": the no-break space after a one-letter word keeps it with the next one
        // at every width that holds the two words.
        for (qreal width = 120; width <= 400; width += 7) {
            const auto it = item(u"Byłem w domu i dalej szedłem z psem u sąsiadów"_s, width);
            const QString text = it->plainText();
            QVERIFY(text.contains(u"w domu"));
            for (int k = 0; k < it->lineCount(); ++k) {
                const QTextLine line = it->textLayout()->lineAt(k);
                const QString piece = text.mid(line.textStart(), line.textLength()).trimmed();
                for (const QChar one : {u'w', u'i', u'z', u'u'})
                    QVERIFY2(!piece.endsWith(u' ' + QString(one)) && piece != QString(one), qPrintable(piece));
            }
        }
    }

    void emojiAreOneCharacter()
    {
        const QString markdown = u"a 👨‍👩‍👧 b"_s;
        const auto it = item(markdown, 400);
        const QString text = it->plainText();
        const QTextLine line = it->textLayout()->lineAt(0);
        const qsizetype at = text.indexOf(u"👨");
        const qreal mid = (line.cursorToX(int(at)) + line.cursorToX(int(text.indexOf(u" b")))) / 2;
        const int position = it->displayPositionAt(mid, 5);
        QVERIFY2(position == at || position == text.indexOf(u" b"), qPrintable(QString::number(position)));
        QCOMPARE(it->implicitHeight(), LineHeight);
    }

    void drawsAFrame_data()
    {
        QTest::addColumn<bool>("software");
        QTest::addColumn<QString>("markdown");
        for (const bool software : {false, true}) {
            const char *api = software ? "software" : "scene graph";
            QTest::addRow("%s, shapes", api) << software << u"tekst `kod` #tag [[Inna]]"_s;
            // The underline is the only shape, in a frame after one without any (the batch
            // renderer once left such a node out).
            QTest::addRow("%s, no shapes", api) << software << u"tekst [[Inna]]"_s;
        }
    }
    void drawsAFrame()
    {
        QFETCH(bool, software);
        QFETCH(QString, markdown);
        QQuickWindow::setGraphicsApi(software ? QSGRendererInterface::Software : QSGRendererInterface::Unknown);
        QQuickWindow window;
        window.resize(400, 120);
        window.setColor(Qt::white);
        auto *it = new RenderedTextItem(window.contentItem());
        static_cast<QQmlParserStatus *>(it)->classBegin();
        it->setHtml(InlineRenderer(context()).render(markdown));
        it->setFont(font());
        it->setLineHeight(LineHeight);
        it->setColor(Qt::black);
        it->setWidth(400);
        it->componentComplete();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const QImage frame = window.grabWindow();
        QVERIFY(!frame.isNull());
        // (The graphics API is chosen once per process: offscreen it is the software renderer for
        // both rows, under X11 the first row's.)
        if (software != (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software))
            QSKIP("this graphics API is not available here");
        // The tag's pill is drawn (its colour in the middle of its shape, left of the text) and the
        // text is (dark pixels).
        bool dark = false;
        for (int x = 0; x < 60 && !dark; ++x) {
            for (int y = 0; y < int(LineHeight) && !dark; ++y)
                dark = qGray(frame.pixel(x, y)) < 100;
        }
        QVERIFY(dark);
        if (!it->shapes().isEmpty()) {
            const RenderedTextItem::Shape tag = it->shapes().last();
            const QColor inside = frame.pixelColor(int(tag.rect.left() + 2), int(tag.rect.center().y()));
            QVERIFY2(inside != QColor(Qt::white), qPrintable(inside.name()));
        }
        // The hovered link's underline: drawn in the next frame, nothing laid out again.
        it->setHoveredLink(u"page:Inna"_s);
        QCOMPARE(it->overlays().size(), 1);
        const QRectF line = it->overlays().first().rect;
        const QImage hovered = window.grabWindow();
        const QColor under = hovered.pixelColor(int(line.center().x()), int(line.center().y()));
        QVERIFY2(under != QColor(Qt::white), qPrintable(under.name()));
        QCOMPARE(it->layoutCount(), 1);
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Unknown);
    }
};

QTEST_MAIN(TestRenderedTextItem)
#include "tst_editor_renderedtext.moc"
