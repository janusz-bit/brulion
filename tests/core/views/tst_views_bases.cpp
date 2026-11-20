// Obsidian Bases (views/bases.h): the fixtures of tests/core/views/fixtures read, run over a small
// Polish notebook of books, and written back (order, sort) byte for byte elsewhere.
#include "views/bases.h"
#include "views/edits.h"

#include "markdown/frontmatter.h"

#include <QFile>
#include <QTest>

using namespace brulion;
using namespace brulion::views;
using namespace Qt::StringLiterals;

namespace {

const QDate Today(2026, 10, 14);

QString lines(const QStringList &l)
{
    return l.join(u'\n');
}

QString fixture(const QString &name)
{
    QFile f(QStringLiteral(BRULION_BASES_FIXTURES) + u'/' + name);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

void build(VaultIndex &index)
{
    index.update(u"Książki/Lalka.md"_s, lines({u"---"_s, u"autor: \"[[Bolesław Prus]]\""_s, u"cena: 39.9"_s,
                                               u"strony: 680"_s, u"stan: czytam"_s, u"okladka: \"[[lalka.jpg]]\""_s,
                                               u"tags: [książka]"_s, u"---"_s, u"Powieść."_s}));
    index.update(u"Książki/Quo vadis.md"_s,
                 lines({u"---"_s, u"autor: \"[[Henryk Sienkiewicz]]\""_s, u"cena: 25"_s, u"strony: 600"_s,
                        u"stan: do przeczytania"_s, u"tags: [książka]"_s, u"---"_s, u"Powieść."_s}));
    index.update(u"Książki/Ferdydurke.md"_s,
                 lines({u"---"_s, u"autor: \"[[Witold Gombrowicz]]\""_s, u"cena: 45"_s, u"strony: 300"_s,
                        u"stan: przeczytana"_s, u"tags: [książka/polska]"_s, u"---"_s, u"Powieść."_s}));
    index.update(u"Inne/Tygrys.md"_s, lines({u"---"_s, u"cena: 10"_s, u"tags: [książka]"_s, u"---"_s, u"Inna."_s}));
    index.update(u"Ludzie/Bolesław Prus.md"_s, u"Pisarz."_s);
    index.update(u"Książki/lalka.jpg"_s, QString());
    index.update(u"Książki/okładka.png"_s, QString());
    index.update(u"Czytelnia.md"_s, lines({u"```base"_s, u"views:"_s, u"  - type: table"_s, u"```"_s}));
}

ViewOptions options(const QString &current = u"Książki/Książki.base"_s)
{
    ViewOptions o;
    o.today = Today;
    o.now = QDateTime(Today, QTime(10, 0));
    o.currentPath = current;
    o.newPagesFolder = u"Nowe"_s;
    return o;
}

QStringList paths(const ViewResult &v)
{
    QStringList out;
    for (const Row &r : v.rows)
        out.append(r.path);
    return out;
}

QStringList labels(const ViewResult &v)
{
    QStringList out;
    for (const Column &c : v.columns)
        out.append(c.label);
    return out;
}

} // namespace

class TestViewsBases : public QObject {
    Q_OBJECT

private:
    VaultIndex m_index;
    Base m_books;

    ViewResult run(const Base &base, int view, const ViewOptions &o = options())
    {
        const auto r = runBase(base, view, m_index, o);
        if (const QString *error = std::get_if<QString>(&r)) {
            qWarning("base error: %s", qPrintable(*error));
            return {};
        }
        return std::get<ViewResult>(r);
    }

private Q_SLOTS:
    void initTestCase()
    {
        build(m_index);
        const auto parsed = parseBase(fixture(u"ksiazki.base"_s));
        QVERIFY(std::holds_alternative<Base>(parsed));
        m_books = std::get<Base>(parsed);
    }

