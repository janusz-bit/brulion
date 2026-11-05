#include "blockviews.h"

#include <QCoreApplication>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::editor {

// ---- BlockViews -------------------------------------------------------------------------------

BlockViews::BlockViews(QObject *parent) : QObject(parent) { }

namespace {

/// `shown[row] !== false` of a table's `shown` flags (a row past them is shown).
bool rowShown(const QVariantList &shown, int row)
{
    if (row < 0 || row >= shown.size())
        return true;
    const QVariant &v = shown.at(row);
    return !(v.typeId() == QMetaType::Bool && !v.toBool());
}

QString typeIcon(const QString &type, bool unsupportedIcon)
{
    if (type == u"number")
        return u"sigma"_s;
    if (type == u"checkbox")
        return u"square-check-big"_s;
    if (type == u"date")
        return u"calendar"_s;
    if (type == u"datetime")
        return u"calendar-clock"_s;
    if (type == u"list")
        return u"list"_s;
    if (type == u"tags")
        return u"tags"_s;
    if (type == u"aliases")
        return u"bookmark"_s;
    if (type == u"unsupported" && unsupportedIcon)
        return u"code"_s;
    return u"type"_s;
}

} // namespace

QList<int> BlockViews::tableShownRows(const QVariantMap &table, int editRow) const
{
    const QVariantList shown = table.value(u"shown"_s).toList();
    const qsizetype rows = table.value(u"rows"_s).toList().size();
    QList<int> out;
    out.reserve(rows);
    for (int r = 0; r < rows; ++r) {
        if (rowShown(shown, r) || r == editRow)
            out.append(r);
    }
    return out;
}

int BlockViews::tableNextShownRow(const QVariantMap &table, int row, int step) const
{
    const QVariantList shown = table.value(u"shown"_s).toList();
    const int rows = int(table.value(u"rows"_s).toList().size());
    if (step == 0)
        return row;
    for (int n = row + step; n >= 0 && n < rows; n += step) {
        if (rowShown(shown, n))
            return n;
    }
    return step > 0 ? rows : -1;
}

QList<qreal> BlockViews::tableColumnWidths(const QVariantMap &table, const QFont &headerFont, const QFont &cellFont,
                                           qreal width, int padding) const
{
    const QStringList header = table.value(u"header"_s).toStringList();
    const QVariantList rows = table.value(u"rows"_s).toList();
    const QFontMetricsF headerMetrics(headerFont);
    const QFontMetricsF cellMetrics(cellFont);
    QList<qreal> out;
    out.reserve(header.size());
    qreal total = 0;
    for (qsizetype c = 0; c < header.size(); ++c) {
        qreal w = headerMetrics.horizontalAdvance(header.at(c));
        for (const QVariant &row : rows) {
            const QVariantList cells = row.toList();
            if (c >= cells.size())
                continue;
            const QVariantMap cell = cells.at(c).toMap();
            if (cell.isEmpty())
                continue;
            const QString text =
                    cell.contains(u"plain"_s) ? cell.value(u"plain"_s).toString() : cell.value(u"display"_s).toString();
            w = std::max(w, cellMetrics.horizontalAdvance(text));
        }
        w = std::min<qreal>(280, std::max<qreal>(56, std::ceil(w) + 2 * padding));
        out.append(w);
        total += w;
    }
    if (total > 0 && total < width) {
        const qreal k = width / total;
        for (qreal &w : out)
            w = std::floor(w * k);
    }
    return out;
}

QVariantMap BlockViews::tableCell(const QVariantMap &table, int row, int column) const
{
    QVariantMap out{{u"text"_s, QString()},  {u"display"_s, QString()}, {u"html"_s, QString()},
                    {u"plain"_s, QString()}, {u"error"_s, QString()},   {u"formula"_s, false}};
    if (column < 0)
        return out;
    if (row == -1) {
        const QStringList header = table.value(u"header"_s).toStringList();
        const QVariantList headerHtml = table.value(u"headerHtml"_s).toList();
        if (column < header.size()) {
            out[u"text"_s] = header.at(column);
            out[u"display"_s] = header.at(column);
            out[u"plain"_s] = header.at(column);
        }
        if (column < headerHtml.size())
            out[u"html"_s] = headerHtml.at(column).toString();
        return out;
    }
    const QVariantList rows = table.value(u"rows"_s).toList();
    if (row < 0 || row >= rows.size())
        return out;
    const QVariantList cells = rows.at(row).toList();
    if (column >= cells.size())
        return out;
    const QVariantMap cell = cells.at(column).toMap();
    for (auto it = cell.cbegin(); it != cell.cend(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

qreal BlockViews::widestText(const QFont &font, const QStringList &texts) const
{
    const QFontMetricsF metrics(font);
    qreal widest = 0;
    for (const QString &t : texts)
        widest = std::max(widest, metrics.horizontalAdvance(t));
    return widest;
}

QString BlockViews::resolvedFamily(const QFont &font) const
{
    return QFontInfo(font).family();
}

QString BlockViews::colorName(const QColor &color) const
{
    return color.name(color.alpha() != 255 ? QColor::HexArgb : QColor::HexRgb);
}

QStringList BlockViews::calcResultTexts(const QVariantList &results) const
{
    QStringList out;
    out.reserve(results.size());
    for (const QVariant &v : results) {
        const QVariantMap r = v.toMap();
        const QString error = r.value(u"error"_s).toString();
        out.append(!error.isEmpty() ? error : r.value(u"value"_s).toString());
    }
    return out;
}

int BlockViews::lineStart(const QString &text, int line) const
{
    if (line <= 0)
        return 0;
    int seen = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) == u'\n' && ++seen == line)
            return int(i + 1);
    }
    return 0;
}

