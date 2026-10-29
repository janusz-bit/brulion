// DrawingCanvas — the drawing editor's paper (QML: qml/drawing/DrawingView.qml; docs/UI.md
// "Drawing editor"): shows a drawing (core drawing module) and draws, erases, selects, moves and
// writes on it, and moves around it.
//
//   DrawingCanvas { tool: "pen"; color: "#1f2430"; penWidth: 1; background: "lines"
//                   fingerDraws: !stylusSeen; appDark: Theme.dark
//                   onContentChanged: saveTimer.restart() }
//   canvas.load(Workspace.readAttachment(path)) → "" | "readonly" | "notDrawing" | "corrupt"
//   const bytes = canvas.fileBytes(); Workspace.writeAttachment(path, bytes); canvas.markSaved(bytes)
//
// A preview (`interactive: false`: the sync center's conflict of two drawings and their merge)
// only shows a drawing, all of it (fitAll()): no input reaches it, it never takes the pen from its
// window.
//
// Files (core drawing/drawingfile.h): Brulion's SVG, a `.excalidraw` scene and an Obsidian
// Excalidraw plugin file (`*.excalidraw.md`) — each written back in its own format (`format`,
// fileBytes(): the plugin file's Markdown around the scene kept as it was).
//
// Tools (`tool`): "pen", "highlighter" (freehand lines), "eraser" (whole elements it touches, a
// shape with its label), "select" (a lasso: elements at least half inside it — or a tap: the
// topmost one — taken with their groups and labels; a drag on the selection moves it; a double
// tap on a text edits it, on a shape its label), "rectangle", "ellipse", "diamond", "line",
// "arrow" (dragged: colour the pen's, width penWidth × 2; Shift: a square, a circle, 15° steps; the
// tool stays), "text" (a tap: a new text — or the text, the shape's label tapped — edited in QML's
// DrawingTextEditor: textEditRequested → commitText). The selection's actions: deleteSelection,
// duplicateSelection, recolorSelection, reorderSelection, nudgeSelection, selectAll (QML's
// DrawingSelectionBar and keys). Locked elements are never selected, moved or erased.
//
// Input (the rules of docs/UI.md "Drawing editor"):
//  * a stylus works the tool with its pressure; its eraser end (or a held barrel button) erases
//    whatever the tool is. Qt Quick gives tablet events only to pointer handlers, and Qt makes
//    mouse events of the unhandled ones only on some platforms (X11, the browser; not Android,
//    whose stylus would never reach an item): the canvas takes the tablet events from its window
//    (an event filter) when it is the topmost item taking input under the pen — not under a
//    popup, a toast or a bar, nor while a modal popup is open —, so no mouse events are made of
//    them; mouse events the system makes of the pen itself (Windows) are recognised by their time
//    and place and left alone. A mouse event whose device is a stylus works as a stylus without
//    pressure;
//  * a mouse works the tool with the left button; the middle or right button, or the left one
//    while `panMode` (Space held), moves the paper; the wheel moves it (Shift: sideways),
//    Ctrl+wheel and a touchpad's pinch zoom at the pointer;
//  * fingers: with `fingerDraws` one finger works the tool, else it moves the paper (a palm on a
//    selection never drags it); two fingers move and zoom (a second finger that lands while a
//    stroke has just begun turns the stroke into the gesture — the stroke is dropped; a lasso or
//    a shape being dragged is dropped). A stylus seen (`stylusSeen`) is what QML uses to turn
//    finger drawing off: the palm then never draws. Palm rejection: while the pen is down every
//    touch is ignored (until all fingers lift); a pen that lands while fingers move the paper or
//    draw ends their gesture (a finger stroke just begun is dropped) and takes over.
// Every gesture and action is one undo step (a generic diff of the elements: drawing::ElementDiff);
// the background and the theme too. Undo history: the edits of this session (cleared by load()).
//
// Light and dark (core drawing/palette.h): the drawing's own `theme` — "auto" (as the app:
// `appDark`, which QML binds to Theme.dark), "light" or "dark" — is saved in the file; the paper,
// its lines and the elements are shown in the colours of the theme in force (`dark`), the stored
// colours never change. A change of either repaints at once (a stroke being drawn too).
//
// Rendering: a QQuickPaintedItem (every scene graph backend, the software one of the screenshots
// too). Every element is prepared once (drawingrender.h Prepared: its paths, by version) and the
// committed ones rendered on the GUI thread into a cached image of the view; while the view moves
// the cached image is drawn moved and scaled and rendered again when the gesture rests; what is
// being drawn (a stroke, a shape, a moved selection, the lasso, the selection's frame) is drawn on
// top. paint() may run on the render thread: it draws only a snapshot taken in updatePaintNode()
// (the GUI thread waits there). The paper and the elements are painted as the previews paint them
// (drawingrender.h).
#pragma once

