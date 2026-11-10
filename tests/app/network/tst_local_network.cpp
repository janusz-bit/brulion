// LocalOnlyNetworkFactory (src/app/ui/localnetwork.h): the QML engine's access manager serves
// local URLs only. A local server stands in for a tracker: refused requests and rich text with a
// remote <img> must never reach it, while data: and file: URLs (the vault's pictures) still load.
// A file: URL of another machine (a host, a UNC path) is refused; Image still reads one itself.
#include "localnetwork.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QNetworkReply>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using brulion::ui::LocalOnlyNetworkAccessManager;
using brulion::ui::LocalOnlyNetworkFactory;

namespace {

/// A 1×1 PNG.
const QByteArray Pixel = QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==");

/// A local TCP server that records whether anyone connected.
class Tracker : public QObject {
public:
    Tracker()
    {
        QVERIFY(m_server.listen(QHostAddress::LocalHost));
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                ++connections;
                socket->setParent(this);
            }
        });
    }

    QString url(const QString &path) const { return u"http://127.0.0.1:%1/%2"_s.arg(m_server.serverPort()).arg(path); }

    int connections = 0;

private:
    QTcpServer m_server;
};

/// A QML item made from `qml` (QtQuick imported) in a shown window of its own.
struct Scene {
    QQuickWindow window;
    std::unique_ptr<QObject> object;
};

std::unique_ptr<Scene> show(QQmlEngine &engine, const QByteArray &qml)
{
    auto scene = std::make_unique<Scene>();
    QQmlComponent component(&engine);
    component.setData("import QtQuick\n" + qml, QUrl(u"file:///brulion-test/Scene.qml"_s));
    scene->object.reset(component.create());
    if (!scene->object) {
        qWarning().noquote() << component.errorString();
        return nullptr;
    }
    qobject_cast<QQuickItem *>(scene->object.get())->setParentItem(scene->window.contentItem());
    scene->window.resize(400, 200);
    scene->window.show();
    return scene;
}

} // namespace

