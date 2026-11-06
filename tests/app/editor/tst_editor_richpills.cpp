// RichTextPills (src/app/editor/richtextpills.h) on a QTextDocument in an offscreen
// QGuiApplication, fed with real InlineRenderer HTML: the background brushes of chips, tags, code
// and highlights are taken off (no square is painted), their colours and kinds become rectangles
// from the document's own layout — one per line of a wrapped chip, centred on the glyphs inside
// the fixed-height line —, every paragraph gets the fixed line height, and malformed input never
// leaves a square behind. The lists' mode (BRichText): the HTML set by RichTextPills itself and
// cut after maximumLineCount lines with "…" — never inside a character, never as part of a link
// or a chip —, cut anew for another width or font, nothing done before the QML object is complete.
#include "markdown/highlight.h"
#include "markdown/inlinerenderer.h"
#include "richtextpills.h"

#include <QAbstractTextDocumentLayout>
#include <QFont>
#include <QFontMetricsF>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>

using namespace brulion::editor;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

constexpr qreal LineHeight = 26;
constexpr qreal TextWidth = 640;

QString render(const QString &markdown)
{
    RenderContext c;
    c.today = QDate(2026, 10, 14);
    c.dateLinks = true;
    return InlineRenderer(c).render(markdown);
}

/// A span with a background brush in Qt's own import of `html` (what a rich-text Text paints).
struct Tint {
    QColor color;
    bool foreground = false;
    bool fixedPitch = false;
    QString text;
};

QList<Tint> tintsOf(const QString &html)
{
    QTextDocument doc;
    doc.setHtml(html);
    QList<Tint> out;
    int end = -1; // where the last tint ended: adjacent fragments of one colour are one span
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            const QTextCharFormat format = f.charFormat();
            if (!format.hasProperty(QTextFormat::BackgroundBrush) || format.background().style() == Qt::NoBrush)
                continue;
            const QColor color = format.background().color();
            const bool foreground = format.hasProperty(QTextFormat::ForegroundBrush);
            if (!out.isEmpty() && end == f.position() && out.last().color == color) {
                out.last().text += f.text();
                out.last().foreground = out.last().foreground && foreground;
                out.last().fixedPitch = out.last().fixedPitch || format.fontFixedPitch();
            } else {
                out.append({color, foreground, format.fontFixedPitch(), f.text()});
            }
            end = f.position() + f.length();
        }
    }
    return out;
}

int brushesLeft(const QTextDocument &doc)
{
    int n = 0;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (format.hasProperty(QTextFormat::BackgroundBrush) && format.background().style() != Qt::NoBrush)
                ++n;
        }
    }
    return n;
}

/// A document laid out as BlockText's TextEdit: the editor's font, no margin, a fixed width.
void setUp(QTextDocument &doc, qreal width = TextWidth)
{
    QFont font;
    font.setPixelSize(16);
    doc.setDefaultFont(font);
    doc.setDocumentMargin(0);
    doc.setTextWidth(width);
}

/// A typed shape as the map `pills` gives for it.
QVariantMap shapeMap(const RichTextPill &p)
{
    return {{u"x"_s, p.x},         {u"y"_s, p.y},       {u"width"_s, p.width}, {u"height"_s, p.height},
            {u"color"_s, p.color}, {u"kind"_s, p.kind}, {u"icon"_s, p.icon}};
}

/// The distinct lines (by their top) the pills of `span` (its from position) lie on.
QList<qreal> linesOf(const QList<RichTextPills::Pill> &pills, int from)
{
    QList<qreal> out;
    for (const RichTextPills::Pill &p : pills) {
        if (p.from == from && !out.contains(p.rect.top()))
            out.append(p.rect.top());
    }
    return out;
}

/// The lines of the laid-out document (every block laid out with its current width).
int lineCountOf(QTextDocument &doc)
{
    int n = 0;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        (void)doc.documentLayout()->blockBoundingRect(block);
        n += block.layout() ? block.layout()->lineCount() : 0;
    }
    return n;
}

