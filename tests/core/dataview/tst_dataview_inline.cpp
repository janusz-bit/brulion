// Inline Dataview queries (dataview::evaluateInline, docs/SYNTAX.md §10 "Inline queries"): the code
// of `` `= this.file.name` `` evaluated for the page it is on, over a small Polish notebook. "Now"
// is Wednesday 2026-10-14, 10:00 local time.
#include "dataview/dataview.h"

#include <QElapsedTimer>
#include <QTest>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

const QDateTime Now(QDate(2026, 10, 14), QTime(10, 0));

QString lines(const QStringList &l)
{
    return l.join(u'\n');
}

dataview::Options options(const QString &current)
{
    dataview::Options o;
    o.currentPath = current;
    o.now = Now;
    return o;
}

/// "kind: text" ("value: Kraków", "null", "error: …"), "none" when it is no Dataview expression.
QString shown(const std::optional<dataview::InlineValue> &v)
{
    if (!v)
        return u"none"_s;
    switch (v->kind) {
    case dataview::InlineValue::Kind::Value: return u"value: "_s + v->text;
    case dataview::InlineValue::Kind::Null: return u"null"_s;
    case dataview::InlineValue::Kind::Error: return u"error"_s;
    }
    return {};
}

} // namespace

class TestDataviewInline : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        m_index.update(u"Projekty/Łąka zielona.md"_s,
                       lines({u"---"_s, u"właściciel: \"[[Żaneta Kowalska]]\""_s, u"budżet: 1500"_s,
                              u"termin: 2026-10-20"_s, u"Data oddania: 2026-11-02"_s, u"tags: [projekt]"_s, u"---"_s,
                              u"postęp:: 40"_s, u""_s, u"Nazwa: `= this.file.name`, termin `= this.termin`."_s,
                              u"- [ ] Skosić trawę 📅 2026-10-15"_s, u"- [x] Kupić nasiona"_s}));
        m_index.update(u"Ludzie/Żaneta Kowalska.md"_s, lines({u"miasto:: Kraków"_s, u"telefon:: 600 100 200"_s}));
        m_index.update(u"Strona.md"_s, lines({u"pole:: wartość ąę"_s, u"Zobacz [[Łąka zielona]]."_s}));
        m_index.update(u"journals/2026-10-14.md"_s, lines({u"- Spotkanie w [[Łąka zielona]]"_s}));
    }

    void values_data()
    {
        QTest::addColumn<QString>("current");
        QTest::addColumn<QString>("expression");
        QTest::addColumn<QString>("expected");
        const QString page = u"Projekty/Łąka zielona.md"_s;
        QTest::newRow("this.file.name") << page << u"this.file.name"_s << u"value: Łąka zielona"_s;
        QTest::newRow("a bare field of the page") << page << u"file.name"_s << u"value: Łąka zielona"_s;
        QTest::newRow("the folder") << page << u"this.file.folder"_s << u"value: Projekty"_s;
        QTest::newRow("a number field") << page << u"this.budżet * 2"_s << u"value: 3000"_s;
        QTest::newRow("an inline field") << page << u"budżet + postęp"_s << u"value: 1540"_s;
        QTest::newRow("today plus a week") << page << u"date(today) + dur(1 week)"_s << u"value: 2026-10-21"_s;
        QTest::newRow("a late evening, local time")
                << page << u"date(\"2026-10-15 23:30\")"_s << u"value: 2026-10-15 23:30"_s;
        QTest::newRow("days left") << page << u"(this.termin - date(today)).days"_s << u"value: 6"_s;
        QTest::newRow("a key with a blank") << page << u"this[\"data oddania\"]"_s << u"value: 2026-11-02"_s;
        QTest::newRow("a field of a link") << page << u"[[Strona]].pole"_s << u"value: wartość ąę"_s;
        QTest::newRow("a person's city") << page << u"[[Żaneta Kowalska]].miasto"_s << u"value: Kraków"_s;
        QTest::newRow("a link field's page") << page << u"this.właściciel.miasto"_s << u"value: Kraków"_s;
        QTest::newRow("the link") << page << u"this.file.link"_s << u"value: Łąka zielona"_s;
        QTest::newRow("backlinks") << page << u"length(this.file.inlinks)"_s << u"value: 2"_s;
        QTest::newRow("open tasks") << page << u"length(filter(this.file.tasks, (t) => !t.completed))"_s
                                    << u"value: 1"_s;
        QTest::newRow("a function") << page << u"upper(\"zażółć\")"_s << u"value: ZAŻÓŁĆ"_s;
        QTest::newRow("a missing field") << page << u"this.brak"_s << u"null"_s;
        QTest::newRow("null") << page << u"null"_s << u"null"_s;
        QTest::newRow("no page: its name") << u"Nie ma.md"_s << u"this.file.name"_s << u"value: Nie ma"_s;
        QTest::newRow("no page: no fields") << u"Nie ma.md"_s << u"this.budżet"_s << u"null"_s;
        QTest::newRow("no page, plain arithmetic") << u"Nie ma.md"_s << u"1 + 1"_s << u"value: 2"_s;
        // Not Dataview: left to the calculator (its error, or the code as written).
        QTest::newRow("an operator missing its side") << page << u"this.file.name +"_s << u"none"_s;
        QTest::newRow("an unknown function") << page << u"żółw(1)"_s << u"none"_s;
        QTest::newRow("a regular expression") << page << u"regextest(\"a\", \"b\")"_s << u"none"_s;
        QTest::newRow("a calculator's percent") << page << u"20% of 150"_s << u"none"_s;
        QTest::newRow("a unit") << page << u"5 km to mi"_s << u"none"_s;
        QTest::newRow("unclosed") << page << u"[[Strona"_s << u"none"_s;
        QTest::newRow("empty") << page << QString() << u"none"_s;
    }
    void values()
    {
        QFETCH(QString, current);
        QFETCH(QString, expression);
        QFETCH(QString, expected);
        QCOMPARE(shown(dataview::evaluateInline(expression, m_index, options(current))), expected);
    }

    void markdownOfALink()
    {
        const auto v = dataview::evaluateInline(u"[[Żaneta Kowalska]]"_s, m_index, options(u"Strona.md"_s));
        QVERIFY(v);
        QCOMPARE(v->kind, dataview::InlineValue::Kind::Value);
        QCOMPARE(v->markdown, u"[[Żaneta Kowalska]]"_s);
        QCOMPARE(v->text, u"Żaneta Kowalska"_s);
        const auto list = dataview::evaluateInline(u"this.file.outlinks"_s, m_index, options(u"Strona.md"_s));
        QVERIFY(list);
        QCOMPARE(list->markdown, u"[[Łąka zielona]]"_s);
    }

    void tooCostly()
    {
        dataview::Options o = options(u"Strona.md"_s);
        o.maxSteps = 5;
        const auto v = dataview::evaluateInline(u"1 + 2 + 3 + 4 + 5 + 6 + 7 + 8"_s, m_index, o);
        QVERIFY(v);
        QCOMPARE(v->kind, dataview::InlineValue::Kind::Error);
        QVERIFY2(v->text.contains(u"too long"_s), qPrintable(v->text));
    }

    void boundedBudget()
    {
        // A heavy expression stops (with its message) instead of freezing the page it is drawn on.
        QElapsedTimer timer;
        timer.start();
        const auto v = dataview::evaluateInline(
                u"length(map(split(padleft(\"\", 10000, \"a\"), \"\"), (x) => map(split(padleft(\"\", 10000, \"ż\"), \"\"), (y) => y)))"_s, m_index,
                options(u"Strona.md"_s));
        QVERIFY(v);
        QCOMPARE(v->kind, dataview::InlineValue::Kind::Error);
        QVERIFY(timer.elapsed() < 20000);
    }

private:
    VaultIndex m_index;
};

QTEST_GUILESS_MAIN(TestDataviewInline)
#include "tst_dataview_inline.moc"
