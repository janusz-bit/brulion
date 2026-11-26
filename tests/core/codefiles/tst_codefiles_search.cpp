// The project search (docs/CODE-FILES.md "Project search"): ripgrep's and git grep's arguments and
// output, the matched ranges, Brulion's own search — and the real programs where they are on PATH.
#include "codefiles/search.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::codefiles;
using Ranges = QList<std::pair<int, int>>;

namespace {

SearchOptions options(const QString &pattern, bool caseSensitive = false, bool wholeWord = false, bool regex = false)
{
    SearchOptions o;
    o.pattern = pattern;
    o.caseSensitive = caseSensitive;
    o.wholeWord = wholeWord;
    o.regex = regex;
    return o;
}

/// A literal with its NULs.
template<qsizetype N>
QByteArray raw(const char (&s)[N])
{
    return QByteArray(s, N - 1);
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

/// A project with Polish text, a binary file, an ignored file and a .git folder.
void makeProject(const QString &root)
{
    QVERIFY(writeFile(root + u"/src/main.cpp"_s, "int main() {\n    return zolw(); // Żółw\n}\n"));
    QVERIFY(writeFile(root + u"/docs/żółw.md"_s, "# Żółw\n\nżółw idzie wolno, ŻÓŁW stoi.\nżółwik\n"));
    QVERIFY(writeFile(root + u"/bin.dat"_s, QByteArray("żółw\0\1\2", 10)));
    QVERIFY(writeFile(root + u"/ignored.log"_s, "żółw w logu\n"));
    QVERIFY(writeFile(root + u"/.gitignore"_s, "*.log\n"));
}

QString run(const QString &program, const QStringList &args, const QString &folder, QByteArray *out)
{
    QProcess p;
    p.setWorkingDirectory(folder);
    p.setStandardInputFile(QProcess::nullDevice());
    p.start(program, args);
    if (!p.waitForFinished(30000))
        return u"timeout"_s;
    *out = p.readAllStandardOutput();
    return p.exitCode() <= 1 ? QString() : QString::fromUtf8(p.readAllStandardError());
}

} // namespace

class TestCodeFilesSearch : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void ranges_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("pattern");
        QTest::addColumn<bool>("caseSensitive");
        QTest::addColumn<bool>("wholeWord");
        QTest::addColumn<bool>("regex");
        QTest::addColumn<Ranges>("ranges");
        QTest::newRow("literal") << u"a.b a.b"_s << u"a.b"_s << false << false << false << Ranges{{0, 3}, {4, 7}};
        QTest::newRow("literal not a pattern") << u"axb"_s << u"a.b"_s << false << false << false << Ranges{};
        QTest::newRow("case ignored") << u"Żółw żółw"_s << u"ŻÓŁW"_s << false << false << false << Ranges{{0, 4}, {5, 9}};
        QTest::newRow("case kept") << u"Żółw żółw"_s << u"żółw"_s << true << false << false << Ranges{{5, 9}};
        QTest::newRow("whole word") << u"żółw żółwik"_s << u"żółw"_s << false << true << false << Ranges{{0, 4}};
        QTest::newRow("whole word Polish edge") << u"ażółw żółw"_s << u"żółw"_s << false << true << false
                                                << Ranges{{6, 10}};
        QTest::newRow("regex") << u"foo1 foo22"_s << u"foo\\d+"_s << false << false << true << Ranges{{0, 4}, {5, 10}};
        QTest::newRow("regex whole word") << u"foo1 xfoo2"_s << u"foo\\d"_s << false << true << true << Ranges{{0, 4}};
        QTest::newRow("empty matches skipped") << u"abc"_s << u"x*"_s << false << false << true << Ranges{};
        QTest::newRow("invalid regex") << u"a(b"_s << u"a("_s << false << false << true << Ranges{};
        QTest::newRow("empty pattern") << u"abc"_s << QString() << false << false << false << Ranges{};
    }
    void ranges()
    {
        QFETCH(QString, line);
        QFETCH(QString, pattern);
        QFETCH(bool, caseSensitive);
        QFETCH(bool, wholeWord);
        QFETCH(bool, regex);
        QFETCH(Ranges, ranges);
        QCOMPARE(matchRanges(line, options(pattern, caseSensitive, wholeWord, regex)), ranges);
    }

    void patternErrors()
    {
        QVERIFY(!patternError(options(QString())).isEmpty());
        QVERIFY(patternError(options(u"a("_s)).isEmpty()); // (a literal)
        QVERIFY(!patternError(options(u"a("_s, false, false, true)).isEmpty());
        QVERIFY(patternError(options(u"a+"_s, false, false, true)).isEmpty());
    }

    void arguments()
    {
        SearchOptions o = options(u"-rf $(rm) `x`"_s, true, true, false);
        o.maxPerFile = 7;
        const QStringList rg = ripgrepArguments(o);
        QVERIFY(rg.contains(u"--json"_s));
        QVERIFY(rg.contains(u"--no-config"_s));
        QVERIFY(rg.contains(u"--fixed-strings"_s));
        QVERIFY(rg.contains(u"--word-regexp"_s));
        QVERIFY(rg.contains(u"--case-sensitive"_s));
        QVERIFY(rg.contains(u"--max-count=7"_s));
        // The pattern is one argument, never an option; a path is always given.
        QVERIFY(rg.contains(u"--regexp=-rf $(rm) `x`"_s));
        QCOMPARE(rg.last(), u"."_s);
        QCOMPARE(rg.at(rg.size() - 2), u"--"_s);
        const QStringList regex = ripgrepArguments(options(u"a+"_s, false, false, true));
        QVERIFY(!regex.contains(u"--fixed-strings"_s));
        QVERIFY(regex.contains(u"--ignore-case"_s));

        const QStringList git = gitGrepArguments(o);
        QCOMPARE(git.first(), u"grep"_s);
        QVERIFY(git.contains(u"-F"_s) && git.contains(u"-w"_s) && !git.contains(u"-i"_s));
        QVERIFY(git.contains(u"--untracked"_s) && git.contains(u"-I"_s) && git.contains(u"-z"_s));
        QCOMPARE(git.at(git.indexOf(u"-e"_s) + 1), o.pattern);
        const QStringList gitRegex = gitGrepArguments(options(u"a+"_s, false, false, true));
        QVERIFY(gitRegex.contains(u"-E"_s) && gitRegex.contains(u"-i"_s));
    }

    void ripgrepOutput()
    {
        // Byte offsets of UTF-8: "żółw" is 7 bytes, 4 characters.
        const QByteArray line = "{\"type\":\"match\",\"data\":{\"path\":{\"text\":\"./docs/żółw.md\"},\"lines\":{\"text\":\"żółw idzie, ŻÓŁW stoi\\n\"},\"line_number\":3,\"absolute_offset\":10,\"submatches\":[{\"match\":{\"text\":\"żółw\"},\"start\":0,\"end\":7},{\"match\":{\"text\":\"ŻÓŁW\"},\"start\":15,\"end\":22}]}}";
        const std::optional<SearchMatch> m = parseRipgrepLine(line);
        QVERIFY(m);
        QCOMPARE(m->path, u"docs/żółw.md"_s);
        QCOMPARE(m->line, 3);
        QCOMPARE(m->text, u"żółw idzie, ŻÓŁW stoi"_s);
        QCOMPARE(m->ranges, (Ranges{{0, 4}, {12, 16}}));
        // Paths and lines that are no UTF-8: base64 bytes.
        const QByteArray bytes = QByteArray("{\"type\":\"match\",\"data\":{\"path\":{\"bytes\":\"") + QByteArray("a\xff.txt").toBase64()
                                 + "\"},\"lines\":{\"bytes\":\"" + QByteArray("x\xffy\r\n").toBase64()
                                 + "\"},\"line_number\":1,\"submatches\":[{\"match\":{\"text\":\"y\"},\"start\":2,\"end\":3}]}}";
        const std::optional<SearchMatch> b = parseRipgrepLine(bytes);
        QVERIFY(b);
        QCOMPARE(b->line, 1);
        QCOMPARE(b->text, u"x�y"_s);
        QCOMPARE(b->ranges, (Ranges{{2, 3}}));
        // Other messages and garbage: nothing.
        QVERIFY(!parseRipgrepLine("{\"type\":\"begin\",\"data\":{\"path\":{\"text\":\"a\"}}}"));
        QVERIFY(!parseRipgrepLine("{\"type\":\"summary\",\"data\":{\"elapsed_total\":{\"secs\":0}}}"));
        QVERIFY(!parseRipgrepLine("not json \"match\""));
        QVERIFY(!parseRipgrepLine(""));
        QVERIFY(!parseRipgrepLine("{\"type\":\"match\",\"data\":{\"path\":{\"text\":\"../../etc/passwd\"},\"lines\":{\"text\":\"x\"},\"line_number\":1}}"));
        QVERIFY(!parseRipgrepLine("{\"type\":\"match\",\"data\":{\"path\":{\"text\":\"a\"},\"lines\":{\"text\":\"x\"},\"line_number\":0}}"));
        // Offsets out of range are clamped.
        const std::optional<SearchMatch> c = parseRipgrepLine(
                "{\"type\":\"match\",\"data\":{\"path\":{\"text\":\"a\"},\"lines\":{\"text\":\"abc\"},\"line_number\":2,\"submatches\":[{\"start\":1,\"end\":99},{\"start\":50,\"end\":60}]}}");
        QVERIFY(c);
        QCOMPARE(c->ranges, (Ranges{{1, 3}}));
    }

    void gitGrepOutput()
    {
        const QByteArray out = raw("docs/\xC5\xBC\xC3\xB3\xC5\x82w.md\0" "3\0" "1\0" "żółw idzie\n"
                                   "src/main.cpp\0" "2\0" "24\0" "    return zolw(); // Żółw\n"
                                   "bad\0" "x\0" "1\0" "żółw\n"
                                   "last\0" "9\0" "żółw");
        const QList<SearchMatch> matches = parseGitGrep(out, options(u"żółw"_s), 100);
        QCOMPARE(matches.size(), 3);
        QCOMPARE(matches.at(0).path, u"docs/żółw.md"_s);
        QCOMPARE(matches.at(0).line, 3);
        QCOMPARE(matches.at(0).ranges, (Ranges{{0, 4}}));
        QCOMPARE(matches.at(1).ranges, (Ranges{{22, 26}}));
        QCOMPARE(matches.at(2).path, u"last"_s);
        QCOMPARE(matches.at(2).line, 9);
        QCOMPARE(parseGitGrep(out, options(u"żółw"_s), 1).size(), 1);
        QCOMPARE(parseGitGrep("garbage without nul", options(u"x"_s), 10).size(), 0);
        QCOMPARE(parseGitGrep(QByteArray(), options(u"x"_s), 10).size(), 0);
    }

    void ownSearch()
    {
        const QString text = u"pierwsza\r\nżółw raz\rtrzecia\nŻÓŁW i żółw\n\n"_s;
        const QList<SearchMatch> m = searchText(u"a.txt"_s, text, searchExpression(options(u"żółw"_s)), 10);
        QCOMPARE(m.size(), 2);
        QCOMPARE(m.at(0).line, 2);
        QCOMPARE(m.at(0).text, u"żółw raz"_s);
        QCOMPARE(m.at(1).line, 4);
        QCOMPARE(m.at(1).ranges, (Ranges{{0, 4}, {7, 11}}));
        QCOMPARE(searchText(u"a.txt"_s, text, searchExpression(options(u"żółw"_s)), 1).size(), 1);
        QCOMPARE(searchText(u"a.txt"_s, text, searchExpression(options(QString())), 10).size(), 0);
        QCOMPARE(searchText(u"a.txt"_s, QString(), searchExpression(options(u"x"_s)), 10).size(), 0);
    }

    void longLines()
    {
        SearchMatch m{u"a"_s, 1, QString(1000, u'x') + u"żółw"_s + QString(1000, u'y'), {{1000, 1004}}};
        const SearchMatch c = clipped(m);
        QVERIFY(c.text.size() <= MaxLineLength + 2);
        QVERIFY(c.text.startsWith(u'…') && c.text.endsWith(u'…'));
        QCOMPARE(c.ranges.size(), 1);
        QCOMPARE(c.text.mid(c.ranges.first().first, 4), u"żółw"_s);
        // Short lines stay; a match at the start keeps the start.
        const SearchMatch s{u"a"_s, 1, u"krótka"_s, {{0, 2}}};
        QCOMPARE(clipped(s), s);
        SearchMatch start{u"a"_s, 1, u"żółw"_s + QString(1000, u'y'), {{0, 4}}};
        const SearchMatch cs = clipped(start);
        QVERIFY(cs.text.startsWith(u"żółw"_s));
        QCOMPARE(cs.ranges.first(), (std::pair<int, int>{0, 4}));
    }

    // The real programs, where they are: their output read by the parsers.
    void realRipgrep()
    {
        const QString rg = QStandardPaths::findExecutable(u"rg"_s);
        if (rg.isEmpty())
            QSKIP("no rg on PATH");
        QTemporaryDir dir;
        makeProject(dir.path());
        QProcess::execute(u"git"_s, {u"-C"_s, dir.path(), u"init"_s, u"-q"_s});
        QByteArray out;
        QCOMPARE(run(rg, ripgrepArguments(options(u"żółw"_s)), dir.path(), &out), QString());
        QList<SearchMatch> matches;
        for (const QByteArray &line : out.split('\n')) {
            if (const std::optional<SearchMatch> m = parseRipgrepLine(line))
                matches.append(*m);
        }
        QStringList paths;
        for (const SearchMatch &m : std::as_const(matches))
            paths.append(m.path + u':' + QString::number(m.line));
        // Case ignored, sorted by path; the binary file, the ignored one and .git not searched.
        QCOMPARE(paths, (QStringList{u"docs/żółw.md:1"_s, u"docs/żółw.md:3"_s, u"docs/żółw.md:4"_s,
                                     u"src/main.cpp:2"_s}));
        QCOMPARE(matches.at(1).ranges, (Ranges{{0, 4}, {18, 22}}));
        // Whole words, case kept.
        out.clear();
        QCOMPARE(run(rg, ripgrepArguments(options(u"żółw"_s, true, true)), dir.path(), &out), QString());
        int count = 0;
        for (const QByteArray &line : out.split('\n'))
            count += parseRipgrepLine(line) ? 1 : 0;
        QCOMPARE(count, 1);
    }

    void realGitGrep()
    {
        const QString git = QStandardPaths::findExecutable(u"git"_s);
        if (git.isEmpty())
            QSKIP("no git on PATH");
        QTemporaryDir dir;
        makeProject(dir.path());
        QCOMPARE(QProcess::execute(git, {u"-C"_s, dir.path(), u"init"_s, u"-q"_s}), 0);
        QByteArray out;
        const SearchOptions o = options(u"żółw"_s);
        QCOMPARE(run(git, QStringList{u"-C"_s, dir.path()} + gitGrepArguments(o), dir.path(), &out), QString());
        QStringList paths;
        for (const SearchMatch &m : parseGitGrep(out, o, 100))
            paths.append(m.path + u':' + QString::number(m.line));
        // (git grep's -i folds ASCII only in C locales: "Żółw" may or may not match "żółw".)
        QVERIFY2(paths.contains(u"docs/żółw.md:3"_s) && paths.contains(u"docs/żółw.md:4"_s)
                         && !paths.contains(u"ignored.log:1"_s)
                         && !paths.contains(u"bin.dat:1"_s),
                 qPrintable(paths.join(u", "_s)));
        // A regular expression.
        out.clear();
        const SearchOptions re = options(u"ret[a-z]+n"_s, false, false, true);
        QCOMPARE(run(git, QStringList{u"-C"_s, dir.path()} + gitGrepArguments(re), dir.path(), &out), QString());
        const QList<SearchMatch> m = parseGitGrep(out, re, 100);
        QCOMPARE(m.size(), 1);
        QCOMPARE(m.first().ranges, (Ranges{{4, 10}}));
    }
};

QTEST_GUILESS_MAIN(TestCodeFilesSearch)
#include "tst_codefiles_search.moc"