#include "drawingrender.h"

#include "drawing/drawing.h"
#include "drawing/drawingfile.h"
#include "drawing/edit.h"
#include "drawing/merge.h"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QPainterPath>
#include <QPointer>
#include <QPolygonF>
#include <QQuickPaintedItem>
#include <QSet>
#include <QTimer>
#include <QVariantMap>

#include <functional>

class QPointingDevice;

namespace brulion::drawingview {

class DrawingCanvas : public QQuickPaintedItem {
    Q_OBJECT

    /// "pen", "highlighter", "eraser", "select", "rectangle", "ellipse", "diamond", "line", "arrow"
    /// or "text" (anything else: "pen").
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged FINAL)
    /// The colour of new strokes, shapes and text.
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged FINAL)
    /// The width of new strokes (Excalidraw's strokeWidth: 0.5 fine … 2 thick; the highlighter 4–8);
    /// shapes are drawn twice as wide.
    Q_PROPERTY(qreal penWidth READ penWidth WRITE setPenWidth NOTIFY penWidthChanged FINAL)
    /// The size of new text (scene units: Excalidraw's 16, 20, 28, 36).
    Q_PROPERTY(qreal fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged FINAL)
    /// How many elements are selected, and their frame in item coordinates (the bar's place).
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged FINAL)
    Q_PROPERTY(QRectF selectionRect READ selectionRect NOTIFY selectionChanged FINAL)
    /// The colour of the lasso and the selection's frame (QML: Theme.info).
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor NOTIFY selectionColorChanged FINAL)
    /// A text is being written (QML's editor is open): the canvas takes no other input.
    Q_PROPERTY(bool textEditing READ textEditing NOTIFY textEditingChanged FINAL)
    /// The paper's pattern: "plain", "lines", "grid" (setting it is an undo step).
    Q_PROPERTY(QString background READ background WRITE setBackground NOTIFY backgroundChanged FINAL)
    /// The drawing's own light or dark: "auto" (as the app), "light", "dark" (setting it is an undo
    /// step; saved in the file).
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged FINAL)
    /// The app is dark (QML: Theme.dark): an "auto" drawing follows it, at once. No edit.
    Q_PROPERTY(bool appDark READ appDark WRITE setAppDark NOTIFY appDarkChanged FINAL)
    /// The drawing is shown dark now (its theme "dark", or "auto" while the app is dark).
    Q_PROPERTY(bool dark READ dark NOTIFY renderChanged FINAL)
    /// One finger draws (else it moves the paper).
    Q_PROPERTY(bool fingerDraws READ fingerDraws WRITE setFingerDraws NOTIFY fingerDrawsChanged FINAL)
    /// A stylus has touched the canvas.
    Q_PROPERTY(bool stylusSeen READ stylusSeen NOTIFY stylusSeenChanged FINAL)
    /// The left mouse button / one finger moves the paper (Space held).
    Q_PROPERTY(bool panMode READ panMode WRITE setPanMode NOTIFY panModeChanged FINAL)
    /// Takes input (default). False: a preview (the sync center's conflict of drawings) — no mouse,
    /// touch, stylus, wheel or pinch reaches it (they go to what lies behind: a list scrolls),
    /// nothing is drawn or moved there; the whole drawing is in view (fitAll()) after load() and
    /// whenever the item's size changes.
    Q_PROPERTY(bool interactive READ interactive WRITE setInteractive NOTIFY interactiveChanged FINAL)
    /// The view: zoom (1 = one scene unit per pixel) and the scene point at the item's centre.
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY viewChanged FINAL)
    Q_PROPERTY(qreal centerX READ centerX WRITE setCenterX NOTIFY viewChanged FINAL)
    Q_PROPERTY(qreal centerY READ centerY WRITE setCenterY NOTIFY viewChanged FINAL)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged FINAL)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged FINAL)
    /// Changed since load() or markSaved().
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged FINAL)
    /// The loaded drawing can be edited (only elements Brulion draws, read).
    Q_PROPERTY(bool editable READ editable NOTIFY loadedChanged FINAL)
    /// The file's format: "svg", "excalidraw" (a `.excalidraw` scene), "excalidrawMd" (an Obsidian
    /// Excalidraw plugin file); "" when nothing was read. fileBytes() writes it.
    Q_PROPERTY(QString format READ format NOTIFY loadedChanged FINAL)
    /// The file is in a form Brulion only shows (the Excalidraw plugin's oldest one).
    Q_PROPERTY(bool legacy READ legacy NOTIFY loadedChanged FINAL)
    /// How many elements the drawing has.
    Q_PROPERTY(int strokeCount READ strokeCount NOTIFY contentChanged FINAL)
    /// A stroke, an erasing gesture, a lasso, a move or a shape is in progress.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    /// The paper's colour as shown (the drawing's, in the theme in force).
    Q_PROPERTY(QColor paperColor READ paperColor NOTIFY renderChanged FINAL)
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

    /// The tolerance of a tap on an element (px on screen; touch and pen).
    static constexpr double TapTolerance = 8;
    static constexpr double TouchTapTolerance = 14;
    /// Shorter lassos (px) are taps; smaller shapes (px) are none.
    static constexpr double TapLength = 8;
    static constexpr double MinShape = 3;
    /// A duplicate lies this far (scene units) from its original.
    static constexpr double DuplicateOffset = 10;

    QString tool() const { return m_tool; }
    void setTool(const QString &tool);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    qreal penWidth() const { return m_penWidth; }
    void setPenWidth(qreal width);
    qreal fontSize() const { return m_fontSize; }
    void setFontSize(qreal size);
    int selectionCount() const { return int(m_selected.size()); }
    QRectF selectionRect() const;
    QColor selectionColor() const { return m_selectionColor; }
    void setSelectionColor(const QColor &color);
    bool textEditing() const { return m_textEditing; }
    /// The selected elements' ids in drawing order (tests).
    QStringList selection() const;
    QString background() const;
    void setBackground(const QString &name);
    QString theme() const;
    void setTheme(const QString &name);
    bool appDark() const { return m_appDark; }
    void setAppDark(bool on);
    bool dark() const;
    bool fingerDraws() const { return m_fingerDraws; }
    void setFingerDraws(bool on);
    bool stylusSeen() const { return m_stylusSeen; }
    bool panMode() const { return m_panMode; }
    void setPanMode(bool on);
    bool interactive() const { return m_interactive; }
    void setInteractive(bool on);
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
    QString format() const { return m_formatKnown ? drawing::formatName(m_format) : QString(); }
    bool legacy() const { return m_formatKnown && !m_writable; }
    int strokeCount() const { return int(m_doc.strokes.size()); }
    bool busy() const
    {
        return m_gesture == Gesture::Draw || m_gesture == Gesture::Erase || m_gesture == Gesture::Lasso
               || m_gesture == Gesture::Move || m_gesture == Gesture::Shape;
    }
    QColor paperColor() const { return dark() ? m_paperDark : m_paperLight; }
    double lastRenderMs() const { return m_lastRenderMs; }

    /// Show the drawing in `bytes` (Brulion's SVG, excalidraw.com's "Embed scene" SVG, a
    /// .excalidraw file, an Obsidian Excalidraw plugin file; empty: a new drawing — an SVG). ""
    /// when it can be edited; else nothing is shown and nothing can be drawn — "readonly" (frames,
    /// embedded web pages … from Excalidraw, which Brulion would lose, or the Excalidraw plugin's
    /// oldest form: QML shows the file as a picture), "notDrawing" (no scene: a picture),
    /// "corrupt" (a scene that cannot be read). Clears the undo history, the selection and
    /// `modified`; the view shows the elements from their top left at zoom 1, zoomed out to the
    /// view's width when they are wider.
    Q_INVOKABLE QString load(const QByteArray &bytes);
    /// Take in a version of the file another program wrote while this one is open (Syncthing):
    /// with no changes here it replaces the drawing; else the elements are merged (drawing::merge
    /// against the last load or save) — the other version, plus what was drawn or changed here,
    /// minus what was erased here (`modified` stays), the paper's pattern and theme theirs unless
    /// changed here; an Excalidraw plugin file's Markdown theirs (their back of the card, their
    /// text entries). False when `bytes` holds no editable drawing (nothing changes then). The undo
    /// history is cleared; the selected elements that still exist stay selected. A gesture in
    /// progress goes on and ends on the new drawing.
    Q_INVOKABLE bool takeExternal(const QByteArray &bytes);
    /// The file to write, in the format read (core drawing::writeDrawingFile: an SVG, a
    /// `.excalidraw` scene, the Excalidraw plugin's Markdown with the scene in it; tombstones of
    /// the plugin's elements stamped with the clock). Empty while not editable.
    Q_INVOKABLE QByteArray fileBytes() const;
    /// The drawing as Brulion's SVG, whatever the file's format (tests, "export as SVG"). Empty
    /// while not editable.
    Q_INVOKABLE QByteArray svg() const;
    /// The drawing was written as `written` (fileBytes()): `modified` is false, takeExternal()
    /// merges against this state.
    Q_INVOKABLE void markSaved(const QByteArray &written = QByteArray());
    /// `bytes` are the file as this canvas last loaded, took in or wrote it (the echo of its own
    /// save, which needs no merge).
    Q_INVOKABLE bool isKnownFile(const QByteArray &bytes) const { return !bytes.isEmpty() && bytes == m_known; }
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    /// The whole drawing in view (at most zoom 1), centred across, from its top (a preview —
    /// `interactive` false — centred both ways).
    Q_INVOKABLE void fitAll();
    Q_INVOKABLE void zoomBy(qreal factor, qreal x = -1, qreal y = -1);
    Q_INVOKABLE void panBy(qreal dx, qreal dy);
    /// The scene rect the elements cover (tests, QML).
    Q_INVOKABLE QRectF contentBounds() const;

    /// The selection (every action one undo step; locked elements never taken).
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void deleteSelection();
    /// Copies DuplicateOffset to the right and down, selected instead.
    Q_INVOKABLE void duplicateSelection();
    /// The stored colour (as on light paper) of the selected elements.
    Q_INVOKABLE void recolorSelection(const QColor &stored);
    /// "forward", "backward", "front", "back".
    Q_INVOKABLE void reorderSelection(const QString &how);
    /// Move the selection by (dx, dy) scene units (the arrow keys).
    Q_INVOKABLE void nudgeSelection(qreal dx, qreal dy);
    /// Drop a lasso, a move or a shape in progress (Escape); false when there is none.
    Q_INVOKABLE bool cancelGesture();
    /// Text: QML shows its editor for `request` (textEditRequested) and calls commitText() with what
    /// was written (or cancelText()). commitText: an existing text edited (emptied: deleted), a
    /// shape's label added, or a new text — one undo step.
    Q_INVOKABLE void commitText(const QVariantMap &request, const QString &text);
    Q_INVOKABLE void cancelText();
    /// Edit the text (or the shape's label) `id` as a double tap would (tests, QML).
    Q_INVOKABLE void editText(const QString &id);
    /// Move the view so the scene rect is on screen above `bottomInset` px (a phone's keyboard).
    Q_INVOKABLE void ensureVisible(const QRectF &sceneRect, qreal bottomInset = 0);
    /// A stored colour as the paper shows it now (the tool bar's swatches): mapped in the dark
    /// (drawing::darkRgba), a highlighter's colour as it is.
    Q_INVOKABLE QColor displayColor(const QColor &stored, bool highlighter = false) const;
    /// Scene ↔ item coordinates.
    Q_INVOKABLE QPointF toScene(QPointF item) const;
    Q_INVOKABLE QPointF toItem(QPointF scene) const;

    /// The drawing as it is now (tests).
    drawing::Drawing currentDrawing() const { return m_doc; }
    /// Select the elements `ids` (with their groups and labels; tests).
    void select(const QStringList &ids);
    /// The clock of new strokes (ms since the epoch); tests fix it.
    void setClock(std::function<qint64()> clock) { m_clock = std::move(clock); }
    /// Render into `painter` for an item of `size` (paint(), tests).
    void render(QPainter *painter, const QSizeF &size);

    void paint(QPainter *painter) override;

