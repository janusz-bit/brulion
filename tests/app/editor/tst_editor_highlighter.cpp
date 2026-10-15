// MarkdownHighlighter (src/app/editor/markdownhighlighter.h) on a QTextDocument in an offscreen
// QGuiApplication: which formats land on which characters, multi-line states in source mode,
// palette colours and the fixed line height.
#include "markdownhighlighter.h"

#include <QCoreApplication>
#include <QFont>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

constexpr int NameProperty = QTextFormat::UserProperty + 1;

/// Format name at character `pos` of block `blockNumber` ("" when unformatted).
QString nameAt(const QTextDocument &doc, int blockNumber, int pos)
{
    const QTextBlock block = doc.findBlockByNumber(blockNumber);
    QString name;
    for (const QTextLayout::FormatRange &r : block.layout()->formats()) {
        if (pos >= r.start && pos < r.start + r.length)
            name = r.format.property(NameProperty).toString();
    }
    return name;
}

QTextCharFormat formatAt(const QTextDocument &doc, int blockNumber, int pos)
{
    const QTextBlock block = doc.findBlockByNumber(blockNumber);
    QTextCharFormat f;
    for (const QTextLayout::FormatRange &r : block.layout()->formats()) {
        if (pos >= r.start && pos < r.start + r.length)
            f = r.format;
    }
    return f;
}

/// Index of `needle` in block `blockNumber`.
int at(const QTextDocument &doc, int blockNumber, const QString &needle)
{
    return int(doc.findBlockByNumber(blockNumber).text().indexOf(needle));
}

} // namespace

