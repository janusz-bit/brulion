// DrawingCanvas — the drawing editor's paper (QML: qml/drawing/DrawingView.qml; docs/UI.md
// "Drawing editor"): shows a drawing (core drawing module) and draws, erases and moves around it.
//
//   DrawingCanvas { tool: "pen"; color: "#1f2430"; penWidth: 1; background: "lines"
//                   fingerDraws: !stylusSeen
//                   onContentChanged: saveTimer.restart() }
//   canvas.load(Workspace.readAttachment(path)) → "" | "readonly" | "notDrawing" | "corrupt"
//   const bytes = canvas.svg(); Workspace.writeAttachment(path, bytes); canvas.markSaved(bytes)
//
// Input (the rules of docs/UI.md "Drawing editor"):
//  * a stylus draws with its pressure; its eraser end (or a held barrel button) erases whatever
//    the tool is. Qt Quick gives tablet events only to pointer handlers, and Qt makes mouse
//    events of the unhandled ones only on some platforms (X11, the browser; not Android, whose
//    stylus would never reach an item): the canvas takes the tablet events from its window (an
//    event filter) when it is the topmost item taking input under the pen — not under a popup,
//    a toast or a bar, nor while a modal popup is open —, so no mouse events are made of them;
//    mouse events the system makes of the pen itself (Windows) are recognised by their time and
//    place and left alone. A mouse event whose device is a stylus draws as a stylus without
//    pressure;
//  * a mouse draws with the left button; the middle or right button, or the left one while
//    `panMode` (Space held), moves the paper; the wheel moves it (Shift: sideways), Ctrl+wheel and
//    a touchpad's pinch zoom at the pointer;
//  * fingers: with `fingerDraws` one finger draws, else it moves the paper; two fingers move and
//    zoom (a second finger that lands while a stroke has just begun turns the stroke into the
//    gesture — the stroke is dropped). A stylus seen (`stylusSeen`) is what QML uses to turn
//    finger drawing off: the palm then never draws.
// Erasing removes whole strokes the eraser touches (as Excalidraw's eraser), one undo step per
// gesture. A stroke is one undo step; the background too. Undo history: the strokes of this
// session (cleared by load()).
//
// Rendering: a QQuickPaintedItem (every scene graph backend, the software one of the screenshots
// too). The committed strokes are rendered on the GUI thread into a cached image of the view;
// while the view moves the cached image is drawn moved and scaled and rendered again when the
// gesture rests; the stroke being drawn is drawn on top, only its area repainted. paint() may
// run on the render thread: it draws only a snapshot taken in updatePaintNode() (the GUI thread
// waits there).
#pragma once

#include "drawing/drawing.h"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QPainterPath>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QSet>
#include <QTimer>

#include <functional>

class QPointingDevice;

namespace brulion::drawingview {

class DrawingCanvas : public QQuickPaintedItem {
    Q_OBJECT

    /// "pen", "highlighter" or "eraser".
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged FINAL)
    /// The colour of new strokes.
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged FINAL)
    /// The width of new strokes (Excalidraw's strokeWidth: 0.5 fine … 2 thick; the highlighter 4–8).
    Q_PROPERTY(qreal penWidth READ penWidth WRITE setPenWidth NOTIFY penWidthChanged FINAL)
    /// The paper's pattern: "plain", "lines", "grid" (setting it is an undo step).
    Q_PROPERTY(QString background READ background WRITE setBackground NOTIFY backgroundChanged FINAL)
    /// One finger draws (else it moves the paper).
    Q_PROPERTY(bool fingerDraws READ fingerDraws WRITE setFingerDraws NOTIFY fingerDrawsChanged FINAL)
    /// A stylus has touched the canvas.
    Q_PROPERTY(bool stylusSeen READ stylusSeen NOTIFY stylusSeenChanged FINAL)
    /// The left mouse button / one finger moves the paper (Space held).
    Q_PROPERTY(bool panMode READ panMode WRITE setPanMode NOTIFY panModeChanged FINAL)
    /// The view: zoom (1 = one scene unit per pixel) and the scene point at the item's centre.
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY viewChanged FINAL)
    Q_PROPERTY(qreal centerX READ centerX WRITE setCenterX NOTIFY viewChanged FINAL)
    Q_PROPERTY(qreal centerY READ centerY WRITE setCenterY NOTIFY viewChanged FINAL)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged FINAL)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged FINAL)
    /// Changed since load() or markSaved().
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged FINAL)
    /// The loaded drawing can be edited (strokes only, read).
    Q_PROPERTY(bool editable READ editable NOTIFY loadedChanged FINAL)
    Q_PROPERTY(int strokeCount READ strokeCount NOTIFY contentChanged FINAL)
    /// A stroke or an erasing gesture is in progress.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    /// The paper's colour (from the drawing).
    Q_PROPERTY(QColor paperColor READ paperColor NOTIFY loadedChanged FINAL)
    /// Milliseconds the last cached image took to render (diagnostics).
    Q_PROPERTY(double lastRenderMs READ lastRenderMs FINAL)

