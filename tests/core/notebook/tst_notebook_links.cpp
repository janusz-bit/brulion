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

    void copiedLinks_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("fromPath");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("path");
        QTest::newRow("tag") << u"tag:dom"_s << QString() << u"#dom"_s << QString();
        QTest::newRow("tag with spaces") << u"tag:wa%C5%BCne%20sprawy"_s << QString() << u"#[[ważne sprawy]]"_s
                                         << QString();
        QTest::newRow("block") << u"block:abc123"_s << QString() << u"((abc123))"_s << QString();
        QTest::newRow("web address") << u"ext:https://example.org/a?b=1"_s << QString()
                                     << u"https://example.org/a?b=1"_s << QString();
        // The page by its shortest target (two pages are called "Dom"), the heading kept.
        QTest::newRow("page with heading") << u"page:Dom#Plan%20pi%C4%99tra"_s << u"Inne.md"_s
                                           << u"[[Projekty/Dom#Plan piętra]]"_s << u"Projekty/Dom.md"_s;
        QTest::newRow("same page heading")
                << u"page:#Zakupy"_s << u"Projekty/Dom.md"_s << u"[[Projekty/Dom#Zakupy]]"_s << u"Projekty/Dom.md"_s;
        QTest::newRow("daily note") << u"page:2026-10-22"_s << u"Inne.md"_s << u"[[2026-10-22]]"_s
                                    << u"journals/2026-10-22.md"_s;
        QTest::newRow("no such page") << u"page:Nowa%20strona"_s << u"Inne.md"_s << u"[[Nowa strona]]"_s << QString();
        QTest::newRow("no such page, heading") << u"page:Nowa#Cel"_s << u"Inne.md"_s << u"[[Nowa#Cel]]"_s << QString();
        QTest::newRow("date chip") << u"date:1-2"_s << QString() << QString() << QString();
        QTest::newRow("no scheme") << u"Dom"_s << QString() << QString() << QString();
    }

    void copiedLinks()
    {
        QFETCH(QString, url);
        QFETCH(QString, fromPath);
        QFETCH(QString, text);
        QFETCH(QString, path);
        const VaultConfig config;
        const auto resolve = [](const QString &target, const QString &) {
            if (target == u"Dom")
                return u"Projekty/Dom.md"_s;
            if (target == u"2026-10-22")
                return u"journals/2026-10-22.md"_s;
            return QString();
        };
        const auto shortest = [](const VaultPath &p) {
            return p == u"Projekty/Dom.md" ? u"Projekty/Dom"_s : QString();
        };
        const CopiedLink copied = notebook::copiedLink(url, fromPath, config, resolve, shortest);
        QCOMPARE(copied.text, text);
        QCOMPARE(copied.path, path);
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

    // A path part of a link (part:, docs/SYNTAX.md §2 "Path parts"): what it opens, as a link
    // pasted anywhere reaches it — a page by its shortest target, a folder by its path for a
    // `path:` filter, a new page as written.
    void copiedPartLinks_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("folder");
        QTest::newRow("a page") << u"part:1:Firma%2FNowa"_s << u"[[Firma]]"_s << u"Firma.md"_s << QString();
        QTest::newRow("a page named like another")
                << u"part:2:Archiwum%2FFirma%2FX"_s << u"[[Archiwum/Firma]]"_s << u"Archiwum/Firma.md"_s << QString();
        QTest::newRow("a folder") << u"part:2:Klienci%2FFirma%2FAdam"_s << u"Klienci/Firma/"_s << u"Klienci/Firma"_s
                                  << u"Klienci/Firma"_s;
        QTest::newRow("a Polish folder") << u"part:1:Zwierz%C4%99ta%2F%C5%BB%C3%B3%C5%82w"_s << u"Zwierzęta/"_s
                                         << u"Zwierzęta"_s << u"Zwierzęta"_s;
        QTest::newRow("a new page") << u"part:1:Nowa%2FStrona"_s << u"[[Nowa]]"_s << QString() << QString();
        QTest::newRow("a new deeper page")
                << u"part:2:Nowa%2FG%C5%82%C4%99boka%2FStrona"_s << u"[[Nowa/Głęboka]]"_s << QString() << QString();
        QTest::newRow("part 0") << u"part:0:Firma%2FAdam"_s << QString() << QString() << QString();
        QTest::newRow("the last part") << u"part:2:Firma%2FAdam"_s << QString() << QString() << QString();
        QTest::newRow("no path") << u"part:1:Firma"_s << QString() << QString() << QString();
        QTest::newRow("no number") << u"part::Firma%2FAdam"_s << QString() << QString() << QString();
    }
    void copiedPartLinks()
    {
        QFETCH(QString, url);
        QFETCH(QString, text);
        QFETCH(QString, path);
        QFETCH(QString, folder);
        VaultIndex index;
        for (const QString &p : {u"Firma.md"_s, u"Klienci/Firma/Adam.md"_s, u"Archiwum/Firma.md"_s,
                                 u"Zwierzęta/Gady/Żółw.md"_s})
            index.update(p, u"- notatka\n"_s);
        const auto resolve = [&index](const QString &target, const QString &from) {
            return index.resolveLink(target, from).value_or(QString());
        };
        const auto shortest = [&index](const VaultPath &p) { return index.linkTarget(p).value_or(QString()); };
        const auto part = [&index](const QString &target, int k, const QString &from) {
            return index.resolveLinkPart(target, k, from);
        };
        const CopiedLink copied = notebook::copiedLink(url, u"Notatka.md"_s, index.config(), resolve, shortest, part);
        QCOMPARE(copied.text, text);
        QCOMPARE(copied.path, path);
        QCOMPARE(copied.folder, folder);
        // Without the part resolver nothing is copied.
        QCOMPARE(notebook::copiedLink(url, u"Notatka.md"_s, index.config(), resolve, shortest).text, QString());
    }
};

QTEST_GUILESS_MAIN(TestNotebookLinks)
#include "tst_notebook_links.moc"