QVariantMap BlockViews::propertyOf(const QVariantList &properties, const QString &key) const
{
    for (const QVariant &v : properties) {
        const QVariantMap p = v.toMap();
        if (p.value(u"key"_s).toString() == key)
            return p;
    }
    return {};
}

QStringList BlockViews::fieldTexts(const QVariantList &entries, const QString &field) const
{
    QStringList out;
    out.reserve(entries.size());
    for (const QVariant &v : entries)
        out.append(valueText(v.toMap().value(field)));
    return out;
}

QStringList BlockViews::headerListKeys(const QVariantMap &pageProperties) const
{
    QStringList out;
    for (const QString &list : {u"tags"_s, u"aliases"_s}) {
        for (const QVariant &v : pageProperties.value(list).toList()) {
            const QVariantMap t = v.toMap();
            out.append(t.value(u"source"_s).toString() + u':' + t.value(u"property"_s).toString());
        }
    }
    return out;
}

QString BlockViews::valueText(const QVariant &value) const
{
    if (!value.isValid() || value.isNull())
        return {};
    switch (value.typeId()) {
    case QMetaType::Bool: return value.toBool() ? u"true"_s : u"false"_s;
    case QMetaType::Double:
    case QMetaType::Float: {
        const double d = value.toDouble();
        if (std::isnan(d))
            return u"NaN"_s;
        if (std::isinf(d))
            return d > 0 ? u"Infinity"_s : u"-Infinity"_s;
        return QString::number(d, 'g', QLocale::FloatingPointShortest);
    }
    case QMetaType::QStringList: return value.toStringList().join(u',');
    case QMetaType::QVariantList: {
        QStringList parts;
        for (const QVariant &v : value.toList())
            parts.append(valueText(v));
        return parts.join(u',');
    }
    default: return value.toString();
    }
}

QString BlockViews::textAt(const QVariantMap &map, const QString &key) const
{
    return valueText(map.value(key));
}

QStringList BlockViews::splitListItems(const QString &text) const
{
    QStringList parts;
    int depth = 0;
    bool quoted = false;
    qsizetype start = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        const QChar next = i + 1 < text.size() ? text.at(i + 1) : QChar();
        if (quoted) {
            if (c == u'"')
                quoted = false;
            continue;
        }
        if (c == u'"') {
            quoted = true;
        } else if (c == u'[' && next == u'[') {
            ++depth;
            ++i;
        } else if (c == u']' && next == u']' && depth > 0) {
            --depth;
            ++i;
        } else if (c == u',' && depth == 0) {
            parts.append(text.mid(start, i - start));
            start = i + 1;
        }
    }
    parts.append(text.mid(start));
    QStringList out;
    for (const QString &p : std::as_const(parts)) {
        const QString t = p.trimmed();
        if (!t.isEmpty())
            out.append(t);
    }
    return out;
}

