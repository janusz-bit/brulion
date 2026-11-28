#include "canvasmodels.h"

#include "canvas/geometry.h"
#include "markdown/blockclipboard.h"

#include <QPointF>

#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::canvasview {

namespace {

QString number(double v)
{
    return QString::number(std::round(v * 10) / 10, 'f', 1);
}

/// What a card shows as its title when zoomed out: a text card's first line (without Markdown's
/// heading marks), a file's name, a link's address, a group's label.
QString titleOf(const canvas::Node &n)
{
    if (n.isFile()) {
        const qsizetype slash = n.file.lastIndexOf(u'/');
        QString name = slash < 0 ? n.file : n.file.mid(slash + 1);
        if (name.endsWith(u".md"_s, Qt::CaseInsensitive))
            name.chop(3);
        return name + n.subpath;
    }
    if (n.isLink())
        return n.url.trimmed();
    if (n.isGroup())
        return n.label.simplified();
    for (const QStringView line : QStringView(n.text).split(u'\n')) {
        QStringView t = line.trimmed();
        while (t.startsWith(u'#') || t.startsWith(u'-') || t.startsWith(u'*') || t.startsWith(u'>'))
            t = t.sliced(1).trimmed();
        if (!t.isEmpty())
            return markdown::stripInlineMarkup(t.left(200).toString()).left(120);
    }
    return {};
}

} // namespace

QString arrowsPath(const QList<QPointF> &start, const QList<QPointF> &end)
{
    QString out;
    for (const QList<QPointF> *arrow : {&start, &end}) {
        if (arrow->size() != 3)
            continue;
        out += u"M "_s + number(arrow->at(0).x()) + u' ' + number(arrow->at(0).y());
        for (int i = 1; i < 3; ++i)
            out += u" L "_s + number(arrow->at(i).x()) + u' ' + number(arrow->at(i).y());
        out += u" Z "_s;
    }
    return out.trimmed();
}

// ---- CanvasNodeModel ------------------------------------------------------------------------------

CanvasNodeModel::CanvasNodeModel(QObject *parent) : QAbstractListModel(parent) { }

int CanvasNodeModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant CanvasNodeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    const canvas::Node &n = r.node;
    switch (role) {
    case NodeIdRole: return n.id;
    case KindRole: return n.type;
    case XRole: return canvas::nodeRect(n).x();
    case YRole: return canvas::nodeRect(n).y();
    case WidthRole: return canvas::nodeRect(n).width();
    case HeightRole: return canvas::nodeRect(n).height();
    case ColorRole: return n.color;
    case TextRole: return n.text;
    case FileRole: return n.file;
    case SubpathRole: return n.subpath;
    case UrlRole: return n.url;
    case LabelRole: return n.label;
    case BackgroundRole: return n.background;
    case BackgroundStyleRole: return n.backgroundStyle;
    case ShapeRole: return canvas::nodeStyle(n).shape;
    case BorderRole: return canvas::nodeStyle(n).border;
    case SelectedRole: return r.selected;
    case FileKindRole: return r.fileKind;
    case FilePathRole: return r.filePath;
    case TitleRole: return r.title;
    case OrderRole: return r.order;
    default: return {};
    }
}

QHash<int, QByteArray> CanvasNodeModel::roleNames() const
{
    // (Not "color", "text", "title": delegates cannot require their own types' names.)
    return {{NodeIdRole, "nodeId"},
            {KindRole, "kind"},
            {XRole, "nodeX"},
            {YRole, "nodeY"},
            {WidthRole, "nodeWidth"},
            {HeightRole, "nodeHeight"},
            {ColorRole, "cardColor"},
            {TextRole, "cardText"},
            {FileRole, "cardFile"},
            {SubpathRole, "subpath"},
            {UrlRole, "cardUrl"},
            {LabelRole, "cardLabel"},
            {BackgroundRole, "background"},
            {BackgroundStyleRole, "backgroundStyle"},
            {ShapeRole, "cardShape"},
            {BorderRole, "cardBorder"},
            {SelectedRole, "selected"},
            {FileKindRole, "fileKind"},
            {FilePathRole, "filePath"},
            {TitleRole, "cardTitle"},
            {OrderRole, "order"}};
}

int CanvasNodeModel::rowOf(const QString &id) const
{
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).node.id == id)
            return int(i);
    }
    return -1;
}

