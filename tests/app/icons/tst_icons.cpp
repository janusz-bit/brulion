// BIcon (src/app/qml/components/BIcon.qml) and its pictures (src/app/ui/iconprovider.h): one Image
// whose picture comes from image://bicon/… must look exactly like Qt's IconImage, which BIcon used
// to hold in a Loader — every pixel, at every size, colour, box and position, at device pixel
// ratio 1 and 2 (tst_icons_dpr2: QT_SCALE_FACTOR=2). Also its sizes (collapseWhenEmpty, an
// explicit width), names that must not leave the icon folder, names with Polish letters, and the
// cache: many icons of one name and size rasterise the SVG once, a new colour only recolours.
//
// The test loads the app's own BIcon.qml from the source tree into a module "Brulion" of its own
// (Theme: a stand-in with the two defaults BIcon reads; Icons: the real singleton) and renders with
// Qt Quick's software backend, offscreen.
#include "iconprovider.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using brulion::ui::IconImageProvider;
using brulion::ui::Icons;

namespace {

const QString kSourceDir = QStringLiteral(BRULION_SOURCE_DIR);

QString iconFolder()
{
    return kSourceDir + u"/assets/icons"_s;
}

} // namespace

class TestIcons : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_folder; // the provider's: the app's icons plus one with Polish letters in its name
    std::unique_ptr<QQmlEngine> m_engine;
    IconImageProvider *m_provider = nullptr; // the engine's

    std::unique_ptr<QQuickWindow> show(QQmlComponent &component, const QVariantMap &properties, QQuickItem **root)
    {
        QObject *object = component.createWithInitialProperties(properties);
        if (!object) {
            qWarning() << component.errors();
            return {};
        }
        auto *item = qobject_cast<QQuickItem *>(object);
        auto window = std::make_unique<QQuickWindow>();
        window->resize(int(item->width()), int(item->height()));
        item->setParentItem(window->contentItem());
        item->setParent(window.get());
        window->show();
        if (!QTest::qWaitForWindowExposed(window.get()))
            return {};
        *root = item;
        return window;
    }

    QQuickItem *child(QQuickItem *root, const QString &name) { return root->findChild<QQuickItem *>(name); }
    /// A BIcon's Image (its first child).
    static QQuickItem *imageOf(QQuickItem *icon)
    {
        QQuickItem *image = icon ? icon->childItems().value(0) : nullptr;
        return image && image->inherits("QQuickImage") ? image : nullptr;
    }
    static int statusOf(QQuickItem *icon)
    {
        QQuickItem *image = imageOf(icon);
        return image ? image->property("status").toInt() : -1;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_folder = m_dir.filePath(u"icons"_s);
        QVERIFY(QDir().mkpath(m_folder));
        const QStringList names = QDir(iconFolder()).entryList({u"*.svg"_s}, QDir::Files);
        QVERIFY(names.size() > 100);
        for (const QString &name : names)
            QVERIFY(QFile::copy(iconFolder() + u'/' + name, m_folder + u'/' + name));
        QVERIFY(QFile::copy(iconFolder() + u"/bell.svg"_s, m_folder + u"/dzwonek-żółty.svg"_s));

        // A module "Brulion" of the test's own, first on the import path: the real BIcon.qml (a
        // copy, as the app's module holds it), a Theme with BIcon's defaults, and the real Icons
        // (C++). Its own qmldir, so that no other "Brulion" found on the way — the app's, whose
        // qmldir points into resources this test does not have (Windows CI: "Type BIcon
        // unavailable … LinkPreviews.qml: No such file") — is the one `import Brulion` takes.
        const QString module = m_dir.filePath(u"Brulion"_s);
        QVERIFY(QDir().mkpath(module));
        QFile themeFile(module + u"/Theme.qml"_s);
        QVERIFY(themeFile.open(QIODevice::WriteOnly));
        themeFile.write("pragma Singleton\nimport QtQuick\nQtObject {\n"
                        "    readonly property int iconSize: 16\n"
                        "    readonly property color fg: \"#1f2328\"\n}\n");
        themeFile.close();
        QVERIFY(QFile::copy(kSourceDir + u"/src/app/qml/components/BIcon.qml"_s, module + u"/BIcon.qml"_s));
        QFile qmldir(module + u"/qmldir"_s);
        QVERIFY(qmldir.open(QIODevice::WriteOnly));
        qmldir.write("module Brulion\nsingleton Theme 1.0 Theme.qml\nBIcon 1.0 BIcon.qml\n");
        qmldir.close();
        qmlRegisterSingletonType<Icons>("Brulion", 1, 0, "Icons",
                                        [](QQmlEngine *engine, QJSEngine *) { return Icons::forEngine(engine); });

        m_engine = std::make_unique<QQmlEngine>();
        m_engine->addImportPath(m_dir.path()); // (first: added paths come before the defaults)
        // The provider before Icons gives the engine one of its own (Icons::forEngine keeps it).
        m_provider = new IconImageProvider(m_folder);
        m_engine->addImageProvider(IconImageProvider::providerId(), m_provider);
    }

    void cleanupTestCase() { m_engine.reset(); }

    /// The old BIcon (an IconImage filling an Item, Qt's own recolouring) and the new one side by
    /// side, in boxes of the same size at the same sub-pixel offset: the same pixels.
    void looksLikeIconImage_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<qreal>("size");
        QTest::addColumn<QColor>("color");
        QTest::addColumn<qreal>("box"); // the icon's width and height; -1: its size
        QTest::addColumn<qreal>("offset");

        const QColor fg(u"#1f2328"_s);
        QTest::newRow("bell 16") << u"bell"_s << 16.0 << fg << -1.0 << 0.0;
        QTest::newRow("check 14 white") << u"check"_s << 14.0 << QColor(Qt::white) << -1.0 << 0.0;
        QTest::newRow("chevron-right 20 accent") << u"chevron-right"_s << 20.0 << QColor(u"#0969da"_s) << -1.0 << 0.0;
        QTest::newRow("folder-open 24 translucent")
                << u"folder-open"_s << 24.0 << QColor(u"#80d1242f"_s) << -1.0 << 0.0;
        // A colour made with alpha() or by an animation keeps 16 bits a channel: rounded to 8 bits,
        // this one fills some pixels differently.
        QTest::newRow("bell 18 rgbF") << u"bell"_s << 18.0 << QColor::fromRgbF(0.421f, 0.736f, 0.448f, 0.480f) << -1.0
                                      << 0.0;
        QTest::newRow("minus 17 odd") << u"minus"_s << 17.0 << fg << -1.0 << 0.0;
        QTest::newRow("bell 18.4 fractional size") << u"bell"_s << 18.4 << fg << -1.0 << 0.0;
        // IconImage leaves a fully transparent colour alone: the SVG's own black.
        QTest::newRow("bell transparent") << u"bell"_s << 16.0 << QColor(Qt::transparent) << -1.0 << 0.0;
        // A box larger than the icon: drawn at its size, centred (Pad).
        QTest::newRow("bell in 30") << u"bell"_s << 16.0 << fg << 30.0 << 0.0;
        QTest::newRow("bell in 25 odd") << u"bell"_s << 16.0 << fg << 25.0 << 0.0;
        // A smaller one: scaled down (PreserveAspectFit).
        QTest::newRow("bell in 10") << u"bell"_s << 16.0 << fg << 10.0 << 0.0;
        QTest::newRow("bell at half a pixel") << u"bell"_s << 16.0 << fg << -1.0 << 0.5;
        QTest::newRow("check at 0.3") << u"check"_s << 20.0 << QColor(u"#cf222e"_s) << -1.0 << 0.3;
    }
    void looksLikeIconImage()
    {
        QFETCH(QString, name);
        QFETCH(qreal, size);
        QFETCH(QColor, color);
        QFETCH(qreal, box);
        QFETCH(qreal, offset);

        QQmlComponent component(m_engine.get());
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls.impl as Impl
            import Brulion
            Item {
                id: scene
                required property string name
                required property real size
                required property color color
                required property real box
                required property real offset
                required property url file
                width: 120
                height: 60
                // (Grey: a white icon shows too.)
                Rectangle {
                    anchors.fill: parent
                    color: "#8c959f"
                }
                // BIcon until 2026-11: an Item with a Loader of an IconImage.
                Item {
                    x: 8 + scene.offset
                    y: 8 + scene.offset
                    width: scene.box >= 0 ? scene.box : scene.size
                    height: width
                    Impl.IconImage {
                        anchors.fill: parent
                        sourceSize: Qt.size(scene.size, scene.size)
                        source: scene.file
                        color: scene.color
                        fillMode: Image.PreserveAspectFit
                    }
                }
                BIcon {
                    objectName: "icon"
                    x: 68 + scene.offset
                    y: 8 + scene.offset
                    width: scene.box >= 0 ? scene.box : scene.size
                    height: width
                    name: scene.name
                    size: scene.size
                    color: scene.color
                }
            })"_ba,
                          QUrl(u"qrc:/scene.qml"_s));
        QVERIFY2(component.isReady(), qPrintable(component.errorString() + u"(imports: "_s
                                                 + m_engine->importPathList().join(u", "_s) + u')'));
        QQuickItem *root = nullptr;
        auto window = show(component,
                           {{u"name"_s, name},
                            {u"size"_s, size},
                            {u"color"_s, color},
                            {u"box"_s, box},
                            {u"offset"_s, offset},
                            {u"file"_s, QUrl::fromLocalFile(iconFolder() + u'/' + name + u".svg"_s)}},
                           &root);
        QVERIFY(window);
        QQuickItem *icon = child(root, u"icon"_s);
        QVERIFY(icon);
        QTRY_COMPARE(statusOf(icon), 1); // Image.Ready

        const QImage shot = window->grabWindow();
        QVERIFY(!shot.isNull());
        // (In device pixels: the software backend's grab says a device pixel ratio of 1.)
        QCOMPARE(shot.width(), qRound(120 * window->devicePixelRatio()));
        const int half = shot.width() / 2;
        const QImage old = shot.copy(0, 0, half, shot.height());
        const QImage now = shot.copy(half, 0, half, shot.height());
        // Something was drawn at all (not two empty halves).
        bool drawn = false;
        for (int y = 0; y < old.height() && !drawn; ++y)
            for (int x = 0; x < old.width() && !drawn; ++x)
                drawn = old.pixel(x, y) != old.pixel(0, 0);
        QVERIFY(drawn);
        // TST_ICONS_SAVE=<dir>: the two halves of a row that differ, as PNGs.
        const QString saveDir = qEnvironmentVariable("TST_ICONS_SAVE");
        if (old != now && !saveDir.isEmpty()) {
            const QString tag = QString::fromLatin1(QTest::currentDataTag()).replace(u' ', u'_');
            old.save(saveDir + u'/' + tag + u"-old.png"_s);
            now.save(saveDir + u'/' + tag + u"-new.png"_s);
        }
        QCOMPARE(now, old);
    }

    /// BIcon's sizes as before: its size, none without a name (unless collapseWhenEmpty is off),
    /// the implicit size following an explicit width — also after the picture has loaded (which
    /// sets an Image's own implicit size).
    void sizes()
    {
        QQmlComponent component(m_engine.get());
        component.setData(R"(
            import QtQuick
            import Brulion
            Row {
                width: 200
                height: 60
                BIcon { objectName: "plain"; name: "bell" }
                BIcon { objectName: "sized"; name: "bell"; size: 22 }
                BIcon { objectName: "empty"; name: "" }
                BIcon { objectName: "slot"; name: ""; collapseWhenEmpty: false; size: 20 }
                BIcon { objectName: "wide"; name: "bell"; width: 30 }
                BIcon { objectName: "narrow"; name: "bell"; width: 10; height: 10 }
                BIcon { objectName: "toggled"; name: "check" }
            })"_ba,
                          QUrl(u"qrc:/sizes.qml"_s));
        QVERIFY2(component.isReady(), qPrintable(component.errorString() + u"(imports: "_s
                                                 + m_engine->importPathList().join(u", "_s) + u')'));
        QQuickItem *root = nullptr;
        auto window = show(component, {}, &root);
        QVERIFY(window);
        const auto check = [&](const char *name, qreal width, qreal height) {
            QQuickItem *icon = child(root, QString::fromLatin1(name));
            QVERIFY(icon);
            QCOMPARE(icon->width(), width);
            QCOMPARE(icon->height(), height);
            // BIcon's implicit size is its size, whatever its picture's.
            QCOMPARE(icon->implicitWidth(), width);
            QCOMPARE(icon->implicitHeight(), height);
        };
        QTRY_COMPARE(statusOf(child(root, u"plain"_s)), 1);
        check("plain", 16, 16);
        check("sized", 22, 22);
        check("empty", 0, 0);
        check("slot", 20, 20);
        check("wide", 30, 16);
        // Scaled down (PreserveAspectFit): Image sets the picture's implicit size on every resize.
        check("narrow", 10, 10);
        QQuickItem *narrow = child(root, u"narrow"_s);
        narrow->setWidth(12);
        narrow->setHeight(12);
        check("narrow", 12, 12);
        QVERIFY(imageOf(child(root, u"empty"_s)));
        QCOMPARE(imageOf(child(root, u"empty"_s))->property("source").toUrl(), QUrl());

        // A name that goes and comes back.
        QQuickItem *toggled = child(root, u"toggled"_s);
        QTRY_COMPARE(statusOf(toggled), 1);
        toggled->setProperty("name", u""_s);
        check("toggled", 0, 0);
        toggled->setProperty("collapseWhenEmpty", false);
        check("toggled", 16, 16);
        toggled->setProperty("name", u"check"_s);
        QTRY_COMPARE(statusOf(toggled), 1);
        check("toggled", 16, 16);
    }

    /// Many icons of one name, size and colour: one SVG rasterised; another colour only recolours.
    void sharedPictures()
    {
        QQmlComponent component(m_engine.get());
        component.setData(R"(
            import QtQuick
            import Brulion
            Grid {
                property color tint: "#57606a"
                columns: 10
                width: 200
                height: 100
                Repeater {
                    model: 40
                    BIcon { name: "folder-open"; size: 15; color: parent.tint }
                }
            })"_ba,
                          QUrl(u"qrc:/shared.qml"_s));
        QVERIFY2(component.isReady(), qPrintable(component.errorString() + u"(imports: "_s
                                                 + m_engine->importPathList().join(u", "_s) + u')'));
        const int before = m_provider->rasterCount();
        QQuickItem *root = nullptr;
        auto window = show(component, {}, &root);
        QVERIFY(window);
        QCOMPARE(m_provider->rasterCount(), before + 1);
        root->setProperty("tint", QColor(u"#cf222e"_s));
        QTRY_VERIFY(window->grabWindow().pixelColor(QPoint(7, 7)).isValid());
        QCOMPARE(m_provider->rasterCount(), before + 1);
    }

    /// Names come from notes too (a page's icon): none may leave the folder; others are found by
    /// their name as it is, Polish letters included.
    void names_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<bool>("found");
        QTest::newRow("lucide") << u"chevron-right"_s << true << true;
        QTest::newRow("polish") << u"dzwonek-żółty"_s << true << true;
        QTest::newRow("missing") << u"zażółć-gęślą-jaźń"_s << true << false;
        QTest::newRow("with # and ?") << u"bell#x?y"_s << true << false;
        QTest::newRow("empty") << u""_s << false << false;
        QTest::newRow("parent folder") << u"../icons/bell"_s << false << false;
        QTest::newRow("sub folder") << u"a/bell"_s << false << false;
        QTest::newRow("backslash") << u"..\\bell"_s << false << false;
        QTest::newRow("hidden") << u".bell"_s << false << false;
    }
    void names()
    {
        QFETCH(QString, name);
        QFETCH(bool, valid);
        QFETCH(bool, found);
        Icons icons;
        const QUrl url = icons.source(name, QColor(u"#1f2328"_s));
        QCOMPARE(!url.isEmpty(), valid);
        QCOMPARE(IconImageProvider::validName(name), valid);
        // The provider's id, as Qt Quick's pixmap cache takes it from the URL.
        const QString id = url.toString(QUrl::RemoveScheme | QUrl::RemoveAuthority).mid(1);
        QSize size;
        const QImage image = m_provider->requestImage(id, &size, QSize(16, 16));
        QCOMPARE(!image.isNull(), found);
        if (found) {
            QCOMPARE(image.size(), QSize(16, 16));
            QCOMPARE(size, QSize(16, 16));
        }
        // Malformed ids give nothing.
        QVERIFY(m_provider->requestImage(u"zzzz/bell"_s, &size, QSize(16, 16)).isNull());
        QVERIFY(m_provider->requestImage(u"bell"_s, &size, QSize(16, 16)).isNull());
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    TestIcons test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_icons.moc"
