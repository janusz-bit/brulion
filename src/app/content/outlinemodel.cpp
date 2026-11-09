#include "outlinemodel.h"

#include <algorithm>

namespace brulion::content {

using namespace Qt::StringLiterals;

OutlineModel::OutlineModel(QObject *parent) : QAbstractListModel(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, &OutlineModel::rebuild);
}

void OutlineModel::setSource(QAbstractItemModel *source)
{
    if (m_source == source)
        return;
    for (const QMetaObject::Connection &c : std::as_const(m_connections))
        disconnect(c);
    m_connections.clear();
    m_source = source;
    if (m_source) {
        const auto changed = [this] { schedule(); };
        m_connections = {
                connect(m_source, &QAbstractItemModel::dataChanged, this, changed),
                connect(m_source, &QAbstractItemModel::rowsInserted, this, changed),
                connect(m_source, &QAbstractItemModel::rowsRemoved, this, changed),
                connect(m_source, &QAbstractItemModel::rowsMoved, this, changed),
                connect(m_source, &QAbstractItemModel::modelReset, this, changed),
                connect(m_source, &QAbstractItemModel::layoutChanged, this, changed),
                connect(m_source, &QObject::destroyed, this, changed),
        };
    }
    Q_EMIT sourceChanged();
    rebuild();
}

void OutlineModel::schedule()
{
    if (!m_timer.isActive())
        m_timer.start();
}

void OutlineModel::rebuild()
{
    m_timer.stop();
    QList<Entry> next;
    int minLevel = 6;
    if (m_source) {
        const QHash<int, QByteArray> roles = m_source->roleNames();
        const int levelRole = roles.key(QByteArrayLiteral("headingLevel"), -1);
        const int textRole = roles.key(QByteArrayLiteral("plainText"), -1);
        const int viewRole = roles.key(QByteArrayLiteral("view"), -1);
        const int lineRole = roles.key(QByteArrayLiteral("line"), -1);
        if (levelRole >= 0 && textRole >= 0) {
            for (int i = 0, n = m_source->rowCount(); i < n; ++i) {
                const QModelIndex idx = m_source->index(i, 0);
                const int level = m_source->data(idx, levelRole).toInt();
                if (level <= 0)
                    continue;
                if (viewRole >= 0 && m_source->data(idx, viewRole).toString() != u"heading")
                    continue;
                const QString text = m_source->data(idx, textRole).toString().simplified();
                if (text.isEmpty())
                    continue;
                const int line = lineRole >= 0 ? m_source->data(idx, lineRole).toInt() : -1;
                next.append(Entry{.row = i, .level = level, .text = text, .line = line});
                minLevel = std::min(minLevel, level);
            }
        }
    }
    for (Entry &e : next)
        e.depth = std::max(0, e.level - minLevel);
    if (next == m_rows)
        return;
    const bool countChange = next.size() != m_rows.size();
    if (countChange) {
        beginResetModel();
        m_rows = next;
        endResetModel();
        Q_EMIT countChanged();
        return;
    }
    // Same number of headings (typing in one): update in place, the list keeps its delegates.
    m_rows = next;
    Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1));
}

int OutlineModel::entryForSourceRow(int sourceRow) const
{
    // The rows are in source order: the last heading at or above sourceRow.
    return int(std::ranges::upper_bound(m_rows, sourceRow, {}, &Entry::row) - m_rows.cbegin()) - 1;
}

int OutlineModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant OutlineModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Entry &e = m_rows.at(index.row());
    switch (role) {
    case RowRole: return e.row;
    case LevelRole: return e.level;
    case DepthRole: return e.depth;
    case TextRole: return e.text;
    case LineRole: return e.line;
    }
    return {};
}

QHash<int, QByteArray> OutlineModel::roleNames() const
{
    static const QHash<int, QByteArray> names{
            {RowRole, "row"}, {LevelRole, "level"}, {DepthRole, "depth"}, {TextRole, "text"}, {LineRole, "line"}};
    return names;
}

} // namespace brulion::content
