// QML registration of the canvas view's C++ (library brulion_canvasview, canvas/CMakeLists.txt) in
// the app's QML module "Brulion": CanvasEditor (the board), CanvasCards (what cards show) and the
// lists of the board are creatable / known types (qml/canvas/CanvasView.qml).
#pragma once

#include "canvascards.h"
#include "canvaseditor.h"
#include "canvasmodels.h"

#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

namespace brulion::canvasview {

struct CanvasEditorForeign {
    Q_GADGET
    QML_FOREIGN(brulion::canvasview::CanvasEditor)
    QML_NAMED_ELEMENT(CanvasEditor)
};

struct CanvasCardsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::canvasview::CanvasCards)
    QML_NAMED_ELEMENT(CanvasCards)
};

struct CanvasCardContentForeign {
    Q_GADGET
    QML_FOREIGN(brulion::canvasview::CanvasCardContent)
    QML_NAMED_ELEMENT(CanvasCardContent)
};

/// CanvasColors.hex(model.cardColor, Theme.dark)
struct CanvasColorsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::canvasview::CanvasColors)
    QML_NAMED_ELEMENT(CanvasColors)
    QML_SINGLETON
public:
    static CanvasColors *create(QQmlEngine *, QJSEngine *) { return new CanvasColors; }
};

struct CanvasNodeModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::canvasview::CanvasNodeModel)
    QML_NAMED_ELEMENT(CanvasNodeModel)
    QML_UNCREATABLE("CanvasEditor.nodes")
};

struct CanvasEdgeModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::canvasview::CanvasEdgeModel)
    QML_NAMED_ELEMENT(CanvasEdgeModel)
    QML_UNCREATABLE("CanvasEditor.edges")
};

} // namespace brulion::canvasview
