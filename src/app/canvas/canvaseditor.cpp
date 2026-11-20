#include "canvaseditor.h"

#include "canvasmodels.h"
#include "editorhost.h"
#include "picturesize.h"

#include "canvas/edit.h"
#include "canvas/geometry.h"
#include "canvas/indextext.h"
#include "canvas/merge.h"
#include "canvas/svg.h"
#include "util/paths.h"

#include <QBuffer>
#include <QClipboard>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QUrl>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::canvasview {

namespace {

/// A web address alone on its line (pasted as a link card).
bool isWebAddress(const QString &text)
{
    static const QRegularExpression re(u"^(https?|ftp|mailto|obsidian|zotero):\\S+$"_s,
                                       QRegularExpression::CaseInsensitiveOption);
    return re.match(text).hasMatch();
}

/// A new text card's size for `text`: wider and taller with more text, within reason.
QSizeF textCardSize(const QString &text)
{
    if (text.isEmpty())
        return canvas::defaultSize(canvas::TextType);
    const QStringList lines = text.split(u'\n');
    qsizetype longest = 0;
    for (const QString &l : lines)
        longest = std::max(longest, l.size());
    const double width = std::clamp(double(longest) * 8.5 + 40, 250.0, 480.0);
    const double perLine = std::max(1.0, width / 8.5 - 4);
    double rows = 0;
    for (const QString &l : lines)
        rows += std::max(1.0, std::ceil(double(l.size()) / perLine));
    return {width, std::clamp(rows * 24 + 36, 60.0, 600.0)};
}

} // namespace

CanvasEditor::CanvasEditor(QQuickItem *parent)
    : QQuickItem(parent), m_nodes(new CanvasNodeModel(this)), m_edges(new CanvasEdgeModel(this))
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
    setAcceptTouchEvents(true);
    m_longPress.setSingleShot(true);
    m_longPress.setInterval(500);
    connect(&m_longPress, &QTimer::timeout, this, [this] {
        if (m_gesture != Gesture::Press || !m_pressTouch)
            return;
        m_longPressed = true;
        const Hit &h = m_pressHit;
        if (!h.node.isEmpty() && !m_selNodes.contains(h.node))
            setSelection({h.node}, {});
        else if (!h.edge.isEmpty() && !m_selEdges.contains(h.edge))
            setSelection({}, {h.edge});
        Q_EMIT contextMenuRequested(h.node, h.edge, m_pressPos.x(), m_pressPos.y());
    });
    m_viewAnimation = new QVariantAnimation(this);
    m_viewAnimation->setDuration(220);
    m_viewAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_viewAnimation->setStartValue(0.0);
    m_viewAnimation->setEndValue(1.0);
    connect(m_viewAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        const double t = v.toDouble();
        // Zoom geometrically, the pan so the view's middle travels straight.
        const double z = m_animFromZoom * std::pow(m_animToZoom / m_animFromZoom, t);
        const QPointF mid(width() / 2, height() / 2);
        const QPointF fromCenter = (mid - m_animFromPan) / m_animFromZoom;
        const QPointF toCenter = (mid - m_animToPan) / m_animToZoom;
        const QPointF center = fromCenter + (toCenter - fromCenter) * t;
        setView(z, mid - center * z);
    });
}

CanvasEditor::~CanvasEditor() = default;

void CanvasEditor::setHost(EditorHost *host)
{
    if (m_host == host)
        return;
    if (m_host)
        disconnect(m_host, nullptr, this, nullptr);
    m_host = host;
    if (m_host) {
        // A file a card shows came or went: its kind is asked again.
        const auto again = [this] {
            m_nodes->forgetKinds();
            refreshModels();
        };
        connect(m_host, &EditorHost::indexChanged, this, again);
    }
    m_nodes->forgetKinds();
    refreshModels();
    Q_EMIT hostChanged();
}

void CanvasEditor::setPath(const QString &path)
{
    if (m_path == path)
        return;
    m_path = path;
    Q_EMIT pathChanged();
}

QRectF CanvasEditor::viewRect() const
{
    return QRectF(toCanvas(0, 0), toCanvas(width(), height())).normalized();
}

void CanvasEditor::setAnimate(bool on)
{
    if (m_animate == on)
        return;
    m_animate = on;
    Q_EMIT animateChanged();
}

QStringList CanvasEditor::selectedNodes() const
{
    // In z-order (stable for QML and tests).
    QStringList out;
    for (const canvas::Node &n : m_canvas.nodes) {
        if (m_selNodes.contains(n.id))
            out.append(n.id);
    }
    return out;
}

QStringList CanvasEditor::selectedEdges() const
{
    QStringList out;
    for (const canvas::Edge &e : m_canvas.edges) {
        if (m_selEdges.contains(e.id))
            out.append(e.id);
    }
    return out;
}

