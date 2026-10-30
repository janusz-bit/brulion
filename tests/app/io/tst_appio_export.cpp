// ExportController and selectExportFiles(): what each scope exports (the whole notebook with its
// config, a folder with the pictures its pages link to from elsewhere, one page with its
// attachments), the summary and its errors, producing a Markdown ZIP / tasks CSV / single HTML in
// the background, saving to a picked file and to the export folder, and resetting on changes;
// pictures from the internet kept as addresses or included as copies (the device's cache, the
// missing ones fetched from a tiny HTTP server on 127.0.0.1, failures as warnings).
#include "exportcontroller.h"
#include "workspace.h"

#include "io/zip.h"
#include "vault/vault.h"
#include "webpicture/picturecache.h"

#include "../minihttpserver.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>

using namespace Qt::StringLiterals;
using brulion::FileStat;
using brulion::ioviews::ExportController;

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

FileStat file(const QString &path, qint64 size = 10)
{
    FileStat f;
    f.path = path;
    f.size = size;
    return f;
}

FileStat dir(const QString &path)
{
    FileStat f;
    f.path = path;
    f.isDir = true;
    return f;
}

QStringList zipPaths(const QByteArray &zip)
{
    QStringList out;
    for (const brulion::io::FileEntry &e : brulion::io::readZip(zip))
        out.append(e.path);
    out.sort();
    return out;
}

} // namespace

