// GraphItem — draws a GraphModel and handles the interaction (QML: GraphView.qml).
//
//   GraphItem { model: graphModel; pageColor: Theme.link; ... ; onNodeActivated: (id) => open(id) }
//
// Rendering: a QQuickItem with its own scene-graph nodes (GraphSceneNode, graphscene.h): the
// edges, discs and rings as vertex-antialiased triangle meshes in world units under a camera
// transform (QSGVertexColorMaterial, no custom shaders), the labels as QSGTextNodes on rounded
// backgrounds in screen units. Panning a settled graph only changes the camera's matrix (and places
// the labels again); a moving layout, zooming, colours and the highlight rebuild the meshes
// (buildMesh(), during the scene graph's sync). Labels: at sufficient zoom every visible node,
// otherwise the highlighted neighbourhood, the local centre and the best connected nodes, never
// overlapping (except the highlighted one), at most MaxLabels — the same rule for both renderers
// (geometry::placeLabels).
// With the software scene graph renderer (tests, screenshots, the Raspberry Pi's offscreen runs)
// the QPainter code (render()) paints into an image node instead: off-screen nodes and edges are
// culled, discs blitted from cached antialiased sprites, antialiasing off for large graphs while
// things move; when a frame takes longer than slowPaintMs while the layout moves, the moving
// layout is repainted at most every movingRepaintInterval() ms and without labels (except the
// highlighted and the focused node) — gestures and camera moves still repaint at once.
// While the model is `settling` (reduced motion, fresh layout) only the background is drawn.
//
// Interaction (mouse, touch, touchpad):
// * wheel / pinch / touchpad pinch: zoom at the pointer; drag on the background / two-finger
//   drag: pan; drag a node: move it (the simulation follows);
// * hover (mouse): highlight the node and its neighbours (hoveredId); click: nodeActivated(id);
// * touch (`touchMode`): the first tap selects a node (selectedId, neighbours highlighted, the
//   view can show a card), a tap on the selected node activates it; a tap on the background
//   clears the selection (backgroundTapped).
// The camera: zoom (1 = one world unit per pixel) and centerX/centerY (world point at the
// item's centre); fitAll(), centerOn(id), zoomBy(), panBy(). `autoFit` keeps the whole graph in
// view while the layout unfolds, until the user moves the camera.
#pragma once

#include "graphgeometry.h"
#include "graphmodel.h"

#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPointer>
#include <QQuickItem>
#include <QStaticText>
#include <QTimer>
#include <QVariantAnimation>

#include <atomic>
#include <mutex>
#include <vector>

namespace brulion::planning {

class GraphItem : public QQuickItem {
    Q_OBJECT