QRectF CanvasEditor::selectionScreenRect() const
{
    QRectF b = canvas::bounds(m_canvas, m_selNodes);
    for (const canvas::Edge &e : m_canvas.edges) {
        if (!m_selEdges.contains(e.id))
            continue;
        const canvas::EdgeShape s = canvas::edgeShape(m_canvas, e);
        if (!s.valid)
            continue;
        const QRectF r = QRectF(s.labelPoint - QPointF(1, 1), QSizeF(2, 2));
        b = b.isNull() ? r : b.united(r);
    }
    if (b.isNull())
        return {};
    return QRectF(toScreen(b.left(), b.top()), toScreen(b.right(), b.bottom()));
}

QString CanvasEditor::selectionKind() const
{
    QSet<QString> kinds;
    for (const canvas::Node &n : m_canvas.nodes) {
        if (m_selNodes.contains(n.id))
            kinds.insert(n.isText() || n.isFile() || n.isLink() || n.isGroup() ? n.type : u"text"_s);
    }
    if (!m_selEdges.isEmpty())
        kinds.insert(u"edge"_s);
    if (kinds.isEmpty())
        return {};
    return kinds.size() == 1 ? *kinds.cbegin() : u"mixed"_s;
}

QString CanvasEditor::selectionColor() const
{
    QSet<QString> colors;
    for (const canvas::Node &n : m_canvas.nodes) {
        if (m_selNodes.contains(n.id))
            colors.insert(canvas::normalizeColor(n.color));
    }
    for (const canvas::Edge &e : m_canvas.edges) {
        if (m_selEdges.contains(e.id))
            colors.insert(canvas::normalizeColor(e.color));
    }
    return colors.size() == 1 ? *colors.cbegin() : QString();
}

bool CanvasEditor::selectionArrowStart() const
{
    bool any = false;
    for (const canvas::Edge &e : m_canvas.edges) {
        if (!m_selEdges.contains(e.id))
            continue;
        if (!e.arrowAtStart())
            return false;
        any = true;
    }
    return any;
}

bool CanvasEditor::selectionArrowEnd() const
{
    bool any = false;
    for (const canvas::Edge &e : m_canvas.edges) {
        if (!m_selEdges.contains(e.id))
            continue;
        if (!e.arrowAtEnd())
            return false;
        any = true;
    }
    return any;
}

QRectF CanvasEditor::hoverRect() const
{
    const canvas::Node *n = m_canvas.node(m_hoverNode);
    return n && m_hoverNode != m_editingNode ? canvas::nodeRect(*n) : QRectF();
}

QRectF CanvasEditor::singleRect() const
{
    if (m_selNodes.size() != 1 || !m_selEdges.isEmpty())
        return {};
    const canvas::Node *n = m_canvas.node(*m_selNodes.cbegin());
    return n && n->id != m_editingNode ? canvas::nodeRect(*n) : QRectF();
}

QRectF CanvasEditor::rubberBand() const
{
    if (m_gesture != Gesture::Band)
        return {};
    return QRectF(m_pressPos, m_lastPos).normalized();
}

void CanvasEditor::setPanMode(bool on)
{
    if (m_panMode == on)
        return;
    m_panMode = on;
    setCursor(on ? Qt::OpenHandCursor : Qt::ArrowCursor);
    Q_EMIT panModeChanged();
}

void CanvasEditor::setSelectMode(bool on)
{
    if (m_selectMode == on)
        return;
    m_selectMode = on;
    Q_EMIT selectModeChanged();
}

void CanvasEditor::setEditingNode(const QString &id)
{
    if (m_editingNode == id)
        return;
    m_editingNode = id;
    Q_EMIT editingNodeChanged();
    Q_EMIT hoverChanged();
    Q_EMIT selectionGeometryChanged();
}

void CanvasEditor::setHandleRadius(qreal r)
{
    if (qFuzzyCompare(m_handleRadius, r) || r <= 0)
        return;
    m_handleRadius = r;
    Q_EMIT handleRadiusChanged();
}

// ---- the file ------------------------------------------------------------------------------------

QString CanvasEditor::load(const QByteArray &bytes)
{
    const canvas::ReadResult r = canvas::readCanvas(QString::fromUtf8(bytes));
    m_undo.clear();
    m_redo.clear();
    m_selNodes.clear();
    m_selEdges.clear();
    m_scrollable.clear();
    m_savedBytes = bytes;
    m_pendingExternal.clear();
    QString status;
    if (r.canvas) {
        m_saved = *r.canvas;
    } else {
        m_saved = canvas::Canvas();
        status = r.error == canvas::ReadError::TooLarge ? u"tooLarge"_s : u"corrupt"_s;
    }
    setDocument(m_saved);
    setModified(false);
    Q_EMIT historyChanged();
    Q_EMIT selectionChanged();
    m_fitPending = true;
    if (width() > 0 && height() > 0) {
        m_fitPending = false;
        const bool animate = m_animate;
        m_animate = false;
        zoomToFit();
        m_animate = animate;
    }
    return status;
}

