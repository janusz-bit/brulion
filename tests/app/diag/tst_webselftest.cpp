// The browser build's self-tests (src/app/diag/webselftest.h, run in browsers by tests/web) on the
// desktop: the facts of a freehand line, the texts a scene shows, pictures from the internet
// settling on the real Workspace (against a tiny HTTP server on 127.0.0.1) and the export with
// copies after them, a picture that never answers, the pen's report of a drawing canvas.
// Offscreen with the software scene graph.
#include "webselftest.h"

#include "drawingcanvas.h"
#include "workspace.h"

#include "drawing/drawing.h"

#include "../minihttpserver.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using brulion::diag::PenSelfTest;
using brulion::diag::WebPicturesSelfTest;
using brulion::drawingview::DrawingCanvas;
using brulion::shell::Workspace;

namespace {

bool writeFile(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}

QByteArray pngBytes()
{
    QImage image(64, 48, QImage::Format_RGB32);
    image.fill(QColor(u"#ff00ff"_s));
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return buffer.data();
}

const QByteArray svgBytes = R"(<svg xmlns="http://www.w3.org/2000/svg" width="20" height="10"><rect width="20" height="10" fill="#00ff00"/></svg>)";

/// A port nobody listens on (taken, then let go).
quint16 closedPort()
{
    QTcpServer server;
    server.listen(QHostAddress::LocalHost);
    const quint16 port = server.serverPort();
    server.close();
    return port;
}

/// Collects what a self-test publishes.
struct Published {
    QList<std::pair<QByteArray, QJsonObject>> results;
    brulion::diag::PublishFn fn()
    {
        return [this](const char *name, const QJsonObject &result) { results.append({QByteArray(name), result}); };
    }
    QJsonObject last(const char *name) const
    {
        for (auto it = results.crbegin(); it != results.crend(); ++it) {
            if (it->first == name)
                return it->second;
        }
        return {};
    }
    QList<QByteArray> names() const
    {
        QList<QByteArray> out;
        for (const auto &r : results)
            out.append(r.first);
        return out;
    }
};

QJsonObject pictureOf(const QJsonObject &report, const QString &url)
{
    for (const QJsonValue &p : report.value(u"pictures"_s).toArray()) {
        if (p.toObject().value(u"url"_s).toString() == url)
            return p.toObject();
    }
    return {};
}

} // namespace

class TestWebSelfTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;
    int m_n = 0;

    std::unique_ptr<Workspace> openVault(const QHash<QString, QByteArray> &files)
    {
        const QString root = m_tmp.filePath(u"Brulion %1"_s.arg(++m_n));
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            if (!writeFile(root + u'/' + it.key(), it.value()))
                qWarning() << "cannot write" << it.key();
        }
        auto ws = std::make_unique<Workspace>();
        ws->sync()->setSyncthingDiscovery(false);
        ws->setWebPictureCacheDir(m_tmp.filePath(u"cache-%1"_s.arg(m_n)));
        QString error;
        if (!ws->open(root, root + u"-stan"_s, &error))
            qWarning() << error;
        ws->finishIndexing();
        return ws;
    }

