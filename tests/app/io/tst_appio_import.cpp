// ImportController and its helpers: the daily-note collision policy (appended under "## Import
// (<źródło>)", line endings, BOM, frontmatter, importing the same thing twice), kinds, default and
// user-typed target folders, reading a picked folder (skipped folders, limits), recognising the
// source with an override, the dry-run preview, writing with unique names for taken paths, the
// report, cancelling and the error states — against real Workspaces over temporary vaults.
#include "fileexchange.h"
#include "importcontroller.h"
#include "iobackground.h"
#include "workspace.h"

#include "io/importer.h"
#include "vault/vault.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>

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
        QVERIFY(!QFileInfo::exists(root + u"/archiwum"_s));
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
