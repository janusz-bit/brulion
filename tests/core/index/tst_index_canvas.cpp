// Canvases in the index (docs/SYNTAX.md §2 "Canvases"): a `.canvas` file is a note of its cards'
// text — its file nodes and its text nodes' links are links (backlinks, the graph), its words are
// searched, it is named by its file name like an attachment, nothing of a note's tasks or
// reminders comes from it; a rename or move rewrites the paths and links in the canvas file itself.
#include "canvas/canvas.h"
#include "canvas/indextext.h"
#include "index/index.h"
#include "index/vaultindex_p.h"

#include <QSignalSpy>
#include <QTest>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

const QString BoardPath = u"Plany/Kuchnia.canvas"_s;

QString board()
{
    return u"{\n\t\"nodes\":[\n"
           u"\t\t{\"id\":\"g1\",\"type\":\"group\",\"x\":-420,\"y\":-320,\"width\":1380,\"height\":660,\"label\":\"Remont kuchni\"},\n"
           u"\t\t{\"id\":\"t1\",\"type\":\"text\",\"text\":\"# Plan\\n\\n- [ ] zamówić płytki 📅 2026-11-21\\n- przypomnienie ⏰ 2026-11-21 09:00\\n\\nZobacz [[Budżet domowy]] i #remont\",\"x\":-400,\"y\":-280,\"width\":320,\"height\":240},\n"
           u"\t\t{\"id\":\"f1\",\"type\":\"file\",\"file\":\"Projekty/Kuchnia.md\",\"x\":-40,\"y\":-280,\"width\":400,\"height\":400},\n"
           u"\t\t{\"id\":\"f2\",\"type\":\"file\",\"file\":\"Projekty/Kuchnia.md\",\"subpath\":\"#Szafki\",\"x\":-40,\"y\":160,\"width\":400,\"height\":140},\n"
           u"\t\t{\"id\":\"f3\",\"type\":\"file\",\"file\":\"assets/płytki.jpg\",\"x\":-400,\"y\":0,\"width\":320,\"height\":240},\n"
           u"\t\t{\"id\":\"l1\",\"type\":\"link\",\"url\":\"https://example.com/sklep\",\"x\":520,\"y\":-280,\"width\":400,\"height\":200}\n"
           u"\t],\n\t\"edges\":[\n"
           u"\t\t{\"id\":\"e1\",\"fromNode\":\"t1\",\"fromSide\":\"right\",\"toNode\":\"f1\",\"toSide\":\"left\",\"label\":\"szczegóły zamówienia\"}\n"
           u"\t]\n}"_s;
}

void fill(VaultIndex &index)
{
    index.update(u"Projekty/Kuchnia.md"_s, u"# Kuchnia\n\nPlan kuchni.\n\n## Szafki\n\nBiałe szafki."_s);
    index.update(u"Budżet domowy.md"_s, u"# Budżet domowy\n\nKoszty remontu."_s);
    index.update(u"Notatka.md"_s, u"Zobacz [[Plany/Kuchnia.canvas]] i [[Kuchnia]]. Plan Kuchnia na tablicy."_s);
    index.update(u"assets/płytki.jpg"_s, QString());
    index.update(BoardPath, board());
}

/// The index keeps the canvas's file text (IndexAccess::describeState() lists it).
bool hasSource(const VaultIndex &index, const QString &path)
{
    const QStringList state = index_detail::IndexAccess::describeState(index);
    return std::ranges::any_of(state, [&path](const QString &l) { return l.startsWith(u"canvas "_s + path + u' '); });
}

QStringList sources(const QList<Backlink> &links)
{
    QStringList out;
    for (const Backlink &b : links)
        out.append(b.path);
    return out;
}

} // namespace