private Q_SLOTS:
    void strokeFacts_data()
    {
        QTest::addColumn<QList<double>>("pressures");
        QTest::addColumn<bool>("simulate");
        QTest::addColumn<bool>("highlighter");
        QTest::addColumn<double>("width");
        QTest::addColumn<QList<double>>("expected"); // min, max, first, last ([] → null)

        QTest::newRow("pióro, nacisk 0,1→0,9")
                << QList<double>{0.1, 0.35, 0.6, 0.9} << false << false << 1.0 << QList<double>{0.1, 0.9, 0.1, 0.9};
        QTest::newRow("stały nacisk") << QList<double>{0.5, 0.5, 0.5} << true << false << 2.0
                                      << QList<double>{0.5, 0.5, 0.5, 0.5};
        QTest::newRow("zakreślacz") << QList<double>{0.3, 0.8, 0.4} << false << true << 6.0
                                    << QList<double>{0.3, 0.8, 0.3, 0.4};
        QTest::newRow("linia bez punktów") << QList<double>{} << true << false << 1.0 << QList<double>{};
        QTest::newRow("nacisk poza zakresem 1,4 / −0,3")
                << QList<double>{1.4, -0.3, 0.7} << false << false << 0.5 << QList<double>{-0.3, 1.4, 1.4, 0.7};
    }

    void strokeFacts()
    {
        QFETCH(QList<double>, pressures);
        QFETCH(bool, simulate);
        QFETCH(bool, highlighter);
        QFETCH(double, width);
        QFETCH(QList<double>, expected);
        brulion::drawing::Stroke stroke;
        for (qsizetype i = 0; i < pressures.size(); ++i)
            stroke.points.append({double(10 * i), 5.0, pressures.at(i)});
        stroke.simulatePressure = simulate;
        stroke.tool = highlighter ? brulion::drawing::Tool::Highlighter : brulion::drawing::Tool::Pen;
        stroke.width = width;
        const QJsonObject facts = brulion::diag::strokeFacts(stroke);
        QCOMPARE(facts.value(u"tool"_s).toString(), highlighter ? u"highlighter"_s : u"pen"_s);
        QCOMPARE(facts.value(u"simulatePressure"_s).toBool(), simulate);
        QCOMPARE(facts.value(u"points"_s).toInt(), int(pressures.size()));
        QCOMPARE(facts.value(u"width"_s).toDouble(), width);
        const QStringList keys{u"pressureMin"_s, u"pressureMax"_s, u"pressureFirst"_s, u"pressureLast"_s};
        for (qsizetype i = 0; i < keys.size(); ++i) {
            const QJsonValue v = facts.value(keys.at(i));
            if (expected.isEmpty()) {
                QVERIFY2(v.isNull(), qPrintable(keys.at(i)));
            } else {
                QVERIFY2(v.isDouble(), qPrintable(keys.at(i)));
                QCOMPARE(v.toDouble(), expected.at(i));
            }
        }
    }

    void shownTextsFindsVisibleOnly()
    {
        QQuickWindow window;
        window.resize(400, 300);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            Column {
                Text { objectName: "webPictureText"; text: "Łąka — zażółć" }
                Text { objectName: "webPictureText"; text: "ukryty"; visible: false }
                Item { visible: false; Text { objectName: "webPictureText"; text: "w ukrytym" } }
                Item { opacity: 0; Text { objectName: "webPictureText"; text: "przezroczysty" } }
                Text { objectName: "inny"; text: "inna nazwa" }
                Repeater { model: 2; Text { objectName: "webPictureText"; text: "wiersz " + index } }
                Rectangle { objectName: "webPictureText"; width: 4; height: 4 }
            }
        )",
                          QUrl());
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(object.get());
        QVERIFY(item);
        item->setParentItem(window.contentItem());
        QCOMPARE(brulion::diag::shownTexts(window.contentItem(), u"webPictureText"_s),
                 (QStringList{u"Łąka — zażółć"_s, u"wiersz 0"_s, u"wiersz 1"_s}));
        QCOMPARE(brulion::diag::shownTexts(window.contentItem(), u"brak"_s), QStringList());
        QCOMPARE(brulion::diag::shownTexts(nullptr, u"webPictureText"_s), QStringList());
    }

    // The pictures of a journal as a browser page on https sees them (the desktop has no CORS
    // or mixed content: closed ports stand for them), then the export with copies.
    void webPicturesSettleAndExport()
    {
        MiniHttpServer server;
        server.bodies.insert("/%C5%82%C4%85ka.png", pngBytes());
        server.bodies.insert("/strona.html", "<!doctype html><title>Strona</title><p>Zażółć</p>");
        server.bodies.insert("/galeria-%C5%BC%C3%B3%C5%82w.svg", svgBytes);
        const QString port = QString::number(closedPort());
        const QString ok = server.url(u"/łąka.png"_s);
        const QString http = u"http://127.0.0.1:%1/x.png"_s.arg(port);
        const QString https = u"https://127.0.0.1:%1/zdj%C4%99cie.png"_s.arg(port);
        const QString missing = server.url(u"/brak.png"_s);
        const QString page = server.url(u"/strona.html"_s);
        const QString gallery = server.url(u"/galeria-żółw.svg"_s);
        const QString journal = u"- Łąka: ![łąka](%1)\n- ![obraz](%2)\n- ![zdjęcie](%3)\n- ![brak](%4)\n"
                                u"- ![strona](%5)\n- zwykły tekst, zażółć gęślą jaźń\n"_s.arg(ok, http, https, missing, page);
        auto ws = openVault({{u"journals/2026-10-14.md"_s, journal.toUtf8()},
                             {u"Galeria.md"_s, u"- ![żółw](%1)\n"_s.arg(gallery).toUtf8()}});
        QVERIFY(ws->isOpen());
        ws->setWebPictureHostCheck([](const QString &) { return false; }); // 127.0.0.1 as the internet
        ws->setWebPageScheme(u"HTTPS"_s);
        QCOMPARE(ws->webPageScheme(), u"https"_s);
        ws->setWebPicturesShown(true);

        Published published;
        WebPicturesSelfTest::Options options;
        options.page = u"journals/2026-10-14.md"_s;
        options.uiGraceMs = 0;
        options.settleMs = 20000;
        WebPicturesSelfTest test(ws.get(), options, published.fn());
        QSignalSpy finished(&test, &WebPicturesSelfTest::finished);
        test.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QCOMPARE(published.names(), (QList<QByteArray>{"brulionWebPictureSelfTest", "brulionExportSelfTest"}));

        const QJsonObject pictures = published.last("brulionWebPictureSelfTest");
        QCOMPARE(pictures, test.picturesReport());
        QCOMPARE(pictures.value(u"page"_s).toString(), options.page);
        QCOMPARE(pictures.value(u"pageScheme"_s).toString(), u"https"_s);
        QVERIFY(pictures.value(u"done"_s).toBool());
        QVERIFY(!pictures.value(u"timedOut"_s).toBool());
        QVERIFY(!pictures.contains(u"shownTexts"_s)); // no window
        const QJsonArray all = pictures.value(u"pictures"_s).toArray();
        QCOMPARE(all.size(), 5); // as written, in the order of the page
        QCOMPARE(all.at(0).toObject().value(u"url"_s).toString(), ok);
        for (const QJsonValue &p : all)
            QVERIFY(!p.toObject().value(u"uiFetched"_s).toBool()); // no editor here
        const QJsonObject ready = pictureOf(pictures, ok);
        QCOMPARE(ready.value(u"state"_s).toString(), u"ready"_s);
        QCOMPARE(ready.value(u"format"_s).toString(), u"png"_s);
        QCOMPARE(ready.value(u"error"_s).toString(), QString());
        const QString httpError = pictureOf(pictures, http).value(u"error"_s).toString();
        QCOMPARE(pictureOf(pictures, http).value(u"state"_s).toString(), u"failed"_s);
        QVERIFY2(httpError.contains(u"http://"_s) && httpError.contains(u"https://"_s) && !httpError.contains(u"(CORS)"_s),
                 qPrintable(httpError));
        const QString corsError = pictureOf(pictures, https).value(u"error"_s).toString();
        QVERIFY2(corsError.contains(u"(CORS)"_s) && corsError.contains(u"127.0.0.1"_s), qPrintable(corsError));
        QVERIFY(pictureOf(pictures, missing).value(u"error"_s).toString().contains(u"404"_s));
        const QString notPicture = pictureOf(pictures, page).value(u"error"_s).toString();
        QVERIFY2(notPicture.contains(u"127.0.0.1"_s) && !notPicture.contains(u"(CORS)"_s), qPrintable(notPicture));

        const QJsonObject exported = published.last("brulionExportSelfTest");
        QCOMPARE(exported, test.exportReport());
        QCOMPARE(exported.value(u"state"_s).toString(), u"ready"_s); // not a browser: nothing saved
        QVERIFY(!exported.value(u"saved"_s).toBool());
        QCOMPARE(exported.value(u"format"_s).toString(), u"html-single"_s);
        QCOMPARE(exported.value(u"scope"_s).toString(), u"vault"_s);
        QVERIFY(exported.value(u"fileName"_s).toString().endsWith(u".html"_s));
        QVERIFY(exported.value(u"bytes"_s).toDouble() > 1000);
        // The cached picture and the gallery's, fetched by the export itself.
        QCOMPARE(exported.value(u"dataPictures"_s).toInt(), 2);
        QCOMPARE(server.requests.count("/galeria-%C5%BC%C3%B3%C5%82w.svg"), 1); // not on the page: once
        QCOMPARE(server.requests.count("/%C5%82%C4%85ka.png"), 1); // the export took the cached copy
        const QJsonArray warnings = exported.value(u"warnings"_s).toArray();
        QCOMPARE(warnings.size(), 4);
        for (const QJsonValue &w : warnings)
            QVERIFY2(w.toString().contains(u"127.0.0.1"_s), qPrintable(w.toString()));
    }

    void webPicturesTimeout()
    {
        MiniHttpServer server;
        server.stalled.insert("/czeka%C5%82.png");
        const QString stalled = server.url(u"/czekał.png"_s);
        auto ws = openVault({{u"Strona.md"_s, u"![](%1)\n"_s.arg(stalled).toUtf8()}});
        ws->setWebPictureHostCheck([](const QString &) { return false; });
        // Not shown without asking: the test asks itself.
        Published published;
        WebPicturesSelfTest::Options options;
        options.page = u"Strona.md"_s;
        options.uiGraceMs = 100;
        options.settleMs = 500;
        options.exportCopies = false;
        WebPicturesSelfTest test(ws.get(), options, published.fn());
        QSignalSpy finished(&test, &WebPicturesSelfTest::finished);
        test.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QCOMPARE(published.names(), QList<QByteArray>{"brulionWebPictureSelfTest"});
        const QJsonObject report = published.last("brulionWebPictureSelfTest");
        QVERIFY(report.value(u"timedOut"_s).toBool());
        QCOMPARE(report.value(u"pageScheme"_s).toString(), QString());
        QCOMPARE(pictureOf(report, stalled).value(u"state"_s).toString(), u"loading"_s);
        QVERIFY(report.value(u"elapsedMs"_s).toDouble() >= 500);
        QTRY_COMPARE(server.requests.size(), 1);
    }

    void webPicturesOfAMissingPage()
    {
        auto ws = openVault({{u"Strona.md"_s, "- zażółć\n"}});
        Published published;
        WebPicturesSelfTest::Options options;
        options.page = u"Nie ma takiej.md"_s;
        options.exportCopies = false;
        WebPicturesSelfTest test(ws.get(), options, published.fn());
        QSignalSpy finished(&test, &WebPicturesSelfTest::finished);
        test.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        const QJsonObject report = published.last("brulionWebPictureSelfTest");
        QVERIFY(report.value(u"done"_s).toBool());
        QVERIFY(!report.value(u"timedOut"_s).toBool());
        QVERIFY(report.value(u"pictures"_s).toArray().isEmpty());
    }

    void penReportsTheCanvas()
    {
        QQuickWindow window;
        window.resize(600, 500);
        // A preview (no input) larger than the editor's paper is not the one.
        auto *preview = new DrawingCanvas(window.contentItem());
        preview->setSize(QSizeF(600, 500));
        preview->setInteractive(false);
        preview->setZ(-1);
        auto *canvas = new DrawingCanvas(window.contentItem());
        canvas->setPosition(QPointF(20, 40));
        canvas->setSize(QSizeF(400, 300));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(canvas->load({}), QString());

        Published published;
        PenSelfTest test(&window, published.fn());
        test.start();
        QJsonObject report = published.last("brulionPenSelfTest");
        QVERIFY(report.value(u"found"_s).toBool());
        QVERIFY(report.value(u"editable"_s).toBool());
        QVERIFY(!report.value(u"stylusSeen"_s).toBool());
        QCOMPARE(report.value(u"zoom"_s).toDouble(), 1.0);
        QCOMPARE(report.value(u"strokeCount"_s).toInt(), 0);
        const QJsonObject rect = report.value(u"canvas"_s).toObject();
        QCOMPARE(rect.value(u"x"_s).toDouble(), 20.0);
        QCOMPARE(rect.value(u"y"_s).toDouble(), 40.0);
        QCOMPARE(rect.value(u"width"_s).toDouble(), 400.0);
        QCOMPARE(rect.value(u"height"_s).toDouble(), 300.0);

        // A mouse line in window coordinates: its bounds come back in them.
        QTest::mousePress(&window, Qt::LeftButton, {}, QPoint(60, 100));
        for (int i = 1; i <= 20; ++i)
            QTest::mouseMove(&window, QPoint(60 + 12 * i, 100 + 3 * i));
        QTest::mouseRelease(&window, Qt::LeftButton, {}, QPoint(300, 160));
        QCOMPARE(canvas->strokeCount(), 1);
        report = published.last("brulionPenSelfTest");
        QCOMPARE(report, test.report());
        QCOMPARE(report.value(u"strokeCount"_s).toInt(), 1);
        QCOMPARE(report.value(u"elements"_s).toInt(), 1);
        QCOMPARE(report.value(u"changes"_s).toInt(), 1);
        const QJsonObject stroke = report.value(u"strokes"_s).toArray().first().toObject();
        QCOMPARE(stroke.value(u"tool"_s).toString(), u"pen"_s);
        QVERIFY(stroke.value(u"simulatePressure"_s).toBool());
        QVERIFY(stroke.value(u"points"_s).toInt() >= 10);
        const QJsonObject bounds = stroke.value(u"bounds"_s).toObject();
        QVERIFY(qAbs(bounds.value(u"x"_s).toDouble() - 60) <= 3);
        QVERIFY(qAbs(bounds.value(u"y"_s).toDouble() - 100) <= 3);
        QVERIFY(qAbs(bounds.value(u"x"_s).toDouble() + bounds.value(u"width"_s).toDouble() - 300) <= 3);
        QVERIFY(qAbs(bounds.value(u"y"_s).toDouble() + bounds.value(u"height"_s).toDouble() - 160) <= 3);

        // Moved and zoomed: the bounds follow the view.
        canvas->zoomBy(2, 0, 0);
        report = published.last("brulionPenSelfTest");
        QCOMPARE(report.value(u"zoom"_s).toDouble(), 2.0);
        const QJsonObject zoomed = report.value(u"strokes"_s).toArray().first().toObject().value(u"bounds"_s).toObject();
        QVERIFY(zoomed.value(u"width"_s).toDouble() > 1.8 * bounds.value(u"width"_s).toDouble());

        // Undo is a change too: no lines left.
        canvas->undo();
        report = published.last("brulionPenSelfTest");
        QCOMPARE(report.value(u"strokeCount"_s).toInt(), 0);
        QCOMPARE(report.value(u"changes"_s).toInt(), 2);
    }

    void penWithoutACanvas()
    {
        QQuickWindow window;
        window.resize(300, 200);
        auto *hidden = new DrawingCanvas(window.contentItem());
        hidden->setSize(QSizeF(300, 200));
        hidden->setVisible(false);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        Published published;
        PenSelfTest test(&window, published.fn());
        test.setDeadlineMs(300);
        test.start();
        QVERIFY(published.results.isEmpty());
        QTRY_COMPARE(published.results.size(), 1);
        const QJsonObject report = published.last("brulionPenSelfTest");
        QVERIFY(!report.value(u"found"_s).toBool());
        QCOMPARE(report.value(u"error"_s).toString(), u"no drawing canvas on screen"_s);
        QCOMPARE(test.report(), report);
        // A window gone: the same answer, no crash.
        PenSelfTest none(nullptr, published.fn());
        none.setDeadlineMs(0);
        none.start();
        QCOMPARE(published.results.size(), 2);
    }
};

QTEST_MAIN(TestWebSelfTest)
#include "tst_webselftest.moc"
