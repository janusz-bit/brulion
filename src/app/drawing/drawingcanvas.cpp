#include "drawingcanvas.h"

#include "backdropimage.h"
#include "drawingrender.h"

#include "drawing/backdrop.h"
#include "drawing/drawingfile.h"
#include "drawing/geometry.h"
#include "drawing/palette.h"
#include "drawing/stroke.h"
#include "drawing/svgwrite.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QTabletEvent>

#include <algorithm>
#include <climits>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::drawingview {

namespace {

drawing::Background backgroundOf(const QString &name)
{
    return drawing::backgroundFromName(name);
}

const QStringList &tools()
{
    static const QStringList all{u"pen"_s,     u"highlighter"_s, u"eraser"_s, u"select"_s, u"rectangle"_s,
                                 u"ellipse"_s, u"diamond"_s,     u"line"_s,   u"arrow"_s,  u"text"_s};
    return all;
}

} // namespace

DrawingCanvas::DrawingCanvas(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAcceptTouchEvents(true);
    setAcceptHoverEvents(true);
    setOpaquePainting(true);
    setCursor(Qt::CrossCursor);
    m_settle.setSingleShot(true);
    m_settle.setInterval(160);
    connect(&m_settle, &QTimer::timeout, this, &DrawingCanvas::endViewGesture);
    // The selection's frame on screen moves with the view.
    connect(this, &DrawingCanvas::viewChanged, this, [this] {
        if (!m_selected.isEmpty())
            Q_EMIT selectionChanged();
    });
    m_clock = [] { return QDateTime::currentMSecsSinceEpoch(); };
    m_doc.paper = m_paperLight.name();
    // Tablet events pass the window first: the canvas takes the stylus's (see the header). (An
    // item made with a parent has its window before this constructor runs.)
    connect(this, &QQuickItem::windowChanged, this, &DrawingCanvas::filterWindow);
    filterWindow(window());
}

void DrawingCanvas::filterWindow(QQuickWindow *w)
{
    if (m_filteredWindow == w)
        return;
    if (m_filteredWindow)
        m_filteredWindow->removeEventFilter(this);
    m_filteredWindow = w;
    if (w)
        w->installEventFilter(this);
}

DrawingCanvas::~DrawingCanvas()
{
    if (m_filteredWindow)
        m_filteredWindow->removeEventFilter(this);
}

// --- Properties -------------------------------------------------------------------------------

void DrawingCanvas::setTool(const QString &tool)
{
    const QString t = tools().contains(tool) ? tool : u"pen"_s;
    if (t == m_tool)
        return;
    if (busy())
        cancelToolGesture();
    m_tool = t;
    m_hover = QPointF(-1, -1);
    // (The selection belongs to the select tool: another tool draws on.)
    if (t != u"select")
        clearSelection();
    setCursor(m_panMode ? Qt::OpenHandCursor
              : t == u"select" ? Qt::ArrowCursor
              : t == u"text"   ? Qt::IBeamCursor
                               : Qt::CrossCursor);
    update();
    Q_EMIT toolChanged();
}

bool DrawingCanvas::isShapeTool() const
{
    return m_tool == u"rectangle" || m_tool == u"ellipse" || m_tool == u"diamond" || m_tool == u"line"
           || m_tool == u"arrow";
}

void DrawingCanvas::setFontSize(qreal size)
{
    size = std::clamp(size, 1.0, 1000.0);
    if (!std::isfinite(size) || qFuzzyCompare(size, m_fontSize))
        return;
    m_fontSize = size;
    Q_EMIT fontSizeChanged();
}

void DrawingCanvas::setSelectionColor(const QColor &color)
{
    if (!color.isValid() || color == m_selectionColor)
        return;
    m_selectionColor = color;
    update();
    Q_EMIT selectionColorChanged();
}

void DrawingCanvas::setColor(const QColor &color)
{
    if (!color.isValid() || color == m_color)
        return;
    m_color = color;
    Q_EMIT colorChanged();
}

void DrawingCanvas::setPenWidth(qreal width)
{
    width = std::clamp(width, 0.1, 40.0);
    if (qFuzzyCompare(width, m_penWidth))
        return;
    m_penWidth = width;
    Q_EMIT penWidthChanged();
}

QString DrawingCanvas::background() const
{
    return drawing::backgroundName(m_doc.background);
}

void DrawingCanvas::setBackground(const QString &name)
{
    const drawing::Background b = backgroundOf(name);
    if (b == m_doc.background || !m_editable)
        return;
    Op op;
    op.kind = Op::Background;
    op.before = m_doc.background;
    op.after = b;
    apply(op, true);
    push(std::move(op));
}

QString DrawingCanvas::theme() const
{
    return drawing::paperThemeName(m_doc.theme);
}

void DrawingCanvas::setTheme(const QString &name)
{
    // (Allowed while a stroke is being drawn: it changes only how everything looks.)
    const drawing::PaperTheme t = drawing::paperThemeFromName(name);
    if (t == m_doc.theme || !m_editable)
        return;
    Op op;
    op.kind = Op::Theme;
    op.themeBefore = m_doc.theme;
    op.themeAfter = t;
    apply(op, true);
    push(std::move(op));
}

bool DrawingCanvas::dark() const
{
    return drawing::rendersDark(m_doc.theme, m_appDark);
}

void DrawingCanvas::setAppDark(bool on)
{
    if (on == m_appDark)
        return;
    const bool wasDark = dark();
    m_appDark = on;
    Q_EMIT appDarkChanged();
    renderMaybeChanged(wasDark);
}

void DrawingCanvas::renderMaybeChanged(bool wasDark)
{
    if (dark() == wasDark)
        return;
    invalidateCache();
    if (m_gesture == Gesture::Draw)
        updateLive();
    Q_EMIT renderChanged();
}

QColor DrawingCanvas::displayColor(const QColor &stored, bool highlighter) const
{
    if (!stored.isValid() || !dark() || highlighter)
        return stored;
    const drawing::Rgba c = drawing::darkRgba({stored.red(), stored.green(), stored.blue(), stored.alpha()});
    return QColor(c.r, c.g, c.b, c.a);
}

void DrawingCanvas::setFingerDraws(bool on)
{
    if (on == m_fingerDraws)
        return;
    m_fingerDraws = on;
    Q_EMIT fingerDrawsChanged();
}

void DrawingCanvas::setPanMode(bool on)
{
    if (on == m_panMode)
        return;
    m_panMode = on;
    setCursor(on ? Qt::OpenHandCursor
              : m_tool == u"select" ? Qt::ArrowCursor
              : m_tool == u"text"   ? Qt::IBeamCursor
                                    : Qt::CrossCursor);
    Q_EMIT panModeChanged();
}

void DrawingCanvas::setInteractive(bool on)
{
    if (on == m_interactive)
        return;
    m_interactive = on;
    if (!on) {
        // A preview: whatever was going on ends, nothing more reaches it.
        cancelStroke();
        if (busy())
            cancelToolGesture();
        endTextEdit();
        m_tabletGesture = false;
        m_hover = QPointF(-1, -1);
        ungrabMouse();
        ungrabTouchPoints();
        unsetCursor();
    } else {
        setCursor(m_panMode ? Qt::OpenHandCursor
                  : m_tool == u"select" ? Qt::ArrowCursor
                  : m_tool == u"text"   ? Qt::IBeamCursor
                                        : Qt::CrossCursor);
    }
    setAcceptedMouseButtons(on ? Qt::LeftButton | Qt::RightButton | Qt::MiddleButton : Qt::NoButton);
    setAcceptTouchEvents(on);
    setAcceptHoverEvents(on);
    if (!on)
        fitAll();
    update();
    Q_EMIT interactiveChanged();
}