    void readsTheBase()
    {
        QCOMPARE(m_books.views.size(), 5);
        QCOMPARE(m_books.views.at(0).kind, ViewKind::Table);
        QCOMPARE(m_books.views.at(1).kind, ViewKind::Gallery);
        QCOMPARE(m_books.views.at(2).kind, ViewKind::Board);
        QCOMPARE(m_books.views.at(3).kind, ViewKind::List);
        QCOMPARE(m_books.views.at(4).kind, ViewKind::Table);
        QVERIFY(m_books.views.at(4).notice.contains(u"Map"_s));
        QCOMPARE(m_books.views.at(0).name, u"Wszystkie"_s);
        QCOMPARE(m_books.views.at(0).sort.size(), 1);
        QVERIFY(m_books.views.at(0).sort.first().descending);
        QCOMPARE(m_books.views.at(2).groupOrder->size(), 3);
        QCOMPARE(m_books.views.at(3).limit, 1);
        QCOMPARE(m_books.displayNames.value(u"note.autor"_s), u"Autor"_s);
        QCOMPARE(m_books.displayNames.value(u"file.name"_s), u"Tytuł"_s);
        // A formula comes after those it reads.
        QStringList names;
        for (const auto &[name, e] : m_books.formulas)
            names.append(name);
        // (Formula names are canonical keys, as Dataview's fields: "cena_brutto" -> "cenabrutto".)
        const qsizetype brutto = names.indexOf(dataview::canonicalKey(u"cena_brutto"));
        QVERIFY(brutto >= 0 && brutto < names.indexOf(dataview::canonicalKey(u"na_strone")));
        QCOMPARE(names.size(), 3);
        QVERIFY2(m_books.problems.isEmpty(), qPrintable(m_books.problems.join(u'\n')));
        QCOMPARE(baseViewNamed(m_books, u"stan"_s), 2);
        QCOMPARE(baseViewNamed(m_books, u"nie ma"_s), 0);
        QCOMPARE(baseProperty(u"autor"_s), u"note.autor"_s);
        QCOMPARE(baseProperty(u"file.name"_s), u"file.name"_s);
    }

    void table()
    {
        const ViewResult v = run(m_books, 0);
        // In Książki/ with the tag (a nested tag too), by price, highest first.
        QCOMPARE(paths(v), QStringList({u"Książki/Ferdydurke.md"_s, u"Książki/Lalka.md"_s, u"Książki/Quo vadis.md"_s}));
        QCOMPARE(labels(v), QStringList({u"Tytuł"_s, u"Autor"_s, u"cena"_s, u"Cena brutto"_s, u"strony"_s}));
        const Row &lalka = v.rows.at(1);
        QCOMPARE(lalka.cells.at(0).markdown, u"[[Lalka]]"_s);
        QCOMPARE(lalka.cells.at(1).markdown, u"[[Bolesław Prus]]"_s);
        QCOMPARE(lalka.cells.at(2).value, QVariant(39.9));
        QCOMPARE(lalka.cells.at(3).markdown, u"49.077"_s);
        QVERIFY(v.columns.at(1).editable);
        QCOMPARE(v.columns.at(1).property, u"autor"_s);
        QVERIFY(!v.columns.at(3).editable);
        QCOMPARE(v.columns.at(2).summaryText, u"109.9"_s);
        QCOMPARE(v.columns.at(4).summaryText, u"526.7"_s); // the custom summary
        QCOMPARE(v.columns.at(2).sortKey, u"note.cena"_s);
        // New rows go where the filters say: the folder and the tag.
        QCOMPARE(v.newRow.folder, u"Książki"_s);
        QCOMPARE(v.newRow.tags, QStringList{u"książka"_s});
    }

    void cards()
    {
        const ViewResult v = run(m_books, 1);
        QCOMPARE(v.kind, ViewKind::Gallery);
        const auto lalka = std::ranges::find(v.rows, u"Książki/Lalka.md"_s, &Row::path);
        QVERIFY(lalka != v.rows.cend());
        QCOMPARE(lalka->cover, u"Książki/lalka.jpg"_s);
        QCOMPARE(lalka->coverKind, u"file"_s);
    }

    void kanban()
    {
        const ViewResult v = run(m_books, 2);
        QCOMPARE(v.kind, ViewKind::Board);
        QCOMPARE(v.groupKind, u"property"_s);
        QCOMPARE(v.groupProperty, u"stan"_s);
        QStringList groups;
        for (const Group &g : v.groups)
            groups.append(g.label + u'=' + QString::number(g.rows.size()));
        QCOMPARE(groups, QStringList({u"do przeczytania=1"_s, u"czytam=1"_s, u"przeczytana=1"_s}));
        const Row &quo = v.rows.at(v.groups.at(0).rows.first());
        QCOMPARE(quo.path, u"Książki/Quo vadis.md"_s);
        const std::optional<RowEdit> e = moveEdit(v, quo, 0, 1);
        QVERIFY(e);
        const EditOutcome r = applyRowEdit(m_index.content(quo.path), *e, Today);
        QVERIFY(r.content.contains(u"stan: czytam"_s));
        QVERIFY(newPageContent(v, 2).contains(u"stan: przeczytana"_s));
    }

