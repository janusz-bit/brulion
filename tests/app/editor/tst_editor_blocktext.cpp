// Pure helpers of the block editor (src/app/editor/blocktext.h): block shapes and the inline
// edit mapping, fences, token edits, completion triggers, slash commands, query/frontmatter data.
#include "blocktext.h"

#include "markdown/blockdocument.h"

#include <QTest>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

markdown::BlockEntry entryOf(const QString &content, int index = 0)
{
    const markdown::BlockDocument doc(content);
    return doc.at(index);
}

} // namespace

class TestBlockText : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void fences_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<bool>("isFence");
        QTest::addColumn<QString>("lang");
        QTest::addColumn<QString>("inner");
        QTest::addColumn<bool>("closed");
        QTest::newRow("calc") << u"```calc\nx = 1\ny = 2\n```"_s << true << u"calc"_s << u"x = 1\ny = 2"_s << true;
        QTest::newRow("tilde query") << u"~~~Query\ntype:tasks\n~~~"_s << true << u"query"_s << u"type:tasks"_s << true;
        QTest::newRow("unclosed") << u"```js\nlet a"_s << true << u"js"_s << u"let a"_s << false;
        QTest::newRow("empty closed") << u"```\n```"_s << true << QString() << QString() << true;
        QTest::newRow("empty inner line") << u"```calc\n\n```"_s << true << u"calc"_s << QString() << true;
        QTest::newRow("longer closing") << u"```\nx\n`````"_s << true << QString() << u"x"_s << true;
        QTest::newRow("shorter closing is content") << u"````\nx\n```"_s << true << QString() << u"x\n```"_s << false;
        QTest::newRow("two backticks") << u"``\nx\n``"_s << false << QString() << QString() << false;
        QTest::newRow("backtick in info") << u"``` a`b\nx\n```"_s << false << QString() << QString() << false;
        QTest::newRow("paragraph") << u"zwykły tekst"_s << false << QString() << QString() << false;
    }

    void fences()
    {
        QFETCH(QString, body);
        QFETCH(bool, isFence);
        QFETCH(QString, lang);
        QFETCH(QString, inner);
        QFETCH(bool, closed);
        const auto f = parseFence(body);
        QCOMPARE(f.has_value(), isFence);
        if (!f)
            return;
        QCOMPARE(f->lang, lang);
        QCOMPARE(f->inner, inner);
        QCOMPARE(f->closed, closed);
    }

    void shapes_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("index");
        QTest::addColumn<QString>("view");
        QTest::addColumn<QString>("editText");
        QTest::addColumn<int>("offset");
        QTest::addColumn<QString>("task");
        QTest::newRow("bullet") << u"- Zażółć gęślą jaźń\n"_s << 0 << u"text"_s << u"Zażółć gęślą jaźń"_s << 0 << QString();
        QTest::newRow("checkbox") << u"- [ ] Kupić mleko 📅 2026-10-16\n"_s << 0 << u"text"_s
                                  << u"Kupić mleko 📅 2026-10-16"_s << 4 << u"todo"_s;
        QTest::newRow("done") << u"- [x] Zrobione\n"_s << 0 << u"text"_s << u"Zrobione"_s << 4 << u"done"_s;
        QTest::newRow("doing") << u"- [/] W toku\n"_s << 0 << u"text"_s << u"W toku"_s << 4 << u"doing"_s;
        QTest::newRow("logseq keyword") << u"- TODO Zadzwonić\n"_s << 0 << u"text"_s << u"Zadzwonić"_s << 5 << u"todo"_s;
        QTest::newRow("empty task") << u"- [ ] \n"_s << 0 << u"text"_s << QString() << 4 << u"todo"_s;
        QTest::newRow("paragraph") << u"Akapit\ndruga linia\n"_s << 0 << u"text"_s << u"Akapit\ndruga linia"_s << 0 << QString();
        QTest::newRow("heading") << u"## Koszty\n"_s << 0 << u"heading"_s << u"## Koszty"_s << 0 << QString();
        QTest::newRow("list heading") << u"- ## Sekcja\n"_s << 0 << u"heading"_s << u"## Sekcja"_s << 0 << QString();
        QTest::newRow("code") << u"```js\nlet a = 1\n```\n"_s << 0 << u"code"_s << u"let a = 1"_s << 6 << QString();
        QTest::newRow("calc") << u"```calc\nx = 2\n```\n"_s << 0 << u"calc"_s << u"x = 2"_s << 8 << QString();
        QTest::newRow("query") << u"```query\ntag:dom\n```\n"_s << 0 << u"query"_s << u"tag:dom"_s << 9 << QString();
        QTest::newRow("lone fence") << u"```calc\n"_s << 0 << u"text"_s << u"```calc"_s << 0 << QString();
        QTest::newRow("lone fence item") << u"- ```js\n"_s << 0 << u"text"_s << u"```js"_s << 0 << QString();
        QTest::newRow("list calc") << u"- ```calc\n  a = 1\n  ```\n"_s << 0 << u"calc"_s << u"a = 1"_s << 8 << QString();
        QTest::newRow("table") << u"| a | b |\n|---|---|\n| 1 | 2 |\n"_s << 0 << u"table"_s
                               << u"| a | b |\n|---|---|\n| 1 | 2 |"_s << 0 << QString();
        QTest::newRow("list table") << u"- | a | b |\n  |---|---|\n  | 1 | 2 |\n"_s << 0 << u"table"_s
                                    << u"| a | b |\n|---|---|\n| 1 | 2 |"_s << 0 << QString();
        QTest::newRow("quote") << u"> Cytat\n> dalej\n"_s << 0 << u"quote"_s << u"> Cytat\n> dalej"_s << 0 << QString();
        QTest::newRow("callout") << u"> [!tip] Wskazówka\n> Treść\n"_s << 0 << u"callout"_s
                                 << u"> [!tip] Wskazówka\n> Treść"_s << 0 << QString();
        QTest::newRow("list quote") << u"- > cytat w liście\n"_s << 0 << u"quote"_s << u"> cytat w liście"_s << 0 << QString();
        QTest::newRow("rule") << u"a\n\n---\n\nb\n"_s << 1 << u"rule"_s << u"---"_s << 0 << QString();
        QTest::newRow("frontmatter") << u"---\ntitle: x\ntags: [a]\n---\n\n- a\n"_s << 0 << u"frontmatter"_s
                                     << u"title: x\ntags: [a]"_s << 4 << QString();
        QTest::newRow("page properties") << u"typ:: plan\n\n- a\n"_s << 0 << u"frontmatter"_s << u"typ:: plan"_s << 0
                                         << QString();
    }

    void shapes()
    {
        QFETCH(QString, content);
        QFETCH(int, index);
        QFETCH(QString, view);
        QFETCH(QString, editText);
        QFETCH(int, offset);
        QFETCH(QString, task);
        const markdown::BlockEntry e = entryOf(content, index);
        const BlockShape s = shapeOf(e);
        QCOMPARE(viewName(s.view), view);
        QCOMPARE(s.editText, editText);
        QCOMPARE(s.editOffset, offset);
        QCOMPARE(taskStatusName(s.task), task);
        // The mapping is consistent: hidden prefix + edit text + hidden suffix == body.
        QCOMPARE(e.body.left(s.editOffset) + s.editText + e.body.right(s.editSuffix), e.body);
        // Writing the same edit text back gives the same body.
        QCOMPARE(bodyWithEditText(e, s.editText), e.body);
    }

    void headingLevels()
    {
        QCOMPARE(shapeOf(entryOf(u"# Tytuł\n"_s)).headingLevel, 1);
        QCOMPARE(shapeOf(entryOf(u"- ### Trzeci\n"_s)).headingLevel, 3);
        QCOMPARE(shapeOf(entryOf(u"- #tag nie nagłówek\n"_s)).view, BlockView::Text);
        QCOMPARE(shapeOf(entryOf(u"- [ ] # Zadanie-nagłówek\n"_s)).view, BlockView::Heading);
    }

    void calloutTypes()
    {
        const BlockShape s = shapeOf(entryOf(u"> [!WARNING]- Uwaga\n> treść\n"_s));
        QCOMPARE(s.calloutType, u"warning"_s);
        QVERIFY(s.calloutFolded);
    }

    void bodyFromEditText()
    {
        // Task marker kept.
        const markdown::BlockEntry task = entryOf(u"- [ ] stare\n"_s);
        QCOMPARE(bodyWithEditText(task, u"nowe zadanie"_s), u"[ ] nowe zadanie"_s);
        // A marker without trailing space gets one when text follows.
        const markdown::BlockEntry bare = entryOf(u"- [ ]\n"_s);
        QCOMPARE(shapeOf(bare).editText, QString());
        QCOMPARE(bodyWithEditText(bare, u"tekst"_s), u"[ ] tekst"_s);
        // Fences kept on their own lines.
        const markdown::BlockEntry code = entryOf(u"```calc\na = 1\n```\n"_s);
        QCOMPARE(bodyWithEditText(code, u"a = 2\nb = a * 3"_s), u"```calc\na = 2\nb = a * 3\n```"_s);
        QCOMPARE(bodyWithEditText(code, QString()), u"```calc\n\n```"_s);
        const markdown::BlockEntry openFence = entryOf(u"```js\nlet a\n"_s);
        QCOMPARE(bodyWithEditText(openFence, u"x"_s), u"```js\nx"_s);
        const markdown::BlockEntry loneFence = entryOf(u"```js\n"_s); // text until Enter
        QCOMPARE(bodyWithEditText(loneFence, u"```js2"_s), u"```js2"_s);
        const markdown::BlockEntry fm = entryOf(u"---\na: 1\n---\n"_s);
        QCOMPARE(bodyWithEditText(fm, u"a: 2\nb: ąę"_s), u"---\na: 2\nb: ąę\n---"_s);
        // Plain bodies are replaced as they are.
        QCOMPARE(bodyWithEditText(entryOf(u"Akapit\n"_s), u"Inny"_s), u"Inny"_s);
    }

    void withoutMarker()
    {
        QCOMPARE(withoutTaskMarker(entryOf(u"- [x] Zrobione ✅ 2026-10-14\n"_s)), u"Zrobione ✅ 2026-10-14"_s);
        QCOMPARE(withoutTaskMarker(entryOf(u"- DONE x\n"_s)), u"x"_s);
        QCOMPARE(withoutTaskMarker(entryOf(u"- zwykły\n"_s)), u"zwykły"_s);
    }

    void plainText()
    {
        QCOMPARE(plainTextOf(entryOf(u"- [ ] Kupić **płytki** w [[Castorama|Castoramie]] 📅 2026-10-16 ^abc\n"_s)),
                 u"Kupić płytki w Castoramie"_s);
        QCOMPARE(plainTextOf(entryOf(u"## Koszty remontu\n"_s)), u"Koszty remontu"_s);
        QCOMPARE(plainTextOf(entryOf(u"- bardzo długi tekst bloku który się nie zmieści\n"_s), 20), u"bardzo długi tekst…"_s);
        QCOMPARE(plainTextOf(entryOf(u"> [!tip] Wskazówka\n> treść\n"_s)), u"Wskazówka treść"_s);
    }

    void queryData()
    {
        const QVariantMap q = queryInfo(u"type:tasks status:open,doing -tag:praca \"żółty kot\" pilne"_s);
        const QVariantList filters = q.value(u"filters"_s).toList();
        QCOMPARE(filters.size(), 3);
        QCOMPARE(filters[0].toMap().value(u"key"_s).toString(), u"type"_s);
        QCOMPARE(filters[1].toMap().value(u"value"_s).toString(), u"open,doing"_s);
        QVERIFY(filters[2].toMap().value(u"negated"_s).toBool());
        QCOMPARE(q.value(u"terms"_s).toStringList(), QStringList({u"żółty kot"_s, u"pilne"_s}));
    }

    void frontmatterData()
    {
        const QVariantList yaml = frontmatterProperties(u"---\ntytuł: Remont łazienki\ntags: [projekt, dom]\n---"_s);
        QCOMPARE(yaml.size(), 2);
        QCOMPARE(yaml[0].toMap().value(u"key"_s).toString(), u"tytuł"_s);
        QCOMPARE(yaml[0].toMap().value(u"value"_s).toString(), u"Remont łazienki"_s);
        QCOMPARE(yaml[1].toMap().value(u"value"_s).toString(), u"projekt, dom"_s);
        const QVariantList logseq = frontmatterProperties(u"typ:: plan\nautor:: [[Lem]]"_s);
        QCOMPARE(logseq.size(), 2);
        QCOMPARE(logseq[1].toMap().value(u"value"_s).toString(), u"[[Lem]]"_s);
    }

    void embeds()
    {
        const QList<Embed> e = embedsOf(u"Szkic ![[łazienka.png]] i ![opis](assets/plan.JPG) oraz ![[Strona]] ![[doc.pdf]]"_s);
        QCOMPARE(e.size(), 3);
        QCOMPARE(e[0].target, u"łazienka.png"_s);
        QVERIFY(e[0].image);
        QCOMPARE(e[1].target, u"assets/plan.JPG"_s);
        QVERIFY(e[1].image);
        QCOMPARE(e[2].target, u"doc.pdf"_s);
        QVERIFY(!e[2].image);
        QVERIFY(onlyEmbeds(u"![[a.png]]"_s));
        QVERIFY(onlyEmbeds(u"  ![[a.png]] ![[b.png]] "_s));
        QVERIFY(!onlyEmbeds(u"tekst ![[a.png]]"_s));
        QVERIFY(!onlyEmbeds(u"zwykły tekst"_s));
        QVERIFY(!onlyEmbeds(u"![[Strona]]"_s));
        // With transclusions: pages, sections and blocks count as embeds too.
        QVERIFY(onlyEmbeds(u"![[Strona]]"_s, true));
        QVERIFY(onlyEmbeds(u" ![[Budżet#Październik]] !((abc123)) ![[a.png]]"_s, true));
        QVERIFY(!onlyEmbeds(u"Zobacz ![[Budżet#Październik]]"_s, true));
        QVERIFY(!onlyEmbeds(u"[[Strona]]"_s, true));
        QVERIFY(!onlyEmbeds(u"((abc123))"_s, true));
    }

    void transclusions_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<QStringList>("expected"); // kind|target|heading|blockId|display|ref
        QTest::newRow("page") << u"![[Budżet domowy]]"_s << QStringList{u"page|Budżet domowy||||0"_s};
        QTest::newRow("section") << u"Zobacz ![[Budżet domowy#Październik]] tutaj"_s
                                 << QStringList{u"heading|Budżet domowy|Październik|||0"_s};
        QTest::newRow("block in page") << u"![[Kontakty#^hydr]]"_s << QStringList{u"block|Kontakty||hydr||0"_s};
        QTest::newRow("same page") << u"![[#Koszty]] ![[#^przyg]]"_s
                                   << QStringList{u"heading||Koszty|||0"_s, u"block|||przyg||0"_s};
        QTest::newRow("block ref embed") << u"!((hydr)) i ((tylko-odwołanie))"_s << QStringList{u"block|||hydr||1"_s};
        QTest::newRow("alias") << u"![[Zażółć gęślą|jaźń]]"_s << QStringList{u"page|Zażółć gęślą|||jaźń|0"_s};
        QTest::newRow("attachments are not transclusions") << u"![[plan.png]] ![[umowa.pdf]]"_s << QStringList{};
        QTest::newRow("links are not") << u"[[Strona]] ((abc)) [x](Strona.md)"_s << QStringList{};
        QTest::newRow("in code") << u"`![[Strona]]` i `!((abc))`"_s << QStringList{};
        QTest::newRow("empty") << u"![[]] ![[#]]"_s << QStringList{};
    }

    void transclusions()
    {
        QFETCH(QString, body);
        QFETCH(QStringList, expected);
        QStringList got;
        for (const Transclusion &t : transclusionsOf(body)) {
            const QString kind = t.kind == Transclusion::Kind::Page ? u"page"_s
                                 : t.kind == Transclusion::Kind::Heading ? u"heading"_s
                                                                         : u"block"_s;
            got.append(QStringList{kind, t.target, t.heading, t.blockId, t.display, t.blockRef ? u"1"_s : u"0"_s}.join(u'|'));
        }
        QCOMPARE(got, expected);
    }

    // --- tokens ---------------------------------------------------------------------------------

    void setTokens_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("token");
        QTest::addColumn<bool>("append");
        QTest::addColumn<QString>("expected");
        const int due = int(markdown::TokenKind::Due);
        const int rem = int(markdown::TokenKind::Reminder);
        const int rec = int(markdown::TokenKind::Recurrence);
        QTest::newRow("append due") << u"[ ] Kupić płytki"_s << due << u"📅 2026-10-16"_s << false
                                    << u"[ ] Kupić płytki 📅 2026-10-16"_s;
        QTest::newRow("replace due") << u"[ ] Kupić 📅 2026-10-16 #zakupy"_s << due << u"📅 2026-10-20"_s << false
                                     << u"[ ] Kupić 📅 2026-10-20 #zakupy"_s;
        QTest::newRow("remove due") << u"[ ] Kupić 📅 2026-10-16 #zakupy"_s << due << QString() << false
                                    << u"[ ] Kupić #zakupy"_s;
        QTest::newRow("before block id") << u"Zadzwonić ^abc1"_s << rem << u"⏰ 2026-10-15 18:00"_s << true
                                         << u"Zadzwonić ⏰ 2026-10-15 18:00 ^abc1"_s;
        QTest::newRow("second reminder") << u"x ⏰ 2026-10-15 09:00"_s << rem << u"⏰! 2026-10-16 07:00"_s << true
                                         << u"x ⏰ 2026-10-15 09:00 ⏰! 2026-10-16 07:00"_s;
        QTest::newRow("first line only") << u"pierwsza\ndruga linia"_s << rec << u"🔁 every week"_s << false
                                         << u"pierwsza 🔁 every week\ndruga linia"_s;
        QTest::newRow("empty body") << QString() << due << u"📅 2026-10-16"_s << false << u"📅 2026-10-16"_s;
        QTest::newRow("trailing spaces") << u"tekst   "_s << due << u"📅 2026-10-16"_s << false << u"tekst 📅 2026-10-16   "_s;
        QTest::newRow("not inside code") << u"`📅 2026-10-16` x"_s << due << u"📅 2026-10-17"_s << false
                                         << u"`📅 2026-10-16` x 📅 2026-10-17"_s;
        QTest::newRow("replace recurrence") << u"x 🔁 every day 📅 2026-10-16"_s << rec << u"🔁 every week on Monday"_s
                                            << false << u"x 🔁 every week on Monday 📅 2026-10-16"_s;
    }

    void setTokens()
    {
        QFETCH(QString, body);
        QFETCH(int, kind);
        QFETCH(QString, token);
        QFETCH(bool, append);
        QFETCH(QString, expected);
        QCOMPARE(setFirstLineToken(body, markdown::TokenKind(kind), token, append), expected);
    }

    void replaceRanges()
    {
        const QString body = u"a ⏰ 2026-10-15 09:00 b"_s;
        QCOMPARE(replaceTokenRange(body, 2, 20, u"⏰! 2026-10-16 07:30"_s), u"a ⏰! 2026-10-16 07:30 b"_s);
        QCOMPARE(replaceTokenRange(body, 2, 20, QString()), u"a b"_s);
        QCOMPARE(replaceTokenRange(body, 5, 2, u"x"_s), body);
        QCOMPARE(replaceTokenRange(body, 2, 999, u"x"_s), body);
    }

    void insertTokens()
    {
        int cursor = -1;
        QCOMPARE(insertTokenAt(u"Kupić mleko"_s, 11, u"📅 2026-10-16"_s, &cursor), u"Kupić mleko 📅 2026-10-16 "_s);
        QCOMPARE(cursor, 26);
        QCOMPARE(insertTokenAt(u"Kupić  mleko"_s, 6, u"⏫"_s, &cursor), u"Kupić ⏫ mleko"_s);
        QCOMPARE(cursor, 7);
        QCOMPARE(insertTokenAt(QString(), 0, u"⏰ 2026-10-15"_s, &cursor), u"⏰ 2026-10-15 "_s);
        QCOMPARE(insertTokenAt(u"x"_s, 99, u"y"_s, &cursor), u"x y "_s);
    }

    // --- completion triggers ----------------------------------------------------------------------

    void triggers_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("cursor");
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("filter");
        QTest::addColumn<int>("from");
        QTest::addColumn<int>("to");
        QTest::addColumn<QString>("token");
        QTest::addColumn<QString>("closing");
        QTest::newRow("none") << u"zwykły tekst"_s << 5 << QString() << QString() << -1 << -1 << QString() << QString();
        QTest::newRow("pages") << u"zobacz [[Rem"_s << 12 << u"pages"_s << u"Rem"_s << 9 << 12 << u"[["_s << QString();
        QTest::newRow("pages polish") << u"[[Łaz"_s << 5 << u"pages"_s << u"Łaz"_s << 2 << 5 << u"[["_s << QString();
        QTest::newRow("pages empty") << u"[["_s << 2 << u"pages"_s << QString() << 2 << 2 << u"[["_s << QString();
        QTest::newRow("pages closed") << u"[[Rem]] dalej"_s << 4 << u"pages"_s << u"Re"_s << 2 << 5 << u"[["_s << u"]]"_s;
        QTest::newRow("pages alias") << u"[[Do|m]]"_s << 3 << u"pages"_s << u"D"_s << 2 << 4 << u"[["_s << u"|"_s;
        QTest::newRow("embed") << u"![[szk"_s << 6 << u"pages"_s << u"szk"_s << 3 << 6 << u"![["_s << QString();
        QTest::newRow("link done") << u"[[Strona]] x"_s << 12 << QString() << QString() << -1 << -1 << QString() << QString();
        QTest::newRow("blocks") << u"zobacz ((płyt"_s << 13 << u"blocks"_s << u"płyt"_s << 9 << 13 << u"(("_s << QString();
        QTest::newRow("blocks after word") << u"f(("_s << 3 << QString() << QString() << -1 << -1 << QString() << QString();
        QTest::newRow("blocks closed") << u"((ab))"_s << 3 << u"blocks"_s << u"a"_s << 2 << 4 << u"(("_s << u"))"_s;
        QTest::newRow("tags") << u"Kupić #zak"_s << 10 << u"tags"_s << u"zak"_s << 7 << 10 << u"#"_s << QString();
        QTest::newRow("tags polish") << u"x #żółw"_s << 7 << u"tags"_s << u"żółw"_s << 3 << 7 << u"#"_s << QString();
        QTest::newRow("tags middle") << u"x #zakupy y"_s << 5 << u"tags"_s << u"za"_s << 3 << 9 << u"#"_s << QString();
        QTest::newRow("tags bare at start") << u"#"_s << 1 << QString() << QString() << -1 << -1 << QString() << QString();
        QTest::newRow("tags at start typed") << u"#pr"_s << 3 << u"tags"_s << u"pr"_s << 1 << 3 << u"#"_s << QString();
        QTest::newRow("hash in word") << u"C#x"_s << 3 << QString() << QString() << -1 << -1 << QString() << QString();
        QTest::newRow("heading") << u"# Ty"_s << 4 << QString() << QString() << -1 << -1 << QString() << QString();
        QTest::newRow("slash") << u"/prz"_s << 4 << u"slash"_s << u"prz"_s << 0 << 4 << u"/"_s << QString();
        QTest::newRow("slash after text") << u"tekst /tab"_s << 10 << u"slash"_s << u"tab"_s << 6 << 10 << u"/"_s << QString();
        QTest::newRow("slash in url") << u"http://x"_s << 8 << QString() << QString() << -1 << -1 << QString() << QString();
        QTest::newRow("slash path") << u"a/b"_s << 3 << QString() << QString() << -1 << -1 << QString() << QString();
        QTest::newRow("date reminder") << u"Zadzwonić ⏰ jutro o 9"_s << 21 << u"date"_s << u"jutro o 9"_s << 12 << 21 << u"⏰"_s
                                       << QString();
        QTest::newRow("date alarm") << u"x ⏰! pt"_s << 7 << u"date"_s << u"pt"_s << 5 << 7 << u"⏰!"_s << QString();
        QTest::newRow("date due empty") << u"x 📅 "_s << 5 << u"date"_s << QString() << 5 << 5 << u"📅"_s << QString();
        QTest::newRow("date canonical") << u"x 📅 2026-10-15"_s << 15 << QString() << QString() << -1 << -1 << QString()
                                        << QString();
        QTest::newRow("at date") << u"spotkanie @jutro"_s << 16 << u"date"_s << u"jutro"_s << 10 << 16 << u"@"_s << QString();
        QTest::newRow("email") << u"jan@example.com"_s << 15 << QString() << QString() << -1 << -1 << QString() << QString();
        QTest::newRow("inside code") << u"`[[x`"_s << 4 << QString() << QString() << -1 << -1 << QString() << QString();
        QTest::newRow("second line") << u"pierwsza\n[[dru"_s << 14 << u"pages"_s << u"dru"_s << 11 << 14 << u"[["_s << QString();
        QTest::newRow("bad cursor") << u"[[x"_s << 99 << QString() << QString() << -1 << -1 << QString() << QString();
    }

    void triggers()
    {
        QFETCH(QString, text);
        QFETCH(int, cursor);
        QFETCH(QString, mode);
        QFETCH(QString, filter);
        QFETCH(int, from);
        QFETCH(int, to);
        QFETCH(QString, token);
        QFETCH(QString, closing);
        const CompletionTrigger t = completionTriggerAt(text, cursor);
        QCOMPARE(t.mode, mode);
        if (mode.isEmpty())
            return;
        QCOMPARE(t.filter, filter);
        QCOMPARE(t.from, from);
        QCOMPARE(t.to, to);
        QCOMPARE(t.token, token);
        QCOMPARE(t.closing, closing);
        QCOMPARE(CompletionTrigger::fromMap(t.toMap()).toMap(), t.toMap());
    }

    // --- slash commands ---------------------------------------------------------------------------

    void slash_data()
    {
        QTest::addColumn<QString>("filter");
        QTest::addColumn<QString>("first");
        QTest::newRow("empty") << QString() << u"task"_s;
        QTest::newRow("polish") << u"przyp"_s << u"reminder"_s;
        QTest::newRow("without diacritics") << u"naglowek"_s << u"h1"_s;
        QTest::newRow("diacritics typed") << u"Nagłówek 2"_s << u"h2"_s;
        QTest::newRow("english") << u"remind"_s << u"reminder"_s;
        QTest::newRow("table") << u"tab"_s << u"table"_s;
        QTest::newRow("tabela") << u"tabe"_s << u"table"_s;
        QTest::newRow("calc") << u"oblicz"_s << u"calc"_s;
        QTest::newRow("powtarzaj") << u"powt"_s << u"recurrence"_s;
        QTest::newRow("termin") << u"termin"_s << u"due"_s;
        QTest::newRow("zaplanuj") << u"zapl"_s << u"scheduled"_s;
        QTest::newRow("alarm") << u"alarm"_s << u"alarm"_s;
        QTest::newRow("keyword") << u"budzik"_s << u"alarm"_s;
        QTest::newRow("szablon") << u"szabl"_s << u"template"_s;
        QTest::newRow("osadz") << u"osadz"_s << u"embed"_s;
        QTest::newRow("obraz") << u"obraz"_s << u"image"_s;
        QTest::newRow("zdjęcie") << u"zdjecie"_s << u"image"_s;
        QTest::newRow("image") << u"image"_s << u"image"_s;
    }

    void slash()
    {
        QFETCH(QString, filter);
        QFETCH(QString, first);
        const QList<const SlashCommand *> found = filterSlashCommands(filter);
        QVERIFY(!found.isEmpty());
        QCOMPARE(found.first()->id, first);
    }

    void slashNoMatch()
    {
        QVERIFY(filterSlashCommands(u"xyzzy"_s).isEmpty());
        QCOMPARE(filterSlashCommands(QString()).size(), slashCommands().size());
        QVERIFY(slashCommand(u"calc"_s));
        QVERIFY(!slashCommand(u"nope"_s));
        for (const SlashCommand &c : slashCommands()) {
            QVERIFY(!c.namePl.isEmpty());
            QVERIFY(!c.nameEn.isEmpty());
            QVERIFY(!c.icon.isEmpty());
        }
    }

    void slashTemplates()
    {
        int cursor = -1;
        QCOMPARE(slashTemplate(u"calc", false, &cursor), u"```calc\n\n```"_s);
        QCOMPARE(cursor, 8);
        QVERIFY(slashTemplate(u"table", false, &cursor).contains(u"Kolumna 1"_s));
        QVERIFY(slashTemplate(u"table", true, &cursor).contains(u"Column 1"_s));
        QVERIFY(slashTemplate(u"reminder", false, &cursor).isEmpty());
    }

    void vaultConfig()
    {
        const VaultConfig c = vaultConfigFromMap({{u"journalFileFormat"_s, u"dd.MM.yyyy"_s},
                                                   {u"defaultReminderTime"_s, u"08:30"_s},
                                                   {u"weekStartsOn"_s, 7}});
        QCOMPARE(c.journalFileFormat, u"dd.MM.yyyy"_s);
        QCOMPARE(c.defaultReminderTime, QTime(8, 30));
        QCOMPARE(c.weekStartsOn, Qt::Sunday);
        QCOMPARE(vaultConfigFromMap({}).journalsFolder, u"journals"_s);
    }

    // --- text coming back from a QML TextEdit -----------------------------------------------------

    void restoreLineEndings_data()
    {
        // TextEdit (QTextDocument) reports every "\r\n" / "\r" as "\n": untouched lines keep their
        // terminators, line breaks typed in the edited range get the document's.
        QTest::addColumn<QString>("original");
        QTest::addColumn<QString>("edited");
        QTest::addColumn<QString>("eol");
        QTest::addColumn<QString>("expected");
        QTest::newRow("unchanged crlf") << u"- a\r\n- b\r\n"_s << u"- a\n- b\n"_s << u"\r\n"_s << u"- a\r\n- b\r\n"_s;
        QTest::newRow("typed in a line") << u"- a\r\n- b\r\n\r\nKoniec\r\n"_s << u"- aąę\n- b\n\nKoniec\n"_s
                                         << u"\r\n"_s << u"- aąę\r\n- b\r\n\r\nKoniec\r\n"_s;
        QTest::newRow("new line") << u"- a\r\n- b\r\n"_s << u"- a\n- nowy\n- b\n"_s << u"\r\n"_s
                                  << u"- a\r\n- nowy\r\n- b\r\n"_s;
        QTest::newRow("removed line") << u"- a\r\n- b\r\n- c\r\n"_s << u"- a\n- c\n"_s << u"\r\n"_s << u"- a\r\n- c\r\n"_s;
        QTest::newRow("mixed endings kept") << u"a\r\nb\nc\rd"_s << u"a\nbX\nc\nd"_s << u"\n"_s << u"a\r\nbX\nc\rd"_s;
        QTest::newRow("join lines") << u"ab\r\ncd\r\n"_s << u"abcd\n"_s << u"\r\n"_s << u"abcd\r\n"_s;
        QTest::newRow("lf only") << u"- źdźbło\n"_s << u"- źdźbło!\n"_s << u"\n"_s << u"- źdźbło!\n"_s;
        QTest::newRow("bom and emoji") << u"﻿- 📅 jutro\r\n"_s << u"﻿- 📅 jutro 🔁\n"_s << u"\r\n"_s
                                       << u"﻿- 📅 jutro 🔁\r\n"_s;
        QTest::newRow("all replaced") << u"x\r\n"_s << u"y\nz"_s << u"\r\n"_s << u"y\r\nz"_s;
        QTest::newRow("empty") << QString() << u"a\nb"_s << u"\r\n"_s << u"a\r\nb"_s;
    }

    void restoreLineEndings()
    {
        QFETCH(QString, original);
        QFETCH(QString, edited);
        QFETCH(QString, eol);
        QFETCH(QString, expected);
        QCOMPARE(brulion::editor::restoreLineEndings(original, edited, eol), expected);
    }

    void textDeltas_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::newRow("insert") << u"- a\n- b\n"_s << u"- aąę\n- b\n"_s;
        QTest::newRow("remove") << u"- a\n- b\n- c\n"_s << u"- a\n- c\n"_s;
        QTest::newRow("replace") << u"zażółć gęślą jaźń"_s << u"zażółć 😀 jaźń"_s;
        QTest::newRow("same") << u"x"_s << u"x"_s;
        QTest::newRow("from empty") << QString() << u"- nowa\n"_s;
        QTest::newRow("to empty") << u"- stara\n"_s << QString();
        QTest::newRow("repeated chars") << u"aaaa"_s << u"aaaaaa"_s;
    }

    void textDeltas()
    {
        QFETCH(QString, before);
        QFETCH(QString, after);
        const TextDelta d = TextDelta::between(before, after);
        QCOMPARE(d.apply(before), std::optional<QString>(after));
        QCOMPARE(d.revert(after), std::optional<QString>(before));
        // Only the changed part is stored.
        QVERIFY(d.removed.size() + d.inserted.size() <= std::abs(before.size() - after.size()) + 4);
        // A text that is not the expected one is refused (never patched into garbage).
        if (before != after) {
            QVERIFY(!d.apply(after + u"?"_s).has_value());
            QVERIFY(!d.revert(before + u"?"_s).has_value());
        }
        // Composition: two consecutive deltas as one.
        const QString third = after + u"++"_s;
        const TextDelta second = TextDelta::between(after, third);
        const std::optional<TextDelta> both = TextDelta::compose(d, second, after);
        QVERIFY(both.has_value());
        QCOMPARE(both->apply(before), std::optional<QString>(third));
        QCOMPARE(both->revert(third), std::optional<QString>(before));
    }
};

QTEST_MAIN(TestBlockText)
#include "tst_editor_blocktext.moc"
