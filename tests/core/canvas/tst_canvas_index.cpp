// What the index reads of a canvas (canvas/indextext.h): the Markdown text of its cards, the
// links of file and text nodes, what of a note never applies (tasks, reminders, properties, ids),
// the card at a line, and the rewriting of paths and links on a rename; the SVG picture
// (canvas/svg.h).
#include "canvas/edit.h"
#include "canvas/indextext.h"
#include "canvas/svg.h"
#include "fixtures.h"

#include <QTest>

using namespace brulion;
using namespace brulion::canvas;
using namespace Qt::StringLiterals;

namespace {

Canvas read(const QString &text)
{
    ReadResult r = readCanvas(text);
    if (!r.canvas)
        qFatal("not a canvas");
    return *r.canvas;
}

QStringList targets(const ParsedNote &note)
{
    QStringList out;
    for (const Link &l : note.links)
        out.append(l.target + (l.heading.isEmpty() ? QString() : u'#' + l.heading));
    return out;
}

} // namespace

class TestCanvasIndex : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void textOfCards()
    {
        const IndexText t = indexText(read(canvasfixtures::obsidian()));
        // Group label, the text card, the note, its section, the picture, the web page, the label.
        QVERIFY(t.text.startsWith(u"Remont kuchni\n\n# Plan\n\n- [ ] zamówić płytki\n"_s));
        QVERIFY(t.text.contains(
                u"\n\n[[Projekty/Kuchnia.md]]\n\n[[Projekty/Kuchnia.md#Szafki]]\n\n[[assets/płytki.jpg]]\n\n"
                u"<https://example.com/żółw>\n\nszczegóły"_s));
        QCOMPARE(t.segments.size(), 7);
        QCOMPARE(t.segments.at(0).nodeId, u"5b4c2f0e9a1d3c7e"_s);
        QCOMPARE(t.segments.at(1).firstLine, 2);
        QCOMPARE(t.segments.at(1).lastLine, 7);
        QCOMPARE(t.segments.constLast().edgeId, u"e1e1e1e1e1e1e1e1"_s);
        QCOMPARE(nodeAtLine(t, 4), u"a1b2c3d4e5f60718"_s);
        QString edge;
        QCOMPARE(nodeAtLine(t, t.segments.constLast().firstLine, &edge), QString());
        QCOMPARE(edge, u"e1e1e1e1e1e1e1e1"_s);
        QCOMPARE(nodeAtLine(t, 1), QString()); // the blank line between two cards
        QCOMPARE(nodeAtLine(t, 999), QString());
    }

    void parsedNote()
    {
        const IndexText t = indexText(read(canvasfixtures::obsidian()));
        const ParsedNote note = indexNote(u"Plany/Kuchnia.canvas"_s, t.text);
        QCOMPARE(note.title, u"Kuchnia"_s);
        QCOMPARE(targets(note),
                 (QStringList{u"Budżet domowy"_s, u"Projekty/Kuchnia.md"_s, u"Projekty/Kuchnia.md#Szafki"_s,
                              u"assets/płytki.jpg"_s, u"https://example.com/żółw"_s}));
        QVERIFY(note.tags.contains(u"remont"_s));
        // A card's task is no task of the notebook.
        QVERIFY(note.tasks.isEmpty());
        QVERIFY(std::ranges::none_of(note.blocks, [](const Block &b) { return bool(b.task); }));
    }

    void noteThingsLeftOut()
    {
        Canvas c;
        c = addNode(c, makeTextNode(u"---\ntags: [x]\naliases: [Inna]\n---\nkey:: value"_s, QRectF(0, 0, 250, 60)));
        c = addNode(c,
                    makeTextNode(u"- [ ] zadanie 📅 2026-11-21 ⏰ 2026-11-21 09:00 ^blok1"_s, QRectF(0, 100, 250, 60)));
        const IndexText t = indexText(c);
        QVERIFY(t.text.startsWith(u"\n---"_s)); // never frontmatter
        const ParsedNote note = indexNote(u"journals/2026-11-20.canvas"_s, t.text);
        QVERIFY(note.frontmatter.isEmpty());
        QVERIFY(note.aliases.isEmpty());
        QVERIFY(note.properties.isEmpty());
        QVERIFY(note.tasks.isEmpty());
        QVERIFY(note.reminders.isEmpty());
        QVERIFY(note.blockIds.isEmpty());
        QVERIFY(!note.journalDate);
        for (const Block &b : note.blocks) {
            QVERIFY(b.reminders.isEmpty());
            QVERIFY(b.id.isEmpty());
        }
    }

    void cardsStayApart()
    {
        // An unclosed fence or math block in one card never swallows the next card.
        Canvas c;
        c = addNode(c, makeTextNode(u"```js\nlet a = 1"_s, QRectF(0, 0, 250, 60)));
        c = addNode(c, makeTextNode(u"$$\nx^2"_s, QRectF(0, 100, 250, 60)));
        c = addNode(c, makeTextNode(u"<!-- komentarz"_s, QRectF(0, 200, 250, 60)));
        c = addNode(c, makeTextNode(u"Zobacz [[Dom]]\r\n\r\n"_s, QRectF(0, 300, 250, 60)));
        const IndexText t = indexText(c);
        QVERIFY(t.text.contains(u"let a = 1\n```\n\n$$\nx^2\n$$\n\n<!-- komentarz -->\n\nZobacz [[Dom]]"_s));
        QVERIFY(!t.text.contains(u'\r'));
        const ParsedNote note = indexNote(u"A.canvas"_s, t.text);
        QCOMPARE(targets(note), QStringList{u"Dom"_s});
    }

    void fileLinks_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("subpath");
        QTest::addColumn<QString>("link");
        QTest::newRow("note") << u"Dom.md"_s << QString() << u"[[Dom.md]]"_s;
        QTest::newRow("heading") << u"Dom.md"_s << u"#Ogród"_s << u"[[Dom.md#Ogród]]"_s;
        QTest::newRow("block") << u"a/Dom.md"_s << u"#^abc"_s << u"[[a/Dom.md#^abc]]"_s;
        QTest::newRow("subpath without #") << u"Dom.md"_s << u"Ogród"_s << u"[[Dom.md#Ogród]]"_s;
        QTest::newRow("brackets") << u"a/[Szkic] 1.png"_s << QString() << u"[(Szkic) 1.png](a/%5BSzkic%5D%201.png)"_s;
        QTest::newRow("hash") << u"C#.md"_s << QString() << u"[C#.md](C%23.md)"_s;
    }
    void fileLinks()
    {
        QFETCH(QString, path);
        QFETCH(QString, subpath);
        QFETCH(QString, link);
        QCOMPARE(fileLink(path, subpath), link);
        // Each resolves to its file.
        const ParsedNote note = indexNote(u"x.canvas"_s, link);
        QCOMPARE(note.links.size(), 1);
        QCOMPARE(note.links.at(0).target, path);
    }

    void unreadableFile()
    {
        QCOMPARE(indexText(u"nie JSON"_s).text, QString());
        QCOMPARE(indexText(QString()).text, QString());
    }

    void rewritePaths()
    {
        const QString text = canvasfixtures::obsidian();
        int paths = 0;
        const std::optional<QString> out = rewriteCanvas(
                text,
                [](const QString &t) {
                    QString s = t;
                    return s.replace(u"[[Budżet domowy]]"_s, u"[[Finanse/Budżet domowy]]"_s);
                },
                [](const QString &p) { return p == u"Projekty/Kuchnia.md" ? u"Archiwum/Kuchnia.md"_s : p; }, &paths);
        QVERIFY(out);
        QCOMPARE(paths, 2);
        QVERIFY(out->contains(u"\"file\":\"Archiwum/Kuchnia.md\",\"x\":-40"_s));
        QVERIFY(out->contains(u"\"file\":\"Archiwum/Kuchnia.md\",\"subpath\":\"#Szafki\""_s));
        QVERIFY(out->contains(u"[[Finanse/Budżet domowy]]"_s));
        // Untouched lines as they were.
        QVERIFY(out->contains(
                u"\t\t{\"id\":\"99aabbccddeeff00\",\"type\":\"file\",\"file\":\"assets/płytki.jpg\",\"x\":-400,\"y\":0,\"width\":320,\"height\":240},"_s));
        // Nothing to change: the same text, byte for byte.
        const std::optional<QString> same =
                rewriteCanvas(text, [](const QString &t) { return t; }, [](const QString &p) { return p; });
        QCOMPARE(*same, text);
        QVERIFY(!rewriteCanvas(u"{"_s, {}, {}));
    }

    void groupBackgroundsRewritten()
    {
        Canvas c;
        Node g = makeNode(GroupType, QRectF(0, 0, 400, 300));
        g.background = u"assets/tło.png"_s;
        c = addNode(c, g);
        int paths = 0;
        const std::optional<QString> out = rewriteCanvas(
                writeCanvas(c), {}, [](const QString &p) { return p == u"assets/tło.png" ? u"obrazy/tło.png"_s : p; },
                &paths);
        QCOMPARE(paths, 1);
        QVERIFY(out->contains(u"\"background\":\"obrazy/tło.png\""_s));
    }

    void svgPicture()
    {
        Canvas c = read(canvasfixtures::obsidian());
        c = setNodeText(c, u"a1b2c3d4e5f60718"_s, u"# Plan <script>alert(1)</script>\n\n- [x] zrobione & gotowe"_s);
        SvgOptions o;
        o.pictureDataUrl = [](const QString &path) {
            return path.endsWith(u".jpg") ? u"data:image/jpeg;base64,AAAA"_s : QString();
        };
        o.noteText = [](const QString &, const QString &) { return u"Szafki i blaty"_s; };
        const QString svg = toSvg(c, o);
        QVERIFY(svg.startsWith(u"<svg xmlns=\"http://www.w3.org/2000/svg\""_s));
        QVERIFY(svg.contains(u"Remont kuchni"_s));
        QVERIFY(svg.contains(u">Plan"_s));
        QVERIFY(!svg.contains(u"<script"_s));
        QVERIFY(svg.contains(u"[x] zrobione &amp; gotowe"_s));
        QVERIFY(svg.contains(u"href=\"data:image/jpeg;base64,AAAA\""_s));
        QVERIFY(svg.contains(u"Szafki i blaty"_s));
        QVERIFY(svg.contains(u"https://example.com/żółw"_s));
        QVERIFY(svg.contains(u"szczegóły"_s));
        QVERIFY(svg.contains(u"stroke=\"#ec7500\""_s)); // the orange edge
        // A data URL that is no picture is never embedded.
        o.pictureDataUrl = [](const QString &) { return u"javascript:alert(1)"_s; };
        QVERIFY(!toSvg(c, o).contains(u"javascript"_s));
        const QSizeF size = svgSize(c, o);
        QVERIFY(size.width() > 1300 && size.width() < 1500);
        // A huge canvas is scaled down to the limit.
        Canvas big;
        big = addNode(big, makeTextNode(u"a"_s, QRectF(0, 0, 100000, 100)));
        QCOMPARE(svgSize(big).width(), 8192.0);
        QVERIFY(toSvg(Canvas()).contains(u"<svg"_s));
    }

    void plainCardLines()
    {
        QList<int> levels;
        const QStringList lines = plainLines(
                u"# Tytuł\n\n**Ważne** i *mniej* [[Strona|link]]\n- punkt\n  - [ ] podpunkt"_s, 40, 10, &levels);
        QCOMPARE(lines,
                 (QStringList{u"Tytuł"_s, QString(), u"Ważne i mniej link"_s, u"• punkt"_s, u" [ ] podpunkt"_s}));
        QCOMPARE(levels.at(0), 1);
        QCOMPARE(plainLines(u"jeden dwa trzy cztery"_s, 9, 10), (QStringList{u"jeden dwa"_s, u"trzy"_s, u"cztery"_s}));
        QCOMPARE(plainLines(u"a\nb\nc\nd"_s, 10, 2).size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestCanvasIndex)
#include "tst_canvas_index.moc"
