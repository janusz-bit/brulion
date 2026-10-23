// Benchmarks of the markdown module: parseNote on typical notes (1–50 KB, a 1 MB note, a
// journal outline) and on pathological input (raw inline HTML, long lists, many links/tags) at
// several sizes (to expose super-linear scaling); BlockDocument load / serialise / editing
// operations on 500 and 5000 blocks (including what one keystroke costs in the core); the
// InlineRenderer per block and for a whole page; renameLinks.
#include "benchdata.h"

#include "markdown/markdown.h"

#include <QHash>
#include <QTest>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

QString typicalNote(int kb)
{
    bench::Rng r(quint64(kb) * 31 + 1);
    const QString note = bench::documentNote(r, kb, 500, qsizetype(kb) * 1024);
    bench::reportInput(u"typicalNote %1 KB"_s.arg(kb), note);
    return note;
}

QString outline(int blocks)
{
    bench::Rng r{quint64(blocks)};
    const QString note = bench::outlineNote(r, blocks, 500);
    bench::reportInput(u"outline %1 blocks"_s.arg(blocks), note);
    return note;
}

RenderContext renderContext()
{
    static QHash<QString, QString> pages = [] {
        QHash<QString, QString> h;
        for (int i = 0; i < 500; ++i)
            h.insert(u"Strona %1"_s.arg(i), u"Strony/Folder %1/Strona %2.md"_s.arg(i % 50).arg(i));
        return h;
    }();
    RenderContext ctx;
    ctx.resolvePage = [](const QString &target) -> std::optional<QString> {
        const auto it = pages.constFind(target);
        return it == pages.cend() ? std::nullopt : std::optional<QString>(*it);
    };
    ctx.blockText = [](const QString &id) -> std::optional<QString> { return u"Tekst bloku "_s + id; };
    ctx.inlineCalc = [](const QString &) { return u"42"_s; };
    ctx.linkTitle = journalLinkTitle(VaultConfig(), dates::UiLocale::Polish, bench::fixedToday());
    ctx.today = bench::fixedToday();
    return ctx;
}

/// A block index in the middle of `doc` that is a list item with a previous sibling (indent()
/// and moveUp() do something there).
int middleItem(const BlockDocument &doc)
{
    for (int i = doc.count() / 2; i < doc.count(); ++i) {
        if (doc.at(i).kind != BlockKind::ListItem || doc.hasChildren(i))
            continue;
        for (int j = i - 1; j >= 0; --j) {
            if (doc.at(j).depth < doc.at(i).depth)
                break;
            if (doc.at(j).depth == doc.at(i).depth && doc.at(j).parent == doc.at(i).parent)
                return i;
        }
    }
    return doc.count() / 2;
}

} // namespace