    void listWithItsOwnFilterAndLimit()
    {
        const ViewResult v = run(m_books, 3);
        QCOMPARE(v.kind, ViewKind::List);
        QCOMPARE(paths(v), QStringList{u"Książki/Quo vadis.md"_s});
        QCOMPARE(v.total, 1);
        // The map: a table with a note.
        const ViewResult map = run(m_books, 4);
        QCOMPARE(map.kind, ViewKind::Table);
        QVERIFY(!map.notice.isEmpty());
    }

    void problemsAreShownNotFatal()
    {
        const auto parsed = parseBase(fixture(u"zle.base"_s));
        QVERIFY(std::holds_alternative<Base>(parsed));
        const Base bad = std::get<Base>(parsed);
        const QString problems = bad.problems.join(u'\n');
        QVERIFY2(problems.contains(u"Regular expressions"_s), qPrintable(problems));
        QVERIFY2(problems.contains(u"unknownFunction"_s), qPrintable(problems));
        QVERIFY2(problems.contains(u"“a”"_s) && problems.contains(u"“b”"_s) && problems.contains(u"“c”"_s),
                 qPrintable(problems));
        QVERIFY2(problems.contains(u"“d”"_s), qPrintable(problems));
        QCOMPARE(bad.formulas.size(), 1); // ok
        const ViewResult kanban = run(bad, 0, options(u"Zle.base"_s));
        QCOMPARE(kanban.kind, ViewKind::Table);
        QVERIFY(!kanban.notice.isEmpty());
        const ViewResult plugin = run(bad, 1, options(u"Zle.base"_s));
        QVERIFY(plugin.notice.contains(u"timeline"_s));
        QCOMPARE(plugin.rows.size(), 4); // the books (the tag); the statements that do not parse match nothing
        const auto tygrys = std::ranges::find(plugin.rows, u"Inne/Tygrys.md"_s, &Row::path);
        QVERIFY(tygrys != plugin.rows.cend());
        QCOMPARE(tygrys->cells.at(1).markdown, u"20"_s);
        QVERIFY(tygrys->cells.at(2).empty);
    }

    void everyFileWithoutFilters()
    {
        const Base all = std::get<Base>(parseBase(QString()));
        QCOMPARE(all.views.size(), 1);
        const ViewResult v = run(all, 0, options(u"Czytelnia.md"_s));
        // Notes and attachments, the page showing it included (a base does not list only itself).
        QVERIFY(paths(v).contains(u"Książki/okładka.png"_s));
        QVERIFY(paths(v).contains(u"Czytelnia.md"_s));
        QCOMPARE(v.rows.size(), 8); // six notes, two pictures
        // An attachment row: its file name, no properties.
        const Base files =
                std::get<Base>(parseBase(lines({u"filters: 'file.ext == \"png\"'"_s, u"views:"_s, u"  - type: table"_s,
                                                u"    order: [file.name, file.ext, cena]"_s})));
        const ViewResult png = run(files, 0);
        QCOMPARE(paths(png), QStringList{u"Książki/okładka.png"_s});
        QCOMPARE(png.rows.first().cells.at(1).markdown, u"png"_s);
        QVERIFY(png.rows.first().cells.at(2).empty);
    }

    void thisIsThePageShowingIt()
    {
        const Base b = std::get<Base>(
                parseBase(lines({u"filters: file.hasLink(this.file)"_s, u"views:"_s, u"  - type: list"_s})));
        const ViewResult v = run(b, 0, options(u"Ludzie/Bolesław Prus.md"_s));
        QCOMPARE(paths(v), QStringList{u"Książki/Lalka.md"_s});
        const Base folder = std::get<Base>(parseBase(u"filters: file.inFolder(this.file.folder)\n"_s));
        QCOMPARE(run(folder, 0).rows.size(), 5); // the folder of Książki/Książki.base: books and pictures
    }