class TestIndexCanvas : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void indexedAsNote()
    {
        VaultIndex index;
        fill(index);
        QVERIFY(VaultIndex::readsText(BoardPath));
        QVERIFY(!VaultIndex::readsText(u"assets/płytki.jpg"_s));
        const ParsedNote *note = index.note(BoardPath);
        QVERIFY(note);
        QCOMPARE(note->title, u"Kuchnia"_s);
        QVERIFY(index.paths().contains(BoardPath));
        QVERIFY(!index.attachments().contains(BoardPath));
        // The content is the cards' text, never the JSON.
        QVERIFY(index.content(BoardPath).startsWith(u"Remont kuchni\n\n# Plan"_s));
        QVERIFY(!index.content(BoardPath).contains(u"\"nodes\""_s));
    }

    void linksAndBacklinks()
    {
        VaultIndex index;
        fill(index);
        QStringList targets;
        for (const ResolvedLink &l : index.outgoingLinks(BoardPath))
            targets.append(l.target);
        QCOMPARE(targets, (QStringList{u"Budżet domowy.md"_s, u"Projekty/Kuchnia.md"_s, u"Projekty/Kuchnia.md"_s,
                                       u"assets/płytki.jpg"_s}));
        QCOMPARE(sources(index.backlinks(u"Projekty/Kuchnia.md"_s)).count(BoardPath), 2);
        QVERIFY(sources(index.backlinks(u"Budżet domowy.md"_s)).contains(BoardPath));
        QVERIFY(index.linksTo(u"assets/płytki.jpg"_s).contains(BoardPath));
        // The canvas by its file name; its title alone names the page.
        QCOMPARE(index.resolveLink(u"Plany/Kuchnia.canvas"_s).value_or(QString()), BoardPath);
        QCOMPARE(index.resolveLink(u"Kuchnia.canvas"_s).value_or(QString()), BoardPath);
        QCOMPARE(index.resolveLink(u"Kuchnia"_s).value_or(QString()), u"Projekty/Kuchnia.md"_s);
        QVERIFY(sources(index.backlinks(BoardPath)).contains(u"Notatka.md"_s));
        // In the graph: the canvas with its links.
        const GraphData g = index.graph();
        QVERIFY(std::ranges::any_of(g.nodes, [](const GraphNode &n) { return n.id == BoardPath; }));
        QVERIFY(std::ranges::any_of(g.edges, [](const GraphEdge &e) {
            return e.source == BoardPath && e.target == u"Projekty/Kuchnia.md";
        }));
    }

    void noTasksOrReminders()
    {
        VaultIndex index;
        fill(index);
        QVERIFY(index.tasksOf(BoardPath).isEmpty());
        QVERIFY(index.remindersOf(BoardPath).isEmpty());
        QVERIFY(std::ranges::none_of(index.tasks(), [](const TaskRef &t) { return t.path == BoardPath; }));
        QVERIFY(std::ranges::none_of(index.reminders(), [](const ReminderRef &r) { return r.path == BoardPath; }));
        // Its tags are the canvas's (Tags lists it).
        QVERIFY(index.note(BoardPath)->tags.contains(u"remont"_s));
    }

    void searched()
    {
        VaultIndex index;
        fill(index);
        const QList<SearchHit> hits = index.search(u"płytki"_s);
        const auto hit = std::ranges::find_if(hits, [](const SearchHit &h) { return h.path == BoardPath; });
        QVERIFY(hit != hits.cend());
        QVERIFY(hit->excerpt.contains(u"zamówić"_s));
        // The excerpt's line leads to its card.
        QCOMPARE(canvas::nodeAtLine(canvas::indexText(board()), hit->line), u"t1"_s);
        // An edge's label and a group's label are found too; JSON keys never.
        QVERIFY(std::ranges::any_of(index.search(u"szczegóły zamówienia"_s), [](const SearchHit &h) { return h.path == BoardPath; }));
        QVERIFY(std::ranges::none_of(index.search(u"fromSide"_s), [](const SearchHit &h) { return h.path == BoardPath; }));
    }

    void noUnlinkedMentions()
    {
        VaultIndex index;
        fill(index);
        index.update(u"Plany/Druga.canvas"_s, u"{\"nodes\":[{\"id\":\"a\",\"type\":\"text\",\"text\":\"Budżet domowy bez linku\",\"x\":0,\"y\":0,\"width\":250,\"height\":60}]}"_s);
        const QList<UnlinkedRef> refs = index.unlinkedReferences(u"Budżet domowy.md"_s);
        QVERIFY(std::ranges::none_of(refs, [](const UnlinkedRef &r) { return r.path.endsWith(u".canvas"); }));
    }

    void updatesAndRenames()
    {
        VaultIndex index;
        fill(index);
        const int v = index.version();
        index.update(BoardPath, board()); // the same file: nothing
        QCOMPARE(index.version(), v);
        QString edited = board();
        edited.replace(u"[[Budżet domowy]]"_s, u"[[Notatka]]"_s);
        index.update(BoardPath, edited);
        QVERIFY(index.version() > v);
        QVERIFY(!sources(index.backlinks(u"Budżet domowy.md"_s)).contains(BoardPath));
        QVERIFY(sources(index.backlinks(u"Notatka.md"_s)).contains(BoardPath));
        // A move of the canvas (the vault already moved it).
        index.rename(BoardPath, u"Archiwum/Kuchnia.canvas"_s, edited);
        QVERIFY(!index.note(BoardPath));
        QVERIFY(index.note(u"Archiwum/Kuchnia.canvas"_s));
        QVERIFY(sources(index.backlinks(u"Notatka.md"_s)).contains(u"Archiwum/Kuchnia.canvas"_s));
        QVERIFY(!hasSource(index, BoardPath));
        index.remove(u"Archiwum/Kuchnia.canvas"_s);
        QVERIFY(!index.contains(u"Archiwum/Kuchnia.canvas"_s));
        QVERIFY(!hasSource(index, u"Archiwum/Kuchnia.canvas"_s));
    }

    void brokenCanvas()
    {
        VaultIndex index;
        index.update(u"Zepsuta.canvas"_s, u"{\"nodes\":["_s);
        QVERIFY(index.note(u"Zepsuta.canvas"_s));
        QVERIFY(index.content(u"Zepsuta.canvas"_s).isEmpty());
        QVERIFY(index.outgoingLinks(u"Zepsuta.canvas"_s).isEmpty());
    }

    void preparedLikeUpdated()
    {
        // The loader's way (prepare on a worker, updatePrepared here) gives the same index.
        VaultIndex a;
        fill(a);
        VaultIndex b;
        b.update(u"Projekty/Kuchnia.md"_s, u"# Kuchnia\n\nPlan kuchni.\n\n## Szafki\n\nBiałe szafki."_s);
        b.update(u"Budżet domowy.md"_s, u"# Budżet domowy\n\nKoszty remontu."_s);
        b.update(u"Notatka.md"_s, u"Zobacz [[Plany/Kuchnia.canvas]] i [[Kuchnia]]. Plan Kuchnia na tablicy."_s);
        b.update(u"assets/płytki.jpg"_s, QString());
        PreparedNote prepared = VaultIndex::prepare(BoardPath, board(), b.config());
        QCOMPARE(prepared.text(), a.content(BoardPath));
        b.updatePrepared(std::move(prepared));
        QCOMPARE(index_detail::IndexAccess::describeState(b), index_detail::IndexAccess::describeState(a));
        const int v = b.version();
        b.updatePrepared(VaultIndex::prepare(BoardPath, board(), b.config()));
        QCOMPARE(b.version(), v); // the same file again: nothing
        // A changed config parses the canvas again from its file.
        VaultConfig config = b.config();
        config.journalsFolder = u"dziennik"_s;
        b.setConfig(config);
        QCOMPARE(b.content(BoardPath), a.content(BoardPath));
    }

    void movePageRewritesCanvas()
    {
        VaultIndex index;
        fill(index);
        const RelocationPlan plan = index.planRelocation({{u"Projekty/Kuchnia.md"_s, u"Archiwum/Kuchnia.md"_s}});
        const auto f = std::ranges::find_if(plan.files(), [](const RelocationPlan::File &x) { return x.path == BoardPath; });
        QVERIFY(f != plan.files().cend());
        QCOMPARE(f->before, board()); // the file's JSON, for the compare-and-swap
        QCOMPARE(f->links, 2);
        QVERIFY(f->after.contains(u"{\"id\":\"f1\",\"type\":\"file\",\"file\":\"Archiwum/Kuchnia.md\",\"x\":-40"_s));
        QVERIFY(f->after.contains(u"\"file\":\"Archiwum/Kuchnia.md\",\"subpath\":\"#Szafki\""_s));
        // Every other line as it was.
        QCOMPARE(f->after.count(u'\n'), board().count(u'\n'));
        QVERIFY(f->after.contains(u"\"file\":\"assets/płytki.jpg\""_s));
        // The file as read after the move: the same rewrite.
        QCOMPARE(plan.rewrite(BoardPath, board()), f->after);
        QVERIFY(canvas::readCanvas(f->after).canvas);
    }

    void renamePageRewritesTextLinks()
    {
        VaultIndex index;
        fill(index);
        const RelocationPlan plan = index.planRelocation({{u"Budżet domowy.md"_s, u"Finanse/Wydatki.md"_s}});
        const auto f = std::ranges::find_if(plan.files(), [](const RelocationPlan::File &x) { return x.path == BoardPath; });
        QVERIFY(f != plan.files().cend());
        QCOMPARE(f->links, 1);
        QVERIFY(f->after.contains(u"Zobacz [[Wydatki]] i #remont"_s));
        // A canvas edited since the plan: the rewrite applied to what it holds now.
        QString now = board();
        now.replace(u"\"x\":520"_s, u"\"x\":600"_s);
        const QString rewritten = plan.rewrite(BoardPath, now);
        QVERIFY(rewritten.contains(u"[[Wydatki]]"_s));
        QVERIFY(rewritten.contains(u"\"x\":600"_s));
    }

    void folderMoveRewritesPaths()
    {
        VaultIndex index;
        fill(index);
        const RelocationPlan plan =
                index.planRelocation({{u"assets/płytki.jpg"_s, u"zdjęcia/płytki.jpg"_s},
                                      {u"Projekty/Kuchnia.md"_s, u"Stare/Projekty/Kuchnia.md"_s}});
        const auto f = std::ranges::find_if(plan.files(), [](const RelocationPlan::File &x) { return x.path == BoardPath; });
        QVERIFY(f != plan.files().cend());
        QCOMPARE(f->links, 3);
        QVERIFY(f->after.contains(u"\"file\":\"zdjęcia/płytki.jpg\""_s));
        QCOMPARE(f->after.count(u"\"file\":\"Stare/Projekty/Kuchnia.md\""_s), 2);
    }

    void movedCanvasItself()
    {
        VaultIndex index;
        fill(index);
        // The note linking to the canvas follows it; the canvas's own exact paths stay.
        const RelocationPlan plan = index.planRelocation({{BoardPath, u"Archiwum/Kuchnia.canvas"_s}});
        const auto note = std::ranges::find_if(plan.files(), [](const RelocationPlan::File &x) { return x.path == u"Notatka.md"; });
        QVERIFY(note != plan.files().cend());
        QVERIFY(note->after.contains(u"[[Archiwum/Kuchnia.canvas]]"_s));
        const auto self = std::ranges::find_if(plan.files(), [](const RelocationPlan::File &x) { return x.path == BoardPath; });
        QVERIFY(self == plan.files().cend());
    }
};

QTEST_GUILESS_MAIN(TestIndexCanvas)
#include "tst_index_canvas.moc"
