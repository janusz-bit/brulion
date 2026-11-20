// Standalone expressions (dataview/expression.h): Dataview formulas of query views and the
// expression language of Obsidian Bases (docs/SYNTAX.md §10 "Obsidian Bases") over a small Polish
// notebook. "Now" is Wednesday 2026-10-14, 10:00.
#include "dataview/expression.h"

#include <QTest>

using namespace brulion;
using namespace brulion::dataview;
using namespace Qt::StringLiterals;

namespace {

const QDateTime Now(QDate(2026, 10, 14), QTime(10, 0));

QString lines(const QStringList &l)
{
    return l.join(u'\n');
}

void build(VaultIndex &index)
{
    index.update(u"Książki/Lalka.md"_s,
                 lines({u"---"_s, u"autor: \"[[Bolesław Prus]]\""_s, u"cena: 39.9"_s, u"strony: 680"_s,
                        u"ocena: 5"_s, u"przeczytana: true"_s, u"wydano: 1890-01-01"_s,
                        u"tags: [książka, klasyka/polska]"_s, u"Data zakupu: 2026-10-01"_s, u"gatunki: [powieść, realizm]"_s,
                        u"---"_s, u"Zobacz [[Bolesław Prus]] i ![[okładka.png]]."_s}));
    index.update(u"Książki/Quo vadis.md"_s,
                 lines({u"---"_s, u"autor: \"[[Henryk Sienkiewicz]]\""_s, u"cena: 25"_s, u"strony: 600"_s,
                        u"przeczytana: false"_s, u"tags: [książka]"_s, u"---"_s, u"Treść."_s}));
    index.update(u"Ludzie/Bolesław Prus.md"_s, lines({u"---"_s, u"urodzony: 1847"_s, u"---"_s, u"Pisarz."_s}));
    index.update(u"Ludzie/Henryk Sienkiewicz.md"_s, u"Pisarz."_s);
    index.update(u"Książki/okładka.png"_s, QString()); // an attachment
}

Options options(const QString &current = u"Książki/Lalka.md"_s)
{
    Options o;
    o.currentPath = current;
    o.now = Now;
    o.stat = [](const VaultPath &path) -> std::optional<FileStat> {
        return FileStat{path, false, path.endsWith(u".png") ? 2048 : 100,
                        QDateTime(QDate(2026, 10, 10), QTime(12, 0)).toMSecsSinceEpoch()};
    };
    return o;
}

/// A value as one line of text for comparing (lists "[a, b]", links "[[path]]", dates ISO).
QString show(const Value &v)
{
    switch (v.type()) {
    case Value::Type::Null: return u"null"_s;
    case Value::Type::Boolean: return v.asBool() ? u"true"_s : u"false"_s;
    case Value::Type::Number: return QString::number(v.asNumber(), 'g', 12);
    case Value::Type::String: return u'"' + v.asString() + u'"';
    case Value::Type::Date:
        return v.asDate().hasTime ? v.asDate().at.toString(u"yyyy-MM-dd HH:mm"_s) : v.asDate().at.date().toString(Qt::ISODate);
    case Value::Type::Duration: return u"dur(%1m,%2ms)"_s.arg(v.asDuration().months).arg(v.asDuration().ms);
    case Value::Type::Link: return u"[["_s + v.asLink().path + u"]]"_s + (v.asLink().embed ? u"!"_s : QString());
    case Value::Type::List: {
        QStringList parts;
        for (const Value &item : v.asList())
            parts.append(show(item));
        return u'[' + parts.join(u", "_s) + u']';
    }
    case Value::Type::Object: return u"{"_s + basesTypeName(v) + u'}';
    case Value::Type::Function: return u"fn"_s;
    }
    return {};
}

} // namespace

class TestDataviewExpression : public QObject {
    Q_OBJECT

private:
    VaultIndex m_index;