/// The format of the document's last character.
QTextCharFormat lastCharFormat(QTextDocument &doc)
{
    QTextCursor c(&doc);
    c.movePosition(QTextCursor::End);
    return c.charFormat();
}

const QString Ellipsis = QString(QChar(0x2026));

/// A long Polish paragraph: a #tag at the start, a [[link]] and a due date further on.
QString longText()
{
    return render(u"#podróże Wyjazd do Zakopanego: zarezerwować nocleg w Kościelisku, kupić bilety na pociąg, "
                  u"sprawdzić szlaki na Giewont i Kasprowy Wierch, zabrać kurtkę przeciwdeszczową, [[Lista rzeczy]], "
                  u"zapytać Łukasza o samochód i zatankować przed wyjazdem 📅 2026-10-20 — źdźbło, gęś, jaźń."_s);
}

} // namespace

class TestRichPills : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void kinds_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<QStringList>("expected"); // a kind per tinted span, in order
        QTest::newRow("polish tag") << u"Wyjazd #podróże w sierpniu"_s << QStringList{u"pill"_s};
        QTest::newRow("nested tag is one chip") << u"Raport #praca/raporty/pilne dziś"_s << QStringList{u"pill"_s};
        QTest::newRow("due chip") << u"[ ] Zadanie 📅 2026-10-20"_s << QStringList{u"pill"_s};
        QTest::newRow("done chip") << u"[x] Zrobione 📅 2026-10-13 ✅ 2026-10-14"_s
                                   << QStringList{u"pill"_s, u"pill"_s};
        QTest::newRow("reminder chip") << u"Spotkanie ⏰ 2026-10-14 09:00"_s << QStringList{u"pill"_s};
        QTest::newRow("code") << u"Uruchom `npm i` w katalogu"_s << QStringList{u"box"_s};
        QTest::newRow("highlight with bold") << u"To jest ==bardzo **ważne** zdanie== dziś"_s
                                             << QStringList{u"box"_s};
        QTest::newRow("all together") << u"#dom ==uwaga== `kod` 📅 2026-10-20 #źródła"_s
                                      << QStringList{u"pill"_s, u"box"_s, u"box"_s, u"pill"_s, u"pill"_s};
        QTest::newRow("no tint") << u"Zwykły tekst z [[Linkiem]] i **pogrubieniem**"_s << QStringList{};
    }

    void kinds()
    {
        QFETCH(QString, markdown);
        QFETCH(QStringList, expected);
        const QString html = render(markdown);
        const QList<Tint> tints = tintsOf(html);
        QCOMPARE(tints.size(), expected.size());

        QTextDocument doc;
        setUp(doc);
        RichTextPills pills;
        pills.setLineHeight(LineHeight);
        pills.attach(&doc);
        QSignalSpy changed(&pills, &RichTextPills::pillsChanged);
        doc.setHtml(html); // as a TextEdit sets its text: after the pills were attached

        QCOMPARE(brushesLeft(doc), 0);
        QCOMPARE(changed.isEmpty(), expected.isEmpty());
        // One span per tint, in order, with its colour and kind (a short chip never wraps here).
        QList<RichTextPills::Pill> spans;
        for (const RichTextPills::Pill &p : pills.pillList()) {
            if (spans.isEmpty() || spans.last().from != p.from)
                spans.append(p);
        }
        QCOMPARE(spans.size(), expected.size());
        for (int i = 0; i < spans.size(); ++i) {
            QCOMPARE(spans[i].color, tints[i].color);
            QCOMPARE(spans[i].round ? u"pill"_s : u"box"_s, expected[i]);
            // The rule the kind follows, on Qt's own import of the renderer's HTML.
            QCOMPARE(spans[i].round, tints[i].foreground && !tints[i].fixedPitch);
            QCOMPARE(doc.toRawText().mid(spans[i].from, spans[i].to - spans[i].from), tints[i].text);
        }
        // The QML list says the same.
        const QVariantList list = pills.pills();
        QCOMPARE(list.size(), pills.pillList().size());
        for (int i = 0; i < list.size(); ++i) {
            const QVariantMap m = list[i].toMap();
            QCOMPARE(m.value(u"kind"_s).toString(), pills.pillList()[i].round ? u"pill"_s : u"box"_s);
            QCOMPARE(m.value(u"color"_s).value<QColor>(), pills.pillList()[i].color);
            QCOMPARE(m.value(u"width"_s).toReal(), pills.pillList()[i].rect.width());
        }
        // So does the typed list, entry by entry.
        const QList<RichTextPill> shapes = pills.shapes();
        QCOMPARE(shapes.size(), list.size());
        for (int i = 0; i < shapes.size(); ++i)
            QCOMPARE(shapeMap(shapes[i]), list[i].toMap());
        // Split for BTintShapes: no icons here, every shape is a pill or a box.
        QCOMPARE(pills.plainShapes(), shapes);
        QVERIFY(pills.iconShapes().isEmpty());
    }

    // Coloured highlights (`<mark>`) are boxes in their palette colours; a page's app icon in front
    // of a link is a pill of kind "icon" naming the icon, in the link's colour, over its em space.
    void colouredHighlightsAndPageIcons()
    {
        RenderContext c;
        c.palette.link = u"#2c6aa3"_s;
        c.pageIcon = [](const QString &target) {
            return target == u"Książki"_s ? PageIcon{PageIcon::Kind::App, u"book-open"_s} : PageIcon();
        };
        const QString html = InlineRenderer(c).render(
                u"Czytam <mark style=\"background: #FF5582A6;\">ważne</mark> w [[Książki]] i ==to=="_s);
        QTextDocument doc;
        setUp(doc);
        RichTextPills pills;
        pills.setLineHeight(LineHeight);
        pills.attach(&doc);
        doc.setHtml(html);
        QCOMPARE(brushesLeft(doc), 0);
        const QVariantList list = pills.pills();
        QCOMPARE(list.size(), 3);
        const QVariantMap red = list[0].toMap();
        QCOMPARE(red.value(u"kind"_s).toString(), u"box"_s);
        QCOMPARE(red.value(u"color"_s).value<QColor>(), QColor(highlightTint(HighlightColor::Red)));
        const QVariantMap icon = list[1].toMap();
        QCOMPARE(icon.value(u"kind"_s).toString(), u"icon"_s);
        QCOMPARE(icon.value(u"icon"_s).toString(), u"book-open"_s);
        QCOMPARE(icon.value(u"color"_s).value<QColor>(), QColor(u"#2c6aa3"_s));
        QVERIFY(icon.value(u"width"_s).toReal() > 4);
        const RichTextPills::Pill &iconPill = pills.pillList()[1];
        QCOMPARE(doc.toRawText().mid(iconPill.from, iconPill.to - iconPill.from), QString(3, QChar(0x00A0)));
        QCOMPARE(list[2].toMap().value(u"kind"_s).toString(), u"box"_s);
        QCOMPARE(list[2].toMap().value(u"icon"_s).toString(), QString());
        const QList<RichTextPill> shapes = pills.shapes();
        QCOMPARE(shapes.size(), 3);
        for (int i = 0; i < shapes.size(); ++i)
            QCOMPARE(shapeMap(shapes[i]), list[i].toMap());
        // Split for BTintShapes: the shapes without the icon, in order, and the icon on its own.
        QCOMPARE(pills.plainShapes(), (QList<RichTextPill>{shapes[0], shapes[2]}));
        QCOMPARE(pills.iconShapes(), QList<RichTextPill>{shapes[1]});
        // The icon's anchor is the link's: a click on it follows the link.
        QTextCursor cursor(&doc);
        cursor.setPosition(iconPill.from + 1);
        QCOMPARE(cursor.charFormat().anchorHref(), u"page:Ksi%C4%85%C5%BCki"_s);
    }

    // Each rectangle covers its span's glyphs and stays inside its fixed-height line, centred on
    // the glyphs (the font's ascent and descent around the baseline), not on the line box.
    void geometry()
    {
        QTextDocument doc;
        setUp(doc);
        RichTextPills pills;
        pills.setLineHeight(LineHeight);
        pills.attach(&doc);
        doc.setHtml(render(u"Wyjazd #podróże i zakupy 📅 2026-10-20 oraz `kod`"_s));
        QCOMPARE(pills.pillList().size(), 3);
        const QTextBlock block = doc.begin();
        const QTextLine line = block.layout()->lineAt(0);
        const QFontMetricsF metrics(doc.defaultFont());
        for (const RichTextPills::Pill &p : pills.pillList()) {
            QVERIFY2(p.rect.top() >= 0 && p.rect.bottom() <= LineHeight + 0.01,
                     qPrintable(u"inside the line: %1–%2"_s.arg(p.rect.top()).arg(p.rect.bottom())));
            QVERIFY(p.rect.height() < LineHeight);
            QVERIFY(p.rect.left() >= 0 && p.rect.right() <= TextWidth);
            // The span's own x range from the layout.
            const int from = p.from - block.position();
            const int to = p.to - block.position();
            QCOMPARE(p.rect.left(), std::min(line.cursorToX(from), line.cursorToX(to)));
            QCOMPARE(p.rect.right(), std::max(line.cursorToX(from), line.cursorToX(to)));
            if (p.round) {
                const qreal baseline = line.y() + line.ascent();
                QCOMPARE(p.rect.top(), baseline - metrics.ascent());
                QCOMPARE(p.rect.bottom(), baseline + metrics.descent());
            }
        }
        // `inset` takes that much off the top and the bottom.
        const QRectF before = pills.pillList().first().rect;
        pills.setInset(1);
        QCOMPARE(pills.pillList().first().rect, before.adjusted(0, 1, 0, -1));
    }

    // A chip longer than one line is a rectangle per line, each from the layout of its line; the
    // space the line broke at is not part of the first one.
    void wrappedChip()
    {
        const QString html = u"Na końcu <span style=\"background-color:#299fc3e6;color:#2b5f8f\">&nbsp;🔁 co dwa "
                             u"tygodnie w poniedziałek środę i piątek aż do końca roku&nbsp;</span> koniec"_s;
        QTextDocument doc;
        setUp(doc, 220);
        RichTextPills pills;
        pills.setLineHeight(LineHeight);
        pills.attach(&doc);
        doc.setHtml(html);
        const QList<RichTextPills::Pill> list = pills.pillList();
        QVERIFY2(list.size() >= 2, qPrintable(QString::number(list.size())));
        const QList<qreal> lines = linesOf(list, list.first().from);
        QCOMPARE(lines.size(), list.size()); // one per line
        const QTextLayout *layout = doc.begin().layout();
        QVERIFY(layout->lineCount() >= list.size());
        for (int i = 0; i < list.size(); ++i) {
            QVERIFY(list[i].round);
            QVERIFY(list[i].rect.right() <= 220 + 0.01);
            if (i > 0) {
                QVERIFY(list[i].rect.top() > list[i - 1].rect.top() + LineHeight / 2); // the next line
                QCOMPARE(list[i].rect.left(), layout->lineAt(i).cursorToX(layout->lineAt(i).textStart()));
            }
        }
        // The first line's part ends at its text, not after the space it broke at.
        const QTextLine first = layout->lineAt(0);
        QVERIFY(list.first().rect.right() <= first.naturalTextRect().right() + 0.01);
        // A narrower or wider document: the rectangles follow the new lines.
        doc.setTextWidth(TextWidth * 2);
        QCOMPARE(pills.pillList().size(), 1);
        doc.setTextWidth(160);
        QVERIFY(pills.pillList().size() > list.size() || pills.pillList().size() >= 3);
    }

    void lineHeight()
    {
        QTextDocument doc;
        setUp(doc);
        doc.setHtml(render(u"Pierwsza linia #tag"_s) + u"<p>Drugi akapit "_s + render(u"`x`"_s) + u"</p>"_s);
        RichTextPills pills;
        pills.attach(&doc); // the text first, then the pills (attached to a filled document)
        QCOMPARE(brushesLeft(doc), 0);
        QCOMPARE(pills.pillList().size(), 2);
        pills.setLineHeight(LineHeight);
        for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
            QCOMPARE(b.blockFormat().lineHeightType(), int(QTextBlockFormat::FixedHeight));
            QCOMPARE(b.blockFormat().lineHeight(), LineHeight);
        }
        QVERIFY(!doc.isUndoAvailable());
        // A new text: the fixed height again, the rectangles of the new text only.
        doc.setHtml(render(u"Inny tekst ⏰ 2026-10-14 09:00"_s));
        QCOMPARE(doc.begin().blockFormat().lineHeight(), LineHeight);
        QCOMPARE(pills.pillList().size(), 1);
        // The same text again: the same rectangles (no duplicates from the kept tints).
        const QList<RichTextPills::Pill> before = pills.pillList();
        doc.setHtml(render(u"Inny tekst ⏰ 2026-10-14 09:00"_s));
        QCOMPARE(pills.pillList(), before);
        // A format change somebody else makes keeps them (the tint lives on the format).
        QTextCursor c(&doc);
        c.select(QTextCursor::Document);
        QTextCharFormat bold;
        bold.setFontWeight(QFont::Bold);
        c.mergeCharFormat(bold);
        QCOMPARE(pills.pillList().size(), 1);
        QCOMPARE(pills.pillList().first().color, before.first().color);
    }

    void emptyAndDetached()
    {
        QTextDocument doc;
        setUp(doc);
        RichTextPills pills;
        pills.setLineHeight(LineHeight);
        pills.attach(&doc);
        QVERIFY(pills.pillList().isEmpty());
        doc.setHtml(render(u"#tag"_s));
        QCOMPARE(pills.pillList().size(), 1);
        QSignalSpy changed(&pills, &RichTextPills::pillsChanged);
        doc.setHtml(QString());
        QVERIFY(pills.pillList().isEmpty());
        QCOMPARE(changed.size(), 1);
        pills.attach(nullptr);
        QVERIFY(pills.pillList().isEmpty());
        // A document that goes away first.
        auto *gone = new QTextDocument;
        setUp(*gone);
        pills.attach(gone);
        gone->setHtml(render(u"#tag"_s));
        QCOMPARE(pills.pillList().size(), 1);
        delete gone;
        QVERIFY(pills.pillList().isEmpty());
        pills.refresh();
        QVERIFY(pills.pillList().isEmpty());
    }

    void malformed_data()
    {
        QTest::addColumn<QString>("html");
        QTest::newRow("bad colour") << u"<span style=\"background-color:#zz\">źle</span> tekst"_s;
        QTest::newRow("empty colour") << u"<span style=\"background-color:\">pusto</span>"_s;
        QTest::newRow("unclosed") << u"<span style=\"background-color:#ffe08a\">bez końca <b>i dalej"_s;
        QTest::newRow("nested tints") << u"<span style=\"background-color:#ffe08a\">a <span style=\"background-color:"
                                         u"#299fc3e6;color:#2b5f8f\">#b</span> c</span>"_s;
        QTest::newRow("only spaces") << u"<span style=\"background-color:#ffe08a\">&nbsp;&nbsp;</span>"_s;
        QTest::newRow("line breaks") << u"<code style=\"background-color:#eee\">a<br/>b<br/>c</code>"_s;
        QTest::newRow("table") << u"<table><tr><td style=\"background-color:#eee\">komórka</td><td>"
                                  u"<span style=\"background-color:#ffe08a\">ż</span></td></tr></table>"_s;
        QTest::newRow("script text") << u"&lt;script&gt;alert(1)&lt;/script&gt; <span style=\"background-color:red;"
                                        u"color:white\">x</span>"_s;
    }

    void malformed()
    {
        QFETCH(QString, html);
        QTextDocument doc;
        setUp(doc);
        RichTextPills pills;
        pills.setLineHeight(LineHeight);
        pills.attach(&doc);
        doc.setHtml(html);
        QCOMPARE(brushesLeft(doc), 0);
        for (const RichTextPills::Pill &p : pills.pillList()) {
            QVERIFY(p.rect.width() > 0 && p.rect.height() > 0);
            QVERIFY(p.from < p.to);
        }
        doc.setTextWidth(40); // narrow: wraps whatever there is
        QCOMPARE(brushesLeft(doc), 0);
    }

    // ---- The lists' mode: the text set by RichTextPills, cut after maximumLineCount lines ----

    void setsTheTextItself()
    {
        QTextDocument doc;
        setUp(doc);
        RichTextPills pills;
        pills.setLineHeight(LineHeight);
        pills.attach(&doc);
        const QString html = render(u"Wyjazd #podróże w sierpniu"_s);
        QVERIFY(tintsOf(html).size() == 1);
        pills.setHtml(html);
        QVERIFY(doc.toPlainText().contains(u"Wyjazd"_s));
        QCOMPARE(brushesLeft(doc), 0);
        QCOMPARE(pills.pillList().size(), 1);
        QVERIFY(!pills.truncated());
        QVERIFY(!doc.toPlainText().contains(Ellipsis));
        // A new text replaces it (the tints of the new one go too).
        pills.setHtml(render(u"Inny tekst bez tagów"_s));
        QCOMPARE(doc.toPlainText(), u"Inny tekst bez tagów"_s);
        QVERIFY(pills.pillList().isEmpty());
        // An empty text empties the document.
        pills.setHtml(QString());
        QVERIFY(doc.toPlainText().isEmpty());
    }

    void cutsAfterLines_data()
    {
        QTest::addColumn<int>("lines");
        QTest::addColumn<qreal>("width");
        QTest::newRow("one line") << 1 << qreal(300);
        QTest::newRow("two lines") << 2 << qreal(300);
        QTest::newRow("four lines, narrow") << 4 << qreal(160);
        QTest::newRow("six lines, very narrow") << 6 << qreal(60);
        QTest::newRow("one line, very narrow") << 1 << qreal(30);
    }
    void cutsAfterLines()
    {
        QFETCH(int, lines);
        QFETCH(qreal, width);
        QTextDocument doc;
        setUp(doc, width);
        RichTextPills pills;
        pills.setLineHeight(LineHeight);
        pills.setMaximumLineCount(lines);
        pills.setWidth(width);
        pills.attach(&doc);
        pills.setHtml(longText());
        QVERIFY(pills.truncated());
        QCOMPARE(lineCountOf(doc), lines);
        const QString text = doc.toPlainText();
        QVERIFY2(text.endsWith(Ellipsis), qPrintable(text));
        QVERIFY2(!text.chopped(1).endsWith(u' ') && !text.chopped(1).endsWith(QChar(0x00A0)), qPrintable(text));
        // "…" is no link and no chip, in the text's own colour.
        const QTextCharFormat f = lastCharFormat(doc);
        QVERIFY(!f.isAnchor());
        QVERIFY(f.anchorHref().isEmpty());
        QVERIFY(!f.hasProperty(RichTextPills::TintColorProperty));
        QVERIFY(!f.hasProperty(QTextFormat::ForegroundBrush));
        QCOMPARE(brushesLeft(doc), 0);
        // No pill below the last line shown, none on the "…".
        const int ellipsisAt = int(text.size()) - 1;
        for (const RichTextPills::Pill &p : pills.pillList()) {
            QVERIFY(p.rect.bottom() <= lines * LineHeight + 0.5);
            QVERIFY(p.to <= ellipsisAt);
        }
    }

    void shortTextIsNotCut_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<int>("lines");
        QTest::newRow("one line") << u"Krótko #tag"_s << 1;
        QTest::newRow("fits four") << u"Zażółć gęślą jaźń 📅 2026-10-20"_s << 4;
        QTest::newRow("empty") << QString() << 1;
    }
    void shortTextIsNotCut()
    {
        QFETCH(QString, markdown);
        QFETCH(int, lines);
        QTextDocument doc;
        setUp(doc);
        RichTextPills pills;
        pills.setLineHeight(LineHeight);
        pills.setMaximumLineCount(lines);
        pills.attach(&doc);
        QSignalSpy truncated(&pills, &RichTextPills::truncatedChanged);
        pills.setHtml(render(markdown));
        QVERIFY(!pills.truncated());
        QCOMPARE(truncated.count(), 0);
        QVERIFY(!doc.toPlainText().contains(Ellipsis));
    }

    void exactlyTheLimitIsNotCut()
    {
        // Two paragraphs, two lines: a limit of 2 keeps both; a limit of 1 cuts the second.
        QTextDocument doc;
        setUp(doc);
        RichTextPills pills;
        pills.setLineHeight(LineHeight);
        pills.setMaximumLineCount(2);
        pills.attach(&doc);
        pills.setHtml(
                u"<p>Pierwszy akapit</p><p>Drugi <span style=\"background-color:#ffe08a;color:#333\">#akapit</span></p>"_s);
        QVERIFY(!pills.truncated());
        QCOMPARE(lineCountOf(doc), 2);
        QCOMPARE(pills.pillList().size(), 1);
        pills.setMaximumLineCount(1);
        QVERIFY(pills.truncated());
        QCOMPARE(lineCountOf(doc), 1);
        QVERIFY(doc.toPlainText().startsWith(u"Pierwszy akapit"_s));
        QVERIFY(doc.toPlainText().endsWith(Ellipsis));
        QVERIFY(pills.pillList().isEmpty()); // the tag was in the paragraph cut off
        // Back to no limit: the whole text again.
        pills.setMaximumLineCount(0);
        QVERIFY(!pills.truncated());
        QVERIFY(doc.toPlainText().contains(u"Drugi"_s));
    }

    void blankRestIsNoCut()
    {
        QTextDocument doc;
        setUp(doc);
        RichTextPills pills;
        pills.setMaximumLineCount(1);
        pills.attach(&doc);
        pills.setHtml(u"<p>Jedna linia</p><p>&nbsp;</p><p></p>"_s);
        QVERIFY(!pills.truncated());
        QVERIFY(!doc.toPlainText().contains(Ellipsis));
    }

    void widthChangeCutsAnew()
    {
        QTextDocument doc;
        // (Wide enough for the whole text on one line in any font: DejaVu, the Nix sandbox's only one,
        // is wider than most.)
        setUp(doc, 4000);
        RichTextPills pills;
        pills.setLineHeight(LineHeight);
        pills.setMaximumLineCount(2);
        pills.setWidth(4000);
        pills.attach(&doc);
        pills.setHtml(longText());
        QVERIFY(!pills.truncated());
        const QString whole = doc.toPlainText();
        pills.setWidth(200);
        QVERIFY(pills.truncated());
        QCOMPARE(lineCountOf(doc), 2);
        QCOMPARE(doc.textWidth(), qreal(200));
        // Wider again: the whole text is back (it is set again, not grown from the cut one).
        pills.setWidth(4000);
        QVERIFY(!pills.truncated());
        QCOMPARE(doc.toPlainText(), whole);
        QCOMPARE(brushesLeft(doc), 0);
    }

    void fontChangeCutsAnew()
    {
        QTextDocument doc;
        setUp(doc, 800);
        RichTextPills pills;
        pills.setLineHeight(LineHeight);
        pills.setMaximumLineCount(3);
        pills.setWidth(800);
        QFont small;
        small.setPixelSize(8);
        pills.setFont(small);
        pills.attach(&doc);
        pills.setHtml(longText());
        QVERIFY(!pills.truncated());
        QFont big;
        big.setPixelSize(22);
        pills.setFont(big);
        QCOMPARE(doc.defaultFont().pixelSize(), 22);
        QVERIFY(pills.truncated());
        QCOMPARE(lineCountOf(doc), 3);
    }

    void cutNeverSplitsACharacter_data()
    {
        QTest::addColumn<QString>("html");
        QTest::newRow("emoji") << QString(u"😀🎉👍🏽🇵🇱"_s).repeated(40);
        QTest::newRow("one long word") << QString(u"Konstantynopolitańczykówna"_s).repeated(12);
        QTest::newRow("combining marks") << QString(u"éąż "_s).repeated(80);
    }
    void cutNeverSplitsACharacter()
    {
        QFETCH(QString, html);
        QTextDocument doc;
        setUp(doc, 120);
        RichTextPills pills;
        pills.setMaximumLineCount(2);
        pills.setWidth(120);
        pills.attach(&doc);
        pills.setHtml(html);
        QVERIFY(pills.truncated());
        QVERIFY(lineCountOf(doc) <= 2);
        const QString text = doc.toPlainText();
        QVERIFY(text.endsWith(Ellipsis));
        for (qsizetype i = 0; i < text.size(); ++i) {
            if (text.at(i).isHighSurrogate())
                QVERIFY(i + 1 < text.size() && text.at(i + 1).isLowSurrogate());
            if (text.at(i).isLowSurrogate())
                QVERIFY(i > 0 && text.at(i - 1).isHighSurrogate());
        }
    }

    void cutInsideALink()
    {
        // A long link on the last line shown: the text is cut inside it, "…" is not part of it.
        QTextDocument doc;
        setUp(doc, 200);
        RichTextPills pills;
        pills.setMaximumLineCount(1);
        pills.setWidth(200);
        pills.attach(&doc);
        pills.setHtml(render(u"[[Bardzo długa nazwa strony o wyjeździe do Zakopanego zimą]]"_s));
        QVERIFY(pills.truncated());
        const QTextCharFormat f = lastCharFormat(doc);
        QVERIFY(!f.isAnchor());
        QTextCursor c(&doc);
        c.setPosition(1);
        QVERIFY(c.charFormat().isAnchor()); // the rest of the link stays a link
    }

    void malformedIsCutSafely_data()
    {
        QTest::addColumn<QString>("html");
        QTest::newRow("unclosed tags") << u"<b><i><span style=\"background-color:#eee\">tekst "_s.repeated(30);
        QTest::newRow("table")
                << u"<table><tr><td>a b c d e f g h</td><td>ż ź ć ń ó ł ę ą</td></tr></table>"_s.repeated(10);
        QTest::newRow("line breaks only") << u"<br/>"_s.repeated(50);
        QTest::newRow("pre") << u"<pre>linia\nlinia\nlinia\nlinia</pre>"_s;
        QTest::newRow("escaped script") << u"&lt;script&gt;alert(1)&lt;/script&gt; "_s.repeated(20);
    }
    void malformedIsCutSafely()
    {
        QFETCH(QString, html);
        QTextDocument doc;
        setUp(doc, 150);
        RichTextPills pills;
        pills.setLineHeight(LineHeight);
        pills.setMaximumLineCount(2);
        pills.setWidth(150);
        pills.attach(&doc);
        pills.setHtml(html);
        QCOMPARE(brushesLeft(doc), 0);
        for (const RichTextPills::Pill &p : pills.pillList())
            QVERIFY(p.rect.width() > 0 && p.rect.height() > 0);
    }

    void deferredUntilComplete()
    {
        QTextDocument doc;
        setUp(doc, 200);
        doc.setPlainText(u"poprzedni"_s);
        RichTextPills pills;
        pills.classBegin();
        pills.attach(&doc);
        pills.setHtml(longText());
        pills.setMaximumLineCount(2);
        pills.setWidth(200);
        pills.setLineHeight(LineHeight);
        QCOMPARE(doc.toPlainText(), u"poprzedni"_s); // nothing until it is complete
        pills.componentComplete();
        QVERIFY(pills.truncated());
        QCOMPARE(lineCountOf(doc), 2);
    }

    void withoutHtmlTheDocumentIsLeftAlone()
    {
        // BlockText's mode: the TextEdit's own text, never replaced or cut.
        QTextDocument doc;
        setUp(doc, 100);
        RichTextPills pills;
        pills.setMaximumLineCount(1);
        pills.attach(&doc);
        doc.setHtml(longText());
        QVERIFY(!pills.truncated());
        QVERIFY(!doc.toPlainText().contains(Ellipsis));
        QVERIFY(lineCountOf(doc) > 1);
        QCOMPARE(brushesLeft(doc), 0);
    }
};

QTEST_MAIN(TestRichPills)
#include "tst_editor_richpills.moc"
