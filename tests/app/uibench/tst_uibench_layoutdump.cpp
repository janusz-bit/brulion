// The layout dump (src/app/ui/layoutdump.h, `--dump-layout`; docs/UI.md "UI benchmark (Brulion vs
// Obsidian)") on a QML scene, offscreen with the software renderer at QT_SCALE_FACTOR 2 (rectangles
// stay logical): a text's painted rectangle (left, right and centre aligned), colour, font and
// Polish characters; rich-text fragments (a link's colour, a chip's background); invisible,
// transparent and clipped items left out; a half-transparent parent's opacity; targets (a
// MouseArea, a button with a MouseArea filling it — one target —, a TapHandler, a disabled
// MouseArea left out, an editable field); occlusion by a later opaque rectangle and — after a modal
// popup opened — by the popup's dim (not the popup's own text); the theme's tokens; writing to a
// missing folder fails with an error.
#include "layoutdump.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

namespace {

const char *const kScene = R"(
import QtQuick
import QtQuick.Templates as T

Window {
    id: win
    width: 400; height: 400
    visible: true
    color: "#fbf7ee"
    property QtObject theme: QtObject {
        property color fg: "#1f2430"
        property color fgMuted: "#535a6a"
        property color fgFaint: "#676c7b"
        property color bg: "#fbf7ee"
        property color bgElevated: "#fffdf7"
        property color bgSunken: "#f3eee2"
        property color accent: "#c8433a"
        property color link: "#2c6aa3"
    }
    Rectangle {
        objectName: "page"
        width: 400; height: 400
        color: "#fbf7ee"
        Text { objectName: "plain"; x: 10; y: 10; width: 200; height: 20; text: "Zażółć   gęślą jaźń"; color: "#1f2430"; font.pixelSize: 14 }
        Text { objectName: "right"; x: 10; y: 30; width: 200; height: 20; horizontalAlignment: Text.AlignRight; text: "Prawo"; font.pixelSize: 14 }
        Text { objectName: "centre"; x: 10; y: 50; width: 200; height: 20; horizontalAlignment: Text.AlignHCenter; text: "Środek"; font.pixelSize: 14 }
        Text {
            objectName: "rich"; x: 10; y: 70; width: 300
            textFormat: Text.RichText; color: "#1f2430"; linkColor: "#2c6aa3"; font.pixelSize: 14
            text: 'Zobacz <a href="x">link do strony</a> i <span style="background-color:#529fc3e6">#tag</span>'
        }
        Text { objectName: "hidden"; visible: false; x: 10; y: 100; text: "Ukryty" }
        Text { objectName: "transparent"; opacity: 0; x: 10; y: 100; text: "Przezroczysty" }
        Item {
            x: 10; y: 120; width: 50; height: 20; clip: true
            Text { objectName: "clipped"; x: 0; y: 30; text: "Obcięty" }
        }
        Item {
            opacity: 0.5; x: 10; y: 150; width: 100; height: 20
            Text { objectName: "half"; text: "Pół"; color: "#1f2430"; font.bold: true }
        }
        MouseArea { objectName: "mouse"; x: 250; y: 10; width: 20; height: 20 }
        T.Button {
            objectName: "button"; x: 250; y: 40; width: 60; height: 30; text: "Przycisk"
            MouseArea { objectName: "buttonMouse"; anchors.fill: parent }
        }
        Item { objectName: "tappable"; x: 250; y: 80; width: 40; height: 40; TapHandler { } }
        MouseArea { objectName: "disabledMouse"; enabled: false; x: 250; y: 130; width: 30; height: 30 }
        MouseArea { objectName: "hoverOnly"; acceptedButtons: Qt.NoButton; x: 300; y: 130; width: 30; height: 30 }
        TextInput { objectName: "input"; x: 250; y: 170; width: 100; height: 20; text: "Pole"; font.pixelSize: 14 }
        Text { objectName: "covered"; x: 10; y: 200; text: "Zasłonięty"; font.pixelSize: 14 }
        Rectangle { objectName: "cover"; x: 0; y: 195; width: 120; height: 30; color: "#ffffff" }
        Text { objectName: "underDim"; x: 10; y: 260; text: "Pod przyciemnieniem"; font.pixelSize: 14 }
    }
    T.Popup {
        id: popup
        objectName: "popup"
        x: 200; y: 250; width: 120; height: 60
        modal: true; dim: true
        T.Overlay.modal: Rectangle { color: "#80000000" }
        background: Rectangle { color: "#ffffff" }
        contentItem: Text { objectName: "inPopup"; text: "W okienku"; font.pixelSize: 14 }
    }
}
)";

