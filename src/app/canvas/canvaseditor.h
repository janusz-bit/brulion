// CanvasEditor — the canvas view's board (docs/UI.md "Canvas view"): one canvas of the core module
// `canvas` (JSON Canvas), its undo and redo, the view onto it (pan and zoom), the selection and
// every pointer gesture on it. A QQuickItem without content of its own: it lies under the cards
// (QML delegates of `nodes`, placed in canvas units in an item scaled by `zoom` and moved by
// `panX` / `panY`) and gets the presses nothing above it takes.
//
//   CanvasEditor { id: board; host: Workspace; path: route.path }
//   board.load(Workspace.readAttachment(path))      // "" | "corrupt" | "tooLarge"
//   Workspace.writeAttachment(path, board.fileBytes()); board.markSaved(bytes)
//
// Input (mouse): a press on a card selects it (Shift / Ctrl add or remove it) and a drag moves the
// selection — a group with the cards inside it —; a press on empty canvas draws a rubber band
// (Shift adds); the middle button, or Space held, moves the canvas; a drag from one of the four
// handles on the sides of the card under the pointer connects it to the card it ends on (or to a
// new text card where it ends on empty canvas); the corners and sides of the one selected card
// resize it; the wheel and a touchpad's two fingers move the canvas, Ctrl + wheel and a touchpad's
// pinch zoom at the pointer; a click on an edge selects it. Touch: one finger moves the canvas, or
// the selection when it starts on a selected card (or a handle: connects, resizes); two fingers
// move and zoom; a tap selects; a long press opens the menu; `selectMode` — taps add to the
// selection and a drag draws a rubber band. Double click / tap and the keys are the view's (QML):
// doubleTapped(), the commands below.
//
// Every change is one undo step (a gesture's at its end); external versions are merged node by
// node (takeExternal(): canvas::merge against the version last loaded or saved).
#pragma once

#include "canvas/canvas.h"
#include "canvasmodels.h"
#include "editorhost.h"

#include <QElapsedTimer>
#include <QPointF>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QSet>
#include <QTimer>
#include <QVariantAnimation>
#include <QVariantMap>

#include <optional>