void DrawingCanvas::setZoom(qreal zoom)
{
    zoom = std::clamp(zoom, MinZoom, MaxZoom);
    if (!std::isfinite(zoom) || qFuzzyCompare(zoom, m_zoom))
        return;
    m_zoom = zoom;
    viewMoved(false);
}

void DrawingCanvas::setCenterX(qreal x)
{
    if (!std::isfinite(x) || qFuzzyCompare(x, m_center.x()))
        return;
    m_center.setX(x);
    viewMoved(false);
}

void DrawingCanvas::setCenterY(qreal y)
{
    if (!std::isfinite(y) || qFuzzyCompare(y, m_center.y()))
        return;
    m_center.setY(y);
    viewMoved(false);
}

// --- The drawing ------------------------------------------------------------------------------

void DrawingCanvas::syncPrepared()
{
    // Reused while the element's key, version, versionNonce and `updated` stay (another device's
    // edit of the same version differs in them); elements sharing all of them (a duplicated id)
    // take the prepared ones of their kind in order.
    const auto stampOf = [](const QString &key, int version, int nonce, qint64 updated) {
        return key + QChar(0x1f) + QString::number(version) + QChar(0x1f) + QString::number(nonce) + QChar(0x1f)
               + QString::number(updated);
    };
    QHash<QString, QList<qsizetype>> old;
    old.reserve(m_prepared.size());
    for (qsizetype i = 0; i < m_prepared.size(); ++i) {
        const Prepared &p = m_prepared.at(i);
        old[stampOf(p.key, p.version, p.nonce, p.updated)].append(i);
    }
    QList<Prepared> previous = std::move(m_prepared);
    m_prepared.clear();
    m_prepared.reserve(m_doc.strokes.size());
    QList<qsizetype> fresh;
    qsizetype reusedHatch = 0;
    for (const drawing::Stroke &s : std::as_const(m_doc.strokes)) {
        const QString key = s.id.isEmpty() ? drawing::mergeKey(s) : s.id;
        const auto it = old.find(stampOf(key, s.version, s.versionNonce, s.updated));
        if (it != old.end() && !it->isEmpty()) {
            m_prepared.append(std::move(previous[it->takeFirst()]));
            reusedHatch += m_prepared.last().hatch.size();
        } else {
            fresh.append(m_prepared.size());
            m_prepared.append(Prepared());
        }
    }
    // The drawing's hatch lines stay within drawing::DrawingHatchLines (what is kept counts first).
    qsizetype budget = std::max<qsizetype>(0, drawing::DrawingHatchLines - reusedHatch);
    for (const qsizetype i : std::as_const(fresh)) {
        m_prepared[i] = prepare(m_doc.strokes.at(i), std::min(drawing::MaxHatchLines, budget));
        budget -= m_prepared.at(i).hatch.size();
    }
    prunePictures();
}

void DrawingCanvas::setDocument(drawing::Drawing doc)
{
    const bool sameFiles = doc.files == m_doc.files;
    m_doc = std::move(doc);
    if (!sameFiles)
        m_pictures.clear();
    syncPrepared();
    m_paperLight = toQColor(drawing::displayPaper(m_doc.paper, false));
    m_paperDark = toQColor(drawing::displayPaper(m_doc.paper, true));
    m_erasing.clear();
    invalidateCache();
}

QString DrawingCanvas::load(const QByteArray &bytes)
{
    cancelStroke();
    if (busy())
        cancelToolGesture();
    endTextEdit();
    QString status;
    drawing::Drawing doc;
    // A new drawing is an SVG (Brulion's own).
    m_format = drawing::FileFormat::Svg;
    m_formatKnown = bytes.isEmpty();
    m_markdown.reset();
    m_writable = true;
    if (!bytes.isEmpty()) {
        drawing::ReadError error = drawing::ReadError::None;
        std::optional<drawing::DrawingFile> read = drawing::readDrawingFile(bytes, &error);
        if (!read) {
            status = error == drawing::ReadError::Corrupt ? u"corrupt"_s : u"notDrawing"_s;
        } else {
            m_format = read->format;
            m_formatKnown = true;
            m_markdown = read->markdown;
            m_writable = read->writable;
            if (!read->writable || !drawing::editable(read->drawing))
                status = u"readonly"_s;
            else
                doc = std::move(read->drawing);
        }
    }
    m_editable = status.isEmpty();
    m_prepared.clear();
    setDocument(m_editable ? std::move(doc) : drawing::Drawing());
    m_base = drawing::mergeBase(m_doc);
    m_known = bytes;
    setSelection({});
    const bool hadHistory = canUndo() || canRedo();
    m_undo.clear();
    m_redo.clear();
    if (hadHistory)
        Q_EMIT historyChanged();
    setModified(false);
    m_viewSet = false;
    placeView();
    if (!m_interactive)
        fitAll(); // a preview shows the whole drawing
    Q_EMIT loadedChanged();
    Q_EMIT backgroundChanged();
    Q_EMIT themeChanged();
    Q_EMIT renderChanged();
    Q_EMIT contentChanged();
    return status;
}

bool DrawingCanvas::takeExternal(const QByteArray &bytes)
{
    std::optional<drawing::DrawingFile> file = drawing::readDrawingFile(bytes);
    if (!file || !file->writable || !drawing::editable(file->drawing) || !m_editable)
        return false;
    // Their file: its format and its Markdown around the scene (the other device's back of the
    // card and text entries); the merged scene goes into it.
    const bool formatChanged = !m_formatKnown || m_format != file->format;
    m_format = file->format;
    m_formatKnown = true;
    m_markdown = file->markdown;
    m_writable = true;
    std::optional<drawing::Drawing> theirs = std::move(file->drawing);
    // A gesture in progress goes on (a stroke is added when it ends, on top of the new elements);
    // the elements an erasing gesture has touched so far stay touched (by their keys).
    const drawing::MergeBase taken = drawing::mergeBase(*theirs);
    if (m_modified)
        *theirs = drawing::merge(m_base, m_doc, std::move(*theirs));
    const QSet<QString> erasing = m_erasing;
    setDocument(std::move(*theirs));
    m_erasing = erasing;
    m_base = taken;
    m_known = bytes;
    pruneSelection();
    if (canUndo() || canRedo()) {
        m_undo.clear();
        m_redo.clear();
        Q_EMIT historyChanged();
    }
    if (m_gesture == Gesture::Draw)
        updateLive(); // (in the colours of the theme taken in)
    if (formatChanged)
        Q_EMIT loadedChanged();
    Q_EMIT backgroundChanged();
    Q_EMIT themeChanged();
    Q_EMIT renderChanged();
    Q_EMIT contentChanged();
    return true;
}