QByteArray CanvasEditor::exportPicture(const QString &format, bool dark) const
{
    if (m_canvas.nodes.isEmpty())
        return {};
    canvas::SvgOptions options;
    options.dark = dark;
    QPointer<EditorHost> host = m_host;
    options.pictureDataUrl = [host](const QString &path) -> QString {
        if (!host || !paths::isPicturePath(path))
            return {};
        const QUrl url = host->assetUrl(paths::normalize(path));
        if (!url.isLocalFile())
            return {};
        QFile file(url.toLocalFile());
        static constexpr qint64 MaxPicture = 16 * 1024 * 1024;
        if (file.size() > MaxPicture || !file.open(QIODevice::ReadOnly))
            return {};
        const QString ext = paths::extension(path);
        const QString mime = ext == u".jpg" || ext == u".jpeg" ? u"image/jpeg"_s
                             : ext == u".svg"                  ? u"image/svg+xml"_s
                             : ext == u".gif"                  ? u"image/gif"_s
                             : ext == u".webp"                 ? u"image/webp"_s
                                                               : u"image/png"_s;
        return u"data:"_s + mime + u";base64,"_s + QString::fromLatin1(file.readAll().toBase64());
    };
    options.noteText = [host](const QString &path, const QString &) -> QString {
        if (!host || !paths::isMarkdown(path))
            return {};
        bool ok = false;
        QString text = host->read(paths::normalize(path), &ok);
        return ok ? text.left(4000) : QString();
    };
    const QString svg = canvas::toSvg(m_canvas, options);
    if (format == u"svg")
        return svg.toUtf8();
    QSvgRenderer renderer(svg.toUtf8());
    if (!renderer.isValid())
        return {};
    const QSizeF own = canvas::svgSize(m_canvas, options);
    const double scale = std::min(2.0, 8192.0 / std::max(own.width(), own.height()));
    const QSize size(std::max(1, int(own.width() * scale)), std::max(1, int(own.height() * scale)));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        renderer.render(&p, QRectF(QPointF(0, 0), QSizeF(size)));
    }
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        return {};
    return png;
}

QByteArray CanvasEditor::fileBytes() const
{
    return canvas::writeCanvas(m_canvas).toUtf8();
}

bool CanvasEditor::isKnownFile(const QByteArray &bytes) const
{
    return bytes == m_savedBytes;
}

void CanvasEditor::markSaved(const QByteArray &bytes)
{
    m_savedBytes = bytes;
    // The written file is what later writes keep byte for byte (its layout, each node's text).
    const canvas::ReadResult r = canvas::readCanvas(QString::fromUtf8(bytes));
    if (r.canvas && r.canvas->sameContent(m_canvas))
        m_canvas = *r.canvas;
    m_saved = m_canvas;
    setModified(false);
}

bool CanvasEditor::takeExternal(const QByteArray &bytes)
{
    if (m_gesture != Gesture::None && m_gesture != Gesture::Pan && m_gesture != Gesture::Pinch) {
        m_pendingExternal = bytes; // after the gesture
        return true;
    }
    const canvas::ReadResult r = canvas::readCanvas(QString::fromUtf8(bytes));
    if (!r.canvas)
        return false;
    canvas::Canvas next = m_modified ? canvas::merge(&m_saved, m_canvas, *r.canvas) : *r.canvas;
    m_saved = *r.canvas;
    m_savedBytes = bytes;
    m_undo.clear();
    m_redo.clear();
    setDocument(std::move(next));
    Q_EMIT historyChanged();
    return true;
}

void CanvasEditor::takePendingExternal()
{
    if (m_pendingExternal.isEmpty())
        return;
    const QByteArray bytes = std::exchange(m_pendingExternal, QByteArray());
    takeExternal(bytes);
}

// ---- changes -------------------------------------------------------------------------------------

void CanvasEditor::apply(canvas::Canvas next, bool undoable)
{
    if (next.sameContent(m_canvas))
        return;
    if (undoable)
        pushUndo(m_canvas);
    // (Modified first: the view saves on contentChanged() when the canvas is modified.)
    setModified(true);
    setDocument(std::move(next));
}

void CanvasEditor::pushUndo(const canvas::Canvas &before)
{
    m_undo.append(before);
    while (m_undo.size() > MaxUndo)
        m_undo.removeFirst();
    m_redo.clear();
    Q_EMIT historyChanged();
}

void CanvasEditor::setDocument(canvas::Canvas doc)
{
    m_canvas = std::move(doc);
    documentChanged();
}

void CanvasEditor::documentChanged()
{
    ++m_revision;
    const qsizetype selected = m_selNodes.size() + m_selEdges.size();
    pruneSelection();
    refreshModels();
    Q_EMIT contentChanged();
    if (m_selNodes.size() + m_selEdges.size() != selected)
        Q_EMIT selectionChanged();
    Q_EMIT selectionGeometryChanged();
    if (!m_hoverNode.isEmpty())
        Q_EMIT hoverChanged(); // (its rectangle may have moved)
}

void CanvasEditor::setModified(bool on)
{
    if (m_modified == on)
        return;
    m_modified = on;
    Q_EMIT modifiedChanged();
}