    Q_PROPERTY(brulion::planning::GraphModel *model READ model WRITE setModel NOTIFY modelChanged FINAL)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY viewChanged FINAL)
    Q_PROPERTY(qreal centerX READ centerX WRITE setCenterX NOTIFY viewChanged FINAL)
    Q_PROPERTY(qreal centerY READ centerY WRITE setCenterY NOTIFY viewChanged FINAL)
    Q_PROPERTY(qreal minZoom READ minZoom WRITE setMinZoom NOTIFY viewChanged FINAL)
    Q_PROPERTY(qreal maxZoom READ maxZoom WRITE setMaxZoom NOTIFY viewChanged FINAL)
    /// Zoom from which every visible node gets a label (default 1.15).
    Q_PROPERTY(qreal labelZoom READ labelZoom WRITE setLabelZoom NOTIFY viewChanged FINAL)
    /// Keep the whole graph in view while it unfolds (until the user moves the camera).
    Q_PROPERTY(bool autoFit READ autoFit WRITE setAutoFit NOTIFY autoFitChanged FINAL)
    /// Camera animations (fitAll, centerOn) in ms; 0 = jump (reduced motion).
    Q_PROPERTY(int animationDuration READ animationDuration WRITE setAnimationDuration NOTIFY animationDurationChanged FINAL)
    /// Room kept free of nodes when fitting (overlays: the toolbar at the top, controls below).
    Q_PROPERTY(qreal fitTopMargin READ fitTopMargin WRITE setFitTopMargin NOTIFY fitMarginsChanged FINAL)
    Q_PROPERTY(qreal fitBottomMargin READ fitBottomMargin WRITE setFitBottomMargin NOTIFY fitMarginsChanged FINAL)
    /// Touch UI: tap selects, a second tap activates; no hover.
    Q_PROPERTY(bool touchMode READ touchMode WRITE setTouchMode NOTIFY touchModeChanged FINAL)

    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor edgeColor READ edgeColor WRITE setEdgeColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor highlightColor READ highlightColor WRITE setHighlightColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor pageColor READ pageColor WRITE setPageColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor journalColor READ journalColor WRITE setJournalColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor tagColor READ tagColor WRITE setTagColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor missingColor READ missingColor WRITE setMissingColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor focusColor READ focusColor WRITE setFocusColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor labelColor READ labelColor WRITE setLabelColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor labelBackground READ labelBackground WRITE setLabelBackground NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged FINAL)

    /// Node under the mouse ("" none).
    Q_PROPERTY(QString hoveredId READ hoveredId NOTIFY hoveredIdChanged FINAL)
    /// Selected node (search result, tap on touch); highlighted when nothing is hovered.
    Q_PROPERTY(QString selectedId READ selectedId WRITE setSelectedId NOTIFY selectedIdChanged FINAL)
    /// Hovered, else selected.
    Q_PROPERTY(QString highlightedId READ highlightedId NOTIFY highlightedIdChanged FINAL)
    /// Item position of the highlighted node (for a tooltip / card), follows the layout.
    Q_PROPERTY(QPointF highlightedPosition READ highlightedPosition NOTIFY highlightedPositionChanged FINAL)
    /// A pointer gesture (pan, pinch, node drag) is in progress.
    Q_PROPERTY(bool interacting READ interacting NOTIFY interactingChanged FINAL)
    /// Milliseconds the last frame took to build (meshes and labels, or painting with the software
    /// renderer; diagnostics; read on demand — this happens on the render thread, so there is no
    /// change signal).
    Q_PROPERTY(double lastPaintMs READ lastPaintMs FINAL)

