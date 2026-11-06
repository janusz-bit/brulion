// What "Clean up" offers (notebook/cleanup.h): notes and attachments nothing refers to — every kind
// of reference keeps a file off the list, and what nobody links to on purpose is never on it.
#include "index/vaultindex.h"
#include "notebook/cleanup.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::notebook;

namespace {
using Files = QList<QPair<QString, QString>>;

QStringList listed(const Files &files, const VaultConfig &config = {})
{
    VaultIndex index;
    index.setConfig(config);
    for (const auto &[path, content] : files)
        index.update(path, content);
    QStringList out;
    for (const UnreferencedFile &file : unreferencedFiles(index, config))
        out.append(file.path);
    return out;
}
} // namespace

class TestNotebookCleanup : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void references_data()
    {
        QTest::addColumn<QString>("linking"); // the text of Inna.md, which is linked itself
        QTest::addColumn<QString>("file");    // the file it may refer to
        QTest::addColumn<bool>("isListed");

        const QString page = u"Żółw domowy.md"_s;
        QTest::newRow("no reference") << u"- nic\n"_s << page << true;
        QTest::newRow("plain mention of a page") << u"- Żółw domowy\n"_s << page << true;
        QTest::newRow("wiki link") << u"- [[Żółw domowy]]\n"_s << page << false;
        QTest::newRow("wiki link, letter case") << u"- [[żółw DOMOWY|on]]\n"_s << page << false;
        QTest::newRow("embed") << u"- ![[Żółw domowy]]\n"_s << page << false;
        QTest::newRow("Markdown link") << u"- [on](%C5%BB%C3%B3%C5%82w%20domowy.md)\n"_s << page << false;
        QTest::newRow("tag") << u"- #[[Żółw domowy]]\n"_s << page << false;
        QTest::newRow("page tag") << u"---\ntags: [Żółw domowy]\n---\n- a\n"_s << page << false;
        QTest::newRow("query ref") << u"```query\nref:[[Żółw domowy]]\n```\n"_s << page << false;
        QTest::newRow("query page") << u"```query\npage:\"Żółw domowy\"\n```\n"_s << page << false;
        QTest::newRow("query tag") << u"```query\ntag:[[Żółw domowy]]\n```\n"_s << page << false;
        QTest::newRow("link in frontmatter") << u"---\nrodzic: \"[[Żółw domowy]]\"\n---\n- a\n"_s << page << false;
        QTest::newRow("link in a property") << u"rodzic:: [[Żółw domowy]]\n\n- a\n"_s << page << false;
        QTest::newRow("a link to another page") << u"- [[Żółw]]\n"_s << page << true;

        const QString picture = u"assets/Zdjęcie żółwia.png"_s;
        QTest::newRow("attachment: no reference") << u"- nic\n"_s << picture << true;
        QTest::newRow("attachment: embed") << u"- ![[Zdjęcie żółwia.png]]\n"_s << picture << false;
        QTest::newRow("attachment: link") << u"- [[assets/Zdjęcie żółwia.png]]\n"_s << picture << false;
        QTest::newRow("attachment: Markdown, encoded")
                << u"- ![](assets/Zdj%C4%99cie%20%C5%BC%C3%B3%C5%82wia.png)\n"_s << picture << false;
        QTest::newRow("attachment: raw HTML")
                << u"<img src=\"assets/Zdjęcie żółwia.png\" width=\"100\">\n"_s << picture << false;
        QTest::newRow("attachment: raw HTML, encoded, other case")
                << u"<img src='assets/zdj%C4%99cie%20%C5%BC%C3%B3%C5%82wia.PNG'>\n"_s << picture << false;
        QTest::newRow("attachment: in frontmatter") << u"---\ncover: assets/Zdjęcie żółwia.png\n---\n- a\n"_s << picture << false;
        QTest::newRow("attachment: name inside a longer word") << u"- MojeZdjęcie żółwia.png\n"_s << picture << true;
        QTest::newRow("attachment: another extension") << u"- Zdjęcie żółwia.pngx\n"_s << picture << true;

        const QString noExtension = u"LICENSE"_s;
        QTest::newRow("attachment without an extension: a word is no reference") << u"- LICENSE\n"_s << noExtension << true;
        QTest::newRow("attachment without an extension: a link") << u"- [[LICENSE]]\n"_s << noExtension << false;
    }
    void references()
    {
        QFETCH(QString, linking);
        QFETCH(QString, file);
        QFETCH(bool, isListed);
        const QStringList out = listed({ { u"Strona główna.md"_s, u"- [[Inna]]\n"_s },
                                         { u"Inna.md"_s, linking },
                                         { file, file.endsWith(u".md"_s) ? u"- treść\n"_s : QString() } });
        QCOMPARE(out.contains(file), isListed);
        QVERIFY(!out.contains(u"Inna.md"_s));
    }

    void ownReferencesDoNotCount()
    {
        const QString own = u"# Żółw\n- [[Żółw]] ![[Żółw]] #Żółw ((abc))\n- blok ^abc\n```query\nref:[[Żółw]]\n```\n"_s;
        QCOMPARE(listed({ { u"Żółw.md"_s, own } }), QStringList{ u"Żółw.md"_s });
    }

    void blockReference()
    {
        const Files files{ { u"Bloki.md"_s, u"- blok ^abc123\n"_s }, { u"Inna.md"_s, u"- ((abc123))\n"_s } };
        QCOMPARE(listed(files), QStringList{ u"Inna.md"_s });
    }

    void aReferenceFromAnUnreferencedNoteCounts()
    {
        const Files files{ { u"A.md"_s, u"- [[B]]\n"_s }, { u"B.md"_s, u"- [[C]]\n"_s }, { u"C.md"_s, u"- c\n"_s } };
        QCOMPARE(listed(files), QStringList{ u"A.md"_s });
    }

    void aReferenceFromAJournalOrTemplateCounts()
    {
        const Files files{ { u"journals/2026-10-14.md"_s, u"- [[A]]\n"_s },
                           { u"templates/Spotkanie.md"_s, u"- ![[logo.png]]\n"_s },
                           { u"A.md"_s, u"- a\n"_s },
                           { u"assets/logo.png"_s, QString() } };
        QVERIFY(listed(files).isEmpty());
    }

    void neverListed()
    {
        VaultConfig config;
        config.templatesFolder = u"Szablony"_s;
        config.journalTemplate = u"Inne/dziennik.md"_s;
        const Files files{ { u"journals/2026-10-14.md"_s, u"- dziś\n"_s },
                           { u"Szablony/Spotkanie.md"_s, u"- szablon\n"_s },
                           { u"Szablony/logo.png"_s, QString() },
                           { u"Inne/dziennik.md"_s, u"- szablon\n"_s },
                           { u".sync_abc.db"_s, QString() },
                           { u".ukryte/plik.txt"_s, QString() },
                           { u"Szablony2/Notatka.md"_s, u"- nie szablon\n"_s },
                           { u"journals/Notatka w dziennikach.md"_s, u"- nie dziennik\n"_s } };
        QCOMPARE(listed(files, config),
                 (QStringList{ u"Szablony2/Notatka.md"_s, u"journals/Notatka w dziennikach.md"_s }));
    }

    void notesFirstEachSortedWithWhatDeletingWouldTake()
    {
        VaultIndex index;
        index.update(u"B.md"_s, u"- [ ] do zrobienia\n- [/] w toku\n- [>] odłożone\n- [x] zrobione\n- [-] anulowane\n"_s);
        index.update(u"A.md"_s, u"- spotkanie ⏰ 2026-11-05 10:00\n- [x] zrobione ⏰ 2026-11-05 10:00\n"_s);
        index.update(u"z.png"_s, QString());
        index.update(u"assets/a.pdf"_s, QString());
        const QList<UnreferencedFile> expected{
            { u"A.md"_s, false, 0, 1 },
            { u"B.md"_s, false, 3, 0 },
            { u"assets/a.pdf"_s, true, 0, 0 },
            { u"z.png"_s, true, 0, 0 },
        };
        QCOMPARE(unreferencedFiles(index, VaultConfig{}), expected);
    }

    void empty()
    {
        QVERIFY(listed({}).isEmpty());
    }

    void malformedNotes()
    {
        // Dots with nothing around them, an unclosed link and fence, a name at the text's edges.
        const Files files{ { u"A.md"_s, u"...\n[[\n```query\n.png\n"_s },
                           { u"B.md"_s, u"x.png"_s },
                           { u"x.png"_s, QString() },
                           { u"y.png"_s, QString() } };
        QCOMPARE(listed(files), (QStringList{ u"A.md"_s, u"B.md"_s, u"y.png"_s }));
    }
};

QTEST_GUILESS_MAIN(TestNotebookCleanup)
#include "tst_notebook_cleanup.moc"
