// VaultIndex: notes, backlinks, unlinked references, block ids, tags, tasks, reminders,
// journals, graph, change notification and configuration.
#include "fixture.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

QStringList pathsOf(const QList<Backlink> &links)
{
    QStringList out;
    for (const Backlink &b : links)
        out.append(b.path);
    return out;
}

const GraphNode *findNode(const GraphData &g, const QString &id)
{
    for (const GraphNode &n : g.nodes) {
        if (n.id == id)
            return &n;
    }
    return nullptr;
}

bool hasEdge(const GraphData &g, const QString &source, const QString &target)
{
    return std::any_of(g.edges.cbegin(), g.edges.cend(),
                       [&](const GraphEdge &e) { return e.source == source && e.target == target; });
}

} // namespace

class TestIndexVault : public QObject {
    Q_OBJECT

private:
    VaultIndex m_index;

private Q_SLOTS:
    void initTestCase() { fixture::build(m_index); }

    // ---- notes ------------------------------------------------------------------------------

    void notesAndPaths()
    {
        const QList<VaultPath> expected = {
                u"Archiwum/Stare/Dom.md"_s,  u"Ludzie/Jan Kowalski.md"_s, u"Notatki.md"_s,
                u"Projekty/Budżet.md"_s,     u"Projekty/Dom.md"_s,        u"Zakupy.md"_s,
                u"journals/2026-10-12.md"_s, u"journals/2026-10-13.md"_s, u"journals/2026-10-14.md"_s,
        };
        QCOMPARE(m_index.paths(), expected);
        QCOMPARE(m_index.attachments(), QList<VaultPath>({u"assets/salon.png"_s, u"plan.pdf"_s}));
        const ParsedNote *dom = m_index.note(u"Projekty/Dom.md"_s);
        QVERIFY(dom);
        QCOMPARE(dom->title, u"Dom"_s);
        QCOMPARE(dom->aliases, QStringList({u"Remont"_s, u"Dom Jana"_s}));
        QVERIFY(m_index.content(u"Zakupy.md"_s).startsWith(u"tags:: zakupy"_s));
        QVERIFY(m_index.content(u"nope.md"_s).isNull());
        QCOMPARE(m_index.note(u"nope.md"_s), nullptr);
        QVERIFY(m_index.contains(u"plan.pdf"_s));
        QVERIFY(!m_index.contains(u"Plan.pdf"_s)); // exact paths, case-sensitive
    }

    void ignoredPathsAreSkipped_data()
    {
        QTest::addColumn<QString>("path");
        QTest::newRow("trash") << u".trash/Old.md"_s;
        QTest::newRow("brulion config") << u".brulion/config.json"_s;
        QTest::newRow("obsidian") << u".obsidian/workspace.md"_s;
        QTest::newRow("logseq") << u"logseq/custom.md"_s;
        QTest::newRow("syncthing conflict") << u"Dom.sync-conflict-20261014-101010-ABCDEFG.md"_s;
        QTest::newRow("own conflict") << u"Dom.conflict-laptop-20261014.md"_s;
        QTest::newRow("syncthing temp") << u"~syncthing~Dom.md.tmp"_s;
        QTest::newRow("empty") << QString();
    }
    void ignoredPathsAreSkipped()
    {
        QFETCH(QString, path);
        VaultIndex index;
        index.update(path, u"[[X]] #tag"_s);
        QVERIFY(index.paths().isEmpty());
        QVERIFY(index.attachments().isEmpty());
        QCOMPARE(index.version(), 0);
    }

    // ---- links ------------------------------------------------------------------------------

    void backlinks()
    {
        const QList<Backlink> links = m_index.backlinks(u"Projekty/Dom.md"_s);
        // Journals newest first, then other notes by path; links in document order.
        QCOMPARE(pathsOf(links),
                 QStringList({u"journals/2026-10-13.md"_s, u"journals/2026-10-12.md"_s, u"Ludzie/Jan Kowalski.md"_s,
                              u"Notatki.md"_s, u"Notatki.md"_s, u"Projekty/Budżet.md"_s, u"Projekty/Budżet.md"_s}));
        QCOMPARE(links[0].link.kind, LinkKind::BlockRef);
        QCOMPARE(links[0].link.blockId, u"plan01"_s);
        QCOMPARE(fixture::blockText(m_index, links[0].path, links[0].blockIndex), u"Pomysł na ((plan01))"_s);
        QCOMPARE(links[1].link.display, u"domu"_s);
        QCOMPARE(links[1].blockIndex, 0);
        QCOMPARE(links[3].link.target, u"dom"_s);
        QCOMPARE(links[4].link.target, u"Remont"_s);
        QVERIFY(links[3].link.from < links[4].link.from);
        // The [[Dom]] inside the calc block is code, not a link.
        QCOMPARE(links[5].link.line, 7);
        QCOMPARE(links[6].link.blockId, u"plan01"_s);
    }

