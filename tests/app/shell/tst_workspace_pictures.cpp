// Pictures from the internet on the real Workspace (workspacepictures.cpp) with the block editor:
// nothing is fetched until allowed, "Pokaż" and the "always" setting fetch into the device's
// cache, failures are told, "Pobierz do notatnika" stores an attachment in the vault and points
// the page at it; without asking: no private hosts, cancelled when turned off, once a session;
// the browser's data: URLs follow the cache — against a tiny HTTP server on 127.0.0.1.
#include "pagecontroller.h"
#include "workspace.h"

#include "blocklistmodel.h"
#include "drawing/drawing.h"
#include "drawing/svgwrite.h"

#include "../minihttpserver.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QSaveFile>
#include <QSet>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <functional>

using namespace Qt::StringLiterals;
using brulion::editor::PageController;
using brulion::shell::Workspace;

namespace {

QByteArray pngBytes()
{
    return QByteArray::fromBase64(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mNgAAIAAAUAAen63NgAAAAASUVORK5CYII=");
}

bool writeFile(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}

/// A `width` × 1 PNG.
QByteArray pngOfWidth(int width)
{
    QImage image(width, 1, QImage::Format_RGB32);
    image.fill(Qt::red);
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return buffer.data();
}

/// The only file of the folder `dir` made a minute old (prune() drops it first), "" when there
/// is not exactly one.
QString ageOnlyFile(const QString &dir)
{
    const QStringList files = QDir(dir).entryList(QDir::Files);
    if (files.size() != 1)
        return {};
    QFile f(QDir(dir).filePath(files.first()));
    const QDateTime before = QDateTime::currentDateTime().addSecs(-60);
    if (!f.open(QIODevice::ReadWrite) || !f.setFileTime(before, QFileDevice::FileModificationTime)
        || !f.setFileTime(before, QFileDevice::FileAccessTime))
        return {};
    return f.fileName();
}

/// Treats every host as one of the internet (the server is on 127.0.0.1, a private host).
bool noPrivateHosts(const QString &)
{
    return false;
}

QVariantMap embedOf(PageController &page, int row)
{
    auto *model = page.blocks();
    const int role = model->roleNames().key("embeds");
    return model->data(model->index(row), role).toList().value(0).toMap();
}

} // namespace

class TestWorkspacePictures : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;

private Q_SLOTS:
    void showsOnlyWhenAllowedAndStoresInTheVault()
    {
        MiniHttpServer server;
        server.bodies.insert("/zdj%C4%99cia/kot.png", pngBytes());
        server.bodies.insert("/pies.png", pngBytes());
        const QString kot = server.url(u"/zdj%C4%99cia/kot.png"_s);
        const QString pies = server.url(u"/pies.png"_s);
        const QString brak = server.url(u"/brak.png"_s);
        const QString root = m_tmp.filePath(u"Notatki"_s);
        const QString note = u"- ![Mój kot](%1)\n- ![](%2)\n- ![](%3)\n"_s.arg(kot, pies, brak);
        QVERIFY(writeFile(root + u"/Zwierzęta.md"_s, note.toUtf8()));

        Workspace ws;
        ws.setWebPictureCacheDir(m_tmp.filePath(u"cache"_s));
        QVERIFY(ws.open(root, m_tmp.filePath(u"state"_s)));
        ws.finishIndexing();
        PageController page;
        page.setSaveDelay(0);
        page.setHost(&ws);
        page.setPath(u"Zwierzęta.md"_s);

        // Shown: nothing fetched.
        QCOMPARE(embedOf(page, 0).value(u"state"_s).toString(), u"ask"_s);
        QCOMPARE(embedOf(page, 1).value(u"state"_s).toString(), u"ask"_s);
        QTest::qWait(100);
        QVERIFY(server.requests.isEmpty());

        // "Pokaż" on one picture: fetched into the cache, a file: URL.
        page.showWebPicture(kot);
        QTRY_COMPARE(embedOf(page, 0).value(u"state"_s).toString(), u"ready"_s);
        const QUrl cached = embedOf(page, 0).value(u"url"_s).toUrl();
        QVERIFY(cached.isLocalFile());
        QVERIFY(cached.toLocalFile().startsWith(m_tmp.filePath(u"cache"_s)));
        QCOMPARE(embedOf(page, 0).value(u"width"_s).toInt(), 1);
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(embedOf(page, 1).value(u"state"_s).toString(), u"ask"_s);

        // The "always" setting fetches nothing from this device or its network (127.0.0.1)...
        ws.setWebPicturesShown(true);
        QCOMPARE(embedOf(page, 1).value(u"state"_s).toString(), u"ask"_s);
        QTest::qWait(100);
        QCOMPARE(server.requests.size(), 1);
        // ...but the others from the internet; a missing one fails with a sentence.
        ws.setWebPictureHostCheck(noPrivateHosts);
        page.blocks()->webPictureChanged(QString());
        QTRY_COMPARE(embedOf(page, 1).value(u"state"_s).toString(), u"ready"_s);
        QTRY_COMPARE(embedOf(page, 2).value(u"state"_s).toString(), u"failed"_s);
        QVERIFY(embedOf(page, 2).value(u"error"_s).toString().contains(u"404"_s));
        const int requests = int(server.requests.size());
        QCOMPARE(requests, 3);
        // Failed once: not asked again on every repaint.
        page.blocks()->webPictureChanged(QString());
        QCOMPARE(embedOf(page, 2).value(u"state"_s).toString(), u"failed"_s);
        QTest::qWait(100);
        QCOMPARE(server.requests.size(), requests);
        ws.setWebPicturesShown(false);

        // "Pobierz do notatnika": an attachment of the vault, the page points at it.
        QSignalSpy done(&page, &PageController::webPicturesDownloaded);
        QCOMPARE(page.downloadWebPictures(kot), 1);
        QCOMPARE(done.size(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), 1);
        QVERIFY(page.text().startsWith(u"- ![Mój kot](assets/kot.png)\n"_s));
        QFile asset(root + u"/assets/kot.png"_s);
        QVERIFY(asset.open(QIODevice::ReadOnly));
        QCOMPARE(asset.readAll(), pngBytes());
        QCOMPARE(embedOf(page, 0).value(u"web"_s).toBool(), false);
        QVERIFY(embedOf(page, 0).value(u"url"_s).toUrl().isLocalFile());
        QCOMPARE(server.requests.size(), requests); // from the cache
        QCOMPARE(ws.webPicture(pies).value(u"format"_s).toString(), u"png"_s);

        // The browser build: data: URLs, whose format the cache tells — so the picture shows.
        ws.setDataUrlAssets(true);
        page.blocks()->webPictureChanged(QString());
        QCOMPARE(embedOf(page, 1).value(u"url"_s).toUrl().scheme(), u"data"_s);
        QCOMPARE(embedOf(page, 1).value(u"format"_s).toString(), u"PNG"_s);
        QCOMPARE(embedOf(page, 1).value(u"decodable"_s).toBool(), true);
        QCOMPARE(embedOf(page, 1).value(u"width"_s).toInt(), 1);
        ws.setDataUrlAssets(false);

        // A new session finds the cached pictures (the device's cache, not the notebook's).
        Workspace other;
        other.setWebPictureCacheDir(m_tmp.filePath(u"cache"_s));
        QCOMPARE(other.webPicture(pies).value(u"state"_s).toString(), u"ready"_s);
        QCOMPARE(other.webPicture(brak).value(u"state"_s).toString(), u"ask"_s);
        QString format;
        QCOMPARE(other.webPictureData(pies, &format), pngBytes());
        QCOMPARE(format, u"png"_s);
        QCOMPARE(other.webPicture(u"ftp://example.com/a.png"_s).value(u"state"_s).toString(), u"failed"_s);
    }

