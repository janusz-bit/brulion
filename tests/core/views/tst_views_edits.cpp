// Edits of database views (views/edits.h): table cells writing page properties (YAML and Logseq
// lines) and block properties, task fields, board moves — byte for byte everywhere else.
#include "views/edits.h"

#include "markdown/blockprops.h"
#include "markdown/pageprops.h"

#include <QTest>

using namespace brulion;
using namespace brulion::views;
using namespace Qt::StringLiterals;

Q_DECLARE_METATYPE(brulion::views::EditField)

namespace {

const QDate Today(2026, 10, 14);

QString lines(const QStringList &l, const QString &eol = u"\n"_s)
{
    return l.join(eol);
}

RowEdit pageEdit(const QString &key, const QVariant &value, const QString &type = QString())
{
    RowEdit e;
    e.field = EditField::Property;
    e.key = key;
    e.value = value;
    e.type = type;
    return e;
}

RowEdit blockEdit(const QString &key, const QVariant &value, int line, int ownEnd, const QString &type = QString())
{
    RowEdit e = pageEdit(key, value, type);
    e.blockRow = true;
    e.line = line;
    e.ownEndLine = ownEnd;
    return e;
}

const QString Yaml = lines({u"---"_s, u"status: w toku  # stan"_s, u"koszt: 1200"_s, u"Budżet: 5000"_s,
                            u"termin: 2026-10-20"_s, u"pilne: true"_s, u"etykiety:"_s, u"  - remont"_s,
                            u"  - \"łazienka\""_s, u"---"_s, u"# Dom"_s, u""_s, u"Treść **bez zmian**."_s, u""_s});

} // namespace