class TestAppIoExport : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;

    std::unique_ptr<brulion::shell::Workspace> open(const QHash<QString, QByteArray> &files, QString *rootOut = nullptr)
    {
        const QString root = m_tmp.filePath(u"Mój brulion %1"_s.arg(++m_n));
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            writeRaw(root + u'/' + it.key(), it.value());
        auto ws = std::make_unique<brulion::shell::Workspace>();
        ws->setClock([] { return QDateTime(QDate(2026, 10, 15), QTime(9, 30)); });
        ws->sync()->setSyncthingDiscovery(false);
        QString error;
        if (!ws->open(root, root + u"-state"_s, &error))
            qWarning() << error;
        ws->finishIndexing();
        if (rootOut)
            *rootOut = root;
        return ws;
    }

    static QHash<QString, QByteArray> sampleVault()
    {
        return {
            { u"journals/2026-10-14.md"_s, "- [ ] Zadzwonić do Łucji 📅 2026-10-16\n- spotkanie ⏰ 2026-10-15 18:00\n" },
            { u"Projekty/Remont.md"_s, "# Remont\n- [ ] kupić farbę\n- ![[assets/kuchnia.jpg]]\n- [[Zakupy]]\n" },
            { u"Projekty/Ogród/Grządki.md"_s, "- marchew, pietruszka\n" },
            { u"Zakupy.md"_s, "- mleko\n- ![[paragon.png]]\n" },
            { u"assets/kuchnia.jpg"_s, "JPEG" },
            { u"assets/paragon.png"_s, "PNG" },
            { u"assets/niepotrzebne.png"_s, "PNG2" },
            { u".brulion/config.json"_s, "{\"journalsFolder\": \"journals\"}\n" },
        };
    }

    static bool waitState(ExportController &c, const QString &state, int ms = 10000)
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
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QStandardPaths::setTestModeEnabled(true);
    }

    void selection_data()
    {
        QTest::addColumn<QString>("scope");
        QTest::addColumn<QString>("folder");
        QTest::addColumn<QString>("page");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("vault") << u"vault"_s << QString() << QString()
                               << QStringList{ u"Projekty/Ogród/Grządki.md"_s, u"Projekty/Remont.md"_s,
                                               u"assets/kuchnia.jpg"_s, u"assets/niepotrzebne.png"_s,
                                               u"assets/paragon.png"_s, u"Zakupy.md"_s };
        QTest::newRow("folder with linked picture")
                << u"folder"_s << u"Projekty"_s << QString()
                << QStringList{ u"Projekty/Ogród/Grządki.md"_s, u"Projekty/Remont.md"_s, u"assets/kuchnia.jpg"_s };
        QTest::newRow("subfolder") << u"folder"_s << u"Projekty/Ogród/"_s << QString()
                                   << QStringList{ u"Projekty/Ogród/Grządki.md"_s };
        QTest::newRow("page with picture") << u"page"_s << QString() << u"Zakupy.md"_s
                                           << QStringList{ u"assets/paragon.png"_s, u"Zakupy.md"_s };
        QTest::newRow("missing page") << u"page"_s << QString() << u"Nie ma.md"_s << QStringList{};
        QTest::newRow("folder prefix is not a folder")
                << u"folder"_s << u"Proj"_s << QString() << QStringList{};
    }

    void selection()
    {
        QFETCH(QString, scope);
        QFETCH(QString, folder);
        QFETCH(QString, page);
        QFETCH(QStringList, expected);
        const QList<FileStat> files{ dir(u"Projekty"_s), file(u"Projekty/Remont.md"_s, 100),
                                     dir(u"Projekty/Ogród"_s), file(u"Projekty/Ogród/Grządki.md"_s, 20),
                                     file(u"Zakupy.md"_s, 30), file(u"assets/kuchnia.jpg"_s, 1000),
                                     file(u"assets/paragon.png"_s, 500), file(u"assets/niepotrzebne.png"_s, 7) };
        const auto links = [](const QString &p) -> QList<QString> {
            if (p == u"Projekty/Remont.md")
                return { u"assets/kuchnia.jpg"_s, u"Zakupy.md"_s, QString() };
            if (p == u"Zakupy.md")
                return { u"assets/paragon.png"_s, u"assets/nie-ma.png"_s };
            return {};
        };
        const brulion::ioviews::ExportSelection s = brulion::ioviews::selectExportFiles(files, scope, folder, page, links);
        QStringList got = s.paths;
        got.sort();
        expected.sort();
        QCOMPARE(got, expected);
        int pages = 0;
        for (const QString &p : expected)
            pages += p.endsWith(u".md"_s) ? 1 : 0;
        QCOMPARE(s.pages, pages);
        QCOMPARE(s.attachments, int(expected.size()) - pages);
    }

    void formats()
    {
        ExportController c;
        const QVariantList formats = c.formats();
        QStringList ids;
        for (const QVariant &v : formats) {
            ids.append(v.toMap().value(u"id"_s).toString());
            QVERIFY(!v.toMap().value(u"description"_s).toString().isEmpty());
            QVERIFY(v.toMap().value(u"ext"_s).toString().startsWith(u'.'));
        }
        QCOMPARE(ids, QStringList({u"markdown-zip"_s, u"obsidian-zip"_s, u"logseq-zip"_s, u"html-site"_s,
                                   u"html-single"_s, u"pandoc"_s, u"json"_s, u"ics"_s, u"opml"_s, u"tasks-csv"_s}));
        c.setFormat(u"nie-ma"_s);
        QCOMPARE(c.format(), u"markdown-zip"_s);
        c.setScope(u"cokolwiek"_s);
        QCOMPARE(c.scope(), u"vault"_s);
        QCOMPARE(c.summary().value(u"valid"_s).toBool(), false);
    }

    void formatsFollowTheLanguage()
    {
        ExportController c;
        QSignalSpy formats(&c, &ExportController::formatsChanged);
        const QString description = c.formats().first().toMap().value(u"description"_s).toString();
        MarkingTranslator translation;
        QVERIFY(QCoreApplication::installTranslator(&translation));
        QCOMPARE(formats.size(), 1);
        QCOMPARE(c.formats().first().toMap().value(u"description"_s).toString(), u"[pl] "_s + description);
        QVERIFY(QCoreApplication::removeTranslator(&translation));
        QCOMPARE(formats.size(), 2);
        QCOMPARE(c.formats().first().toMap().value(u"description"_s).toString(), description);
    }

    void wholeVaultZip()
    {
        QString root;
        auto ws = open(sampleVault(), &root);
        ExportController c;
        c.setReadChunk(2);
        c.setWorkspace(ws.get());
        QVERIFY(c.summary().value(u"valid"_s).toBool());
        QCOMPARE(c.summary().value(u"pages"_s).toInt(), 4);
        QCOMPARE(c.summary().value(u"attachments"_s).toInt(), 3);
        QVERIFY(c.folders().contains(u"Projekty/Ogród"_s));
        QSignalSpy progress(&c, &ExportController::progressChanged);
        c.prepare();
        QCOMPARE(c.state(), u"preparing"_s);
        QVERIFY(waitState(c, u"ready"_s));
        QVERIFY(progress.size() >= 4);
        QVERIFY(c.fileName().endsWith(u".zip"_s));
        QVERIFY(!c.fileSizeText().isEmpty());
        const QStringList entries = zipPaths(c.result().first().data);
        QVERIFY2(entries.size() >= 7, qPrintable(entries.join(u", "_s)));
        bool hasJournal = false, hasConfig = false;
        for (const QString &e : entries) {
            hasJournal |= e.endsWith(u"journals/2026-10-14.md"_s);
            hasConfig |= e.endsWith(u".brulion/config.json"_s);
        }
        QVERIFY(hasJournal);
        QVERIFY(hasConfig);

        const QString target = m_tmp.filePath(u"eksport/kopia zapasowa.zip"_s);
        QSignalSpy saved(&c, &ExportController::saved);
        QVERIFY(c.saveTo(QUrl::fromLocalFile(target)));
        QCOMPARE(c.state(), u"saved"_s);
        QCOMPARE(saved.size(), 1);
        QVERIFY(QFileInfo(target).size() > 0);
        QCOMPARE(QDir::fromNativeSeparators(c.savedTo()), target);
        // Changing an option drops the produced file.
        c.setScope(u"page"_s);
        QCOMPARE(c.state(), u"idle"_s);
        QVERIFY(c.result().isEmpty());
    }

    void folderAndPage()
    {
        auto ws = open(sampleVault());
        ExportController c;
        c.setWorkspace(ws.get());
        c.setScope(u"folder"_s);
        QTRY_VERIFY(!c.summary().value(u"valid"_s).toBool());
        QVERIFY(!c.summary().value(u"error"_s).toString().isEmpty());
        c.setFolder(u"Projekty"_s);
        QTRY_COMPARE(c.summary().value(u"files"_s).toInt(), 3);
        QCOMPARE(c.summary().value(u"attachments"_s).toInt(), 1);
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        const QStringList entries = zipPaths(c.result().first().data);
        QVERIFY2(entries.size() == 3, qPrintable(entries.join(u", "_s)));
        // The file is named after the folder.
        QVERIFY2(c.fileName().startsWith(u"Projekty"_s), qPrintable(c.fileName()));

        c.setScope(u"page"_s);
        c.setPage(u"Zakupy.md"_s);
        c.setFormat(u"html-single"_s);
        QTRY_COMPARE(c.summary().value(u"files"_s).toInt(), 2);
        QCOMPARE(c.pageTitle(), u"Zakupy"_s);
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        QVERIFY(c.fileName().endsWith(u".html"_s));
        const QString html = QString::fromUtf8(c.result().first().data);
        QVERIFY(html.contains(u"mleko"_s));
        QVERIFY(!html.contains(u"marchew"_s));

        c.setPage(u"Nie ma.md"_s);
        QTRY_VERIFY(!c.summary().value(u"valid"_s).toBool());
        c.prepare();
        QCOMPARE(c.state(), u"error"_s);
    }

    void suggestedSaveFile()
    {
        // The save dialog's file (FilePicker.selectedFile; Android's "Save as" shows its name):
        // a file URL in defaultSaveFolder whose name is the export's — spaces, "%", Polish
        // letters and all.
        auto ws = open({{u"Plany 100% – żółw/Grządki.md"_s, "- marchew\n"}});
        ExportController c;
        c.setWorkspace(ws.get());
        QVERIFY(c.suggestedSaveFile().isEmpty());
        c.setScope(u"folder"_s);
        c.setFolder(u"Plany 100% – żółw"_s);
        QTRY_COMPARE(c.summary().value(u"files"_s).toInt(), 1);
        QSignalSpy state(&c, &ExportController::stateChanged);
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        QVERIFY2(c.fileName().startsWith(u"Plany 100% – żółw"_s) && c.fileName().endsWith(u".zip"_s),
                 qPrintable(c.fileName()));
        const QUrl url = c.suggestedSaveFile();
        QVERIFY(url.isLocalFile());
        QCOMPARE(url.fileName(), c.fileName());
        QCOMPARE(QFileInfo(url.toLocalFile()).absolutePath(), QDir(c.defaultSaveFolder().toLocalFile()).absolutePath());
        QVERIFY(!url.hasFragment() && !url.hasQuery());
        c.discard();
        QVERIFY(c.suggestedSaveFile().isEmpty());
        QVERIFY(state.size() >= 3); // its NOTIFY signal: preparing, ready, idle
    }

    void tasksCsvToExportFolder()
    {
        auto ws = open(sampleVault());
        ExportController c;
        c.setWorkspace(ws.get());
        c.setFormat(u"tasks-csv"_s);
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        const QString csv = QString::fromUtf8(c.result().first().data);
        QVERIFY2(csv.contains(u"Zadzwonić do Łucji"_s), qPrintable(csv));
        QVERIFY(csv.contains(u"kupić farbę"_s));
        QVERIFY(!c.exportFolder().isEmpty());
        QVERIFY(c.saveToExportFolder());
        const QString first = QDir::fromNativeSeparators(c.savedTo());
        QVERIFY(QFileInfo::exists(first));
        // A second save never overwrites the first.
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        QVERIFY(c.saveToExportFolder());
        QVERIFY(QDir::fromNativeSeparators(c.savedTo()) != first);
        QVERIFY(QFileInfo::exists(QDir::fromNativeSeparators(c.savedTo())));
        QFile::remove(first);
        QFile::remove(QDir::fromNativeSeparators(c.savedTo()));
        // Saving into a folder that cannot exist (below a file) fails with a message.
        const QString blocker = m_tmp.filePath(u"plik, nie folder"_s);
        QFile file(blocker);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        QVERIFY(!c.saveTo(QUrl::fromLocalFile(blocker + u"/nie/wolno.csv"_s)));
        QCOMPARE(c.state(), u"error"_s);
        QVERIFY(!c.errorText().isEmpty());
        QVERIFY(!c.saveInBrowser()); // desktop build
    }

    void webPictures()
    {
        const QByteArray png = QByteArray::fromBase64(
                "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mNgAAIAAAUAAen63NgAAAAASUVORK5CYII=");
        const QByteArray gif = QByteArray("GIF89a\x01\x00\x01\x00\x00\x00\x00;", 14);
        MiniHttpServer server;
        server.bodies.insert("/kot.png", png);
        const QString kot = server.url(u"/kot.png"_s);
        const QString brak = server.url(u"/brak.png"_s);
        const QString cached = u"https://example.invalid/%C5%82%C4%85ka.gif"_s;
        const QString page = u"# Zwierzęta\n- ![Mój kot](%1)\n- ![[%2]]\n- ![łąka](%3)\n"_s.arg(kot, brak, cached);
        auto ws = open({{u"Zwierzęta.md"_s, page.toUtf8()}});
        const QString cacheDir = m_tmp.filePath(u"cache-%1"_s.arg(m_n));
        ws->setWebPictureCacheDir(cacheDir);
        ws->setWebPictureHostCheck([](const QString &) { return false; }); // 127.0.0.1 as the internet
        brulion::webpicture::PictureCache cache(cacheDir);
        QVERIFY(!cache.store(QUrl(cached), gif, u"gif"_s).isEmpty());

        ExportController c;
        c.setWorkspace(ws.get());
        QCOMPARE(c.webPictures(), u"link"_s);
        c.setFormat(u"markdown-zip"_s);
        QVERIFY(!c.webPicturesApply());
        c.setFormat(u"html-single"_s);
        QVERIFY(c.webPicturesApply());

        // "Zostaw adresy": nothing fetched, the URLs as written.
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        QString html = QString::fromUtf8(c.result().first().data);
        QVERIFY2(html.contains(u"<img src=\""_s + kot + u'"'), qPrintable(html));
        QVERIFY(html.contains(u"<img src=\""_s + cached + u'"'));
        QVERIFY(server.requests.isEmpty());

        // "Dołącz kopie": the cached one as it is, the missing ones fetched, a failure keeps its
        // address with a warning.
        c.setWebPictures(u"include"_s);
        QCOMPARE(c.state(), u"idle"_s);
        QStringList texts;
        connect(&c, &ExportController::progressChanged, &c, [&] { texts.append(c.progressText()); });
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        QVERIFY2(texts.contains(u"Fetching pictures from the internet (1 of 2)…"_s), qPrintable(texts.join(u" | "_s)));
        html = QString::fromUtf8(c.result().first().data);
        QVERIFY2(html.contains(u"src=\"data:image/png;base64,"_s + QString::fromLatin1(png.toBase64()) + u'"'),
                 qPrintable(html));
        QVERIFY(html.contains(u"src=\"data:image/gif;base64,"_s + QString::fromLatin1(gif.toBase64()) + u'"'));
        QVERIFY(html.contains(u"<img src=\""_s + brak + u'"'));
        QCOMPARE(server.requests.size(), 2);
        QCOMPARE(c.warnings().size(), 1);
        QVERIFY2(c.warnings().first().contains(u"127.0.0.1"_s) && c.warnings().first().contains(u"404"_s),
                 qPrintable(c.warnings().first()));

        // Pandoc: the copies are files next to the document; now all from the cache.
        c.setFormat(u"pandoc"_s);
        QCOMPARE(c.webPictures(), u"include"_s);
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        QStringList entries = zipPaths(c.result().first().data);
        QVERIFY2(entries.join(u'\n').contains(u"/assets/web/kot.png"_s), qPrintable(entries.join(u", "_s)));
        QVERIFY2(entries.join(u'\n').contains(u"/assets/web/łąka.gif"_s), qPrintable(entries.join(u", "_s)));
        QCOMPARE(server.requests.size(), 3); // only the missing one asked again

        // Other formats never fetch: the notes' text is exported as written.
        c.setFormat(u"markdown-zip"_s);
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(server.requests.size(), 3);
        entries = zipPaths(c.result().first().data);
        QVERIFY(!entries.join(u'\n').contains(u"assets/web"_s));
    }

    void webPicturesCancelWhileFetching()
    {
        MiniHttpServer server;
        server.stalled.insert("/wolny.png");
        const QString wolny = server.url(u"/wolny.png"_s);
        auto ws = open({{u"Strona.md"_s, u"![](%1)\n"_s.arg(wolny).toUtf8()}});
        ws->setWebPictureCacheDir(m_tmp.filePath(u"cache-%1"_s.arg(m_n)));
        ws->setWebPictureHostCheck([](const QString &) { return false; });
        ExportController c;
        c.setWorkspace(ws.get());
        c.setFormat(u"html-site"_s);
        c.setWebPictures(u"include"_s);
        c.prepare();
        QTRY_COMPARE(server.requests.size(), 1);
        QCOMPARE(c.state(), u"preparing"_s);
        QVERIFY(c.progressText().contains(u"1"_s));
        c.cancel();
        QCOMPARE(c.state(), u"idle"_s);
        QTest::qWait(50);
        QCOMPARE(c.state(), u"idle"_s);
        // The next export is not stuck on the old fetch.
        c.setWebPictures(u"link"_s);
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
    }

    // A note (synced, imported) naming a picture on this device or its network: the export never
    // fetches it on its own — only a copy the user already asked for (cached) is included.
    void webPicturesOfPrivateHostsAreNotFetched()
    {
        const QByteArray gif = QByteArray("GIF89a\x01\x00\x01\x00\x00\x00\x00;", 14);
        MiniHttpServer server;
        server.bodies.insert("/router.png", gif);
        const QString router = server.url(u"/router.png"_s);
        const QString shown = u"http://nas.local/zdj%C4%99cie.gif"_s;
        const QString lan = u"http://192.168.1.1/cgi-bin/reboot?x.png"_s;
        auto ws = open({{u"Sieć.md"_s, u"- ![](%1)\n- ![](%2)\n- ![[%3]]\n"_s.arg(router, shown, lan).toUtf8()}});
        const QString cacheDir = m_tmp.filePath(u"cache-%1"_s.arg(m_n));
        ws->setWebPictureCacheDir(cacheDir);
        brulion::webpicture::PictureCache cache(cacheDir);
        QVERIFY(!cache.store(QUrl(shown), gif, u"gif"_s).isEmpty());
        ExportController c;
        c.setWorkspace(ws.get());
        c.setFormat(u"html-single"_s);
        c.setWebPictures(u"include"_s);
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        QTest::qWait(50);
        QVERIFY(server.requests.isEmpty());
        const QString html = QString::fromUtf8(c.result().first().data);
        QVERIFY2(html.contains(u"<img src=\""_s + router + u'"'), qPrintable(html));
        QVERIFY(html.contains(u"src=\"data:image/gif;base64,"_s + QString::fromLatin1(gif.toBase64()) + u'"'));
        QCOMPARE(c.warnings().size(), 1);
        QVERIFY2(c.warnings().first().contains(u"127.0.0.1, 192.168.1.1"_s), qPrintable(c.warnings().first()));
    }

    // The copies are held in memory until the export is made: past the limit the rest keep
    // their addresses (one warning with the count), and nothing more is fetched.
    void webPicturesLimit()
    {
        const QByteArray gif = QByteArray("GIF89a\x01\x00\x01\x00\x00\x00\x00;", 14);
        MiniHttpServer server;
        server.bodies.insert("/trzy.gif", gif);
        const QString raz = u"https://example.invalid/raz.gif"_s;
        const QString dwa = u"https://example.invalid/dwa.gif"_s;
        const QString trzy = server.url(u"/trzy.gif"_s);
        auto ws = open({{u"Żółw.md"_s, u"![](%1) ![](%2) ![](%3)\n"_s.arg(raz, dwa, trzy).toUtf8()}});
        const QString cacheDir = m_tmp.filePath(u"cache-%1"_s.arg(m_n));
        ws->setWebPictureCacheDir(cacheDir);
        ws->setWebPictureHostCheck([](const QString &) { return false; });
        brulion::webpicture::PictureCache cache(cacheDir);
        QVERIFY(!cache.store(QUrl(raz), gif, u"gif"_s).isEmpty());
        QVERIFY(!cache.store(QUrl(dwa), gif, u"gif"_s).isEmpty());
        ExportController c;
        c.setReadChunk(1); // the cache read in several turns
        c.setWebCopiesLimit(gif.size());
        c.setWorkspace(ws.get());
        c.setFormat(u"html-single"_s);
        c.setWebPictures(u"include"_s);
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        QVERIFY(server.requests.isEmpty());
        const QString html = QString::fromUtf8(c.result().first().data);
        QCOMPARE(html.count(u"src=\"data:image/gif;base64,"_s), 1);
        QCOMPARE(c.warnings().size(), 1);
        QVERIFY2(c.warnings().first().startsWith(u"2 picture(s)"_s), qPrintable(c.warnings().first()));
    }

    void cancelWhileReading()
    {
        QHash<QString, QByteArray> files;
        for (int i = 0; i < 200; ++i)
            files.insert(u"Notatki/Strona %1.md"_s.arg(i), "- zażółć\n");
        auto ws = open(files);
        ExportController c;
        c.setReadChunk(5);
        c.setWorkspace(ws.get());
        c.prepare();
        QTRY_VERIFY(c.progress() > 0);
        c.cancel();
        QCOMPARE(c.state(), u"idle"_s);
        QTest::qWait(100);
        QCOMPARE(c.state(), u"idle"_s);
        QVERIFY(c.result().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestAppIoExport)
#include "tst_appio_export.moc"
