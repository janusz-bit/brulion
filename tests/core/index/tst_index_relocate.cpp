// Moving and renaming without breaking links (docs/SYNTAX.md §2 "Rewriting on rename and move"):
// VaultIndex::linkTarget() / tagTarget() — the shortest name reaching a file — and
// VaultIndex::planRelocation() — the notes to rewrite so every link and tag keeps its target.
#include "fixture.h"

#include "markdown/rename.h"
#include "util/paths.h"

#include <QRandomGenerator>
#include <QTest>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

/// An index of `files` (path -> content).
void build(VaultIndex &index, const QList<QPair<QString, QString>> &files)
{
    for (const auto &[path, content] : files)
        index.update(path, content);
}

/// The plan's new content of `path` ("" when the plan leaves it alone).
QString planned(const RelocationPlan &plan, const QString &path)
{
    for (const RelocationPlan::File &f : plan.files()) {
        if (f.path == path)
            return f.after;
    }
    return {};
}

/// Apply a plan the way Workspace does: index the moved files at their new paths, then the
/// rewritten notes. Afterwards every link should name what it named before.
void apply(VaultIndex &index, const RelocationPlan &plan)
{
    for (const FileMove &m : plan.moves()) {
        const QString content = index.content(m.from);
        if (content.isNull()) { // an attachment
            index.remove(m.from);
            index.update(m.to, QString());
        } else {
            index.rename(m.from, m.to, content);
        }
    }
    for (const RelocationPlan::File &f : plan.files())
        index.update(f.newPath, f.after);
}

/// Every resolved link of every note: "note: target -> file".
QStringList resolutions(const VaultIndex &index, const QHash<QString, QString> &moved = {})
{
    QStringList out;
    for (const VaultPath &p : index.paths()) {
        for (const ResolvedLink &l : index.outgoingLinks(p)) {
            if (l.link.kind == LinkKind::BlockRef || l.link.kind == LinkKind::BlockEmbed || l.target.isEmpty())
                continue;
            out.append(moved.value(p, p) + u": "_s + moved.value(l.target, l.target));
        }
    }
    out.sort();
    return out;
}

/// Every tag of every note that names a page: "note: page".
QStringList tagResolutions(const VaultIndex &index, const QHash<QString, QString> &moved = {})
{
    QStringList out;
    for (const VaultPath &p : index.paths()) {
        for (const QString &tag : index.note(p)->tags) {
            if (const auto page = index.resolveTag(tag))
                out.append(moved.value(p, p) + u": "_s + moved.value(*page, *page));
        }
    }
    out.sort();
    return out;
}

} // namespace

