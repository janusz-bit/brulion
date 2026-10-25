// Pictures from the internet on the real Workspace (workspacepictures.cpp) with the block editor:
// nothing is fetched until allowed, "Pokaż" and the "always" setting fetch into the device's
// cache, failures are told, "Pobierz do notatnika" stores an attachment in the vault and points
// the page at it — against a tiny HTTP server on 127.0.0.1.
#include "pagecontroller.h"
#include "workspace.h"

#include "blocklistmodel.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

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

/// A tiny HTTP/1.1 server on 127.0.0.1: `responses` by path, 404 for the rest.
class MiniHttpServer : public QObject {
public:
    QHash<QByteArray, QByteArray> bodies; ///< path → picture bytes
    QList<QByteArray> requests;

    MiniHttpServer()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                socket->setParent(this);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onData(socket); });
            }
        });
    }
    QString url(const QString &path) const { return u"http://127.0.0.1:%1%2"_s.arg(m_server.serverPort()).arg(path); }

private:
    void onData(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer += socket->readAll();
        if (!buffer.contains("\r\n\r\n"))
            return;
        const QByteArray path = buffer.left(buffer.indexOf('\n')).split(' ').value(1);
        m_buffers.remove(socket);
        requests.append(path);
        const auto it = bodies.constFind(path);
        const QByteArray body = it == bodies.constEnd() ? QByteArray("<html>Nie ma</html>") : *it;
        const QByteArray status = it == bodies.constEnd() ? "404 Not Found" : "200 OK";
        socket->write("HTTP/1.1 " + status + "\r\nContent-Length: " + QByteArray::number(body.size())
                      + "\r\nConnection: close\r\n\r\n" + body);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

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

        // The "always" setting: the others are fetched; a missing one fails with a sentence.
        ws.setWebPicturesShown(true);
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
};

QTEST_MAIN(TestWorkspacePictures)
#include "tst_workspace_pictures.moc"