QJsonObject byName(const QJsonArray &list, const QString &name)
{
    for (const QJsonValue &v : list) {
        if (v[u"name"_s].toString() == name)
            return v.toObject();
    }
    return {};
}

QRectF rectOf(const QJsonObject &o)
{
    const QJsonArray r = o[u"rect"_s].toArray();
    return QRectF(r.at(0).toDouble(), r.at(1).toDouble(), r.at(2).toDouble(), r.at(3).toDouble());
}

} // namespace

class TestUiBenchLayoutDump : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void viewportAndTokens();
    void texts();
    void richText();
    void leftOut();
    void targets();
    void occlusion();
    void boxes();
    void writeToMissingFolder();

private:
    QQmlEngine m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QJsonObject m_dump;    ///< the page
    QJsonObject m_dimmed;  ///< the page under the modal popup
};

void TestUiBenchLayoutDump::initTestCase()
{
    QQmlComponent component(&m_engine);
    component.setData(kScene, QUrl(u"qrc:/scene.qml"_s));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    m_window.reset(qobject_cast<QQuickWindow *>(object.release()));
    QVERIFY(m_window);
    QSignalSpy frames(m_window.get(), &QQuickWindow::frameSwapped);
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    QTRY_VERIFY(frames.count() > 0);
    QTest::qWait(300); // a second frame
    const QObject *theme = m_window->property("theme").value<QObject *>();
    QVERIFY(theme);
    m_dump = brulion::ui::layoutDump(m_window.get(), theme);
    QVERIFY(!m_dump.isEmpty());

    QObject *popup = m_window->findChild<QObject *>(u"popup"_s);
    QVERIFY(popup);
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    frames.clear();
    QTRY_VERIFY(frames.count() > 0);
    QTest::qWait(200);
    m_dimmed = brulion::ui::layoutDump(m_window.get(), theme);
}

void TestUiBenchLayoutDump::viewportAndTokens()
{
    QCOMPARE(m_dump[u"schema"_s].toInt(), 1);
    QCOMPARE(m_dump[u"app"_s].toString(), u"brulion"_s);
    const QJsonObject vp = m_dump[u"viewport"_s].toObject();
    QCOMPARE(vp[u"w"_s].toInt(), 400);
    QCOMPARE(vp[u"h"_s].toInt(), 400);
    QCOMPARE(vp[u"dpr"_s].toDouble(), qEnvironmentVariableIsSet("QT_SCALE_FACTOR") ? qEnvironmentVariable("QT_SCALE_FACTOR").toDouble() : 1.0);
    const QJsonObject tokens = m_dump[u"tokens"_s].toObject();
    QCOMPARE(tokens[u"fg"_s].toString(), u"#1f2430ff"_s);
    QCOMPARE(tokens[u"bgSunken"_s].toString(), u"#f3eee2ff"_s);
    QCOMPARE(tokens.size(), 8);
}