QString BlockViews::listLinkTarget(const QString &item) const
{
    static const QRegularExpression re(uR"(^\[\[([^\[\]|]+)(\|[^\[\]]*)?\]\]$)"_s);
    const auto m = re.match(item.trimmed());
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

QVariantList BlockViews::propertyKeyRows(const QVariantList &suggestions, bool withCount, bool unsupportedIcon) const
{
    QVariantList out;
    out.reserve(suggestions.size());
    for (const QVariant &v : suggestions) {
        const QVariantMap k = v.toMap();
        const QString key = k.value(u"key"_s).toString();
        out.append(QVariantMap{
                {u"title"_s, key},
                {u"subtitle"_s, withCount ? QCoreApplication::translate("PropertiesPanel", "%n page(s)", nullptr,
                                                                        k.value(u"count"_s).toInt())
                                          : QString()},
                {u"icon"_s, typeIcon(k.value(u"type"_s).toString(), unsupportedIcon)},
                {u"value"_s, key}});
    }
    return out;
}

QVariantList BlockViews::propertyValueRows(const QVariantList &suggestions, bool forList, const QStringList &held,
                                           const QString &context) const
{
    QStringList heldLower;
    heldLower.reserve(held.size());
    for (const QString &h : held)
        heldLower.append(h.toLower());
    const QByteArray ctx = context.toUtf8();
    // The word for a page's row, translated in the QML file asking (marked here for lupdate:
    // `context` is one of these).
    static const char *const pageWords[] = {QT_TRANSLATE_NOOP("PropertyRow", "page"),
                                            QT_TRANSLATE_NOOP("PropertyListValue", "page")};
    Q_UNUSED(pageWords);
    QVariantList out;
    for (const QVariant &v : suggestions) {
        const QVariantMap s = v.toMap();
        const QString value = valueText(s.value(u"value"_s));
        const QString kind = s.value(u"kind"_s).toString();
        const bool page = kind == u"page";
        if (forList && heldLower.contains(value.toLower()))
            continue;
        QString subtitle;
        if (page)
            subtitle = QCoreApplication::translate(ctx.constData(), "page");
        else if (forList && s.value(u"count"_s).toInt() > 0)
            subtitle = QString::number(s.value(u"count"_s).toInt());
        const QString icon = page ? u"file-text"_s : forList && kind == u"tag" ? u"hash"_s : QString();
        out.append(QVariantMap{{u"title"_s, forList && kind == u"tag" ? u"#"_s + value : value},
                               {u"subtitle"_s, subtitle},
                               {u"icon"_s, icon},
                               {u"value"_s, value}});
    }
    return out;
}

QVariantList BlockViews::preparedItems(const QVariantList &items) const
{
    QVariantList out;
    for (const QVariant &v : items) {
        const QVariantMap item = v.toMap();
        if (item.contains(u"error"_s) && item.value(u"error"_s).toString().isEmpty())
            out.append(v);
    }
    return out;
}

QString BlockViews::firstItemError(const QVariantList &items) const
{
    for (const QVariant &v : items) {
        const QVariantMap item = v.toMap();
        if (!item.contains(u"error"_s) || !item.value(u"error"_s).toString().isEmpty())
            return item.value(u"error"_s).toString();
    }
    return {};
}

int BlockViews::transclusionTaskLine(const QVariantList &transclusions, const QString &path, const QString &html,
                                     int line) const
{
    int best = -1;
    for (const QVariant &f : transclusions) {
        for (const QVariant &e : f.toMap().value(u"blocks"_s).toList()) {
            const QVariantMap b = e.toMap();
            if (b.value(u"kind"_s).toString() == u"embed" || b.value(u"task"_s).toString().isEmpty()
                || b.value(u"path"_s).toString() != path || b.value(u"html"_s).toString() != html)
                continue;
            const int l = b.value(u"line"_s).toInt();
            if (best < 0 || std::abs(l - line) < std::abs(best - line))
                best = l;
        }
    }
    return best;
}

// ---- ChipFlow ---------------------------------------------------------------------------------

ChipFlow::ChipFlow(QQuickItem *parent) : QQuickItem(parent) { }

void ChipFlow::setSpacing(qreal spacing)
{
    if (qFuzzyCompare(spacing, m_spacing))
        return;
    m_spacing = spacing;
    Q_EMIT spacingChanged();
    relayout();
}

void ChipFlow::setKeepWith(QQuickItem *item)
{
    if (item == m_keepWith)
        return;
    m_keepWith = item;
    Q_EMIT keepWithChanged();
    relayout();
}

ChipFlow::Layout ChipFlow::compute(qreal w) const
{
    QList<QQuickItem *> shown;
    for (QQuickItem *c : childItems()) {
        if (c->isVisible() && c->width() > 0)
            shown.append(c);
    }
    const qreal s = m_spacing;
    // The line each item starts: a break before index `forced` too.
    const auto lines = [&](qsizetype forced) {
        QList<qsizetype> starts;
        qreal x = 0;
        for (qsizetype i = 0; i < shown.size(); ++i) {
            if (i == forced || (x > 0 && x + shown[i]->width() > w)) {
                starts.append(i);
                x = 0;
            } else if (i == 0) {
                starts.append(0);
            }
            x += shown[i]->width() + s;
        }
        return starts;
    };
    QList<qsizetype> starts = lines(-1);
    const qsizetype add = m_keepWith ? shown.indexOf(m_keepWith.data()) : -1;
    if (add > 1 && starts.indexOf(add) > 0 && !starts.contains(add - 1)
        && shown[add - 1]->width() + s + m_keepWith->width() <= w)
        starts = lines(add - 1);
    Layout out;
    qreal y = 0;
    for (qsizetype l = 0; l < starts.size(); ++l) {
        const qsizetype first = starts[l];
        const qsizetype last = l + 1 < starts.size() ? starts[l + 1] : shown.size();
        qreal x = 0;
        qreal line = 0;
        for (qsizetype i = first; i < last; ++i) {
            out.places.append({shown[i], x, y});
            x += shown[i]->width() + s;
            line = std::max(line, shown[i]->height());
        }
        out.lineHeight = std::max(out.lineHeight, line);
        y += line + (l + 1 < starts.size() ? s : 0);
    }
    out.height = y;
    return out;
}

QVariantMap ChipFlow::layoutFor(qreal width) const
{
    const Layout layout = compute(width);
    QVariantList items;
    for (const Place &p : layout.places)
        items.append(QVariantMap{{u"item"_s, QVariant::fromValue<QObject *>(p.item)}, {u"x"_s, p.x}, {u"y"_s, p.y}});
    return {{u"items"_s, items}, {u"height"_s, layout.height}, {u"lineHeight"_s, layout.lineHeight}};
}

void ChipFlow::watch(QQuickItem *child)
{
    connect(child, &QQuickItem::widthChanged, this, &ChipFlow::relayout, Qt::UniqueConnection);
    connect(child, &QQuickItem::heightChanged, this, &ChipFlow::relayout, Qt::UniqueConnection);
    connect(child, &QQuickItem::visibleChanged, this, &ChipFlow::relayout, Qt::UniqueConnection);
}

void ChipFlow::relayout()
{
    if (m_laying || !isComponentComplete())
        return;
    m_laying = true;
    const Layout layout = compute(width());
    // Hidden items keep their place at 0, 0 (as the bindings it replaces had them).
    for (QQuickItem *c : childItems()) {
        const auto it =
                std::find_if(layout.places.cbegin(), layout.places.cend(), [c](const Place &p) { return p.item == c; });
        if (it != layout.places.cend())
            c->setPosition(QPointF(it->x, it->y));
        else
            c->setPosition(QPointF(0, 0));
    }
    const bool changed = !qFuzzyCompare(layout.lineHeight + 1, m_lineHeight + 1)
                         || !qFuzzyCompare(layout.height + 1, m_contentHeight + 1);
    m_lineHeight = layout.lineHeight;
    m_contentHeight = layout.height;
    setImplicitHeight(layout.height);
    m_laying = false;
    if (changed)
        Q_EMIT layoutChanged();
}

void ChipFlow::itemChange(ItemChange change, const ItemChangeData &data)
{
    QQuickItem::itemChange(change, data);
    if (change == ItemChildAddedChange && data.item) {
        watch(data.item);
        relayout();
    } else if (change == ItemChildRemovedChange && data.item) {
        disconnect(data.item, nullptr, this, nullptr);
        relayout();
    }
}

void ChipFlow::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.width() != oldGeometry.width())
        relayout();
}

