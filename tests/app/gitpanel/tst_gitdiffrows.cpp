// The changes panel's pure rows (src/app/gitpanel/gitdiffrows.h): a diff's hunks with their
// headers and the lines each changes (the same rule as gitcli::Hunk, so a hunk shown is the hunk
// staged), unified and split rows, word changes, whitespace ignored, the whole file, "skip" rows;
// found rows and a line's rich text (escaped); the file list as a tree (folded folders, chains of
// folders in one row); review comments for the chat (the lines, the language, a fence longer than
// the lines' backticks). Polish text, CRLF, empty texts.
#include "gitdiffrows.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::gitpanel;

namespace {

QString numbered(int count)
{
    QString out;
    for (int i = 1; i <= count; ++i)
        out += QString::number(i) + u'\n';
    return out;
}

QStringList kinds(const QVariantList &rows)
{
    QStringList out;
    for (const QVariant &v : rows)
        out.append(v.toMap().value(u"kind"_s).toString());
    return out;
}

QVariantMap at(const QVariantList &rows, int i)
{
    return rows.at(i).toMap();
}

QVariantMap change(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return QVariantMap{{u"path"_s, path},
                       {u"oldPath"_s, QString()},
                       {u"status"_s, u"modified"_s},
                       {u"name"_s, slash < 0 ? path : path.sliced(slash + 1)},
                       {u"dir"_s, slash < 0 ? QString() : path.left(slash)}};
}

} // namespace

class TestGitDiffRows : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void unifiedHunks();
    void splitRows();
    void wordChanges();
    void ignoreWhitespace();
    void wholeFile();
    void identicalAndEmpty();
    void crlf();
    void find();
    void html();
    void tree();
    void comment();
    void languages_data();
    void languages();
};

void TestGitDiffRows::unifiedHunks()
{
    QString changed = numbered(30);
    changed.replace(u"\n3\n"_s, u"\ntrzy\n"_s);
    changed.replace(u"\n25\n"_s, u"\n25\nnowa po 25\n"_s);
    const QVariantList rows = diffRows(numbered(30), changed, {});
    QCOMPARE(kinds(rows).count(u"hunk"_s), 2);
    // hunk 1: lines 1–6 (3 replaced); a skip; hunk 2: 23–28 with one insertion; a skip at the end.
    QCOMPARE(at(rows, 0).value(u"kind"_s).toString(), u"hunk"_s);
    QCOMPARE(at(rows, 0).value(u"line"_s).toString(), u"@@ -1,6 +1,6 @@"_s);
    QCOMPARE(at(rows, 0).value(u"oldFirst"_s).toInt(), 3);
    QCOMPARE(at(rows, 0).value(u"oldLast"_s).toInt(), 3);
    QCOMPARE(at(rows, 0).value(u"newFirst"_s).toInt(), 3);
    QCOMPARE(at(rows, 0).value(u"newLast"_s).toInt(), 3);
    QCOMPARE(at(rows, 3).value(u"kind"_s).toString(), u"delete"_s);
    QCOMPARE(at(rows, 3).value(u"oldNo"_s).toInt(), 3);
    QCOMPARE(at(rows, 3).value(u"line"_s).toString(), u"3"_s);
    QCOMPARE(at(rows, 4).value(u"kind"_s).toString(), u"insert"_s);
    QCOMPARE(at(rows, 4).value(u"newNo"_s).toInt(), 3);
    QCOMPARE(at(rows, 4).value(u"line"_s).toString(), u"trzy"_s);
    const int skip = int(kinds(rows).indexOf(u"skip"_s));
    QCOMPARE(at(rows, skip).value(u"hiddenLines"_s).toInt(), 16); // 7 … 22
    const int second = int(kinds(rows).lastIndexOf(u"hunk"_s));
    QCOMPARE(at(rows, second).value(u"line"_s).toString(), u"@@ -23,6 +23,7 @@"_s);
    QCOMPARE(at(rows, second).value(u"newFirst"_s).toInt(), 26);
    QCOMPARE(at(rows, second).value(u"newLast"_s).toInt(), 26);
    QCOMPARE(at(rows, second).value(u"oldFirst"_s).toInt(), 26);
    QCOMPARE(at(rows, second).value(u"hunk"_s).toInt(), 1);
    QCOMPARE(kinds(rows).last(), u"skip"_s);
    QCOMPARE(at(rows, int(rows.size()) - 1).value(u"hiddenLines"_s).toInt(), 2); // 29, 30
    // Every row has every key.
    for (const QVariant &v : rows) {
        const QVariantMap r = v.toMap();
        for (const QString &k : {u"kind"_s, u"hunk"_s, u"oldNo"_s, u"newNo"_s, u"line"_s, u"leftLine"_s, u"rightLine"_s,
                                 u"segs"_s, u"leftSegs"_s, u"rightSegs"_s, u"hiddenLines"_s, u"oldFirst"_s, u"oldLast"_s,
                                 u"newFirst"_s, u"newLast"_s})
            QVERIFY2(r.contains(k), qPrintable(k));
    }
}

