// QML registration of the drawing editor's C++ (library brulion_drawing, drawing/CMakeLists.txt)
// in the app's QML module "Brulion": DrawingCanvas is a creatable type (qml/drawing/DrawingView.qml);
// DrawingFonts the fonts its text is drawn in (and the previews': drawingrender.h setTextFonts).
#pragma once

#include "drawingcanvas.h"
#include "drawingrender.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace brulion::drawingview {

struct DrawingCanvasForeign {
    Q_GADGET
    QML_FOREIGN(brulion::drawingview::DrawingCanvas)
    QML_NAMED_ELEMENT(DrawingCanvas)
};

/// DrawingFonts.set(Theme.fontUi, Theme.fontMono): text in drawings in the app's fonts (Excalidraw's
/// monospace families in the monospace one); DrawingFonts.family(n) the one of Excalidraw's family n.
class DrawingFonts : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;
    Q_INVOKABLE void set(const QString &sans, const QString &mono) { setTextFonts(sans, mono); }
    Q_INVOKABLE QString family(int excalidrawFamily) const { return textFontFamily(excalidrawFamily); }
};

} // namespace brulion::drawingview