class TestViewsEdits : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void yamlProperties_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<QString>("type");
        QTest::addColumn<QString>("line"); // the property's new line(s), as written
        QTest::newRow("text keeps its comment") << u"status"_s << QVariant(u"zrobione"_s) << u"text"_s
                                                << u"status: zrobione  # stan"_s;
        QTest::newRow("number from a number") << u"koszt"_s << QVariant(1250.5) << u"number"_s << u"koszt: 1250.5"_s;
        QTest::newRow("number typed with a comma") << u"koszt"_s << QVariant(u"1 300,5"_s) << u"number"_s
                                                   << u"koszt: 1300.5"_s;
        QTest::newRow("Polish key") << u"Budżet"_s << QVariant(6000.0) << u"number"_s << u"Budżet: 6000"_s;
        QTest::newRow("key folded") << u"budzet"_s << QVariant(7000.0) << u"number"_s << u"Budżet: 7000"_s;
        QTest::newRow("date") << u"termin"_s << QVariant(u"2026-11-01"_s) << u"date"_s << u"termin: 2026-11-01"_s;
        QTest::newRow("checkbox") << u"pilne"_s << QVariant(false) << u"checkbox"_s << u"pilne: false"_s;
        QTest::newRow("list keeps untouched items") << u"etykiety"_s << QVariant(QStringList{u"remont"_s, u"łazienka"_s, u"kuchnia"_s})
                                                    << u"list"_s << u"etykiety:\n  - remont\n  - \"łazienka\"\n  - kuchnia"_s;
        QTest::newRow("list from a text") << u"etykiety"_s << QVariant(u"a, [[B]]"_s) << u"list"_s
                                          << u"etykiety:\n  - a\n  - \"[[B]]\""_s;
        QTest::newRow("text that YAML would read otherwise") << u"status"_s << QVariant(u"yes"_s) << u"text"_s
                                                             << u"status: \"yes\"  # stan"_s;
        QTest::newRow("type kept when not given") << u"koszt"_s << QVariant(u"99"_s) << QString() << u"koszt: 99"_s;
        QTest::newRow("new key") << u"właściciel"_s << QVariant(u"[[Jan Kowalski]]"_s) << u"text"_s
                                 << u"właściciel: \"[[Jan Kowalski]]\""_s;
        QTest::newRow("new number key from the value") << u"ocena"_s << QVariant(4.0) << QString() << u"ocena: 4"_s;
    }
    void yamlProperties()
    {
        QFETCH(QString, key);
        QFETCH(QVariant, value);
        QFETCH(QString, type);
        QFETCH(QString, line);
        for (const QString &eol : {u"\n"_s, u"\r\n"_s}) {
            QString content = Yaml;
            content.replace(u"\n"_s, eol);
            const EditOutcome r = applyRowEdit(content, pageEdit(key, value, type), Today);
            QVERIFY2(r.changed, qPrintable(r.error));
            QString want = line;
            want.replace(u"\n"_s, eol);
            QVERIFY2(r.content.contains(want), qPrintable(r.content));
            // The body stays byte for byte; so do the other properties.
            QVERIFY(r.content.endsWith(lines({u"---"_s, u"# Dom"_s, u""_s, u"Treść **bez zmian**."_s, u""_s}, eol)));
            for (const QString &kept : {u"koszt: 1200"_s, u"termin: 2026-10-20"_s, u"pilne: true"_s}) {
                if (!line.startsWith(kept.section(u':', 0, 0)))
                    QVERIFY2(r.content.contains(kept), qPrintable(kept));
            }
            QCOMPARE(r.content.count(u"\r\n"_s), eol == u"\r\n" ? r.content.count(u'\n') : 0);
            // Reading it back gives the value.
            const markdown::PageProperties back = markdown::readPageProperties(r.content);
            QVERIFY(std::ranges::any_of(back.items, [&](const markdown::PageProperty &p) {
                return queryPropertyKey(p.key) == queryPropertyKey(key) && !p.empty;
            }));
        }
    }

    void yamlRemoveAndSame()
    {
        const EditOutcome removed = applyRowEdit(Yaml, pageEdit(u"pilne"_s, QVariant()), Today);
        QVERIFY(removed.changed);
        QVERIFY(!removed.content.contains(u"pilne"_s));
        const EditOutcome emptyList = applyRowEdit(Yaml, pageEdit(u"etykiety"_s, QStringList()), Today);
        QVERIFY(emptyList.changed);
        QVERIFY(!emptyList.content.contains(u"remont"_s));
        // The same value is not rewritten.
        const EditOutcome same = applyRowEdit(Yaml, pageEdit(u"koszt"_s, 1200.0, u"number"_s), Today);
        QVERIFY(!same.changed);
        QCOMPARE(same.content, Yaml);
        // Removing what is not there changes nothing.
        QVERIFY(!applyRowEdit(Yaml, pageEdit(u"brak"_s, QString()), Today).changed);
        // A number that is none.
        const EditOutcome bad = applyRowEdit(Yaml, pageEdit(u"koszt"_s, u"dużo"_s, u"number"_s), Today);
        QVERIFY(!bad.changed);
        QVERIFY(!bad.error.isEmpty());
        // A page without properties gets a frontmatter.
        const EditOutcome fresh = applyRowEdit(u"Treść.\n"_s, pageEdit(u"status"_s, u"nowy"_s), Today);
        QCOMPARE(fresh.content, u"---\nstatus: nowy\n---\nTreść.\n"_s);
    }

    void logseqPageProperties()
    {
        const QString content = lines({u"status:: zrobione"_s, u"koszt:: 99.5"_s, u""_s, u"- Treść"_s, u""_s});
        const EditOutcome r = applyRowEdit(content, pageEdit(u"status"_s, u"planowany"_s), Today);
        QCOMPARE(r.content, lines({u"status:: planowany"_s, u"koszt:: 99.5"_s, u""_s, u"- Treść"_s, u""_s}));
        const EditOutcome n = applyRowEdit(content, pageEdit(u"koszt"_s, 100.0, u"number"_s), Today);
        QCOMPARE(n.content, lines({u"status:: zrobione"_s, u"koszt:: 100"_s, u""_s, u"- Treść"_s, u""_s}));
        // A new key goes after the last property line; a Polish one cannot be a `key::` line.
        const EditOutcome added = applyRowEdit(content, pageEdit(u"etap"_s, u"zakupy"_s), Today);
        QVERIFY(added.content.startsWith(u"status:: zrobione\nkoszt:: 99.5\netap:: zakupy\n"_s));
        const EditOutcome polish = applyRowEdit(content, pageEdit(u"właściciel"_s, u"Jan"_s), Today);
        QVERIFY(!polish.changed);
        QVERIFY(polish.error.contains(u"właściciel"_s));
    }

    void blockProperties()
    {
        const QString content = lines({u"# Zadania"_s, u""_s, u"- [ ] Kupić płytki 📅 2026-10-15"_s, u"  etap:: zakupy"_s,
                                       u"  - [ ] Podzadanie"_s, u"- [ ] Drugie"_s, u""_s});
        // Change an existing line.
        EditOutcome r = applyRowEdit(content, blockEdit(u"etap"_s, u"montaż"_s, 2, 3), Today);
        QCOMPARE(r.content, QString(content).replace(u"etap:: zakupy"_s, u"etap:: montaż"_s));
        // A new line under the block's text.
        r = applyRowEdit(content, blockEdit(u"koszt"_s, 120.0, 5, 5, u"number"_s), Today);
        QVERIFY2(r.content.contains(u"- [ ] Drugie\n  koszt:: 120\n"_s), qPrintable(r.content));
        // After the block's last property line.
        r = applyRowEdit(content, blockEdit(u"koszt"_s, 12.5, 2, 3, u"number"_s), Today);
        QVERIFY(r.content.contains(u"  etap:: zakupy\n  koszt:: 12.5\n  - [ ] Podzadanie"_s));
        // A list, a checkbox.
        r = applyRowEdit(content, blockEdit(u"ludzie"_s, QStringList{u"Jan"_s, u"[[Anna]]"_s}, 5, 5, u"list"_s), Today);
        QVERIFY(r.content.contains(u"  ludzie:: [[Jan]], [[Anna]]\n"_s));
        r = applyRowEdit(content, blockEdit(u"pilne"_s, true, 5, 5, u"checkbox"_s), Today);
        QVERIFY(r.content.contains(u"  pilne:: true\n"_s));
        // Removed.
        r = applyRowEdit(content, blockEdit(u"etap"_s, QVariant(), 2, 3), Today);
        QVERIFY(!r.content.contains(u"etap::"_s));
        QVERIFY(r.content.contains(u"- [ ] Kupić płytki 📅 2026-10-15\n  - [ ] Podzadanie"_s));
        // The same value: nothing written.
        QVERIFY(!applyRowEdit(content, blockEdit(u"etap"_s, u"zakupy"_s, 2, 3), Today).changed);
        // Refused: a Polish key, a line break, a date that is none.
        QVERIFY(!applyRowEdit(content, blockEdit(u"właściciel"_s, u"x"_s, 2, 3), Today).changed);
        QVERIFY(!applyRowEdit(content, blockEdit(u"termin"_s, u"jutro"_s, 2, 3, u"date"_s), Today).changed);
        const EditOutcome multi = applyRowEdit(content, blockEdit(u"opis"_s, u"a\nb"_s, 5, 5, u"text"_s), Today);
        QVERIFY(multi.content.contains(u"  opis:: a b\n"_s));
        // CRLF kept.
        QString crlf = content;
        crlf.replace(u"\n"_s, u"\r\n"_s);
        r = applyRowEdit(crlf, blockEdit(u"etap"_s, u"montaż"_s, 2, 3), Today);
        QCOMPARE(r.content, QString(crlf).replace(u"etap:: zakupy"_s, u"etap:: montaż"_s));
    }

    void taskFields_data()
    {
        QTest::addColumn<EditField>("field");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<QString>("expected");
        QTest::newRow("status done") << EditField::Status << QString() << QVariant(u"done"_s)
                                     << u"- [x] Kupić płytki 📅 2026-10-15 ✅ 2026-10-14"_s;
        QTest::newRow("status doing") << EditField::Status << QString() << QVariant(u"doing"_s)
                                      << u"- [/] Kupić płytki 📅 2026-10-15"_s;
        QTest::newRow("no status") << EditField::Status << QString() << QVariant(QString())
                                   << u"- Kupić płytki 📅 2026-10-15"_s;
        QTest::newRow("priority") << EditField::Priority << QString() << QVariant(u"high"_s)
                                  << u"- [ ] Kupić płytki 📅 2026-10-15 ⏫"_s;
        QTest::newRow("due moved") << EditField::TaskDate << u"due"_s << QVariant(u"2026-10-22"_s)
                                   << u"- [ ] Kupić płytki 📅 2026-10-22"_s;
        QTest::newRow("due removed") << EditField::TaskDate << u"due"_s << QVariant() << u"- [ ] Kupić płytki"_s;
        QTest::newRow("scheduled added") << EditField::TaskDate << u"scheduled"_s << QVariant(u"2026-10-16 09:30"_s)
                                         << u"- [ ] Kupić płytki 📅 2026-10-15 ⏳ 2026-10-16 09:30"_s;
    }
    void taskFields()
    {
        QFETCH(EditField, field);
        QFETCH(QString, key);
        QFETCH(QVariant, value);
        QFETCH(QString, expected);
        const QString content = lines({u"Notatka."_s, u"- [ ] Kupić płytki 📅 2026-10-15"_s, u"- [ ] Inne"_s, u""_s});
        RowEdit e;
        e.field = field;
        e.key = key;
        e.value = value;
        e.line = 1;
        e.blockRow = true;
        const EditOutcome r = applyRowEdit(content, e, Today);
        QCOMPARE(r.content, lines({u"Notatka."_s, expected, u"- [ ] Inne"_s, u""_s}));
    }

    void badTaskValues()
    {
        const QString content = u"- [ ] Zadanie\n"_s;
        RowEdit e;
        e.field = EditField::Status;
        e.value = u"bogus"_s;
        QVERIFY(!applyRowEdit(content, e, Today).changed);
        e.field = EditField::TaskDate;
        e.key = u"due"_s;
        e.value = u"2026-02-30"_s;
        QVERIFY(!applyRowEdit(content, e, Today).changed);
        e.key = u"when"_s;
        e.value = u"2026-02-03"_s;
        QVERIFY(!applyRowEdit(content, e, Today).changed);
        e.field = EditField::Priority;
        e.value = u"pilne"_s;
        QVERIFY(!applyRowEdit(content, e, Today).changed);
    }

    void boardMoveRoundTrip()
    {
        // A board by a page property: moving a card writes the property, reading it again puts the
        // card in its new column.
        VaultIndex index;
        index.update(u"A.md"_s, lines({u"---"_s, u"etap: nowy"_s, u"---"_s, u"Treść."_s}));
        index.update(u"B.md"_s, lines({u"---"_s, u"etap: gotowy"_s, u"---"_s, u"Treść."_s}));
        const auto viewOf = [&] {
            const Query q = std::get<Query>(parseQuery(u"type:pages view:board group:etap sort:page"_s));
            ViewOptions o;
            o.today = Today;
            return std::get<ViewResult>(buildQueryView(q, runQuery(q, index, Today), index, o));
        };
        ViewResult v = viewOf();
        QCOMPARE(v.groups.size(), 3); // gotowy, nowy, No value
        const int a = 0;
        QCOMPARE(v.rows.at(a).path, u"A.md"_s);
        const int from = v.rows.at(a).group;
        QCOMPARE(v.groups.at(from).value, u"nowy"_s);
        const std::optional<RowEdit> e = moveEdit(v, v.rows.at(a), from, 0);
        QVERIFY(e);
        const EditOutcome r = applyRowEdit(index.content(u"A.md"_s), *e, Today);
        QCOMPARE(r.content, lines({u"---"_s, u"etap: gotowy"_s, u"---"_s, u"Treść."_s}));
        index.update(u"A.md"_s, r.content);
        v = viewOf();
        QCOMPARE(v.groups.at(0).value, u"gotowy"_s);
        QCOMPARE(v.groups.at(0).rows.size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestViewsEdits)
#include "tst_views_edits.moc"