class TestIndexRelocate : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- linkTarget -------------------------------------------------------------------------

    void linkTarget_data()
    {
        QTest::addColumn<QStringList>("files");
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("from");
        QTest::addColumn<QString>("expected"); // "<none>" for nullopt
        const QString none = u"<none>"_s;
        QTest::newRow("unique Polish name")
                << QStringList{u"Zwierzęta/Żółw.md"_s} << u"Zwierzęta/Żółw.md"_s << QString() << u"Żółw"_s;
        const QStringList dom{u"Dom.md"_s, u"Projekty/Dom.md"_s};
        QTest::newRow("root page of a duplicate name") << dom << u"Dom.md"_s << QString() << u"Dom"_s;
        QTest::newRow("other page of a duplicate name")
                << dom << u"Projekty/Dom.md"_s << QString() << u"Projekty/Dom"_s;
        QTest::newRow("from inside its folder")
                << dom << u"Projekty/Dom.md"_s << u"Projekty/Notatka.md"_s << u"Projekty/Dom"_s;
        const QStringList deep{u"A/Projekty/Dom.md"_s, u"B/Projekty/Dom.md"_s};
        QTest::newRow("same length, alphabetical first") << deep << u"A/Projekty/Dom.md"_s << QString() << u"Dom"_s;
        QTest::newRow("same suffix: the whole path")
                << deep << u"B/Projekty/Dom.md"_s << QString() << u"B/Projekty/Dom"_s;
        QTest::newRow("relative to the linking note")
                << QStringList{u"Archiwum/Stare/Dom.md"_s, u"Dom.md"_s} << u"Archiwum/Stare/Dom.md"_s
                << u"Archiwum/Notatka.md"_s << u"Stare/Dom"_s;
        QTest::newRow("a title that is another page's alias")
                << QStringList{u"Remont.md"_s, u"Projekty/Dom.md|Remont"_s} << u"Remont.md"_s << QString()
                << u"Remont"_s;
        QTest::newRow("the page with the alias") << QStringList{u"Remont.md"_s, u"Projekty/Dom.md|Remont"_s}
                                                 << u"Projekty/Dom.md"_s << QString() << u"Dom"_s;
        QTest::newRow("case twin elsewhere") << QStringList{u"żółw.md"_s, u"Zwierzęta/Żółw.md"_s}
                                             << u"Zwierzęta/Żółw.md"_s << QString() << u"Zwierzęta/Żółw"_s;
        QTest::newRow("NFD path") << QStringList{u"Miasta/Żółw.md"_s} << u"Miasta/Z\u0307o\u0301łw.md"_s << QString()
                                  << u"Żółw"_s;
        QTest::newRow("attachment keeps its extension") << QStringList{u"assets/plan.png"_s, u"Projekty/plan.png"_s}
                                                        << u"assets/plan.png"_s << QString() << u"plan.png"_s;
        QTest::newRow("the other attachment") << QStringList{u"assets/plan.png"_s, u"Projekty/plan.png"_s}
                                              << u"Projekty/plan.png"_s << QString() << u"Projekty/plan.png"_s;
        QTest::newRow("journal") << QStringList{u"journals/2026-10-14.md"_s} << u"journals/2026-10-14.md"_s << QString()
                                 << u"2026-10-14"_s;
        QTest::newRow("empty path") << dom << QString() << QString() << none;
        QTest::newRow("dot-dot") << dom << u".."_s << QString() << none;
        QTest::newRow("folder, trailing slash") << dom << u"Projekty/"_s << QString() << none;
        QTest::newRow("not in the index") << dom << u"Inne/Dom.md"_s << QString() << none;
        QTest::newRow("'#' in a name") << QStringList{u"C#.md"_s} << u"C#.md"_s << QString() << none;
        QTest::newRow("'|' in a name") << QStringList{u"a|b.md"_s} << u"a|b.md"_s << QString() << none;
    }
    void linkTarget()
    {
        QFETCH(QStringList, files);
        QFETCH(QString, path);
        QFETCH(QString, from);
        QFETCH(QString, expected);
        VaultIndex index;
        for (const QString &f : std::as_const(files)) {
            const QString alias = f.section(u'|', 1);
            index.update(f.section(u'|', 0, 0),
                         alias.isEmpty() ? u"x"_s : u"---\naliases: ["_s + alias + u"]\n---\n"_s);
        }
        const std::optional<QString> target = index.linkTarget(path, from);
        QCOMPARE(target.value_or(u"<none>"_s), expected);
        // What it returns reaches the file from there.
        if (target)
            QCOMPARE(index.resolveLink(*target, from).value_or(QString()), paths::normalize(path));
    }

    void tagTarget()
    {
        VaultIndex index;
        build(index, {{u"Dom.md"_s, u"x"_s},
                      {u"Projekty/Dom.md"_s, u"x"_s},
                      {u"Projekty/Mój dom.md"_s, u"x"_s},
                      {u"assets/dom.png"_s, QString()},
                      {u"C++.md"_s, u"x"_s}});
        QCOMPARE(index.tagTarget(u"Dom.md"_s).value_or(QString()), u"Dom"_s);
        QCOMPARE(index.tagTarget(u"Projekty/Dom.md"_s).value_or(QString()), u"Projekty/Dom"_s);
        QCOMPARE(index.tagTarget(u"Projekty/Mój dom.md"_s).value_or(QString()), u"Mój dom"_s);
        QCOMPARE(index.tagTarget(u"C++.md"_s).value_or(QString()), u"C++"_s);
        QVERIFY(!index.tagTarget(u"assets/dom.png"_s));
        QVERIFY(!index.tagTarget(u"Brak.md"_s));
        QVERIFY(!index.tagTarget(QString()));
    }

    // ---- planRelocation ---------------------------------------------------------------------

    // A unique page moved: its links still reach it, nothing is rewritten.
    void moveUniquePageRewritesNothing()
    {
        VaultIndex index;
        build(index, {{u"Dom.md"_s, u"x"_s}, {u"Notatka.md"_s, u"[[Dom]] [[Dom|d]] ![[Dom#^id]] [x](Dom.md) #dom"_s}});
        const RelocationPlan plan = index.planRelocation({{u"Dom.md"_s, u"Projekty/Dom.md"_s}});
        QCOMPARE(plan.moves().size(), 1);
        QVERIFY(plan.files().isEmpty());
        QCOMPARE(plan.links(), 0);
        QCOMPARE(plan.tags(), 0);
    }

    // Renaming Projekty/Dom to "Ogród" next to a root Ogród.md: [[Ogród]] would name the root
    // page, so the links become [[Projekty/Ogród]]; the root page's own links stay.
    void renameNextToSameName()
    {
        VaultIndex index;
        build(index, {{u"Ogród.md"_s, u"x"_s},
                      {u"Projekty/Dom.md"_s, u"x"_s},
                      {u"Notatka.md"_s,
                       u"- [[Dom]] [[Dom|mój dom]] [[Dom#Kuchnia]] ![[Dom#^abc]]\r\n- [[Projekty/Dom]] [[Ogród]]\r\n"
                       u"- #dom #Projekty/Dom #ogród\r\n"_s}});
        const RelocationPlan plan = index.planRelocation({{u"Projekty/Dom.md"_s, u"Projekty/Ogród.md"_s}});
        QCOMPARE(planned(plan, u"Notatka.md"_s),
                 u"- [[Projekty/Ogród]] [[Projekty/Ogród|mój dom]] [[Projekty/Ogród#Kuchnia]] "
                 u"![[Projekty/Ogród#^abc]]\r\n"
                 u"- [[Projekty/Ogród]] [[Ogród]]\r\n- #Projekty/Ogród #Projekty/Ogród #ogród\r\n"_s);
        QCOMPARE(plan.links(), 5);
        QCOMPARE(plan.tags(), 2);
        const QStringList before = resolutions(index, {{u"Projekty/Dom.md"_s, u"Projekty/Ogród.md"_s}});
        apply(index, plan);
        QCOMPARE(resolutions(index), before);
    }

    // A folder rename changes path lengths: [[Dom]] named A/Dom.md (the shortest), after "A" →
    // "Archiwum" Bardzo/Dom.md would be shorter — the link becomes [[Archiwum/Dom]].
    void folderRenameKeepsBareLinks()
    {
        VaultIndex index;
        build(index, {{u"A/Dom.md"_s, u"x"_s},
                      {u"A/Plan.md"_s, u"[[Dom]] i [[A/Dom]]"_s},
                      {u"Bardzo/Dom.md"_s, u"x"_s},
                      {u"Notatka.md"_s, u"[[Dom]] [[Bardzo/Dom]] [x](A/Dom.md) #dom #A/Dom"_s}});
        const RelocationPlan plan =
                index.planRelocation({{u"A/Dom.md"_s, u"Archiwum/Dom.md"_s}, {u"A/Plan.md"_s, u"Archiwum/Plan.md"_s}});
        QCOMPARE(planned(plan, u"Notatka.md"_s),
                 u"[[Archiwum/Dom]] [[Bardzo/Dom]] [x](Archiwum/Dom.md) #Archiwum/Dom #Archiwum/Dom"_s);
        // In the moved note: [[Dom]] (its folder's page) still reaches it, the path follows.
        QCOMPARE(planned(plan, u"A/Plan.md"_s), u"[[Dom]] i [[Archiwum/Dom]]"_s);
        for (const RelocationPlan::File &f : plan.files()) {
            if (f.path == u"A/Plan.md")
                QCOMPARE(f.newPath, u"Archiwum/Plan.md"_s);
        }
        const QStringList before =
                resolutions(index, {{u"A/Dom.md"_s, u"Archiwum/Dom.md"_s}, {u"A/Plan.md"_s, u"Archiwum/Plan.md"_s}});
        apply(index, plan);
        QCOMPARE(resolutions(index), before);
    }

    // A page moved to a shallower folder takes [[Dom]] over: the links to the other Dom get its
    // path; the moved page's own links follow it.
    void moveToShallowerFolderQualifiesOthers()
    {
        VaultIndex index;
        build(index, {{u"Projekty/Dom.md"_s, u"x"_s},
                      {u"Archiwum/Stare/Dom.md"_s, u"stary"_s},
                      {u"Notatka.md"_s, u"[[Dom]], [[Stare/Dom]] i [x](Dom.md)"_s}});
        const RelocationPlan plan = index.planRelocation({{u"Archiwum/Stare/Dom.md"_s, u"Dom.md"_s}});
        QCOMPARE(planned(plan, u"Notatka.md"_s), u"[[Projekty/Dom]], [[Dom]] i [x](Projekty/Dom.md)"_s);
        const QStringList before = resolutions(index, {{u"Archiwum/Stare/Dom.md"_s, u"Dom.md"_s}});
        apply(index, plan);
        QCOMPARE(resolutions(index), before);
    }

    // Attachments inside a renamed folder: wiki embeds and Markdown pictures follow.
    void folderRenameMovesAttachmentLinks()
    {
        VaultIndex index;
        build(index, {{u"Projekty/plan.png"_s, QString()},
                      {u"Inne/plan.png"_s, QString()},
                      {u"Notatka.md"_s, u"![[Projekty/plan.png]] ![](Projekty/plan.png) ![[plan.png|200]]"_s}});
        const RelocationPlan plan = index.planRelocation({{u"Projekty/plan.png"_s, u"Archiwum/plan.png"_s}});
        // (![[plan.png]] named the shorter Inne/plan.png and still does.)
        QCOMPARE(planned(plan, u"Notatka.md"_s), u"![[Archiwum/plan.png]] ![](Archiwum/plan.png) ![[plan.png|200]]"_s);
        const QStringList before = resolutions(index, {{u"Projekty/plan.png"_s, u"Archiwum/plan.png"_s}});
        apply(index, plan);
        QCOMPARE(resolutions(index), before);
    }

    // A moved note's links relative to its old folder keep their targets.
    void movedNoteKeepsRelativeLinks()
    {
        VaultIndex index;
        build(index, {{u"Projekty/B.md"_s, u"x"_s},
                      {u"Xy/B.md"_s, u"x"_s},
                      {u"Projekty/A.md"_s, u"[[B]], [[#Nagłówek]], [z](../Xy/B.md) i [w](./B.md)"_s}});
        const RelocationPlan plan = index.planRelocation({{u"Projekty/A.md"_s, u"Archiwum/Głęboko/A.md"_s}});
        QCOMPARE(planned(plan, u"Projekty/A.md"_s),
                 u"[[Projekty/B]], [[#Nagłówek]], [z](../Xy/B.md) i [w](Projekty/B.md)"_s);
        const QStringList before = resolutions(index, {{u"Projekty/A.md"_s, u"Archiwum/Głęboko/A.md"_s}});
        apply(index, plan);
        QCOMPARE(resolutions(index), before);
    }

    // Tags resolve like links at the vault root: a bare tag the move would turn to another page
    // gets the page's path, a path-qualified tag follows its page; sub-tags and other tags stay.
    void tagsFollowTheirPages()
    {
        VaultIndex index;
        build(index, {{u"Projekty/Dom.md"_s, u"x"_s},
                      {u"Inne/Długie/Dom.md"_s, u"x"_s},
                      {u"Notatka.md"_s,
                       u"---\ntags: [dom, praca]\n---\n- #dom #Dom/kuchnia #Długie/Dom #praca\n  tags:: dom\n"_s}});
        // Projekty/Dom.md moves deeper: "dom" would name Inne/Długie/Dom.md (shorter now).
        const RelocationPlan plan =
                index.planRelocation({{u"Projekty/Dom.md"_s, u"Projekty/Bardzo/Głęboko/Ukryty/Dom.md"_s}});
        QCOMPARE(
                planned(plan, u"Notatka.md"_s),
                u"---\ntags: [Ukryty/Dom, praca]\n---\n- #Ukryty/Dom #Dom/kuchnia #Długie/Dom #praca\n  tags:: Ukryty/Dom\n"_s);
        QCOMPARE(plan.tags(), 3);
        apply(index, plan);
        QCOMPARE(index.resolveTag(u"Ukryty/Dom"_s).value_or(QString()), u"Projekty/Bardzo/Głęboko/Ukryty/Dom.md"_s);
        QCOMPARE(index.resolveTag(u"Długie/Dom"_s).value_or(QString()), u"Inne/Długie/Dom.md"_s);
    }

    // A rename of a page with a duplicate name elsewhere: only what named this page changes.
    void sameNamedPagesInOtherFolders()
    {
        VaultIndex index;
        build(index, {{u"Dom.md"_s, u"root"_s},
                      {u"Projekty/Dom.md"_s, u"x"_s},
                      {u"Notatka.md"_s, u"[[Dom]] [[Projekty/Dom]] #dom #Projekty/Dom"_s}});
        const RelocationPlan plan = index.planRelocation({{u"Projekty/Dom.md"_s, u"Projekty/Chata.md"_s}});
        QCOMPARE(planned(plan, u"Notatka.md"_s), u"[[Dom]] [[Projekty/Chata]] #dom #Projekty/Chata"_s);
        // The root page renamed: the bare links and tags get the new name.
        const RelocationPlan root = index.planRelocation({{u"Dom.md"_s, u"Mieszkanie.md"_s}});
        QCOMPARE(planned(root, u"Notatka.md"_s), u"[[Mieszkanie]] [[Projekty/Dom]] #Mieszkanie #Projekty/Dom"_s);
    }

    // Names and aliases: a link through an alias keeps working, an alias moved with its page.
    void aliasesAndHeadings()
    {
        VaultIndex index;
        build(index, {{u"Ludzie/Jan Kowalski.md"_s, u"---\naliases: [Janek]\n---\n"_s},
                      {u"Notatka.md"_s, u"[[Janek]] [[Jan Kowalski#Kontakt|Jan]] [[Ludzie/Jan Kowalski#^id1]]"_s}});
        const RelocationPlan plan =
                index.planRelocation({{u"Ludzie/Jan Kowalski.md"_s, u"Kontakty/Jan Kowalski.md"_s}});
        QCOMPARE(planned(plan, u"Notatka.md"_s),
                 u"[[Janek]] [[Jan Kowalski#Kontakt|Jan]] [[Kontakty/Jan Kowalski#^id1]]"_s);
        QCOMPARE(plan.links(), 1);
    }

    // The plan applied to a note changed since: its links are checked anew.
    void rewriteChangedContent()
    {
        VaultIndex index;
        build(index, {{u"Ogród.md"_s, u"x"_s}, {u"Projekty/Dom.md"_s, u"x"_s}, {u"Notatka.md"_s, u"[[Dom]]"_s}});
        const RelocationPlan plan = index.planRelocation({{u"Projekty/Dom.md"_s, u"Projekty/Ogród.md"_s}});
        QCOMPARE(plan.rewrite(u"Notatka.md"_s, u"[[Dom]]"_s), u"[[Projekty/Ogród]]"_s);
        QCOMPARE(plan.rewrite(u"Notatka.md"_s, u"- nowy blok\n- [[Dom|dom]]"_s),
                 u"- nowy blok\n- [[Projekty/Ogród|dom]]"_s);
        QCOMPARE(plan.rewrite(u"Inna.md"_s, u"[[Dom]] [[Ogród]]"_s), u"[[Projekty/Ogród]] [[Ogród]]"_s);
        QCOMPARE(plan.rewrite(u"Inna.md"_s, u"bez linków"_s), u"bez linków"_s);
        QCOMPARE(RelocationPlan().rewrite(u"Inna.md"_s, u"[[Dom]]"_s), u"[[Dom]]"_s);
    }

    // Unknown files, no-ops and empty paths plan nothing.
    void malformedMoves()
    {
        VaultIndex index;
        build(index, {{u"Dom.md"_s, u"x"_s}, {u"Notatka.md"_s, u"[[Dom]]"_s}});
        QVERIFY(index.planRelocation({}).moves().isEmpty());
        QVERIFY(index.planRelocation({{u"Brak.md"_s, u"Inne.md"_s}}).moves().isEmpty());
        QVERIFY(index.planRelocation({{u"Dom.md"_s, u"Dom.md"_s}}).moves().isEmpty());
        QVERIFY(index.planRelocation({{QString(), u"x.md"_s}, {u"Dom.md"_s, QString()}}).moves().isEmpty());
        const RelocationPlan twice = index.planRelocation({{u"Dom.md"_s, u"A.md"_s}, {u"Dom.md"_s, u"B.md"_s}});
        QCOMPARE(twice.moves().size(), 1);
        QCOMPARE(planned(twice, u"Notatka.md"_s), u"[[A]]"_s);
    }

    // The fixture vault: Projekty → Archiwum/Projekty keeps every link of every note.
    void fixtureFolderMoveKeepsEveryLink()
    {
        VaultIndex index;
        fixture::build(index);
        QList<FileMove> moves;
        QHash<QString, QString> moved;
        for (const VaultPath &p : index.paths() + index.attachments()) {
            if (p.startsWith(u"Projekty/"_s)) {
                moves.append({p, u"Archiwum/"_s + p});
                moved.insert(p, u"Archiwum/"_s + p);
            }
        }
        QVERIFY(!moves.isEmpty());
        const QStringList before = resolutions(index, moved);
        const RelocationPlan plan = index.planRelocation(moves);
        apply(index, plan);
        QCOMPARE(resolutions(index), before);
    }

    // Random notebooks with many duplicate names, random moves of pages and folders: after the
    // plan every link names the file it named before.
    void randomMovesKeepEveryLink()
    {
        QRandomGenerator rng(20261023);
        const QStringList names{u"Dom"_s, u"Ogród"_s, u"Żółw"_s, u"Plan"_s, u"Łąka"_s};
        const QStringList folders{QString(), u"A"_s, u"Projekty"_s, u"Projekty/Dom"_s, u"B/C"_s, u"Archiwum/Stare"_s};
        for (int round = 0; round < 60; ++round) {
            VaultIndex index;
            QStringList notes;
            for (int i = 0; i < 12; ++i) {
                const QString folder = folders.at(rng.bounded(int(folders.size())));
                const QString name = names.at(rng.bounded(int(names.size())));
                const QString path = (folder.isEmpty() ? QString() : folder + u'/') + name + u".md"_s;
                if (!notes.contains(path, Qt::CaseInsensitive))
                    notes.append(path);
            }
            QList<QPair<QString, QString>> files;
            for (const QString &path : std::as_const(notes)) {
                QString text;
                for (int l = 0; l < 4; ++l) {
                    const QString other = notes.at(rng.bounded(int(notes.size())));
                    const QString stem = paths::stripMarkdownExtension(other);
                    switch (rng.bounded(4)) {
                    case 0: text += u"- [["_s + paths::pageTitle(other) + u"]]\n"_s; break;
                    case 1: text += u"- [["_s + stem + u"#Nagłówek|x]]\n"_s; break;
                    case 2: text += u"- [x]("_s + QString(stem).replace(u' ', u"%20"_s) + u".md)\n"_s; break;
                    default: text += u"- #"_s + paths::pageTitle(other) + u"\n"_s; break;
                    }
                }
                files.append({path, text});
            }
            build(index, files);
            // Move one page to another folder, or rename a folder.
            QList<FileMove> moves;
            if (rng.bounded(2) == 0) {
                const QString page = notes.at(rng.bounded(int(notes.size())));
                const QString folder = folders.at(rng.bounded(int(folders.size())));
                const QString to = (folder.isEmpty() ? QString() : folder + u'/') + paths::basename(page);
                if (!notes.contains(to, Qt::CaseInsensitive))
                    moves.append({page, to});
            } else {
                const QString folder = folders.at(1 + rng.bounded(int(folders.size()) - 1));
                const QString renamed = u"Nowy/"_s + folder;
                for (const QString &n : std::as_const(notes)) {
                    if (n.startsWith(folder + u'/'))
                        moves.append({n, renamed + n.mid(folder.size())});
                }
            }
            QHash<QString, QString> moved;
            for (const FileMove &m : std::as_const(moves))
                moved.insert(m.from, m.to);
            const QStringList before = resolutions(index, moved);
            const QStringList tagsBefore = tagResolutions(index, moved);
            const RelocationPlan plan = index.planRelocation(moves);
            apply(index, plan);
            QCOMPARE(resolutions(index), before);
            QCOMPARE(tagResolutions(index), tagsBefore);
        }
    }
};

QTEST_GUILESS_MAIN(TestIndexRelocate)
#include "tst_index_relocate.moc"
