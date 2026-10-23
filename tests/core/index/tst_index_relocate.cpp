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

/// The plan made from the index (its parsed links; only notes with a ```query block or a renamed
/// tag looked into for those) equals the plan of each note's text read anew: rewrite() of the
/// text with a block added at its end.
void checkPlanOfTheText(const VaultIndex &index, const RelocationPlan &plan)
{
    const QString tail = u"\n- dopisane po planie\n"_s;
    for (const VaultPath &p : index.paths()) {
        const QString content = index.content(p);
        const QString after = planned(plan, p);
        QCOMPARE(plan.rewrite(p, content + tail), (after.isNull() ? content : after) + tail);
    }
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

    // The filters of ```query blocks that name a moved page or folder or a renamed tag follow them
    // (SYNTAX §2, §10): exactly the tag (sub-tags stay), ref: / [[…]] / page: like links, path: a
    // prefix whose every file moved; counted as links and tags.
    void queriesFollow_data()
    {
        QTest::addColumn<QStringList>("files"); // "path=content"; the query note is Q.md
        QTest::addColumn<QString>("from");
        QTest::addColumn<QString>("to");       // a folder move: "from/" -> "to/"
        QTest::addColumn<QString>("query");    // Q.md's query line before
        QTest::addColumn<QString>("expected"); // … and after ("" = Q.md is not rewritten)
        QTest::addColumn<int>("links");        // the plan's counts (links, tags, queries)
        QTest::addColumn<int>("tags");
        QTest::addColumn<int>("queries");
        const QStringList dom{u"Dom.md=- x"_s, u"Notatka.md=- #dom #dom/kuchnia"_s};
        QTest::newRow("a page renamed: every key, negated, in lists")
                << dom << u"Dom.md"_s << u"Mieszkanie.md"_s
                << u"type:tasks tag:dom -tag:dom/kuchnia #dom tagi:praca,DOM ref:Dom [[Dom]] page:Dom ścieżka:Dom"_s
                << u"type:tasks tag:Mieszkanie -tag:dom/kuchnia #Mieszkanie tagi:praca,Mieszkanie ref:Mieszkanie "
                   "[[Mieszkanie]] page:Mieszkanie ścieżka:Mieszkanie"_s
                << 4 << 4 << 7; // links: 4 filters; tags: Notatka's #dom + 3 filters
        QTest::newRow("a name with spaces: in [[ ]] or quotes")
                << dom << u"Dom.md"_s << u"Mój dom.md"_s << u"tag:dom #dom ref:Dom ref:[[Dom]] page:\"Dom\" path:Dom"_s
                << u"tag:[[Mój dom]] #[[Mój dom]] ref:[[Mój dom]] ref:[[Mój dom]] page:\"Mój dom\" path:[[Mój dom]]"_s
                << 4 << 3 << 6;
        QTest::newRow("comments, phrases, wildcards and this stay")
                << dom << u"Dom.md"_s << u"Mieszkanie.md"_s
                << u"page:*Dom* path:D* ref:this page:ta \"tag:dom\" ref:Dom // tag:dom ref:Dom"_s
                << u"page:*Dom* path:D* ref:this page:ta \"tag:dom\" ref:Mieszkanie // tag:dom ref:Dom"_s << 1 << 1
                << 1;
        QTest::newRow("a folder renamed: path prefixes and paths")
                << QStringList{u"Projekty/Dom.md=- x"_s, u"Projekty/Plan.md=- y"_s} << u"Projekty/"_s << u"Prace/"_s
                << u"path:Projekty/ -path:Projekty/Plan path:./Projekty/ ref:[[Projekty/Dom]] tag:Projekty/Dom "
                   "page:Projekty/Dom ref:Dom"_s
                << u"path:Prace/ -path:Prace/Plan path:./Prace/ ref:[[Prace/Dom]] tag:Prace/Dom page:Prace/Dom ref:Dom"_s
                << 5 << 1 << 6;
        QTest::newRow("a folder moved deeper: only path: changes (suffixes still resolve)")
                << QStringList{u"Projekty/Dom.md=- x"_s} << u"Projekty/"_s << u"Archiwum/Projekty/"_s
                << u"path:Projekty/ ref:[[Projekty/Dom]] tag:Projekty/Dom page:Projekty/Dom"_s
                << u"path:Archiwum/Projekty/ ref:[[Projekty/Dom]] tag:Projekty/Dom page:Projekty/Dom"_s << 1 << 0 << 1;
        QTest::newRow("a prefix that also names a file that stays")
                << QStringList{u"Projekty/Dom.md=- x"_s, u"Projekty-stare/Dom.md=- y"_s} << u"Projekty/"_s
                << u"Prace/"_s << u"path:Projekty path:Projekty/"_s << u"path:Projekty path:Prace/"_s << 1 << 0 << 1;
        QTest::newRow("same names elsewhere: only what named this page")
                << QStringList{u"Dom.md=- root"_s, u"Projekty/Dom.md=- x"_s, u"Notatka.md=- #dom #Projekty/Dom"_s}
                << u"Projekty/Dom.md"_s << u"Projekty/Chata.md"_s
                << u"ref:Dom tag:dom ref:Projekty/Dom tag:Projekty/Dom page:Projekty/Dom"_s
                << u"ref:Dom tag:dom ref:Projekty/Chata tag:Projekty/Chata page:Projekty/Chata"_s << 2 << 2 << 3;
        // (`page:Dom` matched both pages titled Dom: one value cannot name the two, it stays.)
        QTest::newRow("the shorter of two Doms renamed: the other would take its name")
                << QStringList{u"Dom.md=- root"_s, u"Projekty/Dom.md=- x"_s, u"Notatka.md=- #dom #Projekty/Dom"_s}
                << u"Dom.md"_s << u"Mieszkanie.md"_s << u"ref:Dom tag:dom page:Dom ref:Projekty/Dom tag:Projekty/Dom"_s
                << u"ref:Mieszkanie tag:Mieszkanie page:Dom ref:Projekty/Dom tag:Projekty/Dom"_s << 1 << 2 << 2;
        QTest::newRow("a missing page stays missing")
                << dom << u"Dom.md"_s << u"Mieszkanie.md"_s << u"ref:Nieistniejąca tag:nieistniejący"_s << QString()
                << 0 << 1 << 0; // (only Notatka's #dom)
        QTest::newRow("Polish names and paths")
                << QStringList{u"Zwierzęta/Żółw.md=- x"_s} << u"Zwierzęta/Żółw.md"_s << u"Gady/Żółw.md"_s
                << u"ref:Zwierzęta/Żółw path:Zwierzęta/Żółw page:Żółw"_s << u"ref:Gady/Żółw path:Gady/Żółw page:Żółw"_s
                << 2 << 0 << 2;
        // The new prefix would match more than the files the old one did: the query would widen.
        QTest::newRow("a page moved into a folder with other pages: its folder prefix stays")
                << QStringList{u"Skrzynka/Dom.md=- x"_s, u"Archiwum/Stare.md=- y"_s} << u"Skrzynka/Dom.md"_s
                << u"Archiwum/Dom.md"_s << u"path:Skrzynka/ path:Skrzynka/Dom"_s
                << u"path:Skrzynka/ path:Archiwum/Dom"_s << 1 << 0 << 1;
        QTest::newRow("a page renamed to the start of another page's name: path: stays")
                << QStringList{u"Dom.md=- x"_s, u"Mieszkanie stare.md=- y"_s} << u"Dom.md"_s << u"Mieszkanie.md"_s
                << u"path:Dom"_s << QString() << 0 << 0 << 0;
        // A note written on another system (NFD) names the folder as runQuery reads it.
        QTest::newRow("a prefix written in NFD")
                << QStringList{u"Zwierzęta/Żółw.md=- x"_s, u"Zwierzęta/Łoś.md=- y"_s} << u"Zwierzęta/"_s << u"Gady/"_s
                << u"path:Zwierzęta/"_s.normalized(QString::NormalizationForm_D) << u"path:Gady/"_s << 1 << 0 << 1;
    }
    void queriesFollow()
    {
        QFETCH(QStringList, files);
        QFETCH(QString, from);
        QFETCH(QString, to);
        QFETCH(QString, query);
        QFETCH(QString, expected);
        QFETCH(int, links);
        QFETCH(int, tags);
        QFETCH(int, queries);
        VaultIndex index;
        for (const QString &f : std::as_const(files))
            index.update(f.section(u'=', 0, 0), f.section(u'=', 1));
        const QString note = u"# Zapytanie\r\n\r\n```query\r\n"_s + query + u"\r\n```\r\n- [[Inna]] tag:dom\r\n"_s;
        index.update(u"Q.md"_s, note);
        QCOMPARE(index.notesWithQueries(), QList<VaultPath>{u"Q.md"_s});
        QList<FileMove> moves;
        if (from.endsWith(u'/')) {
            for (const VaultPath &p : index.paths() + index.attachments()) {
                if (p.startsWith(from))
                    moves.append({p, to + p.mid(from.size())});
            }
        } else {
            moves.append({from, to});
        }
        const RelocationPlan plan = index.planRelocation(moves);
        const QString after = planned(plan, u"Q.md"_s);
        if (expected.isEmpty())
            QCOMPARE(after, QString());
        else
            QCOMPARE(after, QString(note).replace(query, expected)); // CRLF and the rest byte for byte
        QCOMPARE(plan.links(), links);
        QCOMPARE(plan.tags(), tags);
        QCOMPARE(plan.queries(), queries);
        // A note changed since the plan: its query is checked anew.
        if (!expected.isEmpty())
            QCOMPARE(plan.rewrite(u"Q.md"_s, u"```query\n"_s + query + u"\n```"_s),
                     u"```query\n"_s + expected + u"\n```"_s);
    }

    // `page:` matches a page's title, aliases, path or link target (runQuery): after a move it
    // matches exactly the pages it matched, at their new paths (SYNTAX §2). The moved page's value
    // is written as a link to it would be, or — when that name is another page's too — as the
    // shortest form naming only it; a value naming a page that stays is qualified when a moved
    // page would share it. A value that matched several pages or none stays, and so do `*` and
    // `this`.
    void pageFiltersKeepTheirPages_data()
    {
        QTest::addColumn<QStringList>("files"); // "path=content"; the query note is Q.md
        QTest::addColumn<QString>("from");
        QTest::addColumn<QString>("to");
        QTest::addColumn<QString>("query");     // Q.md's filters (type:pages)
        QTest::addColumn<QString>("expected");  // … after the move ("" = Q.md is not rewritten)
        QTest::addColumn<int>("rewritten");     // values rewritten (counted as links)
        QTest::addColumn<QStringList>("pages"); // what it matches before the move
        QTest::addColumn<bool>("kept");         // … and after it, at their new paths
        const QStringList mieszkanie{u"Dom.md=- x"_s, u"Archiwum/Mieszkanie.md=- y"_s};
        QTest::newRow("renamed to another page's title: its path with .md")
                << mieszkanie << u"Dom.md"_s << u"Mieszkanie.md"_s << u"page:Dom"_s << u"page:Mieszkanie.md"_s << 1
                << QStringList{u"Dom.md"_s} << true;
        QTest::newRow("another page renamed to its title: qualified")
                << mieszkanie << u"Dom.md"_s << u"Mieszkanie.md"_s << u"page:Mieszkanie"_s
                << u"page:Archiwum/Mieszkanie"_s << 1 << QStringList{u"Archiwum/Mieszkanie.md"_s} << true;
        QTest::newRow("both, case variants, a list, negated, the Polish key")
                << mieszkanie << u"Dom.md"_s << u"Mieszkanie.md"_s << u"page:dom,MIESZKANIE -strona:mieszkanie"_s
                << u"page:Mieszkanie.md,Archiwum/Mieszkanie -strona:Archiwum/Mieszkanie"_s << 3
                << QStringList{u"Dom.md"_s} << true;
        QTest::newRow("the extension as written") << mieszkanie << u"Dom.md"_s << u"Mieszkanie.md"_s << u"page:dom.MD"_s
                                                  << u"page:Mieszkanie.MD"_s << 1 << QStringList{u"Dom.md"_s} << true;
        QTest::newRow("renamed in place: the folder as written")
                << QStringList{u"Projekty/Dom.md=- x"_s, u"Archiwum/Mieszkanie.md=- y"_s} << u"Projekty/Dom.md"_s
                << u"Projekty/Mieszkanie.md"_s << u"page:PROJEKTY/dom,Mieszkanie"_s
                << u"page:PROJEKTY/Mieszkanie,Archiwum/Mieszkanie"_s << 2
                << QStringList{u"Projekty/Dom.md"_s, u"Archiwum/Mieszkanie.md"_s} << true;
        QTest::newRow("a name without diacritics another page takes")
                << QStringList{u"Zwierzęta/Żółw.md=- x"_s, u"Szkic.md=- y"_s} << u"Szkic.md"_s << u"Zolw.md"_s
                << u"page:zolw"_s << u"page:Zwierzęta/Żółw"_s << 1 << QStringList{u"Zwierzęta/Żółw.md"_s} << true;
        QTest::newRow("an alias another page takes as its title: the title")
                << QStringList{u"Ludzie/Jan Kowalski.md=---\naliases: [Janek]\n---\n- x"_s, u"Szkic.md=- y"_s}
                << u"Szkic.md"_s << u"Janek.md"_s << u"page:Janek"_s << u"page:[[Jan Kowalski]]"_s << 1
                << QStringList{u"Ludzie/Jan Kowalski.md"_s} << true;
        QTest::newRow("a moved page keeps its alias")
                << QStringList{u"Ludzie/Jan Kowalski.md=---\naliases: [Janek]\n---\n- x"_s}
                << u"Ludzie/Jan Kowalski.md"_s << u"Ludzie/Jan Nowak.md"_s << u"page:Janek page:[[Jan Kowalski]]"_s
                << u"page:Janek page:[[Jan Nowak]]"_s << 1 << QStringList{u"Ludzie/Jan Kowalski.md"_s} << true;
        QTest::newRow("journals: a page renamed to a day")
                << QStringList{u"journals/2026-10-13.md=- x"_s, u"Szkic.md=- y"_s} << u"Szkic.md"_s
                << u"2026-10-13.md"_s << u"page:2026-10-13"_s << u"page:journals/2026-10-13"_s << 1
                << QStringList{u"journals/2026-10-13.md"_s} << true;
        QTest::newRow("journals: a day moved to a name another page has")
                << QStringList{u"journals/2026-10-13.md=- x"_s, u"Archiwum/2026-10-14.md=- y"_s}
                << u"journals/2026-10-13.md"_s << u"journals/2026-10-14.md"_s << u"page:2026-10-13"_s
                << u"page:journals/2026-10-14"_s << 1 << QStringList{u"journals/2026-10-13.md"_s} << true;
        QTest::newRow("the query's note moves: a relative value")
                << QStringList{u"Mieszkanie.md=- x"_s, u"Archiwum/Mieszkanie.md=- y"_s} << u"Q.md"_s
                << u"Archiwum/Q.md"_s << u"page:./Mieszkanie"_s << u"page:Mieszkanie.md"_s << 1
                << QStringList{u"Mieszkanie.md"_s} << true;
        QTest::newRow("a page moved, its title kept: nothing to do")
                << QStringList{u"Skrzynka/Żółw.md=- x"_s, u"Archiwum/Łoś.md=- y"_s} << u"Skrzynka/Żółw.md"_s
                << u"Archiwum/Żółw.md"_s << u"page:żółw"_s << QString() << 0 << QStringList{u"Skrzynka/Żółw.md"_s}
                << true;
        // What one value cannot keep stays as written.
        QTest::newRow("several pages of that title: stays")
                << QStringList{u"Dom.md=- x"_s, u"Projekty/Dom.md=- y"_s} << u"Dom.md"_s << u"Mieszkanie.md"_s
                << u"page:Dom"_s << QString() << 0 << QStringList{u"Dom.md"_s, u"Projekty/Dom.md"_s} << false;
        QTest::newRow("a missing page a renamed page takes: stays")
                << QStringList{u"Dom.md=- x"_s} << u"Dom.md"_s << u"Mieszkanie.md"_s << u"page:Mieszkanie"_s
                << QString() << 0 << QStringList() << false;
        QTest::newRow("a wildcard: stays") << mieszkanie << u"Dom.md"_s << u"Mieszkanie.md"_s << u"page:Mieszk*"_s
                                           << QString() << 0 << QStringList{u"Archiwum/Mieszkanie.md"_s} << false;
    }
    void pageFiltersKeepTheirPages()
    {
        QFETCH(QStringList, files);
        QFETCH(QString, from);
        QFETCH(QString, to);
        QFETCH(QString, query);
        QFETCH(QString, expected);
        QFETCH(int, rewritten);
        QFETCH(QStringList, pages);
        QFETCH(bool, kept);
        VaultIndex index;
        for (const QString &f : std::as_const(files))
            index.update(f.section(u'=', 0, 0), f.section(u'=', 1));
        const QString note = u"# Zapytanie\r\n\r\n```query\r\ntype:pages "_s + query + u"\r\n```\r\n- koniec\r\n"_s;
        index.update(u"Q.md"_s, note);
        // The pages the query finds (runQuery from its note at `at`).
        const auto found = [&index](const QString &filters, const QString &at) {
            const auto parsed = parseQuery(u"type:pages "_s + filters);
            if (!std::holds_alternative<Query>(parsed))
                return QStringList{u"error: "_s + std::get<QString>(parsed)};
            QueryOptions options;
            options.currentPath = at;
            QStringList out;
            const QueryResult result =
                    runQuery(std::get<Query>(parsed), index, QDate(2026, 10, 14), Qt::Monday, options);
            for (const QueryItem &item : result.items)
                out.append(item.path);
            out.sort();
            return out;
        };
        pages.sort();
        QCOMPARE(found(query, u"Q.md"_s), pages);

        const RelocationPlan plan = index.planRelocation({{from, to}});
        if (expected.isEmpty())
            QCOMPARE(planned(plan, u"Q.md"_s), QString());
        else
            QCOMPARE(planned(plan, u"Q.md"_s), QString(note).replace(query, expected)); // CRLF, byte for byte
        QCOMPARE(plan.queries(), rewritten);
        QCOMPARE(plan.links(), rewritten);
        checkPlanOfTheText(index, plan);

        apply(index, plan);
        QStringList movedPages;
        for (const QString &p : std::as_const(pages))
            movedPages.append(p == from ? to : p);
        movedPages.sort();
        const QStringList now = found(expected.isEmpty() ? query : expected, from == u"Q.md" ? to : u"Q.md"_s);
        if (kept)
            QCOMPARE(now, movedPages);
        else
            QVERIFY(now != movedPages);
    }

    // A `path:` prefix keeps its meaning only when the new one matches exactly the files the old
    // one did: not one that was moved there from elsewhere.
    void pathPrefixDoesNotWiden()
    {
        VaultIndex index;
        const QString note = u"```query\npath:A/\n```\n"_s;
        build(index, {{u"A/x.md"_s, u"- x\n"_s}, {u"C/y.md"_s, u"- y\n"_s}, {u"Q.md"_s, note}});
        const RelocationPlan both = index.planRelocation({{u"A/x.md"_s, u"B/x.md"_s}, {u"C/y.md"_s, u"B/y.md"_s}});
        QCOMPARE(planned(both, u"Q.md"_s), QString());
        QCOMPARE(both.queries(), 0);
        QCOMPARE(both.rewrite(u"Q.md"_s, note), note);
        // Alone, x moves as the whole of A/.
        const RelocationPlan one = index.planRelocation({{u"A/x.md"_s, u"B/x.md"_s}});
        QCOMPARE(planned(one, u"Q.md"_s), u"```query\npath:B/\n```\n"_s);
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

    // RelocationPlanner: the plan of planRelocation() a note at a time; dropped when the index
    // changes in between.
    void plannerMakesThePlanInSteps()
    {
        VaultIndex index;
        fixture::build(index);
        build(index, {{u"Zapytania.md"_s, u"```query\ntag:dom path:Projekty/ ref:[[Projekty/Dom]]\n```\n"_s},
                      {u"Łąka/Notatka.md"_s, u"- [[Projekty/Dom]] #[[Projekty/Dom]] [[Dom]]\n"_s}});
        QList<FileMove> moves;
        for (const VaultPath &p : index.paths() + index.attachments()) {
            if (p.startsWith(u"Projekty/"_s))
                moves.append({p, u"Archiwum 2026/"_s + p.mid(9)});
        }
        QVERIFY(moves.size() > 1);
        const RelocationPlan whole = index.planRelocation(moves);
        QVERIFY(whole.files().size() > 2);
        RelocationPlanner planner(index, moves);
        QVERIFY(planner.valid());
        int steps = 0;
        while (!planner.step(0))
            ++steps;
        QVERIFY(steps > 0);
        QVERIFY(planner.done());
        const RelocationPlan stepped = planner.plan();
        QCOMPARE(stepped.moves(), whole.moves());
        QCOMPARE(stepped.files().size(), whole.files().size());
        for (qsizetype i = 0; i < whole.files().size(); ++i) {
            const RelocationPlan::File &a = whole.files().at(i);
            const RelocationPlan::File &b = stepped.files().at(i);
            QCOMPARE(b.path, a.path);
            QCOMPARE(b.newPath, a.newPath);
            QCOMPARE(b.after, a.after);
            QCOMPARE(b.links, a.links);
            QCOMPARE(b.tags, a.tags);
            QCOMPARE(b.queries, a.queries);
        }
        QCOMPARE(stepped.links(), whole.links());
        QCOMPARE(stepped.tags(), whole.tags());
        QCOMPARE(stepped.queries(), whole.queries());

        // The index changes half way: the planner stops (its caller makes a new one).
        RelocationPlanner interrupted(index, moves);
        QVERIFY(!interrupted.step(0));
        index.update(u"Nowa.md"_s, u"- [[Projekty/Dom]]\n"_s);
        QVERIFY(!interrupted.valid());
        const qsizetype planned = interrupted.plan().files().size();
        QVERIFY(!interrupted.step(1000));
        QCOMPARE(interrupted.plan().files().size(), planned);

        // Nothing to move: done at once, an empty plan.
        RelocationPlanner nothing(index, {{u"Brak.md"_s, u"Inne.md"_s}});
        QVERIFY(nothing.done());
        QVERIFY(nothing.step(0));
        QVERIFY(nothing.plan().moves().isEmpty());
        QVERIFY(nothing.plan().files().isEmpty());
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
        checkPlanOfTheText(index, plan);
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
                    switch (rng.bounded(6)) {
                    case 0: text += u"- [["_s + paths::pageTitle(other) + u"]]\n"_s; break;
                    case 1: text += u"- [["_s + stem + u"#Nagłówek|x]]\n"_s; break;
                    case 2: text += u"- [x]("_s + QString(stem).replace(u' ', u"%20"_s) + u".md)\n"_s; break;
                    case 3:
                        text += u"- ```query\n  tag:"_s + paths::pageTitle(other) + u" ref:[["_s + stem + u"]] path:"_s
                                + paths::dirname(other) + u"/\n  ```\n"_s;
                        break;
                    case 4: text += u"- #[["_s + stem + u"]]\n"_s; break;
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
            checkPlanOfTheText(index, plan);
            apply(index, plan);
            QCOMPARE(resolutions(index), before);
            QCOMPARE(tagResolutions(index), tagsBefore);
        }
    }
};

QTEST_GUILESS_MAIN(TestIndexRelocate)
#include "tst_index_relocate.moc"