void TestGitDiffRows::splitRows()
{
    const QVariantList rows = diffRows(u"a\nb\nc\nd\n"_s, u"a\nB\nc\nx\ny\n"_s, {.split = true});
    QCOMPARE(kinds(rows), (QStringList{u"hunk"_s, u"equal"_s, u"change"_s, u"equal"_s, u"change"_s, u"insert"_s}));
    QCOMPARE(at(rows, 2).value(u"leftLine"_s).toString(), u"b"_s);
    QCOMPARE(at(rows, 2).value(u"rightLine"_s).toString(), u"B"_s);
    QCOMPARE(at(rows, 2).value(u"oldNo"_s).toInt(), 2);
    QCOMPARE(at(rows, 2).value(u"newNo"_s).toInt(), 2);
    QCOMPARE(at(rows, 5).value(u"oldNo"_s).toInt(), 0);
    QCOMPARE(at(rows, 5).value(u"newNo"_s).toInt(), 5);
    QCOMPARE(at(rows, 5).value(u"rightLine"_s).toString(), u"y"_s);
    QCOMPARE(at(rows, 1).value(u"line"_s).toString(), QString());
    QCOMPARE(at(rows, 0).value(u"line"_s).toString(), u"@@ -1,4 +1,5 @@"_s);
}

void TestGitDiffRows::wordChanges()
{
    const QVariantList rows = diffRows(u"zażółć gęślą jaźń\n"_s, u"zażółć GĘŚLĄ jaźń\n"_s, {});
    const QVariantList del = at(rows, 1).value(u"segs"_s).toList();
    QCOMPARE(del.size(), 3);
    QCOMPARE(del.at(0).toMap().value(u"text"_s).toString(), u"zażółć "_s);
    QCOMPARE(del.at(1).toMap().value(u"text"_s).toString(), u"gęślą"_s);
    QVERIFY(del.at(1).toMap().value(u"changed"_s).toBool());
    QCOMPARE(del.at(2).toMap().value(u"text"_s).toString(), u" jaźń"_s);
    const QVariantList ins = at(rows, 2).value(u"segs"_s).toList();
    QCOMPARE(ins.at(1).toMap().value(u"text"_s).toString(), u"GĘŚLĄ"_s);
    // Nothing in common: no highlight (the whole line is new).
    const QVariantList other = diffRows(u"abc\n"_s, u"xyz\n"_s, {});
    QVERIFY(at(other, 1).value(u"segs"_s).toList().isEmpty());
    // A word changed in its middle is marked whole.
    const QVariantList mid = diffRows(u"funkcja(stary)\n"_s, u"funkcja(nowy)\n"_s, {});
    QCOMPARE(at(mid, 1).value(u"segs"_s).toList().at(1).toMap().value(u"text"_s).toString(), u"stary"_s);
}

void TestGitDiffRows::ignoreWhitespace()
{
    const QString before = u"if (x) {\n    return 1;\n}\n"_s;
    const QString after = u"if (x)  {\n\treturn 1;\n}\nnowa\n"_s;
    QCOMPARE(kinds(diffRows(before, after, {})).count(u"delete"_s), 2);
    const QVariantList rows = diffRows(before, after, {.ignoreWhitespace = true});
    QCOMPARE(kinds(rows).count(u"delete"_s), 0);
    QCOMPARE(kinds(rows).count(u"insert"_s), 1);
    // The equal lines show the new text.
    QCOMPARE(at(rows, 1).value(u"line"_s).toString(), u"if (x)  {"_s);
    QVERIFY(diffRows(u"a b\n"_s, u"a  b\n"_s, {.ignoreWhitespace = true}).isEmpty());
}