    void drawingsAreSavedAndShownAgain()
    {
        // A drawing (an SVG with an embedded scene) on the real Workspace: known as a drawing, read
        // and written as bytes, its URL carrying its version (Qt's pixmap cache keys by the URL:
        // the embed shows the drawing saved again), the page's embed computed again after a save.
        const QString root = m_tmp.filePath(u"Rysunki"_s);
        brulion::drawing::Drawing d;
        QVERIFY(writeFile(root + u"/assets/Rysunek — wykład z łączności ćw. 3.svg"_s, brulion::drawing::toSvg(d)));
        QVERIFY(writeFile(root + u"/assets/logo.svg"_s, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\"/>"));
        QVERIFY(writeFile(root + u"/Wykład.md"_s, "- ![[Rysunek — wykład z łączności ćw. 3.svg]] ![[logo.svg]]\n"));
        Workspace ws;
        QVERIFY(ws.open(root, m_tmp.filePath(u"state-drawings"_s)));
        ws.finishIndexing();
        const QString path = u"assets/Rysunek — wykład z łączności ćw. 3.svg"_s;
        QVERIFY(ws.isDrawing(path));
        QVERIFY(!ws.isDrawing(u"assets/logo.svg"_s));
        QVERIFY(!ws.isDrawing(u"Wykład.md"_s));
        QVERIFY(!ws.isDrawing(u"assets/brak.svg"_s));
        const QUrl before = ws.assetUrl(path);
        QVERIFY(before.isLocalFile());
        QVERIFY(before.query().startsWith(u"v="_s));
        QCOMPARE(QFileInfo(before.toLocalFile()).fileName(), u"Rysunek — wykład z łączności ćw. 3.svg"_s);
        QVERIFY(!ws.assetUrl(u"assets/logo.svg"_s).hasQuery()); // other pictures as they were
        // The previews: the drawing image provider, the file by its absolute path, the version and
        // the app's theme in the query.
        const QUrl light = ws.drawingUrl(path, false);
        const QUrl dark = ws.drawingUrl(path, true);
        QCOMPARE(light.scheme(), u"image"_s);
        QCOMPARE(light.host(), u"drawing"_s);
        QCOMPARE(QUrl::fromPercentEncoding(light.path(QUrl::FullyEncoded).mid(1).toLatin1()), before.toLocalFile());
        QCOMPARE(QUrlQuery(light).queryItemValue(u"v"_s), QUrlQuery(before).queryItemValue(u"v"_s));
        QCOMPARE(QUrlQuery(light).queryItemValue(u"dark"_s), u"0"_s);
        QCOMPARE(QUrlQuery(dark).queryItemValue(u"dark"_s), u"1"_s);
        QVERIFY(!light.path(QUrl::FullyEncoded).mid(1).contains(u'/')); // one segment: the path's slashes escaped
        QVERIFY(ws.drawingUrl(QString(), false).isEmpty());
        QCOMPARE(ws.readAttachment(path), brulion::drawing::toSvg(d));
        QVERIFY(ws.readAttachment(u"assets/brak.svg"_s).isEmpty());

        PageController page;
        page.setSaveDelay(0);
        page.setHost(&ws);
        page.setPath(u"Wykład.md"_s);
        QVERIFY(embedOf(page, 0).value(u"drawing"_s).toBool());
        QCOMPARE(embedOf(page, 0).value(u"width"_s).toInt(), brulion::drawing::EmptyWidth);

        // Saved again (the drawing editor): a new URL, the embed follows.
        brulion::drawing::Stroke s;
        s.id = u"kreska"_s;
        s.points = {{0, 0, 0.5}, {600, 100, 0.5}};
        d.strokes = {s};
        QSignalSpy changed(&ws, &Workspace::fileChanged);
        QTest::qWait(20); // a later modification time
        QVERIFY(ws.writeAttachment(path, brulion::drawing::toSvg(d)));
        QVERIFY(changed.size() >= 1);
        QCOMPARE(changed.first().first().toString(), path);
        QVERIFY(ws.assetUrl(path) != before);
        QCOMPARE(ws.readAttachment(path), brulion::drawing::toSvg(d));
        QTRY_VERIFY(embedOf(page, 0).value(u"width"_s).toInt() > 600);
        QCOMPARE(embedOf(page, 0).value(u"url"_s).toUrl(), ws.drawingUrl(path, false));
        QCOMPARE(embedOf(page, 0).value(u"darkUrl"_s).toUrl(), ws.drawingUrl(path, true));
        QVERIFY(ws.drawingUrl(path, false) != light);
        // Nothing to write, or nowhere: refused.
        QVERIFY(!ws.writeAttachment(path, QByteArray()));
        QVERIFY(!ws.writeAttachment(QString(), "x"));
        // Deleted on the disk before the vault noticed (Syncthing, a file manager): the drawing
        // editor's next save never writes it again, and the vault reports the deletion.
        QSignalSpy removed(&ws, &Workspace::pathRemoved);
        const QString disk = ws.assetUrl(path).toLocalFile();
        QVERIFY(QFile::remove(disk));
        QVERIFY(ws.exists(path));
        QVERIFY(!ws.writeAttachment(path, brulion::drawing::toSvg(d)));
        QVERIFY(!QFileInfo::exists(disk));
        QTRY_VERIFY(removed.contains(QVariantList{path}));
        QVERIFY(!QFileInfo::exists(disk));
    }