void ChipFlow::componentComplete()
{
    QQuickItem::componentComplete();
    for (QQuickItem *c : childItems())
        watch(c);
    relayout();
}

// ---- KeyListModel -----------------------------------------------------------------------------

KeyListModel::KeyListModel(QObject *parent) : QAbstractListModel(parent) { }

void KeyListModel::setKeys(const QStringList &keys)
{
    if (keys == m_keys)
        return;
    // As few changes as it takes: the keys gone are removed, those that stay are moved into
    // place, the new ones inserted — the delegates of the keys that stay are kept.
    for (qsizetype i = m_keys.size() - 1; i >= 0; --i) {
        if (keys.contains(m_keys.at(i)))
            continue;
        beginRemoveRows({}, int(i), int(i));
        m_keys.removeAt(i);
        endRemoveRows();
    }
    for (qsizetype i = 0; i < keys.size(); ++i) {
        if (i < m_keys.size() && m_keys.at(i) == keys.at(i))
            continue;
        const qsizetype at = m_keys.indexOf(keys.at(i), i + 1);
        if (at >= 0) {
            beginMoveRows({}, int(at), int(at), {}, int(i));
            m_keys.move(at, i);
            endMoveRows();
        } else {
            beginInsertRows({}, int(i), int(i));
            m_keys.insert(i, keys.at(i));
            endInsertRows();
        }
    }
    // (Repeated keys: what is left past the new list goes.)
    if (m_keys.size() > keys.size()) {
        beginRemoveRows({}, int(keys.size()), int(m_keys.size() - 1));
        m_keys.resize(keys.size());
        endRemoveRows();
    }
    Q_EMIT keysChanged();
}

int KeyListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_keys.size());
}

QVariant KeyListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_keys.size())
        return {};
    if (role == Qt::UserRole + 1 || role == Qt::DisplayRole)
        return m_keys.at(index.row());
    return {};
}

QHash<int, QByteArray> KeyListModel::roleNames() const
{
    return {{Qt::UserRole + 1, "key"}};
}

} // namespace brulion::editor
