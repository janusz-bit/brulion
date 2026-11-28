// A project's files (docs/CODE-FILES.md): the fuzzy matcher, the index, the viewer's reading, file
// links of agents' answers, @-mentions and review comments.
#include "codefiles/fileindex.h"
#include "codefiles/fuzzy.h"
#include "codefiles/viewer.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::codefiles;

namespace {

int nameStartOf(const QString &path)
{
    return int(path.lastIndexOf(u'/') + 1);
}

int score(const QString &query, const QString &path)
{
    return scorePath(prepareQuery(query), path, nameStartOf(path));
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

} // namespace

class TestCodeFiles : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- The fuzzy matcher ------------------------------------------------------------------------

    void foldsPolishLetters()
    {
        QCOMPARE(foldChar(u'Ż'), u'z');
        QCOMPARE(foldChar(u'ł'), u'l');
        QCOMPARE(foldChar(u'Ł'), u'l');
        QCOMPARE(foldChar(u'ó'), u'o');
        QCOMPARE(foldChar(u'É'), u'e');
        QCOMPARE(foldChar(u'A'), u'a');
        QCOMPARE(foldChar(u'/'), u'/');
        QCOMPARE(foldChar(u'Ж'), u'ж');
    }

    void matches_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("matches");
        QTest::newRow("subsequence") << u"mncp"_s << u"src/main.cpp"_s << true;
        QTest::newRow("over folders") << u"srcapp"_s << u"src/app/main.cpp"_s << true;
        QTest::newRow("out of order") << u"pcm"_s << u"src/main.cpp"_s << false;
        QTest::newRow("case ignored") << u"MAIN"_s << u"src/main.cpp"_s << true;
        QTest::newRow("Polish folded") << u"zolw"_s << u"notatki/Żółw.md"_s << true;
        QTest::newRow("Polish typed") << u"żółw"_s << u"notatki/Zolw.md"_s << true;
        QTest::newRow("spaces dropped") << u"main cpp"_s << u"src/main.cpp"_s << true;
        QTest::newRow("slash") << u"app/ma"_s << u"src/app/main.cpp"_s << true;
        QTest::newRow("longer than the path") << u"mainmainmain"_s << u"main"_s << false;
        QTest::newRow("empty path") << u"a"_s << QString() << false;
        QTest::newRow("empty query") << QString() << u"a.txt"_s << true;
        QTest::newRow("only spaces") << u"   "_s << u"a.txt"_s << true;
        QTest::newRow("Cyrillic") << u"жук"_s << u"docs/Жук.txt"_s << true;
        QTest::newRow("emoji") << u"🐢"_s << u"a/🐢.md"_s << true;
    }
    void matches()
    {
        QFETCH(QString, query);
        QFETCH(QString, path);
        QFETCH(bool, matches);
        QCOMPARE(score(query, path) > 0, matches);
        const FuzzyMatch m = matchPath(prepareQuery(query), path, nameStartOf(path));
        QCOMPARE(m.score, score(query, path));
        // The positions are the query's characters, in order, in the path itself.
        const FuzzyQuery q = prepareQuery(query);
        if (matches && !q.isEmpty()) {
            QCOMPARE(m.positions.size(), q.folded.size());
            for (int i = 0; i < m.positions.size(); ++i) {
                QCOMPARE(QChar(foldChar(path.at(m.positions.at(i)).unicode())), q.folded.at(i));
                if (i > 0)
                    QVERIFY(m.positions.at(i) > m.positions.at(i - 1));
            }
        } else {
            QVERIFY(m.positions.isEmpty());
        }
    }

    // A path that should rank above another one for a query.
    void ranks_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("better");
        QTest::addColumn<QString>("worse");
        QTest::newRow("base name first") << u"main"_s << u"src/main.cpp"_s << u"main/other.cpp"_s;
        QTest::newRow("exact name") << u"main"_s << u"src/main.cpp"_s << u"src/mainwindow.cpp"_s;
        QTest::newRow("name prefix") << u"chat"_s << u"qml/ChatView.qml"_s << u"qml/BChatterView.qml"_s;
        QTest::newRow("consecutive") << u"view"_s << u"src/view.cpp"_s << u"src/v_i_e_w.cpp"_s;
        QTest::newRow("word starts") << u"cv"_s << u"qml/CodeView.qml"_s << u"qml/actives.qml"_s;
        QTest::newRow("camel humps") << u"gcp"_s << u"qml/GitChangesPanel.qml"_s << u"qml/gochopper.qml"_s;
        QTest::newRow("separator starts") << u"sapm"_s << u"src/app/main.cpp"_s << u"xsxaxpxm/a.cpp"_s;
        QTest::newRow("shorter path") << u"readme"_s << u"README.md"_s << u"docs/old/README.md"_s;
        QTest::newRow("case typed") << u"View"_s << u"a/View.qml"_s << u"a/view.qml"_s;
        QTest::newRow("slash query") << u"app/main"_s << u"src/app/main.cpp"_s << u"src/apple/x/main.cpp"_s;
        QTest::newRow("Polish") << u"zolw"_s << u"Żółw.md"_s << u"z/o/l/w.md"_s;
        QTest::newRow("dash and dot starts") << u"fbt"_s << u"foo-bar.test.ts"_s << u"fabtx.ts"_s;
    }
    void ranks()
    {
        QFETCH(QString, query);
        QFETCH(QString, better);
        QFETCH(QString, worse);
        const int b = score(query, better);
        const int w = score(query, worse);
        QVERIFY2(b > 0 && w > 0, qPrintable(u"%1 %2"_s.arg(b).arg(w)));
        QVERIFY2(b > w, qPrintable(u"%1: %2 (%3) ≤ %4 (%5)"_s.arg(query, better).arg(b).arg(worse).arg(w)));
    }

    void namePositions()
    {
        // A query that fits the base name is marked there, not in the folders.
        const QString path = u"main/src/main.cpp"_s;
        const FuzzyMatch m = matchPath(prepareQuery(u"main"_s), path, nameStartOf(path));
        QVERIFY(m.inName);
        QCOMPARE(m.positions, (QList<int>{9, 10, 11, 12}));
        // Separators and word starts win over the first occurrence.
        const QString p2 = u"aaa/b_ar.txt"_s;
        const FuzzyMatch m2 = matchPath(prepareQuery(u"ar"_s), p2, nameStartOf(p2));
        QCOMPARE(m2.positions, (QList<int>{6, 7}));
    }

    void limits()
    {
        const QString longQuery(MaxQueryLength + 1, u'a');
        QCOMPARE(score(longQuery, QString(200, u'a')), 0);
        QCOMPARE(score(u"a"_s, QString(MaxPathLength + 1, u'a')), 0);
        QVERIFY(score(u"a"_s, QString(MaxPathLength, u'a')) > 0);
        // Long gaps still score above zero.
        QVERIFY(score(u"az"_s, u"a"_s + QString(400, u'x') + u"z"_s) > 0);
    }

    // ---- The index --------------------------------------------------------------------------------

    void cleanPaths_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("clean");
        QTest::newRow("plain") << u"src/a.cpp"_s << u"src/a.cpp"_s;
        QTest::newRow("dot slash") << u"./src/a.cpp"_s << u"src/a.cpp"_s;
        QTest::newRow("trailing slash") << u"src/"_s << u"src"_s;
        QTest::newRow("backslashes") << u"src\\a.cpp"_s << u"src/a.cpp"_s;
        QTest::newRow("absolute") << u"/etc/passwd"_s << QString();
        QTest::newRow("drive") << u"C:/x"_s << QString();
        QTest::newRow("dot dot") << u"src/../../x"_s << QString();
        QTest::newRow("dot part") << u"src/./x"_s << QString();
        QTest::newRow("double slash") << u"src//x"_s << QString();
        QTest::newRow("line break") << u"a\nb"_s << QString();
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("Polish") << u"notatki/żółw ą.md"_s << u"notatki/żółw ą.md"_s;
    }
    void cleanPaths()
    {
        QFETCH(QString, path);
        QFETCH(QString, clean);
        QCOMPARE(cleanRelativePath(path), clean);
    }

    void fromGitOutput()
    {
        const char data[] = "src/app/main.cpp\0src/app/qml/Main.qml\0README.md\0docs/żółw.md\0"
                            "../escape\0/abs\0\0src/app/main.cpp\0last-without-nul";
        const QByteArray out(data, sizeof(data) - 1);
        const FileIndex index = FileIndex::fromNulSeparated(out);
        QCOMPARE(index.fileCount(), 5);
        QCOMPARE(index.folderCount(), 4); // docs, src, src/app, src/app/qml
        QVERIFY(!index.truncated());
        QVERIFY(index.contains(u"docs/żółw.md"));
        QVERIFY(index.contains(u"src/app"));
        QVERIFY(index.isFolder(index.find(u"src/app")));
        QVERIFY(!index.isFolder(index.find(u"README.md")));
        QVERIFY(index.contains(u"last-without-nul"));
        QVERIFY(!index.contains(u"../escape"));
        QVERIFY(!index.contains(u"/abs"));
        QCOMPARE(index.find(u"nope"), -1);
        QCOMPARE(index.name(index.find(u"src/app/qml/Main.qml")).toString(), u"Main.qml"_s);
        // Sorted.
        for (int i = 1; i < index.size(); ++i)
            QVERIFY(index.path(i - 1) < index.path(i));
        QCOMPARE(index.files().size(), 5);
    }

    void search()
    {
        const FileIndex index = FileIndex::fromPaths({u"src/app/main.cpp"_s, u"src/app/qml/Main.qml"_s,
                                                      u"src/main/other.cpp"_s, u"README.md"_s, u"docs/Żółw.md"_s});
        QList<FileIndex::Hit> hits = index.search(u"main", 10, FileIndex::Kinds::Files);
        QVERIFY(hits.size() >= 3);
        QCOMPARE(index.path(hits.at(0).entry).toString(), u"src/app/main.cpp"_s); // (shorter than Main.qml's)
        QCOMPARE(index.path(hits.at(1).entry).toString(), u"src/app/qml/Main.qml"_s);
        QCOMPARE(hits.at(0).positions, (QList<int>{8, 9, 10, 11}));
        // Folders only, all kinds, a limit.
        hits = index.search(u"main", 10, FileIndex::Kinds::Folders);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(index.path(hits.at(0).entry).toString(), u"src/main"_s);
        QCOMPARE(index.search(u"main", 2, FileIndex::Kinds::All).size(), 2);
        QCOMPARE(index.search(u"main", 0).size(), 0);
        QCOMPARE(index.search(u"zolw", 5).size(), 1);
        QCOMPARE(index.search(u"qqqq", 5).size(), 0);
        // An empty query: shallow first.
        hits = index.search(QString(), 3, FileIndex::Kinds::All);
        QCOMPARE(hits.size(), 3);
        QCOMPARE(index.path(hits.at(0).entry).toString(), u"README.md"_s);
        QCOMPARE(index.path(hits.at(1).entry).toString(), u"docs"_s);
        QCOMPARE(index.path(hits.at(2).entry).toString(), u"src"_s);
        hits = index.search(QString(), 1, FileIndex::Kinds::Files);
        QCOMPARE(index.path(hits.at(0).entry).toString(), u"README.md"_s);
    }

    void bounded()
    {
        QStringList files;
        for (int i = 0; i < 100; ++i)
            files << u"d%1/f.txt"_s.arg(i, 3, 10, QLatin1Char('0'));
        const FileIndex index = FileIndex::fromPaths(files, 50);
        QVERIFY(index.truncated());
        QVERIFY(index.size() <= 50);
        QCOMPARE(index.fileCount(), index.folderCount()); // (each file with its folder)
        QVERIFY(index.contains(u"d000/f.txt"));
        QVERIFY(!index.contains(u"d099/f.txt"));
        QCOMPARE(FileIndex::fromPaths(files, 0).size(), 0);
        // A path that is a file and a folder: a folder (a submodule beside its files).
        const FileIndex both = FileIndex::fromPaths({u"lib"_s, u"lib/a.c"_s});
        QCOMPARE(both.size(), 2);
        QVERIFY(both.isFolder(both.find(u"lib")));
        QCOMPARE(FileIndex().search(u"a", 5).size(), 0);
    }

    void walk()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        for (const char *f : {"README.md", ".env", "src/main.cpp", "src/żółw/a.txt", ".git/config", "node_modules/x/i.js",
                              "build/a.o", "build-clang/b.o", "cmake-build-debug/c.o", ".cache/d", "dist/e.js",
                              "srcbuild/kept.txt", "a/b/c/d/e.txt"})
            QVERIFY(writeFile(root + u'/' + QString::fromUtf8(f), "x\n"));
        QVERIFY(QDir(root).mkpath(u"empty"_s));