namespace brulion::canvasview {

class CanvasEditor : public QQuickItem {
    Q_OBJECT
    /// The notebook (pastes store pictures through it, file cards are classified by it).
    Q_PROPERTY(brulion::EditorHost *host READ host WRITE setHost NOTIFY hostChanged FINAL)
    /// The canvas file's vault path (relative links of pasted files, new cards' file paths).
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged FINAL)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged FINAL)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged FINAL)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged FINAL)
    Q_PROPERTY(int nodeCount READ nodeCount NOTIFY contentChanged FINAL)
    Q_PROPERTY(int edgeCount READ edgeCount NOTIFY contentChanged FINAL)
    /// The canvas grows with every change: bumped by each (QML bindings that read the document).
    Q_PROPERTY(int revision READ revision NOTIFY contentChanged FINAL)
    /// screen = canvas × zoom + (panX, panY), in this item's coordinates.
    Q_PROPERTY(qreal zoom READ zoom NOTIFY viewChanged FINAL)
    Q_PROPERTY(qreal panX READ panX NOTIFY viewChanged FINAL)
    Q_PROPERTY(qreal panY READ panY NOTIFY viewChanged FINAL)
    /// The part of the canvas in view (canvas units).
    Q_PROPERTY(QRectF viewRect READ viewRect NOTIFY viewChanged FINAL)
    /// Cards are shown in full (zoomed in enough); else as their colour and title only.
    Q_PROPERTY(bool detailed READ detailed NOTIFY detailedChanged FINAL)
    /// Moves of the view (zoom to fit, to the selection, to a card) are animated.
    Q_PROPERTY(bool animate READ animate WRITE setAnimate NOTIFY animateChanged FINAL)
    /// The cards and edges in or near the view, in z-order (CanvasNodeModel, CanvasEdgeModel).
    Q_PROPERTY(brulion::canvasview::CanvasNodeModel *nodes READ nodes CONSTANT FINAL)
    Q_PROPERTY(brulion::canvasview::CanvasEdgeModel *edges READ edges CONSTANT FINAL)
    Q_PROPERTY(QStringList selectedNodes READ selectedNodes NOTIFY selectionChanged FINAL)
    Q_PROPERTY(QStringList selectedEdges READ selectedEdges NOTIFY selectionChanged FINAL)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged FINAL)
    /// The selected cards' bounds on screen (this item's coordinates; empty: none).
    Q_PROPERTY(QRectF selectionScreenRect READ selectionScreenRect NOTIFY selectionGeometryChanged FINAL)
    /// What is selected: "" none, "text", "file", "link", "group", "edge", "mixed" (several kinds).
    Q_PROPERTY(QString selectionKind READ selectionKind NOTIFY selectionChanged FINAL)
    /// The selection's colour when all of it has one ("" none or several).
    Q_PROPERTY(QString selectionColor READ selectionColor NOTIFY selectionChanged FINAL)
    /// The selected edges' arrowheads (all of them alike: else false).
    Q_PROPERTY(bool selectionArrowStart READ selectionArrowStart NOTIFY selectionChanged FINAL)
    Q_PROPERTY(bool selectionArrowEnd READ selectionArrowEnd NOTIFY selectionChanged FINAL)
    /// The card under the pointer (its side handles are shown), "" none, and its rectangle (canvas
    /// units; empty for none).
    Q_PROPERTY(QString hoverNode READ hoverNode NOTIFY hoverChanged FINAL)
    Q_PROPERTY(QRectF hoverRect READ hoverRect NOTIFY hoverChanged FINAL)
    /// The one selected card's rectangle (canvas units; empty unless exactly one card and no edge
    /// is selected): its resize handles.
    Q_PROPERTY(QRectF singleRect READ singleRect NOTIFY selectionGeometryChanged FINAL)
    /// The rubber band on screen (empty: none).
    Q_PROPERTY(QRectF rubberBand READ rubberBand NOTIFY gestureChanged FINAL)
    /// An edge being dragged out of a card: its curve and arrowhead as SVG paths (canvas units),
    /// the card it would connect to ("" none).
    Q_PROPERTY(bool connecting READ connecting NOTIFY gestureChanged FINAL)
    Q_PROPERTY(QString connectPath READ connectPath NOTIFY gestureChanged FINAL)
    Q_PROPERTY(QString connectArrow READ connectArrow NOTIFY gestureChanged FINAL)
    Q_PROPERTY(QString connectTarget READ connectTarget NOTIFY gestureChanged FINAL)
    /// A gesture is in progress (the view keeps its toolbars still).
    Q_PROPERTY(bool busy READ busy NOTIFY gestureChanged FINAL)
    /// Space held: the mouse moves the canvas.
    Q_PROPERTY(bool panMode READ panMode WRITE setPanMode NOTIFY panModeChanged FINAL)
    /// Touch: taps add to the selection, a drag draws a rubber band.
    Q_PROPERTY(bool selectMode READ selectMode WRITE setSelectMode NOTIFY selectModeChanged FINAL)
    /// A card's text is being edited in the view (its node id): no gestures start on it.
    Q_PROPERTY(QString editingNode READ editingNode WRITE setEditingNode NOTIFY editingNodeChanged FINAL)
    /// Screen pixels of the side handles' and edges' hit areas (larger on touch screens).
    Q_PROPERTY(qreal handleRadius READ handleRadius WRITE setHandleRadius NOTIFY handleRadiusChanged FINAL)