void CanvasEditor::pruneSelection()
{
    m_selNodes.removeIf([this](const QString &id) { return !m_canvas.node(id); });
    m_selEdges.removeIf([this](const QString &id) { return !m_canvas.edge(id); });
    m_scrollable.removeIf([this](const QString &id) { return !m_canvas.node(id); });
    if (!m_hoverNode.isEmpty() && !m_canvas.node(m_hoverNode)) {
        m_hoverNode.clear();
        Q_EMIT hoverChanged();
    }
}

void CanvasEditor::refreshModels()
{
    if (width() <= 0 || height() <= 0)
        return;
    // In view and half a screen around it: cards come before they are reached while panning.
    const QRectF v = viewRect();
    const QRectF area = v.adjusted(-v.width() / 2, -v.height() / 2, v.width() / 2, v.height() / 2);
    QPointer<EditorHost> host = m_host;
    const QString from = m_path;
    const FileKindOf kindOf = [host, from](const QString &file) -> FileKind {
        if (!host)
            return {u"file"_s, file};
        const QString path = paths::normalize(file);
        // The exact path, else as a link resolves it (a path written in another letter case).
        QString resolved = host->resolveLink(path, from);
        if (resolved.isEmpty())
            return {u"missing"_s, path};
        if (canvas::isCanvasPath(resolved))
            return {u"canvas"_s, resolved};
        if (host->isDrawing(resolved))
            return {u"drawing"_s, resolved};
        if (paths::isMarkdown(resolved))
            return {u"note"_s, resolved};
        if (paths::isPdfPath(resolved))
            return {u"pdf"_s, resolved};
        if (paths::isPicturePath(resolved))
            return {u"picture"_s, resolved};
        return {u"file"_s, resolved};
    };
    QSet<QString> selected = m_selNodes;
    m_nodes->sync(m_canvas, area, selected, kindOf);
    m_edges->sync(m_canvas, area, m_selEdges);
}

void CanvasEditor::setSelection(QSet<QString> nodes, QSet<QString> edges)
{
    if (nodes == m_selNodes && edges == m_selEdges)
        return;
    m_selNodes = std::move(nodes);
    m_selEdges = std::move(edges);
    pruneSelection();
    refreshModels();
    Q_EMIT selectionChanged();
    Q_EMIT selectionGeometryChanged();
}

// ---- the view ------------------------------------------------------------------------------------

double CanvasEditor::clampZoom(double z) const
{
    return std::isfinite(z) ? std::clamp(z, MinZoom, MaxZoom) : 1.0;
}

void CanvasEditor::setView(double zoom, QPointF pan)
{
    zoom = clampZoom(zoom);
    if (!std::isfinite(pan.x()) || !std::isfinite(pan.y()))
        return;
    if (zoom == m_zoom && pan == m_pan)
        return;
    const bool wasDetailed = detailed();
    m_zoom = zoom;
    m_pan = pan;
    refreshModels();
    Q_EMIT viewChanged();
    Q_EMIT selectionGeometryChanged();
    if (detailed() != wasDetailed)
        Q_EMIT detailedChanged();
}

void CanvasEditor::animateTo(double zoom, QPointF pan)
{
    m_viewAnimation->stop();
    zoom = clampZoom(zoom);
    if (!m_animate || !isVisible() || width() <= 0) {
        setView(zoom, pan);
        return;
    }
    m_animFromZoom = m_zoom;
    m_animFromPan = m_pan;
    m_animToZoom = zoom;
    m_animToPan = pan;
    m_viewAnimation->start();
}

void CanvasEditor::showRect(const QRectF &rect, double maxZoom)
{
    if (width() <= 0 || height() <= 0)
        return;
    const double margin = std::min(80.0, std::min(width(), height()) / 6);
    const double w = std::max(1.0, width() - 2 * margin);
    const double h = std::max(1.0, height() - 2 * margin);
    const double z = clampZoom(std::min({w / std::max(1.0, rect.width()), h / std::max(1.0, rect.height()), maxZoom}));
    const QPointF mid(width() / 2, height() / 2);
    animateTo(z, mid - rect.center() * z);
}

void CanvasEditor::zoomToFit()
{
    QRectF b = canvas::bounds(m_canvas);
    if (b.isNull()) {
        animateTo(1, QPointF(width() / 2, height() / 2));
        return;
    }
    b.adjust(0, -canvas::GroupLabelHeight, 0, 0);
    showRect(b, 1);
}

void CanvasEditor::zoomToSelection()
{
    QRectF b = canvas::bounds(m_canvas, m_selNodes);
    for (const canvas::Edge &e : m_canvas.edges) {
        if (!m_selEdges.contains(e.id))
            continue;
        const canvas::EdgeShape s = canvas::edgeShape(m_canvas, e);
        if (s.valid) {
            const QRectF r = QRectF(s.start, s.end).normalized();
            b = b.isNull() ? r : b.united(r);
        }
    }
    if (b.isNull()) {
        zoomToFit();
        return;
    }
    showRect(b, 1.5);
}

void CanvasEditor::focusNode(const QString &id)
{
    if (const canvas::Node *n = m_canvas.node(id))
        showRect(canvas::nodeRect(*n), 1);
}

