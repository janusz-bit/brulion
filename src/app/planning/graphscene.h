// GraphSceneNode — the scene-graph side of GraphItem (render thread, during the scene graph's
// sync while the GUI thread waits). The geometry itself is built by GraphItem::buildMesh() and
// graphgeometry.h; this node only holds it:
//
//   GraphSceneNode
//    ├─ QSGRectangleNode background                (item rect)
//    ├─ QSGTransformNode camera                    (translate(w/2, h/2) · scale(zoom) · translate(−cx, −cy))
//    │   └─ QSGGeometryNode × chunks               (edges, discs, rings in world units; QSGVertexColorMaterial)
//    └─ labels                                     (screen coordinates)
//        ├─ QSGGeometryNode × chunks               (rounded label backgrounds)
//        └─ QSGTextNode per label                  (its matrix places it; QTextLayout cached per node)
//
// Panning only changes the camera's matrix and places the labels again; positions, zoom, colours,
// highlight and structure rebuild the meshes (GraphItem decides). Nodes are owned by the scene
// graph (OwnedByParent), geometry and materials by their nodes.
#pragma once

#include "graphgeometry.h"

#include <QColor>
#include <QFont>
#include <QMatrix4x4>
#include <QSGNode>
#include <QString>

#include <memory>
#include <unordered_map>
#include <vector>

class QQuickWindow;
class QSGGeometryNode;
class QSGRectangleNode;
class QSGTextNode;
class QSGTransformNode;
class QTextLayout;

namespace brulion::planning {

class GraphSceneNode : public QSGNode {
public:
    explicit GraphSceneNode(QQuickWindow *window);
    ~GraphSceneNode() override;

    struct Label {
        int node = -1;
        QString text; ///< the (elided) label text
        QPointF pos;  ///< top-left of the text, screen coordinates
        bool dimmed = false;
    };

    void setBackground(const QRectF &rect, const QColor &color);
    void setCamera(const QMatrix4x4 &matrix);
    /// World-space meshes in drawing order (under the camera).
    void setWorldMeshes(const geometry::MeshChunks &chunks);
    /// Screen-space label backgrounds.
    void setLabelBackgrounds(const geometry::MeshChunks &chunks);
    /// The labels of this frame (others are removed). `window` creates text nodes.
    void setLabels(QQuickWindow *window, const std::vector<Label> &labels, const QFont &font, const QColor &color,
                   const QColor &dimmedColor, qreal dpr);
    /// Node indices changed (a rebuild): cached text layouts and text nodes go.
    void clearLabels();

    /// Labels shown in the last frame.
    int labelCount() const { return int(m_shown); }
    int chunkCount() const { return int(m_world.size()); }

    // What the meshes were built for (GraphItem compares and rebuilds when they differ).
    double meshZoom = -1;
    qreal meshDpr = -1;

private:
    struct LabelNode {
        QSGTextNode *node = nullptr;
        std::unique_ptr<QTextLayout> layout;
        QString text;
        QColor color;
        bool used = false;
    };

    static void syncChunks(QSGNode *parent, std::vector<QSGGeometryNode *> &nodes, const geometry::MeshChunks &chunks);

    QSGRectangleNode *m_background = nullptr;
    QSGTransformNode *m_camera = nullptr;
    QSGNode *m_labelRoot = nullptr;
    QSGNode *m_labelBackgroundRoot = nullptr;
    QSGNode *m_labelTextRoot = nullptr;
    std::vector<QSGGeometryNode *> m_world;
    std::vector<QSGGeometryNode *> m_labelBackgrounds;
    std::unordered_map<int, LabelNode> m_labels; ///< by graph node index (shown ones have a text node)
    qsizetype m_shown = 0;
    QFont m_font;
};

} // namespace brulion::planning