void TestGitDiffRows::wholeFile()
{
    QString changed = numbered(30);
    changed.replace(u"\n15\n"_s, u"\npiętnaście\n"_s);
    const QVariantList rows = diffRows(numbered(30), changed, {.context = -1});
    QVERIFY(!kinds(rows).contains(u"skip"_s));
    QCOMPARE(kinds(rows).count(u"hunk"_s), 1);
    QCOMPARE(rows.size(), 1 + 29 + 2);
    QCOMPARE(at(rows, 0).value(u"line"_s).toString(), u"@@ -1,30 +1,30 @@"_s);
    QVERIFY(diffRows(numbered(5), numbered(5), {.context = -1}).isEmpty());
}

void TestGitDiffRows::identicalAndEmpty()
{
    QVERIFY(diffRows(u"same\n"_s, u"same\n"_s, {}).isEmpty());
    QVERIFY(diffRows(QString(), QString(), {}).isEmpty());
    // A new file: all inserted, the old side starting at 0.
    const QVariantList added = diffRows(QString(), u"a\nb\n"_s, {});
    QCOMPARE(kinds(added), (QStringList{u"hunk"_s, u"insert"_s, u"insert"_s}));
    QCOMPARE(at(added, 0).value(u"line"_s).toString(), u"@@ -0,0 +1,2 @@"_s);
    QCOMPARE(at(added, 0).value(u"newFirst"_s).toInt(), 1);
    QCOMPARE(at(added, 0).value(u"newLast"_s).toInt(), 2);
    const QVariantList deleted = diffRows(u"a\nb\n"_s, QString(), {});
    QCOMPARE(at(deleted, 0).value(u"line"_s).toString(), u"@@ -1,2 +0,0 @@"_s);
    QCOMPARE(at(deleted, 0).value(u"oldLast"_s).toInt(), 2);
    // A last line without a newline is a line.
    QCOMPARE(kinds(diffRows(u"a"_s, u"b"_s, {})).count(u"insert"_s), 1);
}

void TestGitDiffRows::crlf()
{
    const QVariantList rows = diffRows(u"a\r\nb\r\n"_s, u"a\nB\n"_s, {});
    QCOMPARE(kinds(rows), (QStringList{u"hunk"_s, u"equal"_s, u"delete"_s, u"insert"_s}));
    QCOMPARE(at(rows, 2).value(u"line"_s).toString(), u"b"_s);
}

void TestGitDiffRows::find()
{
    const QVariantList rows = diffRows(u"Żółw\nkot\npies\n"_s, u"Żółw\nKOT\npies\nżółw mały\n"_s, {});
    // ("hunk" rows never; case-insensitive, Polish letters too.)
    QCOMPARE(findRows(rows, u"żółw"_s), (QList<int>{1, 5}));
    QCOMPARE(findRows(rows, u"kot"_s), (QList<int>{2, 3}));
    QVERIFY(findRows(rows, QString()).isEmpty());
    QVERIFY(findRows(rows, u"@@"_s).isEmpty());
    QCOMPARE(longestLine(rows), 9);
    QCOMPARE(longestLine(diffRows(QString(), u"\tx\n"_s, {})), 5);
}

void TestGitDiffRows::html()
{
    const QVariantList segs{QVariantMap{{u"text"_s, u"a "_s}, {u"changed"_s, false}},
                            QVariantMap{{u"text"_s, u"<b>"_s}, {u"changed"_s, true}}};
    QCOMPARE(lineHtml(u"a <b>"_s, segs, u"#ff0000"_s, QString(), u"#00ff00"_s, true),
             u"<span style=\"white-space:pre-wrap\">a <span style=\"background-color:#ff0000\">&lt;b&gt;</span></span>"_s);
    QCOMPARE(lineHtml(u"Kot i kot"_s, {}, u"#f00"_s, u"KOT"_s, u"#0f0"_s, false),
             u"<span style=\"white-space:pre\"><span style=\"background-color:#0f0\">Kot</span> i "
             "<span style=\"background-color:#0f0\">kot</span></span>"_s);
    QCOMPARE(lineHtml(QString(), {}, u"#f00"_s, u"x"_s, u"#0f0"_s, true), u"<span style=\"white-space:pre-wrap\"></span>"_s);
}