    void notYaml()
    {
        QVERIFY(std::holds_alternative<QString>(parseBase(u"to nie jest YAML"_s)));
        QVERIFY(std::holds_alternative<QString>(parseBase(QString(MaxBaseBytes + 1, u'a'))));
        // Filters nested too deep are left out with a problem, never a crash.
        QString deep = u"filters:\n"_s;
        for (int i = 0; i < 80; ++i)
            deep += QString(2 * i + 2, u' ') + u"and:\n"_s + QString(2 * i + 2, u' ') + u"  -\n"_s;
        const auto parsed = parseBase(deep);
        QVERIFY(std::holds_alternative<Base>(parsed) || std::holds_alternative<QString>(parsed));
    }

    void orderWrittenBack()
    {
        const QString yaml = fixture(u"ksiazki.base"_s);
        const std::optional<QString> out = setBaseViewOrder(yaml, 0, {u"file.name"_s, u"cena"_s, u"Data zakupu"_s});
        QVERIFY(out);
        // Only the order lines changed.
        QVERIFY(out->contains(u"    order:\n      - file.name\n      - cena\n      - \"Data zakupu\"\n    sort:\n"_s));
        QString expected = yaml;
        expected.replace(u"      - autor\n      - cena\n      - formula.cena_brutto\n      - strony\n"_s,
                         u"      - cena\n      - \"Data zakupu\"\n"_s);
        QCOMPARE(*out, expected);
        // A flow list becomes a block list; the comment on the type line stays.
        const std::optional<QString> cards = setBaseViewOrder(yaml, 1, {u"file.name"_s});
        QVERIFY(cards);
        QVERIFY(cards->contains(u"  - type: cards   # a gallery\n"_s));
        QVERIFY(cards->contains(u"    image: note.okladka\n    order:\n      - file.name\n  - type: kanban"_s));
        // A view without order gets one.
        const std::optional<QString> kanban = setBaseViewOrder(yaml, 4, {u"file.name"_s, u"stan"_s});
        QVERIFY(kanban);
        QVERIFY(kanban->endsWith(u"    order:\n      - file.name\n      - stan\n"_s));
        // It reads back.
        const Base back = std::get<Base>(parseBase(*out));
        QCOMPARE(back.views.at(0).order, QStringList({u"file.name"_s, u"cena"_s, u"Data zakupu"_s}));
    }

    void sortWrittenBack()
    {
        const QString yaml = fixture(u"ksiazki.base"_s);
        const std::optional<QString> out = setBaseViewSort(yaml, 1, {{u"note.cena"_s, false}, {u"file.name"_s, true}});
        QVERIFY(out);
        QVERIFY(out->contains(
                u"    sort:\n      - property: note.cena\n        direction: ASC\n      - property: file.name\n"
                u"        direction: DESC\n"_s));
        const Base back = std::get<Base>(parseBase(*out));
        QCOMPARE(back.views.at(1).sort, QList<BaseSort>({{u"note.cena"_s, false}, {u"file.name"_s, true}}));
        // Removed.
        const std::optional<QString> none = setBaseViewSort(yaml, 0, {});
        QVERIFY(none);
        QVERIFY(!none->contains(u"direction: DESC"_s));
        QVERIFY(none->contains(u"      - strony\n    summaries:"_s));
        // CRLF stays.
        QString crlf = yaml;
        crlf.replace(u"\n"_s, u"\r\n"_s);
        const std::optional<QString> c = setBaseViewSort(crlf, 0, {{u"cena"_s, false}});
        QVERIFY(c);
        QCOMPARE(c->count(u'\n'), c->count(u"\r\n"_s));
    }

    void unsafeYamlIsRefused()
    {
        QVERIFY(!setBaseViewOrder(u"views: [{type: table}]\n"_s, 0, {u"a"_s}));
        QVERIFY(!setBaseViewOrder(u"views:\n  - {type: table, order: [a]}\n"_s, 0, {u"b"_s}));
        QVERIFY(!setBaseViewOrder(u"filters: x\n"_s, 0, {u"a"_s}));
        QVERIFY(!setBaseViewOrder(u"views:\n  - type: table\n"_s, 3, {u"a"_s}));
        QVERIFY(setBaseViewOrder(u"views:\n- type: table\n  name: T\n"_s, 0, {u"a"_s}));
    }
};

QTEST_GUILESS_MAIN(TestViewsBases)
#include "tst_views_bases.moc"
