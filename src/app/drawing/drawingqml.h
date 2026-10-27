// QML registration of the drawing editor's C++ (library brulion_drawing, drawing/CMakeLists.txt)
// in the app's QML module "Brulion": DrawingCanvas is a creatable type (qml/drawing/DrawingView.qml).
#pragma once

#include "drawingcanvas.h"

#include <QtQml/qqmlregistration.h>

namespace brulion::drawingview {

struct DrawingCanvasForeign {
    Q_GADGET
    QML_FOREIGN(brulion::drawingview::DrawingCanvas)
    QML_NAMED_ELEMENT(DrawingCanvas)
};

} // namespace brulion::drawingview