    void backlinksOfOtherPages()
    {
        QCOMPARE(pathsOf(m_index.backlinks(u"Ludzie/Jan Kowalski.md"_s)),
                 QStringList({u"journals/2026-10-12.md"_s, u"Notatki.md"_s}));
        // ((koszty)) in Zakupy.md refers to a block of Budżet.
        QCOMPARE(pathsOf(m_index.backlinks(u"Projekty/Budżet.md"_s)),
                 QStringList({u"Projekty/Dom.md"_s, u"Zakupy.md"_s}));
        QCOMPARE(pathsOf(m_index.backlinks(u"Zakupy.md"_s)), QStringList{u"Projekty/Budżet.md"_s});
        QCOMPARE(pathsOf(m_index.backlinks(u"assets/salon.png"_s)),
                 QStringList({u"Notatki.md"_s, u"Projekty/Dom.md"_s}));
        QVERIFY(m_index.backlinks(u"Archiwum/Stare/Dom.md"_s).isEmpty());
        QVERIFY(m_index.backlinks(u"missing.md"_s).isEmpty());
    }

    void linksTo()
    {
        QCOMPARE(m_index.linksTo(u"Projekty/Dom.md"_s),
                 QList<VaultPath>({u"Ludzie/Jan Kowalski.md"_s, u"Notatki.md"_s, u"Projekty/Budżet.md"_s,
                                   u"journals/2026-10-12.md"_s}));
        QCOMPARE(m_index.linksTo(u"plan.pdf"_s), QList<VaultPath>{u"Projekty/Dom.md"_s});
        QVERIFY(m_index.linksTo(u"Zakupy.md"_s).size() == 1);
    }

    void outgoingLinks()
    {
        const QList<ResolvedLink> out = m_index.outgoingLinks(u"Projekty/Dom.md"_s);
        QStringList targets;
        for (const ResolvedLink &l : out)
            targets.append(l.target.isEmpty() ? u"-"_s : l.target);
        QCOMPARE(targets, QStringList({u"Projekty/Budżet.md"_s, u"-"_s, u"assets/salon.png"_s, u"plan.pdf"_s, u"-"_s}));
        const QList<ResolvedLink> refs = m_index.outgoingLinks(u"Zakupy.md"_s);
        QCOMPARE(refs.size(), 1);
        QCOMPARE(refs.first().link.kind, LinkKind::BlockRef);
        QCOMPARE(refs.first().target, u"Projekty/Budżet.md"_s);
    }

    void missingPages()
    {
        const QList<MissingPage> missing = m_index.missingPages();
        QCOMPARE(missing.size(), 1); // brak.png is a missing attachment, not a page
        QCOMPARE(missing.first().name, u"Nieistniejąca strona"_s);
        QCOMPARE(missing.first().sources, QList<VaultPath>({u"Notatki.md"_s, u"Projekty/Dom.md"_s}));
    }

    void unlinkedReferences()
    {
        const QList<UnlinkedRef> refs = m_index.unlinkedReferences(u"Projekty/Dom.md"_s);
        QCOMPARE(refs.size(), 3);
        QCOMPARE(refs[0].path, u"journals/2026-10-14.md"_s);
        QCOMPARE(refs[0].line, 2);
        QCOMPARE(refs[0].excerpt, u"Dzisiaj rozmawiałem z Jan Kowalski o dom i Dom Jana."_s);
        QCOMPARE(refs[1].path, u"journals/2026-10-12.md"_s); // alias "Remont", other case
        QCOMPARE(refs[1].line, 3);
        QCOMPARE(refs[2].path, u"Archiwum/Stare/Dom.md"_s); // "# Stary dom"
        QCOMPARE(refs[2].line, 0);
        QCOMPARE(refs[2].excerpt, u"Stary dom"_s);

        const QList<UnlinkedRef> jan = m_index.unlinkedReferences(u"Ludzie/Jan Kowalski.md"_s);
        QCOMPARE(jan.size(), 1);
        QCOMPARE(jan.first().path, u"journals/2026-10-14.md"_s);
        QVERIFY(m_index.unlinkedReferences(u"nope.md"_s).isEmpty());
    }