public:
    static constexpr int MaxLabels = 260;

    explicit GraphItem(QQuickItem *parent = nullptr);
    ~GraphItem() override;

    GraphModel *model() const { return m_model; }
    void setModel(GraphModel *model);

    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    qreal centerX() const { return m_cx; }
    void setCenterX(qreal x);
    qreal centerY() const { return m_cy; }
    void setCenterY(qreal y);
    qreal minZoom() const { return m_minZoom; }
    void setMinZoom(qreal z);
    qreal maxZoom() const { return m_maxZoom; }
    void setMaxZoom(qreal z);
    qreal labelZoom() const { return m_labelZoom; }
    void setLabelZoom(qreal z);
    bool autoFit() const { return m_autoFit; }
    void setAutoFit(bool on);
    int animationDuration() const { return m_animationDuration; }
    void setAnimationDuration(int ms);
    bool touchMode() const { return m_touchMode; }
    void setTouchMode(bool on);
    qreal fitTopMargin() const { return m_fitTop; }
    void setFitTopMargin(qreal margin);
    qreal fitBottomMargin() const { return m_fitBottom; }
    void setFitBottomMargin(qreal margin);

    QColor backgroundColor() const { return m_bg; }
    void setBackgroundColor(const QColor &c);
    QColor edgeColor() const { return m_edge; }
    void setEdgeColor(const QColor &c);
    QColor highlightColor() const { return m_highlight; }
    void setHighlightColor(const QColor &c);
    QColor pageColor() const { return m_page; }
    void setPageColor(const QColor &c);
    QColor journalColor() const { return m_journal; }
    void setJournalColor(const QColor &c);
    QColor tagColor() const { return m_tag; }
    void setTagColor(const QColor &c);
    QColor missingColor() const { return m_missing; }
    void setMissingColor(const QColor &c);
    QColor focusColor() const { return m_focus; }
    void setFocusColor(const QColor &c);
    QColor labelColor() const { return m_label; }
    void setLabelColor(const QColor &c);
    QColor labelBackground() const { return m_labelBg; }
    void setLabelBackground(const QColor &c);
    QFont font() const { return m_font; }
    void setFont(const QFont &font);

    QString hoveredId() const;
    QString selectedId() const;
    void setSelectedId(const QString &id);
    QString highlightedId() const;
    QPointF highlightedPosition() const;
    bool interacting() const { return m_interacting; }
    double lastPaintMs() const { return m_lastPaintMs.load(std::memory_order_relaxed); }
    /// While the layout moves with the software renderer: the least time between two repaints for
    /// a paint of `paintMs` (painting takes at most a quarter of the time), ms.
    static int movingRepaintInterval(double paintMs);
    /// Software renderer: a frame slower than this (ms) while the layout moves throttles repaints
    /// and leaves out labels (default 16; tests: 0).
    void setSlowPaintMs(double ms) { m_slowPaintMs = ms; }

    /// Show the whole graph (animated unless `animated` is false or animationDuration is 0).
    Q_INVOKABLE void fitAll(bool animated = true);
    /// Centre on a node (and zoom to `zoom` when > 0); selects nothing. The camera keeps following
    /// the node while the layout still moves, until the user moves the camera.
    Q_INVOKABLE void centerOn(const QString &id, qreal zoom = -1, bool animated = true);
    /// Zoom by `factor` around the item centre (or around (x, y) when given).
    Q_INVOKABLE void zoomBy(qreal factor, qreal x = -1, qreal y = -1);
    /// Pan by screen pixels.
    Q_INVOKABLE void panBy(qreal dx, qreal dy);
    /// Id of the node at an item position ("" none).
    Q_INVOKABLE QString nodeAt(qreal x, qreal y) const;
    /// Item position of a node (invalid point when unknown).
    Q_INVOKABLE QPointF nodePosition(const QString &id) const;
    /// Back to auto-fit (after a mode change).
    Q_INVOKABLE void resetView();

    /// Draw the graph with QPainter for an item of `size` (the software renderer and tests).
    void render(QPainter *painter, const QSizeF &size);
    /// The world-space meshes of the current state (edges, discs, rings; empty while settling)
    /// for a device pixel ratio (the scene-graph renderer and tests).
    void buildMesh(geometry::MeshChunks &out, qreal dpr) const;

    /// A label to draw: the node, its box (screen coordinates) and whether it is dimmed.
    struct PlacedLabel {
        int node = -1;
        QRectF rect;
        bool dimmed = false;
    };
    /// The labels of a frame of `size` (both renderers; `sparse`: only the highlighted and the
    /// focused node).
    std::vector<PlacedLabel> labelsFor(const QSizeF &size, bool sparse = false);
    /// The size of a node's label text (elided to the maximum width, the item's font).
    QSizeF labelSize(int node);

    /// What the last frames did (tests, diagnostics; any thread).
    struct SceneStats {
        int frames = 0;      ///< updatePaintNode calls
        int meshBuilds = 0;  ///< world meshes built (scene graph)
        int chunks = 0;      ///< geometry chunks of the world meshes
        int vertices = 0;    ///< their vertices
        int labels = 0;      ///< labels in the last frame
        bool software = false; ///< the last frame was painted with QPainter (software renderer)
        QList<QRectF> labelRects; ///< label boxes of the last frame (either renderer)
    };
    SceneStats sceneStats() const;

Q_SIGNALS:
    void modelChanged();
    void viewChanged();
    void autoFitChanged();
    void animationDurationChanged();
    void touchModeChanged();
    void fitMarginsChanged();
    void colorsChanged();
    void fontChanged();
    void hoveredIdChanged();
    void selectedIdChanged();
    void highlightedIdChanged();
    void highlightedPositionChanged();
    void interactingChanged();
    /// A node was clicked (mouse) or tapped while selected (touch).
    void nodeActivated(const QString &id);
    /// A node was tapped (touch: it is now selected).
    void nodeTapped(const QString &id);
    void backgroundTapped();
    /// The user moved the camera (wheel, pinch, pan) — autoFit is off now.
    void userMovedView();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void touchEvent(QTouchEvent *event) override;
    void touchUngrabEvent() override;
    bool event(QEvent *event) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override;