    void automaticFetchesLimits();
    void browserDataUrlsFollowTheCache();
    void browserNetworkErrorsNameTheBrowsersLimits_data();
    void browserNetworkErrorsNameTheBrowsersLimits();
};

void TestWorkspacePictures::automaticFetchesLimits()
{
    MiniHttpServer server;
    server.bodies.insert("/a.png", pngBytes());
    server.bodies.insert("/b.png", pngBytes());
    QString note = u"- ![](%1)\n- ![](%2)\n"_s.arg(server.url(u"/a.png"_s),
                                                   server.url(u"/a.png"_s).replace(u"http:"_s, u"HTTP:"_s));
    QStringList slow;
    for (int i = 0; i < 5; ++i) {
        server.stalled.insert("/wolny-" + QByteArray::number(i) + ".png");
        slow.append(server.url(u"/wolny-%1.png"_s.arg(i)));
        note += u"- ![Żółw %1](%2)\n"_s.arg(i).arg(slow.last());
    }
    const QString b = server.url(u"/b.png"_s);
    note += u"- ![](%1)\n"_s.arg(b);
    const QString root = m_tmp.filePath(u"Automat"_s);
    QVERIFY(writeFile(root + u"/Strona.md"_s, note.toUtf8()));
    Workspace ws;
    ws.setWebPictureCacheDir(m_tmp.filePath(u"cache-automat"_s));
    ws.setWebPictureHostCheck(noPrivateHosts);
    QVERIFY(ws.open(root, m_tmp.filePath(u"state-automat"_s)));
    ws.finishIndexing();
    PageController page;
    page.setSaveDelay(0);
    page.setHost(&ws);
    page.setPath(u"Strona.md"_s);
    ws.setWebPicturesShown(true);

    // Two spellings of one URL: one request, both told when it ends.
    QCOMPARE(embedOf(page, 0).value(u"state"_s).toString(), u"loading"_s);
    QCOMPARE(embedOf(page, 1).value(u"state"_s).toString(), u"loading"_s);
    QTRY_COMPARE(embedOf(page, 0).value(u"state"_s).toString(), u"ready"_s);
    QTRY_COMPARE(embedOf(page, 1).value(u"state"_s).toString(), u"ready"_s);
    QCOMPARE(server.requests.count("/a.png"), 1);

    // Turned off while fetching: what it started stops, "Pokaż" goes on.
    for (int row = 2; row < 7; ++row)
        QCOMPARE(embedOf(page, row).value(u"state"_s).toString(), u"loading"_s);
    QCOMPARE(embedOf(page, 7).value(u"state"_s).toString(), u"loading"_s); // queued behind them
    page.showWebPicture(b);
    QTRY_COMPARE(server.requests.size(), 1 + 4); // 4 at once
    ws.setWebPicturesShown(false);
    for (int row = 2; row < 7; ++row)
        QCOMPARE(embedOf(page, row).value(u"state"_s).toString(), u"ask"_s);
    QTRY_COMPARE(embedOf(page, 7).value(u"state"_s).toString(), u"ready"_s);
    QTest::qWait(100);
    QVERIFY(!server.requests.contains("/wolny-4.png")); // the queued one never sent
    QCOMPARE(server.requests.size(), 1 + 4 + 1);

    // Fetched without asking and dropped by the cache since: waits for "Pokaż" (no new request
    // on every repaint).
    const QString cached = embedOf(page, 0).value(u"url"_s).toUrl().toLocalFile();
    QVERIFY(QFile::remove(cached));
    ws.setWebPicturesShown(true);
    QCOMPARE(embedOf(page, 0).value(u"state"_s).toString(), u"ask"_s);
    QTest::qWait(100);
    QCOMPARE(server.requests.count("/a.png"), 1);
    page.showWebPicture(server.url(u"/a.png"_s));
    QTRY_COMPARE(embedOf(page, 0).value(u"state"_s).toString(), u"ready"_s);
    QCOMPARE(server.requests.count("/a.png"), 2);
    ws.setWebPicturesShown(false);
}