QByteArray DrawingCanvas::fileBytes() const
{
    if (!m_editable || !m_writable)
        return {};
    drawing::DrawingFile file;
    file.format = m_format;
    file.markdown = m_markdown;
    return drawing::writeDrawingFile(file, m_doc, m_clock ? m_clock() : QDateTime::currentMSecsSinceEpoch(),
                                     svgOptions());
}

QByteArray DrawingCanvas::svg() const
{
    if (!m_editable)
        return {};
    return drawing::toSvg(m_doc, svgOptions());
}

drawing::SvgOptions DrawingCanvas::svgOptions() const
{
    drawing::SvgOptions options;
    const qsizetype backdrop = drawing::backdropIndex(m_doc);
    if (backdrop < 0 || m_doc.theme != drawing::PaperTheme::Dark)
        return options;
    const QString fileId = m_doc.strokes.at(backdrop).fileId;
    auto it = m_darkBackdrops.find(fileId);
    if (it == m_darkBackdrops.end()) {
        // Once per backdrop: the dark twin painted already when there is one, else made now.
        QImage dark = m_pictures.value(darkPictureKey(fileId));
        if (dark.isNull())
            dark = darkImage(m_pictures.contains(fileId) ? m_pictures.value(fileId)
                                                         : decodePicture(m_doc.files, fileId));
        const drawing::BackdropImage encoded = encodeBackdrop(dark);
        it = m_darkBackdrops.insert(fileId, encoded.bytes.isEmpty() ? QString() : drawing::dataUrl(encoded));
    }
    if (!it->isEmpty())
        options.pictureImages.insert(fileId, *it);
    return options;
}

bool DrawingCanvas::hasBackdrop() const
{
    return drawing::backdropIndex(m_doc) >= 0;
}

QString DrawingCanvas::backdropPath() const
{
    const std::optional<drawing::BackdropSource> source = drawing::backdropSource(m_doc);
    return source ? source->path : QString();
}

int DrawingCanvas::backdropPage() const
{
    const std::optional<drawing::BackdropSource> source = drawing::backdropSource(m_doc);
    return source ? source->page : 0;
}

QString DrawingCanvas::setBackdrop(const QVariantMap &prepared)
{
    if (!m_editable || !m_writable)
        return u"readonly"_s;
    if (busy() || m_textEditing)
        return u"busy"_s;
    const PreparedBackdrop p = fromVariant(prepared);
    if (!p.error.isEmpty() || !drawing::validBackdropImage(p.image))
        return u"invalid"_s;
    const drawing::EditStamp s = stamp();
    drawing::Drawing next = m_doc;
    if (!drawing::setBackdrop(next, p.image, p.source, s))
        return u"invalid"_s;
    // A scene full of pictures may leave no room for one more: never write a file that does not
    // open again.
    drawing::DrawingFile file;
    file.format = m_format;
    file.markdown = m_markdown;
    if (!drawing::readsBack(file, next, s.now))
        return u"tooLarge"_s;
    // (One undo step; push() reports the change.)
    return editElements([&next](drawing::Drawing &doc) { doc = std::move(next); }) ? QString() : u"invalid"_s;
}

void DrawingCanvas::removeBackdrop()
{
    if (!m_editable || !m_writable || busy() || m_textEditing)
        return;
    editElements([](drawing::Drawing &doc) { drawing::removeBackdrop(doc); });
}

void DrawingCanvas::markSaved(const QByteArray &written)
{
    if (!written.isEmpty()) {
        m_known = written;
        // The plugin file as written is the layout of the next save: elements of this save erased
        // later get their tombstones, text written now counts as read.
        if (m_formatKnown && m_format == drawing::FileFormat::ExcalidrawMarkdown) {
            if (std::optional<drawing::ExcalidrawMarkdownRead> read = drawing::readExcalidrawMarkdown(written))
                m_markdown = std::make_shared<const drawing::ExcalidrawMarkdown>(std::move(read->layout));
        }
    }
    m_base = drawing::mergeBase(m_doc);
    setModified(false);
}

void DrawingCanvas::setModified(bool on)
{
    if (on == m_modified)
        return;
    m_modified = on;
    Q_EMIT modifiedChanged();
}

void DrawingCanvas::changed()
{
    setModified(true);
    Q_EMIT contentChanged();
    if (!m_selected.isEmpty())
        Q_EMIT selectionChanged(); // (its frame)
}

QRectF DrawingCanvas::contentBounds() const
{
    QRectF out;
    for (const Prepared &p : m_prepared) {
        if (!p.bounds.isNull())
            out = out.isNull() ? p.bounds : out.united(p.bounds);
    }
    return out;
}

void DrawingCanvas::prunePictures()
{
    if (m_pictures.isEmpty() && m_darkBackdrops.isEmpty())
        return;
    QSet<QString> shown;
    for (const drawing::Stroke &e : std::as_const(m_doc.strokes)) {
        if (e.type == drawing::ElementType::Image && !e.fileId.isEmpty())
            shown.insert(e.fileId);
    }
    m_pictures.removeIf(
            [&shown](const PictureCache::iterator it) { return !shown.contains(fileIdOfPictureKey(it.key())); });
    m_darkBackdrops.removeIf(
            [&shown](const QHash<QString, QString>::iterator it) { return !shown.contains(it.key()); });
}

// --- Undo -------------------------------------------------------------------------------------

void DrawingCanvas::push(Op op)
{
    m_undo.append(std::move(op));
    if (m_undo.size() > 500)
        m_undo.removeFirst();
    m_redo.clear();
    Q_EMIT historyChanged();
    changed();
}

void DrawingCanvas::apply(const Op &op, bool forward)
{
    switch (op.kind) {
    case Op::Background:
        m_doc.background = forward ? op.after : op.before;
        Q_EMIT backgroundChanged();
        break;
    case Op::Theme: {
        const bool wasDark = dark();
        m_doc.theme = forward ? op.themeAfter : op.themeBefore;
        Q_EMIT themeChanged();
        renderMaybeChanged(wasDark);
        break;
    }
    case Op::Elements: {
        // Stamped as a new edit: another device's copy or the Excalidraw plugin merges by version.
        const drawing::EditStamp s = stamp();
        drawing::applyDiff(m_doc.strokes, op.diff, forward, &s);
        syncPrepared();
        pruneSelection();
        break;
    }
    }
    invalidateCache();
}

void DrawingCanvas::undo()
{
    if (m_undo.isEmpty() || busy())
        return;
    Op op = m_undo.takeLast();
    apply(op, false);
    m_redo.append(std::move(op));
    Q_EMIT historyChanged();
    changed();
}

void DrawingCanvas::redo()
{
    if (m_redo.isEmpty() || busy())
        return;
    Op op = m_redo.takeLast();
    apply(op, true);
    m_undo.append(std::move(op));
    Q_EMIT historyChanged();
    changed();
}

// --- The view ---------------------------------------------------------------------------------

QTransform DrawingCanvas::sceneToItem() const
{
    QTransform t;
    t.translate(width() / 2, height() / 2);
    t.scale(m_zoom, m_zoom);
    t.translate(-m_center.x(), -m_center.y());
    return t;
}

QPointF DrawingCanvas::toScene(QPointF item) const
{
    return (item - QPointF(width() / 2, height() / 2)) / m_zoom + m_center;
}