bool CanvasEditor::focusLine(int line)
{
    const canvas::IndexText text = canvas::indexText(m_canvas);
    QString edge;
    const QString node = canvas::nodeAtLine(text, line, &edge);
    if (!node.isEmpty()) {
        setSelection({node}, {});
        focusNode(node);
        return true;
    }
    if (!edge.isEmpty()) {
        setSelection({}, {edge});
        zoomToSelection();
        return true;
    }
    return false;
}

void CanvasEditor::zoomBy(qreal factor, qreal x, qreal y)
{
    if (!std::isfinite(factor) || factor <= 0)
        return;
    m_viewAnimation->stop();
    const QPointF at = x < 0 || y < 0 ? QPointF(width() / 2, height() / 2) : QPointF(x, y);
    const double z = clampZoom(m_zoom * factor);
    // The point under `at` stays there.
    const QPointF c = (at - m_pan) / m_zoom;
    setView(z, at - c * z);
}

void CanvasEditor::setZoom(qreal zoom)
{
    zoomBy(zoom / m_zoom);
}

void CanvasEditor::panBy(qreal dx, qreal dy)
{
    m_viewAnimation->stop();
    setView(m_zoom, m_pan - QPointF(dx, dy));
}

QPointF CanvasEditor::toScreen(qreal x, qreal y) const
{
    return QPointF(x, y) * m_zoom + m_pan;
}

QPointF CanvasEditor::toCanvas(qreal x, qreal y) const
{
    return (QPointF(x, y) - m_pan) / m_zoom;
}

QRectF CanvasEditor::nodeScreenRect(const QString &id) const
{
    const canvas::Node *n = m_canvas.node(id);
    if (!n)
        return {};
    const QRectF r = canvas::nodeRect(*n);
    return QRectF(toScreen(r.left(), r.top()), toScreen(r.right(), r.bottom()));
}

QPointF CanvasEditor::viewCenter() const
{
    return toCanvas(width() / 2, height() / 2);
}

QPointF CanvasEditor::canvasAtScene(qreal x, qreal y) const
{
    const QPointF local = mapFromScene(QPointF(x, y));
    return toCanvas(local.x(), local.y());
}

bool CanvasEditor::containsScene(qreal x, qreal y) const
{
    const QPointF local = mapFromScene(QPointF(x, y));
    return isVisible() && QRectF(0, 0, width(), height()).contains(local);
}

void CanvasEditor::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() == oldGeometry.size())
        return;
    if (m_fitPending && newGeometry.width() > 0 && newGeometry.height() > 0) {
        m_fitPending = false;
        const bool animate = m_animate;
        m_animate = false;
        zoomToFit();
        m_animate = animate;
        return;
    }
    // The middle stays where it was.
    if (oldGeometry.width() > 0 && oldGeometry.height() > 0) {
        const QPointF shift = (QPointF(newGeometry.width(), newGeometry.height())
                               - QPointF(oldGeometry.width(), oldGeometry.height()))
                              / 2;
        m_pan += shift;
    }
    refreshModels();
    Q_EMIT viewChanged();
    Q_EMIT selectionGeometryChanged();
}

// ---- the selection -----------------------------------------------------------------------------

void CanvasEditor::select(const QStringList &nodeIds, const QStringList &edgeIds)
{
    setSelection(QSet<QString>(nodeIds.cbegin(), nodeIds.cend()), QSet<QString>(edgeIds.cbegin(), edgeIds.cend()));
}

void CanvasEditor::selectAll()
{
    QSet<QString> nodes;
    for (const canvas::Node &n : m_canvas.nodes)
        nodes.insert(n.id);
    QSet<QString> edges;
    for (const canvas::Edge &e : m_canvas.edges)
        edges.insert(e.id);
    setSelection(std::move(nodes), std::move(edges));
}

void CanvasEditor::clearSelection()
{
    setSelection({}, {});
}

// ---- what a card holds ---------------------------------------------------------------------------

QVariantMap CanvasEditor::nodeData(const QString &id) const
{
    const canvas::Node *n = m_canvas.node(id);
    if (!n)
        return {};
    const QRectF r = canvas::nodeRect(*n);
    return {{u"id"_s, n->id},
            {u"type"_s, n->type},
            {u"x"_s, r.x()},
            {u"y"_s, r.y()},
            {u"width"_s, r.width()},
            {u"height"_s, r.height()},
            {u"color"_s, n->color},
            {u"text"_s, n->text},
            {u"file"_s, n->file},
            {u"subpath"_s, n->subpath},
            {u"url"_s, n->url},
            {u"label"_s, n->label},
            {u"background"_s, n->background},
            {u"backgroundStyle"_s, n->backgroundStyle}};
}

QVariantMap CanvasEditor::edgeData(const QString &id) const
{
    const canvas::Edge *e = m_canvas.edge(id);
    if (!e)
        return {};
    return {{u"id"_s, e->id},
            {u"fromNode"_s, e->fromNode},
            {u"toNode"_s, e->toNode},
            {u"label"_s, e->label},
            {u"color"_s, e->color},
            {u"arrowStart"_s, e->arrowAtStart()},
            {u"arrowEnd"_s, e->arrowAtEnd()}};
}

