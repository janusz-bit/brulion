// Link resolution (docs/SYNTAX.md §2) and its incremental maintenance in VaultIndex.
#include "fixture.h"

#include <QTest>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

QString resolved(const VaultIndex &index, const QString &target, const QString &from = {})
{
    return index.resolveLink(target, from).value_or(u"<none>"_s);
}

QStringList backlinkSources(const VaultIndex &index, const QString &path)
{
    QStringList out;
    for (const Backlink &b : index.backlinks(path))
        out.append(b.path);
    return out;
}

} // namespace

class TestIndexResolve : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void fixtureTargets_data()
    {
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("from");
        QTest::addColumn<QString>("expected");
        const QString none = u"<none>"_s;
        // 1) exact path, with or without .md, case-insensitive
        QTest::newRow("exact path") << u"Projekty/Dom"_s << QString() << u"Projekty/Dom.md"_s;
        QTest::newRow("exact path with .md") << u"Projekty/Dom.md"_s << QString() << u"Projekty/Dom.md"_s;
        QTest::newRow("exact path, other case") << u"projekty/DOM"_s << QString() << u"Projekty/Dom.md"_s;
        QTest::newRow("root anchored") << u"/Zakupy"_s << u"Projekty/Dom.md"_s << u"Zakupy.md"_s;
        QTest::newRow("attachment path") << u"assets/salon.png"_s << QString() << u"assets/salon.png"_s;
        QTest::newRow("attachment path, case") << u"ASSETS/Salon.PNG"_s << QString() << u"assets/salon.png"_s;
        // 2) relative to the linking file's folder
        QTest::newRow("explicit relative") << u"../Zakupy.md"_s << u"Projekty/Budżet.md"_s << u"Zakupy.md"_s;
        QTest::newRow("dot relative") << u"./Budżet"_s << u"Projekty/Dom.md"_s << u"Projekty/Budżet.md"_s;
        QTest::newRow("bare name in source folder")
                << u"Dom"_s << u"Archiwum/Stare/Inna.md"_s << u"Archiwum/Stare/Dom.md"_s;
        QTest::newRow("relative subpath") << u"Stare/Dom"_s << u"Archiwum/Notatka.md"_s << u"Archiwum/Stare/Dom.md"_s;
        // 3) base name: shortest path, then alphabetical
        QTest::newRow("unique base name") << u"Budżet"_s << QString() << u"Projekty/Budżet.md"_s;
        QTest::newRow("duplicate name -> shortest") << u"Dom"_s << QString() << u"Projekty/Dom.md"_s;
        QTest::newRow("duplicate name, lower case") << u"dom"_s << u"Notatki.md"_s << u"Projekty/Dom.md"_s;
        QTest::newRow("base name with .md") << u"Jan Kowalski.md"_s << QString() << u"Ludzie/Jan Kowalski.md"_s;
        QTest::newRow("path suffix") << u"Stare/Dom"_s << QString() << u"Archiwum/Stare/Dom.md"_s;
        QTest::newRow("attachment by name") << u"salon.png"_s << u"Projekty/Dom.md"_s << u"assets/salon.png"_s;
        QTest::newRow("attachment at root") << u"plan.pdf"_s << QString() << u"plan.pdf"_s;
        // 4) aliases (frontmatter and Logseq alias::)
        QTest::newRow("frontmatter alias") << u"Remont"_s << QString() << u"Projekty/Dom.md"_s;
        QTest::newRow("multi-word alias, case") << u"dom jana"_s << QString() << u"Projekty/Dom.md"_s;
        QTest::newRow("logseq alias") << u"Janek"_s << QString() << u"Ludzie/Jan Kowalski.md"_s;
        // fragments and decoration are ignored
        QTest::newRow("block fragment") << u"Projekty/Dom#^plan01"_s << QString() << u"Projekty/Dom.md"_s;
        QTest::newRow("heading fragment") << u"Budżet#Koszty"_s << QString() << u"Projekty/Budżet.md"_s;
        QTest::newRow("display part") << u"Remont|remoncie"_s << QString() << u"Projekty/Dom.md"_s;
        QTest::newRow("brackets") << u"[[Zakupy]]"_s << QString() << u"Zakupy.md"_s;
        QTest::newRow("surrounding spaces") << u"  Zakupy  "_s << QString() << u"Zakupy.md"_s;
        QTest::newRow("backslashes") << u"Projekty\\Dom"_s << QString() << u"Projekty/Dom.md"_s;
        // Unicode: NFD input, Polish letters in other case
        QTest::newRow("nfd target") << u"Budz\u0307et"_s << QString() << u"Projekty/Budżet.md"_s;
        QTest::newRow("upper-case polish") << u"BUDŻET"_s << QString() << u"Projekty/Budżet.md"_s;
        // unresolved
        QTest::newRow("missing page") << u"Nieistniejąca strona"_s << QString() << none;
        QTest::newRow("missing attachment") << u"brak.png"_s << QString() << none;
        QTest::newRow("empty without source") << QString() << QString() << none;
        QTest::newRow("only a fragment") << u"#^plan01"_s << u"Projekty/Dom.md"_s << u"Projekty/Dom.md"_s;
        QTest::newRow("diacritics matter") << u"Budzet"_s << QString() << none;
    }
    void fixtureTargets()
    {
        QFETCH(QString, target);
        QFETCH(QString, from);
        QFETCH(QString, expected);
        VaultIndex index;
        fixture::build(index);
        QCOMPARE(resolved(index, target, from), expected);
    }

    void exactPathBeatsBaseName()
    {
        VaultIndex index;
        index.update(u"a/Note.md"_s, u"x"_s);
        index.update(u"Note.md"_s, u"x"_s);
        index.update(u"b/c/Note.md"_s, u"x"_s);
        QCOMPARE(resolved(index, u"Note"_s, u"b/c/Other.md"_s), u"Note.md"_s);     // root first
        QCOMPARE(resolved(index, u"c/Note"_s, u"b/Other.md"_s), u"b/c/Note.md"_s); // relative to b/
        QCOMPARE(resolved(index, u"a/Note"_s, u"b/Other.md"_s), u"a/Note.md"_s);   // exact path
    }

    void shortestThenAlphabetical()
    {
        VaultIndex index;
        index.update(u"zz/Note.md"_s, u"x"_s);
        index.update(u"aa/Note.md"_s, u"x"_s);
        index.update(u"long/path/Note.md"_s, u"x"_s);
        QCOMPARE(resolved(index, u"Note"_s), u"aa/Note.md"_s);
        index.remove(u"aa/Note.md"_s);
        QCOMPARE(resolved(index, u"Note"_s), u"zz/Note.md"_s);
    }

    void attachmentsVersusNotes()
    {
        VaultIndex index;
        index.update(u"img/photo.png"_s, QString());
        index.update(u"notes/photo.png.md"_s, u"note named like a file"_s);
        index.update(u"Dr. Smith.md"_s, u"x"_s);
        index.update(u"files/Dr. Smith"_s, QString());                 // an attachment without a real extension
        QCOMPARE(resolved(index, u"photo.png"_s), u"img/photo.png"_s); // attachments first
        QCOMPARE(resolved(index, u"Dr. Smith"_s), u"Dr. Smith.md"_s);  // ". Smith" is no extension
        QCOMPARE(resolved(index, u"files/Dr. Smith"_s), u"files/Dr. Smith"_s);
        // An exact path (+ .md) at the vault root still wins over everything else.
        index.update(u"photo.png.md"_s, u"root note"_s);
        QCOMPARE(resolved(index, u"photo.png"_s), u"photo.png.md"_s);
        index.remove(u"photo.png.md"_s);
        QVERIFY(index.contains(u"img/photo.png"_s));
        QCOMPARE(index.attachments(), QList<VaultPath>({u"files/Dr. Smith"_s, u"img/photo.png"_s}));
        QCOMPARE(index.note(u"img/photo.png"_s), nullptr);
    }

    void exactSpellingWinsAmongCaseVariants()
    {
        // Case-sensitive file systems allow "A.md" next to "a.md": the exact spelling wins, for
        // bare names as for paths, at the root and in the linking note's folder.
        VaultIndex index;
        index.update(u"A.md"_s, u"x"_s);
        index.update(u"a.md"_s, u"y"_s);
        index.update(u"x/Ż.md"_s, u"x"_s);
        index.update(u"x/ż.md"_s, u"y"_s);
        QCOMPARE(resolved(index, u"a"_s), u"a.md"_s);
        QCOMPARE(resolved(index, u"A"_s), u"A.md"_s);
        QCOMPARE(resolved(index, u"a.md"_s), u"a.md"_s);
        QCOMPARE(resolved(index, u"ż"_s, u"x/N.md"_s), u"x/ż.md"_s);
        QCOMPARE(resolved(index, u"Ż"_s, u"x/N.md"_s), u"x/Ż.md"_s);
        QCOMPARE(resolved(index, u"x/ż"_s), u"x/ż.md"_s);
        // Another case: shortest, then code units.
        QCOMPARE(resolved(index, u"Ż"_s), u"x/Ż.md"_s);
        index.update(u"N.md"_s, u"[[a]] [[A]]"_s);
        const QList<ResolvedLink> links = index.outgoingLinks(u"N.md"_s);
        QCOMPARE(links[0].target, u"a.md"_s);
        QCOMPARE(links[1].target, u"A.md"_s);
    }

    void notesBeforeExtensionlessAttachments()
    {
        // "Dom" is a page name (no real extension): a note wins over an attachment named "Dom".
        VaultIndex index;
        index.update(u"files/Dom"_s, QString());
        index.update(u"files/Dom.md"_s, u"x"_s);
        QCOMPARE(resolved(index, u"files/Dom"_s), u"files/Dom.md"_s);
        QCOMPARE(resolved(index, u"Dom"_s, u"files/N.md"_s), u"files/Dom.md"_s);
        index.remove(u"files/Dom.md"_s);
        QCOMPARE(resolved(index, u"files/Dom"_s), u"files/Dom"_s);
    }

    void dottedPageNames()
    {
        // Dates and versions in page names are no file extensions: such links are pages.
        VaultIndex index;
        index.update(u"A.md"_s, u"[[Spotkanie 14.10.2026]] [[v1.2]] [[zdjęcie.png]] [[Dr. Who]]"_s);
        QStringList names;
        for (const MissingPage &m : index.missingPages())
            names.append(m.name);
        names.sort();
        QCOMPARE(names, QStringList({u"Dr. Who"_s, u"Spotkanie 14.10.2026"_s, u"v1.2"_s}));
        const GraphData g = index.graph(false, true);
        QCOMPARE(g.nodes.size(), 4); // A + three missing pages, no node for the image
        index.update(u"Spotkania/Spotkanie 14.10.2026.md"_s, u"x"_s);
        index.update(u"v1.2.md"_s, u"x"_s);
        QCOMPARE(index.outgoingLinks(u"A.md"_s)[0].target, u"Spotkania/Spotkanie 14.10.2026.md"_s);
        QCOMPARE(index.outgoingLinks(u"A.md"_s)[1].target, u"v1.2.md"_s);
        QCOMPARE(index.missingPages().size(), 1);
    }

    void percentEncodedMarkdownTargets()
    {
        // The parser decodes Markdown link targets; '#' and '|' in them belong to the file name.
        VaultIndex index;
        index.update(u"Języki/C#.md"_s, u"x"_s);
        index.update(u"A|B.md"_s, u"x"_s);
        index.update(u"assets/wykres #1.png"_s, QString());
        index.update(
                u"N.md"_s,
                u"[nuty](Języki/C%23.md) [ab](A%7CB.md#Sekcja) ![](assets/wykres%20%231.png) [[C#]] [[Brak%23]]"_s);
        const QList<ResolvedLink> links = index.outgoingLinks(u"N.md"_s);
        QCOMPARE(links.size(), 5);
        QCOMPARE(links[0].target, u"Języki/C#.md"_s);
        QCOMPARE(links[1].target, u"A|B.md"_s);
        QCOMPARE(links[1].link.heading, u"Sekcja"_s);
        QCOMPARE(links[2].target, u"assets/wykres #1.png"_s);
        QVERIFY(links[3].target.isEmpty()); // [[C#]] is page "C", heading ""
        QCOMPARE(index.backlinks(u"Języki/C#.md"_s).size(), 1);
        // Incremental: removing and re-adding the target relinks the decoded link.
        index.remove(u"Języki/C#.md"_s);
        QVERIFY(index.outgoingLinks(u"N.md"_s)[0].target.isEmpty());
        index.update(u"Języki/C#.md"_s, u"y"_s);
        QCOMPARE(index.outgoingLinks(u"N.md"_s)[0].target, u"Języki/C#.md"_s);
        // An alias with '#' is reached by such a link too.
        index.update(u"Q.md"_s, u"[q](F%23)"_s);
        index.update(u"Muzyka/Fis.md"_s, u"---\naliases: [\"F#\"]\n---\nx"_s);
        QCOMPARE(index.outgoingLinks(u"Q.md"_s).first().target, u"Muzyka/Fis.md"_s);
    }

    void markdownExtensionVariants()
    {
        VaultIndex index;
        index.update(u"Old.markdown"_s, u"x"_s);
        QCOMPARE(resolved(index, u"Old"_s), u"Old.markdown"_s);
        QCOMPARE(resolved(index, u"old.markdown"_s), u"Old.markdown"_s);
    }

    void nfdPathsResolve()
    {
        VaultIndex index;
        const QString nfdPath = u"Notatki/Z\u0307o\u0301\u0142w.md"_s; // "Żółw" decomposed
        index.update(nfdPath, u"x"_s);
        QCOMPARE(resolved(index, u"żółw"_s), nfdPath);
        QCOMPARE(resolved(index, u"Notatki/Żółw"_s), nfdPath);
    }

    // ---- incremental maintenance ------------------------------------------------------------

    void linkResolvesWhenPageAppears()
    {
        VaultIndex index;
        index.update(u"A.md"_s, u"- see [[Nowa Strona]] and [[nowa strona#Sekcja]]"_s);
        QCOMPARE(index.missingPages().size(), 1);
        QCOMPARE(index.missingPages().first().name, u"Nowa Strona"_s);
        QVERIFY(index.backlinks(u"folder/Nowa Strona.md"_s).isEmpty());

        index.update(u"folder/Nowa Strona.md"_s, u"hello"_s);
        QCOMPARE(backlinkSources(index, u"folder/Nowa Strona.md"_s), QStringList({u"A.md"_s, u"A.md"_s}));
        QVERIFY(index.missingPages().isEmpty());
        QCOMPARE(index.linksTo(u"folder/Nowa Strona.md"_s), QList<VaultPath>{u"A.md"_s});

        index.remove(u"folder/Nowa Strona.md"_s);
        QVERIFY(index.backlinks(u"folder/Nowa Strona.md"_s).isEmpty());
        QCOMPARE(index.missingPages().size(), 1);
    }

    void linkMovesToBetterTarget()
    {
        VaultIndex index;
        index.update(u"src.md"_s, u"[[Note]]"_s);
        index.update(u"deep/er/Note.md"_s, u"x"_s);
        QCOMPARE(index.outgoingLinks(u"src.md"_s).first().target, u"deep/er/Note.md"_s);
        index.update(u"a/Note.md"_s, u"x"_s); // shorter path appears later
        QCOMPARE(index.outgoingLinks(u"src.md"_s).first().target, u"a/Note.md"_s);
        QCOMPARE(backlinkSources(index, u"a/Note.md"_s), QStringList{u"src.md"_s});
        QVERIFY(index.backlinks(u"deep/er/Note.md"_s).isEmpty());
        index.update(u"Note.md"_s, u"x"_s); // the vault root wins
        QCOMPARE(index.outgoingLinks(u"src.md"_s).first().target, u"Note.md"_s);
    }

    void aliasChangesRelink()
    {
        VaultIndex index;
        index.update(u"A.md"_s, u"- [[Szef]]"_s);
        index.update(u"Ludzie/Ania.md"_s, u"---\naliases: [Szef]\n---\nAnia"_s);
        QCOMPARE(backlinkSources(index, u"Ludzie/Ania.md"_s), QStringList{u"A.md"_s});
        index.update(u"Ludzie/Ania.md"_s, u"---\naliases: [Szefowa]\n---\nAnia"_s);
        QVERIFY(index.backlinks(u"Ludzie/Ania.md"_s).isEmpty());
        QCOMPARE(index.missingPages().first().name, u"Szef"_s);
        index.update(u"Ludzie/Ania.md"_s, u"alias:: Szef, Szefowa\n\nAnia"_s);
        QCOMPARE(backlinkSources(index, u"Ludzie/Ania.md"_s), QStringList{u"A.md"_s});
        // A real page of that name beats the alias.
        index.update(u"Szef.md"_s, u"x"_s);
        QCOMPARE(resolved(index, u"Szef"_s), u"Szef.md"_s);
        QVERIFY(index.backlinks(u"Ludzie/Ania.md"_s).isEmpty());
    }

    void attachmentAppearsLater()
    {
        VaultIndex index;
        index.update(u"Note.md"_s, u"![[zdjęcie.JPG]] and ![](assets/zdjęcie.jpg)"_s);
        QVERIFY(index.missingPages().isEmpty()); // missing attachments are not pages
        index.update(u"assets/zdjęcie.jpg"_s, QString());
        const QList<ResolvedLink> links = index.outgoingLinks(u"Note.md"_s);
        QCOMPARE(links.size(), 2);
        QCOMPARE(links[0].target, u"assets/zdjęcie.jpg"_s);
        QCOMPARE(links[1].target, u"assets/zdjęcie.jpg"_s);
        QCOMPARE(index.linksTo(u"assets/zdjęcie.jpg"_s), QList<VaultPath>{u"Note.md"_s});
        index.remove(u"assets/zdjęcie.jpg"_s);
        QVERIFY(index.outgoingLinks(u"Note.md"_s).first().target.isEmpty());
        QVERIFY(!index.contains(u"assets/zdjęcie.jpg"_s));
    }

    void renameKeepsLinksByName()
    {
        VaultIndex index;
        index.update(u"A.md"_s, u"[[Plan]] ((blk))"_s);
        index.update(u"Plan.md"_s, u"Treść ^blk"_s);
        QCOMPARE(backlinkSources(index, u"Plan.md"_s), QStringList({u"A.md"_s, u"A.md"_s}));
        const int before = index.version();
        index.rename(u"Plan.md"_s, u"Archiwum/Plan.md"_s, u"Treść ^blk"_s);
        QVERIFY(index.version() > before);
        QVERIFY(!index.contains(u"Plan.md"_s));
        QCOMPARE(backlinkSources(index, u"Archiwum/Plan.md"_s), QStringList({u"A.md"_s, u"A.md"_s}));
        QCOMPARE(index.blockById(u"blk"_s)->path, u"Archiwum/Plan.md"_s);
        // A rename to another name leaves the link unresolved.
        index.rename(u"Archiwum/Plan.md"_s, u"Archiwum/Projekt.md"_s, u"Treść ^blk"_s);
        QCOMPARE(index.missingPages().first().name, u"Plan"_s);
        QCOMPARE(index.linksTo(u"Archiwum/Projekt.md"_s), QList<VaultPath>{});
        // Block references follow the block, wherever it lives.
        QCOMPARE(backlinkSources(index, u"Archiwum/Projekt.md"_s), QStringList{u"A.md"_s});
    }

    void renameAttachment()
    {
        VaultIndex index;
        index.update(u"N.md"_s, u"![[a.png]] ![[b.png]]"_s);
        index.update(u"a.png"_s, QString());
        index.rename(u"a.png"_s, u"img/b.png"_s, QString());
        const QList<ResolvedLink> links = index.outgoingLinks(u"N.md"_s);
        QVERIFY(links[0].target.isEmpty());
        QCOMPARE(links[1].target, u"img/b.png"_s);
        QCOMPARE(index.attachments(), QList<VaultPath>{u"img/b.png"_s});
    }

    void selfLinks()
    {
        VaultIndex index;
        index.update(u"P.md"_s, u"Zobacz [[#Sekcja]], [[P]] i [[#^x]]\n\nTekst ^x"_s);
        QCOMPARE(resolved(index, QString(), u"P.md"_s), u"P.md"_s);
        QVERIFY(index.backlinks(u"P.md"_s).isEmpty());                   // self excluded
        QCOMPARE(index.linksTo(u"P.md"_s), QList<VaultPath>{u"P.md"_s}); // [[P]] links by name
        index.update(u"P.md"_s, u"Zobacz [[#Sekcja]]"_s);
        QVERIFY(index.linksTo(u"P.md"_s).isEmpty()); // same-page links do not count
    }

    void externalLinksAreNotFiles()
    {
        VaultIndex index;
        index.update(u"N.md"_s, u"[x](https://example.com/a.md) <mailto:a@b.pl> https://x.pl [d](C:/docs/a.md)"_s);
        const QList<ResolvedLink> links = index.outgoingLinks(u"N.md"_s);
        QCOMPARE(links.size(), 1); // only the Windows path is a (missing) file link
        QCOMPARE(links.first().link.target, u"C:/docs/a.md"_s);
        QVERIFY(index.missingPages().size() == 1);
    }

    void frontmatterLinksCount()
    {
        VaultIndex index;
        index.update(u"Ania.md"_s, u"x"_s);
        index.update(u"B.md"_s, u"---\nrelated: \"[[Ania]]\"\n---\ntext"_s);
        QCOMPARE(backlinkSources(index, u"Ania.md"_s), QStringList{u"B.md"_s});
    }
};

QTEST_GUILESS_MAIN(TestIndexResolve)
#include "tst_index_resolve.moc"
