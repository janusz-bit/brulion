// ImportController and its helpers: the daily-note collision policy (appended under "## Import
// (<źródło>)", line endings, BOM, frontmatter, importing the same thing twice), kinds, default and
// user-typed target folders, reading a picked folder (skipped folders, limits), recognising the
// source with an override, the dry-run preview, writing with unique names for taken paths, the
// report, cancelling and the error states — against real Workspaces over temporary vaults.
// Android's picked documents (content:// URIs: files, a ZIP, a folder as a document tree) go
// through FakeDocuments, an in-memory DocumentProvider: the same walk, limits and warnings as on
// the phone, where the provider asks Android's DocumentsContract instead.
#include "fileexchange.h"
#include "importcontroller.h"
#include "iobackground.h"
#include "pickedfiles.h"
#include "workspace.h"

#include "io/importer.h"
#include "io/zip.h"
#include "vault/vault.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QTranslator>

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>

using namespace Qt::StringLiterals;
using brulion::io::InputFile;
using brulion::ioviews::ImportController;

namespace {

/// A translation that marks every text it translates ("[pl] …"): tells whether texts are read
/// again after the UI language changed.
class MarkingTranslator : public QTranslator {
public:
    bool isEmpty() const override { return false; }
    QString translate(const char *, const char *sourceText, const char * = nullptr, int = -1) const override
    {
        return u"[pl] "_s + QString::fromUtf8(sourceText);
    }
};

bool writeRaw(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(bytes);
    return true;
}

QByteArray readRaw(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>");
}

InputFile text(const QString &path, const QString &content)
{
    return InputFile{ path, content.toUtf8() };
}

/// A small Logseq graph: a journal page, two pages and a picture.
QList<InputFile> logseqGraph()
{
    return {
        text(u"graf/logseq/config.edn"_s, u"{:journal/file-name-format \"yyyy_MM_dd\"}"_s),
        text(u"graf/journals/2026_10_14.md"_s, u"- Spotkanie z Łucją o źródłach\n- TODO kupić żółty ser\n"_s),
        text(u"graf/pages/Zakupy.md"_s, u"- mleko\n- chleb ![](../assets/paragon.png)\n"_s),
        text(u"graf/pages/Pomysły.md"_s, u"- [[Zakupy]] na święta\n"_s),
        InputFile{ u"graf/assets/paragon.png"_s, QByteArray("\x89PNG\r\n\x1a\n", 8) },
    };
}

/// Android's document provider in memory: picked documents and document trees by URI, like the
/// Storage Access Framework hands them out. Read-only after setup (the gatherer asks it from a
/// worker thread); counts its listings.
class FakeDocuments final : public brulion::ioviews::DocumentProvider {
public:
    struct Node {
        QString name;
        bool dir = false;
        QByteArray data;
        QStringList children; ///< URIs
        bool isVirtual = false;
        bool unreadable = false;
        bool sizeUnknown = false;
    };

    /// A picked tree: `treeUri` (as the picker returns it) names the root document `rootUri`.
    void addTree(const QString &treeUri, const QString &rootUri, const QString &name)
    {
        trees.insert(treeUri, rootUri);
        nodes.insert(rootUri, Node{name, true, {}, {}, false, false, false});
    }
    QString addFolder(const QString &parentUri, const QString &name)
    {
        const QString uri = parentUri + u"%2F"_s + QString::fromLatin1(QUrl::toPercentEncoding(name));
        nodes.insert(uri, Node{name, true, {}, {}, false, false, false});
        nodes[parentUri].children.append(uri);
        return uri;
    }
    QString addFile(const QString &parentUri, const QString &name, const QByteArray &data)
    {
        const QString uri = parentUri + u"%2F"_s + QString::fromLatin1(QUrl::toPercentEncoding(name));
        nodes.insert(uri, Node{name, false, data, {}, false, false, false});
        if (!parentUri.isEmpty())
            nodes[parentUri].children.append(uri);
        return uri;
    }
    /// A picked single document (FilePicker.OpenFiles), not in any tree.
    void addDocument(const QString &uri, const QString &name, const QByteArray &data)
    {
        nodes.insert(uri, Node{name, false, data, {}, false, false, false});
    }

    std::optional<Entry> stat(const QUrl &url, QString *error) const override
    {
        ++stats;
        QString uri = brulion::ioviews::fileSystemPathOf(url);
        uri = trees.value(uri, uri);
        const auto it = nodes.constFind(uri);
        if (it == nodes.cend()) {
            *error = u"no such document"_s;
            return std::nullopt;
        }
        return entry(uri, *it);
    }
    std::optional<QList<Entry>> children(const Entry &folder, QString *error) const override
    {
        ++listings;
        if (delayMs > 0)
            QThread::msleep(delayMs);
        const auto it = nodes.constFind(folder.uri);
        if (it == nodes.cend() || !it->dir) {
            *error = u"not a folder"_s;
            return std::nullopt;
        }
        QList<Entry> out;
        for (const QString &child : it->children)
            out.append(entry(child, nodes.value(child)));
        return out;
    }
    std::optional<QByteArray> read(const Entry &file, qint64 maxBytes, QString *error) const override
    {
        ++reads;
        const Node node = nodes.value(file.uri);
        if (node.unreadable || node.dir) {
            *error = u"permission denied"_s;
            return std::nullopt;
        }
        return node.data.left(maxBytes + 1);
    }

    QHash<QString, Node> nodes;
    QHash<QString, QString> trees;
    int delayMs = 0;
    mutable std::atomic<int> stats{0};
    mutable std::atomic<int> listings{0};
    mutable std::atomic<int> reads{0};

private:
    static Entry entry(const QString &uri, const Node &node)
    {
        return Entry{uri, node.name, node.dir, node.dir || node.sizeUnknown ? -1 : node.data.size(), node.isVirtual};
    }
};

// The URIs Android's pickers hand out (ExternalStorageProvider trees, the Downloads provider).
const QString kTree = u"content://com.android.externalstorage.documents/tree/primary%3ADownload%2FSejf"_s;
const QString kTreeRoot =
        u"content://com.android.externalstorage.documents/tree/primary%3ADownload%2FSejf/document/primary%3ADownload%2FSejf"_s;

/// An Obsidian vault picked as a folder (FilePicker.OpenFolder → a document tree): notes with Polish
/// names, a picture, the vault's settings, a .git folder (skipped) and a Google Docs file that
/// exists only online (skipped with a warning).
std::shared_ptr<FakeDocuments> obsidianTree()
{
    auto docs = std::make_shared<FakeDocuments>();
    docs->addTree(kTree, kTreeRoot, u"Sejf"_s);
    const QString settings = docs->addFolder(kTreeRoot, u".obsidian"_s);
    docs->addFile(settings, u"app.json"_s, "{}");
    const QString notes = docs->addFolder(kTreeRoot, u"Notatki"_s);
    docs->addFile(notes, u"Zażółć gęślą jaźń.md"_s,
                  QStringLiteral("# Jaźń\n\nZobacz [[Łódź]] ![[zdjęcie.png]]\n").toUtf8());
    docs->addFile(notes, u"Łódź.md"_s, QStringLiteral("- Piotrkowska\n").toUtf8());
    docs->addFile(kTreeRoot, u"zdjęcie.png"_s, QByteArray("\x89PNG\r\n\x1a\n", 8));
    const QString git = docs->addFolder(kTreeRoot, u".git"_s);
    docs->addFile(git, u"HEAD"_s, "ref: refs/heads/main\n");
    const QString online = docs->addFile(kTreeRoot, u"Plan podróży"_s, {});
    docs->nodes[online].isVirtual = true;
    return docs;
}

QStringList pathsOf(const QList<InputFile> &files)
{
    QStringList paths;
    for (const InputFile &f : files)
        paths.append(f.path);
    paths.sort();
    return paths;
}

} // namespace

