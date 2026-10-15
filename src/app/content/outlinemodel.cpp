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
            connect(m_source, &QObject::destroyed, this, [this] { schedule(); }),
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
    if (m_source) {
        const QHash<int, QByteArray> roles = m_source->roleNames();
        const auto roleOf = [&roles](const QByteArray &name) {
            for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
                if (it.value() == name)
                    return it.key();
            }
            return -1;
        };
        const int levelRole = roleOf("headingLevel");
        const int textRole = roleOf("plainText");
        const int viewRole = roleOf("view");
        const int lineRole = roleOf("line");
        if (levelRole >= 0 && textRole >= 0) {
            const int n = m_source->rowCount();
            for (int i = 0; i < n; ++i) {
                const QModelIndex idx = m_source->index(i, 0);
                const int level = m_source->data(idx, levelRole).toInt();
                if (level <= 0)
                    continue;
                if (viewRole >= 0 && m_source->data(idx, viewRole).toString() != u"heading")
                    continue;
                const QString text = m_source->data(idx, textRole).toString().simplified();
                if (text.isEmpty())
                    continue;
                next.append(Entry{ i, level, 0, text, lineRole >= 0 ? m_source->data(idx, lineRole).toInt() : -1 });
            }
        }
    }
    int minLevel = 6;
    for (const Entry &e : std::as_const(next))
        minLevel = std::min(minLevel, e.level);
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
    int found = -1;
    for (int i = 0; i < m_rows.size() && m_rows.at(i).row <= sourceRow; ++i)
        found = i;
    return found;
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
    return { { RowRole, "row" }, { LevelRole, "level" }, { DepthRole, "depth" }, { TextRole, "text" }, { LineRole, "line" } };
}

} // namespace brulion::content