void TestUiBenchLayoutDump::texts()
{
    const QJsonArray texts = m_dump[u"texts"_s].toArray();
    const QJsonObject plain = byName(texts, u"plain"_s);
    QVERIFY(!plain.isEmpty());
    QCOMPARE(plain[u"text"_s].toString(), u"Zażółć gęślą jaźń"_s); // whitespace collapsed
    QCOMPARE(plain[u"chars"_s].toInt(), 17);
    QCOMPARE(plain[u"type"_s].toString(), u"Text"_s);
    QCOMPARE(plain[u"color"_s].toString(), u"#1f2430ff"_s);
    QCOMPARE(plain[u"px"_s].toDouble(), 14.0);
    QCOMPARE(plain[u"weight"_s].toInt(), 400);
    QVERIFY(!plain[u"family"_s].toString().isEmpty());
    QCOMPARE(plain[u"lines"_s].toInt(), 1);
    QCOMPARE(plain[u"alpha"_s].toDouble(), 1.0);
    QCOMPARE(plain[u"enabled"_s].toBool(), true);
    QCOMPARE(plain[u"occluded"_s].toBool(), false);
    QCOMPARE(plain[u"fragments"_s].toArray().size(), 0);
    // The painted text, not the item: left-aligned at x 10, narrower than the 200-px item.
    const QRectF r = rectOf(plain);
    QVERIFY2(std::abs(r.left() - 10) <= 1, qPrintable(u"left %1"_s.arg(r.left())));
    QVERIFY(r.width() > 40 && r.width() < 190);
    QVERIFY(r.top() >= 10 && r.bottom() <= 31);

    const QRectF right = rectOf(byName(texts, u"right"_s));
    QVERIFY2(std::abs(right.right() - 210) <= 1, qPrintable(u"right %1"_s.arg(right.right())));
    QVERIFY(right.left() > 100);
    const QRectF centre = rectOf(byName(texts, u"centre"_s));
    QVERIFY2(std::abs(centre.center().x() - 110) <= 1, qPrintable(u"centre %1"_s.arg(centre.center().x())));

    const QJsonObject half = byName(texts, u"half"_s);
    QCOMPARE(half[u"alpha"_s].toDouble(), 0.5);
    QCOMPARE(half[u"weight"_s].toInt(), 700);

    const QJsonObject input = byName(texts, u"input"_s);
    QCOMPARE(input[u"text"_s].toString(), u"Pole"_s);
}

void TestUiBenchLayoutDump::richText()
{
    const QJsonObject rich = byName(m_dump[u"texts"_s].toArray(), u"rich"_s);
    QVERIFY(!rich.isEmpty());
    QCOMPARE(rich[u"text"_s].toString(), u"Zobacz link do strony i #tag"_s);
    const QJsonArray fragments = rich[u"fragments"_s].toArray();
    QVERIFY(fragments.size() >= 3);
    bool link = false, chip = false;
    for (const QJsonValue &f : fragments) {
        if (f[u"text"_s].toString() == u"link do strony" && f[u"color"_s].toString() == u"#2c6aa3ff")
            link = true;
        if (f[u"text"_s].toString() == u"#tag" && f[u"bg"_s].toString() == u"#9fc3e652")
            chip = true;
    }
    QVERIFY2(link, qPrintable(QString::fromUtf8(QJsonDocument(fragments).toJson())));
    QVERIFY2(chip, qPrintable(QString::fromUtf8(QJsonDocument(fragments).toJson())));
}

void TestUiBenchLayoutDump::leftOut()
{
    const QJsonArray texts = m_dump[u"texts"_s].toArray();
    QVERIFY(byName(texts, u"hidden"_s).isEmpty());
    QVERIFY(byName(texts, u"transparent"_s).isEmpty());
    QVERIFY(byName(texts, u"clipped"_s).isEmpty());
    const QJsonArray targets = m_dump[u"targets"_s].toArray();
    QVERIFY(byName(targets, u"disabledMouse"_s).isEmpty());
    QVERIFY(byName(targets, u"hoverOnly"_s).isEmpty());
}