public:
    explicit DrawingCanvas(QQuickItem *parent = nullptr);
    ~DrawingCanvas() override;

    static constexpr double MinZoom = 0.1;
    static constexpr double MaxZoom = 10;
    /// The eraser's radius on screen (px).
    static constexpr double EraserRadius = 12;
    /// The highlighter's opacity (0–100).
    static constexpr int HighlighterOpacity = 40;

    QString tool() const { return m_tool; }
    void setTool(const QString &tool);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    qreal penWidth() const { return m_penWidth; }
    void setPenWidth(qreal width);
    QString background() const;
    void setBackground(const QString &name);
    bool fingerDraws() const { return m_fingerDraws; }
    void setFingerDraws(bool on);
    bool stylusSeen() const { return m_stylusSeen; }
    bool panMode() const { return m_panMode; }
    void setPanMode(bool on);
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    qreal centerX() const { return m_center.x(); }
    void setCenterX(qreal x);
    qreal centerY() const { return m_center.y(); }
    void setCenterY(qreal y);
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    bool modified() const { return m_modified; }
    bool editable() const { return m_editable; }
    int strokeCount() const { return int(m_items.size()); }
    bool busy() const { return m_gesture == Gesture::Draw || m_gesture == Gesture::Erase; }
    QColor paperColor() const { return m_paper; }
    double lastRenderMs() const { return m_lastRenderMs; }

    /// Show the drawing in `bytes` (Brulion's SVG, excalidraw.com's "Embed scene" SVG, a
    /// .excalidraw file; empty: a new drawing). "" when it can be edited; else nothing is shown
    /// and nothing can be drawn — "readonly" (shapes, text, pictures … from Excalidraw, which
    /// Brulion would lose: QML shows the file as a picture), "notDrawing" (no scene: a picture),
    /// "corrupt" (a scene that cannot be read). Clears the undo history and `modified`; the view
    /// shows the strokes from their top left at zoom 1, zoomed out to the view's width when they
    /// are wider.
    Q_INVOKABLE QString load(const QByteArray &bytes);
    /// Take in a version of the file another program wrote while this one is open (Syncthing):
    /// with no changes here it replaces the drawing; else the strokes are merged by their ids —
    /// the other version, plus the strokes drawn here since the last load or save, minus the ones
    /// erased here (`modified` stays). False when `bytes` holds no editable drawing (nothing
    /// changes then). The undo history is cleared.
    Q_INVOKABLE bool takeExternal(const QByteArray &bytes);
    /// The file to write (core drawing::toSvg). Empty while not editable.
    Q_INVOKABLE QByteArray svg() const;
    /// The drawing was written as `written` (svg()): `modified` is false, takeExternal() merges
    /// against this state.
    Q_INVOKABLE void markSaved(const QByteArray &written = QByteArray());
    /// `bytes` are the file as this canvas last loaded, took in or wrote it (the echo of its own
    /// save, which needs no merge).
    Q_INVOKABLE bool isKnownFile(const QByteArray &bytes) const { return !bytes.isEmpty() && bytes == m_known; }
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    /// The whole drawing in view (at most zoom 1), centred across, from its top.
    Q_INVOKABLE void fitAll();
    Q_INVOKABLE void zoomBy(qreal factor, qreal x = -1, qreal y = -1);
    Q_INVOKABLE void panBy(qreal dx, qreal dy);
    /// The scene rect the strokes cover (tests, QML).
    Q_INVOKABLE QRectF contentBounds() const;
    /// Scene ↔ item coordinates.
    Q_INVOKABLE QPointF toScene(QPointF item) const;
    Q_INVOKABLE QPointF toItem(QPointF scene) const;

    /// The drawing as it is now (tests).
    drawing::Drawing currentDrawing() const;
    /// The clock of new strokes (ms since the epoch); tests fix it.
    void setClock(std::function<qint64()> clock) { m_clock = std::move(clock); }
    /// Render into `painter` for an item of `size` (paint(), tests).
    void render(QPainter *painter, const QSizeF &size);

    void paint(QPainter *painter) override;

Q_SIGNALS:
    void toolChanged();
    void colorChanged();
    void penWidthChanged();
    void backgroundChanged();
    void fingerDrawsChanged();
    void stylusSeenChanged();
    void panModeChanged();
    void viewChanged();
    void historyChanged();
    void modifiedChanged();
    void loadedChanged();
    void busyChanged();
    /// The drawing changed (a stroke, erasing, undo, redo, the background): QML saves later.
    void contentChanged();
    /// The user moved or zoomed the paper.
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
    bool eventFilter(QObject *watched, QEvent *event) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

