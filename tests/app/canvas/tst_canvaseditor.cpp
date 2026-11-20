// CanvasEditor (src/app/canvas): a canvas file loaded and written back as it was where nothing
// changed, the edits and their undo, the view (fit, zoom at a point, the cards listed in view only),
// mouse gestures in a window — a click selects, Shift adds, a drag on empty canvas draws a rubber
// band, a drag moves the selection (a group with its cards), a corner resizes, a side handle
// connects (onto a card; onto empty canvas: a new card) —, the wheel, an external version taken in
// as it is or merged with what changed here, pastes of fragments and text, the picture of a canvas
// (CanvasImageProvider). Offscreen.
#include "canvaseditor.h"
#include "canvasimageprovider.h"
#include "canvasmodels.h"
#include "fixtures.h"

#include "canvas/canvas.h"
#include "canvas/edit.h"
#include "canvas/geometry.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

using namespace brulion;
using namespace brulion::canvasview;
using namespace Qt::StringLiterals;

namespace {

/// A board in a window of 800 × 600, the canvas `text` loaded.
struct Board {
    QQuickWindow window;
    CanvasEditor *editor = nullptr;

    explicit Board(const QString &text)
    {
        window.resize(800, 600);
        editor = new CanvasEditor(window.contentItem());
        editor->setSize(QSizeF(800, 600));
        editor->setAnimate(false);
        window.show();
        if (!QTest::qWaitForWindowExposed(&window))
            qWarning("the window was not exposed");
        editor->load(text.toUtf8());
    }
    QPointF screen(qreal x, qreal y) const { return editor->toScreen(x, y); }
    QString file() const { return QString::fromUtf8(editor->fileBytes()); }
};

void send(QQuickWindow &w, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons,
          Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent e(type, pos, w.mapToGlobal(pos), button, buttons, modifiers);
    QGuiApplication::sendEvent(&w, &e);
}

/// A drag with the left button (the pointer moved to `from` first).
void drag(QQuickWindow &w, QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    send(w, QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton, modifiers);
    send(w, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, modifiers);
    for (int i = 1; i <= 8; ++i)
        send(w, QEvent::MouseMove, from + (to - from) * (i / 8.0), Qt::NoButton, Qt::LeftButton, modifiers);
    send(w, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, modifiers);
}

void click(QQuickWindow &w, QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
           Qt::MouseButton button = Qt::LeftButton)
{
    send(w, QEvent::MouseButtonPress, at, button, button, modifiers);
    send(w, QEvent::MouseButtonRelease, at, button, Qt::NoButton, modifiers);
}

QStringList listed(CanvasNodeModel *model)
{
    QStringList out;
    for (int i = 0; i < model->rowCount(); ++i)
        out.append(model->data(model->index(i), CanvasNodeModel::NodeIdRole).toString());
    return out;
}

} // namespace