class TestLocalNetwork : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void schemes_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("local");
        QTest::newRow("file") << u"file:///home/ja/notatki/assets/łąka.png"_s << true;
        QTest::newRow("file, windows drive") << u"file:///C:/Users/ja/Notatki/łąka.png"_s << true;
        QTest::newRow("file at localhost") << u"file://LocalHost/home/ja/notatki/łąka.png"_s << true;
        QTest::newRow("file relative") << u"file:assets/łąka.png"_s << true;
        // Another machine (a UNC path on Windows: SMB to it, the user's NTLM hash).
        QTest::newRow("file of a server") << u"file://serwer/udział/łąka.png"_s << false;
        QTest::newRow("file of a server, upper case") << u"FILE://Serwer.lan/udział/łąka.png"_s << false;
        QTest::newRow("file of an address") << u"file://192.168.1.20/udział/łąka.png"_s << false;
        QTest::newRow("file, four slashes") << u"file:////serwer/udział/łąka.png"_s << false;
        QTest::newRow("file, localhost then a server") << u"file://localhost//serwer/udział/łąka.png"_s << false;
        QTest::newRow("file, encoded backslashes") << u"file:///%5C%5Cserwer%5Cudział%5Cłąka.png"_s << false;
        QTest::newRow("file, backslashes") << u"file:\\\\serwer\\udział\\łąka.png"_s << false;
        QTest::newRow("resources") << u"qrc:/icons/sigma.svg"_s << true;
        QTest::newRow("data (pictures in the browser)") << u"data:image/png;base64,AAAA"_s << true;
        QTest::newRow("android assets") << u"assets:/qml/Main.qml"_s << true;
        QTest::newRow("android content") << u"content://com.android.externalstorage/x.png"_s << true;
        QTest::newRow("http") << u"http://example.org/x.png"_s << false;
        QTest::newRow("https upper case") << u"HTTPS://example.org/x.png"_s << false;
        QTest::newRow("ftp") << u"ftp://example.org/x.png"_s << false;
        QTest::newRow("unknown scheme") << u"gopher://example.org/x"_s << false;
        QTest::newRow("relative") << u"obraz.png"_s << false;
    }

    void schemes()
    {
        QFETCH(QString, url);
        QFETCH(bool, local);
        QCOMPARE(LocalOnlyNetworkAccessManager::isLocal(QUrl(url)), local);
    }

    void refusesRemoteRequests_data()
    {
        QTest::addColumn<QString>("scheme");
        QTest::newRow("http") << u"http"_s;
        QTest::newRow("https") << u"https"_s;
    }

    // A refused request is answered at once with a transparent pixel: nothing goes out, and rich
    // text never sees a failed picture (which can crash Qt Quick).
    void refusesRemoteRequests()
    {
        QFETCH(QString, scheme);
        Tracker tracker;
        LocalOnlyNetworkFactory factory;
        std::unique_ptr<QNetworkAccessManager> nam(factory.create(nullptr));
        QUrl url(tracker.url(u"tracker.png"_s));
        url.setScheme(scheme);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"^QML asked for .*tracker\\.png"_s));
        std::unique_ptr<QNetworkReply> reply(nam->get(QNetworkRequest(url)));
        QSignalSpy finished(reply.get(), &QNetworkReply::finished);
        QVERIFY(finished.wait(2000));
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->url(), url);
        const QImage picture = QImage::fromData(reply->readAll());
        QCOMPARE(picture.size(), QSize(1, 1));
        QCOMPARE(qAlpha(picture.pixel(0, 0)), 0);
        QTest::qWait(300);
        QCOMPARE(tracker.connections, 0);
    }

    void servesLocalUrls()
    {
        LocalOnlyNetworkFactory factory;
        std::unique_ptr<QNetworkAccessManager> nam(factory.create(nullptr));
        const QByteArray text = u"Zażółć gęślą jaźń"_s.toUtf8();
        std::unique_ptr<QNetworkReply> data(nam->get(QNetworkRequest(
                QUrl(u"data:text/plain;charset=utf-8;base64,"_s + QString::fromLatin1(text.toBase64())))));
        QSignalSpy dataDone(data.get(), &QNetworkReply::finished);
        QVERIFY(data->isFinished() || dataDone.wait(2000));
        QCOMPARE(data->error(), QNetworkReply::NoError);
        QCOMPARE(data->readAll(), text);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile file(dir.filePath(u"łąka.png"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(Pixel);
        file.close();
        std::unique_ptr<QNetworkReply> local(nam->get(QNetworkRequest(QUrl::fromLocalFile(file.fileName()))));
        QSignalSpy localDone(local.get(), &QNetworkReply::finished);
        QVERIFY(local->isFinished() || localDone.wait(2000));
        QCOMPARE(local->error(), QNetworkReply::NoError);
        QCOMPARE(local->readAll(), Pixel);
    }

    // What the factory prevents: rich text with a remote picture (a note whose rendered Markdown
    // let an <img> through) makes Qt Quick contact its host without a click. Several pictures,
    // shown again and again: refusing them never crashes the Text.
    void richTextFetchesNoRemotePicture_data()
    {
        QTest::addColumn<bool>("guarded");
        QTest::newRow("with the factory") << true;
        QTest::newRow("without (Qt's default)") << false;
    }

    void richTextFetchesNoRemotePicture()
    {
        QFETCH(bool, guarded);
        Tracker tracker;
        LocalOnlyNetworkFactory factory;
        QQmlEngine engine;
        if (guarded)
            engine.setNetworkAccessManagerFactory(&factory);
        for (int round = 0; round < (guarded ? 10 : 1); ++round) {
            const auto scene = show(engine, "Text { width: 400; textFormat: Text.RichText; wrapMode: Text.Wrap }");
            QVERIFY(scene);
            QString html = u"Wzór:"_s;
            for (int k = 0; k < (guarded ? 3 : 1); ++k)
                html += u" <img src=\"%1\"> i dalej"_s.arg(tracker.url(u"%1-%2.png"_s.arg(round).arg(k)));
            scene->object->setProperty("text", html);
            if (!guarded) {
                QTRY_VERIFY_WITH_TIMEOUT(tracker.connections > 0, 5000);
                return;
            }
            QTest::qWait(150);
        }
        QTest::qWait(300);
        QCOMPARE(tracker.connections, 0);
    }

    // The vault's pictures still load: data: URLs in the browser build, file: URLs elsewhere.
    void picturesFromTheVaultLoad()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile file(dir.filePath(u"łąka.png"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(Pixel);
        file.close();
        LocalOnlyNetworkFactory factory;
        QQmlEngine engine;
        engine.setNetworkAccessManagerFactory(&factory);
        for (const QUrl &url : {QUrl(u"data:image/png;base64,"_s + QString::fromLatin1(Pixel.toBase64())),
                                QUrl::fromLocalFile(file.fileName())}) {
            const auto scene = show(engine, "Image { asynchronous: true }");
            QVERIFY(scene);
            scene->object->setProperty("source", url);
            QTRY_COMPARE_WITH_TIMEOUT(scene->object->property("status").toInt(), 1 /* Image.Ready */, 5000);
            QCOMPARE(scene->object->property("implicitWidth").toReal(), 1.0);
        }
    }

    // A file: URL with a host is refused by the manager (it reads no file: the blank pixel). Qt
    // Quick reads file: URLs itself, without the manager, so a notebook on \\server\share keeps its
    // pictures (the Workspace's URLs for it have the server as host). Stand-in for the share: the
    // temporary folder's first part as the host ("//tmp/x" is "/tmp/x" here).
    void fileUrlWithAHost()
    {
#ifdef Q_OS_WIN
        QSKIP("a host in a file: URL is a real server on Windows");
#endif
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = QDir::cleanPath(dir.filePath(u"łąka.png"_s));
        QImage picture(3, 2, QImage::Format_ARGB32);
        picture.fill(Qt::darkGreen);
        QVERIFY(picture.save(file, "PNG"));
        const qsizetype slash = file.indexOf(u'/', 1); // "/tmp/x/łąka.png" as file://tmp/x/łąka.png
        QUrl shared;
        shared.setScheme(u"file"_s);
        shared.setHost(file.mid(1, slash - 1));
        shared.setPath(file.mid(slash));
        if (slash < 2 || shared.host().isEmpty() || !QFileInfo::exists(shared.toLocalFile()))
            QSKIP("the temporary folder cannot be named as a host here");
        QVERIFY(!LocalOnlyNetworkAccessManager::isLocal(shared));

        LocalOnlyNetworkFactory factory;
        std::unique_ptr<QNetworkAccessManager> nam(factory.create(nullptr));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"^QML asked for file://.*\\.png"_s));
        std::unique_ptr<QNetworkReply> reply(nam->get(QNetworkRequest(shared)));
        QSignalSpy finished(reply.get(), &QNetworkReply::finished);
        QVERIFY(finished.wait(2000));
        QCOMPARE(QImage::fromData(reply->readAll()).size(), QSize(1, 1));

        QQmlEngine engine;
        engine.setNetworkAccessManagerFactory(&factory);
        const auto scene = show(engine, "Image { asynchronous: true }");
        QVERIFY(scene);
        scene->object->setProperty("source", shared);
        QTRY_COMPARE_WITH_TIMEOUT(scene->object->property("status").toInt(), 1 /* Image.Ready */, 5000);
        QCOMPARE(scene->object->property("implicitWidth").toReal(), 3.0);
    }
};

QTEST_MAIN(TestLocalNetwork)
#include "tst_local_network.moc"