private:
    struct Item {
        drawing::Stroke stroke;
        QPainterPath path; ///< the outline, scene coordinates
        QRectF bounds;     ///< scene coordinates
        QColor color;
    };
    struct Op {
        enum Kind { Add, Remove, Background } kind = Add;
        /// Add: the strokes appended; Remove: the strokes with their indices (ascending).
        QList<std::pair<int, Item>> items;
        drawing::Background before = drawing::Background::Plain;
        drawing::Background after = drawing::Background::Plain;
    };
    enum class Gesture { None, Draw, Erase, Pan, Pinch };
    /// What a pointer event is (from its device and the tablet event it was made of).
    struct Pointer {
        bool stylus = false;
        bool eraser = false;
        double pressure = 0.5;
    };

    void filterWindow(QQuickWindow *window);
    static Item makeItem(drawing::Stroke stroke);
    static QPainterPath outlinePath(const QList<QPointF> &outline);
    Pointer pointerOf(const QPointerEvent *event) const;
    /// A tablet event that passed the window: true when the canvas took it (see the header).
    bool handleTablet(QTabletEvent *event);
    /// The canvas is the topmost item taking input at `scenePos` (no popup, toast or bar over it).
    bool onTop(QPointF scenePos) const;
    /// A mouse event made of a stylus's tablet event the canvas already took (Windows makes them).
    bool fromTablet(const QMouseEvent *event) const;
    void noteStylus();
    void setGesture(Gesture g);

    void beginStroke(QPointF itemPos, const Pointer &p);
    void extendStroke(QPointF itemPos, const Pointer &p);
    void finishStroke();
    void cancelStroke();
    void beginErase(QPointF itemPos);
    void eraseTo(QPointF itemPos);
    void finishErase();
    void beginPan(QPointF itemPos);
    void panTo(QPointF itemPos);

    void apply(const Op &op, bool forward);
    void push(Op op);
    void setModified(bool on);
    void changed();
    void setDocument(drawing::Drawing doc);
    QList<drawing::Stroke> strokes() const;

    QTransform sceneToItem() const;
    void placeView();
    void viewMoved(bool byUser);
    void endViewGesture();
    /// Take the snapshot paint() draws (GUI thread, or the render thread while the GUI waits).
    void prepareSnapshot();
    void paintSnapshot(QPainter *painter, const QSizeF &size) const;
    void clampZoom();
    /// The cached image of the committed strokes for the current view (GUI thread).
    void renderCache();
    void invalidateCache();
    void drawItemIntoCache(const Item &item);
    void updateLive();
    QString newId() const;

    // The drawing.
    drawing::Drawing m_doc; ///< everything but the strokes (m_items)
    QList<Item> m_items;
    QColor m_paper = QColor(u"#ffffff");
    bool m_editable = true;
    bool m_modified = false;
    /// Stroke ids at the last load or save (takeExternal() merges against them).
    QSet<QString> m_baseIds;
    /// The file as last loaded, taken in or written.
    QByteArray m_known;
    QList<Op> m_undo;
    QList<Op> m_redo;

    // Tools.
    QString m_tool = QStringLiteral("pen");
    QColor m_color = QColor(u"#1f2430");
    qreal m_penWidth = 1;
    bool m_fingerDraws = true;
    bool m_stylusSeen = false;
    bool m_panMode = false;

    // The view.
    double m_zoom = 1;
    QPointF m_center;
    bool m_viewSet = false; ///< the view was placed (load, the user)
    /// The view is being moved (pan, pinch, wheel): the cached image is drawn moved until it rests.
    bool m_interactingView = false;

    // Input.
    Gesture m_gesture = Gesture::None;
    drawing::Stroke m_live;
    bool m_liveStylus = false;
    double m_liveMinPressure = 1;
    double m_liveMaxPressure = 0;
    QElapsedTimer m_liveClock;
    QPainterPath m_livePath; ///< item coordinates
    QRectF m_liveDirty;      ///< item coordinates
    QSet<int> m_erasing;     ///< indices of m_items the erasing gesture removes (hidden meanwhile)
    QPointF m_lastItemPos;
    QPointF m_panStart;
    QPointF m_panCenter;
    double m_pinchDistance = 0;
    double m_pinchZoom = 1;
    QPointF m_pinchScene;
    int m_touchDrawId = -1;
    QPointF m_hover{-1, -1};
    /// A stylus gesture taken from the window's tablet events (see the header).
    bool m_tabletGesture = false;
    /// The mouse events of the current press are a stylus's the tablet events already gave.
    bool m_ignoreMouse = false;
    quint64 m_lastTabletTime = 0;
    QPointF m_lastTabletGlobal{-1e9, -1e9};
    QPointer<QQuickWindow> m_filteredWindow;

    // Rendering.
    QImage m_cache;
    double m_cacheZoom = 0;
    QPointF m_cacheCenter;
    QSizeF m_cacheSize;
    bool m_cacheValid = false;
    QTimer m_settle;
    double m_lastRenderMs = 0;
    struct Snapshot {
        QImage cache;
        QTransform cacheToItem;
        bool cacheCurrent = false;
        QColor paper;
        QPainterPath live;
        QColor liveColor;
        double liveOpacity = 1;
        QPointF eraser{-1, -1};
        double eraserRadius = 0;
    } m_snapshot;

    std::function<qint64()> m_clock;
};

} // namespace brulion::drawingview
