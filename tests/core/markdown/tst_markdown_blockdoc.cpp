// BlockDocument: lossless round trip, block entries and every editing operation (including the
// row changes reported for the UI model).
#include "markdown/blockdocument.h"
#include "markdown/frontmatter.h"
#include "markdown/parse.h"

#include <QElapsedTimer>
#include <QHash>
#include <QRegularExpression>
#include <QTest>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

const QDate Today(2026, 10, 14);

using Type = BlockChange::Type;

QString describe(const QList<BlockChange> &changes)
{
    QStringList out;
    for (const BlockChange &c : changes) {
        switch (c.type) {
        case Type::Removed: out.append(u"removed %1+%2"_s.arg(c.first).arg(c.count)); break;
        case Type::Inserted: out.append(u"inserted %1+%2"_s.arg(c.first).arg(c.count)); break;
        case Type::Changed: out.append(u"changed %1+%2"_s.arg(c.first).arg(c.count)); break;
        case Type::Moved: out.append(u"moved %1+%2->%3"_s.arg(c.first).arg(c.count).arg(c.destination)); break;
        }
    }
    return out.join(u", ");
}

QString kindName(BlockKind k)
{
    switch (k) {
    case BlockKind::ListItem: return u"li"_s;
    case BlockKind::Paragraph: return u"p"_s;
    case BlockKind::Heading: return u"h"_s;
    case BlockKind::Code: return u"code"_s;
    case BlockKind::Table: return u"table"_s;
    case BlockKind::Quote: return u"quote"_s;
    case BlockKind::Callout: return u"callout"_s;
    case BlockKind::Rule: return u"hr"_s;
    case BlockKind::Html: return u"html"_s;
    case BlockKind::Frontmatter: return u"fm"_s;
    }
    return {};
}

/// "kind:depth:prefix|body" per block (prefix/body with visible tabs and line breaks).
QStringList entries(const BlockDocument &doc)
{
    QStringList out;
    for (int i = 0; i < doc.count(); ++i) {
        const BlockEntry &e = doc.at(i);
        QString prefix = e.prefix;
        QString body = e.body;
        prefix.replace(u'\t', u"→"_s);
        body.replace(u'\n', u"⏎"_s).replace(u'\t', u"→"_s);
        out.append(kindName(e.kind) + u':' + QString::number(e.depth) + u':' + prefix + u'|' + body);
    }
    return out;
}

QList<quint64> uids(const BlockDocument &doc)
{
    QList<quint64> out;
    for (int i = 0; i < doc.count(); ++i)
        out.append(doc.at(i).uid);
    return out;
}

/// Replays `changes` on the old uid list (new rows take their uids from `after`) and checks
/// that the result is the new uid list — i.e. the row changes describe the edit exactly.
bool changesReplay(const QList<quint64> &before, const QList<quint64> &after, const QList<BlockChange> &changes,
                   QString *why)
{
    QList<quint64> rows = before;
    for (const BlockChange &c : changes) {
        switch (c.type) {
        case Type::Removed:
            if (c.first < 0 || c.count <= 0 || c.first + c.count > rows.size()) {
                *why = u"bad removal"_s;
                return false;
            }
            rows.remove(c.first, c.count);
            break;
        case Type::Moved: {
            if (c.first < 0 || c.count <= 0 || c.first + c.count > rows.size() || c.destination < 0
                || (c.destination >= c.first && c.destination <= c.first + c.count)) {
                *why = u"bad move"_s;
                return false;
            }
            const QList<quint64> part = rows.mid(c.first, c.count);
            rows.remove(c.first, c.count);
            const int dest = c.destination > c.first ? c.destination - c.count : c.destination;
            for (int k = 0; k < part.size(); ++k)
                rows.insert(dest + k, part[k]);
            break;
        }
        case Type::Inserted:
            if (c.first < 0 || c.count <= 0 || c.first > rows.size() || c.first + c.count > after.size()) {
                *why = u"bad insertion"_s;
                return false;
            }
            for (int k = 0; k < c.count; ++k)
                rows.insert(c.first + k, after[c.first + k]);
            break;
        case Type::Changed:
            if (c.first < 0 || c.count <= 0 || c.first + c.count > after.size()) {
                *why = u"bad change range"_s;
                return false;
            }
            break;
        }
    }
    if (rows != after) {
        *why = u"replayed rows differ"_s;
        return false;
    }
    return true;
}

/// Fixtures for the round trip (ported from the archive's markdown tests plus edge cases).
QStringList fixtures()
{
    return {
            QString(),
            u"\n"_s,
            u"\n\n"_s,
            u"   \r\n\t"_s,
            u"a"_s,
            u"a\n"_s,
            u"\uFEFF# Tytuł z BOM\n\nTreść"_s,
            u"---\ntitle: Tydzień 42\ntags: [projekt, \"Dom/Remont\"]\n---\ntype:: plan\nstatus:: [[W toku]]\n\n# Remont łazienki #dom\n\nPlan prac. ^plan\n"_s,
            u"- a\n  - b\n    - c\n  - d\n- e"_s,
            u"- a\n\t- b\n\t\t- c\n- d\n"_s,
            u"- a\n  - b\n\t- c\n - d"_s,
            u"1. a\n2. b\n   1) c\n+ d\n* e\n"_s,
            u"- a\n  more a\n  - b\nlazy b\n  back in a\n- c"_s,
            u"- a\n\n  para\n\n- b\n\n\ntext"_s,
            u"- a\n# H\n- b\n```\ncode\n```\n- c\n> q"_s,
            u"- item\n  ```js\n  - [ ] not a task\n\n  ```\n  after\n- next"_s,
            u"- ```\n  - x\n  ```\n- y"_s,
            u"- - -\n* * *\n- x"_s,
            u"# One\n## Two ##\nSetext\n===\nAlso\n---\n#nottag"_s,
            u"```Calc\nx = 1\n```\n~~~query {.x}\ntype:tasks\n~~~\n```\nopen\n\n"_s,
            u"````md\n```\ninner\n```\n````\n"_s,
            u"| a | b |\n|---|:-:|\n| 1 | 2 |\nafter"_s,
            u"> [!todo]- Zakupy\n> - [ ] mleko\n>   - [ ] chleb\n> \n> > [!note] inner\n> > text\n\nafter"_s,
            u"<div>\n[[x]]\n</div>\n\n<!-- c\n\nstill -->\ntext\n\n<span>inline</span> text"_s,
            u"a\n\n%%\n- [ ] hidden\n%%\n\nb"_s,
            u"%% komentarz na początku %%\n\nAkapit"_s,
            u"| a | b |\n|---|---|\n| 1 | 2 |\n^tbl\n\n```\nx\n```\n\n^code1\n\n- item\n\n^para"_s,
            u"- [ ] Kupić płytki 📅 2026-10-16 ⏫ #zakupy\r\n  - [x] Zmierzyć ✅ 2026-10-13\r\n- TODO [#B] Zadzwonić\r\n  SCHEDULED: <2026-10-15 Thu 09:00 .+1w>\r\n"_s,
            u"line with trailing spaces   \n\t\n  \nnext\n  "_s,
            u"mixed\r\nendings\nhere\r\n"_s,
            u"old\rmac\rendings"_s,
            u"- DONE Zrobione\n  :LOGBOOK:\n  CLOCK: [2026-10-14 Wed 10:00]\n  :END:\n  DEADLINE: <2026-10-20 Tue>\n"_s,
            u"Wstęp\n\n> Cytat #ważne\n> - [ ] zadanie\n>   - [x] pod\n> ~~~\n> kod\n> ~~~\n\nPo"_s,
            u"tags:: [[Książki]], fantastyka\nautor:: [[Lem]]\n\n- Solaris\n"_s,
            u"\n\n\n- zaczyna się od pustych linii\n"_s,
            u"10. dziesięć\n11. jedenaście\n"_s,
            u"- \n-\n- [ ] \n"_s,
            u"$$\nf(x) = a\n + b\n - c\n$$\n\n- Wzór:\n  $$\n  a\n  + b\n  $$\n- dalej\n"_s,
    };
}

} // namespace