void TestWorkspacePictures::browserDataUrlsFollowTheCache()
{
    MiniHttpServer server;
    server.bodies.insert("/jeden.png", pngOfWidth(1));
    server.bodies.insert("/dwa.png", pngOfWidth(1));
    const QString jeden = server.url(u"/jeden.png"_s);
    const QString dwa = server.url(u"/dwa.png"_s);
    const QString root = m_tmp.filePath(u"Przeglądarka"_s);
    QVERIFY(writeFile(root + u"/Strona.md"_s, u"- ![](%1)\n- ![](%2)\n"_s.arg(jeden, dwa).toUtf8()));
    const QString cacheDir = m_tmp.filePath(u"cache-web"_s);
    Workspace ws;
    ws.setWebPictureCacheDir(cacheDir);
    // Room for one picture only.
    ws.setWebPictureCacheBytes(std::max(pngOfWidth(1).size(), pngOfWidth(3).size()) + 10);
    QVERIFY(ws.open(root, m_tmp.filePath(u"state-web"_s)));
    ws.finishIndexing();
    ws.setDataUrlAssets(true);
    PageController page;
    page.setSaveDelay(0);
    page.setHost(&ws);
    page.setPath(u"Strona.md"_s);

    page.showWebPicture(jeden);
    QTRY_COMPARE(embedOf(page, 0).value(u"state"_s).toString(), u"ready"_s);
    const QUrl first = embedOf(page, 0).value(u"url"_s).toUrl();
    QCOMPARE(first.scheme(), u"data"_s);
    QCOMPARE(embedOf(page, 0).value(u"width"_s).toInt(), 1);
    QVERIFY(!ageOnlyFile(cacheDir).isEmpty());
    // The second picture pushes the first out of the cache — and its data: URL out of memory.
    page.showWebPicture(dwa);
    QTRY_COMPARE(embedOf(page, 1).value(u"state"_s).toString(), u"ready"_s);
    QVERIFY(!ageOnlyFile(cacheDir).isEmpty()); // only the second's file is left
    // The first fetched again, now another picture: shown as it is now, not as it was. Its row
    // is not read until the fetch ends (a read while it is missing would forget it as well).
    server.bodies.insert("/jeden.png", pngOfWidth(3));
    QSignalSpy changed(&ws, &Workspace::webPictureChanged);
    page.showWebPicture(jeden);
    changed.clear(); // "loading"
    QTRY_COMPARE(changed.size(), 1);
    QCOMPARE(changed.at(0).at(0).toString(), jeden);
    QCOMPARE(server.requests.count("/jeden.png"), 2);
    QCOMPARE(embedOf(page, 0).value(u"state"_s).toString(), u"ready"_s);
    QVERIFY(embedOf(page, 0).value(u"url"_s).toUrl() != first);
    QCOMPARE(embedOf(page, 0).value(u"width"_s).toInt(), 3);
}