    QString eval(const QString &source, Syntax syntax, const QString &path = u"Książki/Lalka.md"_s,
                 const QList<std::pair<QString, Value>> &bound = {})
    {
        const auto parsed = parseExpression(source, syntax);
        if (const QString *error = std::get_if<QString>(&parsed))
            return u"error: "_s + *error;
        Evaluator e(m_index, options());
        const Value row = e.file(path);
        const auto value = e.evaluate(std::get<Expression>(parsed), row, bound);
        if (const QString *error = std::get_if<QString>(&value))
            return u"error: "_s + *error;
        return show(std::get<Value>(value));
    }

private Q_SLOTS:
    void initTestCase() { build(m_index); }

    void dataviewFormulas_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("expected");
        QTest::newRow("arithmetic") << u"cena * 2"_s << u"79.8"_s;
        QTest::newRow("round") << u"round(cena * 1.23, 2)"_s << u"49.08"_s;
        QTest::newRow("Polish key with a blank") << u"data-zakupu"_s << u"2026-10-01"_s;
        QTest::newRow("file field") << u"file.name"_s << u"\"Lalka\""_s;
        QTest::newRow("date arithmetic") << u"wydano + dur(1 year)"_s << u"1891-01-01"_s;
        QTest::newRow("text") << u"\"Strony: \" + strony"_s << u"\"Strony: 680\""_s;
        QTest::newRow("link field") << u"autor.urodzony"_s << u"1847"_s;
        QTest::newRow("unknown function") << u"foo(1)"_s << u"error: "_s;
        QTest::newRow("trailing garbage") << u"cena cena"_s << u"error: "_s;
        QTest::newRow("empty") << u""_s << u"error: "_s;
    }
    void dataviewFormulas()
    {
        QFETCH(QString, source);
        QFETCH(QString, expected);
        const QString got = eval(source, Syntax::Dataview);
        if (expected == u"error: ")
            QVERIFY2(got.startsWith(expected), qPrintable(got));
        else
            QCOMPARE(got, expected);
    }

    void basesExpressions_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("expected");
        // Properties and operators.
        QTest::newRow("bare property") << u"cena"_s << u"39.9"_s;
        QTest::newRow("note property") << u"note.strony"_s << u"680"_s;
        QTest::newRow("bracketed property") << u"note[\"Data zakupu\"]"_s << u"2026-10-01"_s;
        QTest::newRow("comparison") << u"cena > 30 && przeczytana"_s << u"true"_s;
        QTest::newRow("or and not") << u"!przeczytana || ocena == 5"_s << u"true"_s;
        QTest::newRow("strict equality") << u"ocena === 5"_s << u"true"_s;
        QTest::newRow("not equal") << u"ocena != 5"_s << u"false"_s;
        QTest::newRow("precedence") << u"1 + 2 * 3 - 4 / 2"_s << u"5"_s;
        QTest::newRow("modulo") << u"strony % 7"_s << u"1"_s;
        QTest::newRow("text concatenation") << u"\"Cena: \" + cena.toFixed(2) + ' zł'"_s << u"\"Cena: 39.90 zł\""_s;
        QTest::newRow("missing property") << u"brak"_s << u"null"_s;
        QTest::newRow("missing compares false") << u"brak > 3"_s << u"false"_s;
        // Files.
        QTest::newRow("file.name has the extension") << u"file.name"_s << u"\"Lalka.md\""_s;
        QTest::newRow("file.basename") << u"file.basename"_s << u"\"Lalka\""_s;
        QTest::newRow("file.ext") << u"file.ext"_s << u"\"md\""_s;
        QTest::newRow("file.folder") << u"file.folder"_s << u"\"Książki\""_s;
        QTest::newRow("file.path") << u"file.path"_s << u"\"Książki/Lalka.md\""_s;
        QTest::newRow("file.size") << u"file.size"_s << u"100"_s;
        QTest::newRow("file.mtime") << u"file.mtime"_s << u"2026-10-10 12:00"_s;
        QTest::newRow("hasTag") << u"file.hasTag(\"książka\")"_s << u"true"_s;
        QTest::newRow("hasTag nested") << u"file.hasTag(\"klasyka\")"_s << u"true"_s;
        QTest::newRow("hasTag any of") << u"file.hasTag(\"x\", \"KSIĄŻKA\")"_s << u"true"_s;
        QTest::newRow("hasTag no") << u"file.hasTag(\"klasyk\")"_s << u"false"_s;
        QTest::newRow("inFolder") << u"file.inFolder(\"Książki\")"_s << u"true"_s;
        QTest::newRow("inFolder other") << u"file.inFolder(\"Ludzie\")"_s << u"false"_s;
        QTest::newRow("hasLink") << u"file.hasLink(\"Bolesław Prus\")"_s << u"true"_s;
        QTest::newRow("hasLink to link") << u"file.hasLink(autor)"_s << u"true"_s;
        QTest::newRow("hasProperty") << u"file.hasProperty(\"ocena\")"_s << u"true"_s;
        QTest::newRow("hasProperty no") << u"file.hasProperty(\"wydawca\")"_s << u"false"_s;
        QTest::newRow("asLink") << u"file.asLink()"_s << u"[[Książki/Lalka.md]]"_s;
        QTest::newRow("this") << u"this.file.name"_s << u"\"Lalka.md\""_s;
        QTest::newRow("link equals this") << u"autor == this"_s << u"false"_s;
        QTest::newRow("link equals link") << u"autor == link(\"Bolesław Prus\")"_s << u"true"_s;
        QTest::newRow("link equals text") << u"autor == \"Bolesław Prus\""_s << u"true"_s;
        QTest::newRow("asFile properties") << u"autor.asFile().properties.urodzony"_s << u"1847"_s;
        QTest::newRow("embeds") << u"file.embeds.length"_s << u"1"_s;
        QTest::newRow("links") << u"file.links.length"_s << u"2"_s;
        QTest::newRow("tags list") << u"file.tags.contains(\"#książka\")"_s << u"true"_s;
        // Strings.
        QTest::newRow("contains") << u"file.basename.contains(\"al\")"_s << u"true"_s;
        QTest::newRow("containsAll") << u"\"żółw\".containsAll(\"ż\", \"w\")"_s << u"true"_s;
        QTest::newRow("containsAny") << u"\"żółw\".containsAny(\"x\", \"ó\")"_s << u"true"_s;
        QTest::newRow("startsWith") << u"file.basename.startsWith(\"La\")"_s << u"true"_s;
        QTest::newRow("lower") << u"\"ŻÓŁW\".lower()"_s << u"\"żółw\""_s;
        QTest::newRow("title") << u"\"pan tadeusz\".title()"_s << u"\"Pan Tadeusz\""_s;
        QTest::newRow("trim") << u"\"  hi  \".trim()"_s << u"\"hi\""_s;
        QTest::newRow("slice") << u"\"hello\".slice(1, 4)"_s << u"\"ell\""_s;
        QTest::newRow("slice negative") << u"\"hello\".slice(-3)"_s << u"\"llo\""_s;
        QTest::newRow("split") << u"\"a,b,c,d\".split(\",\", 3)"_s << u"[\"a\", \"b\", \"c\"]"_s;
        QTest::newRow("replace all") << u"\"a:b:c\".replace(\":\", \"-\")"_s << u"\"a-b-c\""_s;
        QTest::newRow("repeat") << u"\"ab\".repeat(3)"_s << u"\"ababab\""_s;
        QTest::newRow("reverse string") << u"\"żółw\".reverse()"_s << u"\"włóż\""_s;
        QTest::newRow("length") << u"\"żółw\".length"_s << u"4"_s;
        QTest::newRow("isEmpty") << u"\"\".isEmpty()"_s << u"true"_s;
        QTest::newRow("missing isEmpty") << u"brak.isEmpty()"_s << u"true"_s;
        // Numbers.
        QTest::newRow("toFixed") << u"(3.14159).toFixed(2)"_s << u"\"3.14\""_s;
        QTest::newRow("round digits") << u"(2.3333).round(2)"_s << u"2.33"_s;
        QTest::newRow("round") << u"(2.5).round()"_s << u"3"_s;
        QTest::newRow("abs") << u"(-5).abs()"_s << u"5"_s;
        QTest::newRow("ceil floor") << u"(2.1).ceil() + (2.9).floor()"_s << u"5"_s;
        QTest::newRow("number") << u"number(\"3.4\") + number(true)"_s << u"4.4"_s;
        QTest::newRow("min max") << u"max(1, 5, 3) - min(4, 2)"_s << u"3"_s;
        // Dates.
        QTest::newRow("today") << u"today()"_s << u"2026-10-14"_s;
        QTest::newRow("now") << u"now()"_s << u"2026-10-14 10:00"_s;
        QTest::newRow("date plus month") << u"date(\"2024-12-01\") + \"1M\" + \"4h\" + \"3m\""_s
                                         << u"2025-01-01 04:03"_s;
        QTest::newRow("date minus week") << u"today() - \"1 week\""_s << u"2026-10-07"_s;
        QTest::newRow("date compare") << u"file.mtime > now() - \"1 week\""_s << u"true"_s;
        QTest::newRow("date difference") << u"(today() - date(\"2026-10-04\")).days"_s << u"10"_s;
        QTest::newRow("date fields") << u"now().year * 100 + now().month"_s << u"202610"_s;
        QTest::newRow("format") << u"date(\"2025-05-27 09:05\").format(\"YYYY-MM-DD HH:mm, dddd D MMMM\")"_s
                                << u"\"2025-05-27 09:05, Tuesday 27 May\""_s;
        QTest::newRow("format brackets") << u"today().format(\"[Week] W\")"_s << u"\"Week 42\""_s;
        QTest::newRow("date()") << u"now().date()"_s << u"2026-10-14"_s;
        QTest::newRow("time()") << u"now().time()"_s << u"\"10:00:00\""_s;
        QTest::newRow("compare with a date text") << u"wydano < \"1900-01-01\""_s << u"true"_s;
        QTest::newRow("duration times") << u"today() + duration(\"1d\") * 2"_s << u"2026-10-16"_s;
        QTest::newRow("bad duration") << u"today() - \"tuesday\""_s << u"null"_s;
        // Lists.
        QTest::newRow("list contains") << u"gatunki.contains(\"realizm\")"_s << u"true"_s;
        QTest::newRow("filter") << u"[1, 2, 3, 4].filter(value > 2)"_s << u"[3, 4]"_s;
        QTest::newRow("filter by index") << u"[5, 6, 7].filter(index != 1)"_s << u"[5, 7]"_s;
        QTest::newRow("map") << u"[1, 2, 3].map(value + 1)"_s << u"[2, 3, 4]"_s;
        QTest::newRow("reduce") << u"[1, 2, 3].reduce(acc + value, 0)"_s << u"6"_s;
        QTest::newRow("reduce max") << u"[3, 9, 4].reduce(if(acc == null || value > acc, value, acc), null)"_s
                                    << u"9"_s;
        QTest::newRow("join") << u"gatunki.join(\" / \")"_s << u"\"powieść / realizm\""_s;
        QTest::newRow("sort unique") << u"[3, 1, 2, 3].unique().sort()"_s << u"[1, 2, 3]"_s;
        QTest::newRow("flat") << u"[1, [2, 3]].flat()"_s << u"[1, 2, 3]"_s;
        QTest::newRow("index") << u"gatunki[1]"_s << u"\"realizm\""_s;
        QTest::newRow("list()") << u"list(\"x\")"_s << u"[\"x\"]"_s;
        QTest::newRow("mean") << u"[1, 2, 6].mean()"_s << u"3"_s;
        QTest::newRow("list length") << u"gatunki.length"_s << u"2"_s;
        // Any.
        QTest::newRow("isType") << u"\"x\".isType(\"string\") && cena.isType(\"number\") && file.isType(\"file\")"_s
                                << u"true"_s;
        QTest::newRow("isTruthy") << u"(0).isTruthy()"_s << u"false"_s;
        QTest::newRow("toString") << u"(123).toString()"_s << u"\"123\""_s;
        QTest::newRow("if") << u"if(przeczytana, \"tak\", \"nie\")"_s << u"\"tak\""_s;
        QTest::newRow("if without else") << u"if(!przeczytana, \"tak\")"_s << u"null"_s;
        QTest::newRow("object") << u"{\"a\": 1, b: 2}.keys()"_s << u"[\"a\", \"b\"]"_s;
        QTest::newRow("html is text") << u"html(\"<b>x</b>\")"_s << u"\"<b>x</b>\""_s;
        QTest::newRow("escapeHTML") << u"escapeHTML(\"<b>\")"_s << u"\"&lt;b&gt;\""_s;
        QTest::newRow("image link") << u"image(\"okładka.png\")"_s << u"[[Książki/okładka.png]]!"_s;
        QTest::newRow("link url") << u"link(\"https://obsidian.md\", \"Obsidian\")"_s
                                  << u"\"[Obsidian](https://obsidian.md)\""_s;
        // Formulas and summaries (bound by the caller).
        QTest::newRow("values") << u"values.length"_s << u"null"_s;
    }
    void basesExpressions()
    {
        QFETCH(QString, source);
        QFETCH(QString, expected);
        QCOMPARE(eval(source, Syntax::Bases), expected);
    }

    void relativeDates()
    {
        // (English plurals come from the app's translations: here the source text.)
        const QString past = eval(u"date(\"2026-10-11 10:00\").relative()"_s, Syntax::Bases);
        QVERIFY2(past.startsWith(u"\"3 day"_s) && past.endsWith(u" ago\""_s), qPrintable(past));
        const QString future = eval(u"(now() + \"2h\").relative()"_s, Syntax::Bases);
        QVERIFY2(future.startsWith(u"\"in 2 hour"_s), qPrintable(future));
        QCOMPARE(eval(u"now().relative()"_s, Syntax::Bases), u"\"in a few seconds\""_s);
    }

    void basesAttachmentRows()
    {
        QCOMPARE(eval(u"file.name"_s, Syntax::Bases, u"Książki/okładka.png"_s), u"\"okładka.png\""_s);
        QCOMPARE(eval(u"file.ext == \"png\" && file.size > 1000"_s, Syntax::Bases, u"Książki/okładka.png"_s),
                 u"true"_s);
        QCOMPARE(eval(u"file.hasTag(\"książka\")"_s, Syntax::Bases, u"Książki/okładka.png"_s), u"false"_s);
        QCOMPARE(eval(u"file.backlinks.length"_s, Syntax::Bases, u"Książki/okładka.png"_s), u"1"_s);
        QCOMPARE(eval(u"cena"_s, Syntax::Bases, u"Książki/okładka.png"_s), u"null"_s);
    }

    void basesBoundNames()
    {
        const QList<std::pair<QString, Value>> bound{
                {u"formula"_s, Value::map({{u"netto"_s, Value::number(10)}})},
                {u"values"_s, Value::list({Value::number(1), Value::number(4)})}};
        QCOMPARE(eval(u"formula.netto * 2"_s, Syntax::Bases, u"Książki/Lalka.md"_s, bound), u"20"_s);
        QCOMPARE(eval(u"values.mean().round(3)"_s, Syntax::Bases, u"Książki/Lalka.md"_s, bound), u"2.5"_s);
        QCOMPARE(eval(u"formula[\"netto\"]"_s, Syntax::Bases, u"Książki/Lalka.md"_s, bound), u"10"_s);
    }

    void basesFormulaReferences()
    {
        const auto parsed = parseExpression(u"formula.Netto + formula[\"vat\"] * formula.netto"_s, Syntax::Bases);
        QVERIFY(std::holds_alternative<Expression>(parsed));
        QCOMPARE(std::get<Expression>(parsed).formulas(), QStringList({u"netto"_s, u"vat"_s}));
    }

    void properties_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<int>("syntax");
        QTest::addColumn<QString>("property");
        QTest::newRow("dataview name") << u"koszt"_s << int(Syntax::Dataview) << u"koszt"_s;
        QTest::newRow("dataview formula") << u"koszt * 2"_s << int(Syntax::Dataview) << QString();
        QTest::newRow("bases bare") << u"status"_s << int(Syntax::Bases) << u"status"_s;
        QTest::newRow("bases note") << u"note.Status"_s << int(Syntax::Bases) << u"Status"_s;
        QTest::newRow("bases bracket") << u"note[\"Due date\"]"_s << int(Syntax::Bases) << u"Due date"_s;
        QTest::newRow("bases file") << u"file.name"_s << int(Syntax::Bases) << QString();
        QTest::newRow("bases formula") << u"status.lower()"_s << int(Syntax::Bases) << QString();
    }
    void properties()
    {
        QFETCH(QString, source);
        QFETCH(int, syntax);
        const auto parsed = parseExpression(source, Syntax(syntax));
        QVERIFY(std::holds_alternative<Expression>(parsed));
        QTEST(std::get<Expression>(parsed).property(), "property");
    }

    void basesErrors_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("fragment");
        QTest::newRow("regex") << u"/abc/.matches(file.name)"_s << u"Regular expressions"_s;
        QTest::newRow("regex in replace") << u"file.name.replace(/a/g, \"b\")"_s << u"Regular expressions"_s;
        QTest::newRow("unknown method") << u"file.name.shout()"_s << u"shout"_s;
        QTest::newRow("unknown function") << u"plugin(1)"_s << u"plugin"_s;
        QTest::newRow("argument count") << u"if(true)"_s << u"if"_s;
        QTest::newRow("unclosed string") << u"\"abc"_s << QString();
        QTest::newRow("unclosed paren") << u"(1 + 2"_s << QString();
        QTest::newRow("dangling operator") << u"1 +"_s << QString();
        QTest::newRow("empty") << u"   "_s << QString();
        QTest::newRow("stray character") << u"1 # 2"_s << QString();
        QTest::newRow("call of a value") << u"(1)(2)"_s << QString();
    }
    void basesErrors()
    {
        QFETCH(QString, source);
        QFETCH(QString, fragment);
        const auto parsed = parseExpression(source, Syntax::Bases);
        QVERIFY2(std::holds_alternative<QString>(parsed), qPrintable(source));
        QVERIFY2(std::get<QString>(parsed).contains(fragment), qPrintable(std::get<QString>(parsed)));
    }

    void limits()
    {
        // Deep nesting is refused before it can exhaust the stack.
        QString deep;
        for (int i = 0; i < 200; ++i)
            deep += u'(';
        deep += u'1';
        for (int i = 0; i < 200; ++i)
            deep += u')';
        QVERIFY(std::holds_alternative<QString>(parseExpression(deep, Syntax::Bases)));
        QString chain = u"1"_s;
        for (int i = 0; i < 400; ++i)
            chain += u" + 1"_s;
        QVERIFY(std::holds_alternative<QString>(parseExpression(chain, Syntax::Bases)));
        // A value growing without end stops with a message instead of eating the memory.
        const QString huge = u"\"ab\".repeat(100000000)"_s;
        QVERIFY(eval(huge, Syntax::Bases).startsWith(u"error: "_s));
        const QString loop = u"[1, 2, 3, 4, 5, 6, 7, 8, 9, 10].map([1, 2, 3, 4, 5, 6, 7, 8, 9, 10].map("_s
                             u"[1, 2, 3, 4, 5, 6, 7, 8, 9, 10].map(\"x\".repeat(1000000))))"_s;
        QVERIFY(eval(loop, Syntax::Bases).startsWith(u"error: "_s));
    }

    void evaluatorStepsAreShared()
    {
        Options o = options();
        o.maxSteps = 2000;
        Evaluator e(m_index, o);
        const auto parsed = parseExpression(u"[1, 2, 3, 4, 5, 6, 7, 8].map(value * 2).sum()"_s, Syntax::Bases);
        const Expression expr = std::get<Expression>(parsed);
        bool failed = false;
        for (int i = 0; i < 1000 && !failed; ++i)
            failed = std::holds_alternative<QString>(e.evaluate(expr, e.page(u"Książki/Lalka.md"_s)));
        QVERIFY(failed);
        QVERIFY(e.exhausted());
    }
};

QTEST_GUILESS_MAIN(TestDataviewExpression)
#include "tst_dataview_expression.moc"
