// The links Brulion copies (notebook/links.h), as `brulion-cli block link` gives them: a block's
// ((id)), [[Page#^id]] and !((id)) (the id added when missing, Polish text kept), a heading's
// [[Page#Heading]] / ![[Page#Heading]] — or, when its name is not unique, the links through its id
// ![[Page#^id]], as the editor copies them (the editor's own parity check: tests/app/editor
// tst_editor_controller copiedLinksMatchTheCommandLine) — and the page part of a link.
#include "index/vaultindex.h"
#include "notebook/links.h"
#include "util/paths.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::notebook;

class TestNotebookLinks : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void blockLinks_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("line");
        // "ID": the block's id. "" = no links (a line that cannot carry an id).
        QTest::addColumn<QString>("wikiLink");
        QTest::addColumn<QString>("embed");
        QTest::addColumn<QString>("headingLink");
        QTest::newRow("block with an id")
                << u"# Dom\n- farba ^abc123\n"_s << 1 << u"[[Dom#^abc123]]"_s << u"!((abc123))"_s << QString();
        QTest::newRow("block without one")
                << u"# Dom\n- żółta farba\n"_s << 1 << u"[[Dom#^ID]]"_s << u"!((ID))"_s << QString();
        QTest::newRow("unique heading") << u"# Dom\n## Zakupy\n- farba\n## Plan\n"_s << 1 << u"[[Dom#^ID]]"_s
                                        << u"![[Dom#Zakupy]]"_s << u"[[Dom#Zakupy]]"_s;
        QTest::newRow("Logseq heading in a list item")
                << u"- ## Lista rzeczy\n\t- szczoteczka\n"_s << 0 << u"[[Dom#^ID]]"_s << u"![[Dom#Lista rzeczy]]"_s
                << u"[[Dom#Lista rzeczy]]"_s;
        QTest::newRow("repeated heading") << u"## Zakupy\n- mleko\n## Zakupy\n- chleb\n"_s << 2 << u"[[Dom#^ID]]"_s
                                          << u"![[Dom#^ID]]"_s << QString();
        QTest::newRow("same name, other parents")
                << u"# Projekt\n## Zadania\n# Archiwum\n## Zadania\n"_s << 3 << u"[[Dom#^ID]]"_s
                << u"![[Dom#Archiwum#Zadania]]"_s << u"[[Dom#Archiwum#Zadania]]"_s;
        QTest::newRow("blank line after a block")
                << u"- a\n\n- b\n"_s << 1 << u"[[Dom#^ID]]"_s << u"!((ID))"_s << QString();
        QTest::newRow("frontmatter") << u"---\ntytuł: Dom\n---\n- a\n"_s << 1 << QString() << QString() << QString();
    }
    void blockLinks()
    {
        QFETCH(QString, content);
        QFETCH(int, line);
        QFETCH(QString, wikiLink);
        QFETCH(QString, embed);
        QFETCH(QString, headingLink);
        const auto links = notebook::blockLinks(content, line, u"Dom"_s);
        if (wikiLink.isEmpty()) {
            QVERIFY(!links);
            return;
        }
        QVERIFY(links);
        QCOMPARE(links->id.size(), 6);
        QCOMPARE(links->added, !content.contains(u'^' + links->id));
        QCOMPARE(links->content.count(u'^' + links->id), 1);
        QCOMPARE(links->line, content.startsWith(u"- a\n\n"_s) ? 0 : line); // the block's first line
        QCOMPARE(links->ref, u"(("_s + links->id + u"))"_s);
        QCOMPARE(links->wikiLink, QString(wikiLink).replace(u"ID"_s, links->id));
        QCOMPARE(links->embed, QString(embed).replace(u"ID"_s, links->id));
        QCOMPARE(links->headingLink, headingLink);
        // Asked again on the text with the id: the same links, nothing added.
        const auto again = notebook::blockLinks(links->content, line, u"Dom"_s);
        QVERIFY(again);
        QVERIFY(!again->added);
        QCOMPARE(again->id, links->id);
        QCOMPARE(again->embed, links->embed);
    }

    void takenIdsAreAvoided()
    {
        QStringList asked;
        const auto links = notebook::blockLinks(u"- a\n"_s, 0, u"Dom"_s, [&](const QString &id) {
            asked.append(id);
            return asked.size() < 3; // the first two are taken elsewhere in the notebook
        });
        QVERIFY(links);
        QCOMPARE(asked.size(), 3);
        QCOMPARE(links->id, asked.last());
    }

    void pageLinkTarget()
    {
        const VaultConfig config;
        const auto noShortest = [](const VaultPath &) { return QString(); };
        const auto nowhere = [](const QString &) { return QString(); };
        // The index's shortest target wins.
        const auto shortest = [](const VaultPath &) { return u"Projekty/Dom"_s; };
        QCOMPARE(notebook::pageLinkTarget(u"Archiwum/Projekty/Dom.md"_s, config, shortest, nowhere), u"Projekty/Dom"_s);
        // Not indexed yet (a new page): the title, or the path when the title leads elsewhere.
        QCOMPARE(notebook::pageLinkTarget(u"Projekty/Dom.md"_s, config, noShortest, nowhere), u"Dom"_s);
        const auto elsewhere = [](const QString &) { return u"Dom.md"_s; };
        QCOMPARE(notebook::pageLinkTarget(u"Projekty/Dom.md"_s, config, noShortest, elsewhere), u"Projekty/Dom"_s);
        const auto here = [](const QString &) { return u"Projekty/Dom.md"_s; };
        QCOMPARE(notebook::pageLinkTarget(u"Projekty/Dom.md"_s, config, noShortest, here), u"Dom"_s);
        QCOMPARE(notebook::pageLinkTarget(u"Projekty/Dom.md"_s, config, {}, {}), u"Dom"_s);
        // A daily note by its name, whatever the index says.
        QCOMPARE(notebook::pageLinkTarget(u"journals/2026-10-22.md"_s, config, shortest, elsewhere), u"2026-10-22"_s);
        QCOMPARE(notebook::pageLinkTarget(QString(), config, shortest, nowhere), QString());
    }

    // With an index: VaultIndex::linkTarget(), the shortest target that tells same-named pages
    // in different folders apart — the text every copy-link command writes.
    void pageLinkTargetWithIndex_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("target");
        QTest::newRow("unique title") << u"Zakupy/Spożywcze.md"_s << u"Spożywcze"_s;
        QTest::newRow("the shortest of two Dom") << u"Dom.md"_s << u"Dom"_s;
        QTest::newRow("the other Dom by its folder") << u"Archiwum/Projekty/Dom.md"_s << u"Projekty/Dom"_s;
        QTest::newRow("Żółw twice, deeper") << u"Zwierzęta/Gady/Żółw.md"_s << u"Gady/Żółw"_s;
        QTest::newRow("Żółw twice, shallower") << u"Ogród/Żółw.md"_s << u"Żółw"_s;
        QTest::newRow("a daily note") << u"journals/2026-10-22.md"_s << u"2026-10-22"_s;
        QTest::newRow("a daily note named twice") << u"journals/2026-10-23.md"_s << u"2026-10-23"_s;
        QTest::newRow("a page not written yet") << u"Nowe/Plan wakacji.md"_s << u"Plan wakacji"_s;
        QTest::newRow("a new page named like another") << u"Nowe/Dom.md"_s << u"Nowe/Dom"_s;
    }
    void pageLinkTargetWithIndex()
    {
        QFETCH(QString, path);
        QFETCH(QString, target);
        VaultIndex index;
        for (const QString &p :
             {u"Dom.md"_s, u"Archiwum/Projekty/Dom.md"_s, u"Zakupy/Spożywcze.md"_s, u"Zwierzęta/Gady/Żółw.md"_s,
              u"Ogród/Żółw.md"_s, u"journals/2026-10-22.md"_s, u"journals/2026-10-23.md"_s, u"Stare/2026-10-23.md"_s})
            index.update(p, u"- notatka\n"_s);
        QCOMPARE(notebook::pageLinkTarget(path, index.config(), &index), target);
        // The target reaches the page (once it exists) — a daily note keeps its file name, which
        // reads as the date, even when another page has that name too.
        if (index.contains(path) && !path.startsWith(u"journals/"_s))
            QCOMPARE(index.resolveLink(notebook::pageLinkTarget(path, index.config(), &index)).value_or(QString()),
                     path);
        QCOMPARE(notebook::pageLinkTarget(path, index.config(), nullptr), paths::pageTitle(path));
    }
};

QTEST_GUILESS_MAIN(TestNotebookLinks)
#include "tst_notebook_links.moc"