QString CanvasEditor::nodeAtScreen(qreal x, qreal y) const
{
    const qsizetype i = canvas::nodeAt(m_canvas, toCanvas(x, y), true, 14 / m_zoom);
    return i < 0 ? QString() : m_canvas.nodes.at(i).id;
}

QString CanvasEditor::edgeAtScreen(qreal x, qreal y) const
{
    const qsizetype i = canvas::edgeAt(m_canvas, toCanvas(x, y), m_handleRadius / m_zoom);
    return i < 0 ? QString() : m_canvas.edges.at(i).id;
}

void CanvasEditor::setScrollable(const QString &id, bool scrollable)
{
    if (scrollable)
        m_scrollable.insert(id);
    else
        m_scrollable.remove(id);
}

// ---- edits -----------------------------------------------------------------------------------------

QPointF CanvasEditor::clampedCanvas(QPointF p) const
{
    const double limit = canvas::CoordinateLimit / 2;
    return {std::clamp(std::isfinite(p.x()) ? p.x() : 0.0, -limit, limit),
            std::clamp(std::isfinite(p.y()) ? p.y() : 0.0, -limit, limit)};
}

QString CanvasEditor::addText(qreal x, qreal y, const QString &text)
{
    const QSizeF size = textCardSize(text);
    QRectF r(QPointF(), size);
    r.moveCenter(clampedCanvas({x, y}));
    canvas::Node n = canvas::makeTextNode(text, r);
    const QString id = n.id;
    apply(canvas::addNode(m_canvas, std::move(n)));
    setSelection({id}, {});
    return id;
}

QString CanvasEditor::addFile(const QString &path, const QString &subpath, qreal x, qreal y)
{
    if (path.trimmed().isEmpty())
        return {};
    const QString file = paths::normalize(path);
    QSizeF size = canvas::defaultSize(canvas::FileType, file);
    // A picture keeps its shape (400 wide, within 80–800 high).
    if (paths::isPicturePath(file) && m_host) {
        QSize pixels = m_host->assetPixelSize(file);
        if (!pixels.isValid()) {
            const QUrl url = m_host->assetUrl(file);
            if (url.isLocalFile())
                pixels = editor::localPictureSize(url.toLocalFile());
        }
        if (pixels.isValid() && pixels.width() > 0)
            size = QSizeF(400, std::clamp(400.0 * pixels.height() / pixels.width(), 80.0, 800.0));
    }
    QRectF r(QPointF(), size);
    r.moveCenter(clampedCanvas({x, y}));
    canvas::Node n = canvas::makeFileNode(file, subpath, r);
    const QString id = n.id;
    apply(canvas::addNode(m_canvas, std::move(n)));
    setSelection({id}, {});
    return id;
}

QStringList CanvasEditor::addFiles(const QStringList &paths, qreal x, qreal y)
{
    QStringList ids;
    if (paths.isEmpty())
        return ids;
    const canvas::Canvas before = m_canvas;
    const QList<canvas::Canvas> undo = m_undo;
    double at = x;
    for (const QString &p : paths) {
        const QSizeF size = canvas::defaultSize(canvas::FileType, p);
        const QString id = addFile(p, QString(), at + size.width() / 2, y + size.height() / 2);
        if (!id.isEmpty()) {
            ids.append(id);
            at += size.width() + 40;
        }
    }
    // One undo step for all of them.
    if (!ids.isEmpty()) {
        m_undo = undo;
        pushUndo(before);
    }
    setSelection(QSet<QString>(ids.cbegin(), ids.cend()), {});
    return ids;
}

QStringList CanvasEditor::addDroppedFiles(const QList<QUrl> &urls, qreal x, qreal y)
{
    QStringList stored;
    if (!m_host)
        return stored;
    for (const QUrl &url : urls) {
        if (!url.isLocalFile() && url.scheme() != u"content")
            continue;
        const QString path = m_host->importAttachment(url, MaxAttachmentBytes);
        if (!path.isEmpty())
            stored.append(path);
    }
    return addFiles(stored, x, y);
}

QString CanvasEditor::addLink(const QString &url, qreal x, qreal y)
{
    const QString u = url.trimmed();
    if (u.isEmpty())
        return {};
    QRectF r(QPointF(), canvas::defaultSize(canvas::LinkType));
    r.moveCenter(clampedCanvas({x, y}));
    canvas::Node n = canvas::makeLinkNode(u, r);
    const QString id = n.id;
    apply(canvas::addNode(m_canvas, std::move(n)));
    setSelection({id}, {});
    return id;
}

