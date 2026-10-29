// Page properties as typed fields (markdown/pageprops.h): type inference, reading both
// property blocks, and the byte-preserving single-property edits of the "Właściwości" panel.
#include "markdown/frontmatter.h"
#include "markdown/pageprops.h"

#include <QTest>

using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

PropertyType typeOf(const QString &name)
{
    return parsePropertyType(name).value_or(PropertyType::Unsupported);
}

PropertySource sourceOf(const QString &name)
{
    return parsePropertySource(name).value_or(PropertySource::Yaml);
}

/// A compact description of what readPageProperties() found.
QString describe(const PageProperties &p)
{
    QStringList parts;
    const auto block = [](const QString &name, const PagePropertyBlock &b) {
        QString s = name + u'[' + QString::number(b.firstLine) + u'-' + QString::number(b.lastLine);
        if (!b.problem.isEmpty())
            s += u',' + b.problem;
        if (!b.editable)
            s += u",ro"_s;
        return s + u']';
    };
    if (p.yaml)
        parts.append(block(u"yaml"_s, *p.yaml));
    if (p.logseq)
        parts.append(block(u"logseq"_s, *p.logseq));
    for (const PageProperty &i : p.items) {
        QString s = (i.source == PropertySource::Logseq ? u"~"_s : QString()) + i.key + u'=' + propertyTypeName(i.type)
                    + u':' + i.text;
        if (i.empty)
            s += u"!e"_s;
        if (!i.editable)
            s += u"!ro"_s;
        if (i.multiline)
            s += u"!ml"_s;
        if (i.linkDate)
            s += u"!ld"_s;
        s += u'@' + QString::number(i.line);
        parts.append(s);
    }
    return parts.join(u" | "_s);
}

/// The body after the frontmatter (the whole text without one).
QString bodyOf(const QString &content)
{
    const auto fm = findFrontmatter(content);
    return fm ? content.mid(fm->bodyStart) : content;
}

/// The frontmatter mapping without `skip`, canonical.
QString keysWithout(const QString &content, const QStringList &skip)
{
    QVariantMap data = parseFrontmatter(content).data;
    for (const QString &k : skip)
        data.remove(k);
    return stringifyYaml(data);
}

QVariant ll(qlonglong n)
{
    return QVariant(n);
}

QStringList list(std::initializer_list<QString> items)
{
    return QStringList(items);
}

} // namespace