Q_SIGNALS:
    void toolChanged();
    void colorChanged();
    void penWidthChanged();
    void fontSizeChanged();
    void selectionChanged();
    void selectionColorChanged();
    void textEditingChanged();
    /// Show the text editor: { id ("" a new text), containerId (a shape's label), sceneX, sceneY
    /// (the text's top left; a label: its shape's centre, valign "middle"), width (0: as wide as
    /// the text), text, fontSize, fontFamily (the font's family name), color (as shown), align
    /// ("left", "center", "right"), valign ("top", "middle") }.
    void textEditRequested(const QVariantMap &request);
    /// A press on the paper while a text is written: QML commits it.
    void textCommitRequested();
    void backgroundChanged();
    void themeChanged();
    void appDarkChanged();
    /// What the paper shows changed colours: `dark`, `paperColor` (a theme, the app's theme, a
    /// drawing loaded or taken in).
    void renderChanged();
    void fingerDrawsChanged();
    void stylusSeenChanged();
    void panModeChanged();
    void interactiveChanged();
    void viewChanged();
    void historyChanged();
    void modifiedChanged();
    void loadedChanged();
    void busyChanged();
    /// The drawing changed (a stroke, erasing, undo, redo, the background, the theme): QML saves
    /// later.
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
    struct Op {
        enum Kind { Elements, Background, Theme } kind = Elements;
        /// Elements: what changed (core drawing::diffElements).
        drawing::ElementDiff diff;
        drawing::Background before = drawing::Background::Plain;
        drawing::Background after = drawing::Background::Plain;
        drawing::PaperTheme themeBefore = drawing::PaperTheme::Auto;
        drawing::PaperTheme themeAfter = drawing::PaperTheme::Auto;
    };
    enum class Gesture { None, Draw, Erase, Pan, Pinch, Lasso, Move, Shape, Tap };
    /// What a pointer event is (from its device and the tablet event it was made of).
    struct Pointer {
        bool stylus = false;
        bool eraser = false;
        bool touch = false;
        double pressure = 0.5;
        Qt::KeyboardModifiers modifiers;
    };

    void filterWindow(QQuickWindow *window);
    /// Repaint in the colours of the theme in force when `wasDark` differs from it.
    void renderMaybeChanged(bool wasDark);
    Pointer pointerOf(const QPointerEvent *event) const;
    /// A finger (with the keyboard's modifiers).
    static Pointer touchPointer();
    /// A tablet event that passed the window: true when the canvas took it (see the header).
    bool handleTablet(QTabletEvent *event);
    /// The canvas is the topmost item taking input at `scenePos` (no popup, toast or bar over it).
    bool onTop(QPointF scenePos) const;
    /// A mouse event made of a stylus's tablet event the canvas already took (Windows makes them).
    bool fromTablet(const QMouseEvent *event) const;
    void noteStylus();
    /// A pen lands while fingers move the paper or draw: their gesture ends (a stroke just begun
    /// is dropped), and touches are ignored until every finger lifts.
    void yieldTouchToPen();
    void setGesture(Gesture g);

    // The tool's gesture (drawingcanvas_tools.cpp): begun by a press that works the tool, moved,
    // ended (or cancelled: a pinch, the pen taking over).
    void beginToolGesture(QPointF itemPos, const Pointer &p);
    void moveToolGesture(QPointF itemPos, const Pointer &p);
    void endToolGesture(QPointF itemPos, const Pointer &p);
    void cancelToolGesture();
    bool isShapeTool() const;
    void beginSelect(QPointF itemPos, const Pointer &p);
    void lassoTo(QPointF itemPos);
    void finishLasso(QPointF itemPos, const Pointer &p);
    void moveSelectionTo(QPointF itemPos);
    void finishMove(QPointF itemPos, const Pointer &p);
    void beginShape(QPointF itemPos, const Pointer &p);
    void shapeTo(QPointF itemPos, const Pointer &p);
    void finishShape(QPointF itemPos, const Pointer &p);
    void finishTap(QPointF itemPos, const Pointer &p);
    /// A tap with the select tool (a double tap edits a text or a shape's label).
    void selectTap(QPointF itemPos, const Pointer &p);
    /// A tap with the text tool.
    void textTap(QPointF itemPos, const Pointer &p);
    /// The topmost unlocked element at `itemPos` (−1 none).
    qsizetype elementAt(QPointF itemPos, const Pointer &p) const;
    /// The topmost unlocked text, or shape whose inside holds `itemPos` (a label's place; −1 none).
    qsizetype textTargetAt(QPointF itemPos, const Pointer &p) const;
    /// Ask QML for the text editor on the text or the label of the shape at `index`, or a new text
    /// at `scene`.
    void requestTextEdit(qsizetype index, QPointF scene);
    void endTextEdit();
    void setSelection(const QSet<QString> &ids);
    /// The selection's frame in scene coordinates (empty: none).
    QRectF selectionSceneRect() const;
    drawing::EditStamp stamp() const;
    /// Change the elements with `edit` (one undo step when anything changed).
    bool editElements(const std::function<void(drawing::Drawing &)> &edit);

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
    /// m_prepared made to match m_doc.strokes (elements whose version is unchanged reused).
    void syncPrepared();
    /// Selected ids that no longer exist dropped.
    void pruneSelection();
    /// The keys of elements hidden in the cached image (erased, moved, edited meanwhile).
    QSet<QString> hiddenKeys() const;

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
    void drawIntoCache(qsizetype index);
    void updateLive();
    QString newId() const;

    // The drawing.
    drawing::Drawing m_doc;
    /// The elements of m_doc prepared for painting, index by index.
    QList<Prepared> m_prepared;
    /// The decoded pictures of m_doc's files.
    mutable PictureCache m_pictures;
    /// The paper as the light and the dark theme show it (drawing::displayPaper).
    QColor m_paperLight = QColor(u"#ffffff");
    QColor m_paperDark = QColor(u"#121212");
    bool m_appDark = false;
    bool m_interactive = true;
    bool m_editable = true;
    /// The file's format and, for an Excalidraw plugin file, its Markdown around the scene (the
    /// last loaded or taken in); m_writable false: shown only.
    drawing::FileFormat m_format = drawing::FileFormat::Svg;
    bool m_formatKnown = false;
    std::shared_ptr<const drawing::ExcalidrawMarkdown> m_markdown;
    bool m_writable = true;
    bool m_modified = false;
    /// The drawing at the last load or save (takeExternal() merges against it).
    drawing::MergeBase m_base;
    /// The file as last loaded, taken in or written.
    QByteArray m_known;
    QList<Op> m_undo;
    QList<Op> m_redo;

    // Tools.
    QString m_tool = QStringLiteral("pen");
    QColor m_color = QColor(u"#1f2430");
    qreal m_penWidth = 1;
    qreal m_fontSize = 20;
    QColor m_selectionColor = QColor(u"#2c6aa3");
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
    QSet<QString> m_erasing; ///< keys of the elements the erasing gesture removes (hidden meanwhile)
    /// The selected elements' ids.
    QSet<QString> m_selected;
    /// Select, text: where the gesture began (item coordinates), the lasso (item coordinates).
    QPointF m_gestureStart;
    QPolygonF m_lasso;
    double m_lassoLength = 0;
    /// Move: how far the selection is dragged (item coordinates).
    QPointF m_moveDelta;
    /// Shape: the shape being dragged.
    drawing::Stroke m_liveShape;
    bool m_liveShapeShown = false;
    /// The last tap (a double tap edits text).
    QElapsedTimer m_lastTap;
    QPointF m_lastTapPos{-1e9, -1e9};
    /// Text: the request QML is editing; the element hidden meanwhile ("" none).
    bool m_textEditing = false;
    QString m_textEditKey;
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
    /// The gesture in progress is the fingers' (a pen landing ends it: yieldTouchToPen()).
    bool m_touchGesture = false;
    /// Touches are left alone until every finger lifts (they were down while the pen drew).
    bool m_ignoreTouch = false;
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
        bool dark = false;
        QPainterPath live;
        QColor liveColor; ///< with its opacity
        QPointF eraser{-1, -1};
        double eraserRadius = 0;
        /// Elements drawn on top (a moved selection, a shape being dragged) through `overlay`.
        QList<std::pair<drawing::Stroke, Prepared>> elements;
        QTransform overlay;
        QJsonObject files;
        PictureCache pictures;
        /// The lasso and the selection's frames (item coordinates).
        QPolygonF lasso;
        QRectF selection;
        QList<QRectF> selected;
        QColor selectionColor;
    } m_snapshot;

    std::function<qint64()> m_clock;
};

} // namespace brulion::drawingview