class TestBlockDocument : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // --- round trip ------------------------------------------------------------------------------

    void roundTrip_data()
    {
        QTest::addColumn<QString>("content");
        int k = 0;
        for (const QString &f : fixtures()) {
            QTest::newRow(qPrintable(u"fixture %1"_s.arg(k))) << f;
            QString crlf = f;
            crlf.replace(u"\r\n"_s, u"\n"_s).replace(u"\n"_s, u"\r\n"_s);
            QTest::newRow(qPrintable(u"fixture %1 crlf"_s.arg(k))) << crlf;
            if (f.endsWith(u'\n'))
                QTest::newRow(qPrintable(u"fixture %1 no final newline"_s.arg(k))) << f.chopped(1);
            ++k;
        }
    }

    void roundTrip()
    {
        QFETCH(QString, content);
        const BlockDocument doc(content);
        QCOMPARE(doc.toString(), content);
        QString joined;
        for (int i = 0; i < doc.count(); ++i)
            joined += doc.blockSource(i);
        QVERIFY(joined.size() <= content.size());
    }

    void roundTripFuzz()
    {
        const QStringList pieces{
                u"- "_s,    u"  "_s,      u"\t"_s,   u"[ ]"_s,  u"[x]"_s,     u"#"_s,      u"##"_s,
                u">"_s,     u"[!note]"_s, u"```"_s,  u"~~~"_s,  u"|"_s,       u"---"_s,    u"%%"_s,
                u"`"_s,     u"[["_s,      u"]]"_s,   u"^id"_s,  u"::"_s,      u"TODO "_s,  u"📅 2026-10-15"_s,
                u"<div>"_s, u"\n"_s,      u"\n"_s,   u"\r\n"_s, u"zażółć"_s,  u"x"_s,      u" "_s,
                u"1. "_s,   u"2) "_s,     u"<!--"_s, u"-->"_s,  u"key:: v"_s, u"\uFEFF"_s, u"\r"_s};
        quint32 seed = 1234;
        const auto rnd = [&](quint32 n) {
            seed = (seed * 1103515245u + 12345u) % 2147483648u;
            return seed % n;
        };
        for (int k = 0; k < 500; ++k) {
            QString s;
            const quint32 len = 1 + rnd(80);
            for (quint32 i = 0; i < len; ++i)
                s += pieces[rnd(quint32(pieces.size()))];
            const BlockDocument doc(s);
            QCOMPARE(doc.toString(), s);
        }
    }

    void operationsFuzz()
    {
        // Random operations never crash, keep the text parseable, and their row changes replay.
        const QStringList docs = fixtures();
        quint32 seed = 99;
        const auto rnd = [&](quint32 n) {
            seed = (seed * 1103515245u + 12345u) % 2147483648u;
            return n ? seed % n : 0;
        };
        for (int round = 0; round < 400; ++round) {
            BlockDocument doc(docs[rnd(quint32(docs.size()))]);
            for (int step = 0; step < 6; ++step) {
                const QList<quint64> before = uids(doc);
                const int n = doc.count();
                const int i = n ? int(rnd(quint32(n))) : -1;
                EditResult r;
                switch (rnd(14)) {
                case 0: r = doc.setBody(i, u"nowy **tekst** #tag\ndruga linia"_s); break;
                case 1: r = doc.split(i, int(rnd(20))); break;
                case 2: r = doc.insertAfter(i, u"wstawiony"_s, int(rnd(3))); break;
                case 3: r = doc.mergeWithPrevious(i); break;
                case 4: r = doc.remove(i, rnd(2) == 0); break;
                case 5: r = doc.indent(i); break;
                case 6: r = doc.outdent(i); break;
                case 7: r = doc.moveUp(i); break;
                case 8: r = doc.moveDown(i); break;
                case 9: r = doc.toggleTask(i, Today); break;
                case 10: r = doc.ensureBlockId(i).edit; break;
                case 11: r = doc.convertToList(i); break;
                case 12: r = doc.convertToParagraph(i); break;
                default: r = doc.insertAfter(i, u"akapit"_s, 0, false); break;
                }
                QString why;
                QVERIFY2(changesReplay(before, uids(doc), r.changes, &why),
                         qPrintable(why + u": "_s + describe(r.changes)));
                const QString text = doc.toString();
                QCOMPARE(BlockDocument(text).toString(), text);
                for (int b = 0; b < doc.count(); ++b) {
                    const BlockEntry &e = doc.at(b);
                    QVERIFY(e.parent < b);
                    QVERIFY(e.parent < 0 || doc.at(e.parent).depth == e.depth - 1);
                }
                if (r.focus >= 0)
                    QVERIFY(r.focus < doc.count());
            }
        }
    }

    // --- entries ---------------------------------------------------------------------------------

    void entriesOfAMixedNote()
    {
        const QString src =
                u"---\ntitle: x\n---\n\n# Tytuł\n\nAkapit\ndruga linia\n\n- [ ] zadanie ^abc\n\t- dziecko\n\t  kontynuacja\n1. jeden\n\n```js\ncode\n```\n"_s;
        const BlockDocument doc(src);
        QCOMPARE(entries(doc),
                 QStringList({u"fm:0:|---⏎title: x⏎---"_s, u"h:0:|# Tytuł"_s, u"p:0:|Akapit⏎druga linia"_s,
                              u"li:0:- |[ ] zadanie ^abc"_s, u"li:1:→- |dziecko⏎kontynuacja"_s, u"li:0:1. |jeden"_s,
                              u"code:0:|```js⏎code⏎```"_s}));
        QCOMPARE(doc.at(1).headingLevel, 1);
        QCOMPARE(doc.at(3).taskStatus, TaskStatus::Todo);
        QCOMPARE(doc.at(3).blockId, u"abc"_s);
        QVERIFY(!doc.at(4).taskStatus);
        QCOMPARE(doc.at(4).parent, 3);
        QCOMPARE(doc.at(6).lang, u"js"_s);
        QCOMPARE(doc.at(0).trailing, u"\n"_s);
        QCOMPARE(doc.at(3).trailing, QString());
        QList<int> lines;
        for (int i = 0; i < doc.count(); ++i)
            lines.append(doc.at(i).line);
        QCOMPARE(lines, QList<int>({0, 4, 6, 9, 10, 12, 14}));
        QCOMPARE(doc.indexAtLine(5), 1);
        QCOMPARE(doc.indexAtLine(11), 4);
        QCOMPARE(doc.indexAtLine(16), 6);
        QCOMPARE(doc.indexAtLine(17), 6); // the empty line after the final newline
        QCOMPARE(doc.indexAtLine(18), -1);
        QVERIFY(doc.hasChildren(3));
        QVERIFY(!doc.hasChildren(4));
        QCOMPARE(doc.subtreeRange(3), RowRange({3, 2}));
        QCOMPARE(doc.subtreeRange(1), RowRange({1, 1}));
        QCOMPARE(doc.blockSource(4), u"\t- dziecko\n\t  kontynuacja"_s);
    }

    void calloutsAndQuotesAreOneBlock()
    {
        const BlockDocument doc(u"> [!todo] Lista\n> - [ ] a\n>   - [ ] b\n\n> cytat\n> - x\n"_s);
        QCOMPARE(entries(doc),
                 QStringList({u"callout:0:|> [!todo] Lista⏎> - [ ] a⏎>   - [ ] b"_s, u"quote:0:|> cytat⏎> - x"_s}));
    }

    // A display formula between `$$` lines is one block, also when its lines look like list items
    // or quotes (SYNTAX §11): the editor's blocks and the parser's agree, also after an edit.
    void displayMathIsOneBlock()
    {
        BlockDocument doc(u"$$\nf(x) = a\n+ b\n- c\n$$\n\n- Wzór:\n  $$\n  a\n  > b\n  1. c\n  $$\n- dalej\n"_s);
        QCOMPARE(entries(doc), QStringList({u"p:0:|$$⏎f(x) = a⏎+ b⏎- c⏎$$"_s, u"li:0:- |Wzór:⏎$$⏎a⏎> b⏎1. c⏎$$"_s,
                                            u"li:0:- |dalej"_s}));
        QVERIFY(doc.setBody(1, u"Wzór:\n$$\nx\n+ y\n- z\n$$"_s).changed());
        QCOMPARE(doc.toString(), u"$$\nf(x) = a\n+ b\n- c\n$$\n\n- Wzór:\n  $$\n  x\n  + y\n  - z\n  $$\n- dalej\n"_s);
        QCOMPARE(entries(BlockDocument(doc.toString())), entries(doc));
        // Without a closing `$$` line the lines are blocks of their own.
        QCOMPARE(entries(BlockDocument(u"$$\na\n- b\n"_s)), QStringList({u"p:0:|$$⏎a"_s, u"li:0:- |b"_s}));
    }

    void propertiesOnlyAtTheTop()
    {
        const BlockDocument top(u"title:: X\ntags:: a\n\n- a\n\nkey:: value\n"_s);
        QCOMPARE(entries(top), QStringList({u"fm:0:|title:: X⏎tags:: a"_s, u"li:0:- |a"_s, u"p:0:|key:: value"_s}));
        const BlockDocument bom(u"\uFEFF---\na: 1\n---\nprop:: 1\n- x\n"_s);
        QCOMPARE(entries(bom), QStringList({u"fm:0:|---⏎a: 1⏎---"_s, u"fm:0:|prop:: 1"_s, u"li:0:- |x"_s}));
        QCOMPARE(bom.toString(), u"\uFEFF---\na: 1\n---\nprop:: 1\n- x\n"_s);
        const BlockDocument notTop(u"Akapit\n\n---\na: 1\n---\n"_s);
        QCOMPARE(entries(notTop), QStringList({u"p:0:|Akapit"_s, u"hr:0:|---"_s, u"h:0:|a: 1⏎---"_s}));
    }

    void crlfInsertions()
    {
        BlockDocument doc(u"- a\r\n"_s);
        doc.insertAfter(0, u"b\nc"_s, 0);
        QCOMPARE(doc.toString(), u"- a\r\n- b\r\n  c\r\n"_s);
        doc.split(1, 1); // the line break at the split point is dropped
        QCOMPARE(doc.toString(), u"- a\r\n- b\r\n- c\r\n"_s);
    }

    void leadingCommentBecomesABlock()
    {
        const BlockDocument doc(u"\n%% ukryty %%\n\nTekst\n"_s);
        QCOMPARE(entries(doc), QStringList({u"p:0:|%% ukryty %%"_s, u"p:0:|Tekst"_s}));
        QCOMPARE(doc.at(0).line, 1);
    }

    void emptyDocument()
    {
        BlockDocument doc;
        QVERIFY(doc.isEmpty());
        QCOMPARE(doc.toString(), QString());
        const EditResult r = doc.insertAfter(-1, u"pierwszy"_s, 0);
        QCOMPARE(doc.toString(), u"- pierwszy\n"_s);
        QCOMPARE(describe(r.changes), u"inserted 0+1"_s);
        QCOMPARE(r.focus, 0);
        QCOMPARE(r.cursor, 8);
        BlockDocument blank(u"\n\n"_s);
        blank.insertAfter(-1, u"x"_s, 0);
        QCOMPARE(blank.toString(), u"\n\n- x\n"_s);
    }

    // --- setBody ---------------------------------------------------------------------------------

    void setBodyReindentsContinuationLines()
    {
        BlockDocument doc(u"- a\n\t- b\n\t  c\n- d\n"_s);
        const EditResult r = doc.setBody(1, u"x\ny\n\nz"_s);
        QCOMPARE(doc.toString(), u"- a\n\t- x\n\t  y\n\n\t  z\n- d\n"_s);
        QCOMPARE(describe(r.changes), u"changed 1+1"_s);
        QCOMPARE(doc.at(1).body, u"x\ny\n\nz"_s);
        QVERIFY(!doc.setBody(1, u"x\ny\n\nz"_s).changed());
    }

    void setBodyKeepsLineEndingsAndOthers()
    {
        BlockDocument doc(u"Wstęp  \r\n\r\n- a\r\n- b\r\n\r\n```\r\nkod\r\n```"_s);
        doc.setBody(1, u"x\r\ny"_s);
        QCOMPARE(doc.toString(), u"Wstęp  \r\n\r\n- x\r\n  y\r\n- b\r\n\r\n```\r\nkod\r\n```"_s);
    }

    void setBodyChangesKind()
    {
        BlockDocument doc(u"Akapit\n\nKoniec\n"_s);
        doc.setBody(0, u"# Nagłówek"_s);
        QCOMPARE(doc.at(0).kind, BlockKind::Heading);
        QCOMPARE(doc.at(0).headingLevel, 1);
        doc.setBody(0, u"- punkt"_s);
        QCOMPARE(entries(doc), QStringList({u"li:0:- |punkt"_s, u"p:0:|Koniec"_s}));
        QCOMPARE(doc.toString(), u"- punkt\n\nKoniec\n"_s);
        // A list item typed into a paragraph that was glued to the next one gets a blank line.
        BlockDocument glued(u"# T\nAkapit\n"_s);
        glued.setBody(0, u"- x"_s);
        QCOMPARE(glued.toString(), u"- x\n\nAkapit\n"_s);
    }

    void setBodyOnEmptyItem()
    {
        BlockDocument doc(u"-\n- b\n"_s);
        doc.setBody(0, u"tekst"_s);
        QCOMPARE(doc.toString(), u"- tekst\n- b\n"_s);
    }

    // --- split -----------------------------------------------------------------------------------

    void splitContinuesTasks()
    {
        BlockDocument doc(u"- [ ] Kup mleko\n- inne\n"_s);
        const EditResult r = doc.split(0, 13);
        QCOMPARE(doc.toString(), u"- [ ] Kup mleko\n- [ ] \n- inne\n"_s);
        QCOMPARE(describe(r.changes), u"inserted 1+1"_s);
        QCOMPARE(r.focus, 1);
        QCOMPARE(r.cursor, 4);
        BlockDocument logseq(u"- TODO x\n"_s);
        logseq.split(0, 6);
        QCOMPARE(logseq.toString(), u"- TODO x\n- TODO \n"_s);
        BlockDocument done(u"- [x] zrobione ✅ 2026-10-14\n"_s);
        done.split(0, 4);
        QCOMPARE(done.toString(), u"- [x] \n- [ ] zrobione ✅ 2026-10-14\n"_s);
    }

    void splitInTheMiddle()
    {
        BlockDocument doc(u"- abc\n"_s);
        const EditResult r = doc.split(0, 1);
        QCOMPARE(doc.toString(), u"- a\n- bc\n"_s);
        QCOMPARE(describe(r.changes), u"inserted 1+1, changed 0+1"_s);
        QCOMPARE(r.focus, 1);
        QCOMPARE(r.cursor, 0);
    }

    void splitNumberedLists()
    {
        BlockDocument doc(u"1. a\n2. b\n"_s);
        const EditResult r = doc.split(0, 1);
        QCOMPARE(doc.toString(), u"1. a\n2. \n3. b\n"_s);
        QCOMPARE(describe(r.changes), u"inserted 1+1, changed 2+1"_s);
        BlockDocument lazy(u"1. a\n1. b\n"_s);
        lazy.split(0, 1);
        QCOMPARE(lazy.toString(), u"1. a\n1. \n1. b\n"_s);
        BlockDocument paren(u"  9) dziewięć\n"_s);
        paren.split(0, 9);
        QCOMPARE(paren.toString(), u"  9) dziewięć\n  10) \n"_s);
    }

    void splitWithChildren()
    {
        BlockDocument doc(u"- a\n\t- b\n"_s);
        doc.split(0, 1);
        QCOMPARE(doc.toString(), u"- a\n\t- \n\t- b\n"_s);
        QCOMPARE(doc.at(1).depth, 1);
        BlockDocument collapsed(u"- a\n\t- b\n"_s);
        const EditResult r = collapsed.split(0, 1, true);
        QCOMPARE(collapsed.toString(), u"- a\n\t- b\n- \n"_s);
        QCOMPARE(r.focus, 2);
    }

    void splitAtStartInsertsAbove()
    {
        BlockDocument doc(u"- [x] zrobione\n"_s);
        const EditResult r = doc.split(0, 0);
        QCOMPARE(doc.toString(), u"- [ ] \n- [x] zrobione\n"_s);
        QCOMPARE(describe(r.changes), u"inserted 0+1"_s);
        QCOMPARE(r.focus, 1);
        QCOMPARE(r.cursor, 0);
        BlockDocument numbered(u"1. a\n2. b\n"_s);
        numbered.split(1, 0);
        QCOMPARE(numbered.toString(), u"1. a\n2. \n3. b\n"_s);
    }

    void splitParagraphsAndHeadings()
    {
        BlockDocument doc(u"Ala ma kota\n\nDalej\n"_s);
        const EditResult r = doc.split(0, 4);
        QCOMPARE(doc.toString(), u"Ala \n\nma kota\n\nDalej\n"_s);
        QCOMPARE(r.focus, 1);
        QCOMPARE(r.cursor, 0);
        BlockDocument heading(u"# Tytuł\n"_s);
        heading.split(0, 4);
        QCOMPARE(heading.toString(), u"# Ty\ntuł\n"_s);
        QCOMPARE(entries(heading), QStringList({u"h:0:|# Ty"_s, u"p:0:|tuł"_s}));
        QVERIFY(!BlockDocument(u"```\nx\n```"_s).split(0, 2).changed());
    }

    // --- insertAfter -----------------------------------------------------------------------------

    void insertAfterSiblingsAndChildren()
    {
        const QString src = u"- a\n\t- a1\n- b\n"_s;
        BlockDocument doc(src);
        EditResult r = doc.insertAfter(0, u"nowy"_s, 0);
        QCOMPARE(doc.toString(), u"- a\n\t- a1\n- nowy\n- b\n"_s);
        QCOMPARE(describe(r.changes), u"inserted 2+1"_s);
        QCOMPARE(r.focus, 2);
        QCOMPARE(r.cursor, 4);
        BlockDocument child(src);
        child.insertAfter(0, u"pierwsze dziecko"_s, 1);
        QCOMPARE(child.toString(), u"- a\n\t- pierwsze dziecko\n\t- a1\n- b\n"_s);
        BlockDocument up(src);
        up.insertAfter(1, u"x"_s, 0);
        QCOMPARE(up.toString(), u"- a\n\t- a1\n- x\n- b\n"_s);
        BlockDocument first(src);
        first.insertAfter(-1, u"start"_s, 0);
        QCOMPARE(first.toString(), u"- start\n- a\n\t- a1\n- b\n"_s);
        BlockDocument deep(src);
        deep.insertAfter(2, u"dziecko b"_s, 5);
        QCOMPARE(deep.toString(), u"- a\n\t- a1\n- b\n\t- dziecko b\n"_s);
    }

    void insertAfterNumbered()
    {
        BlockDocument doc(u"1. a\n2. b\n"_s);
        doc.insertAfter(0, u"x"_s, 0);
        QCOMPARE(doc.toString(), u"1. a\n2. x\n3. b\n"_s);
        BlockDocument child(u"1. a\n"_s);
        child.insertAfter(0, u"x"_s, 1);
        QCOMPARE(child.toString(), u"1. a\n\t1. x\n"_s);
    }

    void insertParagraphs()
    {
        BlockDocument doc(u"- a\n\t- a1\n- b\n"_s);
        doc.insertAfter(1, u"akapit"_s, 0, false);
        QCOMPARE(doc.toString(), u"- a\n\t- a1\n\nakapit\n\n- b\n"_s);
        QCOMPARE(entries(doc).at(2), u"p:0:|akapit"_s);
        BlockDocument end(u"Tekst"_s);
        end.insertAfter(0, u"## Sekcja"_s, 0, false);
        QCOMPARE(end.toString(), u"Tekst\n\n## Sekcja"_s);
        QCOMPARE(end.at(1).kind, BlockKind::Heading);
    }

    void insertKeepsMissingFinalNewline()
    {
        BlockDocument doc(u"- a"_s);
        doc.insertAfter(0, u"b"_s, 0);
        QCOMPARE(doc.toString(), u"- a\n- b"_s);
    }

    // --- merge / remove --------------------------------------------------------------------------

    void mergeWithPrevious()
    {
        BlockDocument doc(u"- ab\n- cd\n"_s);
        const EditResult r = doc.mergeWithPrevious(1);
        QCOMPARE(doc.toString(), u"- abcd\n"_s);
        QCOMPARE(describe(r.changes), u"removed 1+1, changed 0+1"_s);
        QCOMPARE(r.focus, 0);
        QCOMPARE(r.cursor, 2);
        BlockDocument empty(u"- a\n- \n- c\n"_s);
        const EditResult e = empty.mergeWithPrevious(1);
        QCOMPARE(empty.toString(), u"- a\n- c\n"_s);
        QCOMPARE(describe(e.changes), u"removed 1+1"_s);
        QCOMPARE(e.cursor, 1);
        BlockDocument paras(u"Ala\n\nma kota\n"_s);
        paras.mergeWithPrevious(1);
        QCOMPARE(paras.toString(), u"Alama kota\n"_s);
        BlockDocument code(u"```\nx\n```\n- a\n"_s);
        const EditResult none = code.mergeWithPrevious(1);
        QVERIFY(!none.changed());
        QCOMPARE(none.focus, 0);
        QVERIFY(!code.mergeWithPrevious(0).changed());
    }

    void mergeKeepsChildren()
    {
        BlockDocument doc(u"- a\n- b\n\t- b1\n"_s);
        doc.mergeWithPrevious(1);
        QCOMPARE(doc.toString(), u"- ab\n\t- b1\n"_s);
        QCOMPARE(doc.at(1).parent, 0);
    }

    void removeBlocks()
    {
        BlockDocument doc(u"- a\n\t- b\n- c\n"_s);
        const EditResult r = doc.remove(0);
        QCOMPARE(doc.toString(), u"- c\n"_s);
        QCOMPARE(describe(r.changes), u"removed 0+2"_s);
        QCOMPARE(r.focus, 0);
        BlockDocument keep(u"- a\n\t- b\n- c\n"_s);
        keep.remove(0, false);
        QCOMPARE(keep.toString(), u"\t- b\n- c\n"_s);
        QCOMPARE(keep.at(0).depth, 0);
        BlockDocument paras(u"Akapit\n- lista\n\nKoniec\n"_s);
        const EditResult p = paras.remove(1);
        QCOMPARE(paras.toString(), u"Akapit\n\nKoniec\n"_s);
        QCOMPARE(describe(p.changes), u"removed 1+1, changed 0+1"_s);
        BlockDocument last(u"- a\n- b"_s);
        last.remove(1);
        QCOMPARE(last.toString(), u"- a"_s);
        BlockDocument numbered(u"1. a\n2. b\n3. c\n"_s);
        numbered.remove(0);
        QCOMPARE(numbered.toString(), u"2. b\n3. c\n"_s);
        numbered.remove(1);
        QCOMPARE(numbered.toString(), u"2. b\n"_s);
        BlockDocument middle(u"1. a\n2. b\n3. c\n"_s);
        middle.remove(1);
        QCOMPARE(middle.toString(), u"1. a\n2. c\n"_s);
    }

    // --- indent / outdent ------------------------------------------------------------------------

    void indentWithTabs()
    {
        BlockDocument doc(u"- a\n- b\n- c\n"_s);
        const EditResult r = doc.indent(1);
        QCOMPARE(doc.toString(), u"- a\n\t- b\n- c\n"_s);
        QCOMPARE(describe(r.changes), u"changed 0+2"_s);
        QCOMPARE(doc.at(1).depth, 1);
        QCOMPARE(doc.at(1).parent, 0);
        doc.indent(2);
        QCOMPARE(doc.toString(), u"- a\n\t- b\n\t- c\n"_s);
        QVERIFY(!doc.indent(0).changed());
        QVERIFY(!doc.indent(1).changed()); // no previous sibling
    }

    void indentFollowsSpaceStyle()
    {
        BlockDocument doc(u"- a\n  - x\n- b\n"_s);
        doc.indent(2);
        QCOMPARE(doc.toString(), u"- a\n  - x\n  - b\n"_s);
        BlockDocument fresh(u"- a\n  - x\n- b\n- c\n"_s);
        fresh.indent(3);
        QCOMPARE(fresh.toString(), u"- a\n  - x\n- b\n  - c\n"_s);
    }

    void indentWholeSubtree()
    {
        BlockDocument doc(u"- a\n- b\n\t- b1\n\t  ciąg\n"_s);
        doc.indent(1);
        QCOMPARE(doc.toString(), u"- a\n\t- b\n\t\t- b1\n\t\t  ciąg\n"_s);
        QCOMPARE(doc.at(2).depth, 2);
    }

    void indentRenumbers()
    {
        BlockDocument doc(u"1. a\n2. b\n3. c\n"_s);
        doc.indent(1);
        QCOMPARE(doc.toString(), u"1. a\n\t1. b\n2. c\n"_s);
        doc.indent(2);
        QCOMPARE(doc.toString(), u"1. a\n\t1. b\n\t2. c\n"_s);
    }

    void indentConvertsParagraphs()
    {
        BlockDocument doc(u"- a\n\nAkapit\n"_s);
        const EditResult r = doc.indent(1);
        QCOMPARE(doc.toString(), u"- a\n\n\t- Akapit\n"_s);
        QCOMPARE(describe(r.changes), u"changed 0+2"_s);
        BlockDocument alone(u"Akapit\ndruga linia\n"_s);
        alone.indent(0);
        QCOMPARE(alone.toString(), u"- Akapit\n  druga linia\n"_s);
        QVERIFY(!BlockDocument(u"---\n"_s).indent(0).changed());
    }

    void outdentInPlace()
    {
        BlockDocument doc(u"- a\n\t- b\n\t- c\n"_s);
        const EditResult r = doc.outdent(1);
        QCOMPARE(doc.toString(), u"- a\n- b\n\t- c\n"_s);
        QCOMPARE(describe(r.changes), u"changed 0+3"_s);
        QCOMPARE(doc.at(1).depth, 0);
        QCOMPARE(doc.at(2).parent, 1);
        QVERIFY(!doc.outdent(1).changed());
    }

    void outdentSubtreeAndNumbering()
    {
        BlockDocument doc(u"- p\n  - x\n    - x1\n  - y\n"_s);
        doc.outdent(1);
        QCOMPARE(doc.toString(), u"- p\n- x\n  - x1\n  - y\n"_s);
        BlockDocument numbered(u"1. a\n\t1. x\n\t2. y\n\t3. z\n"_s);
        numbered.outdent(2);
        QCOMPARE(numbered.toString(), u"1. a\n\t1. x\n2. y\n\t1. z\n"_s);
    }

    // --- move ------------------------------------------------------------------------------------

    void moveSubtrees()
    {
        BlockDocument doc(u"- a\n\t- a1\n- b\n- c\n"_s);
        EditResult r = doc.moveUp(2);
        QCOMPARE(doc.toString(), u"- b\n- a\n\t- a1\n- c\n"_s);
        QCOMPARE(describe(r.changes), u"moved 2+1->0"_s);
        QCOMPARE(r.focus, 0);
        r = doc.moveDown(0);
        QCOMPARE(doc.toString(), u"- a\n\t- a1\n- b\n- c\n"_s);
        QCOMPARE(describe(r.changes), u"moved 1+2->0"_s);
        QCOMPARE(r.focus, 2);
        QVERIFY(!doc.moveUp(0).changed());
        QVERIFY(!doc.moveUp(1).changed()); // first child
        QVERIFY(!doc.moveDown(3).changed());
    }

    void moveKeepsSeparatorsInPlace()
    {
        BlockDocument doc(u"Akapit 1\n\nAkapit 2\n"_s);
        doc.moveDown(0);
        QCOMPARE(doc.toString(), u"Akapit 2\n\nAkapit 1\n"_s);
        BlockDocument noFinal(u"- a\n- b"_s);
        noFinal.moveUp(1);
        QCOMPARE(noFinal.toString(), u"- b\n- a"_s);
        BlockDocument crlf(u"# T\r\n\r\n- x\r\n- y\r\n"_s);
        crlf.moveUp(2);
        QCOMPARE(crlf.toString(), u"# T\r\n\r\n- y\r\n- x\r\n"_s);
    }

    void moveNormalisesSiblingIndentation()
    {
        BlockDocument doc(u"- p\n   - x\n  - y\n"_s);
        doc.moveUp(2);
        QCOMPARE(doc.toString(), u"- p\n   - y\n  - x\n"_s);
        QCOMPARE(doc.at(1).parent, 0);
        QCOMPARE(doc.at(2).parent, 0);
    }

    void moveRenumbers()
    {
        BlockDocument doc(u"1. pierwszy\n2. drugi\n"_s);
        doc.moveUp(1);
        QCOMPARE(doc.toString(), u"1. drugi\n2. pierwszy\n"_s);
    }

    void moveMixedTopLevelBlocks()
    {
        BlockDocument doc(u"# Tytuł\n\n- a\n- b\n\nAkapit\n"_s);
        doc.moveUp(3);
        QCOMPARE(doc.toString(), u"# Tytuł\n\n- a\n\nAkapit\n\n- b\n"_s);
        QCOMPARE(entries(doc), QStringList({u"h:0:|# Tytuł"_s, u"li:0:- |a"_s, u"p:0:|Akapit"_s, u"li:0:- |b"_s}));
    }

    void frontmatterStaysAtTheTop()
    {
        // Review regression: the YAML frontmatter and Logseq page properties only work at the very
        // top, so nothing may move above them and they never move down.
        const QString src = u"---\na: 1\n---\ntype:: plan\n\n- x\n- y\n"_s;
        BlockDocument up(src);
        QVERIFY(!up.moveUp(2).changed());
        QVERIFY(!up.moveUp(1).changed());
        QVERIFY(!up.moveDown(0).changed());
        QVERIFY(!up.moveDown(1).changed());
        QCOMPARE(up.toString(), src);
        BlockDocument first(src);
        const EditResult r = first.insertAfter(-1, u"nowy"_s, 0);
        QCOMPARE(first.toString(), u"---\na: 1\n---\ntype:: plan\n\n- nowy\n- x\n- y\n"_s);
        QCOMPARE(r.focus, 2);
        BlockDocument between(src);
        between.insertAfter(0, u"akapit"_s, 0, false);
        QCOMPARE(between.toString(), u"---\na: 1\n---\ntype:: plan\n\nakapit\n\n- x\n- y\n"_s);
        BlockDocument item(src);
        item.insertAfter(0, u"punkt"_s, 0);
        QCOMPARE(item.toString(), u"---\na: 1\n---\ntype:: plan\n\n- punkt\n- x\n- y\n"_s);
        // Moves below the frontmatter still work.
        BlockDocument below(src);
        QVERIFY(below.moveUp(3).changed());
        QCOMPARE(below.toString(), u"---\na: 1\n---\ntype:: plan\n\n- y\n- x\n"_s);
    }

    void positionalKindsFollowEdits()
    {
        // Page properties are only page properties at the top: the model follows the text when an
        // edit changes what comes before them.
        BlockDocument doc(u"---\na: 1\n---\ntype:: plan\n\nTekst\n"_s);
        QCOMPARE(doc.at(1).kind, BlockKind::Frontmatter);
        doc.setBody(0, u"Wstęp"_s);
        QCOMPARE(doc.toString(), u"Wstęp\n\ntype:: plan\n\nTekst\n"_s);
        QCOMPARE(entries(doc), entries(BlockDocument(doc.toString())));
        BlockDocument removed(u"Wstęp\n\nkey:: value\n\n- a\n"_s);
        QCOMPARE(removed.at(1).kind, BlockKind::Paragraph);
        const EditResult r = removed.remove(0);
        QCOMPARE(removed.toString(), u"key:: value\n\n- a\n"_s);
        QCOMPARE(removed.at(0).kind, BlockKind::Frontmatter);
        QCOMPARE(describe(r.changes), u"removed 0+1, changed 0+1"_s);
        QCOMPARE(entries(removed), entries(BlockDocument(removed.toString())));
    }

    void ruleNeverOpensFrontmatter()
    {
        // Review regression: a `---` rule that becomes the first line, with another `---` line
        // below it, would turn everything between them into YAML frontmatter on reload.
        BlockDocument moved(u"Wstęp\n\n---\n\nTytuł\n---\n"_s);
        QCOMPARE(moved.at(1).kind, BlockKind::Rule);
        QVERIFY(moved.moveUp(1).changed());
        QCOMPARE(moved.toString(), u"\n---\n\nWstęp\n\nTytuł\n---\n"_s);
        QCOMPARE(entries(moved), entries(BlockDocument(moved.toString())));
        QCOMPARE(moved.at(0).line, 1);
        BlockDocument typed(u"Akapit\n\n- a\n\n***\n"_s);
        typed.setBody(2, u"---"_s);
        typed.setBody(0, u"---"_s);
        QCOMPARE(typed.toString(), u"\n---\n\n- a\n\n---\n"_s);
        QCOMPARE(entries(typed), entries(BlockDocument(typed.toString())));
        BlockDocument removed(u"Wstęp\n\n---\n\nTekst\n\n---\n"_s);
        removed.remove(0);
        QCOMPARE(removed.toString(), u"\n---\n\nTekst\n\n---\n"_s);
        QCOMPARE(entries(removed), entries(BlockDocument(removed.toString())));
        // A real frontmatter stays one.
        BlockDocument fm(u"---\na: 1\n---\n\nTekst\n"_s);
        fm.setBody(1, u"Inny tekst"_s);
        QCOMPARE(fm.toString(), u"---\na: 1\n---\n\nInny tekst\n"_s);
    }

    void indentedBlocksAfterListsStayTopLevel()
    {
        // Review regression: an indented paragraph after a list item (e.g. the text after the
        // cursor when splitting "# H" before the space) would continue that item on reload.
        BlockDocument doc(u"# H\n"_s);
        doc.split(0, 1);
        QCOMPARE(doc.toString(), u"#\n H\n"_s);
        doc.convertToList(0);
        QCOMPARE(doc.toString(), u"- #\n\nH\n"_s);
        QCOMPARE(entries(doc), entries(BlockDocument(doc.toString())));
        BlockDocument moved(u"  Wcięty akapit\n  druga linia\n\n- a\n"_s);
        moved.moveDown(0);
        QCOMPARE(moved.toString(), u"- a\n\nWcięty akapit\ndruga linia\n"_s);
        QCOMPARE(entries(moved), entries(BlockDocument(moved.toString())));
    }

    void splitDropsEmptyEdgeLines()
    {
        // Enter on an empty line of a multi-line block starts the new block there instead of
        // leaving blank lines at the edges of both blocks.
        BlockDocument doc(u"- a\n\n  b\n"_s);
        QCOMPARE(doc.at(0).body, u"a\n\nb"_s);
        doc.split(0, 2);
        QCOMPARE(doc.toString(), u"- a\n- b\n"_s);
        QCOMPARE(entries(doc), entries(BlockDocument(doc.toString())));
        BlockDocument para(u"Setext\n===\n"_s);
        para.split(0, 6);
        QCOMPARE(para.toString(), u"Setext\n\n===\n"_s);
        QCOMPARE(entries(para), entries(BlockDocument(para.toString())));
    }

    // --- tasks -----------------------------------------------------------------------------------

    void toggleTasks()
    {
        BlockDocument doc(u"- [ ] a\n- b\n"_s);
        EditResult r = doc.toggleTask(0, Today);
        QCOMPARE(doc.toString(), u"- [x] a ✅ 2026-10-14\n- b\n"_s);
        QCOMPARE(describe(r.changes), u"changed 0+1"_s);
        QCOMPARE(doc.at(0).taskStatus, TaskStatus::Done);
        doc.toggleTask(1, Today);
        QCOMPARE(doc.toString(), u"- [x] a ✅ 2026-10-14\n- [ ] b\n"_s);
        doc.setTaskStatus(1, TaskStatus::Doing, Today);
        QCOMPARE(doc.at(1).taskStatus, TaskStatus::Doing);
        BlockDocument para(u"Zrób to\n"_s);
        para.setTaskStatus(0, TaskStatus::Doing, Today);
        QCOMPARE(para.toString(), u"- [/] Zrób to\n"_s);
        QCOMPARE(para.at(0).kind, BlockKind::ListItem);
        QVERIFY(!BlockDocument(u"```\n- [ ] x\n```"_s).toggleTask(0, Today).changed());
    }

    void recurringTaskInsertsBlock()
    {
        BlockDocument doc(u"- [ ] Podlać 🔁 every day 📅 2026-10-14\n- inne\n"_s);
        const EditResult r = doc.toggleTask(0, Today);
        QCOMPARE(
                doc.toString(),
                u"- [ ] Podlać 🔁 every day 📅 2026-10-15\n- [x] Podlać 🔁 every day 📅 2026-10-14 ✅ 2026-10-14\n- inne\n"_s);
        QCOMPARE(describe(r.changes), u"inserted 0+1, changed 1+1"_s);
        QCOMPARE(r.focus, 1);
        QCOMPARE(doc.at(0).taskStatus, TaskStatus::Todo);
        QCOMPARE(doc.at(1).taskStatus, TaskStatus::Done);
    }

    void recurringTaskInQuoteStaysOneBlock()
    {
        BlockDocument doc(u"> - [ ] x 🔁 every day 📅 2026-10-14\n"_s);
        QCOMPARE(doc.at(0).kind, BlockKind::Quote);
        doc.toggleTask(0, Today);
        QCOMPARE(doc.toString(),
                 u"> - [ ] x 🔁 every day 📅 2026-10-15\n> - [x] x 🔁 every day 📅 2026-10-14 ✅ 2026-10-14\n"_s);
        QCOMPARE(doc.count(), 1);
    }

    void tasksInsideCallouts()
    {
        BlockDocument doc(u"> [!todo] Dziś\n> - [ ] a\n"_s);
        QVERIFY(!doc.toggleTask(0, Today).changed());
        doc.toggleTask(0, Today, 1);
        QCOMPARE(doc.toString(), u"> [!todo] Dziś\n> - [x] a ✅ 2026-10-14\n"_s);
    }

    // --- block ids / conversion ------------------------------------------------------------------

    void ensureBlockIds()
    {
        BlockDocument doc(u"- a\n- b ^istn\n"_s);
        QCOMPARE(doc.ensureBlockId(1).id, u"istn"_s);
        const BlockIdResult r = doc.ensureBlockId(0);
        QVERIFY(QRegularExpression(u"^[a-z0-9]{6}$"_s).match(r.id).hasMatch());
        QCOMPARE(doc.toString(), u"- a ^"_s + r.id + u"\n- b ^istn\n"_s);
        QCOMPARE(describe(r.edit.changes), u"changed 0+1"_s);
        QCOMPARE(doc.at(0).blockId, r.id);
        QCOMPARE(doc.ensureBlockId(0).id, r.id);
        BlockDocument code(u"```\nx\n```\nPo\n"_s);
        const BlockIdResult c = code.ensureBlockId(0);
        QCOMPARE(code.toString(), u"```\nx\n```\n^"_s + c.id + u"\nPo\n"_s);
        const ParsedNote n = parseNote(u"a.md"_s, code.toString());
        QCOMPARE(n.blocks[n.blockIds.value(c.id)].kind, BlockKind::Code);
        QCOMPARE(n.blocks.last().text, u"Po"_s);
        QVERIFY(BlockDocument(u"---\na: 1\n---\n"_s).ensureBlockId(0).id.isEmpty());
        // Ids used elsewhere in the vault are avoided too.
        BlockDocument other(u"- x\n"_s);
        int asked = 0;
        const BlockIdResult avoided = other.ensureBlockId(0, [&](const QString &) { return ++asked < 4; });
        QCOMPARE(asked, 4);
        QVERIFY(!avoided.id.isEmpty());
    }

    void convertToList()
    {
        BlockDocument doc(u"Akapit\ndruga\n"_s);
        const EditResult r = doc.convertToList(0);
        QCOMPARE(doc.toString(), u"- Akapit\n  druga\n"_s);
        QCOMPARE(describe(r.changes), u"changed 0+1"_s);
        BlockDocument code(u"```\nx\n\ny\n```\n"_s);
        code.convertToList(0);
        QCOMPARE(code.toString(), u"- ```\n  x\n\n  y\n  ```\n"_s);
        QCOMPARE(code.at(0).kind, BlockKind::ListItem);
        BlockDocument heading(u"# T\n\nPo\n"_s);
        heading.convertToList(0);
        QCOMPARE(heading.toString(), u"- # T\n\nPo\n"_s);
        QVERIFY(!BlockDocument(u"***\n"_s).convertToList(0).changed());
        QVERIFY(!BlockDocument(u"- x\n"_s).convertToList(0).changed());
    }

    void convertToParagraph_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("block");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<QStringList>("kinds");
        QTest::newRow("middle of a list")
                << u"- jabłka\n- gruszki\n- śliwki\n"_s << 1 << u"- jabłka\n\ngruszki\n\n- śliwki\n"_s
                << QStringList{u"li:0:- |jabłka"_s, u"p:0:|gruszki"_s, u"li:0:- |śliwki"_s};
        QTest::newRow("first item") << u"- Zażółć\n- gęślą\n"_s << 0 << u"Zażółć\n\n- gęślą\n"_s
                                    << QStringList{u"p:0:|Zażółć"_s, u"li:0:- |gęślą"_s};
        QTest::newRow("last item, no final newline")
                << u"- a\n- ostatni"_s << 1 << u"- a\n\nostatni"_s << QStringList{u"li:0:- |a"_s, u"p:0:|ostatni"_s};
        QTest::newRow("task checkbox goes too")
                << u"- [ ] Kupić mleko 📅 2026-10-16\n"_s << 0 << u"Kupić mleko 📅 2026-10-16\n"_s
                << QStringList{u"p:0:|Kupić mleko 📅 2026-10-16"_s};
        QTest::newRow("logseq task") << u"- TODO zadzwonić\n"_s << 0 << u"zadzwonić\n"_s
                                     << QStringList{u"p:0:|zadzwonić"_s};
        QTest::newRow("empty item") << u"- a\n- \n"_s << 1 << u"- a\n\n\n"_s << QStringList{u"li:0:- |a"_s, u"p:0:|"_s};
        QTest::newRow("children move out")
                << u"- rodzic\n\t- dziecko\n\t\t- wnuk\n- dalej\n"_s << 0
                << u"rodzic\n\n- dziecko\n\t- wnuk\n- dalej\n"_s
                << QStringList{u"p:0:|rodzic"_s, u"li:0:- |dziecko"_s, u"li:1:→- |wnuk"_s, u"li:0:- |dalej"_s};
        QTest::newRow("children with spaces") << u"- a\n  - b\n    - c\n"_s << 0 << u"a\n\n- b\n  - c\n"_s
                                              << QStringList{u"p:0:|a"_s, u"li:0:- |b"_s, u"li:1:  - |c"_s};
        QTest::newRow("continuation lines, crlf")
                << u"- pierwsza\r\n  druga\r\n- b\r\n"_s << 0 << u"pierwsza\r\ndruga\r\n\r\n- b\r\n"_s
                << QStringList{u"p:0:|pierwsza⏎druga"_s, u"li:0:- |b"_s};
        QTest::newRow("numbered list keeps its numbers") << u"1. a\n2. b\n3. c\n"_s << 1 << u"1. a\n\nb\n\n3. c\n"_s
                                                         << QStringList{u"li:0:1. |a"_s, u"p:0:|b"_s, u"li:0:3. |c"_s};
        QTest::newRow("numbered children join the list")
                << u"1. a\n   1. x\n   2. y\n2. b\n"_s << 0 << u"a\n\n1. x\n2. y\n3. b\n"_s
                << QStringList{u"p:0:|a"_s, u"li:0:1. |x"_s, u"li:0:2. |y"_s, u"li:0:3. |b"_s};
        QTest::newRow("heading text") << u"- # Tytuł\n- b\n"_s << 0 << u"# Tytuł\n- b\n"_s
                                      << QStringList{u"h:0:|# Tytuł"_s, u"li:0:- |b"_s};
        QTest::newRow("after frontmatter")
                << u"---\nbrulion: document\n---\n- punkt\n"_s << 1 << u"---\nbrulion: document\n---\npunkt\n"_s
                << QStringList{u"fm:0:|---⏎brulion: document⏎---"_s, u"p:0:|punkt"_s};
        QTest::newRow("after a nested item") << u"- a\n\t- a1\n- b\n"_s << 2 << u"- a\n\t- a1\n\nb\n"_s
                                             << QStringList{u"li:0:- |a"_s, u"li:1:→- |a1"_s, u"p:0:|b"_s};
    }

    void convertToParagraph()
    {
        QFETCH(QString, content);
        QFETCH(int, block);
        QFETCH(QString, expected);
        QFETCH(QStringList, kinds);
        BlockDocument doc(content);
        const QList<quint64> before = uids(doc);
        const quint64 uid = doc.at(block).uid;
        const EditResult r = doc.convertToParagraph(block);
        QVERIFY(r.changed());
        QCOMPARE(doc.toString(), expected);
        QCOMPARE(entries(doc), kinds);
        QCOMPARE(r.focus, doc.indexOfUid(uid));
        QCOMPARE(r.cursor, 0);
        QString why;
        QVERIFY2(changesReplay(before, uids(doc), r.changes, &why), qPrintable(why + u": "_s + describe(r.changes)));
        // The text parses back to the same blocks (only empty paragraphs are an in-memory state).
        if (!expected.contains(u"\n\n\n"_s))
            QCOMPARE(entries(BlockDocument(doc.toString())), kinds);
    }

    void convertToParagraphLeavesOthers()
    {
        // Nested items, paragraphs and other blocks are not list items to leave.
        for (const auto &[content, block] :
             {std::pair{u"- a\n\t- b\n"_s, 1}, std::pair{u"Akapit\n"_s, 0}, std::pair{u"# H\n"_s, 0},
              std::pair{u"---\na: 1\n---\n"_s, 0}, std::pair{u"- a\n"_s, 5}, std::pair{u"- a\n"_s, -1}}) {
            BlockDocument doc(content);
            QVERIFY2(!doc.convertToParagraph(block).changed(), qPrintable(content));
            QCOMPARE(doc.toString(), content);
        }
        // Untouched blocks keep their bytes.
        const QString src = u"*   odstępy   \r\n\r\n+ inny\r\n+ trzeci  \r\n\r\nKoniec  "_s;
        BlockDocument doc(src);
        doc.convertToParagraph(1);
        QCOMPARE(doc.toString(), u"*   odstępy   \r\n\r\ninny\r\n\r\n+ trzeci  \r\n\r\nKoniec  "_s);
        // Undo-like round trip: the paragraph made a list again is the original list item.
        BlockDocument again(u"- jeden\n- dwa\n"_s);
        again.convertToParagraph(1);
        again.convertToList(1);
        QCOMPARE(again.toString(), u"- jeden\n\n- dwa\n"_s);
    }

    void convertToParagraphNeverMakesFrontmatter()
    {
        // "+ ---" is a list item holding a rule (not a rule itself, unlike "- ---"): as the first
        // block it would become a `---` line that, with another one below, reads as YAML
        // frontmatter on reload — everything between them would turn into properties.
        for (const QString &content : {u"+ ---\n\nŻółta łódź\n\n---\n"_s, u"+ ---\r\n- b\r\n\r\n---\r\n"_s}) {
            BlockDocument doc(content);
            QCOMPARE(doc.at(0).kind, BlockKind::ListItem);
            QVERIFY(doc.convertToParagraph(0).changed());
            const QString out = doc.toString();
            QVERIFY2(!findFrontmatter(out), qPrintable(out));
            const BlockDocument reloaded(out);
            QCOMPARE(reloaded.count(), doc.count());
            for (int i = 0; i < reloaded.count(); ++i)
                QCOMPARE(reloaded.at(i).kind, doc.at(i).kind);
            QVERIFY(reloaded.at(0).kind != BlockKind::Frontmatter);
        }
    }

    // --- performance ---------------------------------------------------------------------------

    void largeDocument()
    {
        QString src;
        for (int n = 0; n < 1000; ++n) {
            src += u"## Sekcja %1\n\n- [ ] Zadanie [[Strona]] #tag 📅 2026-10-15 ^id%1\n\t- podpunkt `kod`\n\t  ciąg\n\n"_s
                           .arg(n);
            src += u"Akapit z ((abc123)) i [link](Strona%20B.md)\n\n"_s;
        }
        QElapsedTimer t;
        t.start();
        BlockDocument doc(src);
        QCOMPARE(doc.toString(), src);
        QCOMPARE(doc.count(), 4000);
        for (int k = 0; k < 20; ++k) {
            doc.setBody(1 + 4 * k, u"zmienione"_s);
            doc.split(2 + 4 * k, 3);
            doc.indent(5 + 4 * k);
        }
        QVERIFY2(t.elapsed() < 10000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(t.elapsed())));
    }

    // --- untouched blocks stay byte-identical ----------------------------------------------------

    void untouchedBlocksKeepBytes()
    {
        const QString src =
                u"---\r\na:   1\r\n---\r\n\r\n#   Tytuł   \r\n\r\n*   odstępy\r\n\t\t*  głęboko\r\n+ inny\r\n\r\n| a |b|\r\n|-|-|\r\n\r\nKoniec  "_s;
        BlockDocument doc(src);
        QCOMPARE(doc.toString(), src);
        doc.setBody(4, u"inny tekst"_s);
        QCOMPARE(
                doc.toString(),
                u"---\r\na:   1\r\n---\r\n\r\n#   Tytuł   \r\n\r\n*   odstępy\r\n\t\t*  głęboko\r\n+ inny tekst\r\n\r\n| a |b|\r\n|-|-|\r\n\r\nKoniec  "_s);
        doc.moveUp(4);
        QCOMPARE(
                doc.toString(),
                u"---\r\na:   1\r\n---\r\n\r\n#   Tytuł   \r\n\r\n+ inny tekst\r\n*   odstępy\r\n\t\t*  głęboko\r\n\r\n| a |b|\r\n|-|-|\r\n\r\nKoniec  "_s);
    }
};

QTEST_GUILESS_MAIN(TestBlockDocument)
#include "tst_markdown_blockdoc.moc"