void TestUiBenchLayoutDump::targets()
{
    const QJsonArray targets = m_dump[u"targets"_s].toArray();
    const QJsonObject mouse = byName(targets, u"mouse"_s);
    QCOMPARE(mouse[u"kind"_s].toString(), u"mouse"_s);
    QCOMPARE(rectOf(mouse), QRectF(250, 10, 20, 20));
    const QJsonObject button = byName(targets, u"button"_s);
    QCOMPARE(button[u"kind"_s].toString(), u"button"_s);
    QCOMPARE(button[u"label"_s].toString(), u"Przycisk"_s);
    QCOMPARE(button[u"inline"_s].toBool(), false);
    // The MouseArea filling the button is the same target.
    QVERIFY(byName(targets, u"buttonMouse"_s).isEmpty());
    int same = 0;
    for (const QJsonValue &t : targets) {
        if (rectOf(t.toObject()) == QRectF(250, 40, 60, 30))
            ++same;
    }
    QCOMPARE(same, 1);
    QCOMPARE(byName(targets, u"tappable"_s)[u"kind"_s].toString(), u"tap"_s);
    QCOMPARE(byName(targets, u"input"_s)[u"kind"_s].toString(), u"field"_s);
}

void TestUiBenchLayoutDump::occlusion()
{
    const QJsonArray texts = m_dump[u"texts"_s].toArray();
    QCOMPARE(byName(texts, u"covered"_s)[u"occluded"_s].toBool(), true);
    QCOMPARE(byName(texts, u"underDim"_s)[u"occluded"_s].toBool(), false);
    QCOMPARE(byName(texts, u"plain"_s)[u"occluded"_s].toBool(), false);
    QCOMPARE(byName(m_dump[u"targets"_s].toArray(), u"mouse"_s)[u"occluded"_s].toBool(), false);
    QVERIFY(byName(texts, u"inPopup"_s).isEmpty());

    // A modal popup's dim (in the overlay, alpha 0.5) hides the page, not the popup's own text.
    const QJsonArray dimmed = m_dimmed[u"texts"_s].toArray();
    QCOMPARE(byName(dimmed, u"underDim"_s)[u"occluded"_s].toBool(), true);
    QCOMPARE(byName(dimmed, u"plain"_s)[u"occluded"_s].toBool(), true);
    QCOMPARE(byName(m_dimmed[u"targets"_s].toArray(), u"mouse"_s)[u"occluded"_s].toBool(), true);
    const QJsonObject inPopup = byName(dimmed, u"inPopup"_s);
    QVERIFY(!inPopup.isEmpty());
    QCOMPARE(inPopup[u"occluded"_s].toBool(), false);
}

void TestUiBenchLayoutDump::boxes()
{
    const QJsonArray boxes = m_dimmed[u"boxes"_s].toArray();
    bool page = false, cover = false, dim = false;
    int lastPaint = -1;
    for (const QJsonValue &b : boxes) {
        const QRectF r = rectOf(b.toObject());
        if (r == QRectF(0, 0, 400, 400) && b[u"color"_s].toString() == u"#fbf7eeff")
            page = true;
        if (r == QRectF(0, 195, 120, 30) && b[u"color"_s].toString() == u"#ffffffff")
            cover = true;
        if (b[u"color"_s].toString() == u"#00000080")
            dim = true;
        QVERIFY(b[u"paint"_s].toInt() > lastPaint);
        lastPaint = b[u"paint"_s].toInt();
    }
    QVERIFY(page);
    QVERIFY(cover);
    QVERIFY(dim);
}

void TestUiBenchLayoutDump::writeToMissingFolder()
{
    QTemporaryDir dir;
    QString error;
    QVERIFY(!brulion::ui::writeLayoutDump(m_window.get(), dir.filePath(u"missing/folder/x.json"_s), nullptr, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(brulion::ui::writeLayoutDump(m_window.get(), dir.filePath(u"x.json"_s), nullptr, &error));
}

QTEST_MAIN(TestUiBenchLayoutDump)
#include "tst_uibench_layoutdump.moc"