void CanvasNodeModel::sync(const canvas::Canvas &c, const QRectF &area, const QSet<QString> &selected,
                           const FileKindOf &kindOf)
{
    QList<Row> next;
    for (qsizetype i = 0; i < c.nodes.size(); ++i) {
        const canvas::Node &n = c.nodes.at(i);
        QRectF r = canvas::nodeRect(n);
        if (n.isGroup())
            r.adjust(0, -canvas::GroupLabelHeight, 0, 0); // its label above it
        if (!r.intersects(area) && !selected.contains(n.id))
            continue;
        Row row;
        row.node = n;
        row.node.origin.reset(); // (the model needs the fields only)
        row.selected = selected.contains(n.id);
        row.order = int(i);
        row.title = titleOf(n);
        if (n.isFile()) {
            auto known = m_kinds.constFind(n.file);
            if (known == m_kinds.cend())
                known = m_kinds.insert(n.file, kindOf ? kindOf(n.file) : FileKind{u"file"_s, n.file});
            row.fileKind = known->kind;
            row.filePath = known->path;
        }
        next.append(std::move(row));
    }
    const qsizetype before = m_rows.size();
    // Rows gone (from the end, so the rows before keep their numbers).
    QSet<QString> wanted;
    for (const Row &r : std::as_const(next))
        wanted.insert(r.node.id);
    for (qsizetype i = m_rows.size() - 1; i >= 0; --i) {
        if (!wanted.contains(m_rows.at(i).node.id)) {
            beginRemoveRows(QModelIndex(), int(i), int(i));
            m_rows.removeAt(i);
            endRemoveRows();
        }
    }
    // The rows left are in the order of `next` unless the z-order changed: then all anew.
    qsizetype k = 0;
    for (const Row &r : std::as_const(next)) {
        if (k < m_rows.size() && m_rows.at(k).node.id == r.node.id)
            ++k;
    }
    if (k != m_rows.size()) {
        beginResetModel();
        m_rows = next;
        endResetModel();
        if (m_rows.size() != before)
            Q_EMIT countChanged();
        return;
    }
    for (qsizetype i = 0; i < next.size(); ++i) {
        const Row &r = next.at(i);
        if (i < m_rows.size() && m_rows.at(i).node.id == r.node.id) {
            if (!m_rows.at(i).sameAs(r)) {
                m_rows[i] = r;
                const QModelIndex at = index(int(i));
                Q_EMIT dataChanged(at, at);
            }
            continue;
        }
        beginInsertRows(QModelIndex(), int(i), int(i));
        m_rows.insert(i, r);
        endInsertRows();
    }
    if (m_rows.size() != before)
        Q_EMIT countChanged();
}

// ---- CanvasEdgeModel ------------------------------------------------------------------------------

CanvasEdgeModel::CanvasEdgeModel(QObject *parent) : QAbstractListModel(parent) { }

int CanvasEdgeModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant CanvasEdgeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case EdgeIdRole: return r.id;
    case PathRole: return r.path;
    case ArrowsRole: return r.arrows;
    case ColorRole: return r.color;
    case LabelRole: return r.label;
    case LabelXRole: return r.labelPoint.x();
    case LabelYRole: return r.labelPoint.y();
    case SelectedRole: return r.selected;
    case FromNodeRole: return r.from;
    case ToNodeRole: return r.to;
    default: return {};
    }
}

QHash<int, QByteArray> CanvasEdgeModel::roleNames() const
{
    return {{EdgeIdRole, "edgeId"},     {PathRole, "edgePath"}, {ArrowsRole, "arrows"}, {ColorRole, "edgeColor"},
            {LabelRole, "edgeLabel"},   {LabelXRole, "labelX"}, {LabelYRole, "labelY"}, {SelectedRole, "selected"},
            {FromNodeRole, "fromNode"}, {ToNodeRole, "toNode"}};
}

void CanvasEdgeModel::sync(const canvas::Canvas &c, const QRectF &area, const QSet<QString> &selected)
{
    QList<Row> next;
    for (const canvas::Edge &e : c.edges) {
        const canvas::EdgeShape s = canvas::edgeShape(c, e);
        if (!s.valid)
            continue;
        const QRectF box = QRectF(s.start, s.end).normalized().united(QRectF(s.c1, s.c2).normalized());
        if (!box.adjusted(-40, -40, 40, 40).intersects(area))
            continue;
        Row r;
        r.id = e.id;
        r.path = canvas::svgPath(s);
        r.arrows = arrowsPath(s.startArrow, s.endArrow);
        r.color = e.color;
        r.label = e.label.simplified();
        r.labelPoint = s.labelPoint;
        r.selected = selected.contains(e.id);
        r.from = e.fromNode;
        r.to = e.toNode;
        next.append(std::move(r));
    }
    const qsizetype before = m_rows.size();
    QSet<QString> wanted;
    for (const Row &r : std::as_const(next))
        wanted.insert(r.id);
    for (qsizetype i = m_rows.size() - 1; i >= 0; --i) {
        if (!wanted.contains(m_rows.at(i).id)) {
            beginRemoveRows(QModelIndex(), int(i), int(i));
            m_rows.removeAt(i);
            endRemoveRows();
        }
    }
    qsizetype k = 0;
    for (const Row &r : std::as_const(next)) {
        if (k < m_rows.size() && m_rows.at(k).id == r.id)
            ++k;
    }
    if (k != m_rows.size()) {
        beginResetModel();
        m_rows = next;
        endResetModel();
    } else {
        for (qsizetype i = 0; i < next.size(); ++i) {
            const Row &r = next.at(i);
            if (i < m_rows.size() && m_rows.at(i).id == r.id) {
                if (!(m_rows.at(i) == r)) {
                    m_rows[i] = r;
                    const QModelIndex at = index(int(i));
                    Q_EMIT dataChanged(at, at);
                }
                continue;
            }
            beginInsertRows(QModelIndex(), int(i), int(i));
            m_rows.insert(i, r);
            endInsertRows();
        }
    }
    if (m_rows.size() != before)
        Q_EMIT countChanged();
}

} // namespace brulion::canvasview
