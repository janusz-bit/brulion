// RichTextPills (src/app/editor/richtextpills.h) on a QTextDocument in an offscreen
// QGuiApplication, fed with real InlineRenderer HTML: the background brushes of chips, tags, code
// and highlights are taken off (no square is painted), their colours and kinds become rectangles
// from the document's own layout — one per line of a wrapped chip, centred on the glyphs inside
// the fixed-height line —, every paragraph gets the fixed line height, and malformed input never
// leaves a square behind.
#include "markdown/inlinerenderer.h"
#include "richtextpills.h"

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

} // namespace

class TestRichPills : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void kinds_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<QStringList>("expected"); // a kind per tinted span, in order
        QTest::newRow("polish tag") << u"Wyjazd #podróże w sierpniu"_s << QStringList{u"pill"_s};
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
};

QTEST_MAIN(TestRichPills)
#include "tst_editor_richpills.moc"