#ifndef Q_OS_WIN
        QVERIFY(QFile::link(root + u"/src"_s, root + u"/link"_s));
#endif
        const FileIndex index = FileIndex::walk(root);
        QVERIFY(index.contains(u"README.md"));
        QVERIFY(index.contains(u".env")); // (hidden files are listed)
        QVERIFY(index.contains(u"src/main.cpp"));
        QVERIFY(index.contains(u"src/żółw/a.txt"));
        QVERIFY(index.contains(u"srcbuild/kept.txt"));
        QVERIFY(index.contains(u"a/b/c/d/e.txt"));
        QVERIFY(!index.contains(u".git/config"));
        QVERIFY(!index.contains(u"node_modules/x/i.js"));
        QVERIFY(!index.contains(u"build/a.o"));
        QVERIFY(!index.contains(u"build-clang/b.o"));
        QVERIFY(!index.contains(u"cmake-build-debug/c.o"));
        QVERIFY(!index.contains(u".cache/d"));
        QVERIFY(!index.contains(u"dist/e.js"));
        QVERIFY(!index.contains(u"empty")); // (a folder holds a file)
        QVERIFY(!index.contains(u"link/main.cpp")); // (links to folders not followed)
        // Bounded; cancelled.
        const FileIndex small = FileIndex::walk(root, 3);
        QVERIFY(small.truncated());
        QVERIFY(small.size() <= 3);
        std::atomic<bool> cancel{true};
        QCOMPARE(FileIndex::walk(root, FileIndex::DefaultMaxEntries, &cancel).size(), 0);
        QCOMPARE(FileIndex::walk(root + u"/nope"_s).size(), 0);
        QVERIFY(FileIndex::skippedFolder(u".github"));
        QVERIFY(!FileIndex::skippedFolder(u"src"));
    }

    // A generated tree of 50 000 files: built and queried fast enough for typing (the numbers in
    // docs/CODE-FILES.md come from this test's output in a release build).
    void bigTree()
    {
        QByteArray out;
        const QStringList words{u"core"_s, u"app"_s, u"qml"_s, u"shell"_s, u"editor"_s, u"sync"_s, u"żółw"_s, u"util"_s,
                                u"views"_s, u"tests"_s};
        int n = 0;
        for (int a = 0; a < 10 && n < 50000; ++a)
            for (int b = 0; b < 10 && n < 50000; ++b)
                for (int c = 0; c < 10 && n < 50000; ++c)
                    for (int f = 0; f < 50 && n < 50000; ++f, ++n)
                        out += (u"src/%1/%2%3/%4_%5/file%6Name.cpp"_s.arg(words[a], words[b]).arg(b).arg(words[c]).arg(c).arg(f))
                                       .toUtf8()
                               + '\0';
        QElapsedTimer t;
        t.start();
        const FileIndex index = FileIndex::fromNulSeparated(out);
        const qint64 built = t.elapsed();
        QCOMPARE(index.fileCount(), 50000);
        t.restart();
        const QList<FileIndex::Hit> hits = index.search(u"zolwfile42", 50, FileIndex::Kinds::Files);
        const qint64 queried = t.elapsed();
        QVERIFY(!hits.isEmpty());
        QVERIFY(index.name(hits.first().entry).startsWith(u"file42"));
        t.restart();
        const QList<FileIndex::Hit> one = index.search(u"f", 50, FileIndex::Kinds::Files);
        const qint64 oneLetter = t.elapsed();
        QCOMPARE(one.size(), 50);
        qInfo("50 000 files: index built in %lld ms, \"zolwfile42\" in %lld ms, \"f\" in %lld ms", built, queried,
              oneLetter);
        // (Generous: a Debug build on a loaded machine, sanitizers.)
        QVERIFY2(built < 20000 && queried < 20000, qPrintable(u"%1 %2"_s.arg(built).arg(queried)));
    }

    // ---- The viewer -------------------------------------------------------------------------------

    void readsForViewer()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        QVERIFY(writeFile(root + u"/a.cpp"_s, "\xEF\xBB\xBFint main() {}\n// żółw\n"));
        QVERIFY(writeFile(root + u"/b.bin"_s, QByteArray("ELF\0\1\2", 6)));
        QVERIFY(writeFile(root + u"/c.png"_s, "not really"));
        QVERIFY(writeFile(root + u"/big.txt"_s, QByteArray(100, 'a') + "\n" + QByteArray(100, 'b') + "\n"));
        QVERIFY(writeFile(root + u"/control.txt"_s, QByteArray(50, '\x01') + QByteArray(50, 'a')));
        QVERIFY(writeFile(root + u"/esc.txt"_s, "\x1b[31mred\x1b[0m\n"));
        QVERIFY(writeFile(root + u"/bad.txt"_s, "a\xff\xfe b\n"));
        ViewerFile v = readForViewer(root + u"/a.cpp"_s);
        QCOMPARE(v.kind, FileKind::Text);
        QCOMPARE(v.text, u"int main() {}\n// żółw\n"_s);
        QVERIFY(!v.truncated);
        QCOMPARE(readForViewer(root + u"/b.bin"_s).kind, FileKind::Binary);
        QCOMPARE(readForViewer(root + u"/c.png"_s).kind, FileKind::Picture);
        QCOMPARE(readForViewer(root + u"/nope"_s).kind, FileKind::Missing);
        QCOMPARE(readForViewer(root).kind, FileKind::Folder);
        QCOMPARE(readForViewer(root + u"/control.txt"_s).kind, FileKind::Binary);
        QCOMPARE(readForViewer(root + u"/esc.txt"_s).kind, FileKind::Text);
        v = readForViewer(root + u"/bad.txt"_s);
        QCOMPARE(v.kind, FileKind::Text);
        QVERIFY(v.text.contains(QChar(0xfffd)));
        // Bounded: cut after the last whole line.
        v = readForViewer(root + u"/big.txt"_s, 150);
        QVERIFY(v.truncated);
        QCOMPARE(v.text, QString(100, u'a') + u'\n');
        QCOMPARE(v.size, 202);
        QVERIFY(isPictureName(u"x/Y.JPEG"));
        QVERIFY(!isPictureName(u"x/png"));
        QVERIFY(looksBinary(QByteArray("a\0b", 3)));
        QVERIFY(!looksBinary("zażółć gęślą jaźń\n\t"));
        QVERIFY(!looksBinary(QByteArray()));
    }

    void languages_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("language");
        QTest::newRow("cpp") << u"src/a.cpp"_s << u"cpp"_s;
        QTest::newRow("header") << u"src/a.h"_s << u"cpp"_s;
        QTest::newRow("qml") << u"Main.qml"_s << u"qml"_s;
        QTest::newRow("upper case") << u"A.PY"_s << u"python"_s;
        QTest::newRow("cmake") << u"src/CMakeLists.txt"_s << u"cmake"_s;
        QTest::newRow("dockerfile") << u"Dockerfile"_s << u"dockerfile"_s;
        QTest::newRow("dockerfile.dev") << u"Dockerfile.dev"_s << u"dockerfile"_s;
        QTest::newRow("makefile") << u"Makefile"_s << u"makefile"_s;
        QTest::newRow("gitignore") << u".gitignore"_s << u"gitignore"_s;
        QTest::newRow("nix") << u"flake.nix"_s << u"nix"_s;
        QTest::newRow("markdown") << u"docs/Żółw.md"_s << u"markdown"_s;
        QTest::newRow("unknown") << u"a.xyz"_s << QString();
        QTest::newRow("none") << u"LICENSE"_s << QString();
    }
    void languages()
    {
        QFETCH(QString, path);
        QFETCH(QString, language);
        QCOMPARE(languageForPath(path), language);
    }

    void fileLinks_data()
    {
        QTest::addColumn<QString>("link");
        QTest::addColumn<QString>("path");
        QTest::addColumn<int>("line");
        QTest::addColumn<int>("column");
        QTest::addColumn<int>("endLine");
        QTest::newRow("plain") << u"src/a.cpp"_s << u"src/a.cpp"_s << 0 << 0 << 0;
        QTest::newRow("line") << u"src/a.cpp:12"_s << u"src/a.cpp"_s << 12 << 0 << 0;
        QTest::newRow("column") << u"src/a.cpp:12:5"_s << u"src/a.cpp"_s << 12 << 5 << 0;
        QTest::newRow("range") << u"src/a.cpp:12-20"_s << u"src/a.cpp"_s << 12 << 0 << 20;
        QTest::newRow("anchor") << u"src/a.cpp#L7"_s << u"src/a.cpp"_s << 7 << 0 << 0;
        QTest::newRow("anchor range") << u"src/a.cpp#L7-L9"_s << u"src/a.cpp"_s << 7 << 0 << 9;
        QTest::newRow("anchor column") << u"src/a.cpp#L7C3"_s << u"src/a.cpp"_s << 7 << 3 << 0;
        QTest::newRow("absolute") << u"/home/ala/p/a.cpp:3"_s << u"/home/ala/p/a.cpp"_s << 3 << 0 << 0;
        QTest::newRow("file url") << u"file:///home/ala/p/%C5%BC%C3%B3%C5%82w.md#L2"_s << u"/home/ala/p/żółw.md"_s << 2
                                  << 0 << 0;
        QTest::newRow("percent") << u"docs/%C5%BC%C3%B3%C5%82w%20a.md"_s << u"docs/żółw a.md"_s << 0 << 0 << 0;
        QTest::newRow("dot slash") << u"./a.cpp:1"_s << u"./a.cpp"_s << 1 << 0 << 0;
        QTest::newRow("no number") << u"a.cpp:x"_s << u"a.cpp:x"_s << 0 << 0 << 0;
        QTest::newRow("zero line") << u"a.cpp:0"_s << u"a.cpp:0"_s << 0 << 0 << 0;
        QTest::newRow("windows") << u"C:/p/a.cpp:4"_s << u"C:/p/a.cpp"_s << 4 << 0 << 0;
        QTest::newRow("name only with line") << u"README:12"_s << u"README"_s << 12 << 0 << 0;
    }
    void fileLinks()
    {
        QFETCH(QString, link);
        QFETCH(QString, path);
        QFETCH(int, line);
        QFETCH(int, column);
        QFETCH(int, endLine);
        const std::optional<FileLink> l = parseFileLink(link);
        QVERIFY(l);
        QCOMPARE(l->path, path);
        QCOMPARE(l->line, line);
        QCOMPARE(l->column, column);
        QCOMPARE(l->endLine, endLine);
    }

    void notFileLinks_data()
    {
        QTest::addColumn<QString>("link");
        QTest::newRow("http") << u"https://example.com/a.cpp"_s;
        QTest::newRow("mailto") << u"mailto:ala@example.com"_s;
        QTest::newRow("brulion") << u"brulion://open?page=x"_s;
        QTest::newRow("remote file") << u"file://server/share/a.txt"_s;
        QTest::newRow("empty") << QString();
        QTest::newRow("spaces") << u"   "_s;
        QTest::newRow("line break") << u"a\nb"_s;
        QTest::newRow("only a line") << u"#L3"_s;
    }
    void notFileLinks()
    {
        QFETCH(QString, link);
        QVERIFY(!parseFileLink(link));
    }

    void insideFolder_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("relative");
        QTest::newRow("relative") << u"src/a.cpp"_s << u"src/a.cpp"_s;
        QTest::newRow("absolute") << u"/p/projekt/src/a.cpp"_s << u"src/a.cpp"_s;
        QTest::newRow("dot parts") << u"./src/../src/a.cpp"_s << u"src/a.cpp"_s;
        QTest::newRow("escape") << u"../other/a.cpp"_s << QString();
        QTest::newRow("absolute outside") << u"/etc/passwd"_s << QString();
        QTest::newRow("sibling prefix") << u"/p/projekt2/a.cpp"_s << QString();
        QTest::newRow("the folder") << u"/p/projekt"_s << QString();
        QTest::newRow("Polish") << u"notatki/żółw.md"_s << u"notatki/żółw.md"_s;
    }
    void insideFolder()
    {
        QFETCH(QString, path);
        QFETCH(QString, relative);
        QCOMPARE(relativeInside(u"/p/projekt"_s, path), relative);
        QCOMPARE(relativeInside(QString(), path), QString());
    }

    // ---- @-mentions -------------------------------------------------------------------------------

    void mentionsAt_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("caret");
        QTest::addColumn<int>("start");
        QTest::addColumn<QString>("query");
        QTest::newRow("start") << u"@src/ap"_s << 7 << 0 << u"src/ap"_s;
        QTest::newRow("after a space") << u"Popraw @mai"_s << 11 << 7 << u"mai"_s;
        QTest::newRow("just @") << u"Zobacz @"_s << 8 << 7 << QString();
        QTest::newRow("in parentheses") << u"(@a.cpp"_s << 7 << 1 << u"a.cpp"_s;
        QTest::newRow("Polish") << u"@notatki/żó"_s << 11 << 0 << u"notatki/żó"_s;
        QTest::newRow("email") << u"ala@example.com"_s << 15 << -1 << QString();
        QTest::newRow("after a space ended") << u"@a.cpp i"_s << 8 << -1 << QString();
        QTest::newRow("caret in the middle") << u"@src/main.cpp"_s << 4 << 0 << u"src"_s;
        QTest::newRow("out of range") << u"@a"_s << 5 << -1 << QString();
        QTest::newRow("negative") << u"@a"_s << -1 << -1 << QString();
    }
    void mentionsAt()
    {
        QFETCH(QString, text);
        QFETCH(int, caret);
        QFETCH(int, start);
        QFETCH(QString, query);
        const MentionAt m = mentionAt(text, caret);
        QCOMPARE(m.start, start);
        QCOMPARE(m.query, query);
    }

    void insertsMentions()
    {
        QCOMPARE(withMention(u"Popraw @mai"_s, 11, u"src/main.cpp"), u"Popraw @src/main.cpp "_s);
        QCOMPARE(mentionEnd(u"Popraw @mai"_s, 11, u"src/main.cpp"), 21);
        // The rest of the word under the caret goes too; a following space is kept, not doubled.
        QCOMPARE(withMention(u"@src/ma i dalej"_s, 4, u"src/main.cpp"), u"@src/main.cpp i dalej"_s);
        QCOMPARE(mentionEnd(u"@src/ma i dalej"_s, 4, u"src/main.cpp"), 14);
        // A folder; a path with a space.
        QCOMPARE(withMention(u"@sr"_s, 3, u"src/"), u"@src/ "_s);
        QCOMPARE(withMention(u"@no"_s, 3, u"notatki/żółw ą.md"), u"@\"notatki/żółw ą.md\" "_s);
        QCOMPARE(mentionEnd(u"@no"_s, 3, u"notatki/żółw ą.md"), 21);
        // None being written: nothing.
        QCOMPARE(withMention(u"abc"_s, 3, u"a"), QString());
        QCOMPARE(withMention(u"@a"_s, 2, u""), QString());
        QCOMPARE(mentionEnd(u"abc"_s, 2, u"x"), 2);
    }

    void mentionedPathsInMessage()
    {
        QCOMPARE(mentionedPaths(u"Popraw @src/main.cpp i @docs/, potem @\"a b/ż.md\". (@x.h) @src/main.cpp"),
                 (QStringList{u"src/main.cpp"_s, u"docs/"_s, u"a b/ż.md"_s, u"x.h"_s}));
        QCOMPARE(mentionedPaths(u"ala@example.com @ @\"niedomknięte"), QStringList());
        QCOMPARE(mentionedPaths(u"Czy @README.md?"), QStringList{u"README.md"_s});
        QCOMPARE(mentionedPaths(QString()), QStringList());
    }

    // Files dropped on a thread's row (T3 Code's "Drop files onto a thread"): pictures go with the
    // message, the others are named — @-mentions inside the thread's folder, quoted paths outside.
    void droppedOnAThread()
    {
        const QString root = QDir::tempPath() + u"/sklep"_s, other = QDir::tempPath() + u"/zdjęcia"_s;
        const QString outside = QDir::tempPath() + u"/hosts"_s;
        const Dropped d = droppedFiles(root, {root + u"/src/main.cpp"_s, other + u"/Żółw.PNG"_s, root + u"/docs/plan rabatu.md"_s,
                                              outside, root + u"/src/main.cpp"_s, root + u"/logo.svg"_s, root, QString()});
        QCOMPARE(d.pictures, (QStringList{other + u"/Żółw.PNG"_s, root + u"/logo.svg"_s}));
        QCOMPARE(d.text, u"@src/main.cpp @\"docs/plan rabatu.md\" `%1` @./"_s.arg(outside));
        QCOMPARE(mentionedPaths(d.text), (QStringList{u"src/main.cpp"_s, u"docs/plan rabatu.md"_s, u"./"_s}));
        // No folder (a thread of nothing): every path as it is.
        QCOMPARE(droppedFiles(QString(), {outside}).text, u"`%1`"_s.arg(outside));
        QVERIFY(droppedFiles(root, {}).text.isEmpty());
        // A backtick in a path: a longer fence around it.
        QCOMPARE(droppedFiles(root, {outside + u"a`b"_s}).text, u"``%1a`b``"_s.arg(outside));
    }

    // ---- Review comments --------------------------------------------------------------------------

    void reviewComments()
    {
        QCOMPARE(reviewComment(u"src/a.cpp", 12, 14, u"int a;\nint b;\nint c;\n", u"  Za dużo zmiennych.  "),
                 u"src/a.cpp:12-14\n```cpp\nint a;\nint b;\nint c;\n```\nZa dużo zmiennych."_s);
        QCOMPARE(reviewComment(u"README", 3, 3, u"x", u""), u"README:3\n```\nx\n```"_s);
        // Lines given the other way round; backticks inside make a longer fence.
        QCOMPARE(reviewComment(u"a.md", 5, 2, u"```js\nx\n```", u"ok"), u"a.md:2-5\n````markdown\n```js\nx\n```\n````\nok"_s);
    }
};

QTEST_GUILESS_MAIN(TestCodeFiles)
#include "tst_codefiles.moc"