class TestPageProps : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- types ----------------------------------------------------------------------------------
    void inferType_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("value"); ///< YAML: the value's source after "k: "; Logseq: the raw text
        QTest::addColumn<QString>("type");
        const auto yaml = [](const char *name, const QString &key, const QString &value, const QString &type) {
            QTest::newRow(name) << u"yaml"_s << key << value << type;
        };
        const auto logseq = [](const char *name, const QString &key, const QString &value, const QString &type) {
            QTest::newRow(name) << u"logseq"_s << key << value << type;
        };
        yaml("bool", u"gotowe"_s, u"true"_s, u"checkbox"_s);
        yaml("integer", u"liczba"_s, u"12"_s, u"number"_s);
        yaml("negative float", u"liczba"_s, u"-3.5"_s, u"number"_s);
        yaml("exponent", u"liczba"_s, u"1e3"_s, u"number"_s);
        yaml("quoted number", u"kod"_s, u"\"12\""_s, u"text"_s);
        yaml("date", u"termin"_s, u"2026-10-28"_s, u"date"_s);
        yaml("invalid date", u"termin"_s, u"2026-02-30"_s, u"text"_s);
        yaml("polish date", u"termin"_s, u"28.10.2026"_s, u"text"_s);
        yaml("datetime T", u"kiedy"_s, u"2026-10-28T09:30"_s, u"datetime"_s);
        yaml("datetime space", u"kiedy"_s, u"2026-10-28 09:30"_s, u"datetime"_s);
        yaml("datetime seconds", u"kiedy"_s, u"2026-10-28T09:30:00"_s, u"datetime"_s);
        yaml("flow list", u"lista"_s, u"[a, b]"_s, u"list"_s);
        yaml("empty list", u"lista"_s, u"[]"_s, u"list"_s);
        yaml("quoted link list", u"rel"_s, u"[\"[[Dom]]\"]"_s, u"list"_s);
        yaml("unquoted link", u"rel"_s, u"[[Dom]]"_s, u"list"_s);
        yaml("map", u"meta"_s, u"{a: 1}"_s, u"unsupported"_s);
        yaml("list of maps", u"meta"_s, u"[{a: 1}]"_s, u"unsupported"_s);
        yaml("nested list", u"meta"_s, u"[[a, b]]"_s, u"unsupported"_s);
        yaml("null", u"pusta"_s, QString(), u"text"_s);
        yaml("tags string", u"Tags"_s, u"\"#a #b\""_s, u"tags"_s);
        yaml("alias", u"alias"_s, u"Dom"_s, u"aliases"_s);
        yaml("empty tags", u"tags"_s, QString(), u"tags"_s);
        yaml("text", u"tytuł"_s, u"Zażółć gęślą jaźń"_s, u"text"_s);
        yaml("block scalar", u"opis"_s, u"|\n  a\n  b"_s, u"text"_s);
        logseq("bool", u"done"_s, u"true"_s, u"checkbox"_s);
        logseq("bool any case", u"done"_s, u"FALSE"_s, u"checkbox"_s);
        logseq("integer", u"n"_s, u"12"_s, u"number"_s);
        logseq("negative", u"n"_s, u"-0.5"_s, u"number"_s);
        logseq("decimal comma", u"n"_s, u"3,5"_s, u"text"_s);
        logseq("thousands", u"n"_s, u"1 200"_s, u"text"_s);
        logseq("date", u"termin"_s, u"2026-10-28"_s, u"date"_s);
        logseq("date link", u"termin"_s, u"[[2026-10-28]]"_s, u"date"_s);
        logseq("datetime", u"kiedy"_s, u"2026-10-28 09:30"_s, u"datetime"_s);
        logseq("links", u"rel"_s, u"[[A]], [[B]]"_s, u"list"_s);
        logseq("hash tags", u"rel"_s, u"#a, #b"_s, u"list"_s);
        logseq("text with comma", u"opis"_s, u"zwykły tekst, z przecinkiem"_s, u"text"_s);
        logseq("tags", u"tags"_s, u"a, b"_s, u"tags"_s);
        logseq("empty", u"status"_s, QString(), u"text"_s);
    }
    void inferType()
    {
        QFETCH(QString, source);
        QFETCH(QString, key);
        QFETCH(QString, value);
        QFETCH(QString, type);
        const PropertySource s = sourceOf(source);
        const QVariant v =
                s == PropertySource::Yaml ? parseYaml(QString(u"k: "_s + value)).value(u"k"_s) : QVariant(value);
        QCOMPARE(propertyTypeName(inferPropertyType(key, v, s)), type);
    }

    void emptyFallback()
    {
        QCOMPARE(inferPropertyType(u"x"_s, QVariant(), PropertySource::Yaml, PropertyType::Number),
                 PropertyType::Number);
        QCOMPARE(inferPropertyType(u"x"_s, QString(), PropertySource::Logseq, PropertyType::Date), PropertyType::Date);
        QCOMPARE(inferPropertyType(u"aliases"_s, QVariant(), PropertySource::Yaml, PropertyType::Date),
                 PropertyType::Aliases);
    }

    void names()
    {
        for (const PropertyType t : {PropertyType::Text, PropertyType::Number, PropertyType::Checkbox,
                                     PropertyType::Date, PropertyType::DateTime, PropertyType::List, PropertyType::Tags,
                                     PropertyType::Aliases, PropertyType::Unsupported})
            QCOMPARE(parsePropertyType(propertyTypeName(t)), t);
        QCOMPARE(parsePropertyType(u"DATE"), PropertyType::Date);
        QVERIFY(!parsePropertyType(u"kolor"));
        QCOMPARE(parsePropertySource(u"logseq"), PropertySource::Logseq);
        QVERIFY(!parsePropertySource(u"toml"));
        QCOMPARE(propertyEditStatusName(PropertyEditStatus::KeyExists), u"keyExists"_s);
        QCOMPARE(fixedPropertyType(u"Tag"), PropertyType::Tags);
        QCOMPARE(fixedPropertyType(u"ALIAS"), PropertyType::Aliases);
        QVERIFY(!fixedPropertyType(u"tagi"));
    }

    // ---- reading --------------------------------------------------------------------------------
    void read_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("expected");
        QTest::newRow("all yaml types")
                << u"---\ntytuł: Dom\nliczba: 3\ngotowe: true\ntermin: 2026-10-28\nkiedy: 2026-10-28T09:30\n"
                   u"tags: [a, \"#b\"]\naliases: Domek\nrel:\n  - [[Dom]]\n  - x\nbrulion: outline\n---\nTreść\n"_s
                << u"yaml[0-12] | tytuł=text:Dom@1 | liczba=number:3@2 | gotowe=checkbox:true@3 | "
                   u"termin=date:2026-10-28@4 | kiedy=datetime:2026-10-28T09:30@5 | tags=tags:a, b@6 | "
                   u"aliases=aliases:Domek@7 | rel=list:[[Dom]], x@8"_s;
        QTest::newRow("logseq") << u"tags:: a, #b\nstatus:: w toku\nilość:: 3\n\n- x\n"_s
                                << u"logseq[0-1] | ~tags=tags:a, b@0 | ~status=text:w toku@1"_s;
        QTest::newRow("both blocks") << u"---\na: 1\n---\n\nb:: [[X]], [[Y]]\nc:: [[2026-10-28]]\n- c\n"_s
                                     << u"yaml[0-2] | logseq[4-5] | a=number:1@1 | ~b=list:[[X]], [[Y]]@4 | "
                                        u"~c=date:[[2026-10-28]]!ld@5"_s;
        QTest::newRow("bom logseq") << u"\uFEFFtags:: a\n"_s << u"logseq[0-0] | ~tags=tags:a@0"_s;
        QTest::newRow("bom crlf yaml") << u"\uFEFF---\r\na: x\r\n---\r\nB"_s << u"yaml[0-2] | a=text:x@1"_s;
        QTest::newRow("dots") << u"---\na: 1\n...\nB"_s << u"yaml[0-2] | a=number:1@1"_s;
        QTest::newRow("comments") << u"---\n# komentarz\na: x # c\n---\n"_s << u"yaml[0-3] | a=text:x@2"_s;
        QTest::newRow("duplicates") << u"---\na: 1\nb: 2\na: 3\n---\n"_s
                                    << u"yaml[0-4] | a=number:3@1 | b=number:2@2"_s;
        QTest::newRow("indented root") << u"---\n  a: 1\n  b: 2\n---\n"_s
                                       << u"yaml[0-3,indented,ro] | a=number:1!ro@-1 | b=number:2!ro@-1"_s;
        QTest::newRow("horizontal rules") << u"---\nżółw\n---\n"_s << u"yaml[0-2,notMapping,ro]"_s;
        QTest::newRow("unclosed") << u"---\nżółw\n"_s << QString();
        QTest::newRow("comment before logseq") << u"%%komentarz%%\ntags:: a\n"_s << u"logseq[1-1] | ~tags=tags:a@1"_s;
        QTest::newRow("comment in value")
                << u"status:: w toku %%notatka%%\n"_s << u"logseq[0-0] | ~status=text:w toku@0"_s;
        QTest::newRow("polish key") << u"---\nźródło: Wikipedia\n---\n"_s << u"yaml[0-2] | źródło=text:Wikipedia@1"_s;
        QTest::newRow("multi-line") << u"---\nopis: |\n  a\n  b\n---\n"_s << u"yaml[0-4] | opis=text:a b!ml@1"_s;
        QTest::newRow("map") << u"---\nmeta:\n  a: 1\n---\n"_s << u"yaml[0-3] | meta=unsupported:{…}!ro@1"_s;
        QTest::newRow("empty values") << u"---\na:\ntags:\nl: []\n---\n"_s
                                      << u"yaml[0-4] | a=text:!e@1 | tags=tags:!e@2 | l=list:!e@3"_s;
        QTest::newRow("only the note mode") << u"---\nbrulion: document\n---\n"_s << u"yaml[0-2]"_s;
        QTest::newRow("shortest double") << u"---\nx: 2.50\n---\n"_s << u"yaml[0-2] | x=number:2.5@1"_s;
        QTest::newRow("logseq empty") << u"status::\n"_s << u"logseq[0-0] | ~status=text:!e@0"_s;
        QTest::newRow("logseq checkbox number")
                << u"done:: TRUE\nn:: -0.5\n"_s << u"logseq[0-1] | ~done=checkbox:TRUE@0 | ~n=number:-0.5@1"_s;
        QTest::newRow("row body without newline") << u"---\na: [x, y]\n---"_s << u"yaml[0-2] | a=list:x, y@1"_s;
        QTest::newRow("no properties") << u"# Tytuł\n\n- a\n"_s << QString();
    }
    void read()
    {
        QFETCH(QString, content);
        QFETCH(QString, expected);
        QCOMPARE(describe(readPageProperties(content)), expected);
    }

    void readValues()
    {
        const PageProperties p = readPageProperties(
                u"---\nn: 3\nd: 2.5\nok: false\nl:\n  - [[Dom]]\n  - 12\ntags: \"#a #b\"\n---\nrel:: [[A]], #b\n"_s);
        QCOMPARE(p.items.size(), 6);
        QCOMPARE(p.items[0].value, QVariant(ll(3)));
        QCOMPARE(p.items[1].value.toDouble(), 2.5);
        QCOMPARE(p.items[2].value, QVariant(false));
        QCOMPARE(p.items[3].items, list({u"[[Dom]]"_s, u"12"_s}));
        QCOMPARE(p.items[4].items, list({u"a"_s, u"b"_s}));
        QCOMPARE(p.items[5].items, list({u"[[A]]"_s, u"#b"_s}));
        QCOMPARE(p.find(PropertySource::Logseq, u"REL"_s), &p.items[5]);
        QCOMPARE(p.find(PropertySource::Yaml, u"N"_s), nullptr);
        QCOMPARE(pageListItems(p, PropertyType::Tags), list({u"a"_s, u"b"_s}));
    }

    void addTarget()
    {
        QCOMPARE(readPageProperties(u"# x"_s).addTarget(), PropertySource::Yaml);
        QVERIFY(readPageProperties(u"# x"_s).canAdd());
        QCOMPARE(readPageProperties(u"tags:: a\n"_s).addTarget(), PropertySource::Logseq);
        QCOMPARE(readPageProperties(u"---\nbrulion: outline\n---\ntags:: a\n"_s).addTarget(), PropertySource::Yaml);
        QVERIFY(!readPageProperties(u"---\n  a: 1\n---\n"_s).canAdd());
        QVERIFY(!readPageProperties(u"---\nżółw\n---\n"_s).canAdd());
    }

    // ---- setting --------------------------------------------------------------------------------
    void set_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<QString>("type");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<QString>("status");
        const auto yaml = [](const char *name, const QString &content, const QString &key, const QVariant &value,
                             const QString &type, const QString &expected, const QString &status = u"changed"_s) {
            QTest::newRow(name) << u"yaml"_s << content << key << value << type
                                << (status == u"changed" ? expected : content) << status;
        };
        const auto logseq = [](const char *name, const QString &content, const QString &key, const QVariant &value,
                               const QString &type, const QString &expected, const QString &status = u"changed"_s) {
            QTest::newRow(name) << u"logseq"_s << content << key << value << type
                                << (status == u"changed" ? expected : content) << status;
        };
        const QVariant none;
        // Replacing in place: the key part and a comment of the line stay as written.
        yaml("replace keeps key and comment", u"---\nStatus :  stary   # komentarz\nb: 1\n---\nx\n"_s, u"Status"_s,
             u"nowy"_s, u"text"_s, u"---\nStatus : nowy   # komentarz\nb: 1\n---\nx\n"_s);
        yaml("quoted key", u"---\n\"klucz: z dwukropkiem\": x\n---\n"_s, u"klucz: z dwukropkiem"_s, u"wartość"_s,
             u"text"_s, u"---\n\"klucz: z dwukropkiem\": wartość\n---\n"_s);
        yaml("y is a boolean elsewhere", u"---\na: x\n---\n"_s, u"a"_s, u"y"_s, u"text"_s, u"---\na: \"y\"\n---\n"_s);
        yaml("polish key and value", u"---\nźródło: Wikipedia\n---\n"_s, u"źródło"_s, u"Zażółć gęślą jaźń"_s, u"text"_s,
             u"---\nźródło: Zażółć gęślą jaźń\n---\n"_s);
        yaml("null value", u"---\na:\nb: 1\n---\n"_s, u"a"_s, u"x"_s, u"text"_s, u"---\na: x\nb: 1\n---\n"_s);
        yaml("block scalar replaced", u"---\nopis: |\n  stary\n  tekst\nb: 1\n---\n"_s, u"opis"_s, u"nowy"_s, u"text"_s,
             u"---\nopis: nowy\nb: 1\n---\n"_s);
        yaml("duplicates dropped", u"---\na: 1\nb: 2\na: 3\n---\n"_s, u"a"_s, ll(4), u"number"_s,
             u"---\na: 4\nb: 2\n---\n"_s);
        yaml("crlf replace", u"---\r\na: 1\r\n---\r\nB\r\n"_s, u"a"_s, ll(2), u"number"_s,
             u"---\r\na: 2\r\n---\r\nB\r\n"_s);
        // Appending a key before the closing delimiter.
        yaml("append", u"---\ntitle: Dom\n---\nBody\n"_s, u"status"_s, u"w toku"_s, u"text"_s,
             u"---\ntitle: Dom\nstatus: w toku\n---\nBody\n"_s);
        yaml("append crlf", u"---\r\ntitle: Ćma\r\n---\r\n"_s, u"a"_s, u"b"_s, u"text"_s,
             u"---\r\ntitle: Ćma\r\na: b\r\n---\r\n"_s);
        yaml("append mixed", u"---\ntitle: a\r\n---\nx\n"_s, u"b"_s, u"c"_s, u"text"_s,
             u"---\ntitle: a\r\nb: c\r\n---\nx\n"_s);
        yaml("append dots", u"--- \na: 1\n... \nBody"_s, u"b"_s, ll(2), u"number"_s, u"--- \na: 1\nb: 2\n... \nBody"_s);
        yaml("append to empty block", u"---\n---\nBody"_s, u"a"_s, u"x"_s, u"text"_s, u"---\na: x\n---\nBody"_s);
        yaml("append bom", u"\uFEFF---\ntags: x\n---\n"_s, u"a"_s, u"b"_s, u"text"_s,
             u"\uFEFF---\ntags: x\na: b\n---\n"_s);
        yaml("append at the end", u"---\na: 1\n---"_s, u"b"_s, u"x"_s, u"text"_s, u"---\na: 1\nb: x\n---"_s);
        yaml("append after a comment", u"---\n# tylko komentarz\n---\n"_s, u"a"_s, ll(1), u"number"_s,
             u"---\n# tylko komentarz\na: 1\n---\n"_s);
        yaml("nested key is another key", u"---\nmeta:\n  status: x\n---\n"_s, u"status"_s, u"nowy"_s, u"text"_s,
             u"---\nmeta:\n  status: x\nstatus: nowy\n---\n"_s);
        // A page without frontmatter gets one.
        yaml("create", u"# Tytuł\n\nTreść\n"_s, u"status"_s, u"w toku"_s, u"text"_s,
             u"---\nstatus: w toku\n---\n# Tytuł\n\nTreść\n"_s);
        yaml("create in empty note", QString(), u"a"_s, u"x"_s, u"text"_s, u"---\na: x\n---\n"_s);
        yaml("create bom", u"\uFEFFTreść"_s, u"a"_s, u"x"_s, u"text"_s, u"\uFEFF---\na: x\n---\nTreść"_s);
        yaml("create no final newline", u"x"_s, u"a"_s, u"b"_s, u"text"_s, u"---\na: b\n---\nx"_s);
        yaml("create lone cr", u"a\rb"_s, u"a"_s, u"x"_s, u"text"_s, u"---\ra: x\r---\ra\rb"_s);
        yaml("create crlf", u"- a\r\n"_s, u"tags"_s, QStringList{u"x"_s}, u"tags"_s,
             u"---\r\ntags:\r\n  - x\r\n---\r\n- a\r\n"_s);
        yaml("create before unclosed dashes", u"---\nżółw\n"_s, u"a"_s, u"x"_s, u"text"_s,
             u"---\na: x\n---\n---\nżółw\n"_s);
        // Quoting what another YAML reader would read differently.
        yaml("yes quoted", u"---\na: x\n---\n"_s, u"a"_s, u"yes"_s, u"text"_s, u"---\na: \"yes\"\n---\n"_s);
        yaml("true quoted", u"---\na: x\n---\n"_s, u"a"_s, u"true"_s, u"text"_s, u"---\na: \"true\"\n---\n"_s);
        yaml("number text quoted", u"---\na: x\n---\n"_s, u"a"_s, u"12"_s, u"text"_s, u"---\na: \"12\"\n---\n"_s);
        yaml("link quoted", u"---\na: x\n---\n"_s, u"a"_s, u"[[Dom]]"_s, u"text"_s, u"---\na: \"[[Dom]]\"\n---\n"_s);
        yaml("colon quoted", u"---\na: x\n---\n"_s, u"a"_s, u"a: b"_s, u"text"_s, u"---\na: \"a: b\"\n---\n"_s);
        yaml("hash quoted", u"---\na: x\n---\n"_s, u"a"_s, u"#x"_s, u"text"_s, u"---\na: \"#x\"\n---\n"_s);
        yaml("ends trimmed", u"---\na: x\n---\n"_s, u"a"_s, u"  lead  "_s, u"text"_s, u"---\na: lead\n---\n"_s);
        yaml("tak stays plain", u"---\na: x\n---\n"_s, u"a"_s, u"tak"_s, u"text"_s, u"---\na: tak\n---\n"_s);
        yaml("multi-line", u"---\na: x\nb: 1\n---\n"_s, u"a"_s, u"linia 1\nlinia 2"_s, u"text"_s,
             u"---\na: |-\n  linia 1\n  linia 2\nb: 1\n---\n"_s);
        yaml("multi-line crlf", u"---\r\na: x\r\n---\r\n"_s, u"a"_s, u"ż\r\nź"_s, u"text"_s,
             u"---\r\na: |-\r\n  ż\r\n  ź\r\n---\r\n"_s);
        yaml("empty text", u"---\na: x # c\n---\n"_s, u"a"_s, QString(), u"text"_s, u"---\na: # c\n---\n"_s);
        // A text committed as it was keeps its form (the field trims the ends of what it holds).
        yaml("block scalar committed unchanged", u"---\nnotatka: |\n  linia 1\n  linia 2\nkoszt: 5\n---\n"_s,
             u"notatka"_s, u"linia 1\nlinia 2\n"_s, u"text"_s, QString(), u"unchanged"_s);
        yaml("block scalar committed trimmed", u"---\nnotatka: |\r\n  żółw\r\n  jaźń\r\n---\r\n"_s, u"notatka"_s,
             u"żółw\njaźń"_s, u"text"_s, QString(), u"unchanged"_s);
        yaml("quoted blanks committed unchanged", u"---\ntytul: \" a \"\n---\n"_s, u"tytul"_s, u" a "_s, u"text"_s,
             QString(), u"unchanged"_s);
        yaml("quoted blanks committed trimmed", u"---\ntytul: \" a \"\n---\n"_s, u"tytul"_s, u"a"_s, u"text"_s,
             QString(), u"unchanged"_s);
        yaml("block scalar changed", u"---\nnotatka: |\n  linia 1\n---\n"_s, u"notatka"_s, u"linia 1\nlinia 2"_s,
             u"text"_s, u"---\nnotatka: |-\n  linia 1\n  linia 2\n---\n"_s);
        // Comment lines under a value written on its key line stay with the key.
        yaml("comment line under the value kept", u"---\nstatus: a\n  # uwaga do kosztu\nkoszt: 1\n---\n"_s,
             u"status"_s, u"b"_s, u"text"_s, u"---\nstatus: b\n  # uwaga do kosztu\nkoszt: 1\n---\n"_s);
        yaml("comment lines and blank kept crlf", u"---\r\nstatus: a # c\r\n  # ż\r\n\r\n  # ź\r\nb: 1\r\n---\r\n"_s,
             u"status"_s, u"Zażółć"_s, u"text"_s, u"---\r\nstatus: Zażółć # c\r\n  # ż\r\n\r\n  # ź\r\nb: 1\r\n---\r\n"_s);
        yaml("comment line under an empty value kept", u"---\nstatus:\n  # do uzupełnienia\n---\n"_s, u"status"_s,
             u"x"_s, u"text"_s, u"---\nstatus: x\n  # do uzupełnienia\n---\n"_s);
        yaml("comment line kept when emptied", u"---\nkoszt: 5\n  # netto\n---\n"_s, u"koszt"_s, QVariant(),
             u"number"_s, u"---\nkoszt:\n  # netto\n---\n"_s);
        yaml("comment line kept under a new list", u"---\nl: a\n  # uwaga\nb: 1\n---\n"_s, u"l"_s,
             list({u"a"_s, u"b"_s}), u"list"_s, u"---\nl:\n  - a\n  - b\n  # uwaga\nb: 1\n---\n"_s);
        yaml("comment line kept under a flow list", u"---\nl: [a]\n  # uwaga\n---\n"_s, u"l"_s,
             list({u"a"_s, u"b"_s}), u"list"_s, u"---\nl: [a, b]\n  # uwaga\n---\n"_s);
        yaml("block scalar lines are no comments", u"---\nopis: |\n  # nagłówek\n  tekst\n---\n"_s, u"opis"_s,
             u"nowy"_s, u"text"_s, u"---\nopis: nowy\n---\n"_s);
        // Numbers, checkboxes, dates.
        yaml("decimal comma", u"---\na: 1\n---\n"_s, u"a"_s, u"3,5"_s, u"number"_s, u"---\na: 3.5\n---\n"_s);
        yaml("thousands", u"---\na: 1\n---\n"_s, u"a"_s, u"1 200"_s, u"number"_s, u"---\na: 1200\n---\n"_s);
        yaml("not a number", u"---\na: 1\n---\n"_s, u"a"_s, u"abc"_s, u"number"_s, QString(), u"invalidValue"_s);
        yaml("same number", u"---\na: 3.5\n---\n"_s, u"a"_s, u"3,5"_s, u"number"_s, QString(), u"unchanged"_s);
        yaml("empty number", u"---\na: 1\n---\n"_s, u"a"_s, QString(), u"number"_s, u"---\na:\n---\n"_s);
        yaml("checkbox", u"---\na: false\n---\n"_s, u"a"_s, true, u"checkbox"_s, u"---\na: true\n---\n"_s);
        yaml("checkbox from text", u"---\n---\n"_s, u"gotowe"_s, u"false"_s, u"checkbox"_s,
             u"---\ngotowe: false\n---\n"_s);
        yaml("checkbox invalid", u"---\na: false\n---\n"_s, u"a"_s, u"może"_s, u"checkbox"_s, QString(),
             u"invalidValue"_s);
        yaml("date", u"---\nt: x\n---\n"_s, u"t"_s, u"2026-10-28"_s, u"date"_s, u"---\nt: 2026-10-28\n---\n"_s);
        yaml("not a date", u"---\nt: x\n---\n"_s, u"t"_s, u"28.10.2026"_s, u"date"_s, QString(), u"invalidValue"_s);
        yaml("datetime new", u"---\nt: x\n---\n"_s, u"t"_s, u"2026-10-28 09:30"_s, u"datetime"_s,
             u"---\nt: 2026-10-28T09:30\n---\n"_s);
        yaml("datetime keeps space", u"---\nt: 2026-10-27 08:00\n---\n"_s, u"t"_s, u"2026-10-28T09:30"_s, u"datetime"_s,
             u"---\nt: 2026-10-28 09:30\n---\n"_s);
        yaml("datetime keeps seconds", u"---\nt: 2026-10-27T08:00:15\n---\n"_s, u"t"_s, u"2026-10-28 09:30"_s,
             u"datetime"_s, u"---\nt: 2026-10-28T09:30:00\n---\n"_s);
        // Lists keep their style.
        yaml("block list indent 0", u"---\ntags:\n- a\n- b\n---\n"_s, u"tags"_s, list({u"a"_s, u"b"_s, u"c"_s}),
             u"tags"_s, u"---\ntags:\n- a\n- b\n- c\n---\n"_s);
        yaml("block list item kept", u"---\nlista:\n  - \"x\"  # pierwszy\n  - y\n---\n"_s, u"lista"_s,
             list({u"x"_s, u"z"_s}), u"list"_s, u"---\nlista:\n  - \"x\"  # pierwszy\n  - z\n---\n"_s);
        yaml("block list fewer", u"---\ntags:\n  - a\n  - b\n---\n"_s, u"tags"_s, list({u"b"_s}), u"tags"_s,
             u"---\ntags:\n  - b\n---\n"_s);
        yaml("block list unquoted link kept", u"---\nrel:\n  - [[Dom]]\n---\n"_s, u"rel"_s,
             list({u"[[Dom]]"_s, u"[[Ogród]]"_s}), u"list"_s, u"---\nrel:\n  - [[Dom]]\n  - \"[[Ogród]]\"\n---\n"_s);
        yaml("block list dash spacing", u"---\nl:\n  -   x\n---\n"_s, u"l"_s, list({u"x"_s, u"w"_s}), u"list"_s,
             u"---\nl:\n  -   x\n  -   w\n---\n"_s);
        yaml("flow list stays flow", u"---\nlista: [a, \"b\"]\n---\n"_s, u"lista"_s, list({u"a"_s, u"b"_s, u"c, d"_s}),
             u"list"_s, u"---\nlista: [a, \"b\", \"c, d\"]\n---\n"_s);
        yaml("flow list over lines", u"---\nlista: [a,\n  b]\nc: 1\n---\n"_s, u"lista"_s,
             list({u"a"_s, u"b"_s, u"c"_s}), u"list"_s, u"---\nlista: [a, b, c]\nc: 1\n---\n"_s);
        yaml("scalar tags become a block list", u"---\ntags: a, b\n---\n"_s, u"tags"_s, list({u"a"_s, u"b"_s, u"c"_s}),
             u"tags"_s, u"---\ntags:\n  - a\n  - b\n  - c\n---\n"_s);
        yaml("new list follows block style", u"---\nx:\n- a\n---\n"_s, u"l"_s, list({u"w"_s}), u"list"_s,
             u"---\nx:\n- a\nl:\n- w\n---\n"_s);
        yaml("empty list", u"---\nlista: [a]\n---\n"_s, u"lista"_s, QStringList(), u"list"_s,
             u"---\nlista: []\n---\n"_s);
        yaml("empty list keeps its type", u"---\nlista:\n---\n"_s, u"lista"_s, QStringList(), u"list"_s,
             u"---\nlista: []\n---\n"_s);
        yaml("tags lose hash", u"---\ntags: []\n---\n"_s, u"tags"_s, list({u"#dom"_s}), u"tags"_s,
             u"---\ntags:\n  - dom\n---\n"_s);
        yaml("polish items deduplicated", u"---\nlista:\n  - Łódź\n---\n"_s, u"lista"_s,
             list({u"Łódź"_s, u"żółw"_s, u"Żółw"_s}), u"list"_s, u"---\nlista:\n  - Łódź\n  - żółw\n---\n"_s);
        yaml("same list", u"---\ntags: [a, b]\n---\n"_s, u"tags"_s, list({u"a"_s, u"b"_s}), u"tags"_s, QString(),
             u"unchanged"_s);
        yaml("same text", u"---\na: \"x\"\n---\n"_s, u"a"_s, u"x"_s, u"text"_s, QString(), u"unchanged"_s);
        yaml("bad alias", u"---\naliases: []\n---\n"_s, u"aliases"_s, list({u"a|b"_s}), u"aliases"_s, QString(),
             u"invalidValue"_s);
        // Refused.
        yaml("indented root", u"---\n  a: 1\n---\n"_s, u"b"_s, u"x"_s, u"text"_s, QString(), u"unsafe"_s);
        yaml("not a mapping", u"---\nżółw\n---\n"_s, u"a"_s, u"x"_s, u"text"_s, QString(), u"unsafe"_s);
        yaml("reserved", u"---\na: 1\n---\n"_s, u"brulion"_s, u"outline"_s, u"text"_s, QString(), u"reserved"_s);
        yaml("reserved any case", u"---\na: 1\n---\n"_s, u"Brulion"_s, u"x"_s, u"text"_s, QString(), u"reserved"_s);
        yaml("key exists in another case", u"---\nStatus: a\n---\n"_s, u"status"_s, u"b"_s, u"text"_s, QString(),
             u"keyExists"_s);
        yaml("key exists in logseq lines", u"---\na: 1\n---\nstatus:: a\n"_s, u"Status"_s, u"b"_s, u"text"_s, QString(),
             u"keyExists"_s);
        yaml("empty key", u"---\na: 1\n---\n"_s, QString(), u"b"_s, u"text"_s, QString(), u"invalidKey"_s);
        yaml("key with line break", u"---\na: 1\n---\n"_s, u"a\nb"_s, u"b"_s, u"text"_s, QString(), u"invalidKey"_s);

        logseq("replace", u"status:: stary\n\n- x\n"_s, u"status"_s, u"nowy"_s, u"text"_s, u"status:: nowy\n\n- x\n"_s);
        logseq("fill empty", u"status::\n- x\n"_s, u"status"_s, u"w toku"_s, u"text"_s, u"status:: w toku\n- x\n"_s);
        logseq("set empty", u"status:: x\n- y\n"_s, u"status"_s, QString(), u"text"_s, u"status::\n- y\n"_s);
        logseq("case-insensitive key", u"Status:: x\n"_s, u"status"_s, u"y"_s, u"text"_s, u"Status:: y\n"_s);
        logseq("add", u"tags:: a\n\n- x\n"_s, u"status"_s, u"nowy"_s, u"text"_s, u"tags:: a\nstatus:: nowy\n\n- x\n"_s);
        logseq("add crlf", u"tags:: a\r\n- x\r\n"_s, u"status"_s, u"nowy"_s, u"text"_s,
               u"tags:: a\r\nstatus:: nowy\r\n- x\r\n"_s);
        logseq("add after last line", u"status:: a"_s, u"b"_s, u"c"_s, u"text"_s, u"status:: a\nb:: c"_s);
        logseq("bom", u"\uFEFFtags:: a\n"_s, u"tags"_s, list({u"a"_s, u"b"_s}), u"tags"_s, u"\uFEFFtags:: a, b\n"_s);
        logseq("links", u"rel:: [[a]], [[b]]\n"_s, u"rel"_s, list({u"[[a]]"_s, u"[[b]]"_s, u"c"_s}), u"list"_s,
               u"rel:: [[a]], [[b]], [[c]]\n"_s);
        logseq("tag with comma", u"tags:: a, b\n"_s, u"tags"_s, list({u"a"_s, u"b"_s, u"c, d"_s}), u"tags"_s,
               u"tags:: a, b, [[c, d]]\n"_s);
        logseq("hash style kept", u"tags:: #a, #b\n"_s, u"tags"_s, list({u"a"_s, u"b"_s, u"c"_s}), u"tags"_s,
               u"tags:: #a, #b, #c\n"_s);
        logseq("line break becomes space", u"opis:: x\n"_s, u"opis"_s, u"a\nb"_s, u"text"_s, u"opis:: a b\n"_s);
        logseq("date link kept", u"termin:: [[2026-10-27]]\n"_s, u"termin"_s, u"2026-10-28"_s, u"date"_s,
               u"termin:: [[2026-10-28]]\n"_s);
        logseq("number", u"ilosc:: 3\n"_s, u"ilosc"_s, u"3,5"_s, u"number"_s, u"ilosc:: 3.5\n"_s);
        logseq("checkbox", u"done:: false\n"_s, u"done"_s, true, u"checkbox"_s, u"done:: true\n"_s);
        logseq("same value", u"status:: x\n"_s, u"status"_s, u"x"_s, u"text"_s, QString(), u"unchanged"_s);
        logseq("polish key", u"tags:: a\n"_s, u"źródło"_s, u"x"_s, u"text"_s, QString(), u"invalidKey"_s);
        logseq("key in yaml", u"---\nstatus: a\n---\ntags:: x\n"_s, u"Status"_s, u"b"_s, u"text"_s, QString(),
               u"keyExists"_s);
        logseq("comment would swallow lines", u"a:: 1\nb:: 2\n\n- tekst %%x%%\n"_s, u"a"_s, u"%%"_s, u"text"_s,
               QString(), u"unsafe"_s);
        logseq("no logseq lines", u"# x\n"_s, u"a"_s, u"b"_s, u"text"_s, QString(), u"notFound"_s);
        // A value committed as it was keeps its form.
        logseq("number as written", u"ilosc:: 1.50\n"_s, u"ilosc"_s, u"1.50"_s, u"number"_s, QString(), u"unchanged"_s);
        logseq("number leading zeros", u"ilosc:: 007\n"_s, u"ilosc"_s, u"007"_s, u"number"_s, QString(),
               u"unchanged"_s);
        logseq("number plus", u"ilosc:: +5\n"_s, u"ilosc"_s, u"+5"_s, u"number"_s, QString(), u"unchanged"_s);
        logseq("number same value", u"ilosc:: 1.50\n"_s, u"ilosc"_s, 1.5, u"number"_s, QString(), u"unchanged"_s);
        logseq("number changed", u"ilosc:: 1.50\n"_s, u"ilosc"_s, u"1,75"_s, u"number"_s, u"ilosc:: 1.75\n"_s);
        logseq("text number becomes one", u"ilosc:: 1,5\n"_s, u"ilosc"_s, 1.5, u"number"_s, u"ilosc:: 1.5\n"_s);
        logseq("checkbox as written", u"done:: TRUE\n"_s, u"done"_s, true, u"checkbox"_s, QString(), u"unchanged"_s);
        // A list is links and tags: a text's parts become links.
        logseq("text becomes a list", u"autorzy:: a, b\n"_s, u"autorzy"_s, list({u"a"_s, u"b"_s}), u"list"_s,
               u"autorzy:: [[a]], [[b]]\n"_s);
        logseq("polish list", u"miejsca:: Łódź\n"_s, u"miejsca"_s, list({u"Łódź"_s, u"#żółw"_s}), u"list"_s,
               u"miejsca:: [[Łódź]], #żółw\n"_s);
        logseq("list item no link can hold", u"rel:: [[a]]\n"_s, u"rel"_s, list({u"[[a]]"_s, u"x]]y"_s}), u"list"_s,
               QString(), u"invalidValue"_s);
        (void)none;
    }
    void set()
    {
        QFETCH(QString, source);
        QFETCH(QString, content);
        QFETCH(QString, key);
        QFETCH(QVariant, value);
        QFETCH(QString, type);
        QFETCH(QString, expected);
        QFETCH(QString, status);
        const PropertyEditResult r = setPageProperty(content, sourceOf(source), key, value, typeOf(type));
        QCOMPARE(propertyEditStatusName(r.status), status);
        QCOMPARE(r.content, expected);
        if (r.status == PropertyEditStatus::Changed) {
            // Idempotent: setting it again changes nothing.
            const PageProperties props = readPageProperties(r.content);
            const PageProperty *p = props.find(sourceOf(source), key);
            QVERIFY(p);
            const PropertyEditResult again = setPageProperty(r.content, sourceOf(source), key, value, typeOf(type));
            QCOMPARE(propertyEditStatusName(again.status), u"unchanged"_s);
            if (source == u"yaml" && findFrontmatter(content)) {
                QCOMPARE(bodyOf(r.content), bodyOf(content));
                QCOMPARE(keysWithout(r.content, {key}), keysWithout(content, {key}));
            }
        }
    }

    // ---- removing -------------------------------------------------------------------------------
    void remove_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<QString>("status");
        const auto row = [](const char *name, const QString &source, const QString &content, const QString &key,
                            const QString &expected, const QString &status = u"changed"_s) {
            QTest::newRow(name) << source << content << key << (status == u"changed" ? expected : content) << status;
        };
        const QString y = u"yaml"_s;
        const QString l = u"logseq"_s;
        row("only key", y, u"---\nstatus: x\n---\nBody\n"_s, u"status"_s, u"Body\n"_s);
        row("among others", y, u"---\na: 1\nstatus: x\nb: 2\n---\n"_s, u"status"_s, u"---\na: 1\nb: 2\n---\n"_s);
        row("note mode stays", y, u"---\nbrulion: outline\nstatus: x\n---\nB"_s, u"status"_s,
            u"---\nbrulion: outline\n---\nB"_s);
        row("duplicates", y, u"---\na: 1\nx: 1\na: 2\n---\n"_s, u"a"_s, u"---\nx: 1\n---\n"_s);
        row("comment kept", y, u"---\n# k\na: 1\n---\nx"_s, u"a"_s, u"---\n# k\n---\nx"_s);
        row("list entry", y, u"---\ntags:\n  - a\n  - b\nc: 1\n---\n"_s, u"tags"_s, u"---\nc: 1\n---\n"_s);
        row("body starts with a block", y, u"---\na: 1\n---\n---\ntitle: x\n---\nbody\n"_s, u"a"_s,
            u"---\n---\n---\ntitle: x\n---\nbody\n"_s);
        row("bom", y, u"\uFEFF---\na: 1\n---\nŻ"_s, u"a"_s, u"\uFEFFŻ"_s);
        row("crlf", y, u"---\r\na: 1\r\nb: 2\r\n---\r\nB\r\n"_s, u"a"_s, u"---\r\nb: 2\r\n---\r\nB\r\n"_s);
        row("not found", y, u"---\na: 1\n---\n"_s, u"b"_s, QString(), u"notFound"_s);
        row("no frontmatter", y, u"# a\n"_s, u"b"_s, QString(), u"notFound"_s);
        row("unsafe", y, u"---\n  a: 1\n---\n"_s, u"a"_s, QString(), u"unsafe"_s);
        row("reserved", y, u"---\nbrulion: outline\n---\n"_s, u"brulion"_s, QString(), u"reserved"_s);
        row("logseq only line", l, u"tags:: x\n\n- a\n"_s, u"tags"_s, u"- a\n"_s);
        row("logseq among others", l, u"a:: 1\nb:: 2\n\n- x\n"_s, u"a"_s, u"b:: 2\n\n- x\n"_s);
        row("logseq last line", l, u"a:: 1\nb:: 2"_s, u"b"_s, u"a:: 1"_s);
        row("logseq after yaml", l, u"---\nx: 1\n---\ntags:: a\n"_s, u"tags"_s, u"---\nx: 1\n---\n"_s);
        row("logseq bom", l, u"\uFEFFtags:: x\n\nB"_s, u"tags"_s, u"\uFEFFB"_s);
        row("logseq any case", l, u"Tags:: x\n"_s, u"tags"_s, QString());
        row("logseq duplicates", l, u"a:: 1\nb:: 2\na:: 3\n"_s, u"a"_s, u"b:: 2\n"_s);
        row("logseq not found", l, u"a:: 1\n"_s, u"b"_s, QString(), u"notFound"_s);
        // The body must not start a frontmatter of its own (a rule and a heading would hide).
        row("logseq body starts with a rule", l, u"status:: a\n\n---\nfoo: bar\n---\ntekst\n"_s, u"status"_s,
            u"\n---\nfoo: bar\n---\ntekst\n"_s);
        row("logseq rule right after", l, u"status:: a\n---\nfoo: bar\n---\ntekst\n"_s, u"status"_s,
            u"\n---\nfoo: bar\n---\ntekst\n"_s);
        row("logseq rule after two lines crlf", l, u"a:: 1\r\nb:: 2\r\n---\r\nż: ź\r\n---\r\n"_s, u"b"_s,
            u"a:: 1\r\n---\r\nż: ź\r\n---\r\n"_s);
        row("logseq bom rule", l, u"\uFEFFstatus:: a\n---\nfoo: bar\n---\n"_s, u"status"_s,
            u"\uFEFF\n---\nfoo: bar\n---\n"_s);
        // Blank lines in front of `key::` lines do not end them: the next paragraph would become them.
        row("logseq next paragraph of properties", l, u"a:: 1\n\nb:: 2\n"_s, u"a"_s, QString(), u"unsafe"_s);
    }
    void remove()
    {
        QFETCH(QString, source);
        QFETCH(QString, content);
        QFETCH(QString, key);
        QFETCH(QString, expected);
        QFETCH(QString, status);
        const PropertyEditResult r = removePageProperty(content, sourceOf(source), key);
        QCOMPARE(propertyEditStatusName(r.status), status);
        QCOMPARE(r.content, expected);
    }

    // ---- adding ("Dodaj właściwość") ---------------------------------------------------------------
    void add_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("type");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<QString>("status");
        const auto row = [](const char *name, const QString &source, const QString &content, const QString &key,
                            const QString &type, const QString &expected, const QString &status = u"changed"_s) {
            QTest::newRow(name) << source << content << key << type << (status == u"changed" ? expected : content)
                                << status;
        };
        const QString y = u"yaml"_s;
        const QString l = u"logseq"_s;
        const QString page = u"---\nstatus: w toku # wybrany\ntags: [projekt, dom]\nźródło: Wikipedia\n---\n- a\n"_s;
        // An existing key is never overwritten by adding it.
        row("existing key", y, page, u"status"_s, u"text"_s, QString(), u"keyExists"_s);
        row("existing tags", y, page, u"tags"_s, u"tags"_s, QString(), u"keyExists"_s);
        row("existing in another case", y, page, u"STATUS"_s, u"text"_s, QString(), u"keyExists"_s);
        row("existing decomposed", y, page, u"z\u0301ro\u0301d\u0142o"_s, u"text"_s, QString(), u"keyExists"_s);
        row("existing in logseq lines", y, u"---\na: 1\n---\nstatus:: x\n"_s, u"Status"_s, u"text"_s, QString(),
            u"keyExists"_s);
        row("existing in yaml", l, u"---\na: 1\n---\nstatus:: x\n"_s, u"a"_s, u"text"_s, QString(), u"keyExists"_s);
        row("existing logseq", l, u"status:: x\n"_s, u"status"_s, u"text"_s, QString(), u"keyExists"_s);
        // New keys are written with an empty value.
        row("new", y, page, u"koszt"_s, u"number"_s,
            u"---\nstatus: w toku # wybrany\ntags: [projekt, dom]\nźródło: Wikipedia\nkoszt:\n---\n- a\n"_s);
        row("new list", y, u"---\na: 1\n---\n"_s, u"autorzy"_s, u"list"_s, u"---\na: 1\nautorzy: []\n---\n"_s);
        row("new frontmatter", y, u"- żółw\n"_s, u"status"_s, u"text"_s, u"---\nstatus:\n---\n- żółw\n"_s);
        row("into the note mode block", y, u"---\nbrulion: outline\n---\n- a\n"_s, u"status"_s, u"text"_s,
            u"---\nbrulion: outline\nstatus:\n---\n- a\n"_s);
        row("new logseq", l, u"status:: x\n"_s, u"termin"_s, u"date"_s, u"status:: x\ntermin::\n"_s);
        // Refused keys.
        row("reserved", y, page, u"brulion"_s, u"text"_s, QString(), u"reserved"_s);
        row("empty", y, page, u"  "_s, u"text"_s, QString(), u"invalidKey"_s);
        row("polish logseq key", l, u"status:: x\n"_s, u"źródło"_s, u"text"_s, QString(), u"invalidKey"_s);
    }
    void add()
    {
        QFETCH(QString, source);
        QFETCH(QString, content);
        QFETCH(QString, key);
        QFETCH(QString, type);
        QFETCH(QString, expected);
        QFETCH(QString, status);
        const PropertyType t = typeOf(type);
        const QVariant empty = t == PropertyType::List || t == PropertyType::Tags ? QVariant(QStringList()) : QVariant();
        const PropertyEditResult r = addPageProperty(content, sourceOf(source), key, empty, t);
        QCOMPARE(propertyEditStatusName(r.status), status);
        QCOMPARE(r.content, expected);
    }

    // ---- renaming -------------------------------------------------------------------------------
    void rename_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("oldKey");
        QTest::addColumn<QString>("newKey");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<QString>("status");
        const auto row = [](const char *name, const QString &source, const QString &content, const QString &oldKey,
                            const QString &newKey, const QString &expected, const QString &status = u"changed"_s) {
            QTest::newRow(name) << source << content << oldKey << newKey << (status == u"changed" ? expected : content)
                                << status;
        };
        const QString y = u"yaml"_s;
        const QString l = u"logseq"_s;
        row("value lines kept", y, u"---\na: 1\nb:\n  - x\n  -  \"y\" # c\n---\n"_s, u"b"_s, u"lista"_s,
            u"---\na: 1\nlista:\n  - x\n  -  \"y\" # c\n---\n"_s);
        row("rest of the line kept", y, u"---\nstatus :  x  # c\n---\n"_s, u"status"_s, u"stan"_s,
            u"---\nstan:  x  # c\n---\n"_s);
        row("quoted new key", y, u"---\na: 1\n---\n"_s, u"a"_s, u"a: b"_s, u"---\n\"a: b\": 1\n---\n"_s);
        row("polish key", y, u"---\nzrodlo: x\n---\n"_s, u"zrodlo"_s, u"źródło"_s, u"---\nźródło: x\n---\n"_s);
        row("case only", y, u"---\nstatus: 1\n---\n"_s, u"status"_s, u"Status"_s, u"---\nStatus: 1\n---\n"_s);
        row("duplicates", y, u"---\na: 1\nb: 2\na: 3\n---\n"_s, u"a"_s, u"c"_s, u"---\nb: 2\nc: 3\n---\n"_s);
        row("exists", y, u"---\na: 1\nb: 2\n---\n"_s, u"a"_s, u"B"_s, QString(), u"keyExists"_s);
        row("reserved", y, u"---\na: 1\n---\n"_s, u"a"_s, u"brulion"_s, QString(), u"reserved"_s);
        row("empty", y, u"---\na: 1\n---\n"_s, u"a"_s, QString(), QString(), u"invalidKey"_s);
        row("not found", y, u"---\na: 1\n---\n"_s, u"x"_s, u"y"_s, QString(), u"notFound"_s);
        row("same", y, u"---\na: 1\n---\n"_s, u"a"_s, u"a"_s, QString(), u"unchanged"_s);
        row("logseq", l, u"status:: x\n"_s, u"status"_s, u"stan"_s, u"stan:: x\n"_s);
        row("logseq polish", l, u"status:: x\n"_s, u"status"_s, u"źródło"_s, QString(), u"invalidKey"_s);
        row("logseq exists", l, u"status:: x\ntags:: a\n"_s, u"status"_s, u"Tags"_s, QString(), u"keyExists"_s);
        row("logseq case only", l, u"status:: x\n"_s, u"STATUS"_s, u"Status"_s, u"Status:: x\n"_s);
    }
    void rename()
    {
        QFETCH(QString, source);
        QFETCH(QString, content);
        QFETCH(QString, oldKey);
        QFETCH(QString, newKey);
        QFETCH(QString, expected);
        QFETCH(QString, status);
        const PropertyEditResult r = renamePageProperty(content, sourceOf(source), oldKey, newKey);
        QCOMPARE(propertyEditStatusName(r.status), status);
        QCOMPARE(r.content, expected);
    }

    // ---- the header's tags and aliases ------------------------------------------------------------
    void listItems_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("kind");
        QTest::addColumn<bool>("add");
        QTest::addColumn<QString>("item");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<QString>("status");
        const auto row = [](const char *name, const QString &content, const QString &kind, bool add,
                            const QString &item, const QString &expected, const QString &status = u"changed"_s) {
            QTest::newRow(name) << content << kind << add << item << (status == u"changed" ? expected : content)
                                << status;
        };
        const QString t = u"tags"_s;
        const QString a = u"aliases"_s;
        row("add to flow", u"---\ntags: [a, b]\n---\n"_s, t, true, u"c"_s, u"---\ntags: [a, b, c]\n---\n"_s);
        row("add to block", u"---\ntags:\n  - a\n---\n"_s, t, true, u"#b"_s, u"---\ntags:\n  - a\n  - b\n---\n"_s);
        row("add singular key", u"---\ntag: a\n---\n"_s, t, true, u"b"_s, u"---\ntag:\n  - a\n  - b\n---\n"_s);
        row("add missing key", u"---\ntitle: x\n---\n"_s, t, true, u"dom"_s, u"---\ntitle: x\ntags:\n  - dom\n---\n"_s);
        row("add logseq", u"tags:: a\n\n- x\n"_s, t, true, u"b"_s, u"tags:: a, b\n\n- x\n"_s);
        row("add alias to logseq page", u"tags:: a\n"_s, a, true, u"Dom"_s, u"tags:: a\naliases:: Dom\n"_s);
        row("add without properties", u"# Tytuł\n"_s, t, true, u"x"_s, u"---\ntags:\n  - x\n---\n# Tytuł\n"_s);
        row("add duplicate", u"---\ntags: [Żółw]\n---\n"_s, t, true, u"żółw"_s, QString(), u"unchanged"_s);
        row("add duplicate in logseq", u"---\na: 1\n---\ntags:: x\n"_s, t, true, u"#X"_s, QString(), u"unchanged"_s);
        row("add bad alias", u"---\naliases: []\n---\n"_s, a, true, u"a|b"_s, QString(), u"invalidValue"_s);
        row("add empty", u"---\ntags: []\n---\n"_s, t, true, u"#"_s, QString(), u"invalidValue"_s);
        row("add to unsafe", u"---\n  tags: [a]\n---\n"_s, t, true, u"b"_s, QString(), u"unsafe"_s);
        row("remove from both", u"---\ntags: [a, b]\n---\ntags:: a, c\n"_s, t, false, u"A"_s,
            u"---\ntags: [b]\n---\ntags:: c\n"_s);
        row("remove last", u"---\ntitle: x\ntags: [a]\n---\n"_s, t, false, u"#a"_s, u"---\ntitle: x\n---\n"_s);
        row("remove singular", u"---\ntag: a\n---\nB"_s, t, false, u"a"_s, u"B"_s);
        row("remove alias", u"---\naliases:\n  - Dom\n  - Domek\n---\n"_s, a, false, u"dom"_s,
            u"---\naliases:\n  - Domek\n---\n"_s);
        row("remove logseq only", u"tags:: a, b\n"_s, t, false, u"b"_s, u"tags:: a\n"_s);
        row("remove missing", u"---\ntags: [a]\n---\n"_s, t, false, u"x"_s, QString(), u"notFound"_s);
    }
    void listItems()
    {
        QFETCH(QString, content);
        QFETCH(QString, kind);
        QFETCH(bool, add);
        QFETCH(QString, item);
        QFETCH(QString, expected);
        QFETCH(QString, status);
        const PropertyType k = typeOf(kind);
        const PropertyEditResult r = add ? addPageListItem(content, k, item) : removePageListItem(content, k, item);
        QCOMPARE(propertyEditStatusName(r.status), status);
        QCOMPARE(r.content, expected);
    }

    // ---- conversions ----------------------------------------------------------------------------
    void convert_data()
    {
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<QString>("from");
        QTest::addColumn<QString>("to");
        QTest::addColumn<bool>("ok");
        QTest::addColumn<QVariant>("expected");
        QTest::newRow("text to list") << QVariant(u"a, [[b, c]], d"_s) << u"text"_s << u"list"_s << true
                                      << QVariant(list({u"a"_s, u"[[b, c]]"_s, u"d"_s}));
        QTest::newRow("list to text") << QVariant(list({u"a"_s, u"b"_s})) << u"list"_s << u"text"_s << true
                                      << QVariant(u"a, b"_s);
        QTest::newRow("list to tags") << QVariant(list({u"#a"_s, u"b"_s})) << u"list"_s << u"tags"_s << true
                                      << QVariant(list({u"a"_s, u"b"_s}));
        QTest::newRow("tags to aliases") << QVariant(list({u"a"_s})) << u"tags"_s << u"aliases"_s << true
                                         << QVariant(list({u"a"_s}));
        QTest::newRow("text to number") << QVariant(u"3,5"_s) << u"text"_s << u"number"_s << true << QVariant(3.5);
        QTest::newRow("text to number fails") << QVariant(u"abc"_s) << u"text"_s << u"number"_s << false << QVariant();
        QTest::newRow("number to text") << QVariant(ll(12)) << u"number"_s << u"text"_s << true << QVariant(u"12"_s);
        QTest::newRow("tak") << QVariant(u"Tak"_s) << u"text"_s << u"checkbox"_s << true << QVariant(true);
        QTest::newRow("nie") << QVariant(u"nie"_s) << u"text"_s << u"checkbox"_s << true << QVariant(false);
        QTest::newRow("maybe") << QVariant(u"może"_s) << u"text"_s << u"checkbox"_s << false << QVariant();
        QTest::newRow("checkbox to text")
                << QVariant(true) << u"checkbox"_s << u"text"_s << true << QVariant(u"true"_s);
        QTest::newRow("text to date") << QVariant(u"2026-10-28"_s) << u"text"_s << u"date"_s << true
                                      << QVariant(u"2026-10-28"_s);
        QTest::newRow("datetime text to date")
                << QVariant(u"2026-10-28T09:30"_s) << u"text"_s << u"date"_s << true << QVariant(u"2026-10-28"_s);
        QTest::newRow("date to datetime")
                << QVariant(u"2026-10-28"_s) << u"date"_s << u"datetime"_s << true << QVariant(u"2026-10-28T00:00"_s);
        QTest::newRow("datetime to date")
                << QVariant(u"2026-10-28 09:30"_s) << u"datetime"_s << u"date"_s << true << QVariant(u"2026-10-28"_s);
        QTest::newRow("text to date fails") << QVariant(u"jutro"_s) << u"text"_s << u"date"_s << false << QVariant();
        QTest::newRow("empty stays empty") << QVariant() << u"text"_s << u"number"_s << true << QVariant();
        QTest::newRow("empty to list") << QVariant() << u"text"_s << u"list"_s << true << QVariant(QStringList());
    }
    void convert()
    {
        QFETCH(QVariant, value);
        QFETCH(QString, from);
        QFETCH(QString, to);
        QFETCH(bool, ok);
        QFETCH(QVariant, expected);
        const auto r = convertPropertyValue(value, typeOf(from), typeOf(to));
        QCOMPARE(r.has_value(), ok);
        if (ok) {
            QCOMPARE(r->typeId(), expected.typeId());
            QCOMPARE(*r, expected);
        }
    }

    void parseNumber_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("ok");
        QTest::addColumn<bool>("integer");
        QTest::addColumn<double>("value");
        QTest::newRow("decimal comma") << u"3,5"_s << true << false << 3.5;
        QTest::newRow("thousands") << u"1 200"_s << true << true << 1200.0;
        QTest::newRow("both") << u"1 200,50"_s << true << false << 1200.5;
        QTest::newRow("negative") << u"-7"_s << true << true << -7.0;
        QTest::newRow("plus") << u"+2"_s << true << true << 2.0;
        QTest::newRow("exponent") << u"1e3"_s << true << true << 1000.0;
        QTest::newRow("dot thousands") << u"1.000,5"_s << true << false << 1000.5;
        QTest::newRow("comma thousands") << u"1,000.5"_s << true << false << 1000.5;
        QTest::newRow("nbsp") << u"12\u00A0345"_s << true << true << 12345.0;
        QTest::newRow("minus sign") << u"−5"_s << true << true << -5.0;
        QTest::newRow("leading dot") << u",25"_s << true << false << 0.25;
        QTest::newRow("big") << u"9007199254740993"_s << true << false << 9007199254740993.0;
        QTest::newRow("letters") << u"abc"_s << false << false << 0.0;
        QTest::newRow("empty") << QString() << false << false << 0.0;
        QTest::newRow("several commas") << u"1,2,3"_s << false << false << 0.0;
        QTest::newRow("nan") << u"NaN"_s << false << false << 0.0;
        QTest::newRow("infinity") << u"Infinity"_s << false << false << 0.0;
        QTest::newRow("two dots") << u"1.2.3"_s << false << false << 0.0;
        QTest::newRow("unit") << u"90 min"_s << false << false << 0.0;
    }
    void parseNumber()
    {
        QFETCH(QString, text);
        QFETCH(bool, ok);
        QFETCH(bool, integer);
        QFETCH(double, value);
        const auto n = parseNumberInput(text);
        QCOMPARE(n.has_value(), ok);
        if (!ok)
            return;
        QCOMPARE(n->typeId() == QMetaType::LongLong, integer);
        QCOMPARE(n->toDouble(), value);
    }

    void checkKey_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("problem");
        QTest::newRow("fine") << u"status"_s << u"yaml"_s << QString();
        QTest::newRow("empty") << QString() << u"yaml"_s << u"empty"_s;
        QTest::newRow("blank") << u"  "_s << u"yaml"_s << u"empty"_s;
        QTest::newRow("line break") << u"a\nb"_s << u"yaml"_s << u"lineBreak"_s;
        QTest::newRow("reserved") << u"brulion"_s << u"yaml"_s << u"reserved"_s;
        QTest::newRow("reserved case") << u"Brulion"_s << u"yaml"_s << u"reserved"_s;
        QTest::newRow("polish yaml") << u"źródło"_s << u"yaml"_s << QString();
        QTest::newRow("polish logseq") << u"źródło"_s << u"logseq"_s << u"logseqKey"_s;
        QTest::newRow("logseq chars") << u"my-key_2"_s << u"logseq"_s << QString();
        QTest::newRow("logseq space") << u"a b"_s << u"logseq"_s << u"logseqKey"_s;
        QTest::newRow("logseq brulion") << u"brulion"_s << u"logseq"_s << QString();
    }
    void checkKey()
    {
        QFETCH(QString, key);
        QFETCH(QString, source);
        QFETCH(QString, problem);
        QCOMPARE(checkPropertyKey(key, sourceOf(source)), problem);
    }

    void cleanItems()
    {
        QCOMPARE(cleanListItem(PropertyType::Tags, u" #dom "_s), u"dom"_s);
        QCOMPARE(cleanListItem(PropertyType::Tags, u"#[[mój dom]]"_s), u"mój dom"_s);
        QCOMPARE(cleanListItem(PropertyType::Tags, u"a\nb"_s), QString());
        QCOMPARE(cleanListItem(PropertyType::Aliases, u" Domek "_s), u"Domek"_s);
        for (const QString &bad : {u"[[x"_s, u"x]]"_s, u"a|b"_s, u"a#b"_s, u"a^b"_s})
            QCOMPARE(cleanListItem(PropertyType::Aliases, bad), QString());
    }

    // ---- round trips and malformed input -------------------------------------------------------------
    void roundTrip_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("content");
        const QString y = u"yaml"_s;
        int row = 0;
        const auto add = [&row](const QString &source, const QString &content) {
            QTest::addRow("%d %s", ++row, qPrintable(source)) << source << content;
        };
        add(y, u"---\na: 1\n---\nB\n"_s);
        add(y, u"# Tytuł\n"_s);
        add(y, QString());
        add(y, u"\uFEFFx"_s);
        add(y, u"x"_s);
        add(y, u"a\r\nb\r\n"_s);
        add(y, u"tags:: a\n\n- x\n"_s);
        add(y, u"---\na: 1\n# k\n---\n"_s);
        add(y, u"---\r\nźródło: \"Wikipedia\"  # skąd\r\n---\r\n"_s);
        add(u"logseq"_s, u"tags:: a\n\n- x\n"_s);
        add(u"logseq"_s, u"a:: 1"_s);
        add(u"logseq"_s, u"\uFEFFa:: 1\r\nb:: 2\r\n"_s);
    }
    void roundTrip()
    {
        QFETCH(QString, source);
        QFETCH(QString, content);
        const PropertySource s = sourceOf(source);
        for (const auto &[value, type] : {std::pair<QVariant, QString>{u"Zażółć"_s, u"text"_s},
                                          {QStringList{u"a"_s, u"b"_s}, u"list"_s},
                                          {ll(42), u"number"_s},
                                          {u"2026-10-28"_s, u"date"_s},
                                          {QVariant(), u"text"_s}}) {
            const PropertyEditResult added = setPageProperty(content, s, u"nowy"_s, value, typeOf(type));
            QCOMPARE(added.status, PropertyEditStatus::Changed);
            const PropertyEditResult removed = removePageProperty(added.content, s, u"nowy"_s);
            QCOMPARE(removed.status, PropertyEditStatus::Changed);
            QCOMPARE(removed.content, content);
        }
    }

    void randomNeverBreaks()
    {
        const QString chars = u"ab: -[]{}\"'|>#\n\t ,!&*%"_s;
        quint32 seed = 11;
        const auto next = [&seed] {
            seed = (seed * 1103515245u + 12345u) % 2147483648u;
            return seed;
        };
        for (int n = 0; n < 300; ++n) {
            QString yaml;
            for (int i = 0; i < 60; ++i)
                yaml += chars[next() % chars.size()];
            const QString content = (n % 3 == 0 ? QString() : u"---\n"_s) + yaml + u"\n---\nBody ż\n"_s;
            const QString body = bodyOf(content);
            const QList<PropertyEditResult> results{
                    setPageProperty(content, PropertySource::Yaml, u"status"_s, u"x"_s, PropertyType::Text),
                    setPageProperty(content, PropertySource::Yaml, u"a"_s, QStringList{u"p, q"_s}, PropertyType::List),
                    removePageProperty(content, PropertySource::Yaml, u"a"_s),
                    renamePageProperty(content, PropertySource::Yaml, u"a"_s, u"b"_s),
                    addPageListItem(content, PropertyType::Tags, u"dom"_s),
                    setPageProperty(content, PropertySource::Logseq, u"a"_s, u"y"_s, PropertyType::Text),
            };
            for (const PropertyEditResult &r : results) {
                if (r.status != PropertyEditStatus::Changed) {
                    QCOMPARE(r.content, content);
                    continue;
                }
                // Changed: the body after a frontmatter that was there stays byte-identical.
                if (findFrontmatter(content) && findFrontmatter(r.content))
                    QCOMPARE(bodyOf(r.content), body);
                QVERIFY(!r.content.isEmpty());
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestPageProps)
#include "tst_markdown_pageprops.moc"