QPointF DrawingCanvas::toItem(QPointF scene) const
{
    return (scene - m_center) * m_zoom + QPointF(width() / 2, height() / 2);
}

void DrawingCanvas::placeView()
{
    if (m_viewSet || width() <= 0 || height() <= 0)
        return;
    constexpr double margin = 24;
    const QRectF b = contentBounds();
    if (b.isNull()) {
        m_zoom = 1;
        m_center = QPointF(width() / 2, height() / 2); // the scene's origin at the top left
    } else {
        // From the top left of the strokes, at zoom 1 — or as wide as the view (notes are read
        // down the page: a tall page is scrolled, not shrunk).
        m_zoom = std::clamp(std::min(1.0, (width() - 2 * margin) / std::max(1.0, b.width())), MinZoom, MaxZoom);
        m_center = b.topLeft() + (QPointF(width() / 2, height() / 2) - QPointF(margin, margin)) / m_zoom;
    }
    m_viewSet = true;
    invalidateCache();
    Q_EMIT viewChanged();
}

void DrawingCanvas::fitAll()
{
    const QRectF b = contentBounds();
    if (b.isNull() || width() <= 0 || height() <= 0) {
        m_zoom = 1;
        m_center = QPointF(width() / 2, height() / 2);
    } else {
        constexpr double margin = 24;
        const double z = std::min({(width() - 2 * margin) / std::max(1.0, b.width()),
                                   (height() - 2 * margin) / std::max(1.0, b.height()), 1.0});
        m_zoom = std::clamp(z, MinZoom, MaxZoom);
        // Centred across, from the top down (room left over stays below, where writing goes on);
        // a preview, where nothing is written, centred both ways.
        m_center = m_interactive ? QPointF(b.center().x(), b.top() - margin / m_zoom + height() / (2 * m_zoom))
                                 : b.center();
    }
    viewMoved(false);
}

void DrawingCanvas::zoomBy(qreal factor, qreal x, qreal y)
{
    if (!std::isfinite(factor) || factor <= 0)
        return;
    const QPointF anchor = x < 0 || y < 0 ? QPointF(width() / 2, height() / 2) : QPointF(x, y);
    const QPointF scene = toScene(anchor);
    m_zoom = std::clamp(m_zoom * factor, MinZoom, MaxZoom);
    m_center = scene - (anchor - QPointF(width() / 2, height() / 2)) / m_zoom;
    viewMoved(true);
}

void DrawingCanvas::panBy(qreal dx, qreal dy)
{
    if (!std::isfinite(dx) || !std::isfinite(dy))
        return;
    m_center -= QPointF(dx, dy) / m_zoom;
    viewMoved(true);
}

void DrawingCanvas::viewMoved(bool byUser)
{
    // The paper ends where strokes may reach (drawing::CoordinateLimit).
    m_center = QPointF(std::clamp(m_center.x(), -drawing::CoordinateLimit, drawing::CoordinateLimit),
                       std::clamp(m_center.y(), -drawing::CoordinateLimit, drawing::CoordinateLimit));
    m_viewSet = true;
    if (m_gesture == Gesture::Draw)
        updateLive();
    update();
    Q_EMIT viewChanged();
    if (byUser)
        Q_EMIT userMovedView();
}

void DrawingCanvas::endViewGesture()
{
    if (m_gesture == Gesture::Pan || m_gesture == Gesture::Pinch)
        return; // still moving: ends with the gesture
    m_interactingView = false;
    update();
}

void DrawingCanvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() == oldGeometry.size())
        return;
    if (!m_interactive)
        fitAll(); // a preview shows the whole drawing
    else if (!m_viewSet)
        placeView();
    else if (oldGeometry.width() > 0 && oldGeometry.height() > 0)
        // The scene point at the top left stays there (a phone's keyboard, a resized window).
        m_center += QPointF(newGeometry.width() - oldGeometry.width(), newGeometry.height() - oldGeometry.height())
                    / (2 * m_zoom);
    invalidateCache();
    if (!m_selected.isEmpty())
        Q_EMIT selectionChanged();
}

void DrawingCanvas::itemChange(ItemChange change, const ItemChangeData &value)
{
    if (change == ItemDevicePixelRatioHasChanged) {
        invalidateCache();
    }
    QQuickPaintedItem::itemChange(change, value);
}

// --- Rendering --------------------------------------------------------------------------------

void DrawingCanvas::invalidateCache()
{
    m_cacheValid = false;
    update();
}

void DrawingCanvas::renderCache()
{
    QElapsedTimer timer;
    timer.start();
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const QSize pixels = (QSizeF(width(), height()) * dpr).toSize();
    m_cacheZoom = m_zoom;
    m_cacheCenter = m_center;
    m_cacheSize = QSizeF(width(), height());
    m_cacheValid = true;
    if (pixels.isEmpty()) {
        m_cache = QImage();
        return;
    }
    // A new image when the snapshot of the render thread still shares this one.
    if (m_cache.size() != pixels || !m_cache.isDetached())
        m_cache = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
    m_cache.setDevicePixelRatio(dpr);
    // (Every pixel: the image is reused, and the paper's rectangle may end inside the last pixel.)
    m_cache.fill(paperColor());
    QPainter p(&m_cache);
    const bool shownDark = dark();
    // The paper, its lines or grid at the scene's multiples of the spacing, as the SVG has them.
    paintPaper(p, m_doc, shownDark, sceneToItem(), QSizeF(width(), height()), dpr);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QRectF visible(toScene(QPointF(0, 0)), toScene(QPointF(width(), height())));
    p.setPen(Qt::NoPen);
    p.setTransform(sceneToItem(), true);
    const QSet<QString> hidden = hiddenKeys();
    const qsizetype backdrop = drawing::backdropIndex(m_doc);
    for (qsizetype i = 0; i < m_prepared.size(); ++i) {
        const Prepared &prepared = m_prepared.at(i);
        if (!prepared.bounds.intersects(visible) || (!hidden.isEmpty() && hidden.contains(prepared.key)))
            continue;
        paintElement(p, m_doc.strokes.at(i), prepared, shownDark, m_doc.files, m_pictures, i == backdrop);
    }
    p.end();
    m_lastRenderMs = double(timer.nsecsElapsed()) / 1e6;
}

void DrawingCanvas::drawIntoCache(qsizetype index)
{
    if (!m_cacheValid || m_cache.isNull() || m_cacheZoom != m_zoom || m_cacheCenter != m_center) {
        invalidateCache();
        return;
    }
    QPainter p(&m_cache); // detaches from a snapshot the render thread may be drawing
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setTransform(sceneToItem(), true);
    paintElement(p, m_doc.strokes.at(index), m_prepared.at(index), dark(), m_doc.files, m_pictures,
                 index == drawing::backdropIndex(m_doc));
}

QSet<QString> DrawingCanvas::hiddenKeys() const
{
    QSet<QString> hidden = m_erasing;
    if (m_gesture == Gesture::Move)
        hidden.unite(m_selected);
    if (!m_textEditKey.isEmpty())
        hidden.insert(m_textEditKey);
    return hidden;
}