private:
    QPointF toScreen(double x, double y) const;
    QPointF toWorld(QPointF screen) const;
    int nodeIndexAt(QPointF pos) const;
    void setHovered(int node);
    void setSelected(int node);
    void setInteracting(bool on);
    void onPositionsChanged();
    /// update() — throttled while the layout moves and the software renderer paints slowly (see
    /// movingRepaintInterval()).
    void repaintMoving();
    /// Something the meshes show changed: rebuild them at the next sync.
    void meshChanged();
    QSGNode *updateImageNode(QSGNode *old, QQuickWindow *window);
    QSGNode *updateSceneNode(QSGNode *old, QQuickWindow *window);
    /// Screen positions and visibility of the nodes, the highlight and its neighbourhood.
    struct Frame {
        int n = 0;
        std::vector<QPointF> pos;
        std::vector<char> visible;
        int hl = -1;
        std::vector<char> hood;
        bool dimming = false;
        int focus = -1;
    };
    Frame frameFor(const QSizeF &size) const;
    std::vector<PlacedLabel> labelsFor(const Frame &frame, bool sparse);
    QString labelText(int node);
    void onStructureChanged();
    void applyAutoFit();
    void stopCameraAnimation();
    void animateCamera(double cx, double cy, double zoom, bool animated);
    void userMoved();
    void pressAt(QPointF pos);
    void moveTo(QPointF pos);
    void releaseAt(QPointF pos);
    void emitHighlightPosition();
    const QStaticText &labelFor(int node);
    /// A cached antialiased disc (or ring, for missing pages) of `radius` px.
    const QImage &spriteFor(const QColor &color, bool ring, double radius, qreal dpr);

    QPointer<GraphModel> m_model;
    double m_zoom = 1;
    double m_cx = 0;
    double m_cy = 0;
    double m_minZoom = 0.04;
    double m_maxZoom = 6;
    double m_labelZoom = 1.15;
    bool m_autoFit = true;
    int m_animationDuration = 280;
    bool m_touchMode = false;
    double m_fitTop = 36;
    double m_fitBottom = 36;
    /// Zoom and centre that show all nodes inside the fit margins.
    void fitCamera(double &cx, double &cy, double &zoom) const;

    QColor m_bg = QColor(u"#fbf7ee");
    QColor m_edge = QColor(u"#cdc2ab");
    QColor m_highlight = QColor(u"#e2574c");
    QColor m_page = QColor(u"#2c6aa3");
    QColor m_journal = QColor(u"#535a6a");
    QColor m_tag = QColor(u"#2a6e44");
    QColor m_missing = QColor(u"#7a5ea3");
    QColor m_focus = QColor(u"#e2574c");
    QColor m_label = QColor(u"#1f2430");
    QColor m_labelBg = QColor(251, 247, 238, 200);
    QFont m_font;

    // Node indices (into the model) of hover / selection; ids survive rebuilds.
    int m_hovered = -1;
    int m_selected = -1;
    QString m_hoveredIdCache;
    QString m_selectedIdCache;
    QString m_lastHighlighted;

    // Pointer state.
    enum class Gesture { None, Pending, Pan, DragNode, Pinch };
    Gesture m_gesture = Gesture::None;
    QPointF m_pressPos;
    QPointF m_lastPos;
    int m_pressNode = -1;
    bool m_interacting = false;
    // Pinch.
    double m_pinchStartDistance = 0;
    double m_pinchStartZoom = 1;
    QPointF m_pinchWorldCenter;

    QVariantAnimation m_camera;
    QString m_follow; ///< node id the camera follows (centerOn), "" none
    double m_camFrom[3] = {0, 0, 1};
    double m_camTo[3] = {0, 0, 1};

    QHash<int, QStaticText> m_labels;       ///< QPainter path: prepared label texts
    QHash<quint64, QImage> m_sprites;
    struct LabelText {
        QString text; ///< elided
        QSizeF size;
    };
    QHash<int, LabelText> m_labelTexts;    ///< both paths (the GUI thread, or the render thread while it waits)
    std::atomic<double> m_lastPaintMs{0};
    double m_slowPaintMs = 16;
    QElapsedTimer m_clock;                  ///< started at construction (any thread reads it)
    std::atomic<qint64> m_lastPaintEnd{-1}; ///< m_clock time at the end of the last paint, ns
    QTimer m_repaintTimer;                  ///< a throttled repaint

    // Scene graph state: written on the GUI thread, read during the sync (the GUI thread waits).
    bool m_meshDirty = true;
    bool m_labelsDirty = true; ///< node indices or the font changed: cached text layouts go
    geometry::MeshChunks m_mesh;       ///< render thread: world meshes (allocations reused)
    geometry::MeshChunks m_labelMesh;  ///< render thread: label backgrounds
    mutable std::mutex m_statsMutex;
    SceneStats m_stats;
};

} // namespace brulion::planning