void TestGitDiffRows::tree()
{
    const QVariantList changes{change(u"src/app/ui/a.cpp"_s), change(u"src/app/ui/b.h"_s), change(u"src/core/c.cpp"_s),
                               change(u"README.md"_s), change(u"docs/żółw.md"_s)};
    const QVariantList rows = treeRows(changes, {});
    QStringList shown;
    for (const QVariant &v : rows) {
        const QVariantMap r = v.toMap();
        shown << QString(r.value(u"depth"_s).toInt() * 2, u' ') + r.value(u"name"_s).toString()
                         + (r.value(u"kind"_s).toString() == u"folder"_s ? u"/"_s : QString());
    }
    QCOMPARE(shown, (QStringList{u"docs/"_s, u"  żółw.md"_s, u"src/"_s, u"  app/ui/"_s, u"    a.cpp"_s, u"    b.h"_s,
                                 u"  core/"_s, u"    c.cpp"_s, u"README.md"_s}));
    QCOMPARE(at(rows, 2).value(u"fileCount"_s).toInt(), 3);
    QCOMPARE(at(rows, 3).value(u"path"_s).toString(), u"src/app/ui"_s);
    // Folded: its files and folders left out.
    const QVariantList folded = treeRows(changes, {u"src"_s});
    QCOMPARE(folded.size(), 4);
    QVERIFY(at(folded, 2).value(u"collapsed"_s).toBool());
    QCOMPARE(at(folded, 3).value(u"name"_s).toString(), u"README.md"_s);
    QVERIFY(treeRows({}, {}).isEmpty());
}

void TestGitDiffRows::comment()
{
    QString changed = numbered(10);
    changed.replace(u"\n5\n"_s, u"\npięć\n"_s);
    const QVariantList rows = diffRows(numbered(10), changed, {});
    // An inserted line alone: the new side's line, in the file's language.
    const int insert = int(kinds(rows).indexOf(u"insert"_s));
    QCOMPARE(reviewComment(u"src/plik.cpp"_s, rows, insert, insert, u"  Dlaczego?  "_s),
             u"`src/plik.cpp` line 5:\n```cpp\npięć\n```\nDlaczego?\n"_s);
    // Across a change: as a diff.
    const QString diff = reviewComment(u"notatka.md"_s, rows, insert - 2, insert + 1, QString());
    QCOMPARE(diff, u"`notatka.md` lines 4–6:\n```diff\n 4\n-5\n+pięć\n 6\n```\n"_s);
    // Only a deleted line: the old line's number.
    const QVariantList removed = diffRows(u"a\nb\n"_s, u"a\n"_s, {});
    QCOMPARE(reviewComment(u"x"_s, removed, 2, 2, u"Po co?"_s), u"`x` old line 2:\n```diff\n-b\n```\nPo co?\n"_s);
    // Backticks in the lines: a longer fence.
    const QVariantList ticks = diffRows(QString(), u"```kod```\n"_s, {});
    QVERIFY(reviewComment(u"a.md"_s, ticks, 1, 1, QString()).contains(u"````markdown\n```kod```\n````"_s));
    // Split rows too.
    const QVariantList split = diffRows(u"a\nb\n"_s, u"a\nB\n"_s, {.split = true});
    QCOMPARE(reviewComment(u"s.py"_s, split, 2, 2, QString()), u"`s.py` line 2:\n```diff\n-b\n+B\n```\n"_s);
}

void TestGitDiffRows::languages_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("language");
    QTest::newRow("cpp") << u"src/a.cpp"_s << u"cpp"_s;
    QTest::newRow("header") << u"a.h"_s << u"cpp"_s;
    QTest::newRow("qml") << u"qml/X.qml"_s << u"qml"_s;
    QTest::newRow("upper") << u"README.MD"_s << u"markdown"_s;
    QTest::newRow("cmake") << u"tests/CMakeLists.txt"_s << u"cmake"_s;
    QTest::newRow("nix") << u"flake.nix"_s << u"nix"_s;
    QTest::newRow("unknown") << u"plik.xyz"_s << QString();
    QTest::newRow("none") << u"LICENSE"_s << QString();
}

void TestGitDiffRows::languages()
{
    QFETCH(QString, path);
    QTEST(languageOf(path), "language");
}

QTEST_GUILESS_MAIN(TestGitDiffRows)
#include "tst_gitdiffrows.moc"