QString CanvasEditor::addGroup(const QString &label)
{
    QString id;
    if (!m_selNodes.isEmpty()) {
        apply(canvas::group(m_canvas, m_selNodes, label, &id));
    } else {
        QRectF r(QPointF(), canvas::defaultSize(canvas::GroupType));
        r.moveCenter(viewCenter());
        canvas::Node g = canvas::makeNode(canvas::GroupType, r);
        g.label = label;
        id = g.id;
        // Below every card.
        canvas::Canvas next = m_canvas;
        next.nodes.prepend(std::move(g));
        apply(std::move(next));
    }
    if (!id.isEmpty())
        setSelection({id}, {});
    return id;
}

void CanvasEditor::deleteSelection()
{
    if (selectionCount() == 0)
        return;
    apply(canvas::removeItems(m_canvas, m_selNodes, m_selEdges));
    clearSelection();
}

void CanvasEditor::duplicateSelection()
{
    if (m_selNodes.isEmpty())
        return;
    QStringList copies;
    apply(canvas::duplicate(m_canvas, canvas::withGroupContents(m_canvas, m_selNodes), {30, 30}, &copies));
    setSelection(QSet<QString>(copies.cbegin(), copies.cend()), {});
}

void CanvasEditor::ungroupSelection()
{
    apply(canvas::ungroup(m_canvas, m_selNodes));
}

void CanvasEditor::colorSelection(const QString &color)
{
    apply(canvas::setColor(m_canvas, m_selNodes, m_selEdges, color));
}

void CanvasEditor::alignSelection(const QString &how)
{
    canvas::Align a = canvas::Align::Left;
    if (how == u"center")
        a = canvas::Align::HCenter;
    else if (how == u"right")
        a = canvas::Align::Right;
    else if (how == u"top")
        a = canvas::Align::Top;
    else if (how == u"middle")
        a = canvas::Align::VCenter;
    else if (how == u"bottom")
        a = canvas::Align::Bottom;
    else if (how != u"left")
        return;
    apply(canvas::align(m_canvas, m_selNodes, a));
}

void CanvasEditor::distributeSelection(const QString &axis)
{
    apply(canvas::distribute(m_canvas, m_selNodes, axis != u"vertical"));
}

void CanvasEditor::reorderSelection(const QString &order)
{
    canvas::Order o = canvas::Order::Front;
    if (order == u"back")
        o = canvas::Order::Back;
    else if (order == u"forward")
        o = canvas::Order::Forward;
    else if (order == u"backward")
        o = canvas::Order::Backward;
    else if (order != u"front")
        return;
    apply(canvas::reorder(m_canvas, m_selNodes, o));
}

void CanvasEditor::nudgeSelection(qreal dx, qreal dy)
{
    if (m_selNodes.isEmpty() || !std::isfinite(dx) || !std::isfinite(dy))
        return;
    apply(canvas::moveNodes(m_canvas, canvas::withGroupContents(m_canvas, m_selNodes), {dx, dy}));
}

void CanvasEditor::setNodeText(const QString &id, const QString &text)
{
    apply(canvas::setNodeText(m_canvas, id, text));
}

void CanvasEditor::setNodeTextFitted(const QString &id, const QString &text, qreal minHeight)
{
    const canvas::Node *n = m_canvas.node(id);
    if (!n)
        return;
    canvas::Canvas next = canvas::setNodeText(m_canvas, id, text);
    const QRectF r = canvas::nodeRect(*n);
    if (std::isfinite(minHeight) && minHeight > r.height() + 0.5)
        next = canvas::setNodeRect(next, id, QRectF(r.topLeft(), QSizeF(r.width(), std::ceil(minHeight))));
    apply(std::move(next));
}

void CanvasEditor::setNodeFile(const QString &id, const QString &file, const QString &subpath)
{
    apply(canvas::setNodeFile(m_canvas, id, paths::normalize(file), subpath));
}

void CanvasEditor::setNodeUrl(const QString &id, const QString &url)
{
    apply(canvas::setNodeUrl(m_canvas, id, url.trimmed()));
}

void CanvasEditor::setGroupLabel(const QString &id, const QString &label)
{
    apply(canvas::setGroupLabel(m_canvas, id, label));
}

void CanvasEditor::setGroupBackground(const QString &id, const QString &path, const QString &style)
{
    apply(canvas::setGroupBackground(m_canvas, id, path.isEmpty() ? QString() : paths::normalize(path), style));
}

void CanvasEditor::setEdgeLabel(const QString &id, const QString &label)
{
    apply(canvas::setEdgeLabel(m_canvas, id, label));
}

void CanvasEditor::setArrows(bool atStart, bool atEnd)
{
    apply(canvas::setEdgeEnds(m_canvas, m_selEdges, atStart, atEnd));
    Q_EMIT selectionChanged(); // (the arrowheads shown in the menu)
}

void CanvasEditor::reverseSelectedEdges()
{
    apply(canvas::reverseEdges(m_canvas, m_selEdges));
}

void CanvasEditor::resizeNode(const QString &id, qreal w, qreal h)
{
    const canvas::Node *n = m_canvas.node(id);
    if (!n || !std::isfinite(w) || !std::isfinite(h))
        return;
    const QRectF r = canvas::nodeRect(*n);
    apply(canvas::setNodeRect(m_canvas, id, QRectF(r.topLeft(), QSizeF(w, h))));
}