void DrawingCanvas::updateLive()
{
    const QRectF before = m_liveDirty;
    if (m_live.points.isEmpty()) {
        m_livePath = QPainterPath();
        m_liveDirty = QRectF();
    } else {
        drawing::OutlineOptions o = drawing::outlineOptions(m_live, false);
        QList<QPointF> outline = drawing::strokeOutline(m_live.points, o);
        const QTransform t = sceneToItem();
        for (QPointF &p : outline)
            p = t.map(p);
        m_livePath = outlinePath(outline);
        m_liveDirty = m_livePath.boundingRect();
    }
    const QRectF dirty = before.isNull() ? m_liveDirty : m_liveDirty.isNull() ? before : before.united(m_liveDirty);
    if (!dirty.isNull())
        update(dirty.toAlignedRect().adjusted(-3, -3, 3, 3));
}

void DrawingCanvas::prepareSnapshot()
{
    const bool current = m_cacheValid && m_cacheZoom == m_zoom && m_cacheCenter == m_center
                         && m_cacheSize == QSizeF(width(), height());
    if (!current && !m_interactingView)
        renderCache();
    Snapshot s;
    s.cache = m_cache;
    s.cacheCurrent = m_cacheValid && m_cacheZoom == m_zoom && m_cacheCenter == m_center;
    if (!s.cacheCurrent && m_cacheZoom > 0) {
        // The cached view moved and scaled to the current one (item_now ← item_then).
        QTransform t;
        t.translate(width() / 2, height() / 2);
        t.scale(m_zoom, m_zoom);
        t.translate(m_cacheCenter.x() - m_center.x(), m_cacheCenter.y() - m_center.y());
        t.scale(1 / m_cacheZoom, 1 / m_cacheZoom);
        t.translate(-m_cacheSize.width() / 2, -m_cacheSize.height() / 2);
        s.cacheToItem = t;
    }
    s.paper = paperColor();
    s.dark = dark();
    s.live = m_livePath;
    s.liveColor = toQColor(drawing::displayInk(m_live, s.dark));
    if (m_gesture == Gesture::Erase || (m_tool == u"eraser" && m_gesture == Gesture::None && m_hover.x() >= 0)) {
        s.eraser = m_gesture == Gesture::Erase ? m_lastItemPos : m_hover;
        s.eraserRadius = EraserRadius;
    }
    // On top: a moved selection, a shape being dragged.
    s.overlay = sceneToItem();
    if (m_gesture == Gesture::Move) {
        QTransform moved;
        moved.translate(m_moveDelta.x(), m_moveDelta.y());
        s.overlay = s.overlay * moved;
        for (qsizetype i = 0; i < m_doc.strokes.size(); ++i) {
            if (m_selected.contains(m_prepared.at(i).key))
                s.elements.append({m_doc.strokes.at(i), m_prepared.at(i)});
        }
    } else if (m_gesture == Gesture::Shape && m_liveShapeShown) {
        s.elements.append({m_liveShape, prepare(m_liveShape)});
    }
    if (!s.elements.isEmpty()) {
        s.files = m_doc.files;
        s.pictures = m_pictures;
    }
    s.selectionColor = m_selectionColor;
    if (m_gesture == Gesture::Lasso)
        s.lasso = m_lasso;
    if (!m_selected.isEmpty() && m_gesture != Gesture::Lasso) {
        const QTransform t = s.overlay;
        for (qsizetype i = 0; i < m_doc.strokes.size(); ++i) {
            if (m_selected.contains(m_prepared.at(i).key) && m_prepared.at(i).key != m_textEditKey)
                s.selected.append(t.mapRect(m_prepared.at(i).bounds));
        }
        s.selection = selectionRect();
    }
    m_snapshot = std::move(s);
}

void DrawingCanvas::paintSnapshot(QPainter *painter, const QSizeF &size) const
{
    const Snapshot &s = m_snapshot;
    if (!s.cacheCurrent || s.cache.isNull())
        painter->fillRect(QRectF(QPointF(0, 0), size), s.paper);
    if (!s.cache.isNull()) {
        painter->save();
        if (!s.cacheCurrent) {
            painter->setRenderHint(QPainter::SmoothPixmapTransform);
            painter->setTransform(s.cacheToItem, true);
        }
        painter->drawImage(QPointF(0, 0), s.cache);
        painter->restore();
    }
    painter->setRenderHint(QPainter::Antialiasing);
    if (!s.elements.isEmpty()) {
        painter->save();
        painter->setTransform(s.overlay, true);
        PictureCache pictures = s.pictures;
        for (const auto &[element, prepared] : s.elements)
            paintElement(*painter, element, prepared, s.dark, s.files, pictures);
        painter->restore();
    }
    if (!s.live.isEmpty())
        painter->fillPath(s.live, s.liveColor);
    if (!s.selection.isEmpty()) {
        // The selection's frame: dashed around all, thin around each when there are several.
        QColor faint = s.selectionColor;
        faint.setAlphaF(0.45);
        painter->setBrush(Qt::NoBrush);
        if (s.selected.size() > 1) {
            painter->setPen(QPen(faint, 1));
            for (const QRectF &r : s.selected)
                painter->drawRect(r.adjusted(-2, -2, 2, 2));
        }
        QPen frame(s.selectionColor, 1.5, Qt::DashLine);
        painter->setPen(frame);
        painter->drawRect(s.selection);
    }
    if (s.lasso.size() > 1) {
        QColor fill = s.selectionColor;
        fill.setAlphaF(0.1);
        painter->setBrush(fill);
        painter->setPen(QPen(s.selectionColor, 1.5, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPolygon(s.lasso);
    }
    if (s.eraser.x() >= 0) {
        // A ring that shows on the paper of either theme.
        painter->setPen(QPen(s.dark ? QColor(220, 224, 232, 160) : QColor(31, 36, 48, 150), 1));
        painter->setBrush(s.dark ? QColor(0, 0, 0, 90) : QColor(255, 255, 255, 90));
        painter->drawEllipse(s.eraser, s.eraserRadius, s.eraserRadius);
    }
}

QSGNode *DrawingCanvas::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data)
{
    prepareSnapshot(); // the GUI thread waits here: the state is consistent
    return QQuickPaintedItem::updatePaintNode(oldNode, data);
}

void DrawingCanvas::paint(QPainter *painter)
{
    paintSnapshot(painter, size());
}

void DrawingCanvas::render(QPainter *painter, const QSizeF &size)
{
    prepareSnapshot();
    paintSnapshot(painter, size);
}

// --- Input ------------------------------------------------------------------------------------

bool DrawingCanvas::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_interactive)
        return QQuickPaintedItem::eventFilter(watched, event); // a preview leaves the pen alone
    switch (event->type()) {
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
        if (handleTablet(static_cast<QTabletEvent *>(event))) {
            event->accept(); // no mouse events made of it
            return true;
        }
        break;
    default: break;
    }
    return QQuickPaintedItem::eventFilter(watched, event);
}