void TestWorkspacePictures::browserNetworkErrorsNameTheBrowsersLimits_data()
{
    QTest::addColumn<QString>("pageScheme");
    QTest::addColumn<QString>("expected");
    QTest::newRow("the app") << QString() << u"No connection to 127.0.0.1."_s;
    QTest::newRow("a page over http: CORS") << u"http"_s << u"(CORS)"_s;
    QTest::newRow("a page over https: mixed content") << u"https"_s << u"over http:// into a page opened over https://"_s;
}

void TestWorkspacePictures::browserNetworkErrorsNameTheBrowsersLimits()
{
    // A browser tells a page no reason for a failed request: Brulion names what it forbids.
    QFETCH(QString, pageScheme);
    QFETCH(QString, expected);
    quint16 port = 0;
    {
        QTcpServer closed;
        QVERIFY(closed.listen(QHostAddress::LocalHost));
        port = closed.serverPort();
    }
    const QString url = u"http://127.0.0.1:%1/%C5%82%C4%85ka.png"_s.arg(port);
    Workspace ws;
    ws.setWebPictureCacheDir(m_tmp.filePath(u"cache-cors"_s));
    ws.setWebPageScheme(pageScheme);
    QSignalSpy changed(&ws, &Workspace::webPictureChanged);
    ws.loadWebPicture(url);
    QTRY_COMPARE(ws.webPicture(url).value(u"state"_s).toString(), u"failed"_s);
    const QString error = ws.webPicture(url).value(u"error"_s).toString();
    QVERIFY2(error.contains(expected), qPrintable(error));
    if (!pageScheme.isEmpty())
        QVERIFY2(error.contains(u"Save the picture in the notebook in the app"_s), qPrintable(error));
}

QTEST_MAIN(TestWorkspacePictures)
#include "tst_workspace_pictures.moc"