QString CanvasEditor::connectCards(const QString &from, const QString &to, const QString &fromSide,
                                   const QString &toSide)
{
    QString id;
    apply(canvas::connectNodes(m_canvas, from, fromSide, to, toSide, &id));
    return id;
}

void CanvasEditor::undo()
{
    if (m_undo.isEmpty() || m_gesture != Gesture::None)
        return;
    m_redo.append(m_canvas);
    setModified(true);
    setDocument(m_undo.takeLast());
    Q_EMIT historyChanged();
}

void CanvasEditor::redo()
{
    if (m_redo.isEmpty() || m_gesture != Gesture::None)
        return;
    m_undo.append(m_canvas);
    setModified(true);
    setDocument(m_redo.takeLast());
    Q_EMIT historyChanged();
}

// ---- the clipboard ---------------------------------------------------------------------------------

bool CanvasEditor::copySelection()
{
    if (m_selNodes.isEmpty() && m_selEdges.isEmpty())
        return false;
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return false;
    clipboard->setText(canvas::fragmentText(m_canvas, canvas::withGroupContents(m_canvas, m_selNodes), m_selEdges));
    return true;
}

bool CanvasEditor::cutSelection()
{
    if (!copySelection())
        return false;
    apply(canvas::removeItems(m_canvas, canvas::withGroupContents(m_canvas, m_selNodes), m_selEdges));
    clearSelection();
    return true;
}

QVariantMap CanvasEditor::paste(qreal x, qreal y)
{
    QVariantMap out{{u"pasted"_s, 0}, {u"kind"_s, QString()}};
    const QClipboard *clipboard = QGuiApplication::clipboard();
    const QMimeData *data = clipboard ? clipboard->mimeData() : nullptr;
    if (!data)
        return out;
    QPointF at =
            x < 0 || y < 0 ? (m_pointer ? toCanvas(m_pointer->x(), m_pointer->y()) : viewCenter()) : toCanvas(x, y);
    const QString text = data->text();
    if (canvas::parseFragment(text))
        return pasteText(text, x, y);
    // Files copied in a file manager: stored in the notebook (or used where they are in it).
    if (data->hasUrls() && m_host) {
        QStringList stored;
        for (const QUrl &url : data->urls()) {
            if (!url.isLocalFile() && url.scheme() != u"content")
                continue;
            const QString path = m_host->importAttachment(url, MaxAttachmentBytes);
            if (!path.isEmpty())
                stored.append(path);
        }
        if (!stored.isEmpty()) {
            const QStringList ids = addFiles(stored, at.x(), at.y());
            out.insert(u"pasted"_s, int(ids.size()));
            out.insert(u"kind"_s, u"files"_s);
            return out;
        }
    }
    // A picture (a screenshot): a PNG in the attachments folder.
    const bool onlyAddress = text.trimmed().isEmpty() || isWebAddress(text.trimmed());
    if (data->hasImage() && onlyAddress && m_host) {
        const QImage image = qvariant_cast<QImage>(data->imageData());
        QByteArray png;
        QBuffer buffer(&png);
        if (!image.isNull() && buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG")) {
            const QString name =
                    tr("Pasted image %1").arg(QDateTime::currentDateTime().toString(u"yyyyMMddHHmmss"_s)) + u".png"_s;
            const QString path = m_host->saveAttachment(name, png);
            if (!path.isEmpty()) {
                addFile(path, QString(), at.x(), at.y());
                out.insert(u"pasted"_s, 1);
                out.insert(u"kind"_s, u"picture"_s);
                return out;
            }
        }
    }
    return pasteText(text, x, y);
}

QVariantMap CanvasEditor::pasteText(const QString &text, qreal x, qreal y)
{
    QVariantMap out{{u"pasted"_s, 0}, {u"kind"_s, QString()}};
    const QPointF at =
            x < 0 || y < 0 ? (m_pointer ? toCanvas(m_pointer->x(), m_pointer->y()) : viewCenter()) : toCanvas(x, y);
    if (const std::optional<canvas::Canvas> fragment = canvas::parseFragment(text)) {
        QStringList ids;
        // The fragment's middle where it is pasted.
        const QRectF b = canvas::bounds(*fragment);
        apply(canvas::paste(m_canvas, *fragment, clampedCanvas(at - QPointF(b.width() / 2, b.height() / 2)), &ids));
        setSelection(QSet<QString>(ids.cbegin(), ids.cend()), {});
        out.insert(u"pasted"_s, int(ids.size()));
        out.insert(u"kind"_s, u"cards"_s);
        return out;
    }
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > MaxPastedText)
        return out;
    if (isWebAddress(trimmed)) {
        addLink(trimmed, at.x(), at.y());
        out.insert(u"pasted"_s, 1);
        out.insert(u"kind"_s, u"link"_s);
        return out;
    }
    addText(at.x(), at.y(), trimmed);
    out.insert(u"pasted"_s, 1);
    out.insert(u"kind"_s, u"text"_s);
    return out;
}

} // namespace brulion::canvasview
