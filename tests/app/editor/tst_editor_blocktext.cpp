// Pure helpers of the block editor (src/app/editor/blocktext.h): block shapes and the inline
// edit mapping, fences, token edits, completion triggers, slash commands, query/frontmatter data.
#include "blocktext.h"

#include "dates/local.h"
#include "markdown/blockdocument.h"
#include "markdown/parse.h"

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

/// The blocks of a document as a reload sees them: kind, depth, block id and body of each.
QStringList blocksOf(const markdown::BlockDocument &doc)
{
    QStringList out;
    for (int i = 0; i < doc.count(); ++i) {
        const markdown::BlockEntry &e = doc.at(i);
        out.append(kindName(e.kind) + u'/' + QString::number(e.depth) + u'/' + e.blockId + u'/' + e.body);
    }
    return out;
}

/// What the index takes from a note: tasks, block ids, links, tags and reminders.
QString indexedOf(const QString &content)
{
    const ParsedNote n = markdown::parseNote(u"Notatki/Strona.md"_s, content);
    QStringList tasks;
    for (const Task &t : n.tasks)
        tasks.append(t.text);
    QStringList ids = n.blockIds.keys();
    ids.sort();
    QStringList links;
    for (const Link &l : n.links)
        links.append(l.target);
    return u"tasks="_s + tasks.join(u'|') + u" ids="_s + ids.join(u',') + u" links="_s + links.join(u',') + u" tags="_s
           + n.tags.join(u',') + u" reminders="_s + QString::number(n.reminders.size());
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
        QTest::newRow("tab-indented fence") << u"\t```js\n\tx\n\t```"_s << true << u"js"_s << u"\tx"_s << true;
        QTest::newRow("shorter closing is content") << u"````\nx\n```"_s << true << QString() << u"x\n```"_s << false;
        QTest::newRow("two backticks") << u"``\nx\n``"_s << false << QString() << QString() << false;
        QTest::newRow("backtick in info") << u"``` a`b\nx\n```"_s << false << QString() << QString() << false;
        QTest::newRow("paragraph") << u"zwykły tekst"_s << false << QString() << QString() << false;
        // The first closing line ends the block (as in the core parser), not the last line.
        QTest::newRow("id after closing") << u"```js\nlet a\n```\n^kod123"_s << true << u"js"_s << u"let a"_s << true;
        QTest::newRow("first of two closings") << u"```\na\n```\nb\n```"_s << true << QString() << u"a"_s << true;
        // Only spaces and tabs may surround a closing fence (a no-break space makes it code).
        QTest::newRow("nbsp after closing")
                << u"```js\nx\n```\u00a0"_s << true << u"js"_s << u"x\n```\u00a0"_s << false;
        QTest::newRow("nbsp before closing")
                << u"```js\nx\n\u00a0```"_s << true << u"js"_s << u"x\n\u00a0```"_s << false;
        QTest::newRow("spaces and tabs around closing")
                << u"```js\nx\n \t```\t "_s << true << u"js"_s << u"x"_s << true;
    }

    void fenceLinesAfterClosing_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<QString>("closeLine");
        QTest::addColumn<QString>("after");
        QTest::addColumn<bool>("onlyMetadata");
        QTest::newRow("nothing") << u"```js\nx\n```"_s << u"```"_s << QString() << true;
        QTest::newRow("block id") << u"```js\nx\n```\n^kod123"_s << u"```"_s << u"^kod123"_s << true;
        QTest::newRow("blank line and id") << u"```js\nx\n```\n\n^kod123"_s << u"```"_s << u"\n^kod123"_s << true;
        QTest::newRow("logseq properties")
                << u"~~~\nx\n~~~~\nid:: 650f1c2a-0000-4000-8000-000000000001\ncollapsed:: true"_s << u"~~~~"_s
                << u"id:: 650f1c2a-0000-4000-8000-000000000001\ncollapsed:: true"_s << true;
        QTest::newRow("text") << u"```js\nx\n```\nopis [[Inna]] #tag"_s << u"```"_s << u"opis [[Inna]] #tag"_s << false;
        QTest::newRow("second fence") << u"```js\nx\n```\n```py\ny\n```"_s << u"```"_s << u"```py\ny\n```"_s << false;
        QTest::newRow("id after text") << u"```js\nx\n```\nżółw\n^kod123"_s << u"```"_s << u"żółw\n^kod123"_s << false;
        // The parser reads a block id or a property after spaces and tabs only.
        QTest::newRow("nbsp before id") << u"```js\nx\n```\n\u00a0^kod123"_s << u"```"_s << u"\u00a0^kod123"_s << false;
        QTest::newRow("indented property") << u"```js\nx\n```\n \tid:: 650f1c2a-0000-4000-8000-000000000001"_s
                                           << u"```"_s << u" \tid:: 650f1c2a-0000-4000-8000-000000000001"_s << true;
    }

    void fenceLinesAfterClosing()
    {
        QFETCH(QString, body);
        QFETCH(QString, closeLine);
        QFETCH(QString, after);
        QFETCH(bool, onlyMetadata);
        const auto f = parseFence(body);
        QVERIFY(f.has_value());
        QVERIFY(f->closed);
        QCOMPARE(f->inner, u"x"_s);
        QCOMPARE(f->closeLine, closeLine);
        QCOMPARE(f->after, after);
        QCOMPARE(f->onlyMetadataAfter, onlyMetadata);
    }

    void missingClosingFences_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<QString>("closing");
        QTest::newRow("no fence") << u"Zażółć gęślą jaźń\n`kod`"_s << QString();
        QTest::newRow("closed") << u"Akapit\n```js\nkod\n```"_s << QString();
        QTest::newRow("opened in a paragraph") << u"Akapit\n```js\nkod"_s << u"```"_s;
        QTest::newRow("lone opening line") << u"```js"_s << u"```"_s;
        QTest::newRow("longer tilde fence") << u"tekst\n  ~~~~~ py\ny\n~~~"_s << u"~~~~~"_s;
        QTest::newRow("second fence open") << u"```\na\n```\n````\nb"_s << u"````"_s;
        QTest::newRow("nbsp closing is code") << u"```\na\n```\u00a0"_s << u"```"_s;
        QTest::newRow("quoted fence") << u"> ```js\n> kod"_s << QString();
        QTest::newRow("backtick in info") << u"``` a`b\nx"_s << QString();
        QTest::newRow("task text") << u"[ ] ```js"_s << QString();
        // Inside a display formula between `$$` lines a fence-like line is formula (SYNTAX §11).
        QTest::newRow("in a display formula") << u"$$\na\n~~~\nb\n$$"_s << QString();
        QTest::newRow("in an item's formula") << u"Wzór:\n$$\n```\n$$\ndalej"_s << QString();
        QTest::newRow("after a display formula") << u"$$\na\n$$\n```js\nkod"_s << u"```"_s;
        QTest::newRow("formula not closed") << u"$$\n```\nb"_s << u"```"_s;
        QTest::newRow("blank line ends the formula") << u"$$\n\n```\n$$"_s << u"```"_s;
        QTest::newRow("$$ in code") << u"```\n$$\n```\n$$\n```"_s << u"```"_s;
    }

    void missingClosingFences()
    {
        QFETCH(QString, body);
        QFETCH(QString, closing);
        QCOMPARE(missingClosingFence(body), closing);
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

    void fenceMarkersAndLengths()
    {
        const auto backtick = parseFence(u"````js\nx\n````"_s);
        QVERIFY(backtick.has_value());
        QCOMPARE(backtick->marker, u'`');
        QCOMPARE(backtick->length, 4);
        QCOMPARE(backtick->lang, u"js"_s);
        const auto tilde = parseFence(u"~~~~\nx\n~~~"_s);
        QVERIFY(tilde.has_value());
        QVERIFY(!tilde->closed);
        QCOMPARE(tilde->marker, u'~');
        QCOMPARE(tilde->length, 4);
    }

    void codeFenceWidening()
    {
        const markdown::BlockEntry plain = entryOf(u"```\na\n```\n"_s);
        // A closing fence typed between the hidden fences is code: the fences grow around it.
        QCOMPARE(bodyWithEditText(plain, u"a\n```\nb"_s), u"````\na\n```\nb\n````"_s);
        QCOMPARE(bodyWithEditText(plain, u"```\n``````\n```"_s),
                 u"```````\n```\n``````\n```\n```````"_s);
        // The info string and the indentation of the opening fence are kept.
        const markdown::BlockEntry js = entryOf(u"```js\na\n```\n"_s);
        QCOMPARE(bodyWithEditText(js, u"a\n```"_s), u"````js\na\n```\n````"_s);
        // Backticks and tildes are independent fence characters.
        const markdown::BlockEntry tilde = entryOf(u"~~~\na\n~~~\n"_s);
        QCOMPARE(bodyWithEditText(tilde, u"a\n~~~"_s), u"~~~~\na\n~~~\n~~~~"_s);
        QCOMPARE(bodyWithEditText(tilde, u"a\n```"_s), u"~~~\na\n```\n~~~"_s);
        // A fence that is not closed yet (a file written elsewhere) is closed by a typed closing
        // line: it is not widened.
        const markdown::BlockEntry open = entryOf(u"```js\na\n"_s);
        QCOMPARE(bodyWithEditText(open, u"a\n```"_s), u"```js\na\n```"_s);
        // A block id or properties after the closing fence stay after it; the closing fence is
        // the one widened.
        const markdown::BlockEntry withId = entryOf(u"```js\na\n```\n^kod123\n\nPo\n"_s);
        QCOMPARE(bodyWithEditText(withId, u"b\n```"_s), u"````js\nb\n```\n````\n^kod123"_s);
        const markdown::BlockEntry withProperty =
                entryOf(u"- ```js\n  a\n  ```\n  id:: 650f1c2a-0000-4000-8000-000000000001\n"_s);
        QCOMPARE(bodyWithEditText(withProperty, u"a\n```"_s),
                 u"````js\na\n```\n````\nid:: 650f1c2a-0000-4000-8000-000000000001"_s);
        // A closing fence long enough already stays as it is.
        const markdown::BlockEntry longClose = entryOf(u"```\na\n``````\n"_s);
        QCOMPARE(bodyWithEditText(longClose, u"a\n```"_s), u"````\na\n```\n``````"_s);
        // Text after the closing fence (in a list item) is edited with the fence: nothing widens.
        const markdown::BlockEntry mixed = entryOf(u"- ```js\n  a\n  ```\n  opis #tag\n"_s);
        QCOMPARE(shapeOf(mixed).editText, u"a\n```\nopis #tag"_s);
        QCOMPARE(bodyWithEditText(mixed, u"b\n```\nopis #tag"_s), u"```js\nb\n```\nopis #tag"_s);
        // A fence stays wide when the line that needed it is removed (no hidden bytes rewritten).
        const markdown::BlockEntry wide = entryOf(u"````\na\n```\nb\n````\n"_s);
        QCOMPARE(bodyWithEditText(wide, u"a\nb"_s), u"````\na\nb\n````"_s);
        // Two backticks do not close a three-backtick fence.
        QCOMPARE(bodyWithEditText(plain, u"a\n``"_s), u"```\na\n``\n```"_s);
        // The same works for a code block inside a list item (the marker stays outside the body).
        const markdown::BlockEntry item = entryOf(u"- ```\n  a\n  ```\n"_s);
        QCOMPARE(item.kind, BlockKind::ListItem);
        QCOMPARE(bodyWithEditText(item, u"a\n```"_s), u"````\na\n```\n````"_s);
        // A tab-indented closing fence stays a fence when it is widened.
        const markdown::BlockEntry tabClose = entryOf(u"```\na\n\t```\n"_s);
        QCOMPARE(bodyWithEditText(tabClose, u"a\n```"_s), u"````\na\n```\n\t````"_s);
    }

    // The edit mapping of every block agrees with the core parser (markdown::parseNote): the
    // hidden prefix + edit text + hidden suffix is the body, the same edit text gives the same
    // body, and typing in a block — also a fence-like line in a code block — keeps the blocks a
    // reload sees; a block id, properties and the text after a code block stay where they are.
    void editMappingKeepsBlocks_data()
    {
        QTest::addColumn<QString>("content");
        QTest::newRow("code with id, task after")
                << u"```js\nlet a = 1;\n```\n^kod123\n\n- [ ] Zadzwonić do mamy #praca ⏰ 2026-10-22 09:00\n- Spotkanie [[Inna]]\n"_s;
        QTest::newRow("id after a blank line") << u"```js\nx\n```\n\n^kod123\n\nPo #tag\n"_s;
        QTest::newRow("logseq id:: and a child")
                << u"- ```js\n  let a = 1;\n  ```\n  id:: 650f1c2a-0000-4000-8000-000000000001\n  - [ ] dziecko #ważne ⏰ 2026-10-22 09:00\n- dalej [[Inna]]\n"_s;
        QTest::newRow("item with id and a child")
                << u"- ```js\n  let a = 1;\n  ```\n  ^kod123\n  - [ ] dziecko\n- dalej\n"_s;
        QTest::newRow("item properties")
                << u"- ```py\n  x = 1\n  ```\n  collapsed:: true\n  tags:: [[Projekt]], #kod\n  - dziecko\n"_s;
        QTest::newRow("item text after code")
                << u"- ```js\n  let a = 1;\n  ```\n  opis [[Inna]] #tag\n  - [ ] dziecko ⏰ 2026-10-22 09:00\n- dalej\n"_s;
        QTest::newRow("item with two fences") << u"- ```js\n  x\n  ```\n  #tag\n  ```py\n  y\n  ```\n- Po\n"_s;
        QTest::newRow("crlf") << u"```js\r\nx\r\n```\r\n^kod123\r\n\r\n- [ ] zadanie\r\n- [[Inna]]\r\n"_s;
        QTest::newRow("crlf item")
                << u"- ```js\r\n  a\r\n  ```\r\n  id:: 650f1c2a-0000-4000-8000-000000000001\r\n  - dziecko\r\n- b\r\n"_s;
        QTest::newRow("tabs")
                << u"- rodzic\n\t- ```js\n\t  a\n\t  ```\n\t  ^tab1\n\t\t- [ ] wnuk #praca\n\t- dalej\n"_s;
        QTest::newRow("tildes with info") << u"~~~python {.numberLines}\nprint('żółw')\n~~~\n^t1\n\nPo\n"_s;
        QTest::newRow("backticks inside tildes") << u"~~~\n```\nx\n```\n~~~\n\nPo\n"_s;
        QTest::newRow("four backticks around three") << u"````md\n```js\nx\n```\n````\n^md1\n\nPo\n"_s;
        QTest::newRow("longer closing") << u"```\nx\n`````\n^id1\n\nPo\n"_s;
        QTest::newRow("spaces around closing") << u"```\nx\n  ```  \n\nPo\n"_s;
        QTest::newRow("nbsp after closing") << u"```js\nx\n```\u00a0\n\nPo #tag\n"_s;
        QTest::newRow("unclosed at the end") << u"Akapit\n\n```js\nkod\n"_s;
        QTest::newRow("unclosed mid page") << u"Akapit\n\n```js\nkod\n\n- [ ] zadanie\n"_s;
        QTest::newRow("item unclosed until dedent") << u"- ```\n  code\n- next\n"_s;
        QTest::newRow("calc with id") << u"```calc\nx = 2\n```\n^calc1\n\n- [ ] zadanie\n"_s;
        QTest::newRow("math item with id") << u"- ```math\n  x^2\n  ```\n  ^wzor1\n"_s;
        QTest::newRow("query") << u"```query\ntag:praca\n```\n"_s;
        QTest::newRow("nested item") << u"- a\n  - ```js\n    b\n    ```\n    ^zag1\n  - c\n"_s;
        QTest::newRow("quoted code") << u"> ```js\n> x\n> ```\n\nPo\n"_s;
        QTest::newRow("text") << u"Akapit ąę\n\n- [ ] zadanie\n\t- dziecko\n"_s;
        QTest::newRow("frontmatter") << u"---\ntitle: Żółw\n---\n\n- a\n"_s;
        QTest::newRow("empty code") << u"```\n```\n\nPo\n"_s;
    }

    void editMappingKeepsBlocks()
    {
        QFETCH(QString, content);
        const markdown::BlockDocument doc(content);
        QCOMPARE(doc.toString(), content);
        for (int i = 0; i < doc.count(); ++i) {
            const markdown::BlockEntry &e = doc.at(i);
            const BlockShape s = shapeOf(e);
            const QString head = e.body.left(s.editOffset);
            const QString tail = e.body.right(s.editSuffix);
            QCOMPARE(head + s.editText + tail, e.body);
            QCOMPARE(bodyWithEditText(e, s.editText), e.body);
            const bool fenced = s.view == BlockView::Code || s.view == BlockView::Calc || s.view == BlockView::Query
                                || s.view == BlockView::Math;
            const auto fence = parseFence(e.body);
            const bool closedFence = fenced && fence && fence->closed && fence->onlyMetadataAfter;
            const bool mixed = fenced && fence && fence->closed && !fence->onlyMetadataAfter;
            if (fence && fence->closed && fence->onlyMetadataAfter && e.body.contains(u'\n'))
                QVERIFY2(fenced, qPrintable(e.body)); // a closed code block shows as code
            // A character typed at the start of the code, at the end of the block (not after the
            // last fence of a code block with text after it: that would open a new fence), and in
            // a closed code block lines that would close it.
            QStringList edits;
            if (fenced)
                edits.append(u"ą"_s + s.editText);
            if (!mixed)
                edits.append(s.editText + u"ż"_s);
            const QString widening = s.editText + u"\n```\n~~~~~\n````\n```"_s;
            if (closedFence)
                edits.append(widening);
            for (const QString &edit : std::as_const(edits)) {
                const QString body = bodyWithEditText(e, edit);
                if (edit != widening) { // only the edit text changes
                    QVERIFY2(body.startsWith(head), qPrintable(body));
                    QVERIFY2(body.endsWith(tail), qPrintable(body));
                }
                markdown::BlockDocument edited(content);
                edited.setBody(i, body);
                QCOMPARE(edited.at(i).body, body);
                QCOMPARE(shapeOf(edited.at(i)).editText, edit);
                QCOMPARE(edited.at(i).blockId, e.blockId);
                // A reload sees the blocks the editor has; nothing of the page became code.
                const QString saved = edited.toString();
                QCOMPARE(blocksOf(markdown::BlockDocument(saved)), blocksOf(edited));
                QCOMPARE(markdown::BlockDocument(saved).count(), doc.count());
                if (fenced) // the code is not indexed: tasks, ids, links and reminders stay
                    QCOMPARE(indexedOf(saved), indexedOf(content));
            }
        }
    }

    // A fence opened by an edit of a text block is closed at the end of the block, so the saved
    // file keeps the blocks after it (docs/UI.md).
    void fenceOpenedInTextIsClosed_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("editText");
        QTest::addColumn<QString>("body");
        QTest::newRow("fence line in a paragraph")
                << u"Akapit\n\n- [ ] zadanie\n"_s << u"Akapit\n```"_s << u"Akapit\n```\n```"_s;
        QTest::newRow("code in a paragraph")
                << u"Akapit\n"_s << u"Akapit\n```js\nkod"_s << u"Akapit\n```js\nkod\n```"_s;
        QTest::newRow("whole paragraph") << u"Akapit\n"_s << u"```js\nkod"_s << u"```js\nkod\n```"_s;
        QTest::newRow("list item") << u"- a\n  - dziecko\n"_s << u"a\n~~~~"_s << u"a\n~~~~\n~~~~"_s;
        QTest::newRow("task") << u"- [ ] zadanie\n"_s << u"zadanie\n```"_s << u"[ ] zadanie\n```\n```"_s;
        QTest::newRow("heading") << u"# Tytuł\n"_s << u"# Tytuł\n```"_s << u"# Tytuł\n```\n```"_s;
        QTest::newRow("quote") << u"> cytat\n"_s << u"> cytat\n```py"_s << u"> cytat\n```py\n```"_s;
        QTest::newRow("longer tilde fence")
                << u"tekst\n"_s << u"tekst\n~~~~~ py\ny\n~~~"_s << u"tekst\n~~~~~ py\ny\n~~~\n~~~~~"_s;
        QTest::newRow("nbsp closing is code")
                << u"tekst\n"_s << u"tekst\n```\ny\n```\u00a0"_s << u"tekst\n```\ny\n```\u00a0\n```"_s;
        QTest::newRow("closing deleted") << u"- a\n  ```\n  b\n  ```\n"_s << u"a\n```\nbc"_s << u"a\n```\nbc\n```"_s;
        QTest::newRow("fence typed after a closed one")
                << u"- a\n  ```\n  b\n  ```\n"_s << u"a\n```\nb\n```\n```"_s << u"a\n```\nb\n```\n```\n```"_s;
        // Fence lines in a text block are a pair: a longer opening fence lengthens the closing
        // one, a removed opening fence removes it.
        QTest::newRow("opening fence lengthened")
                << u"- Akapit\n  ```\n  ```\n"_s << u"Akapit\n````\n```"_s << u"Akapit\n````\n````"_s;
        QTest::newRow("backspace over the opening fence")
                << u"- Akapit\n  ```\n  ```\n"_s << u"Akapit\n``\n```"_s << u"Akapit\n``"_s;
        QTest::newRow("opening line deleted") << u"- a\n  ```js\n  kod\n  ```\n"_s << u"a\nkod\n```"_s << u"a\nkod"_s;
        QTest::newRow("opening fence broken")
                << u"- a\n  ```\n  b\n  ```\n"_s << u"a\nx```\nb\n```"_s << u"a\nx```\nb"_s;
        QTest::newRow("closing fence shortened")
                << u"- a\n  ````\n  b\n  ````\n"_s << u"a\n````\nb\n```"_s << u"a\n````\nb\n```\n````"_s;
        QTest::newRow("new line after a lone fence") << u"```js\n"_s << u"```js\n"_s << u"```js\n\n```"_s;
        QTest::newRow("lone fence item") << u"- ```js\n"_s << u"```js\nkod"_s << u"```js\nkod\n```"_s;
        // A lone opening fence line is a code block being typed (Enter makes it one).
        QTest::newRow("lone fence line") << u"Akapit\n"_s << u"```js"_s << u"```js"_s;
        // Fences inside a quote end with it.
        QTest::newRow("quoted fence") << u"> cytat\n"_s << u"> cytat\n> ```"_s << u"> cytat\n> ```"_s;
        // Closed fences need nothing.
        QTest::newRow("closed") << u"Akapit\n"_s << u"Akapit\n```js\nkod\n```"_s << u"Akapit\n```js\nkod\n```"_s;
        QTest::newRow("task text") << u"- [ ] a\n"_s << u"```js\nb"_s << u"[ ] ```js\nb"_s;
        // A fence that ended the body unclosed before the edit (a file written elsewhere) stays
        // as it is.
        QTest::newRow("unclosed in the file") << u"- a\n  ```\n  b\n"_s << u"a\n```\nbc"_s << u"a\n```\nbc"_s;
        QTest::newRow("unclosed code block") << u"```js\nkod\n"_s << u"kod\nwięcej"_s << u"```js\nkod\nwięcej"_s;
    }

    void fenceOpenedInTextIsClosed()
    {
        QFETCH(QString, content);
        QFETCH(QString, editText);
        QFETCH(QString, body);
        const markdown::BlockEntry e = entryOf(content);
        QCOMPARE(bodyWithEditText(e, editText), body);
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
        // Only list items are tasks (SYNTAX §5): a paragraph keeps "TODO" / "[ ]" as its text.
        QTest::newRow("paragraph TODO") << u"TODO zadzwonić do Łucji\n"_s << 0 << u"text"_s
                                        << u"TODO zadzwonić do Łucji"_s << 0 << QString();
        QTest::newRow("paragraph checkbox")
                << u"[ ] to nie zadanie\n"_s << 0 << u"text"_s << u"[ ] to nie zadanie"_s << 0 << QString();
        QTest::newRow("paragraph after list")
                << u"- [ ] zadanie\n\n[x] tekst\n"_s << 1 << u"text"_s << u"[x] tekst"_s << 0 << QString();
        QTest::newRow("heading") << u"## Koszty\n"_s << 0 << u"heading"_s << u"## Koszty"_s << 0 << QString();
        QTest::newRow("list heading") << u"- ## Sekcja\n"_s << 0 << u"heading"_s << u"## Sekcja"_s << 0 << QString();
        QTest::newRow("code") << u"```js\nlet a = 1\n```\n"_s << 0 << u"code"_s << u"let a = 1"_s << 6 << QString();
        QTest::newRow("calc") << u"```calc\nx = 2\n```\n"_s << 0 << u"calc"_s << u"x = 2"_s << 8 << QString();
        QTest::newRow("query") << u"```query\ntag:dom\n```\n"_s << 0 << u"query"_s << u"tag:dom"_s << 9 << QString();
        QTest::newRow("math") << u"```math\n\\frac{1}{2}\n```\n"_s << 0 << u"math"_s << u"\\frac{1}{2}"_s << 8
                              << QString();
        QTest::newRow("latex fence") << u"```latex\nE = mc^2\n```\n"_s << 0 << u"math"_s << u"E = mc^2"_s << 9
                                     << QString();
        QTest::newRow("tex fence") << u"```tex\nx^2\n```\n"_s << 0 << u"math"_s << u"x^2"_s << 7 << QString();
        QTest::newRow("list math") << u"- ```math\n  a^2 + b^2 = c^2\n  ```\n"_s << 0 << u"math"_s
                                   << u"a^2 + b^2 = c^2"_s << 8 << QString();
        QTest::newRow("code with id") << u"```js\nlet a = 1\n```\n^kod123\n"_s << 0 << u"code"_s << u"let a = 1"_s << 6
                                      << QString();
        QTest::newRow("calc with id") << u"```calc\nx = 2\n```\n\n^calc1\n"_s << 0 << u"calc"_s << u"x = 2"_s << 8
                                      << QString();
        QTest::newRow("list code with id::") << u"- ```js\n  a\n  ```\n  id:: 650f1c2a-0000-4000-8000-000000000001\n"_s
                                             << 0 << u"code"_s << u"a"_s << 6 << QString();
        QTest::newRow("list code with text") << u"- ```js\n  a\n  ```\n  opis #tag\n"_s << 0 << u"code"_s
                                             << u"a\n```\nopis #tag"_s << 6 << QString();
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
        const markdown::BlockEntry math = entryOf(u"```math\nx = 1\n```\n"_s);
        QCOMPARE(bodyWithEditText(math, u"x = 2\\\\y = 3"_s), u"```math\nx = 2\\\\y = 3\n```"_s);
        QCOMPARE(bodyWithEditText(math, QString()), u"```math\n\n```"_s);
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
        // The note mode key is the page header's toggle, not a property (YAML only: a Logseq
        // `brulion::` line is an ordinary property).
        const QVariantList mode = frontmatterProperties(u"---\nbrulion: document\nautor: Łucja\n---"_s);
        QCOMPARE(mode.size(), 1);
        QCOMPARE(mode[0].toMap().value(u"key"_s).toString(), u"autor"_s);
        QCOMPARE(frontmatterProperties(u"brulion:: document"_s).size(), 1);
    }

    void onlyNoteModeKey_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<bool>("only");
        QTest::newRow("document") << u"---\nbrulion: document\n---"_s << true;
        QTest::newRow("outline, crlf") << u"---\r\nbrulion: outline\r\n---"_s << true;
        QTest::newRow("invalid value is still the key") << u"---\nbrulion: kanban\n---"_s << true;
        QTest::newRow("with other keys") << u"---\nbrulion: document\ntags: [a]\n---"_s << false;
        QTest::newRow("empty yaml") << u"---\n---"_s << false;
        QTest::newRow("other key only") << u"---\nautor: Łucja\n---"_s << false;
        QTest::newRow("logseq property") << u"brulion:: document"_s << false;
        // Anything else the user wrote in the block keeps it visible (a hidden block hides it).
        QTest::newRow("comment and key") << u"---\n# tryb notatki\nbrulion: document\n---"_s << false;
        QTest::newRow("text and key") << u"---\nWstęp do notatki: zażółć\n\nbrulion: document\n---"_s << false;
        QTest::newRow("text that is no key") << u"---\nżółw\nbrulion: document\n---"_s << false;
        QTest::newRow("blank lines around the key") << u"---\n\nbrulion: outline\n\n---"_s << true;
        QTest::newRow("comment after the value") << u"---\nbrulion: outline # ręcznie\n---"_s << true;
        QTest::newRow("nested value") << u"---\nbrulion:\n  tryb: document\n---"_s << false;
        QTest::newRow("indented key") << u"---\n  brulion: document\n---"_s << false;
    }

    void onlyNoteModeKey()
    {
        QFETCH(QString, body);
        QFETCH(bool, only);
        QCOMPARE(editor::onlyNoteModeKey(body), only);
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

    void webPictureEmbeds()
    {
        const QList<Embed> e = embedsOf(
                u"Kot ![mój kot](https://example.com/kot.jpg) ![](HTTP://cdn.example.pl/img?id=3) "
                u"![ftp](ftp://example.com/a.png) ![d](data:image/png;base64,AAAA) [link](https://example.com/b.png) "
                u"`![kod](https://example.com/c.png)`"_s);
        QCOMPARE(e.size(), 2);
        QCOMPARE(e[0].target, u"https://example.com/kot.jpg"_s);
        QCOMPARE(e[0].alt, u"mój kot"_s);
        QVERIFY(e[0].image);
        QVERIFY(e[0].web);
        QCOMPARE(e[1].target, u"HTTP://cdn.example.pl/img?id=3"_s);
        QVERIFY(e[1].image); // no extension: `![…](…)` says it is a picture
        QVERIFY(e[1].web);
        QVERIFY(!embedsOf(u"![[łazienka.png]]"_s).at(0).web);
        // A wiki embed of a web address: a picture from the internet too, no transclusion.
        const QList<Embed> wiki = embedsOf(u"![[https://example.com/obraz]]"_s);
        QCOMPARE(wiki.size(), 1);
        QVERIFY(wiki[0].web);
        QVERIFY(onlyEmbeds(u"![[https://example.com/obraz]]"_s));
        QVERIFY(onlyEmbeds(u"![[https://example.com/obraz]]"_s, true));
        QVERIFY(transclusionsOf(u"![[https://example.com/obraz]]"_s).isEmpty());
        QVERIFY(onlyEmbeds(u"![](https://example.com/a.png)"_s));
        QVERIFY(onlyEmbeds(u" ![a](https://example.com/a.png) ![[b.png]]"_s));
        QVERIFY(!onlyEmbeds(u"Zdjęcie: ![a](https://example.com/a.png)"_s));
        // Another scheme is no embed — so the block keeps its text (it was blank before).
        QVERIFY(!onlyEmbeds(u"![a](ftp://example.com/a.png)"_s));
        QVERIFY(!onlyEmbeds(u"![a](data:image/png;base64,AAAA)"_s));
        // A destination with backslash escapes is named as parsed (what retargetWebPicture() and
        // the host get).
        QCOMPARE(embedsOf(u"![](https://example.com/a\\_b\\(1\\).png)"_s).value(0).target,
                 u"https://example.com/a_b(1).png"_s);
    }

    void viewsShowingEmbeds()
    {
        for (const BlockView v : {BlockView::Text, BlockView::Heading, BlockView::Quote, BlockView::Callout})
            QVERIFY(showsEmbeds(v));
        for (const BlockView v : {BlockView::Code, BlockView::Calc, BlockView::Query, BlockView::Math, BlockView::Table,
                                  BlockView::Rule, BlockView::Frontmatter, BlockView::Html})
            QVERIFY(!showsEmbeds(v));
    }

    void retargetWebPictures_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("expected");
        const QString u = u"https://example.com/kot.jpg"_s;
        QTest::newRow("plain") << u"![kot](https://example.com/kot.jpg)"_s << u << u"assets/kot.jpg"_s
                               << u"![kot](assets/kot.jpg)"_s;
        QTest::newRow("spaces polish") << u"Zobacz ![Mój kot](https://example.com/kot.jpg) tutaj"_s << u
                                       << u"assets/Mój kot (2).jpg"_s
                                       << u"Zobacz ![Mój kot](assets/Mój%20kot%20%282%29.jpg) tutaj"_s;
        QTest::newRow("title kept") << u"![k](https://example.com/kot.jpg \"Tytuł\")"_s << u << u"assets/k.jpg"_s
                                    << u"![k](assets/k.jpg \"Tytuł\")"_s;
        QTest::newRow("angle kept") << u"![k](<https://example.com/kot.jpg>)"_s << u << u"assets/a b.jpg"_s
                                    << u"![k](<assets/a%20b.jpg>)"_s;
        QTest::newRow("alt naming the url")
                << u"![https://example.com/kot.jpg](https://example.com/kot.jpg)"_s << u << u"assets/kot.jpg"_s
                << u"![https://example.com/kot.jpg](assets/kot.jpg)"_s;
        QTest::newRow("twice") << u"![a](https://example.com/kot.jpg)\r\n![b](https://example.com/kot.jpg)"_s << u
                               << u"assets/kot.jpg"_s << u"![a](assets/kot.jpg)\r\n![b](assets/kot.jpg)"_s;
        QTest::newRow("other url stays") << u"![a](https://example.com/pies.jpg)"_s << u << u"assets/kot.jpg"_s
                                         << u"![a](https://example.com/pies.jpg)"_s;
        QTest::newRow("link stays") << u"[a](https://example.com/kot.jpg) https://example.com/kot.jpg"_s << u
                                    << u"assets/kot.jpg"_s
                                    << u"[a](https://example.com/kot.jpg) https://example.com/kot.jpg"_s;
        QTest::newRow("code stays") << u"`![a](https://example.com/kot.jpg)`"_s << u << u"assets/kot.jpg"_s
                                    << u"`![a](https://example.com/kot.jpg)`"_s;
        QTest::newRow("hash and percent") << u"![a](https://example.com/kot.jpg)"_s << u << u"assets/100% #1.jpg"_s
                                          << u"![a](assets/100%25%20%231.jpg)"_s;
        QTest::newRow("wiki embed") << u"![[https://example.com/kot.jpg|Mój kot]] ![[https://example.com/kot.jpg]]"_s
                                    << u << u"assets/Mój kot.jpg"_s
                                    << u"![[assets/Mój kot.jpg|Mój kot]] ![[assets/Mój kot.jpg]]"_s;
        QTest::newRow("empty path") << u"![a](https://example.com/kot.jpg)"_s << u << QString()
                                    << u"![a](https://example.com/kot.jpg)"_s;
        // Only the destination the parser read: a title naming the URL, or holding "](", stays.
        QTest::newRow("title naming the url")
                << u"![k](https://example.com/kot.jpg \"źródło: https://example.com/kot.jpg\")"_s << u
                << u"assets/kot.jpg"_s << u"![k](assets/kot.jpg \"źródło: https://example.com/kot.jpg\")"_s;
        QTest::newRow("title with brackets") << u"![k](https://example.com/kot.jpg \"a](b\")"_s << u
                                             << u"assets/kot.jpg"_s << u"![k](assets/kot.jpg \"a](b\")"_s;
        QTest::newRow("title only") << u"![k](https://example.com/pies.jpg \"https://example.com/kot.jpg\")"_s << u
                                    << u"assets/kot.jpg"_s
                                    << u"![k](https://example.com/pies.jpg \"https://example.com/kot.jpg\")"_s;
        // A code span in the alt text or title is read as the parser reads it (masked).
        QTest::newRow("code span in alt") << u"![wykres `a]b`](https://example.com/kot.jpg) ż"_s << u
                                          << u"assets/kot.jpg"_s << u"![wykres `a]b`](assets/kot.jpg) ż"_s;
        QTest::newRow("code span in title") << u"![k](https://example.com/kot.jpg \"a `\"` b\")"_s << u
                                            << u"assets/kot.jpg"_s << u"![k](assets/kot.jpg \"a `\"` b\")"_s;
        // Backslash escapes: the target as parsed (Embed::target) names it; replaced as a whole.
        QTest::newRow("escaped") << u"Zdjęcie ![żółw](https://example.com/Foo_\\(1\\)\\_b.jpg) i tekst"_s
                                 << u"https://example.com/Foo_(1)_b.jpg"_s << u"assets/Foo_(1)_b.jpg"_s
                                 << u"Zdjęcie ![żółw](assets/Foo_%281%29_b.jpg) i tekst"_s;
        QTest::newRow("escaped spelling stays")
                << u"![a](https://example.com/a\\_b.png)"_s << u"https://example.com/a\\_b.png"_s << u"assets/a_b.png"_s
                << u"![a](https://example.com/a\\_b.png)"_s;
    }

    void retargetWebPictures()
    {
        QFETCH(QString, body);
        QFETCH(QString, url);
        QFETCH(QString, path);
        QFETCH(QString, expected);
        QCOMPARE(retargetWebPicture(body, url, path), expected);
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

    void tokenDates_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<QString>("token"); // the token whose range is given ("" = a range with none)
        QTest::addColumn<QString>("spec");
        QTest::addColumn<QString>("expected");

        QTest::newRow("due") << u"- [ ] Zapłacić 📅 2026-10-20 ^z1"_s << u"📅 2026-10-20"_s << u"2026-10-27"_s
                             << u"- [ ] Zapłacić 📅 2026-10-27 ^z1"_s;
        QTest::newRow("due gets a time") << u"x 📅 2026-10-20 y"_s << u"📅 2026-10-20"_s << u"2026-10-21 14:30"_s
                                         << u"x 📅 2026-10-21 14:30 y"_s;
        QTest::newRow("scheduled loses its time") << u"⏳ 2026-10-18 09:00 żółw"_s << u"⏳ 2026-10-18 09:00"_s
                                                  << u"2026-10-19"_s << u"⏳ 2026-10-19 żółw"_s;
        QTest::newRow("variant emoji kept") << u"a 📆 2026-10-20"_s << u"📆 2026-10-20"_s << u"2026-11-01"_s
                                            << u"a 📆 2026-11-01"_s;
        QTest::newRow("T separator") << u"🛫 2026-10-20T08:00 ą"_s << u"🛫 2026-10-20T08:00"_s << u"2026-10-21 08:00"_s
                                     << u"🛫 2026-10-21 08:00 ą"_s;
        QTest::newRow("done") << u"[x] ✅ 2026-10-15"_s << u"✅ 2026-10-15"_s << u"2026-10-14"_s << u"[x] ✅ 2026-10-14"_s;
        QTest::newRow("alarm") << u"a ⏰! 2026-10-15 07:30 b"_s << u"⏰! 2026-10-15 07:30"_s << u"2026-10-16 08:00"_s
                               << u"a ⏰! 2026-10-16 08:00 b"_s;
        QTest::newRow("compat reminder") << u"x (@2026-10-15 09:00) y"_s << u"(@2026-10-15 09:00)"_s
                                         << u"2026-10-16 10:15"_s << u"x (@2026-10-16 10:15) y"_s;
        QTest::newRow("the second of two") << u"📅 2026-10-20 i 📅 2026-10-22"_s << u"📅 2026-10-22"_s << u"2026-10-23"_s
                                           << u"📅 2026-10-20 i 📅 2026-10-23"_s;
        QTest::newRow("CRLF and next line kept") << u"a ⏳ 2026-10-18\r\n  dalej"_s << u"⏳ 2026-10-18"_s
                                                 << u"2026-10-19"_s << u"a ⏳ 2026-10-19\r\n  dalej"_s;
        QTest::newRow("no token there") << u"📅 jutro"_s << QString() << u"2026-10-23"_s << u"📅 jutro"_s;
        QTest::newRow("recurrence is no date") << u"🔁 every day"_s << QString() << u"2026-10-23"_s << u"🔁 every day"_s;
        QTest::newRow("planning is not rewritten") << u"SCHEDULED: <2026-10-15 Thu>"_s << QString() << u"2026-10-23"_s
                                                   << u"SCHEDULED: <2026-10-15 Thu>"_s;
    }

    void tokenDates()
    {
        QFETCH(QString, body);
        QFETCH(QString, token);
        QFETCH(QString, spec);
        QFETCH(QString, expected);
        const qsizetype from = token.isEmpty() ? 0 : body.indexOf(token);
        const qsizetype to = token.isEmpty() ? body.size() : from + token.size();
        QVERIFY(from >= 0);
        QCOMPARE(dateTokenAt(body, from, to).has_value(), !token.isEmpty());
        const auto parsed = dates::parseDateSpec(spec);
        QVERIFY(parsed);
        QCOMPARE(setTokenDate(body, from, to, *parsed), expected);
    }

    void tokenDatesOutOfRange()
    {
        const QString body = u"a 📅 2026-10-20"_s;
        QVERIFY(!dateTokenAt(body, -1, 5));
        QVERIFY(!dateTokenAt(body, 2, 999));
        QVERIFY(!dateTokenAt(body, 5, 2));
        QCOMPARE(setTokenDate(body, 2, int(body.size()), DateSpec{}), body); // an invalid date changes nothing
    }

    void reminderAlarms()
    {
        QCOMPARE(setReminderAlarm(u"a ⏰ 2026-10-15 09:00 b"_s, 2, 20, true), u"a ⏰! 2026-10-15 09:00 b"_s);
        QCOMPARE(setReminderAlarm(u"a ⏰! 2026-10-15 09:00 b"_s, 2, 21, false), u"a ⏰ 2026-10-15 09:00 b"_s);
        QCOMPARE(setReminderAlarm(u"a ⏰ 2026-10-15 09:00 b"_s, 2, 20, false), u"a ⏰ 2026-10-15 09:00 b"_s);
        QCOMPARE(setReminderAlarm(u"x (@2026-10-15 09:00) y"_s, 2, 21, false), u"x ⏰ 2026-10-15 09:00 y"_s);
        QCOMPARE(setReminderAlarm(u"x (@2026-10-15) y"_s, 2, 16, true), u"x ⏰! 2026-10-15 y"_s);
        QCOMPARE(setReminderAlarm(u"a 📅 2026-10-15 b"_s, 2, 15, true), u"a 📅 2026-10-15 b"_s); // no reminder
    }

    void shiftedSpecs_data()
    {
        QTest::addColumn<QString>("spec");
        QTest::addColumn<int>("days");
        QTest::addColumn<int>("minutes");
        QTest::addColumn<QString>("expected");
        QTest::newRow("a day") << u"2026-10-20"_s << 1 << 0 << u"2026-10-21"_s;
        QTest::newRow("a week over the month") << u"2026-10-28 09:00"_s << 7 << 0 << u"2026-11-04 09:00"_s;
        QTest::newRow("an hour") << u"2026-10-20 09:00"_s << 0 << 60 << u"2026-10-20 10:00"_s;
        QTest::newRow("an hour over midnight") << u"2026-10-20 23:30"_s << 0 << 60 << u"2026-10-21 00:30"_s;
        QTest::newRow("back over midnight") << u"2026-10-20 00:15"_s << 0 << -30 << u"2026-10-19 23:45"_s;
        QTest::newRow("over the year") << u"2026-12-31 23:00"_s << 0 << 120 << u"2027-01-01 01:00"_s;
        QTest::newRow("no time: the default") << u"2026-10-20"_s << 0 << 60 << u"2026-10-20 10:00"_s;
        // Wall-clock time: the night the clocks go back (25 October 2026 in Poland) is no different.
        QTest::newRow("DST night") << u"2026-10-25 01:30"_s << 0 << 60 << u"2026-10-25 02:30"_s;
        QTest::newRow("leap day") << u"2028-02-28"_s << 1 << 0 << u"2028-02-29"_s;
    }

    void shiftedSpecs()
    {
        QFETCH(QString, spec);
        QFETCH(int, days);
        QFETCH(int, minutes);
        QFETCH(QString, expected);
        const auto parsed = dates::parseDateSpec(spec);
        QVERIFY(parsed);
        QCOMPARE(shiftedSpec(*parsed, days, minutes, QTime(9, 0)).toString(), expected);
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
        // After a written date the rest of the line completes as anywhere else.
        QTest::newRow("tag after a date")
                << u"x 📅 2026-10-15 #zak"_s << 20 << u"tags"_s << u"zak"_s << 17 << 20 << u"#"_s << QString();
        QTest::newRow("slash after a date")
                << u"x ⏳ 2026-10-15 /prz"_s << 19 << u"slash"_s << u"prz"_s << 15 << 19 << u"/"_s << QString();
        QTest::newRow("date after a date")
                << u"x 📅 2026-10-15 text"_s << 20 << QString() << QString() << -1 << -1 << QString() << QString();
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
        QTest::newRow("wzór") << u"wzor"_s << u"math"_s;
        QTest::newRow("równanie") << u"równanie"_s << u"math"_s;
        QTest::newRow("latex") << u"latex"_s << u"math"_s;
        QTest::newRow("formula") << u"formula"_s << u"math"_s;
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
        // Task statuses: Polish and English names and aliases (the filter is one word).
        QTest::newRow("zadanie") << u"zadanie"_s << u"task"_s;
        QTest::newRow("todo") << u"todo"_s << u"task"_s;
        QTest::newRow("do zrobienia") << u"zrobienia"_s << u"task"_s;
        QTest::newRow("to do") << u"task"_s << u"task"_s;
        QTest::newRow("w toku") << u"toku"_s << u"taskDoing"_s;
        QTest::newRow("doing") << u"doing"_s << u"taskDoing"_s;
        QTest::newRow("in progress") << u"progress"_s << u"taskDoing"_s;
        QTest::newRow("zrobione") << u"zrobione"_s << u"taskDone"_s;
        QTest::newRow("done") << u"done"_s << u"taskDone"_s;
        QTest::newRow("gotowe") << u"gotowe"_s << u"taskDone"_s;
        QTest::newRow("czeka") << u"czeka"_s << u"taskWaiting"_s;
        QTest::newRow("odłożone") << u"odlozone"_s << u"taskWaiting"_s;
        QTest::newRow("oczekujące") << u"oczekujące"_s << u"taskWaiting"_s;
        QTest::newRow("waiting") << u"waiting"_s << u"taskWaiting"_s;
        QTest::newRow("anulowane") << u"anul"_s << u"taskCancelled"_s;
        QTest::newRow("cancel") << u"cancel"_s << u"taskCancelled"_s;
        QTest::newRow("bez statusu") << u"bez"_s << u"taskNone"_s;
        QTest::newRow("no status") << u"none"_s << u"taskNone"_s;
        // The current time keeps its words (no task status takes them).
        QTest::newRow("teraz") << u"teraz"_s << u"time"_s;
        QTest::newRow("now") << u"now"_s << u"time"_s;
    }

    void slashTaskStatuses()
    {
        // "/zad" lists all six statuses, in the order of the status menu.
        QStringList ids;
        for (const SlashCommand *c : filterSlashCommands(u"zad"_s))
            ids.append(c->id);
        QCOMPARE(ids.mid(0, 6), QStringList({u"task"_s, u"taskDoing"_s, u"taskDone"_s, u"taskWaiting"_s,
                                             u"taskCancelled"_s, u"taskNone"_s}));
        QCOMPARE(slashCommand(u"task"_s)->namePl, u"Zadanie: do zrobienia"_s);
        QCOMPARE(slashCommand(u"taskNone"_s)->nameEn, u"Task: no status"_s);
        QCOMPARE(slashCommand(u"taskDone"_s)->syntaxPl, u"- [x] zadanie ✅ data"_s);
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
        QCOMPARE(slashTemplate(u"math", false, &cursor), u"```math\n\n```"_s);
        QCOMPARE(cursor, 8);
        QCOMPARE(slashCommand(u"math"_s)->namePl, u"Wzór matematyczny"_s);
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
        // Note modes: names as in config.json; anything else keeps the defaults.
        const VaultConfig modes =
                vaultConfigFromMap({{u"journalMode"_s, u"Document"_s}, {u"pageMode"_s, u" outline "_s}});
        QCOMPARE(modes.journalMode, NoteMode::Document);
        QCOMPARE(modes.pageMode, NoteMode::Outline);
        const VaultConfig defaults = vaultConfigFromMap({{u"journalMode"_s, u"kanban"_s}, {u"pageMode"_s, 3}});
        QCOMPARE(defaults.journalMode, NoteMode::Outline);
        QCOMPARE(defaults.pageMode, NoteMode::Document);
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