public:
    static constexpr double MinZoom = 0.05;
    static constexpr double MaxZoom = 4;
    /// Below this zoom cards show only their colour and title.
    static constexpr double DetailZoom = 0.3;
    /// Undo steps kept.
    static constexpr int MaxUndo = 200;
    /// Plain text pasted as a card: at most this many characters.
    static constexpr qsizetype MaxPastedText = 200'000;

    explicit CanvasEditor(QQuickItem *parent = nullptr);
    ~CanvasEditor() override;

    EditorHost *host() const { return m_host; }
    void setHost(EditorHost *host);
    QString path() const { return m_path; }
    void setPath(const QString &path);
    bool modified() const { return m_modified; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    int nodeCount() const { return int(m_canvas.nodes.size()); }
    int edgeCount() const { return int(m_canvas.edges.size()); }
    int revision() const { return m_revision; }
    qreal zoom() const { return m_zoom; }
    qreal panX() const { return m_pan.x(); }
    qreal panY() const { return m_pan.y(); }
    QRectF viewRect() const;
    bool detailed() const { return m_zoom >= DetailZoom; }
    bool animate() const { return m_animate; }
    void setAnimate(bool on);
    CanvasNodeModel *nodes() const { return m_nodes; }
    CanvasEdgeModel *edges() const { return m_edges; }
    QStringList selectedNodes() const;
    QStringList selectedEdges() const;
    int selectionCount() const { return int(m_selNodes.size() + m_selEdges.size()); }
    QRectF selectionScreenRect() const;
    QString selectionKind() const;
    QString selectionColor() const;
    bool selectionArrowStart() const;
    bool selectionArrowEnd() const;
    QString hoverNode() const { return m_hoverNode; }
    QRectF hoverRect() const;
    QRectF singleRect() const;
    QRectF rubberBand() const;
    bool connecting() const { return m_gesture == Gesture::Connect; }
    QString connectPath() const { return m_connectPath; }
    QString connectArrow() const { return m_connectArrow; }
    QString connectTarget() const { return m_connectTarget; }
    bool busy() const { return m_gesture != Gesture::None; }
    bool panMode() const { return m_panMode; }
    void setPanMode(bool on);
    bool selectMode() const { return m_selectMode; }
    void setSelectMode(bool on);
    QString editingNode() const { return m_editingNode; }
    void setEditingNode(const QString &id);
    qreal handleRadius() const { return m_handleRadius; }
    void setHandleRadius(qreal r);

    /// The document as it is now.
    const canvas::Canvas &document() const { return m_canvas; }
    /// The selection as sets (tests).
    QSet<QString> selectedNodeSet() const { return m_selNodes; }
    QSet<QString> selectedEdgeSet() const { return m_selEdges; }

    // ---- the file -------------------------------------------------------------------------------
    /// A canvas file's bytes (UTF-8): "" (shown and edited), "corrupt" (no canvas: shown as a
    /// notice, never written), "tooLarge". The history starts anew; the view fits the canvas.
    Q_INVOKABLE QString load(const QByteArray &bytes);
    /// The canvas as its file (canvas::writeCanvas(): untouched nodes as they were read).
    Q_INVOKABLE QByteArray fileBytes() const;
    /// `bytes` are what this view loaded or saved last (its own save coming back).
    Q_INVOKABLE bool isKnownFile(const QByteArray &bytes) const;
    /// `bytes` were written: they are the version later changes are merged against.
    Q_INVOKABLE void markSaved(const QByteArray &bytes);
    /// Another program's version of the file: taken as it is when nothing changed here, else
    /// merged with what changed here (canvas::merge; `modified` stays: it needs a save). False when
    /// it is no canvas (the view shows the notice). The history starts anew. A gesture in
    /// progress finishes first (the version waits for it).
    Q_INVOKABLE bool takeExternal(const QByteArray &bytes);
    /// The file holds anything at all (a write in progress may leave it empty for a moment).
    Q_INVOKABLE bool hasBytes(const QByteArray &bytes) const { return !bytes.isEmpty(); }
    /// The canvas as a picture (the view's "Export as picture"): "svg" — canvas::toSvg() with the
    /// pictures its cards show embedded and the first lines of its notes — or "png" (that SVG drawn
    /// at twice its size, at most 8192 px a side), in the light or the dark theme; empty when
    /// there is nothing to draw.
    Q_INVOKABLE QByteArray exportPicture(const QString &format, bool dark) const;

    // ---- the view -------------------------------------------------------------------------------
    /// The whole canvas in view (an empty one: 100 % at the middle).
    Q_INVOKABLE void zoomToFit();
    Q_INVOKABLE void zoomToSelection();
    /// The card in view at a zoom where it reads well (search hits, a backlink's line).
    Q_INVOKABLE void focusNode(const QString &id);
    /// The card of the canvas's index text line `line` (VaultIndex::content(): a search hit's
    /// line, a backlink's) in view and selected; false when the line is no card's.
    Q_INVOKABLE bool focusLine(int line);
    /// Zoom by `factor` at (x, y) of this item (-1: the middle).
    Q_INVOKABLE void zoomBy(qreal factor, qreal x = -1, qreal y = -1);
    Q_INVOKABLE void setZoom(qreal zoom);
    Q_INVOKABLE void panBy(qreal dx, qreal dy);
    /// Canvas ↔ screen (this item's coordinates).
    Q_INVOKABLE QPointF toScreen(qreal x, qreal y) const;
    Q_INVOKABLE QPointF toCanvas(qreal x, qreal y) const;
    /// A card's rectangle on screen (empty for none).
    Q_INVOKABLE QRectF nodeScreenRect(const QString &id) const;
    /// The middle of the view in canvas units (new cards go there).
    Q_INVOKABLE QPointF viewCenter() const;
    /// A point of the window's scene in canvas units, and whether it lies on the board (a page
    /// dragged from the sidebar's tree dropped there).
    Q_INVOKABLE QPointF canvasAtScene(qreal x, qreal y) const;
    Q_INVOKABLE bool containsScene(qreal x, qreal y) const;

    // ---- the selection ----------------------------------------------------------------------------
    Q_INVOKABLE void select(const QStringList &nodeIds, const QStringList &edgeIds = {});
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();

    // ---- what a card holds (menus, the text editor) -----------------------------------------------
    /// {id, type, x, y, width, height, color, text, file, subpath, url, label, background,
    /// backgroundStyle} of a node, {} for none.
    Q_INVOKABLE QVariantMap nodeData(const QString &id) const;
    /// {id, fromNode, toNode, label, color, arrowStart, arrowEnd} of an edge.
    Q_INVOKABLE QVariantMap edgeData(const QString &id) const;
    /// The node at (x, y) of this item ("" none) and the edge ("" none).
    Q_INVOKABLE QString nodeAtScreen(qreal x, qreal y) const;
    Q_INVOKABLE QString edgeAtScreen(qreal x, qreal y) const;
    /// A card's content is taller than the card (QML tells: the wheel scrolls it while selected).
    Q_INVOKABLE void setScrollable(const QString &id, bool scrollable);

    // ---- edits (each one undo step) ---------------------------------------------------------------
    /// A new text card centred at (x, y) in canvas units (the size of canvas::defaultSize), on top,
    /// selected; its id.
    Q_INVOKABLE QString addText(qreal x, qreal y, const QString &text = QString());
    /// A file card of `path` (a vault path; `subpath` "#Heading" / "#^id" / ""), centred there.
    Q_INVOKABLE QString addFile(const QString &path, const QString &subpath, qreal x, qreal y);
    /// Several files side by side from (x, y) (a drop, a paste of files).
    Q_INVOKABLE QStringList addFiles(const QStringList &paths, qreal x, qreal y);
    /// Files dropped from another app: imported into the notebook through the host (a file of the
    /// notebook used where it is), added side by side from (x, y); their cards' ids.
    Q_INVOKABLE QStringList addDroppedFiles(const QList<QUrl> &urls, qreal x, qreal y);
    Q_INVOKABLE QString addLink(const QString &url, qreal x, qreal y);
    /// A group around the selection (or an empty one in the middle of the view).
    Q_INVOKABLE QString addGroup(const QString &label = QString());
    Q_INVOKABLE void deleteSelection();
    Q_INVOKABLE void duplicateSelection();
    Q_INVOKABLE void ungroupSelection();
    /// "1"–"6", "#rrggbb" or "" for none: the selected cards' and edges' colour.
    Q_INVOKABLE void colorSelection(const QString &color);
    /// "left", "center", "right", "top", "middle", "bottom".
    Q_INVOKABLE void alignSelection(const QString &how);
    /// "horizontal", "vertical".
    Q_INVOKABLE void distributeSelection(const QString &axis);
    /// "front", "back", "forward", "backward".
    Q_INVOKABLE void reorderSelection(const QString &order);
    Q_INVOKABLE void nudgeSelection(qreal dx, qreal dy);
    Q_INVOKABLE void setNodeText(const QString &id, const QString &text);
    /// A text card's text typed in place, and its height grown to `minHeight` when it is lower (one
    /// undo step; nothing when neither changes).
    Q_INVOKABLE void setNodeTextFitted(const QString &id, const QString &text, qreal minHeight);
    Q_INVOKABLE void setNodeFile(const QString &id, const QString &file, const QString &subpath);
    Q_INVOKABLE void setNodeUrl(const QString &id, const QString &url);
    Q_INVOKABLE void setGroupLabel(const QString &id, const QString &label);
    Q_INVOKABLE void setGroupBackground(const QString &id, const QString &path, const QString &style);
    Q_INVOKABLE void setEdgeLabel(const QString &id, const QString &label);
    /// The selected edges' arrowheads.
    Q_INVOKABLE void setArrows(bool atStart, bool atEnd);
    Q_INVOKABLE void reverseSelectedEdges();
    /// A node's size to fit its content (a text card after editing: QML measures it).
    Q_INVOKABLE void resizeNode(const QString &id, qreal width, qreal height);
    /// An edge between two cards (sides "" = chosen when drawn); its id.
    Q_INVOKABLE QString connectCards(const QString &from, const QString &to, const QString &fromSide = QString(),
                                     const QString &toSide = QString());
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    /// A gesture in progress dropped (Escape); false when none.
    Q_INVOKABLE bool cancelGesture();

    // ---- the clipboard ----------------------------------------------------------------------------
    /// The selected cards (and the edges between them) on the clipboard as a JSON Canvas fragment
    /// — its text: another canvas (Brulion's, Obsidian's) pastes it; false when nothing is selected.
    Q_INVOKABLE bool copySelection();
    Q_INVOKABLE bool cutSelection();
    /// The clipboard at (x, y) of this item (-1: the pointer, else the middle of the view): a
    /// canvas fragment's cards and edges; files (copied in a file manager) as file cards — stored
    /// in the notebook through the host —; a picture as a stored PNG's card; a web address as a link
    /// card; other text as a text card. {pasted (count), kind ("cards" | "files" | "picture" |
    /// "link" | "text" | "")}.
    Q_INVOKABLE QVariantMap paste(qreal x = -1, qreal y = -1);
    /// pasteText()'s rules for a text.
    Q_INVOKABLE QVariantMap pasteText(const QString &text, qreal x = -1, qreal y = -1);

Q_SIGNALS:
    void hostChanged();
    void pathChanged();
    void modifiedChanged();
    void historyChanged();
    void contentChanged();
    void viewChanged();
    void detailedChanged();
    void animateChanged();
    void selectionChanged();
    void selectionGeometryChanged();
    void hoverChanged();
    void gestureChanged();
    void panModeChanged();
    void selectModeChanged();
    void editingNodeChanged();
    void handleRadiusChanged();
    /// A click / tap that did not move: on a card ("" none) at (x, y) in canvas units (the view
    /// follows a link there), with the button and the modifiers.
    void tapped(const QString &nodeId, qreal x, qreal y, int button, int modifiers);
    /// A double click / tap: on a card, an edge, or neither (x, y in canvas units).
    void doubleTapped(const QString &nodeId, const QString &edgeId, qreal x, qreal y);
    /// A right click / long press on a card, an edge or the canvas (x, y on screen).
    void contextMenuRequested(const QString &nodeId, const QString &edgeId, qreal x, qreal y);
    /// The wheel over a selected card whose content scrolls (setScrollable()): scroll it by `dy`.
    void cardScrollRequested(const QString &nodeId, qreal dy);
    /// A card made by a gesture (an edge dropped on empty canvas) wants its text typed.
    void editRequested(const QString &nodeId);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void touchEvent(QTouchEvent *event) override;
    void touchUngrabEvent() override;
    bool event(QEvent *event) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    enum class Gesture { None, Press, Move, Resize, Band, Connect, Pan, Pinch };
    /// Which part of a card a press is on.
    enum class Part { None, Body, Handle, Resize };
    struct Hit {
        Part part = Part::None;
        QString node;
        QString edge;
        QString side;  ///< Handle: the side
        int edges = 0; ///< Resize: Qt::Edges of the rectangle
    };

    // Changes.
    /// `next` becomes the document: one undo step (nothing when it holds what the document does).
    void apply(canvas::Canvas next, bool undoable = true);
    void setDocument(canvas::Canvas doc);
    void pushUndo(const canvas::Canvas &before);
    void setModified(bool on);
    void documentChanged();
    void refreshModels();
    void pruneSelection();
    void setSelection(QSet<QString> nodes, QSet<QString> edges);

    // The view.
    void setView(double zoom, QPointF pan);
    void animateTo(double zoom, QPointF pan);
    void showRect(const QRectF &rect, double maxZoom);
    double clampZoom(double z) const;

    // Input.
    Hit hitAt(QPointF itemPos) const;
    void pressAt(QPointF pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers, bool touch);
    void moveTo(QPointF pos);
    void releaseAt(QPointF pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers, bool touch);
    void beginMove();
    void updateConnect(QPointF pos);
    void finishConnect(QPointF pos);
    void updateHover(QPointF pos);
    void setGesture(Gesture g);
    QPointF clampedCanvas(QPointF p) const;
    void takePendingExternal();

    QPointer<EditorHost> m_host;
    QString m_path;
    canvas::Canvas m_canvas;
    /// The version last loaded or saved (what an external version is merged against).
    canvas::Canvas m_saved;
    QByteArray m_savedBytes;
    QByteArray m_pendingExternal;
    QList<canvas::Canvas> m_undo;
    QList<canvas::Canvas> m_redo;
    bool m_modified = false;
    int m_revision = 0;

    double m_zoom = 1;
    QPointF m_pan;
    bool m_fitPending = true;
    bool m_animate = true;
    QVariantAnimation *m_viewAnimation = nullptr;
    double m_animFromZoom = 1;
    double m_animToZoom = 1;
    QPointF m_animFromPan;
    QPointF m_animToPan;

    CanvasNodeModel *m_nodes = nullptr;
    CanvasEdgeModel *m_edges = nullptr;

    QSet<QString> m_selNodes;
    QSet<QString> m_selEdges;
    QSet<QString> m_scrollable;
    QString m_hoverNode;
    QString m_editingNode;
    bool m_panMode = false;
    bool m_selectMode = false;
    qreal m_handleRadius = 9;

    // A gesture.
    Gesture m_gesture = Gesture::None;
    Hit m_pressHit;
    QPointF m_pressPos;    ///< item coordinates
    QPointF m_pressCanvas; ///< canvas units
    QPointF m_lastPos;
    Qt::KeyboardModifiers m_pressModifiers;
    Qt::MouseButton m_pressButton = Qt::NoButton;
    bool m_pressTouch = false;
    bool m_pressSelected = false; ///< the card pressed was selected before the press
    canvas::Canvas m_gestureStart;
    QSet<QString> m_moving;
    QRectF m_resizeStart;
    QSet<QString> m_bandBase; ///< the selection a Shift rubber band adds to
    QString m_connectFrom;
    QString m_connectSide;
    QString m_connectPath;
    QString m_connectArrow;
    QString m_connectTarget;
    QTimer m_longPress;
    bool m_longPressed = false;
    // Pinch.
    double m_pinchDistance = 1;
    double m_pinchZoom = 1;
    QPointF m_pinchCanvas;
    int m_touchPoints = 0;
    QElapsedTimer m_lastTap; ///< touch: the last tap (a second one soon after is a double tap)
    QPointF m_lastTapPos;
    std::optional<QPointF> m_pointer; ///< the mouse over the board (paste lands there)
};

} // namespace brulion::canvasview
