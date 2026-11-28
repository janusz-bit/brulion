// The "Code" mode's project files in the app (docs/CODE-FILES.md): the shared index and its QML view,
// the file viewer's model, the project search, the QML helpers.
#include "codefiledocument.h"
#include "codefileindex.h"
#include "codefiles.h"
#include "codesearch.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::codefiles;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

bool hasGit()
{
    return !QStandardPaths::findExecutable(u"git"_s).isEmpty();
}

bool git(const QString &folder, const QStringList &args)
{
    return QProcess::execute(u"git"_s, QStringList{u"-C"_s, folder} + args) == 0;
}

/// A project: Polish names, a nested folder, an ignored file, a binary one, a picture.
void makeProject(const QString &root)
{
    QVERIFY(writeFile(root + u"/README.md"_s, "# Projekt żółwia\n"));
    QVERIFY(writeFile(root + u"/src/main.cpp"_s, "int main()\n{\n\treturn zolw(); // Żółw <b>&\n}\n"));
    QVERIFY(writeFile(root + u"/src/app/qml/Main.qml"_s, "import QtQuick\nItem {}\n"));
    QVERIFY(writeFile(root + u"/docs/żółw.md"_s, "żółw idzie\n"));
    QVERIFY(writeFile(root + u"/build/out.o"_s, "x"));
    QVERIFY(writeFile(root + u"/data.bin"_s, QByteArray("\0\1\2żółw", 10)));
    QVERIFY(writeFile(root + u"/logo.png"_s, "png"));
    QVERIFY(writeFile(root + u"/.gitignore"_s, "build/\n"));
}

QStringList paths(const QVariantList &rows)
{
    QStringList out;
    for (const QVariant &r : rows)
        out << r.toMap().value(u"path"_s).toString();
    return out;
}

} // namespace