    void unlinkedReferenceRules_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<bool>("mention");
        QTest::newRow("plain") << u"Byłem w Łódź wczoraj"_s << true;
        QTest::newRow("upper case") << u"ŁÓDŹ jest duża"_s << true;
        QTest::newRow("punctuation") << u"(Łódź), a potem"_s << true;
        QTest::newRow("at line end") << u"Jadę do: Łódź"_s << true;
        QTest::newRow("longer word") << u"Łódźka i łódźki"_s << false;
        QTest::newRow("word before") << u"XŁódź"_s << false;
        QTest::newRow("digits glued") << u"Łódź2"_s << false;
        QTest::newRow("underscore glued") << u"Łódź_miasto"_s << false;
        QTest::newRow("folded spelling is not the title") << u"Lodz"_s << false;
        QTest::newRow("wiki link") << u"[[Łódź]]"_s << false;
        QTest::newRow("wiki link alias") << u"[[Miasta|Łódź]]"_s << false;
        QTest::newRow("markdown link") << u"[Łódź](https://lodz.pl)"_s << false;
        QTest::newRow("url") << u"https://example.com/Łódź"_s << false;
        QTest::newRow("inline code") << u"`Łódź`"_s << false;
        QTest::newRow("tag") << u"#Łódź"_s << false;
        QTest::newRow("tag with brackets") << u"#[[Łódź]]"_s << false;
        QTest::newRow("nested tag") << u"#miasta/Łódź"_s << false;
        QTest::newRow("comment") << u"%%Łódź%%"_s << false;
        QTest::newRow("hashes in heading") << u"## Łódź"_s << true;
        QTest::newRow("task") << u"- [ ] Pojechać do Łódź 📅 2026-10-20"_s << true;
        QTest::newRow("alias multi-word") << u"Miasto  włókniarzy\tna mapie"_s << true;
        QTest::newRow("alias across words") << u"Miastowłókniarzy"_s << false;
        QTest::newRow("other letter") << u"Byłem w Ĺodź"_s << false;
        QTest::newRow("nfd text") << u"Byłem w Ło\u0301dz\u0301."_s << true;
    }
    void unlinkedReferenceRules()
    {
        QFETCH(QString, line);
        QFETCH(bool, mention);
        VaultIndex index;
        index.update(u"Miasta/Łódź.md"_s, u"---\naliases: [Miasto włókniarzy, X]\n---\nOpis"_s);
        index.update(u"Notatka.md"_s, line);
        const QList<UnlinkedRef> refs = index.unlinkedReferences(u"Miasta/Łódź.md"_s);
        QCOMPARE(refs.size(), mention ? 1 : 0);
        if (mention) {
            QCOMPARE(refs.first().path, u"Notatka.md"_s);
            QCOMPARE(refs.first().line, 0);
        }
    }

    void unlinkedReferencesUnicodeSpacesInNames()
    {
        // Names split on any Unicode whitespace: an alias typed with a no-break space (common
        // after Polish one-letter words) matches plain spaces in text, and vice versa.
        VaultIndex index;
        index.update(u"Łódź.md"_s, u"---\naliases: [\"Miasto\u00A0włókniarzy\", \"W mieście\"]\n---\nx"_s);
        index.update(u"A.md"_s, u"Miasto włókniarzy jest duże"_s);
        index.update(u"B.md"_s, u"Mieszkam w\u00A0mieście od lat"_s);
        const QList<UnlinkedRef> refs = index.unlinkedReferences(u"Łódź.md"_s);
        QCOMPARE(refs.size(), 2);
        QCOMPARE(refs[0].path, u"A.md"_s);
        QCOMPARE(refs[1].path, u"B.md"_s);
    }

    void unlinkedReferencesNamesStartingWithEmoji()
    {
        // A rejected match ("x🚀 Start" is no whole word) must not stop the search inside a
        // surrogate pair: the second mention on the line is found.
        VaultIndex index;
        index.update(u"🚀 Start.md"_s, u"x"_s);
        index.update(u"A.md"_s, u"x🚀 Start, potem 🚀 Start"_s);
        const QList<UnlinkedRef> refs = index.unlinkedReferences(u"🚀 Start.md"_s);
        QCOMPARE(refs.size(), 1);
        QCOMPARE(refs.first().line, 0);
    }

    void unlinkedReferencesStrayBacktick()
    {
        // Code spans never cross blocks: a lone backtick must not pair with one blocks later and
        // hide everything in between (as masking the whole note at once did).
        VaultIndex index;
        index.update(u"Dom.md"_s, u"x"_s);
        index.update(u"N.md"_s, u"- Użyj ` jako separatora\n- Dom jest duży\n\nAkapit o Dom.\n- a tu `kod` i `Dom`"_s);
        const QList<UnlinkedRef> refs = index.unlinkedReferences(u"Dom.md"_s);
        QCOMPARE(refs.size(), 2);
        QCOMPARE(refs[0].line, 1);
        QCOMPARE(refs[1].line, 3);
    }

    void unlinkedReferencesMultiBlockComment()
    {
        // `%%…%%` comments may span blocks (Obsidian): still no mentions inside.
        VaultIndex index;
        index.update(u"Dom.md"_s, u"x"_s);
        index.update(u"N.md"_s, u"%%\nDom ukryty\n\n- Dom też\n%%\nDom widoczny <!-- Dom -->"_s);
        const QList<UnlinkedRef> refs = index.unlinkedReferences(u"Dom.md"_s);
        QCOMPARE(refs.size(), 1);
        QCOMPARE(refs.first().line, 5);
        QCOMPARE(refs.first().excerpt, u"Dom widoczny <!-- Dom -->"_s);
    }

    void unlinkedReferencesSkipCodeAndFrontmatter()
    {
        VaultIndex index;
        index.update(u"Kawa.md"_s, u"Kawa jest dobra"_s);
        index.update(u"N.md"_s,
                     u"---\ntitle: Kawa\n---\n```\nKawa\n```\n    \n- kawa z mlekiem ^k1\n\n<div>Kawa</div>"_s);
        const QList<UnlinkedRef> refs = index.unlinkedReferences(u"Kawa.md"_s);
        QCOMPARE(refs.size(), 1);
        QCOMPARE(refs.first().line, 7);
        QCOMPARE(refs.first().excerpt, u"kawa z mlekiem"_s); // list marker and block id dropped
    }

    void unlinkedReferencesAreCapped()
    {
        VaultIndex index;
        index.update(u"Kot.md"_s, u"x"_s);
        QStringList many;
        for (int i = 0; i < 150; ++i)
            many.append(u"- kot numer %1"_s.arg(i));
        index.update(u"A.md"_s, many.join(u'\n'));
        index.update(u"B.md"_s, many.join(u'\n'));
        const QList<UnlinkedRef> refs = index.unlinkedReferences(u"Kot.md"_s);
        QCOMPARE(refs.size(), VaultIndex::MaxUnlinkedReferences);
        QCOMPARE(refs.last().path, u"B.md"_s);
        QCOMPARE(refs.last().line, 49);
    }

    void unlinkedExcerptsAreShortened()
    {
        VaultIndex index;
        index.update(u"Kot.md"_s, u"x"_s);
        const QString filler = QString(u"bardzo długi tekst "_s).repeated(12);
        index.update(u"A.md"_s, u"- "_s + filler + u"o kot i "_s + filler);
        const QList<UnlinkedRef> refs = index.unlinkedReferences(u"Kot.md"_s);
        QCOMPARE(refs.size(), 1);
        const QString &excerpt = refs.first().excerpt;
        QVERIFY2(excerpt.size() <= 122, qPrintable(excerpt));
        QVERIFY(excerpt.startsWith(u'…'));
        QVERIFY(excerpt.endsWith(u'…'));
        QVERIFY(excerpt.contains(u"o kot i"_s));
    }

    void unlinkedReferencesLongLineIsLinear()
    {
        // Review regression (perf plan N6): each rejected partial mention ("Strona 12") restarted
        // the regex at the next position with the whole line as the subject, and every match
        // call checked the UTF-16 of the whole line — 20 000 of them took most of a second.
        VaultIndex index;
        index.update(u"Strona 1.md"_s, u"# Strona 1\n"_s);
        QString line;
        for (int i = 0; i < 20000; ++i)
            line += u"Strona 1%1 "_s.arg(i % 10);
        index.update(u"Długa notatka.md"_s, line + u"i na końcu Strona 1.\n"_s);
        QElapsedTimer t;
        t.start();
        const QList<UnlinkedRef> refs = index.unlinkedReferences(u"Strona 1.md"_s);
        QVERIFY2(t.elapsed() < 2000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(t.elapsed())));
        QCOMPARE(refs.size(), 1);
        QVERIFY(refs.first().excerpt.endsWith(u"i na końcu Strona 1."_s));
    }

    void unlinkedReferencesInvalidUtf16()
    {
        // PCRE2 refuses a subject with a lone surrogate, so such a line never had mentions (the
        // line is checked once now instead of on every match call); other lines are searched.
        VaultIndex index;
        index.update(u"Kot.md"_s, u"x"_s);
        index.update(u"A.md"_s, u"Mój kot "_s + QChar(0xD800) + u"\nDrugi kot tutaj\n"_s);
        const QList<UnlinkedRef> refs = index.unlinkedReferences(u"Kot.md"_s);
        QCOMPARE(refs.size(), 1);
        QCOMPARE(refs.first().line, 1);
    }

    // ---- blocks, tags, tasks, reminders, journals -------------------------------------------

    void blockById_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("text");
        QTest::newRow("plain") << u"plan01"_s << u"Projekty/Dom.md"_s << u"Plan remontu domu w Krakowie."_s;
        QTest::newRow("caret") << u"^plan01"_s << u"Projekty/Dom.md"_s << u"Plan remontu domu w Krakowie."_s;
        QTest::newRow("ref syntax") << u"((koszty))"_s << u"Projekty/Budżet.md"_s << u"Farba: [lista](../Zakupy.md)"_s;
        QTest::newRow("spaces") << u"  koszty "_s << u"Projekty/Budżet.md"_s << u"Farba: [lista](../Zakupy.md)"_s;
        QTest::newRow("unknown") << u"nope"_s << QString() << QString();
        QTest::newRow("case-sensitive") << u"PLAN01"_s << QString() << QString();
        QTest::newRow("empty") << QString() << QString() << QString();
    }
    void blockById()
    {
        QFETCH(QString, id);
        QFETCH(QString, path);
        QFETCH(QString, text);
        const std::optional<BlockRef> ref = m_index.blockById(id);
        QCOMPARE(ref.has_value(), !path.isEmpty());
        if (ref) {
            QCOMPARE(ref->path, path);
            QCOMPARE(fixture::blockText(m_index, ref->path, ref->blockIndex), text);
        }
    }

    void duplicateBlockIdsSmallestPathWins()
    {
        VaultIndex index;
        index.update(u"b.md"_s, u"- drugi ^dup"_s);
        index.update(u"a.md"_s, u"- pierwszy ^dup"_s);
        index.update(u"c.md"_s, u"((dup))"_s);
        QCOMPARE(index.blockById(u"dup"_s)->path, u"a.md"_s);
        QCOMPARE(pathsOf(index.backlinks(u"a.md"_s)), QStringList{u"c.md"_s});
        QVERIFY(index.backlinks(u"b.md"_s).isEmpty());
        index.remove(u"a.md"_s);
        QCOMPARE(index.blockById(u"dup"_s)->path, u"b.md"_s);
        QCOMPARE(pathsOf(index.backlinks(u"b.md"_s)), QStringList{u"c.md"_s});
    }

    void logseqIdProperty()
    {
        VaultIndex index;
        index.update(u"L.md"_s, u"- Blok z Logseq\n  id:: 650a1b2c-0000-4000-8000-000000000001"_s);
        const std::optional<BlockRef> ref = index.blockById(u"650a1b2c-0000-4000-8000-000000000001"_s);
        QVERIFY(ref);
        QCOMPARE(fixture::blockText(index, ref->path, ref->blockIndex), u"Blok z Logseq"_s);
    }

    void tags()
    {
        const QList<TagCount> tags = m_index.tags();
        QStringList names;
        QList<int> counts;
        for (const TagCount &t : tags) {
            names.append(t.tag);
            counts.append(t.count);
        }
        QCOMPARE(names, QStringList({u"zakupy"_s, u"dom"_s, u"praca"_s, u"praca/raporty"_s, u"projekt"_s,
                                     u"remont/łazienka"_s}));
        QCOMPARE(counts, QList<int>({3, 2, 2, 1, 1, 1}));
        QCOMPARE(tags[0].paths, QList<VaultPath>({u"Projekty/Dom.md"_s, u"Zakupy.md"_s, u"journals/2026-10-13.md"_s}));
        QCOMPARE(tags[4].display, u"Projekt"_s);         // frontmatter spelling
        QCOMPARE(tags[5].display, u"Remont/Łazienka"_s); // inline spelling
        QCOMPARE(tags[1].display, u"dom"_s);             // first spelling wins (#dom before #Dom)
    }

    void tasks()
    {
        const QList<TaskRef> tasks = m_index.tasks();
        QStringList texts;
        for (const TaskRef &t : tasks)
            texts.append(t.path + u": "_s + t.task.text);
        QCOMPARE(tasks.size(), 13);
        QCOMPARE(tasks.first().path, u"Notatki.md"_s);
        QVERIFY(tasks.first().task.text.startsWith(u"Bez daty"_s));
        QCOMPARE(tasks.last().path, u"journals/2026-10-14.md"_s);
        QCOMPARE(tasks.last().task.status, TaskStatus::Doing);
        for (const TaskRef &t : tasks) {
            const ParsedNote *note = m_index.note(t.path);
            QVERIFY(note);
            QVERIFY(note->blocks[t.blockIndex].task.has_value());
            QCOMPARE(note->blocks[t.blockIndex].task->line, t.task.line);
        }
        const auto raport = std::find_if(tasks.cbegin(), tasks.cend(),
                                         [](const TaskRef &t) { return t.task.text.startsWith(u"Pisanie"_s); });
        QVERIFY(raport != tasks.cend());
        QCOMPARE(raport->task.priority, Priority::High);
        QCOMPARE(raport->task.due->date, QDate(2026, 10, 20));
    }

    void reminders()
    {
        const QList<ReminderRef> reminders = m_index.reminders();
        QCOMPARE(reminders.size(), 3);
        QCOMPARE(reminders[0].path, u"journals/2026-10-14.md"_s);
        QVERIFY(reminders[0].task.has_value());
        QCOMPARE(reminders[0].reminder.at, (DateSpec{QDate(2026, 10, 14), QTime(17, 30)}));
        QVERIFY(reminders[1].reminder.alarm);
        QVERIFY(!reminders[1].task.has_value());
        QCOMPARE(reminders[2].reminder.at.date, QDate(2026, 10, 13));
        QCOMPARE(fixture::blockText(m_index, reminders[1].path, reminders[1].blockIndex), u"Pobudka na lotnisko"_s);
    }

    void journals()
    {
        const QList<JournalRef> expected = {
                {QDate(2026, 10, 14), u"journals/2026-10-14.md"_s},
                {QDate(2026, 10, 13), u"journals/2026-10-13.md"_s},
                {QDate(2026, 10, 12), u"journals/2026-10-12.md"_s},
        };
        QCOMPARE(m_index.journals(), expected);
    }

    void journalConfigChangesReparse()
    {
        VaultIndex index;
        index.update(u"daily/14.10.2026.md"_s, u"- [ ] zadanie ⏰ 2026-10-15"_s);
        index.update(u"journals/2026-10-13.md"_s, u"x"_s);
        QCOMPARE(index.journals().size(), 1);
        QCOMPARE(index.reminders().first().reminder.at.time, QTime(9, 0));
        VaultConfig config;
        config.journalsFolder = u"daily"_s;
        config.journalFileFormat = u"dd.MM.yyyy"_s;
        config.defaultReminderTime = QTime(7, 15);
        const int before = index.version();
        index.setConfig(config);
        QVERIFY(index.version() > before);
        QCOMPARE(index.journals(), QList<JournalRef>({{QDate(2026, 10, 14), u"daily/14.10.2026.md"_s}}));
        QCOMPARE(index.reminders().first().reminder.at.time, QTime(7, 15));
        // Fields that do not affect parsing change nothing.
        config.weekStartsOn = Qt::Sunday;
        const int v = index.version();
        index.setConfig(config);
        QCOMPARE(index.version(), v);
        QCOMPARE(index.config().weekStartsOn, Qt::Sunday);
    }

    // ---- graph ------------------------------------------------------------------------------

    void graph()
    {
        const GraphData g = m_index.graph();
        QCOMPARE(g.nodes.size(), 9);
        QCOMPARE(g.edges.size(), 10);
        QVERIFY(hasEdge(g, u"journals/2026-10-13.md"_s, u"Projekty/Dom.md"_s)); // block reference
        QVERIFY(hasEdge(g, u"Projekty/Budżet.md"_s, u"Zakupy.md"_s));           // Markdown link
        QVERIFY(hasEdge(g, u"Notatki.md"_s, u"Ludzie/Jan Kowalski.md"_s));      // alias
        QVERIFY(!hasEdge(g, u"Notatki.md"_s, u"assets/salon.png"_s));           // attachments skipped
        QCOMPARE(findNode(g, u"Projekty/Dom.md"_s)->degree, 6);
        QCOMPARE(findNode(g, u"Archiwum/Stare/Dom.md"_s)->degree, 0);
        QCOMPARE(findNode(g, u"Projekty/Dom.md"_s)->title, u"Dom"_s);
        int degreeSum = 0;
        for (const GraphNode &n : g.nodes)
            degreeSum += n.degree;
        QCOMPARE(degreeSum, 2 * int(g.edges.size()));
    }

    void graphWithTagsAndMissing()
    {
        const GraphData g = m_index.graph(true, true);
        const GraphNode *missing = findNode(g, u"Nieistniejąca strona"_s);
        QVERIFY(missing);
        QVERIFY(missing->isMissing);
        QCOMPARE(missing->degree, 2);
        QVERIFY(!findNode(g, u"brak.png"_s));
        const GraphNode *tag = findNode(g, u"#zakupy"_s);
        QVERIFY(tag);
        QVERIFY(tag->isTag);
        QCOMPARE(tag->degree, 3);
        QCOMPARE(g.nodes.size(), 9 + 1 + 6);
        QCOMPARE(g.edges.size(), 10 + 2 + 9);
    }

    void graphMissingPagesDeduplicated()
    {
        VaultIndex index;
        index.update(u"A.md"_s, u"[[Brak]] [[brak]]"_s);
        const GraphData g = index.graph(false, true);
        QCOMPARE(g.nodes.size(), 2);
        QCOMPARE(g.edges.size(), 1);
        QCOMPARE(g.nodes[1].id, u"Brak"_s);
        QCOMPARE(g.nodes[1].title, u"Brak"_s);
    }

    // ---- changes and notification -----------------------------------------------------------

    void changedIsCoalesced()
    {
        VaultIndex index;
        QSignalSpy spy(&index, &VaultIndex::changed);
        index.update(u"a.md"_s, u"a"_s);
        index.update(u"b.md"_s, u"b"_s);
        index.remove(u"a.md"_s);
        QCOMPARE(index.version(), 3);
        QCOMPARE(spy.count(), 0); // not synchronously
        QTRY_COMPARE(spy.count(), 1);
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 1);
        index.update(u"b.md"_s, u"b"_s); // unchanged content: no-op
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(index.version(), 3);
        index.remove(u"nothing.md"_s);
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 1);
    }

    void batchesEmitOnceAtTheEnd()
    {
        VaultIndex index;
        QSignalSpy spy(&index, &VaultIndex::changed);
        {
            VaultIndex::Batch outer(index);
            index.update(u"a.md"_s, u"a"_s);
            index.beginBatch();
            index.update(u"b.md"_s, u"b"_s);
            index.endBatch();
            QCoreApplication::processEvents();
            QCOMPARE(spy.count(), 0); // still inside the outer batch
        }
        QCOMPARE(spy.count(), 1); // emitted synchronously by the outermost endBatch()
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 1);
        index.beginBatch();
        index.endBatch(); // nothing changed
        QCOMPARE(spy.count(), 1);
        index.endBatch(); // unbalanced: ignored
        QCOMPARE(spy.count(), 1);
    }

    void clearForgetsEverything()
    {
        VaultIndex index;
        fixture::build(index);
        QSignalSpy spy(&index, &VaultIndex::changed);
        index.clear();
        QVERIFY(index.paths().isEmpty());
        QVERIFY(index.attachments().isEmpty());
        QVERIFY(index.tags().isEmpty());
        QVERIFY(index.tasks().isEmpty());
        QVERIFY(index.search(u"dom"_s).isEmpty());
        QVERIFY(!index.resolveLink(u"Dom"_s));
        QVERIFY(!index.blockById(u"plan01"_s));
        QTRY_COMPARE(spy.count(), 1);
        fixture::build(index);
        QCOMPARE(index.paths().size(), 9);
        QCOMPARE(index.backlinks(u"Projekty/Dom.md"_s).size(), 7);
    }

    void emptyNotesAreNotUnknown()
    {
        // content() is null only for unknown paths: an empty note created with QString() is "".
        VaultIndex index;
        index.update(u"Pusta.md"_s, QString());
        QVERIFY(index.contains(u"Pusta.md"_s));
        QVERIFY(!index.content(u"Pusta.md"_s).isNull());
        QVERIFY(index.content(u"Pusta.md"_s).isEmpty());
        QVERIFY(index.content(u"Brak.md"_s).isNull());
        const int v = index.version();
        index.update(u"Pusta.md"_s, u""_s); // same (empty) content: no-op
        QCOMPARE(index.version(), v);
    }

    void updatesReplaceEverything()
    {
        VaultIndex index;
        index.update(u"Strona.md"_s, u"- [ ] stare #tagA ^b1\n- [[Cel]]"_s);
        index.update(u"Cel.md"_s, u"x"_s);
        QCOMPARE(index.tasks().size(), 1);
        QCOMPARE(index.tags().first().tag, u"taga"_s);
        QVERIFY(index.blockById(u"b1"_s));
        QCOMPARE(index.backlinks(u"Cel.md"_s).size(), 1);
        index.update(u"Strona.md"_s, u"- nowe #tagB"_s);
        QVERIFY(index.tasks().isEmpty());
        QCOMPARE(index.tags().size(), 1);
        QCOMPARE(index.tags().first().tag, u"tagb"_s);
        QVERIFY(!index.blockById(u"b1"_s));
        QVERIFY(index.backlinks(u"Cel.md"_s).isEmpty());
        QCOMPARE(index.suggestPages(QString()).first().path, u"Strona.md"_s); // most recent
    }

    void malformedInputIsHandled_data()
    {
        QTest::addColumn<QString>("content");
        QTest::newRow("empty") << QString();
        QTest::newRow("lone surrogates") << (u"- [[Strona"_s + QChar(0xD800) + u"]] #tag"_s + QChar(0xDC00)
                                             + u" żółw"_s);
        QTest::newRow("nul characters") << (u"- tekst "_s + QChar(0) + u" [[A]] "_s + QChar(0) + u"\n- ((id"_s);
        QTest::newRow("crlf") << u"---\r\naliases: [Ala]\r\n---\r\n- [[A]] ^x1\r\n- ((x1)) żółw\r\n"_s;
        QTest::newRow("lone cr") << u"- a\r- [[B]]\r- [ ] task 📅 2026-10-15"_s;
        QTest::newRow("unclosed constructs") << u"- [[abc\n- ((\n- ![[\n```\nkod bez końca\n- #"_s;
        QTest::newRow("only frontmatter") << u"---\ntags: [a, b]\naliases: x\n---"_s;
        QTest::newRow("broken frontmatter") << u"---\n: : :\n- [[A]]"_s;
        QTest::newRow("huge line") << (u"- "_s + QString(u"słowo [[A]] #t "_s).repeated(5000));
        QString deep;
        for (int i = 0; i < 300; ++i)
            deep += QString(i, u'\t') + u"- poziom %1 [[A]]\n"_s.arg(i);
        QTest::newRow("deep nesting") << deep;
        QTest::newRow("binary-ish") << QString::fromLatin1(QByteArray(256, '\x01')) + u"\x7f\x1b[0m"_s;
    }
    void malformedInputIsHandled()
    {
        QFETCH(QString, content);
        VaultIndex index;
        index.update(u"A.md"_s, u"Strona A"_s);
        index.update(u"Zły plik.md"_s, content);
        index.update(u"Inny.md"_s, u"[[Zły plik]] Zły plik\n\n"_s + content);
        // Every view must cope (no crash, consistent sizes).
        QVERIFY(index.note(u"Zły plik.md"_s));
        QCOMPARE(index.paths().size(), 3);
        for (const VaultPath &p : index.paths()) {
            index.backlinks(p);
            index.linksTo(p);
            index.outgoingLinks(p);
            QVERIFY(index.unlinkedReferences(p).size() <= VaultIndex::MaxUnlinkedReferences);
        }
        index.tags();
        index.tasks();
        index.reminders();
        index.missingPages();
        index.search(u"słowo żółw"_s);
        index.search(content.left(200));
        index.suggestPages(content.left(50));
        index.suggestBlocks(content.left(50));
        const GraphData g = index.graph(true, true);
        int degreeSum = 0;
        for (const GraphNode &n : g.nodes)
            degreeSum += n.degree;
        QCOMPARE(degreeSum, 2 * int(g.edges.size()));
        QVERIFY(!index.backlinks(u"Zły plik.md"_s).isEmpty());
        index.rename(u"Zły plik.md"_s, u"Dobry plik.md"_s, content);
        index.remove(u"Dobry plik.md"_s);
        QCOMPARE(index.paths().size(), 2);
    }

    void noteDataIsConsistentAfterManyEdits()
    {
        // Every derived structure equals a fresh index of the final state.
        VaultIndex incremental;
        fixture::build(incremental);
        incremental.update(u"Projekty/Dom.md"_s, u"---\naliases: [Remont]\n---\n- [ ] nowe #zakupy ^plan01"_s);
        incremental.remove(u"Notatki.md"_s);
        incremental.rename(u"Zakupy.md"_s, u"Listy/Zakupy.md"_s, incremental.content(u"Zakupy.md"_s));
        incremental.update(u"Nieistniejąca strona.md"_s, u"teraz istnieje [[Dom Jana]]"_s);

        VaultIndex fresh;
        for (const VaultPath &p : incremental.paths())
            fresh.update(p, incremental.content(p));
        for (const VaultPath &p : incremental.attachments())
            fresh.update(p, QString());

        QCOMPARE(incremental.paths(), fresh.paths());
        for (const VaultPath &p : fresh.paths()) {
            QCOMPARE(pathsOf(incremental.backlinks(p)), pathsOf(fresh.backlinks(p)));
            QCOMPARE(incremental.linksTo(p), fresh.linksTo(p));
            const QList<ResolvedLink> a = incremental.outgoingLinks(p);
            const QList<ResolvedLink> b = fresh.outgoingLinks(p);
            QCOMPARE(a.size(), b.size());
            for (qsizetype i = 0; i < a.size(); ++i)
                QCOMPARE(a[i].target, b[i].target);
        }
        QCOMPARE(incremental.missingPages().size(), fresh.missingPages().size());
        QCOMPARE(incremental.tags().size(), fresh.tags().size());
        for (qsizetype i = 0; i < fresh.tags().size(); ++i) {
            QCOMPARE(incremental.tags()[i].tag, fresh.tags()[i].tag);
            QCOMPARE(incremental.tags()[i].count, fresh.tags()[i].count);
            QCOMPARE(incremental.tags()[i].paths, fresh.tags()[i].paths);
        }
        QCOMPARE(incremental.graph(true, true).edges.size(), fresh.graph(true, true).edges.size());
    }
};

QTEST_GUILESS_MAIN(TestIndexVault)
#include "tst_index_vault.moc"