namespace {

/// The pen's eraser end by its device: an eraser (the desktops), or Qt for Android's — which
/// still names the pointer type with Qt 5's QTabletEvent::Eraser (3; its pen is 1, Qt 5's Pen),
/// a value no device of Qt 6 has (Generic | Finger).
bool eraserDevice(QPointingDevice::PointerType type)
{
    return type == QPointingDevice::PointerType::Eraser || int(type) == 3;
}

/// The topmost visible, enabled item under `scenePos` that takes mouse or touch input.
QQuickItem *topInputItem(QQuickItem *item, QPointF scenePos)
{
    if (!item->isVisible() || !item->isEnabled() || qFuzzyIsNull(item->opacity()))
        return nullptr;
    if (item->inherits("QQuickOverlay")) {
        // Qt Quick Controls' popup layer: visible over the whole window whenever a Drawer exists
        // or any item is put there (the shell's toasts), and it takes every button. It lies over
        // the canvas only where an open popup is — or everywhere while a modal one is open (the
        // popup item's QObject parent is the popup) — and where an item put there takes input
        // (a toast, not the window-wide host of the toasts).
        QList<QQuickItem *> children = item->childItems();
        std::stable_sort(children.begin(), children.end(),
                         [](QQuickItem *a, QQuickItem *b) { return a->z() < b->z(); });
        for (auto it = children.crbegin(); it != children.crend(); ++it) {
            QQuickItem *child = *it;
            if (!child->isVisible())
                continue;
            if (child->parent() && child->parent()->inherits("QQuickPopup")) {
                if (child->contains(child->mapFromScene(scenePos)) || child->parent()->property("modal").toBool())
                    return child;
                continue;
            }
            if (QQuickItem *hit = topInputItem(child, scenePos))
                return hit;
        }
        return nullptr;
    }
    const QPointF local = item->mapFromScene(scenePos);
    if (item->clip() && !item->contains(local))
        return nullptr;
    QList<QQuickItem *> children = item->childItems();
    std::stable_sort(children.begin(), children.end(), [](QQuickItem *a, QQuickItem *b) { return a->z() < b->z(); });
    for (auto it = children.crbegin(); it != children.crend(); ++it) {
        if (QQuickItem *hit = topInputItem(*it, scenePos))
            return hit;
    }
    if (item->contains(local) && (item->acceptedMouseButtons() != Qt::NoButton || item->acceptTouchEvents()))
        return item;
    return nullptr;
}

} // namespace

bool DrawingCanvas::onTop(QPointF scenePos) const
{
    QQuickWindow *w = window();
    if (!w || !m_interactive || !isVisible() || !isEnabled() || !contains(mapFromScene(scenePos)))
        return false;
    return topInputItem(w->contentItem(), scenePos) == this;
}

bool DrawingCanvas::handleTablet(QTabletEvent *event)
{
    const QPointF pos = mapFromScene(event->position());
    // The eraser end: the desktops and Android report an eraser device (eraserDevice()); Qt for
    // WebAssembly has one pen device and passes the web's eraser button (button 5, `buttons & 32`)
    // as ExtraButton3. A held barrel button erases too: RightButton, and on Android
    // BUTTON_STYLUS_PRIMARY (32), which Qt for Android passes as ExtraButton3 as well.
    const bool eraser = eraserDevice(event->pointerType()) || (event->buttons() & (Qt::RightButton | Qt::ExtraButton3));
    const Pointer p{true, eraser, false, std::clamp(double(event->pressure()), 0.0, 1.0), event->modifiers()};
    const auto remember = [this, event] {
        m_lastTabletTime = event->timestamp();
        m_lastTabletGlobal = event->globalPosition();
    };
    switch (event->type()) {
    case QEvent::TabletPress:
        if (m_tabletGesture) {
            remember();
            return true; // another button of the pen held down
        }
        if (!onTop(event->position()))
            return false;
        if (m_gesture != Gesture::None) {
            // The palm (or a finger) got there first: the pen wins, the hand is left alone until
            // it lifts. A gesture of the mouse goes on.
            if (!m_touchGesture)
                return false;
            yieldTouchToPen();
        }
        remember();
        noteStylus();
        if (m_textEditing) {
            // A press beside the text being written: QML commits it.
            Q_EMIT textCommitRequested();
            return true;
        }
        m_tabletGesture = true;
        m_hover = QPointF(-1, -1);
        if (m_panMode || !m_editable)
            beginPan(pos);
        else
            beginToolGesture(pos, p);
        return true;
    case QEvent::TabletMove:
        if (!m_tabletGesture) {
            // Hovering: the eraser's ring follows the pen.
            if (m_tool == u"eraser" || eraser) {
                m_hover = contains(pos) ? pos : QPointF(-1, -1);
                update();
            }
            return false;
        }
        remember();
        moveToolGesture(pos, p);
        return true;
    case QEvent::TabletRelease:
        if (!m_tabletGesture)
            return false;
        remember();
        if (event->buttons() != Qt::NoButton)
            return true; // a button let go, the tip still down
        m_tabletGesture = false;
        if (m_gesture == Gesture::Draw && !m_live.points.isEmpty()) {
            // The release tells the pen lifted (pressure 0: the browser, Android), on Android a
            // pixel beside the last move (it resamples moves): the line ends with the pressure it
            // had, a stylus pressing one pressure stays without pressure.
            Pointer up = p;
            up.pressure = m_live.points.last().pressure;
            endToolGesture(pos, up);
            return true;
        }
        endToolGesture(pos, p);
        return true;
    default: break;
    }
    return false;
}

bool DrawingCanvas::fromTablet(const QMouseEvent *event) const
{
    if (m_tabletGesture)
        return true;
    const quint64 a = event->timestamp();
    const quint64 b = m_lastTabletTime;
    const QPointF delta = event->globalPosition() - m_lastTabletGlobal;
    return (a > b ? a - b : b - a) <= 500 && std::abs(delta.x()) + std::abs(delta.y()) < 12;
}

DrawingCanvas::Pointer DrawingCanvas::pointerOf(const QPointerEvent *event) const
{
    Pointer p;
    if (const QPointingDevice *device = event->pointingDevice()) {
        p.stylus = device->type() == QInputDevice::DeviceType::Stylus
                   || device->type() == QInputDevice::DeviceType::Airbrush;
        p.eraser = eraserDevice(device->pointerType());
        p.touch = device->type() == QInputDevice::DeviceType::TouchScreen;
    }
    if (const auto *mouse = dynamic_cast<const QSinglePointEvent *>(event))
        p.modifiers = mouse->modifiers();
    return p;
}

void DrawingCanvas::noteStylus()
{
    if (m_stylusSeen)
        return;
    m_stylusSeen = true;
    Q_EMIT stylusSeenChanged();
}

void DrawingCanvas::setGesture(Gesture g)
{
    const bool wasBusy = busy();
    m_gesture = g;
    if (g == Gesture::Pan || g == Gesture::Pinch)
        m_interactingView = true;
    if (busy() != wasBusy)
        Q_EMIT busyChanged();
}

void DrawingCanvas::mousePressEvent(QMouseEvent *event)
{
    if (m_gesture != Gesture::None || m_ignoreMouse) {
        event->accept(); // another button during a gesture
        return;
    }
    if (fromTablet(event)) {
        m_ignoreMouse = true; // the stylus drew it already
        event->accept();
        return;
    }
    const Pointer p = pointerOf(event);
    if (p.stylus)
        noteStylus();
    const QPointF pos = event->position();
    const Qt::MouseButton button = event->button();
    if (m_textEditing && button == Qt::LeftButton) {
        // A press beside the text being written: QML commits it.
        Q_EMIT textCommitRequested();
        event->accept();
        return;
    }
    if (button == Qt::MiddleButton || (button == Qt::RightButton && !p.stylus)
        || (button == Qt::LeftButton && (m_panMode || !m_editable))) {
        beginPan(pos);
    } else if (button == Qt::RightButton) {
        beginErase(pos); // the stylus's barrel button
    } else if (button == Qt::LeftButton) {
        beginToolGesture(pos, p); // (its eraser end, the eraser tool: erasing)
    } else {
        event->ignore();
        return;
    }
    event->accept();
}