class TestCanvasEditor : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void loadAndWriteBack()
    {
        Board b(canvasfixtures::obsidian());
        QCOMPARE(b.editor->nodeCount(), 6);
        QCOMPARE(b.editor->edgeCount(), 3);
        QVERIFY(!b.editor->modified());
        QCOMPARE(b.file(), canvasfixtures::obsidian());
        // The whole canvas in view, at most 100 %.
        QVERIFY(b.editor->zoom() <= 1.0);
        QVERIFY(b.editor->viewRect().contains(canvas::bounds(b.editor->document())));
        QCOMPARE(listed(b.editor->nodes()).size(), 6);
        QCOMPARE(b.editor->edges()->rowCount(), 3);
        // Not a canvas: said so, and nothing to write.
        Board broken(u"{\"nodes\":"_s);
        QCOMPARE(broken.editor->load(QByteArray("{\"nodes\":")), u"corrupt"_s);
        QCOMPARE(broken.editor->nodeCount(), 0);
        QCOMPARE(broken.editor->load(QByteArray()), QString()); // an empty file: an empty canvas
    }

    void editsAndUndo()
    {
        Board b(canvasfixtures::specSample());
        QSignalSpy content(b.editor, &CanvasEditor::contentChanged);
        const QString id = b.editor->addText(0, 0, u"Zażółć gęślą jaźń"_s);
        QVERIFY(!id.isEmpty());
        QVERIFY(b.editor->modified());
        QCOMPARE(b.editor->selectedNodes(), QStringList{id});
        QVERIFY(content.count() >= 1);
        QVERIFY(b.file().contains(u"\"text\":\"Zażółć gęślą jaźń\""_s));
        b.editor->colorSelection(u"3"_s);
        QCOMPARE(b.editor->selectionColor(), u"3"_s);
        b.editor->setNodeTextFitted(id, u"Krótko"_s, 300);
        QCOMPARE(b.editor->nodeData(id).value(u"height"_s).toDouble(), 300.0);
        b.editor->undo();
        QCOMPARE(b.editor->nodeData(id).value(u"text"_s).toString(), u"Zażółć gęślą jaźń"_s);
        b.editor->undo();
        b.editor->undo();
        QCOMPARE(b.editor->nodeCount(), 5);
        QCOMPARE(b.file(), canvasfixtures::specSample()); // back where it started, byte for byte
        b.editor->redo();
        QCOMPARE(b.editor->nodeCount(), 6);
        // Group, ungroup, duplicate, delete.
        b.editor->select({u"7efdbbe0c4742315"_s, u"59e896bc8da20699"_s});
        const QString group = b.editor->addGroup(u"Etap"_s);
        QVERIFY(b.editor->nodeData(group).value(u"type"_s).toString() == u"group");
        QCOMPARE(b.editor->selectionKind(), u"group"_s);
        b.editor->ungroupSelection();
        QVERIFY(b.editor->nodeData(group).isEmpty());
        b.editor->select({u"7efdbbe0c4742315"_s});
        b.editor->duplicateSelection();
        QCOMPARE(b.editor->nodeCount(), 7);
        b.editor->deleteSelection();
        QCOMPARE(b.editor->nodeCount(), 6);
        // A connection between two cards and its arrowheads.
        const QString edge = b.editor->connectCards(u"8132d4d894c80022"_s, u"0ba565e7f30e0652"_s);
        QVERIFY(!edge.isEmpty());
        b.editor->select({}, {edge});
        QVERIFY(b.editor->selectionArrowEnd());
        b.editor->setArrows(true, true);
        QVERIFY(b.editor->selectionArrowStart());
        b.editor->setEdgeLabel(edge, u"zależność"_s);
        QCOMPARE(b.editor->edgeData(edge).value(u"label"_s).toString(), u"zależność"_s);
    }

    void viewAndVisibleCards()
    {
        Board b(canvasfixtures::obsidian());
        // Zoomed into one card: the cards far from it are not listed.
        b.editor->focusNode(u"abcdefabcdefabcd"_s);
        b.editor->setZoom(2);
        const QStringList shown = listed(b.editor->nodes());
        QVERIFY(shown.contains(u"abcdefabcdefabcd"_s));
        QVERIFY(!shown.contains(u"99aabbccddeeff00"_s));
        // Zoom at a point keeps that point where it is.
        const QPointF at(200, 150);
        const QPointF before = b.editor->toCanvas(at.x(), at.y());
        b.editor->zoomBy(3, at.x(), at.y());
        const QPointF after = b.editor->toCanvas(at.x(), at.y());
        QVERIFY(qAbs(before.x() - after.x()) < 0.01 && qAbs(before.y() - after.y()) < 0.01);
        QCOMPARE(b.editor->zoom(), CanvasEditor::MaxZoom); // 2 × 3 is past the limit
        b.editor->zoomToFit();
        QCOMPARE(listed(b.editor->nodes()).size(), 6);
        // A search hit's line: its card selected and in view.
        QVERIFY(b.editor->focusLine(4));
        QCOMPARE(b.editor->selectedNodes(), QStringList{u"a1b2c3d4e5f60718"_s});
    }

    void mouseGestures()
    {
        Board b(canvasfixtures::specSample());
        QQuickWindow &w = b.window;
        // A click selects the card under it; Shift adds another; a click on empty canvas clears.
        click(w, b.screen(-100, 0)); // readme.md
        QCOMPARE(b.editor->selectedNodes(), QStringList{u"8132d4d894c80022"_s});
        click(w, b.screen(500, -200), Qt::ShiftModifier); // spec/1.0.md
        QCOMPARE(b.editor->selectionCount(), 2);
        click(w, b.screen(-200, 420));
        QCOMPARE(b.editor->selectionCount(), 0);
        // A rubber band from empty canvas over two cards.
        drag(w, b.screen(-320, -470), b.screen(300, -270));
        QCOMPARE(b.editor->selectedNodeSet(), (QSet<QString>{u"7efdbbe0c4742315"_s, u"59e896bc8da20699"_s}));
        // A drag moves the selection: one undo step, saved positions whole numbers.
        const qreal zoom = b.editor->zoom();
        drag(w, b.screen(-200, -400), b.screen(-200, -400) + QPointF(50 * zoom, 20 * zoom));
        QCOMPARE(b.editor->nodeData(u"7efdbbe0c4742315"_s).value(u"x"_s).toDouble(), -230.0);
        QCOMPARE(b.editor->nodeData(u"59e896bc8da20699"_s).value(u"y"_s).toDouble(), -420.0);
        QVERIFY(b.editor->canUndo());
        b.editor->undo();
        QCOMPARE(b.editor->nodeData(u"7efdbbe0c4742315"_s).value(u"x"_s).toDouble(), -280.0);
        // Moving a group moves the cards inside it.
        click(w, b.screen(-200, -455)); // the group's frame
        QCOMPARE(b.editor->selectedNodes(), QStringList{u"754a8ef995f366bc"_s});
        drag(w, b.screen(-200, -455), b.screen(-200, -455) + QPointF(0, 40 * zoom));
        QCOMPARE(b.editor->nodeData(u"754a8ef995f366bc"_s).value(u"y"_s).toDouble(), -420.0);
        QCOMPARE(b.editor->nodeData(u"7efdbbe0c4742315"_s).value(u"y"_s).toDouble(), -400.0);
        b.editor->undo();
        // A corner of the one selected card resizes it.
        click(w, b.screen(400, -300)); // spec/1.0.md: 360, -400, 400 × 400
        drag(w, b.screen(760, 0), b.screen(760, 0) + QPointF(-100 * zoom, -50 * zoom));
        QCOMPARE(b.editor->nodeData(u"0ba565e7f30e0652"_s).value(u"width"_s).toDouble(), 300.0);
        QCOMPARE(b.editor->nodeData(u"0ba565e7f30e0652"_s).value(u"height"_s).toDouble(), 350.0);
        // A side handle dragged onto another card connects them.
        const int edges = b.editor->edgeCount();
        click(w, b.screen(-150, -400)); // logo.svg selected: its side handles connect
        drag(w, b.screen(-63, -400), b.screen(500, -200)); // logo.svg's right side onto spec/1.0.md
        QCOMPARE(b.editor->edgeCount(), edges + 1);
        QVERIFY(b.file().contains(u"\"fromNode\":\"7efdbbe0c4742315\",\"fromSide\":\"right\",\"toNode\":\"0ba565e7f30e0652\""_s));
        // … and onto empty canvas: a new card there, connected, to type into.
        QSignalSpy edit(b.editor, &CanvasEditor::editRequested);
        const int nodes = b.editor->nodeCount();
        click(w, b.screen(-150, -400));
        drag(w, b.screen(-280, -400), b.screen(-700, -400)); // logo.svg's left side
        QCOMPARE(b.editor->nodeCount(), nodes + 1);
        QCOMPARE(b.editor->edgeCount(), edges + 2);
        QCOMPARE(edit.count(), 1);
        QCOMPARE(b.editor->selectedNodes(), QStringList{edit.at(0).at(0).toString()});
        // The right button asks for the menu of the card under it.
        QSignalSpy menu(b.editor, &CanvasEditor::contextMenuRequested);
        click(w, b.screen(-100, 0), Qt::NoModifier, Qt::RightButton);
        QCOMPARE(menu.count(), 1);
        QCOMPARE(menu.at(0).at(0).toString(), u"8132d4d894c80022"_s);
    }

    void wheel()
    {
        Board b(canvasfixtures::specSample());
        const qreal panY = b.editor->panY();
        QWheelEvent scroll(QPointF(400, 300), b.window.mapToGlobal(QPointF(400, 300)), QPoint(), QPoint(0, -120),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(&b.window, &scroll);
        QVERIFY(b.editor->panY() < panY);
        const qreal zoom = b.editor->zoom();
        QWheelEvent zoomIn(QPointF(400, 300), b.window.mapToGlobal(QPointF(400, 300)), QPoint(), QPoint(0, 120),
                           Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(&b.window, &zoomIn);
        QVERIFY(b.editor->zoom() > zoom);
    }

    void externalVersions()
    {
        const QString text = canvasfixtures::obsidian();
        Board b(text);
        // Nothing changed here: taken as it is.
        QString theirs = text;
        theirs.replace(u"\"label\":\"Remont kuchni\""_s, u"\"label\":\"Remont łazienki\""_s);
        QVERIFY(b.editor->takeExternal(theirs.toUtf8()));
        QCOMPARE(b.editor->nodeData(u"5b4c2f0e9a1d3c7e"_s).value(u"label"_s).toString(), u"Remont łazienki"_s);
        QVERIFY(!b.editor->modified());
        QVERIFY(b.editor->isKnownFile(theirs.toUtf8()));
        // Changed here: merged card by card.
        b.editor->setNodeText(u"a1b2c3d4e5f60718"_s, u"Tutaj"_s);
        QString again = theirs;
        again.replace(u"\"url\":\"https://example.com/żółw\""_s, u"\"url\":\"https://example.org\""_s);
        QVERIFY(b.editor->takeExternal(again.toUtf8()));
        QVERIFY(b.editor->modified());
        QCOMPARE(b.editor->nodeData(u"a1b2c3d4e5f60718"_s).value(u"text"_s).toString(), u"Tutaj"_s);
        QCOMPARE(b.editor->nodeData(u"abcdefabcdefabcd"_s).value(u"url"_s).toString(), u"https://example.org"_s);
        // Saved: the file written is what later versions are merged against.
        const QByteArray bytes = b.editor->fileBytes();
        b.editor->markSaved(bytes);
        QVERIFY(!b.editor->modified());
        QVERIFY(b.editor->isKnownFile(bytes));
        // No canvas any more: refused.
        QVERIFY(!b.editor->takeExternal(QByteArray("nie JSON")));
    }

    void pastes()
    {
        Board b(canvasfixtures::specSample());
        const canvas::Canvas source = *canvas::readCanvas(canvasfixtures::obsidian()).canvas;
        const QString fragment = canvas::fragmentText(source, {u"a1b2c3d4e5f60718"_s, u"0f1e2d3c4b5a6978"_s});
        QVariantMap r = b.editor->pasteText(fragment, 400, 300);
        QCOMPARE(r.value(u"kind"_s).toString(), u"cards"_s);
        QCOMPARE(r.value(u"pasted"_s).toInt(), 2);
        QCOMPARE(b.editor->nodeCount(), 7);
        QCOMPARE(b.editor->edgeCount(), 2); // the edge between them came along
        r = b.editor->pasteText(u"https://jsoncanvas.org"_s, 100, 100);
        QCOMPARE(r.value(u"kind"_s).toString(), u"link"_s);
        r = b.editor->pasteText(u"Notatka z [[readme]]"_s, 100, 100);
        QCOMPARE(r.value(u"kind"_s).toString(), u"text"_s);
        QCOMPARE(b.editor->nodeData(b.editor->selectedNodes().value(0)).value(u"text"_s).toString(),
                 u"Notatka z [[readme]]"_s);
        QCOMPARE(b.editor->pasteText(u"   "_s, 0, 0).value(u"pasted"_s).toInt(), 0);
    }

    void exportAndPictures()
    {
        Board b(canvasfixtures::obsidian());
        const QByteArray svg = b.editor->exportPicture(u"svg"_s, false);
        QVERIFY(svg.startsWith("<svg"));
        QVERIFY(svg.contains("Remont kuchni"));
        const QByteArray png = b.editor->exportPicture(u"png"_s, true);
        QVERIFY(png.startsWith("\x89PNG"));
        QSize natural;
        const QImage picture = CanvasImageProvider::picture(canvasfixtures::obsidian(), false, QSize(400, 0), &natural);
        QCOMPARE(picture.width(), 400);
        QVERIFY(natural.width() > 1000);
        QVERIFY(CanvasImageProvider::picture(u"nie JSON"_s, false, QSize()).isNull());
        QVERIFY(CanvasImageProvider::url(u"/tmp/a b.canvas"_s, true).toString().startsWith(u"image://canvas/"_s));
    }
};

QTEST_MAIN(TestCanvasEditor)
#include "tst_canvaseditor.moc"