class BenchMarkdown : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- parseNote ------------------------------------------------------------------------

    void parseTypical_data()
    {
        QTest::addColumn<QString>("content");
        for (int kb : {1, 5, 20, 50})
            QTest::newRow(qPrintable(u"document %1 KB"_s.arg(kb))) << typicalNote(kb);
        QTest::newRow("document 1 MB") << typicalNote(1024);
        QTest::newRow("journal outline 200 blocks") << outline(200);
        QTest::newRow("outline 5000 blocks") << outline(5000);
    }
    void parseTypical()
    {
        QFETCH(QString, content);
        const VaultConfig config;
        QBENCHMARK {
            const ParsedNote note = parseNote(u"Strony/Notatka.md"_s, content, config);
            Q_UNUSED(note);
        }
    }

    // Pathological input at 2–3 sizes each: time per row should grow linearly with n.
    void parsePathological_data()
    {
        QTest::addColumn<QString>("content");
        for (int n : bench::sizes({2000, 5000, 20000}, {2000, 5000, 10000}))
            QTest::newRow(qPrintable(u"inline html x%1"_s.arg(n))) << bench::inlineHtml(n);
        for (int n : bench::sizes({1000, 5000, 20000, 50000}, {1000, 5000}))
            QTest::newRow(qPrintable(u"flat list %1 items"_s.arg(n))) << bench::longList(n);
        for (int n : bench::sizes({1000, 5000, 20000}, {1000, 5000})) {
            const QString content = bench::manyLinksTags(n);
            bench::reportInput(u"manyLinksTags %1"_s.arg(n), content);
            QTest::newRow(qPrintable(u"links+tags %1 lines"_s.arg(n))) << content;
        }
        // One long paragraph of links (one block, many inline items).
        for (int n : bench::sizes({1000, 5000}, {1000, 5000})) {
            QString p;
            for (int i = 0; i < n; ++i)
                p += u"[[Strona %1]] #tag%2 "_s.arg(i).arg(i % 50);
            QTest::newRow(qPrintable(u"one paragraph %1 links"_s.arg(n))) << p + u'\n';
        }
        // '<' that never closes an autolink, repeated in one paragraph.
        for (int n : bench::sizes({5000, 20000}, {5000})) {
            QString p;
            for (int i = 0; i < n; ++i)
                p += u"a < b "_s;
            QTest::newRow(qPrintable(u"one paragraph %1 '<'"_s.arg(n))) << p + u'\n';
        }
    }
    void parsePathological() { parseTypical(); }

    // ---- BlockDocument ----------------------------------------------------------------------

    void blockDocLoad_data()
    {
        QTest::addColumn<QString>("content");
        QTest::newRow("outline 500 blocks") << outline(500);
        QTest::newRow("outline 5000 blocks") << outline(5000);
        QTest::newRow("document 50 KB") << typicalNote(50);
        QTest::newRow("document 1 MB") << typicalNote(1024);
    }
    void blockDocLoad()
    {
        QFETCH(QString, content);
        QBENCHMARK {
            BlockDocument doc(content);
            Q_UNUSED(doc);
        }
    }

    void blockDocToString_data() { blockDocLoad_data(); }
    void blockDocToString()
    {
        QFETCH(QString, content);
        const BlockDocument doc(content);
        QCOMPARE(doc.toString(), content);
        QBENCHMARK {
            const QString s = doc.toString();
            Q_UNUSED(s);
        }
    }

    // Editing operations in the middle of an outline, as reversible pairs so every iteration
    // starts from the same document.
    void blockDocOps_data()
    {
        QTest::addColumn<int>("blocks");
        QTest::addColumn<QString>("op");
        for (int n : {500, 5000}) {
            for (const QString &op : {u"setBody"_s, u"split+merge"_s, u"insertAfter+remove"_s, u"indent+outdent"_s,
                                      u"moveDown+moveUp"_s, u"toggleTask x2"_s, u"keystroke"_s})
                QTest::newRow(qPrintable(u"%1 blocks %2"_s.arg(n).arg(op))) << n << op;
        }
    }
    void blockDocOps()
    {
        QFETCH(int, blocks);
        QFETCH(QString, op);
        BlockDocument doc(outline(blocks));
        const int i = middleItem(doc);
        const QString body = doc.at(i).body;
        const quint64 uid = doc.at(i).uid;
        const QDate today = bench::fixedToday();
        int flip = 0;
        qsizetype sink = 0;
        QBENCHMARK {
            if (op == u"setBody") {
                doc.setBody(i, (flip++ & 1) ? body : body + u" x"_s);
            } else if (op == u"split+merge") {
                const EditResult r = doc.split(i, int(body.size() / 2));
                doc.mergeWithPrevious(r.focus);
            } else if (op == u"insertAfter+remove") {
                const EditResult r = doc.insertAfter(i, u"Nowy blok z tekstem"_s, doc.at(i).depth);
                doc.remove(r.focus);
            } else if (op == u"indent+outdent") {
                doc.indent(doc.indexOfUid(uid));
                doc.outdent(doc.indexOfUid(uid));
            } else if (op == u"moveDown+moveUp") {
                doc.moveUp(doc.indexOfUid(uid));
                doc.moveDown(doc.indexOfUid(uid));
            } else if (op == u"toggleTask x2") {
                doc.toggleTask(i, today);
                doc.toggleTask(i, today);
            } else {
                // One typed character as PageController handles it (setBody, then markDirty()):
                // the block's new body, the whole text (dirty check, later the save) and the
                // frontmatter mode key of the new text (resolveMode() returns when it is unchanged).
                doc.setBody(i, (flip++ & 1) ? body : body + u"x"_s);
                const QString text = doc.toString();
                const std::optional<NoteMode> key = markdown::frontmatterNoteMode(text);
                sink += text.size() + (key ? 1 : 0);
            }
        }
        QVERIFY(sink >= 0);
        QVERIFY(doc.count() >= blocks / 2);
    }

    // Selecting every block of an outline and copying it (PageController::selectAll() and
    // copySelection(), docs/UI.md "Selecting and copying"): the top rows of the range and the
    // clipboard's Markdown, or its plain text ("Kopiuj jako tekst"); the HTML flavour is
    // bench_io clipboardHtml.
    void selectAllCopy_data()
    {
        QTest::addColumn<int>("blocks");
        QTest::addColumn<QString>("what");
        for (int n : {500, 5000, 20000}) {
            for (const QString &w : {u"markdown"_s, u"plain text"_s})
                QTest::newRow(qPrintable(u"%1 blocks %2"_s.arg(n).arg(w))) << n << w;
        }
    }
    void selectAllCopy()
    {
        QFETCH(int, blocks);
        QFETCH(QString, what);
        const BlockDocument doc(outline(blocks));
        const QString before = doc.toString();
        PlainTextOptions options;
        options.blockText = [](const QString &id) { return u"Tekst bloku "_s + id; };
        qsizetype size = 0;
        QBENCHMARK {
            const QList<int> tops = doc.rangeTops(0, doc.count() - 1);
            if (what == u"plain text")
                size = blocksPlainText(doc, tops, options).size();
            else
                size = blocksMarkdown(doc, tops).size();
        }
        QVERIFY(size > before.size() / 2);
    }

    // ---- InlineRenderer ----------------------------------------------------------------------

    void renderBlock_data()
    {
        QTest::addColumn<QString>("body");
        QTest::newRow("plain sentence") << u"Kupić farbę do łazienki i zadzwonić do mamy w sprawie obiadu."_s;
        QTest::newRow("link + tag + bold")
                << u"Spotkanie o **budżecie** z [[Strona 12]] i [[Strona 7|zespołem]] #praca #projekt/brulion"_s;
        QTest::newRow("task with dates")
                << u"[ ] Opłacić rachunek za prąd 📅 2026-10-20 🔁 every month ⏰ 2026-10-19 18:00"_s;
        QTest::newRow("journal link + url + code")
                << u"Notatki z [[2026-10-13]]: `kod` i https://example.com/a/b?c=d oraz ((b12x3))"_s;
        bench::Rng r(2);
        QString longPara;
        for (int k = 0; k < 20; ++k) {
            const QString text = bench::sentence(r);
            const QString target = bench::pageTarget(r, 500);
            const QString tag = r.pick(bench::tagNames());
            longPara += text + u" [["_s + target + u"]] #"_s + tag + u' ';
        }
        bench::reportInput(u"renderBlock paragraph"_s, longPara);
        QTest::newRow("paragraph 2 KB, 20 links") << longPara;
    }
    void renderBlock()
    {
        QFETCH(QString, body);
        const InlineRenderer renderer(renderContext());
        const BlockKind kind = body.startsWith(u"[ ]"_s) ? BlockKind::ListItem : BlockKind::Paragraph;
        QBENCHMARK {
            const QString html = renderer.renderBlock(body, kind);
            Q_UNUSED(html);
        }
    }

    // Every block of a page rendered once (opening a page whose delegates are all created).
    void renderPage_data()
    {
        QTest::addColumn<int>("blocks");
        for (int n : {200, 5000})
            QTest::newRow(qPrintable(u"%1 blocks"_s.arg(n))) << n;
    }
    void renderPage()
    {
        QFETCH(int, blocks);
        const BlockDocument doc(outline(blocks));
        const InlineRenderer renderer(renderContext());
        QBENCHMARK {
            qsizetype total = 0;
            for (int i = 0; i < doc.count(); ++i)
                total += renderer.renderBlock(doc.at(i).body, doc.at(i).kind).size();
            Q_UNUSED(total);
        }
    }

    // ---- other pure edits -----------------------------------------------------------------

    void renameLinks_data()
    {
        QTest::addColumn<QString>("content");
        QTest::newRow("document 5 KB") << typicalNote(5);
        QTest::newRow("document 50 KB") << typicalNote(50);
    }
    void renameLinks()
    {
        QFETCH(QString, content);
        QBENCHMARK {
            const QString s = markdown::renameLinks(content, u"Strona 3"_s, u"Strona trzecia"_s);
            Q_UNUSED(s);
        }
    }

    void noteMode_data() { blockDocLoad_data(); }
    void noteMode()
    {
        QFETCH(QString, content);
        const VaultConfig config;
        QBENCHMARK {
            const ResolvedNoteMode m = markdown::resolveNoteMode(content, false, config);
            Q_UNUSED(m);
        }
    }
};

QTEST_GUILESS_MAIN(BenchMarkdown)
#include "bench_markdown.moc"