class TestHighlighter : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void inlineFormats_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("needle");
        QTest::addColumn<QString>("format");
        QTest::newRow("heading") << u"## Koszty remontu"_s << u"Koszty"_s << u"heading"_s;
        QTest::newRow("heading marker") << u"## Koszty"_s << u"##"_s << u"marker"_s;
        QTest::newRow("bold") << u"to **ważne** słowo"_s << u"ważne"_s << u"bold"_s;
        QTest::newRow("bold marker") << u"to **ważne**"_s << u"**"_s << u"marker"_s;
        QTest::newRow("italic") << u"bardzo *pochyłe* słowo"_s << u"pochyłe"_s << u"italic"_s;
        QTest::newRow("italic underscore") << u"bardzo _pochyłe_ słowo"_s << u"pochyłe"_s << u"italic"_s;
        QTest::newRow("snake_case is not italic") << u"zmienna_nazwa_tu"_s << u"nazwa"_s << QString();
        QTest::newRow("strike") << u"~~stare~~ nowe"_s << u"stare"_s << u"strike"_s;
        QTest::newRow("highlight") << u"==zaznacz=="_s << u"zaznacz"_s << u"highlight"_s;
        QTest::newRow("code") << u"kod `x = 1` tu"_s << u"x = 1"_s << u"code"_s;
        QTest::newRow("no syntax in code") << u"`#tag [[a]]`"_s << u"tag"_s << u"code"_s;
        QTest::newRow("wiki link") << u"zobacz [[Remont łazienki]]"_s << u"Remont"_s << u"link"_s;
        QTest::newRow("wiki brackets") << u"zobacz [[Remont]]"_s << u"[["_s << u"marker"_s;
        QTest::newRow("embed") << u"![[szkic.png]]"_s << u"szkic"_s << u"link"_s;
        QTest::newRow("block ref") << u"((abc123))"_s << u"abc"_s << u"blockRef"_s;
        QTest::newRow("md link text") << u"[strona](https://example.com)"_s << u"strona"_s << u"link"_s;
        QTest::newRow("md link url") << u"[strona](https://example.com)"_s << u"example"_s << u"faint"_s;
        QTest::newRow("url") << u"wejdź na https://example.com/żółw dziś"_s << u"example"_s << u"url"_s;
        QTest::newRow("tag") << u"Kupić #zakupy dziś"_s << u"zakupy"_s << u"tag"_s;
        QTest::newRow("polish tag") << u"x #żółw"_s << u"żółw"_s << u"tag"_s;
        QTest::newRow("number is no tag") << u"pozycja #1"_s << u"1"_s << QString();
        QTest::newRow("due") << u"[ ] x 📅 2026-10-16"_s << u"2026"_s << u"due"_s;
        QTest::newRow("scheduled") << u"x ⏳ 2026-10-16"_s << u"2026"_s << u"scheduled"_s;
        QTest::newRow("reminder") << u"x ⏰ 2026-10-16 09:00"_s << u"09:00"_s << u"reminder"_s;
        QTest::newRow("alarm") << u"x ⏰! 2026-10-16 07:00"_s << u"07:00"_s << u"alarm"_s;
        QTest::newRow("recurrence") << u"x 🔁 every week on Monday"_s << u"Monday"_s << u"recurrence"_s;
        QTest::newRow("done") << u"x ✅ 2026-10-14"_s << u"2026"_s << u"done"_s;
        QTest::newRow("priority") << u"x ⏫"_s << u"⏫"_s << u"priority"_s;
        QTest::newRow("block id") << u"zdanie ^abc-1"_s << u"abc"_s << u"blockId"_s;
        QTest::newRow("property") << u"status:: w toku"_s << u"status"_s << u"property"_s;
        QTest::newRow("quote marker") << u"> cytat"_s << u">"_s << u"marker"_s;
        QTest::newRow("checkbox typed") << u"[ ] zadanie"_s << u"[ ]"_s << u"marker"_s;
        QTest::newRow("comment") << u"widać %%ukryte%% dalej"_s << u"ukryte"_s << u"comment"_s;
        QTest::newRow("plain") << u"zwykły tekst ąćęłńóśźż"_s << u"tekst"_s << QString();
    }

    void inlineFormats()
    {
        QFETCH(QString, text);
        QFETCH(QString, needle);
        QFETCH(QString, format);
        QTextDocument doc;
        doc.setPlainText(text);
        MarkdownHighlighter h;
        h.attach(&doc);
        const int pos = at(doc, 0, needle);
        QVERIFY(pos >= 0);
        QCOMPARE(nameAt(doc, 0, pos), format);
    }

    void listMarkersOnlyInSource()
    {
        QTextDocument doc;
        doc.setPlainText(u"- [ ] zadanie\n\t- dziecko"_s);
        MarkdownHighlighter h;
        h.attach(&doc);
        QCOMPARE(nameAt(doc, 0, 0), QString()); // inline mode: "- " is text
        h.setMode(u"source"_s);
        QCOMPARE(nameAt(doc, 0, 0), u"marker"_s);
        QCOMPARE(nameAt(doc, 0, 3), u"marker"_s); // checkbox
        QCOMPARE(nameAt(doc, 1, 1), u"marker"_s);
    }

    void sourceModeStates()
    {
        QTextDocument doc;
        doc.setPlainText(u"---\ntytuł: Remont\n---\n# Nagłówek\n```calc\n# nie nagłówek\n```\nPo %%komentarz\nna dwie linie%% #tag"_s);
        MarkdownHighlighter h;
        h.setMode(u"source"_s);
        h.setMonoFamily(u"monospace"_s);
        h.attach(&doc);
        QCOMPARE(nameAt(doc, 0, 0), u"fence"_s);           // ---
        QCOMPARE(nameAt(doc, 1, 0), u"property"_s);        // tytuł:
        QCOMPARE(nameAt(doc, 1, at(doc, 1, u"Remont"_s)), u"muted"_s);
        QCOMPARE(nameAt(doc, 2, 0), u"fence"_s);           // closing ---
        QCOMPARE(nameAt(doc, 3, 2), u"heading"_s);
        QCOMPARE(nameAt(doc, 4, 0), u"fence"_s);           // ```calc
        QCOMPARE(nameAt(doc, 5, 2), u"code"_s);            // not a heading inside the fence
        QVERIFY(formatAt(doc, 5, 2).fontFamilies().toStringList().contains(u"monospace"_s));
        QCOMPARE(nameAt(doc, 6, 0), u"fence"_s);
        QCOMPARE(nameAt(doc, 7, at(doc, 7, u"komentarz"_s)), u"comment"_s);
        QCOMPARE(nameAt(doc, 8, 0), u"comment"_s);
        QCOMPARE(nameAt(doc, 8, at(doc, 8, u"tag"_s)), u"tag"_s);
        // Inline mode: no frontmatter state.
        h.setMode(u"inline"_s);
        QCOMPARE(nameAt(doc, 1, 0), QString()); // "tytuł:" is plain text outside frontmatter
        QCOMPARE(nameAt(doc, 3, 2), u"heading"_s);
    }

    void codeMode()
    {
        QTextDocument doc;
        doc.setPlainText(u"a = 1 # nie komentarz\n# komentarz\n// też\nb = a * 2"_s);
        MarkdownHighlighter h;
        h.setMode(u"code"_s);
        h.attach(&doc);
        QCOMPARE(nameAt(doc, 0, 0), QString());
        QCOMPARE(nameAt(doc, 1, 2), u"comment"_s);
        QCOMPARE(nameAt(doc, 2, 0), u"comment"_s);
        QCOMPARE(nameAt(doc, 3, 0), QString());
    }

    void paletteColours()
    {
        QTextDocument doc;
        doc.setPlainText(u"#tag [[link]] ⏰ 2026-10-16 09:00"_s);
        MarkdownHighlighter h;
        QSignalSpy spy(&h, &MarkdownHighlighter::paletteChanged);
        h.attach(&doc);
        h.setPalette({{u"tag"_s, QColor(u"#010203"_s)}, {u"link"_s, u"#0a0b0c"_s}, {u"reminder"_s, u"#aabbcc"_s}});
        QCOMPARE(spy.size(), 1);
        QCOMPARE(formatAt(doc, 0, 1).foreground().color(), QColor(u"#010203"_s));
        QCOMPARE(formatAt(doc, 0, at(doc, 0, u"link"_s)).foreground().color(), QColor(u"#0a0b0c"_s));
        QCOMPARE(formatAt(doc, 0, at(doc, 0, u"09:00"_s)).foreground().color(), QColor(u"#aabbcc"_s));
        QCOMPARE(formatAt(doc, 0, at(doc, 0, u"#tag"_s)).fontWeight(), int(QFont::Normal));
    }

    void lineHeight()
    {
        QTextDocument doc;
        doc.setPlainText(u"pierwsza\ndruga"_s);
        MarkdownHighlighter h;
        h.attach(&doc);
        QCOMPARE(doc.firstBlock().blockFormat().lineHeightType(), int(QTextBlockFormat::SingleHeight));
        h.setLineHeight(26);
        for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
            QCOMPARE(b.blockFormat().lineHeightType(), int(QTextBlockFormat::FixedHeight));
            QCOMPARE(b.blockFormat().lineHeight(), 26.0);
        }
        QVERIFY(!doc.isUndoRedoEnabled());
        // New text (TextEdit sets it with setPlainText) gets the line height too.
        doc.setPlainText(u"nowa\nzawartość\ni trzecia"_s);
        for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
            QCOMPARE(b.blockFormat().lineHeight(), 26.0);
        // Typing a line break: the new paragraph too.
        QTextCursor c(&doc);
        c.movePosition(QTextCursor::End);
        c.insertText(u"\nczwarta"_s);
        QCOMPARE(doc.lastBlock().blockFormat().lineHeight(), 26.0);
        QCOMPARE(doc.toPlainText(), u"nowa\nzawartość\ni trzecia\nczwarta"_s);
    }

    void rehighlightsWhileTyping()
    {
        QTextDocument doc;
        doc.setPlainText(u"Kupić "_s);
        MarkdownHighlighter h;
        h.attach(&doc);
        QTextCursor c(&doc);
        c.movePosition(QTextCursor::End);
        c.insertText(u"#zakupy"_s);
        QCOMPARE(nameAt(doc, 0, 7), u"tag"_s);
        h.attach(nullptr);
        QVERIFY(!h.document());
    }
};

QTEST_MAIN(TestHighlighter)
#include "tst_editor_highlighter.moc"