void DrawingCanvas::mouseMoveEvent(QMouseEvent *event)
{
    event->accept();
    if (m_ignoreMouse || m_tabletGesture)
        return;
    moveToolGesture(event->position(), pointerOf(event));
}

void DrawingCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    event->accept();
    if (m_ignoreMouse || m_tabletGesture) {
        if (event->buttons() == Qt::NoButton)
            m_ignoreMouse = false;
        return;
    }
    if (event->buttons() != Qt::NoButton && m_gesture != Gesture::None)
        return; // another button let go; the gesture's own is still held
    endToolGesture(event->position(), pointerOf(event));
}

void DrawingCanvas::mouseUngrabEvent()
{
    m_ignoreMouse = false;
    if (m_tabletGesture)
        return; // the pen's gesture goes on (the tablet events are the canvas's own)
    switch (m_gesture) {
    case Gesture::Draw: finishStroke(); break;
    case Gesture::Erase: finishErase(); break;
    case Gesture::Pan:
    case Gesture::Pinch:
        setGesture(Gesture::None);
        endViewGesture();
        break;
    case Gesture::Lasso:
    case Gesture::Move:
    case Gesture::Shape:
    case Gesture::Tap: cancelToolGesture(); break;
    case Gesture::None: break;
    }
}

void DrawingCanvas::hoverMoveEvent(QHoverEvent *event)
{
    if (m_tool == u"eraser") {
        m_hover = event->position();
        update();
    }
    QQuickPaintedItem::hoverMoveEvent(event);
}

void DrawingCanvas::hoverLeaveEvent(QHoverEvent *event)
{
    if (m_hover.x() >= 0) {
        m_hover = QPointF(-1, -1);
        update();
    }
    QQuickPaintedItem::hoverLeaveEvent(event);
}

void DrawingCanvas::wheelEvent(QWheelEvent *event)
{
    if (!m_interactive) {
        event->ignore(); // (the list behind a preview scrolls)
        return;
    }
    m_interactingView = true;
    m_settle.start();
    if (event->modifiers() & Qt::ControlModifier) {
        const double steps = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->pixelDelta().y() * 4.0;
        zoomBy(std::pow(1.0015, steps), event->position().x(), event->position().y());
    } else {
        QPointF delta = !event->pixelDelta().isNull() ? QPointF(event->pixelDelta())
                                                      : QPointF(event->angleDelta()) / 120.0 * 48.0;
        if ((event->modifiers() & Qt::ShiftModifier) && qFuzzyIsNull(delta.x()))
            delta = QPointF(delta.y(), 0);
        panBy(delta.x(), delta.y());
    }
    event->accept();
}

bool DrawingCanvas::event(QEvent *event)
{
    if (event->type() == QEvent::NativeGesture && m_interactive) {
        // A touchpad's pinch (macOS, Wayland, Windows precision touchpads).
        const auto *g = static_cast<QNativeGestureEvent *>(event);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
            m_interactingView = true;
            m_settle.start();
            zoomBy(1 + g->value(), g->position().x(), g->position().y());
            return true;
        }
    }
    return QQuickPaintedItem::event(event);
}

void DrawingCanvas::yieldTouchToPen()
{
    switch (m_gesture) {
    case Gesture::Draw:
        // A finger's stroke the pen lands in: the palm, unless it has been drawing for a while.
        if (m_liveClock.elapsed() < 300 || m_live.points.size() < 8)
            cancelStroke();
        else
            finishStroke();
        break;
    case Gesture::Erase: finishErase(); break;
    case Gesture::Pan:
    case Gesture::Pinch:
        setGesture(Gesture::None);
        endViewGesture();
        break;
    case Gesture::Lasso:
    case Gesture::Move:
    case Gesture::Shape:
    case Gesture::Tap: cancelToolGesture(); break;
    case Gesture::None: break;
    }
    m_touchGesture = false;
    m_touchDrawId = -1;
    m_ignoreTouch = true;
}

void DrawingCanvas::touchEvent(QTouchEvent *event)
{
    if (event->type() == QEvent::TouchCancel) {
        touchUngrabEvent();
        event->accept();
        return;
    }
    QList<const QEventPoint *> down;
    for (const QEventPoint &p : event->points()) {
        if (p.state() != QEventPoint::State::Released)
            down.append(&p);
    }
    if (m_tabletGesture || m_ignoreTouch) {
        // The pen is drawing, erasing or moving the paper: a hand on the screen (the palm) does
        // nothing — not even after the pen lifts, until every finger has lifted too.
        m_ignoreTouch = !down.isEmpty();
        event->accept();
        return;
    }
    if (m_gesture != Gesture::None && !m_touchGesture) {
        event->accept(); // the mouse's gesture
        return;
    }
    m_touchGesture = !down.isEmpty();
    if (down.size() >= 2) {
        if (m_gesture == Gesture::Draw) {
            // A second finger right after the first one began: a pinch, not a stroke.
            if (m_liveClock.elapsed() < 300 || m_live.points.size() < 8)
                cancelStroke();
            else
                finishStroke();
        } else if (m_gesture == Gesture::Erase) {
            finishErase();
        } else if (m_gesture != Gesture::Pinch && m_gesture != Gesture::Pan && m_gesture != Gesture::None) {
            cancelToolGesture(); // a lasso, a shape, a move: the fingers zoom instead
        }
        const QPointF a = down.at(0)->position();
        const QPointF b = down.at(1)->position();
        const QPointF mid = (a + b) / 2;
        const double distance = std::max(1.0, std::hypot(a.x() - b.x(), a.y() - b.y()));
        if (m_gesture != Gesture::Pinch) {
            setGesture(Gesture::Pinch);
            m_pinchDistance = distance;
            m_pinchZoom = m_zoom;
            m_pinchScene = toScene(mid);
        } else {
            m_zoom = std::clamp(m_pinchZoom * distance / m_pinchDistance, MinZoom, MaxZoom);
            m_center = m_pinchScene - (mid - QPointF(width() / 2, height() / 2)) / m_zoom;
            viewMoved(true);
        }
    } else if (down.size() == 1) {
        const QEventPoint &pt = *down.at(0);
        switch (m_gesture) {
        case Gesture::Pinch:
            beginPan(pt.position()); // a finger lifted: the other one moves on
            break;
        case Gesture::None:
            if (m_textEditing) {
                Q_EMIT textCommitRequested();
                m_ignoreTouch = true;
            } else if (m_fingerDraws && !m_panMode && m_editable) {
                m_touchDrawId = pt.id();
                beginToolGesture(pt.position(), touchPointer());
            } else {
                beginPan(pt.position());
            }
            break;
        case Gesture::Pan: panTo(pt.position()); break;
        default:
            if (pt.id() == m_touchDrawId || m_gesture == Gesture::Erase)
                moveToolGesture(pt.position(), touchPointer());
            break;
        }
    } else {
        // Every finger lifted.
        const QPointF pos = event->points().isEmpty() ? m_lastItemPos : event->points().first().position();
        endToolGesture(pos, touchPointer());
        m_touchDrawId = -1;
    }
    event->accept();
}