class TestCodeFilesApp : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // (The cache is the app's: one for the whole test.)
        QVERIFY(FileIndexCache::instance());
    }

    // ---- The index ------------------------------------------------------------------------------

    // The index follows the disk: a file made in a subfolder, a folder deleted — found without a
    // refresh, a moment later (the folders are watched).
    void indexFollowsTheDisk()
    {
#ifdef Q_OS_WIN
        QSKIP("no folders are watched on Windows (a watched folder cannot be deleted there)");
#endif
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(u"src/main.cpp"_s), "int main() {}\n"));
        QVERIFY(writeFile(dir.filePath(u"docs/a.md"_s), "a\n"));
        FileIndexCache *cache = FileIndexCache::instance();
        cache->setGitProgram(QString()); // (a walk: the folder is no repository anyway)
        auto restore = qScopeGuard([cache] { cache->setGitProgram(u"git"_s); });
        const QString folder = dir.path();
        QVERIFY(!cache->index(folder));
        QTRY_VERIFY(cache->index(folder) && cache->index(folder)->contains(u"src/main.cpp"));
        QTRY_COMPARE(cache->watchedCount(folder), 3); // (the folder, docs, src)
        QVERIFY(writeFile(dir.filePath(u"src/żółw.cpp"_s), "\n"));
        QTRY_VERIFY_WITH_TIMEOUT(cache->index(folder)->contains(u"src/żółw.cpp"), 10000);
        QVERIFY(QDir(dir.filePath(u"docs"_s)).removeRecursively());
        QTRY_VERIFY_WITH_TIMEOUT(!cache->index(folder)->contains(u"docs/a.md"), 10000);
        QTRY_COMPARE(cache->watchedCount(folder), 2);
        // A new folder is watched too.
        QVERIFY(writeFile(dir.filePath(u"lib/x.h"_s), "\n"));
        QTRY_VERIFY_WITH_TIMEOUT(cache->index(folder)->contains(u"lib/x.h"), 10000);
        QVERIFY(writeFile(dir.filePath(u"lib/y.h"_s), "\n"));
        QTRY_VERIFY_WITH_TIMEOUT(cache->index(folder)->contains(u"lib/y.h"), 10000);
    }

    void indexOfARepository()
    {
        if (!hasGit())
            QSKIP("no git on PATH");
        QTemporaryDir dir;
        makeProject(dir.path());
        QVERIFY(git(dir.path(), {u"init"_s, u"-q"_s}));
        CodeFileIndex index;
        index.setKinds(u"files"_s);
        index.setQuery(u"main"_s);
        QSignalSpy rows(&index, &CodeFileIndex::rowsChanged);
        index.setFolder(dir.path());
        QVERIFY(index.building() || index.ready());
        QTRY_VERIFY_WITH_TIMEOUT(index.ready(), 20000);
        QCOMPARE(index.source(), u"git"_s);
        // Untracked files listed, ignored ones not; folders derived.
        QVERIFY(index.contains(u"src/main.cpp"_s));
        QVERIFY(index.contains(u"docs/żółw.md"_s));
        QVERIFY(index.contains(u".gitignore"_s));
        QVERIFY(!index.contains(u"build/out.o"_s));
        QVERIFY(index.isFolder(u"src/app"_s));
        QCOMPARE(index.fileCount(), 7);
        QTRY_VERIFY(!index.rows().isEmpty());
        QCOMPARE(paths(index.rows()), (QStringList{u"src/main.cpp"_s, u"src/app/qml/Main.qml"_s}));
        const QVariantMap first = index.rows().first().toMap();
        QCOMPARE(first.value(u"name"_s).toString(), u"main.cpp"_s);
        QCOMPARE(first.value(u"folder"_s).toString(), u"src"_s);
        QCOMPARE(first.value(u"iconName"_s).toString(), u"code"_s);
        QVERIFY(first.value(u"nameMarkup"_s).toString().startsWith(u"<b><font color="_s));
        QVERIFY(first.value(u"nameMarkup"_s).toString().endsWith(u".cpp"_s));
        QCOMPARE(index.pathAt(1), u"src/app/qml/Main.qml"_s);
        QCOMPARE(index.pathAt(5), QString());
        // A new file: seen after a refresh.
        QVERIFY(writeFile(dir.path() + u"/src/nowy_main.cpp"_s, "x"));
        QVERIFY(!index.contains(u"src/nowy_main.cpp"_s));
        index.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(index.contains(u"src/nowy_main.cpp"_s), 20000);
        QTRY_COMPARE(index.rows().size(), 3);
        // Folders and files; a limit; inactive: no rows.
        index.setKinds(u"all"_s);
        index.setQuery(u"src"_s);
        QVERIFY(paths(index.rows()).contains(u"src"_s));
        QCOMPARE(index.rows().first().toMap().value(u"title"_s).toString(), u"src/"_s);
        index.setLimit(1);
        QCOMPARE(index.rows().size(), 1);
        index.setActive(false);
        QCOMPARE(index.rows().size(), 0);
        index.setActive(true);
        QCOMPARE(index.rows().size(), 1);
    }

    void indexOfAFolder()
    {
        QTemporaryDir dir;
        makeProject(dir.path());
        CodeFileIndex index;
        index.setFolder(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(index.ready(), 20000);
        // (No repository: a walk, build output and hidden folders skipped.)
        QCOMPARE(index.source(), u"folder"_s);
        QVERIFY(index.contains(u"src/main.cpp"_s));
        QVERIFY(!index.contains(u"build/out.o"_s));
        // Recent files first while nothing is typed; only those the index has.
        index.setKinds(u"files"_s);
        index.setRecent({u"docs/żółw.md"_s, u"gone.txt"_s, u"src/main.cpp"_s});
        index.setQuery(QString());
        QStringList shown = paths(index.rows());
        QCOMPARE(shown.mid(0, 2), (QStringList{u"docs/żółw.md"_s, u"src/main.cpp"_s}));
        QCOMPARE(shown.count(u"src/main.cpp"_s), 1);
        QVERIFY(!shown.contains(u"gone.txt"_s));
        // Two views of one folder share its index.
        CodeFileIndex other;
        other.setFolder(dir.path() + u"/"_s);
        QVERIFY(other.ready());
        QCOMPARE(other.fileCount(), index.fileCount());
        // No folder: nothing.
        CodeFileIndex none;
        none.setQuery(u"a"_s);
        QVERIFY(!none.ready());
        QVERIFY(none.rows().isEmpty());
    }

    // Queries of a big index run on a pool thread: the rows come a moment later, the newest
    // query's only.
    void bigIndexQueriesOffTheGuiThread()
    {
        QTemporaryDir dir;
        QStringList files;
        for (int i = 0; i < CodeFileIndex::SyncQueryLimit + 500; ++i)
            files << u"d%1/plik%2.txt"_s.arg(i / 100).arg(i);
        // (Written as a git index would list them: the cache reads `git ls-files` — a fake git.)
        const QString fake = dir.filePath(u"fake-git"_s);
        QByteArray listing;
        for (const QString &f : std::as_const(files))
            listing += f.toUtf8() + '\0';
        QVERIFY(writeFile(dir.filePath(u"listing"_s), listing));
        QVERIFY(writeFile(fake, "#!/bin/sh\ncat \"" + dir.filePath(u"listing"_s).toUtf8() + "\"\n"));
        QFile::setPermissions(fake, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
#ifdef Q_OS_WIN
        QSKIP("a shell script as git");
#endif
        QVERIFY(QDir(dir.path()).mkpath(u"projekt"_s));
        FileIndexCache::instance()->setGitProgram(fake);
        auto restore = qScopeGuard([] { FileIndexCache::instance()->setGitProgram(u"git"_s); });
        CodeFileIndex index;
        index.setKinds(u"files"_s);
        index.setFolder(dir.filePath(u"projekt"_s));
        QTRY_VERIFY_WITH_TIMEOUT(index.ready(), 20000);
        QCOMPARE(index.fileCount(), int(files.size()));
        QSignalSpy rows(&index, &CodeFileIndex::rowsChanged);
        index.setQuery(u"plik1"_s);
        index.setQuery(u"plik20499"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!index.rows().isEmpty() && index.pathAt(0) == u"d204/plik20499.txt"_s, 20000);
        QCOMPARE(index.rows().size(), 1);
    }

    // ---- The file viewer's model ----------------------------------------------------------------

    void document()
    {
        QTemporaryDir dir;
        makeProject(dir.path());
        CodeFileDocument doc;
        doc.setPalette({{u"keyword"_s, QColor(u"#ff0000"_s)}, {u"comment"_s, u"#00ff00"_s}, {u"type"_s, QColor()}});
        QSignalSpy loaded(&doc, &CodeFileDocument::loaded);
        doc.setFolder(dir.path());
        doc.setPath(u"src/main.cpp"_s);
        QTRY_COMPARE(doc.kind(), u"text"_s);
        QCOMPARE(doc.lineCount(), 4);
        QCOMPARE(doc.language(), u"cpp"_s);
        QCOMPARE(doc.rowCount(), 4);
        QCOMPARE(doc.data(doc.index(0), CodeFileDocument::NumberRole).toInt(), 1);
        QCOMPARE(doc.data(doc.index(2), CodeFileDocument::TextRole).toString(), u"\treturn zolw(); // Żółw <b>&"_s);
        // Coloured, escaped, the tab as spaces (StyledText folds plain ones).
        const QString html = doc.data(doc.index(2), CodeFileDocument::HtmlRole).toString();
        QVERIFY2(html.startsWith(u"&nbsp;&nbsp;&nbsp;&nbsp;<font color=\"#ff0000\">return</font>"_s), qPrintable(html));
        QVERIFY2(html.contains(u"<font color=\"#00ff00\">//&nbsp;Żółw&nbsp;&lt;b&gt;&amp;</font>"_s), qPrintable(html));
        QVERIFY(!html.contains(u"<b>"_s));
        QCOMPARE(doc.maxColumns(), 31);
        QCOMPARE(doc.linesText(2, 1), u"int main()\n{"_s);
        QCOMPARE(doc.linesText(-5, 99).count(u'\n'), 3);
        QCOMPARE(doc.lineText(4), u"}"_s);
        QCOMPARE(doc.lineText(5), QString());
        QVERIFY(doc.absolutePath().endsWith(u"/src/main.cpp"_s));
        // A tab width of 2.
        doc.setTabWidth(2);
        QTRY_COMPARE(doc.kind(), u"text"_s);
        QTRY_VERIFY(doc.data(doc.index(2), CodeFileDocument::HtmlRole).toString().startsWith(u"&nbsp;&nbsp;<font"_s));
        // Pictures, binary and missing files, folders, paths that leave the folder.
        doc.setPath(u"logo.png"_s);
        QTRY_COMPARE(doc.kind(), u"picture"_s);
        QCOMPARE(doc.fileUrl(), QUrl::fromLocalFile(dir.filePath(u"logo.png"_s)));
        QCOMPARE(doc.rowCount(), 0);
        doc.setPath(u"data.bin"_s);
        QTRY_COMPARE(doc.kind(), u"binary"_s);
        doc.setPath(u"nope.txt"_s);
        QTRY_COMPARE(doc.kind(), u"missing"_s);
        doc.setPath(u"src"_s);
        QTRY_COMPARE(doc.kind(), u"folder"_s);
        doc.setPath(u"../../etc/passwd"_s);
        QCOMPARE(doc.kind(), u"outside"_s);
        QCOMPARE(doc.absolutePath(), QString());
        doc.setPath(QString());
        QCOMPARE(doc.kind(), QString());
        // Read again after a change.
        doc.setPath(u"README.md"_s);
        QTRY_COMPARE(doc.lineCount(), 1);
        QVERIFY(writeFile(dir.filePath(u"README.md"_s), "# A\n\nB\n"));
        doc.reload();
        // (Its lines stay shown meanwhile: no "loading" for the same file.)
        QCOMPARE(doc.kind(), u"text"_s);
        QCOMPARE(doc.lineCount(), 1);
        QTRY_COMPARE(doc.lineCount(), 3);
    }

    void lineHtml()
    {
        const QHash<int, QString> colours{{int(CodeToken::String), u"#123456"_s}};
        QCOMPARE(CodeFileDocument::lineHtml(u"a \"b\"", {{2, 3, CodeToken::String}}, colours, 4),
                 u"a&nbsp;<font color=\"#123456\">&quot;b&quot;</font>"_s);
        // A kind without a colour: plain text; a span past the line: cut.
        QCOMPARE(CodeFileDocument::lineHtml(u"x<y", {{0, 1, CodeToken::Keyword}, {2, 10, CodeToken::String}}, colours, 4),
                 u"x&lt;<font color=\"#123456\">y</font>"_s);
        // Tabs to the next stop; control characters dropped; long lines cut.
        QCOMPARE(CodeFileDocument::lineHtml(u"ab\tc\x01", {}, colours, 4), u"ab&nbsp;&nbsp;c"_s);
        const QString longLine(CodeFileDocument::MaxLineLength + 10, u'x');
        QVERIFY(CodeFileDocument::lineHtml(longLine, {}, colours, 4).endsWith(u"x…"_s));
        // Matches of the find bar: bold in the match colour, across a span's edge too.
        QCOMPARE(CodeFileDocument::lineHtml(u"a \"żółw\"", {{2, 6, CodeToken::String}}, colours, 4, {{0, 4}}, u"#ff0000"_s),
                 u"<b><font color=\"#ff0000\">a&nbsp;</font></b><b><font color=\"#ff0000\">&quot;ż</font></b>"
                 u"<font color=\"#123456\">ółw&quot;</font>"_s);
        // Without a colour: bold only; a match past the line's end cut.
        QCOMPARE(CodeFileDocument::lineHtml(u"abc", {}, colours, 4, {{1, 9}}, QString()), u"a<b>bc</b>"_s);
    }

    // Editing in the viewer (docs/CODE-FILES.md "Editing"): saved as it was read (CRLF, a byte order
    // mark), read again; refused when the file changed on disk meanwhile, for a file cut short, for a
    // binary one; a new break at the end kept as the file had it.
    void editAndSave()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(u"a.txt"_s), "\xef\xbb\xbfŻółw\r\nje\r\n"));
        CodeFileDocument doc;
        doc.setFolder(dir.path());
        doc.setPath(u"a.txt"_s);
        QTRY_COMPARE(doc.lineCount(), 2);
        QVERIFY(doc.editable());
        QCOMPARE(doc.editText(), u"Żółw\nje\n"_s);
        QSignalSpy loaded(&doc, &CodeFileDocument::loaded);
        QCOMPARE(doc.save(u"Żółw\nje sałatę\nwolno"_s), QString());
        QFile f(dir.filePath(u"a.txt"_s));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("\xef\xbb\xbfŻółw\r\nje sałatę\r\nwolno\r\n"));
        f.close();
        QTRY_COMPARE(doc.lineCount(), 3);
        QVERIFY(!loaded.isEmpty());
        // Changed by someone else meanwhile: refused, the file left as it is.
        QVERIFY(writeFile(dir.filePath(u"a.txt"_s), "inny\n"));
        QVERIFY(!doc.save(u"moje"_s).isEmpty());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("inny\n"));
        f.close();
        // Read again: editable again.
        doc.reload();
        QTRY_COMPARE(doc.lineText(1), u"inny"_s);
        QCOMPARE(doc.save(u"moje\n"_s), QString());
        // Not text: nothing.
        QVERIFY(writeFile(dir.filePath(u"b.bin"_s), QByteArray("\0\0\0", 3)));
        doc.setPath(u"b.bin"_s);
        QTRY_COMPARE(doc.kind(), u"binary"_s);
        QVERIFY(!doc.editable());
        QCOMPARE(doc.editText(), QString());
        QVERIFY(!doc.save(u"x"_s).isEmpty());
        // Invalid UTF-8: not offered (an edit would replace its bytes).
        QVERIFY(writeFile(dir.filePath(u"c.txt"_s), "a\xff b\n"));
        doc.setPath(u"c.txt"_s);
        QTRY_COMPARE(doc.kind(), u"text"_s);
        QVERIFY(!doc.editable());
    }

    // Find within the file shown (Ctrl+F in the viewer): its matches counted, line by line, with the
    // project search's options; an invalid expression said; cleared.
    void findInFile()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(u"a.md"_s), "Żółw je.\nnic\nŻÓŁW i żółw\n"));
        CodeFileDocument doc;
        doc.setFolder(dir.path());
        doc.setPath(u"a.md"_s);
        QTRY_COMPARE(doc.lineCount(), 3);
        QSignalSpy found(&doc, &CodeFileDocument::findChanged);
        doc.find(u"żółw"_s, false, false, false);
        QCOMPARE(doc.matchCount(), 3);
        QCOMPARE(doc.findError(), QString());
        QCOMPARE(doc.matchLine(0), 1);
        QCOMPARE(doc.matchLine(2), 3);
        QCOMPARE(doc.matchLine(3), 0);
        QCOMPARE(doc.matchLine(-1), 0);
        // The next one from a line, either way, wrapping around.
        QCOMPARE(doc.nextMatch(1, 1), 2);
        QCOMPARE(doc.nextMatch(1, -1), 0);
        QCOMPARE(doc.nextMatch(2, 1), 0);
        QCOMPARE(doc.nextMatch(0, -1), 2);
        QVERIFY(doc.data(doc.index(2), CodeFileDocument::HtmlRole).toString().contains(u"<b>"_s));
        QVERIFY(!doc.data(doc.index(1), CodeFileDocument::HtmlRole).toString().contains(u"<b>"_s));
        // Case and whole words; a regular expression, a broken one.
        doc.find(u"żółw"_s, true, false, false);
        QCOMPARE(doc.matchCount(), 1);
        doc.find(u"je"_s, false, true, false);
        QCOMPARE(doc.matchCount(), 1);
        doc.find(u"Ż\\w+"_s, true, false, true);
        QCOMPARE(doc.matchCount(), 2);
        doc.find(u"(["_s, false, false, true);
        QCOMPARE(doc.matchCount(), 0);
        QVERIFY(!doc.findError().isEmpty());
        doc.find(QString(), false, false, false);
        QCOMPARE(doc.matchCount(), 0);
        QCOMPARE(doc.findError(), QString());
        QVERIFY(!doc.data(doc.index(0), CodeFileDocument::HtmlRole).toString().contains(u"<b>"_s));
        QVERIFY(found.size() >= 6);
        QCOMPARE(doc.nextMatch(0, 1), -1);
        // The file read again: the matches of the new text.
        doc.find(u"żółw"_s, false, false, false);
        QVERIFY(writeFile(dir.filePath(u"a.md"_s), "żółw\n"));
        doc.reload();
        QTRY_COMPARE(doc.lineCount(), 1);
        QCOMPARE(doc.matchCount(), 1);
    }

    // ---- The project search ---------------------------------------------------------------------

    void searchWithGitGrep()
    {
        if (!hasGit())
            QSKIP("no git on PATH");
        QTemporaryDir dir;
        makeProject(dir.path());
        QVERIFY(git(dir.path(), {u"init"_s, u"-q"_s}));
        CodeSearch search;
        search.setRipgrepProgram(u"/nonexistent/rg"_s);
        search.setFolder(dir.path());
        search.start(u"żółw"_s);
        QVERIFY(search.running());
        QTRY_VERIFY_WITH_TIMEOUT(search.done(), 20000);
        // (rg did not start: git grep.)
        QCOMPARE(search.engine(), u"git"_s);
        QCOMPARE(search.error(), QString());
        QVERIFY(search.matchCount() >= 2);
        // A file's row, then its matches.
        QCOMPARE(search.data(search.index(0), CodeSearch::KindRole).toString(), u"file"_s);
        const int docRow = int(search.match(search.index(0), CodeSearch::PathRole, u"docs/żółw.md"_s).value(0).row());
        QCOMPARE(search.data(search.index(docRow), CodeSearch::KindRole).toString(), u"file"_s);
        QCOMPARE(search.data(search.index(docRow), CodeSearch::NameRole).toString(), u"żółw.md"_s);
        QCOMPARE(search.data(search.index(docRow), CodeSearch::FolderRole).toString(), u"docs"_s);
        QCOMPARE(search.data(search.index(docRow), CodeSearch::CountRole).toInt(), 1);
        QCOMPARE(search.lineAt(docRow + 1), 1);
        QCOMPARE(search.pathAt(docRow + 1), u"docs/żółw.md"_s);
        QVERIFY(search.data(search.index(docRow + 1), CodeSearch::HtmlRole).toString().startsWith(u"<b><font color="_s));
        QCOMPARE(search.nextMatch(docRow, 1), docRow + 1);
        QCOMPARE(search.nextMatch(-1, 1), 1);
        QCOMPARE(search.nextMatch(1, -1), -1);
        // Not in ignored or binary files.
        QVERIFY(search.match(search.index(0), CodeSearch::PathRole, u"data.bin"_s).isEmpty());
        // Nothing found; an invalid expression; cleared.
        search.start(u"nie ma tego nigdzie"_s);
        QTRY_VERIFY_WITH_TIMEOUT(search.done(), 20000);
        QCOMPARE(search.matchCount(), 0);
        QCOMPARE(search.error(), QString());
        search.setRegex(true);
        search.start(u"a("_s);
        QVERIFY(search.done());
        QVERIFY(!search.error().isEmpty());
        QVERIFY(!search.running());
        search.clear();
        QCOMPARE(search.rowCount(), 0);
        QVERIFY(!search.done());
    }

    void searchWithRipgrep()
    {
        const QString rg = QStandardPaths::findExecutable(u"rg"_s);
        if (rg.isEmpty())
            QSKIP("no rg on PATH");
        QTemporaryDir dir;
        makeProject(dir.path());
        if (hasGit())
            QVERIFY(git(dir.path(), {u"init"_s, u"-q"_s}));
        CodeSearch search;
        search.setRipgrepProgram(rg);
        search.setFolder(dir.path());
        search.setCaseSensitive(true);
        search.start(u"żółw"_s);
        QTRY_VERIFY_WITH_TIMEOUT(search.done(), 20000);
        QCOMPARE(search.engine(), u"rg"_s);
        // README's "żółwia" and docs' "żółw idzie"; not main.cpp's "Żółw" (the case), not the binary file.
        QCOMPARE(search.matchCount(), 2);
        QCOMPARE(search.fileCount(), 2);
        QVERIFY(search.match(search.index(0), CodeSearch::PathRole, u"src/main.cpp"_s).isEmpty());
    }

    void ownSearch()
    {
        QTemporaryDir dir;
        makeProject(dir.path());
        CodeSearch search;
        search.setRipgrepProgram(u"/nonexistent/rg"_s);
        search.setGitProgram(QString());
        search.setFolder(dir.path());
        search.setWholeWord(true);
        search.start(u"żółw"_s);
        QTRY_VERIFY_WITH_TIMEOUT(search.done(), 20000);
        QCOMPARE(search.engine(), u"brulion"_s);
        // "żółw idzie" and "Żółw" (case ignored), not "żółwia"; never the binary file or build output.
        QCOMPARE(search.matchCount(), 2);
        QVERIFY(search.match(search.index(0), CodeSearch::PathRole, u"data.bin"_s).isEmpty());
        // Bounded.
        search.setWholeWord(false);
        search.setMaxMatches(1);
        search.start(u"ż"_s);
        QTRY_VERIFY_WITH_TIMEOUT(search.done(), 20000);
        QCOMPARE(search.matchCount(), 1);
        QVERIFY(search.truncated());
        // Cancelled: nothing more comes.
        search.setMaxMatches(2000);
        search.start(u"a"_s);
        search.cancel();
        QVERIFY(!search.running());
        QTest::qWait(200);
        QCOMPARE(search.matchCount(), 0);
    }

    // ---- The QML helpers ------------------------------------------------------------------------

    void links()
    {
        QTemporaryDir dir;
        makeProject(dir.path());
        CodeFiles files;
        const QString folder = dir.path();
        QVariantMap t = files.linkTarget(folder, u"page:src%2Fmain.cpp#L3"_s);
        QCOMPARE(t.value(u"path"_s).toString(), u"src/main.cpp"_s);
        QCOMPARE(t.value(u"line"_s).toInt(), 3);
        QVERIFY(!t.value(u"isFolder"_s).toBool());
        t = files.linkTarget(folder, u"page:"_s + QString::fromLatin1(QUrl::toPercentEncoding(folder + u"/docs/żółw.md:1"_s)));
        QCOMPARE(t.value(u"path"_s).toString(), u"docs/żółw.md"_s);
        QCOMPARE(t.value(u"line"_s).toInt(), 1);
        QCOMPARE(files.linkTarget(folder, QUrl::fromLocalFile(folder + u"/src"_s).toString()).value(u"isFolder"_s).toBool(), true);
        QCOMPARE(files.linkPath(folder, u"page:README.md"_s), u"README.md"_s);
        QCOMPARE(files.linkLine(folder, u"page:README.md:9"_s), 9);
        // The notebook's pages, web addresses, files not there or outside: none.
        QVERIFY(files.linkTarget(folder, u"page:Dom"_s).isEmpty());
        QVERIFY(files.linkTarget(folder, u"ext:https://example.com/README.md"_s).isEmpty());
        QVERIFY(files.linkTarget(folder, u"tag:README.md"_s).isEmpty());
        QVERIFY(files.linkTarget(folder, u"page:..%2Fsecret.txt"_s).isEmpty());
        QVERIFY(files.linkTarget(folder, u"page:%2Fetc%2Fpasswd"_s).isEmpty());
        QVERIFY(files.linkTarget(QString(), u"page:README.md"_s).isEmpty());
        QCOMPARE(files.relativePaths(folder, {folder + u"/src/main.cpp"_s, u"/etc/passwd"_s, u"src/main.cpp"_s}),
                 QStringList{u"src/main.cpp"_s});
        QCOMPARE(files.absolutePath(folder, u"src/../README.md"_s), QDir::cleanPath(folder + u"/README.md"_s));
        QCOMPARE(files.absolutePath(folder, u"../x"_s), QString());
    }

    void helpers()
    {
        CodeFiles files;
        QCOMPARE(files.mentionStart(u"Popraw @src/ma"_s, 14), 7);
        QCOMPARE(files.mentionQuery(u"Popraw @src/ma"_s, 14), u"src/ma"_s);
        QCOMPARE(files.withMention(u"Popraw @src/ma"_s, 14, u"src/main.cpp"_s), u"Popraw @src/main.cpp "_s);
        QCOMPARE(files.mentionEnd(u"Popraw @src/ma"_s, 14, u"src/main.cpp"_s), 21);
        QCOMPARE(files.reviewComment(u"a.py"_s, 1, 2, u"x = 1\ny = 2"_s, u"Dlaczego?"_s),
                 u"a.py:1-2\n```python\nx = 1\ny = 2\n```\nDlaczego?"_s);
        QCOMPARE(files.withRecent({u"a"_s, u"b"_s, u"c"_s}, u"b"_s, 2), (QStringList{u"b"_s, u"a"_s}));
        QCOMPARE(files.withRecent({}, u"x"_s, 5), QStringList{u"x"_s});
        QVERIFY(files.recentKey(u"/a/b"_s).startsWith(u"code/recentFiles/"_s));
        QCOMPARE(files.recentKey(u"/a/b"_s), files.recentKey(u"/a/b/"_s));
        QVERIFY(files.recentKey(u"/a/b"_s) != files.recentKey(u"/a/c"_s));
        QCOMPARE(files.iconOf(u"a/b.png"_s, false), u"image"_s);
        QCOMPARE(files.iconOf(u"a/b"_s, true), u"folder"_s);
        QCOMPARE(files.iconOf(u"a/b.rs"_s, false), u"code"_s);
        QCOMPARE(files.iconOf(u"notes.md"_s, false), u"file-text"_s);
        QCOMPARE(files.languageOf(u"x.tsx"_s), u"tsx"_s);
        QVERIFY(files.copyText(u"żółw"_s));
    }
};

QTEST_MAIN(TestCodeFilesApp)
#include "tst_codefiles_app.moc"