class TestAppIoImport : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;

    std::unique_ptr<brulion::shell::Workspace> open(const QHash<QString, QByteArray> &files = {}, QString *rootOut = nullptr)
    {
        const QString root = m_tmp.filePath(u"vault-%1"_s.arg(++m_n));
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            writeRaw(root + u'/' + it.key(), it.value());
        auto ws = std::make_unique<brulion::shell::Workspace>();
        ws->setClock([] { return QDateTime(QDate(2026, 10, 15), QTime(9, 0)); });
        ws->sync()->setSyncthingDiscovery(false);
        QString error;
        if (!ws->open(root, root + u"-state"_s, &error))
            qWarning() << error;
        ws->finishIndexing();
        if (rootOut)
            *rootOut = root;
        return ws;
    }

    static bool waitState(ImportController &c, const QString &state, int ms = 10000)
    {
        QElapsedTimer t;
        t.start();
        while (c.state() != state && t.elapsed() < ms)
            QTest::qWait(10);
        if (c.state() != state)
            qWarning() << "state" << c.state() << "error" << c.errorText();
        return c.state() == state;
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    // ---- Collision policy for daily notes --------------------------------------------------------

    void appendJournal_data()
    {
        QTest::addColumn<QByteArray>("existing");
        QTest::addColumn<QByteArray>("imported");
        QTest::addColumn<QByteArray>("expected");
        QTest::newRow("appended under a heading")
                << QByteArray("- rano: kawa\n") << QByteArray("- spotkanie z Łucją\n")
                << QByteArray("- rano: kawa\n\n## Import (Logseq)\n\n- spotkanie z Łucją\n");
        QTest::newRow("CRLF kept")
                << QByteArray("- zażółć\r\n- gęślą\r\n") << QByteArray("- jaźń\n")
                << QByteArray("- zażółć\r\n- gęślą\r\n\r\n## Import (Logseq)\r\n\r\n- jaźń\r\n");
        QTest::newRow("imported frontmatter dropped")
                << QByteArray("- dziś\n") << QByteArray("---\ntags: [import]\n---\n\n- z importu\n")
                << QByteArray("- dziś\n\n## Import (Logseq)\n\n- z importu\n");
        QTest::newRow("empty existing takes the import")
                << QByteArray("\n\n") << QByteArray("- tylko import\n") << QByteArray("- tylko import\n");
        QTest::newRow("empty import changes nothing")
                << QByteArray("- moje\n") << QByteArray("---\na: 1\n---\n\n") << QByteArray("- moje\n");
        QTest::newRow("same import twice")
                << QByteArray("- moje\n\n## Import (Logseq)\n\n- spotkanie\n") << QByteArray("- spotkanie\n")
                << QByteArray("- moje\n\n## Import (Logseq)\n\n- spotkanie\n");
        QTest::newRow("BOM kept")
                << QByteArray("\xEF\xBB\xBF- ąę\n") << QByteArray("- ść\n")
                << QByteArray("\xEF\xBB\xBF- ąę\n\n## Import (Logseq)\n\n- ść\n");
        QTest::newRow("no final newline")
                << QByteArray("- ostatnia linia") << QByteArray("- nowa")
                << QByteArray("- ostatnia linia\n\n## Import (Logseq)\n\n- nowa\n");
    }

    void appendJournal()
    {
        QFETCH(QByteArray, existing);
        QFETCH(QByteArray, imported);
        QFETCH(QByteArray, expected);
        QCOMPARE(brulion::ioviews::appendImportedJournal(existing, imported, u"Logseq"_s), expected);
    }

    void kindsAndFolders()
    {
        const brulion::VaultConfig config;
        QCOMPARE(brulion::ioviews::importedKind(u"journals/2026-10-14.md"_s, false, config), u"journal"_s);
        QCOMPARE(brulion::ioviews::importedKind(u"Import/Notion/Plan.md"_s, false, config), u"page"_s);
        QCOMPARE(brulion::ioviews::importedKind(u"Import/x.md"_s, true, config), u"journal"_s);
        QCOMPARE(brulion::ioviews::importedKind(u"assets/Notion/zdjęcie.jpg"_s, false, config), u"attachment"_s);
        QCOMPARE(brulion::ioviews::defaultImportFolder(u"Google Keep"_s), u"Import/Google Keep"_s);
        QCOMPARE(brulion::ioviews::defaultImportFolder(u"Roam Research"_s), u"Import/Roam Research"_s);
    }

    void cleanTargetFolder_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<bool>("ok");
        QTest::addColumn<QString>("expected");
        QTest::newRow("default") << u"Import/Notion"_s << true << u"Import/Notion"_s;
        QTest::newRow("slashes") << u"/Import//Notion/"_s << true << u"Import/Notion"_s;
        QTest::newRow("backslashes") << u"Import\\Stare notatki"_s << true << u"Import/Stare notatki"_s;
        QTest::newRow("polish") << u"Archiwum/Zażółć gęślą"_s << true << u"Archiwum/Zażółć gęślą"_s;
        QTest::newRow("root") << u""_s << true << u""_s;
        QTest::newRow("dot dot") << u"Import/../.."_s << false << QString();
        QTest::newRow("trash") << u".trash/x"_s << false << QString();
        QTest::newRow("config") << u".brulion"_s << false << QString();
        QTest::newRow("reserved") << u"Import/a:b"_s << false << QString();
        QTest::newRow("star") << u"Imp*rt"_s << false << QString();
    }

    void cleanTargetFolder()
    {
        QFETCH(QString, input);
        QFETCH(bool, ok);
        QFETCH(QString, expected);
        const auto clean = brulion::ioviews::cleanTargetFolder(input);
        QCOMPARE(clean.has_value(), ok);
        if (ok)
            QCOMPARE(*clean, expected);
    }

    void importerTexts()
    {
        ImportController c;
        const QVariantList list = c.importers();
        QCOMPARE(list.size(), brulion::io::importers().size());
        QStringList ids;
        for (const QVariant &v : list) {
            const QVariantMap m = v.toMap();
            ids.append(m.value(u"id"_s).toString());
            QVERIFY2(!m.value(u"howTo"_s).toString().isEmpty(), qPrintable(m.value(u"id"_s).toString()));
            // The phone's steps (Android): every app says where its notes are and what to pick.
            QVERIFY2(!m.value(u"phoneHowTo"_s).toString().isEmpty(), qPrintable(m.value(u"id"_s).toString()));
            QVERIFY(QStringList({ u"files"_s, u"folder"_s, u"both"_s }).contains(m.value(u"pick"_s).toString()));
            QVERIFY(!m.value(u"fileTypes"_s).toStringList().isEmpty());
        }
        QVERIFY(ids.contains(u"notion"_s) && ids.contains(u"keep"_s) && ids.contains(u"ics"_s));
        QVERIFY(c.nameFilters().last().contains(u"*"_s));
        c.setImporterId(u"evernote"_s);
        QCOMPARE(c.nameFilters().first(), u"Evernote (*.enex)"_s);
        QCOMPARE(c.targetFolder(), u"Import/Evernote"_s);
        c.setImporterId(u"nie-ma-takiego"_s);
        QCOMPARE(c.importerId(), QString());
    }

    void importersFollowTheLanguage()
    {
        ImportController c;
        QSignalSpy importers(&c, &ImportController::importersChanged);
        QSignalSpy importer(&c, &ImportController::importerChanged);
        const QString howTo = c.importers().first().toMap().value(u"howTo"_s).toString();
        QVERIFY(!howTo.startsWith(u"[pl] "_s));
        MarkingTranslator translation;
        QVERIFY(QCoreApplication::installTranslator(&translation));
        QCOMPARE(importers.size(), 1);
        QCOMPARE(importer.size(), 1); // nameFilters ("All files (*)")
        QCOMPARE(c.importers().first().toMap().value(u"howTo"_s).toString(), u"[pl] "_s + howTo);
        QVERIFY(c.nameFilters().last().startsWith(u"[pl] "_s));
        QVERIFY(QCoreApplication::removeTranslator(&translation));
        QCOMPARE(importers.size(), 2);
        QCOMPARE(c.importers().first().toMap().value(u"howTo"_s).toString(), howTo);
    }

    // ---- Reading picked files -----------------------------------------------------------------------

    void gatherFolder()
    {
        const QString dir = m_tmp.filePath(u"Sejf Obsidiana"_s);
        writeRaw(dir + u"/.obsidian/app.json"_s, "{}");
        writeRaw(dir + u"/Notatki/Zażółć.md"_s, "- gęślą jaźń\n");
        writeRaw(dir + u"/.git/HEAD"_s, "ref: refs/heads/main\n");
        writeRaw(dir + u"/node_modules/x/index.js"_s, "x");
        writeRaw(dir + u"/duży.bin"_s, QByteArray(3000, 'x'));
        brulion::ioviews::FileGatherer g;
        g.setChunkSize(1);
        brulion::ioviews::GatherLimits limits;
        limits.maxFileBytes = 2000;
        g.setLimits(limits);
        QSignalSpy finished(&g, &brulion::ioviews::FileGatherer::finished);
        QSignalSpy progress(&g, &brulion::ioviews::FileGatherer::progress);
        g.start({ QUrl::fromLocalFile(dir) });
        QVERIFY(finished.wait(5000));
        const auto files = finished.first().at(0).value<QList<InputFile>>();
        QStringList paths;
        for (const InputFile &f : files)
            paths.append(f.path);
        paths.sort();
        QCOMPARE(paths, QStringList({ u"Sejf Obsidiana/.obsidian/app.json"_s, u"Sejf Obsidiana/Notatki/Zażółć.md"_s }));
        QCOMPARE(finished.first().at(2).toString(), u"Sejf Obsidiana"_s);
        const QStringList warnings = finished.first().at(1).toStringList();
        QCOMPARE(warnings.size(), 1);
        QVERIFY(warnings.first().contains(u"duży.bin"_s));
        QVERIFY(progress.size() >= 3); // chunks of one file
        QVERIFY(brulion::io::detectImporter(files));
        QCOMPARE(brulion::io::detectImporter(files)->id(), u"obsidian"_s);
    }

    // The source name and the "too large" warning are shared with the browser's pick
    // (ImportController::pickInBrowser): the texts the gatherer produces itself.
    void gathererTexts_data()
    {
        QTest::addColumn<int>("count");
        QTest::addColumn<QString>("single");
        QTest::addColumn<QString>("expected");
        QTest::newRow("one") << 1 << u"Zażółć gęślą jaźń.md"_s << u"Zażółć gęślą jaźń.md"_s;
        QTest::newRow("one, empty name") << 1 << QString() << QString();
        QTest::newRow("two") << 2 << u"a.md"_s << u"2 file(s)"_s;
        QTest::newRow("five") << 5 << u"a.md"_s << u"5 file(s)"_s;
        QTest::newRow("none") << 0 << QString() << u"0 file(s)"_s;
    }
    void gathererTexts()
    {
        QFETCH(int, count);
        QFETCH(QString, single);
        QFETCH(QString, expected);
        QCOMPARE(brulion::ioviews::FileGatherer::sourceNameFor(count, single), expected);
    }
    void gathererTooLargeWarning()
    {
        const QString dir = m_tmp.filePath(u"Duże pliki"_s);
        writeRaw(dir + u"/duży.zip"_s, QByteArray(3000, 'x'));
        writeRaw(dir + u"/mały.md"_s, "- ćma\n");
        brulion::ioviews::FileGatherer g;
        brulion::ioviews::GatherLimits limits;
        limits.maxFileBytes = 2000;
        g.setLimits(limits);
        QSignalSpy finished(&g, &brulion::ioviews::FileGatherer::finished);
        g.start({QUrl::fromLocalFile(dir + u"/duży.zip"_s), QUrl::fromLocalFile(dir + u"/mały.md"_s)});
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().at(1).toStringList(),
                 QStringList{brulion::ioviews::FileGatherer::tooLargeWarning(u"duży.zip"_s, 3000)});
        QCOMPARE(finished.first().at(2).toString(), brulion::ioviews::FileGatherer::sourceNameFor(2, u"duży.zip"_s));
        QCOMPARE(finished.first().at(2).toString(), u"2 file(s)"_s);
        const QString warning = brulion::ioviews::FileGatherer::tooLargeWarning(u"duży.zip"_s, 600LL * 1024 * 1024);
        QVERIFY2(warning.startsWith(u"“duży.zip” ("_s) && warning.endsWith(u") is too large and was skipped."_s),
                 qPrintable(warning));
        QVERIFY(warning.contains(u"600"_s));
    }

    // The browser's pick (ImportController::pickInBrowser: PickCollector with the gatherer's
    // limits, then setPickedFiles) gives what the desktop's gatherer gives for the same files: the
    // same files read, the same warnings in the same order (too large; reading stopped at the
    // total, once — the browser keeps every read file in memory, so it must stop there too) and
    // the same source name.
    void browserPickAsGathered_data()
    {
        QTest::addColumn<QStringList>("names");
        QTest::addColumn<QList<int>>("sizes");
        QTest::addColumn<qint64>("maxFile");
        QTest::addColumn<qint64>("maxTotal");
        QTest::addColumn<int>("read");
        QTest::addColumn<int>("warnings");
        const qint64 big = brulion::ioviews::GatherLimits{}.maxFileBytes;
        const qint64 all = brulion::ioviews::GatherLimits{}.maxTotalBytes;
        QTest::newRow("jeden plik") << QStringList{u"Zażółć gęślą jaźń.md"_s} << QList<int>{40} << big << all << 1 << 0;
        QTest::newRow("w limitach") << QStringList{u"Przepisy.md"_s, u"Żurek.md"_s, u"pusty.md"_s}
                                    << QList<int>{30, 20, 0} << big << all << 3 << 0;
        QTest::newRow("za duży") << QStringList{u"duży.zip"_s, u"mały.md"_s} << QList<int>{3000, 6} << qint64(2000)
                                 << all << 1 << 1;
        QTest::newRow("suma") << QStringList{u"a.md"_s, u"b.md"_s, u"ć.md"_s, u"d.md"_s}
                              << QList<int>{1000, 1000, 1000, 10} << qint64(2000) << qint64(2500) << 2 << 1;
        QTest::newRow("za duży, potem suma")
                << QStringList{u"ą.md"_s, u"ogromny.zip"_s, u"ę.md"_s, u"ć.md"_s, u"ź.md"_s}
                << QList<int>{1000, 2000, 1000, 1000, 10} << qint64(1500) << qint64(2500) << 2 << 2;
        QTest::newRow("dokładnie suma") << QStringList{u"a.md"_s, u"b.md"_s} << QList<int>{1250, 1250} << qint64(2000)
                                        << qint64(2500) << 2 << 0;
        QTest::newRow("pierwszy ponad sumę")
                << QStringList{u"a.md"_s, u"b.md"_s} << QList<int>{2000, 5} << qint64(-1) << qint64(1000) << 0 << 1;
        QTest::newRow("wszystkie za duże")
                << QStringList{u"a.zip"_s, u"b.zip"_s} << QList<int>{20, 30} << qint64(10) << all << 0 << 2;
    }
    void browserPickAsGathered()
    {
        QFETCH(QStringList, names);
        QFETCH(QList<int>, sizes);
        QFETCH(qint64, maxFile);
        QFETCH(qint64, maxTotal);
        QFETCH(int, read);
        QFETCH(int, warnings);
        brulion::ioviews::GatherLimits limits;
        limits.maxFileBytes = maxFile < 0 ? std::numeric_limits<qint64>::max() : maxFile;
        limits.maxTotalBytes = maxTotal;
        const QString dir = m_tmp.filePath(u"wybór-%1"_s.arg(++m_n));
        QList<QUrl> urls;
        QList<QByteArray> bytes;
        for (qsizetype i = 0; i < names.size(); ++i) {
            bytes.append(QByteArray(sizes.at(i), char('a' + i)));
            writeRaw(dir + u'/' + names.at(i), bytes.last());
            urls.append(QUrl::fromLocalFile(dir + u'/' + names.at(i)));
        }
        // The desktop: the gatherer reads the picked files.
        brulion::ioviews::FileGatherer g;
        g.setLimits(limits);
        QSignalSpy finished(&g, &brulion::ioviews::FileGatherer::finished);
        g.start(urls);
        QVERIFY(finished.wait(5000));
        const auto gathered = finished.first().at(0).value<QList<InputFile>>();
        const QStringList gatheredWarnings = finished.first().at(1).toStringList();
        const QString gatheredName = finished.first().at(2).toString();
        QCOMPARE(gathered.size(), read);
        QCOMPARE(gatheredWarnings.size(), warnings);

        // The browser: Qt's calls as QWasmLocalFileAccess makes them, the gatherer's limits.
        QList<brulion::platform::PickedFile> picked;
        brulion::platform::PickCollector collector(
                limits.maxFileBytes, limits.maxTotalBytes,
                [&](const QList<brulion::platform::PickedFile> &files) { picked = files; });
        collector.closed(int(names.size()));
        for (qsizetype i = 0; i < names.size(); ++i) {
            if (char *buffer = collector.accept(quint64(bytes.at(i).size()), names.at(i))) {
                std::copy(bytes.at(i).cbegin(), bytes.at(i).cend(), buffer);
                collector.ready();
            }
        }
        QVERIFY(collector.finished());
        auto ws = open();
        ImportController c;
        c.setWorkspace(ws.get());
        c.gatherer()->setLimits(limits);
        QSignalSpy inputChanged(&c, &ImportController::inputChanged);
        c.setPickedFiles(picked);
        QCOMPARE(inputChanged.size(), 1);
        QCOMPARE(c.inputCount(), int(gathered.size()));
        qint64 gatheredBytes = 0;
        for (const InputFile &f : gathered)
            gatheredBytes += f.data.size();
        QCOMPARE(c.inputBytes(), gatheredBytes);
        QCOMPARE(c.readWarnings(), gatheredWarnings);
        QCOMPARE(c.sourceName(), gatheredName);
        if (maxTotal >= 0 && gatheredWarnings.contains(brulion::ioviews::FileGatherer::totalLimitWarning(maxTotal)))
            QCOMPARE(gatheredWarnings.last(), brulion::ioviews::FileGatherer::totalLimitWarning(maxTotal));
        // A cancelled pick (no files) changes nothing.
        c.setPickedFiles({});
        QCOMPARE(inputChanged.size(), 1);
        QCOMPARE(c.sourceName(), gatheredName);
    }

    void gatherSkipsLinksOutsideAndSpecialFiles()
    {
#ifdef Q_OS_UNIX
        // A downloaded "vault" with links pointing out of it (a private key, the user's own
        // notes) and a named pipe: only what lies inside the chosen folder is read.
        const QString secret = m_tmp.filePath(u"poza/klucz prywatny"_s);
        writeRaw(secret, "-----BEGIN OPENSSH PRIVATE KEY-----\n");
        const QString dir = m_tmp.filePath(u"Pobrany sejf"_s);
        writeRaw(dir + u"/Notatki/Źródło.md"_s, "- treść\n");
        QVERIFY(QFile::link(secret, dir + u"/Notatki/klucz.md"_s));
        QVERIFY(QFile::link(dir + u"/Notatki/Źródło.md"_s, dir + u"/Skrót.md"_s)); // inside: fine
        QVERIFY(QFile::link(m_tmp.filePath(u"poza"_s), dir + u"/folder-poza"_s)); // folder links: never
        QVERIFY(QProcess::execute(u"mkfifo"_s, { dir + u"/rura"_s }) == 0);
        brulion::ioviews::FileGatherer g;
        QSignalSpy finished(&g, &brulion::ioviews::FileGatherer::finished);
        g.start({ QUrl::fromLocalFile(dir) });
        QVERIFY(finished.wait(5000));
        const auto files = finished.first().at(0).value<QList<InputFile>>();
        QStringList paths;
        for (const InputFile &f : files) {
            paths.append(f.path);
            QVERIFY(!f.data.contains("PRIVATE KEY"));
        }
        paths.sort();
        QCOMPARE(paths, QStringList({ u"Pobrany sejf/Notatki/Źródło.md"_s, u"Pobrany sejf/Skrót.md"_s }));
        const QStringList warnings = finished.first().at(1).toStringList();
        QCOMPARE(warnings.size(), 1);
        QVERIFY(warnings.first().contains(u"klucz.md"_s));
#else
        QSKIP("symlinks and pipes: Unix only");
#endif
    }

    void gatherLimits()
    {
        const QString dir = m_tmp.filePath(u"wiele"_s);
        for (int i = 0; i < 12; ++i)
            writeRaw(dir + u"/n%1.md"_s.arg(i, 2, 10, QLatin1Char('0')), QByteArray(100, 'a'));
        brulion::ioviews::FileGatherer g;
        brulion::ioviews::GatherLimits limits;
        limits.maxFiles = 10;
        limits.maxTotalBytes = 550;
        g.setLimits(limits);
        QSignalSpy finished(&g, &brulion::ioviews::FileGatherer::finished);
        g.start({ QUrl::fromLocalFile(dir), QUrl::fromLocalFile(m_tmp.filePath(u"nie istnieje.zip"_s)) });
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().at(0).value<QList<InputFile>>().size(), 5);
        const QStringList warnings = finished.first().at(1).toStringList();
        QCOMPARE(warnings.size(), 3); // more than 10 files, the missing file, the total limit
        QCOMPARE(finished.first().at(2).toString(), u"2 file(s)"_s);
    }

    void gatherCancel()
    {
        const QString dir = m_tmp.filePath(u"anulowane"_s);
        for (int i = 0; i < 50; ++i)
            writeRaw(dir + u"/n%1.md"_s.arg(i), "x");
        brulion::ioviews::FileGatherer g;
        g.setChunkSize(1);
        QSignalSpy finished(&g, &brulion::ioviews::FileGatherer::finished);
        QSignalSpy cancelled(&g, &brulion::ioviews::FileGatherer::cancelled);
        // Cancelled halfway, from a progress handler (the UI's Cancel button between two chunks).
        connect(&g, &brulion::ioviews::FileGatherer::progress, &g, [&g](int done, int) {
            if (done >= 3)
                g.cancel();
        });
        g.start({ QUrl::fromLocalFile(dir) });
        QTRY_COMPARE(cancelled.size(), 1);
        QTest::qWait(50);
        QCOMPARE(finished.size(), 0);
        QVERIFY(!g.isRunning());
        // Right after start: nothing is read at all.
        g.disconnect(&g);
        g.start({ QUrl::fromLocalFile(dir) });
        g.cancel();
        QCOMPARE(cancelled.size(), 2);
        QTest::qWait(20);
        QCOMPARE(finished.size(), 0);
    }

    // ---- The controller ------------------------------------------------------------------------------

    void previewAndImport()
    {
        QString root;
        auto ws = open({ { u"journals/2026-10-14.md"_s, "- moja notatka z rana\n" },
                         { u"Import/Logseq/Zakupy.md"_s, "- już tu jestem\n" } },
                       &root);
        ImportController c;
        c.setWorkspace(ws.get());
        c.setWriteChunk(2);
        QCOMPARE(c.state(), u"empty"_s);
        c.setInput(logseqGraph(), u"graf"_s);
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(c.detectedId(), u"logseq"_s);
        QCOMPARE(c.effectiveId(), u"logseq"_s);
        QCOMPARE(c.effectiveName(), u"Logseq"_s);
        QCOMPARE(c.targetFolder(), u"Import/Logseq"_s);
        const QVariantMap p = c.preview();
        QCOMPARE(p.value(u"pages"_s).toInt(), 2);
        QCOMPARE(p.value(u"journals"_s).toInt(), 1);
        QCOMPARE(p.value(u"journalsAppended"_s).toInt(), 1);
        QCOMPARE(p.value(u"attachments"_s).toInt(), 1);
        QCOMPARE(p.value(u"fileCount"_s).toInt(), 4);
        QVERIFY(p.value(u"folderExists"_s).toBool());
        // The dry run renamed the taken page and said so.
        QStringList planned;
        for (const QVariant &v : p.value(u"files"_s).toList())
            planned.append(v.toMap().value(u"path"_s).toString());
        QVERIFY2(planned.contains(u"Import/Logseq/Zakupy 2.md"_s), qPrintable(planned.join(u", "_s)));
        QCOMPARE(p.value(u"renamed"_s).toInt(), 1);
        // Nothing was written by the preview.
        QCOMPARE(readRaw(root + u"/journals/2026-10-14.md"_s), QByteArray("- moja notatka z rana\n"));
        QVERIFY(!QFileInfo::exists(root + u"/Import/Logseq/Pomysły.md"_s));

        QSignalSpy imported(&c, &ImportController::imported);
        QSignalSpy progress(&c, &ImportController::progressChanged);
        c.startImport();
        QVERIFY(waitState(c, u"done"_s));
        QCOMPARE(imported.size(), 1);
        QVERIFY(progress.size() >= 3);
        const QVariantMap r = c.report();
        QCOMPARE(r.value(u"pages"_s).toInt(), 2);
        QCOMPARE(r.value(u"journals"_s).toInt(), 1);
        QCOMPARE(r.value(u"appended"_s).toInt(), 1);
        QCOMPARE(r.value(u"attachments"_s).toInt(), 1);
        QCOMPARE(r.value(u"written"_s).toInt(), 4);
        QVERIFY(r.value(u"failed"_s).toList().isEmpty());
        QCOMPARE(r.value(u"folder"_s).toString(), u"Import/Logseq"_s);
        QVERIFY(!r.value(u"cancelled"_s).toBool());

        // The existing journal got the import under a heading; the taken page kept its text.
        const QString journal = QString::fromUtf8(readRaw(root + u"/journals/2026-10-14.md"_s));
        QVERIFY2(journal.startsWith(u"- moja notatka z rana\n\n## Import (Logseq)\n\n- Spotkanie z Łucją o źródłach\n"_s),
                 qPrintable(journal));
        QCOMPARE(readRaw(root + u"/Import/Logseq/Zakupy.md"_s), QByteArray("- już tu jestem\n"));
        QVERIFY(QFileInfo::exists(root + u"/Import/Logseq/Zakupy 2.md"_s));
        // The link to the renamed page follows it.
        QVERIFY(QString::fromUtf8(readRaw(root + u"/Import/Logseq/Pomysły.md"_s)).contains(u"Zakupy 2"_s));
        QVERIFY(QFileInfo::exists(root + u"/assets/Logseq/paragon.png"_s));
        // The replaced journal text is in the history (ChangeSource::Import).
        const QVariantList history = ws->history(u"journals/2026-10-14.md"_s);
        QVERIFY(!history.isEmpty());
        QCOMPARE(history.first().toMap().value(u"reason"_s).toString(), u"import"_s);
        // The index knows the new pages at once.
        QVERIFY(ws->exists(u"Import/Logseq/Pomysły.md"_s));
    }

    void overrideAndTargetFolder()
    {
        auto ws = open();
        ImportController c;
        c.setWorkspace(ws.get());
        // Plain Markdown files: recognised as Markdown, imported as Obsidian on request.
        c.setInput({ text(u"Notatka o źdźbłach.md"_s, u"Treść [[Inna]]\n"_s), text(u"Inna.md"_s, u"x\n"_s) },
                   u"2 pliki"_s);
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(c.detectedId(), u"markdown"_s);
        QCOMPARE(c.targetFolder(), u"Import/Markdown"_s);
        c.setImporterId(u"obsidian"_s);
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(c.effectiveId(), u"obsidian"_s);
        QCOMPARE(c.targetFolder(), u"Import/Obsidian"_s);
        // A folder typed by the user stays, also when the importer changes.
        c.setTargetFolder(u"Archiwum/Stare"_s);
        QVERIFY(c.targetFolderError().isEmpty());
        QVERIFY(waitState(c, u"analyzing"_s, 2000) || c.state() == u"ready"_s);
        QVERIFY(waitState(c, u"ready"_s));
        c.setImporterId(u"markdown"_s);
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(c.targetFolder(), u"Archiwum/Stare"_s);
        QCOMPARE(c.preview().value(u"folder"_s).toString(), u"Archiwum/Stare"_s);
        // An unusable folder blocks the import.
        c.setTargetFolder(u"../poza"_s);
        QVERIFY(!c.targetFolderError().isEmpty());
        QTest::qWait(400);
        c.startImport();
        QVERIFY(c.state() != u"importing"_s);
        c.setTargetFolder(u"Archiwum/Stare"_s);
        QVERIFY(waitState(c, u"ready"_s));
        c.startImport();
        QVERIFY(waitState(c, u"done"_s));
        QVERIFY(ws->exists(u"Archiwum/Stare/Notatka o źdźbłach.md"_s));
        QCOMPARE(c.report().value(u"firstPage"_s).toString().startsWith(u"Archiwum/Stare/"_s), true);
    }

    void lettersCaseNeverMakesASecondPageOrFolder()
    {
        // Case-insensitive storage (Android shared storage, FAT, macOS) holds one "Archiwum" for
        // "archiwum", one "Dom.md" for "dom.md": the import uses the folder as spelled and gives a
        // page differing only in case another name (counted in the preview).
        QString root;
        auto ws = open({{u"Archiwum/Dom.md"_s, "- mój dom\n"}}, &root);
        ImportController c;
        c.setWorkspace(ws.get());
        c.setInput({text(u"dom.md"_s, u"- importowany dom\n"_s), text(u"Ogród.md"_s, u"- [[dom]]\n"_s)}, u"2 pliki"_s);
        QVERIFY(waitState(c, u"ready"_s));
        c.setTargetFolder(u"archiwum"_s);
        QVERIFY(waitState(c, u"analyzing"_s, 2000) || c.state() == u"ready"_s);
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(c.preview().value(u"renamed"_s).toInt(), 1);
        c.startImport();
        QVERIFY(waitState(c, u"done"_s));
        QCOMPARE(readRaw(root + u"/Archiwum/Dom.md"_s), QByteArray("- mój dom\n"));
        QCOMPARE(readRaw(root + u"/Archiwum/dom 2.md"_s), QByteArray("- importowany dom\n"));
        QVERIFY(QFileInfo::exists(root + u"/Archiwum/Ogród.md"_s));
        // (By name: on case-insensitive storage "archiwum" opens "Archiwum".)
        QVERIFY(!QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot).contains(u"archiwum"_s));
        // The preview counted the taken name; the folder spelling alone is no rename.
        QCOMPARE(c.report().value(u"renamed"_s).toInt(), 0);
        for (const QString &w : c.report().value(u"warnings"_s).toStringList())
            QVERIFY2(!w.contains(u"Ogród"_s), qPrintable(w));
    }

    void unrecognisedAndEmpty()
    {
        auto ws = open();
        ImportController c;
        c.setWorkspace(ws.get());
        c.setInput({ InputFile{ u"dane.bin"_s, QByteArray("\x00\x01\x02", 3) } }, u"dane.bin"_s);
        QVERIFY(waitState(c, u"error"_s));
        QVERIFY(c.detectedId().isEmpty());
        QVERIFY(!c.errorText().isEmpty());
        // Choosing the app by hand gets past the detection; an empty Simplenote export imports nothing.
        c.setInput({ text(u"notes.json"_s, u"{\"activeNotes\": []}"_s) }, u"notes.json"_s);
        QVERIFY(waitState(c, u"error"_s));
        QCOMPARE(c.detectedId(), u"simplenote"_s);
        QVERIFY(c.errorText().contains(u"notes.json"_s));
        // No notebook.
        ImportController lonely;
        lonely.setInput({ text(u"a.md"_s, u"x"_s) }, u"a.md"_s);
        QCOMPARE(lonely.state(), u"error"_s);
        // clear() goes back to the start.
        c.clear();
        QCOMPARE(c.state(), u"empty"_s);
        QCOMPARE(c.inputCount(), 0);
        QVERIFY(c.preview().isEmpty());
    }

    void readFolderThroughController()
    {
        const QString dir = m_tmp.filePath(u"Eksport Simplenote"_s);
        writeRaw(dir + u"/source/notes.json"_s,
                 R"({"activeNotes":[{"id":"1","content":"Zakupy\nmleko, jaja","creationDate":"2026-10-01T10:00:00.000Z","lastModified":"2026-10-02T10:00:00.000Z"}]})");
        auto ws = open();
        ImportController c;
        c.setWorkspace(ws.get());
        c.addFolder(QUrl::fromLocalFile(dir));
        QCOMPARE(c.state(), u"reading"_s);
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(c.sourceName(), u"Eksport Simplenote"_s);
        QCOMPARE(c.effectiveId(), u"simplenote"_s);
        QCOMPARE(c.preview().value(u"pages"_s).toInt(), 1);
        c.startImport();
        QVERIFY(waitState(c, u"done"_s));
        QVERIFY(ws->exists(u"Import/Simplenote/Zakupy.md"_s));
    }

    // ---- Android documents (content:// URIs) and typed paths ---------------------------------------

    void contentUrls()
    {
        using brulion::ioviews::displayNameOf;
        using brulion::ioviews::fileSystemPathOf;
        using brulion::ioviews::isDocumentUrl;
        using brulion::ioviews::urlFromUserInput;
        // As the system's picker encodes them: the document id must reach Android unchanged.
        const QUrl tree(kTree);
        QVERIFY(isDocumentUrl(tree));
        QCOMPARE(fileSystemPathOf(tree), kTree);
        QCOMPARE(fileSystemPathOf(QUrl(kTreeRoot)), kTreeRoot);
        QCOMPARE(displayNameOf(tree), u"Sejf"_s); // until the provider names it
        const QUrl download(u"content://com.android.providers.downloads.documents/document/msf%3A1000012345"_s);
        QCOMPARE(fileSystemPathOf(download), download.toString(QUrl::FullyEncoded));
        QCOMPARE(displayNameOf(download), u"1000012345"_s);
        QVERIFY(!isDocumentUrl(QUrl::fromLocalFile(u"/tmp/x.md"_s)));
        // Typed paths and URLs (the path field, route parameters).
        QCOMPARE(urlFromUserInput(u"  /storage/emulated/0/Download/Obsidian "_s),
                 QUrl::fromLocalFile(u"/storage/emulated/0/Download/Obsidian"_s));
        QCOMPARE(urlFromUserInput(u"/home/łucja/Zażółć.md"_s).toLocalFile(), u"/home/łucja/Zażółć.md"_s);
        QCOMPARE(urlFromUserInput(u"file:///tmp/Notatki"_s).toLocalFile(), u"/tmp/Notatki"_s);
        QVERIFY(isDocumentUrl(urlFromUserInput(kTree)));
        QCOMPARE(fileSystemPathOf(urlFromUserInput(kTree)), kTree);
        QVERIFY(urlFromUserInput(u"   "_s).isEmpty());
    }

    void gatherDocumentTree()
    {
        // A folder picked on Android: walked folder by folder through the provider (one listing
        // per folder entered; skipped folders are never listed), names as the provider gives them.
        auto docs = obsidianTree();
        brulion::ioviews::FileGatherer g;
        g.setDocumentProvider(docs);
        QSignalSpy finished(&g, &brulion::ioviews::FileGatherer::finished);
        g.start({QUrl(kTree)});
        QVERIFY(finished.wait(5000));
        const auto files = finished.first().at(0).value<QList<InputFile>>();
        QCOMPARE(pathsOf(files), QStringList({u"Sejf/.obsidian/app.json"_s, u"Sejf/Notatki/Zażółć gęślą jaźń.md"_s,
                                              u"Sejf/Notatki/Łódź.md"_s, u"Sejf/zdjęcie.png"_s}));
        QCOMPARE(finished.first().at(2).toString(), u"Sejf"_s);
        const QStringList warnings = finished.first().at(1).toStringList();
        QCOMPARE(warnings.size(), 1);
        QVERIFY2(warnings.first().contains(u"Plan podróży"_s), qPrintable(warnings.first()));
        QCOMPARE(docs->listings.load(), 3); // Sejf, .obsidian, Notatki — not .git
        QCOMPARE(docs->reads.load(), 4);
        for (const InputFile &f : files) {
            if (f.path.endsWith(u"jaźń.md"_s))
                QVERIFY(QString::fromUtf8(f.data).contains(u"[[Łódź]]"_s));
        }
        QVERIFY(brulion::io::detectImporter(files));
        QCOMPARE(brulion::io::detectImporter(files)->id(), u"obsidian"_s);
    }

    void gatherDocumentsWithLocalFiles()
    {
        // Several files picked at once (documents) plus a local file: same names stay apart,
        // an unreadable document and one bigger than the limit (its size unknown to the
        // provider, so found out by reading) give warnings, the rest is read.
        auto docs = std::make_shared<FakeDocuments>();
        const QString a = u"content://com.android.providers.downloads.documents/document/msf%3A11"_s;
        const QString b = u"content://com.android.providers.downloads.documents/document/msf%3A12"_s;
        const QString locked = u"content://com.google.android.apps.docs.storage/document/acc%3D1%3Bdoc%3D7"_s;
        const QString huge = u"content://com.android.providers.downloads.documents/document/msf%3A13"_s;
        docs->addDocument(a, u"Notatka.md"_s, "- z telefonu\n");
        docs->addDocument(b, u"Zakupy.txt"_s, "mleko\n");
        docs->addDocument(locked, u"Tajne.md"_s, "x");
        docs->nodes[locked].unreadable = true;
        docs->addDocument(huge, u"Wielki.md"_s, QByteArray(5000, 'a'));
        docs->nodes[huge].sizeUnknown = true;
        const QString local = m_tmp.filePath(u"lokalne/Notatka.md"_s);
        writeRaw(local, "- z komputera\n");
        brulion::ioviews::FileGatherer g;
        g.setDocumentProvider(docs);
        brulion::ioviews::GatherLimits limits;
        limits.maxFileBytes = 4000;
        g.setLimits(limits);
        QSignalSpy finished(&g, &brulion::ioviews::FileGatherer::finished);
        g.start({QUrl(a), QUrl(b), QUrl(locked), QUrl(huge), QUrl::fromLocalFile(local)});
        QVERIFY(finished.wait(5000));
        const auto files = finished.first().at(0).value<QList<InputFile>>();
        QCOMPARE(pathsOf(files), QStringList({u"Notatka 2.md"_s, u"Notatka.md"_s, u"Zakupy.txt"_s}));
        QCOMPARE(finished.first().at(2).toString(), u"5 file(s)"_s);
        const QStringList warnings = finished.first().at(1).toStringList();
        QCOMPARE(warnings.size(), 2);
        QVERIFY(warnings.join(u'\n').contains(u"Tajne.md"_s));
        QVERIFY(warnings.join(u'\n').contains(u"Wielki.md"_s));
        // A document that is gone (permission revoked, deleted) is a warning, not a crash.
        g.start({QUrl(u"content://com.android.providers.downloads.documents/document/msf%3A99"_s)});
        QVERIFY(finished.wait(5000));
        QVERIFY(finished.last().at(0).value<QList<InputFile>>().isEmpty());
        QCOMPARE(finished.last().at(1).toStringList().size(), 1);
    }

    void gatherListsOffTheGuiThread()
    {
        // A slow provider (Google Drive, an SD card): listing and reading never block the event
        // loop — on Android a blocked GUI thread is an "app not responding" dialog.
        auto docs = obsidianTree();
        docs->delayMs = 120;
        brulion::ioviews::FileGatherer g;
        g.setDocumentProvider(docs);
        QSignalSpy finished(&g, &brulion::ioviews::FileGatherer::finished);
        QSignalSpy progress(&g, &brulion::ioviews::FileGatherer::progress);
        int ticks = 0;
        QTimer ticker;
        ticker.setInterval(10);
        connect(&ticker, &QTimer::timeout, &g, [&ticks] { ++ticks; });
        ticker.start();
        QElapsedTimer started;
        started.start();
        g.start({QUrl(kTree)});
        QVERIFY2(started.elapsed() < 100, qPrintable(QString::number(started.elapsed())));
        QCOMPARE(progress.first().at(1).toInt(), 0); // "looking for the files"
        QVERIFY(finished.wait(10000 * BRULION_TEST_SLOWDOWN));
        QVERIFY2(ticks >= 10, qPrintable(QString::number(ticks))); // ≥ 360 ms of listing
        QCOMPARE(finished.first().at(0).value<QList<InputFile>>().size(), 4);
    }

    void gatherDocumentTreeCancelled()
    {
        auto docs = obsidianTree();
        docs->delayMs = 50;
        brulion::ioviews::FileGatherer g;
        g.setDocumentProvider(docs);
        QSignalSpy finished(&g, &brulion::ioviews::FileGatherer::finished);
        QSignalSpy cancelled(&g, &brulion::ioviews::FileGatherer::cancelled);
        g.start({QUrl(kTree)});
        QTest::qWait(20);
        g.cancel(); // while the listing runs on its thread
        QCOMPARE(cancelled.size(), 1);
        QTest::qWait(400);
        QCOMPARE(finished.size(), 0); // the listing's late result is dropped
        QVERIFY(!g.isRunning());
        // And a new pick right after works.
        docs->delayMs = 0;
        g.start({QUrl(kTree)});
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().at(0).value<QList<InputFile>>().size(), 4);
    }

    void importFolderPickedOnAndroid()
    {
        // FilePicker.OpenFolder on the phone → a tree URI → preview → import, through the controller.
        QString root;
        auto ws = open({}, &root);
        auto docs = obsidianTree();
        ImportController c;
        c.setWorkspace(ws.get());
        c.gatherer()->setDocumentProvider(docs);
        c.addFolder(QUrl(kTree));
        QCOMPARE(c.state(), u"reading"_s);
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(c.sourceName(), u"Sejf"_s);
        QCOMPARE(c.effectiveId(), u"obsidian"_s);
        QCOMPARE(c.preview().value(u"pages"_s).toInt(), 2);
        QCOMPARE(c.preview().value(u"attachments"_s).toInt(), 1);
        QVERIFY(c.readWarnings().join(u'\n').contains(u"Plan podróży"_s));
        QVERIFY(!QFileInfo::exists(root + u"/Import/Obsidian"_s)); // nothing written yet
        c.startImport();
        QVERIFY(waitState(c, u"done"_s));
        QCOMPARE(c.report().value(u"pages"_s).toInt(), 2);
        QVERIFY(ws->exists(u"Import/Obsidian/Notatki/Zażółć gęślą jaźń.md"_s));
        QVERIFY(ws->exists(u"Import/Obsidian/Notatki/Łódź.md"_s));
        QVERIFY(c.report().value(u"warnings"_s).toStringList().join(u'\n').contains(u"Plan podróży"_s));
        // "Otwórz zaimportowany folder": All pages in the folder.
        QSignalSpy navigation(ws.get(), &brulion::shell::Workspace::navigationRequested);
        c.openTarget();
        QCOMPARE(navigation.size(), 1);
        const QVariantMap route = navigation.first().at(0).toMap();
        QCOMPARE(route.value(u"view"_s).toString(), u"allPages"_s);
        QCOMPARE(route.value(u"path"_s).toString(), u"Import/Obsidian"_s);
    }

    void importZipPickedOnAndroid()
    {
        // A Logseq graph exported as a ZIP and picked from Downloads: the document's id is
        // opaque ("msf:42"), its name comes from the provider; the ZIP is unpacked.
        QString root;
        auto ws = open({}, &root);
        auto docs = std::make_shared<FakeDocuments>();
        const QString zip = u"content://com.android.providers.downloads.documents/document/msf%3A42"_s;
        docs->addDocument(zip, u"Logseq — kopia.zip"_s, brulion::io::writeZip(logseqGraph()));
        ImportController c;
        c.setWorkspace(ws.get());
        c.gatherer()->setDocumentProvider(docs);
        c.addUrls({QUrl(zip)});
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(c.sourceName(), u"Logseq — kopia.zip"_s);
        QCOMPARE(c.effectiveId(), u"logseq"_s);
        QCOMPARE(c.preview().value(u"pages"_s).toInt(), 2);
        QCOMPARE(c.preview().value(u"journals"_s).toInt(), 1);
        c.startImport();
        QVERIFY(waitState(c, u"done"_s));
        QVERIFY(ws->exists(u"Import/Logseq/Pomysły.md"_s));
        QVERIFY(QString::fromUtf8(readRaw(root + u"/journals/2026-10-14.md"_s)).contains(u"Łucją"_s));
    }

    void importSeveralLocalFilesAndPaths()
    {
        // Desktop FilePicker (file:// URLs, several at once) and the typed path field.
        const QString dir = m_tmp.filePath(u"pobrane"_s);
        writeRaw(dir + u"/Przepisy.md"_s, "# Przepisy\n\n- [[Żurek]]\n");
        writeRaw(dir + u"/Żurek.md"_s, "- zakwas, kiełbasa\n");
        writeRaw(dir + u"/Zakupy.txt"_s, "chleb\n");
        auto ws = open();
        ImportController c;
        c.setWorkspace(ws.get());
        c.addUrls({QUrl::fromLocalFile(dir + u"/Przepisy.md"_s), QUrl::fromLocalFile(dir + u"/Żurek.md"_s),
                   QUrl::fromLocalFile(dir + u"/Zakupy.txt"_s)});
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(c.inputCount(), 3);
        QCOMPARE(c.effectiveId(), u"markdown"_s);
        QCOMPARE(c.preview().value(u"pages"_s).toInt(), 3);
        // The typed path takes a folder, also as a file:// URL; a missing path says so.
        c.addPaths({u"file://"_s + QUrl::fromLocalFile(dir).path()});
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(c.sourceName(), u"pobrane"_s);
        QCOMPARE(c.inputCount(), 3);
        c.addPaths({m_tmp.filePath(u"nie ma mnie"_s)});
        QVERIFY(waitState(c, u"error"_s));
        QVERIFY2(c.errorText().contains(u"nie ma mnie"_s), qPrintable(c.errorText()));
        c.addPaths({dir});
        QVERIFY(waitState(c, u"ready"_s));
        c.startImport();
        QVERIFY(waitState(c, u"done"_s));
        QVERIFY(ws->exists(u"Import/Markdown/pobrane/Żurek.md"_s) || ws->exists(u"Import/Markdown/Żurek.md"_s));
    }

    void inlineBackgroundAndCancel()
    {
        // The single-threaded path (WebAssembly) gives the same results.
        brulion::ioviews::backgroundInline() = true;
        auto ws = open();
        ImportController c;
        c.setWorkspace(ws.get());
        c.setWriteChunk(1);
        QList<InputFile> many;
        for (int i = 0; i < 40; ++i)
            many.append(text(u"notatki/Strona %1.md"_s.arg(i), u"- treść %1 ąę\n"_s.arg(i)));
        c.setInput(many, u"notatki"_s);
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(c.preview().value(u"pages"_s).toInt(), 40);
        c.startImport();
        QTRY_VERIFY(c.state() == u"importing"_s && c.progress() > 0);
        c.cancel();
        QVERIFY(waitState(c, u"done"_s));
        const QVariantMap r = c.report();
        QVERIFY(r.value(u"cancelled"_s).toBool());
        QVERIFY(r.value(u"written"_s).toInt() < 40);
        QVERIFY(r.value(u"written"_s).toInt() >= 1);
        brulion::ioviews::backgroundInline() = false;
    }

    void closingTheVaultClears()
    {
        auto ws = open();
        ImportController c;
        c.setWorkspace(ws.get());
        c.setInput(logseqGraph(), u"graf"_s);
        QVERIFY(waitState(c, u"ready"_s));
        ws->close();
        QCOMPARE(c.state(), u"empty"_s);
        QCOMPARE(c.inputCount(), 0);
    }
};

QTEST_GUILESS_MAIN(TestAppIoImport)
#include "tst_appio_import.moc"