void DrawingCanvas::touchUngrabEvent()
{
    if (m_touchGesture)
        mouseUngrabEvent();
    m_touchGesture = false;
    m_ignoreTouch = false;
    m_touchDrawId = -1;
}

// --- Gestures ---------------------------------------------------------------------------------

void DrawingCanvas::beginStroke(QPointF itemPos, const Pointer &p)
{
    if (!m_editable)
        return;
    const bool highlighter = m_tool == u"highlighter";
    m_live = drawing::Stroke();
    m_live.tool = highlighter ? drawing::Tool::Highlighter : drawing::Tool::Pen;
    m_live.color = m_color.name(QColor::HexRgb);
    m_live.width = m_penWidth;
    m_live.opacity = highlighter ? HighlighterOpacity : 100;
    // The highlighter: an even width (a constant pressure Excalidraw draws alike); a pen without
    // a stylus: pressure from the speed.
    m_live.simulatePressure = !highlighter && !p.stylus;
    m_liveStylus = p.stylus;
    m_liveMinPressure = 1;
    m_liveMaxPressure = 0;
    m_liveClock.start();
    setGesture(Gesture::Draw);
    m_lastItemPos = itemPos;
    extendStroke(itemPos, p);
}

void DrawingCanvas::extendStroke(QPointF itemPos, const Pointer &p)
{
    if (m_gesture != Gesture::Draw)
        return;
    const QPointF scene(std::clamp(toScene(itemPos).x(), -drawing::CoordinateLimit, drawing::CoordinateLimit),
                        std::clamp(toScene(itemPos).y(), -drawing::CoordinateLimit, drawing::CoordinateLimit));
    const double pressure = m_live.tool == drawing::Tool::Highlighter || !m_liveStylus ? 0.5 : p.pressure;
    if (!m_live.points.isEmpty()) {
        const drawing::StrokePoint &last = m_live.points.last();
        if (std::hypot(scene.x() - last.x, scene.y() - last.y) * m_zoom < 0.75)
            return; // too close to show
    }
    if (m_liveStylus) {
        m_liveMinPressure = std::min(m_liveMinPressure, pressure);
        m_liveMaxPressure = std::max(m_liveMaxPressure, pressure);
    }
    m_live.points.append({scene.x(), scene.y(), pressure});
    m_lastItemPos = itemPos;
    updateLive();
}

void DrawingCanvas::finishStroke()
{
    if (m_gesture != Gesture::Draw)
        return;
    drawing::Stroke s = std::move(m_live);
    m_live = drawing::Stroke();
    setGesture(Gesture::None);
    updateLive();
    if (s.points.isEmpty())
        return;
    // A stylus whose pressure never changed (some report only 0 or 1) draws as a mouse does.
    if (s.tool == drawing::Tool::Pen && m_liveStylus && m_liveMaxPressure - m_liveMinPressure < 0.02)
        s.simulatePressure = true;
    if (s.simulatePressure) {
        for (drawing::StrokePoint &pt : s.points)
            pt.pressure = 0.5;
    }
    drawing::normalizeStroke(s);
    s.id = drawing::newElementId();
    s.seed = int(QRandomGenerator::global()->bounded(1, 2147483647));
    s.versionNonce = int(QRandomGenerator::global()->bounded(1, 2147483647));
    s.version = 1;
    s.updated = m_clock ? m_clock() : 0;
    // The fast path: one element on top, drawn into the cached image as it is.
    Op op;
    op.kind = Op::Elements;
    op.diff.added.append({m_doc.strokes.size(), s});
    m_prepared.append(prepare(s));
    m_doc.strokes.append(std::move(s));
    drawIntoCache(m_doc.strokes.size() - 1);
    update();
    push(std::move(op));
}

void DrawingCanvas::cancelStroke()
{
    if (m_gesture != Gesture::Draw)
        return;
    m_live = drawing::Stroke();
    setGesture(Gesture::None);
    updateLive();
}

void DrawingCanvas::beginErase(QPointF itemPos)
{
    if (!m_editable)
        return;
    m_erasing.clear();
    setGesture(Gesture::Erase);
    m_lastItemPos = itemPos;
    eraseTo(itemPos);
}

void DrawingCanvas::eraseTo(QPointF itemPos)
{
    if (m_gesture != Gesture::Erase)
        return;
    const QPointF a = toScene(m_lastItemPos);
    const QPointF b = toScene(itemPos);
    const double r = EraserRadius / m_zoom;
    const QRectF sweep = QRectF(a, b).normalized().adjusted(-r, -r, r, r);
    bool erased = false;
    for (qsizetype i = 0; i < m_prepared.size(); ++i) {
        const Prepared &prepared = m_prepared.at(i);
        if (m_erasing.contains(prepared.key) || !prepared.bounds.intersects(sweep))
            continue;
        const drawing::Stroke &e = m_doc.strokes.at(i);
        if (!drawing::touchesSegment(e, a, b, r) || drawing::isLocked(m_doc, e))
            continue;
        m_erasing.insert(prepared.key);
        // A shape goes with its label.
        for (qsizetype k = 0; k < m_doc.strokes.size(); ++k) {
            const drawing::Stroke &label = m_doc.strokes.at(k);
            if (!e.id.isEmpty() && label.type == drawing::ElementType::Text && label.containerId == e.id)
                m_erasing.insert(m_prepared.at(k).key);
        }
        erased = true;
    }
    m_lastItemPos = itemPos;
    if (erased)
        invalidateCache();
    else
        update();
}

void DrawingCanvas::finishErase()
{
    if (m_gesture != Gesture::Erase)
        return;
    setGesture(Gesture::None);
    if (m_erasing.isEmpty()) {
        update();
        return;
    }
    const QSet<QString> keys = std::exchange(m_erasing, {});
    editElements([&keys, this](drawing::Drawing &doc) {
        // By id (references to them cleaned); elements without one by their keys.
        QSet<QString> ids;
        QList<drawing::Stroke> kept;
        for (const drawing::Stroke &e : std::as_const(doc.strokes)) {
            const bool gone = keys.contains(e.id.isEmpty() ? drawing::mergeKey(e) : e.id);
            if (gone && !e.id.isEmpty())
                ids.insert(e.id);
            if (!gone || !e.id.isEmpty())
                kept.append(e);
        }
        doc.strokes = std::move(kept);
        drawing::deleteElements(doc, ids, stamp());
    });
    invalidateCache();
}

void DrawingCanvas::beginPan(QPointF itemPos)
{
    setGesture(Gesture::Pan);
    m_panStart = itemPos;
    m_panCenter = m_center;
    m_lastItemPos = itemPos;
}

void DrawingCanvas::panTo(QPointF itemPos)
{
    if (m_gesture != Gesture::Pan)
        return;
    m_center = m_panCenter - (itemPos - m_panStart) / m_zoom;
    m_lastItemPos = itemPos;
    viewMoved(true);
}

} // namespace brulion::drawingview
