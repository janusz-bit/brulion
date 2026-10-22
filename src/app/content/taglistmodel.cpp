#include "taglistmodel.h"

#include "contentutil.h"
#include "index/vaultindex.h"
#include "util/paths.h"
#include "workspace.h"

#include <algorithm>

namespace brulion::content {

using namespace Qt::StringLiterals;

TagListModel::TagListModel(QObject *parent) : WorkspaceListModel(parent)
{
    setRefreshDelay(300);
}

void TagListModel::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    Q_EMIT filterChanged();
    scheduleRefresh(0);
}

void TagListModel::setSortKey(const QString &key)
{
    const QString k = key == u"name" ? u"name"_s : u"count"_s;
    if (m_sortKey == k)
        return;
    m_sortKey = k;
    m_resetNext = true;
    Q_EMIT sortKeyChanged();
    scheduleRefresh(0);
}

void TagListModel::refresh()
{
    QList<Tag> next;
    int total = 0;
    const VaultIndex *idx = vaultIndex();
    if (idx && workspace() && workspace()->isOpen()) {
        const QStringList terms = searchTerms(m_filter);
        for (const TagCount &t : idx->tags()) {
            ++total;
            const QString display = t.display.isEmpty() ? t.tag : t.display;
            if (!containsAll(display, terms))
                continue;
            Tag tag;
            tag.tag = t.tag;
            tag.display = display;
            tag.count = t.count;
            tag.pageCount = int(t.paths.size());
            for (const int p : matchPositions(display, terms, false))
                tag.matches.append(p);
            tag.page = idx->resolveTag(t.tag).value_or(QString());
            next.append(tag);
        }
        if (m_sortKey == u"name") {
            // Segment by segment, so "praca/projekt" sorts right after "praca" (not after "praca-dom").
            std::stable_sort(next.begin(), next.end(), [](const Tag &a, const Tag &b) {
                const QStringList sa = a.display.split(u'/');
                const QStringList sb = b.display.split(u'/');
                for (qsizetype i = 0; i < std::min(sa.size(), sb.size()); ++i) {
                    const int c = compareNames(sa.at(i), sb.at(i));
                    if (c != 0)
                        return c < 0;
                }
                return sa.size() < sb.size();
            });
        }
    }
    if (m_resetNext) {
        m_resetNext = false;
        beginResetModel();
        m_rows = next;
        endResetModel();
    } else {
        applyRows(m_rows, next, [](const Tag &t) { return t.tag; }, [](const Tag &a, const Tag &b) { return a == b; });
    }
    if (total != m_total) {
        m_total = total;
        Q_EMIT totalCountChanged();
    }
}

QVariant TagListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Tag &t = m_rows.at(index.row());
    const qsizetype slash = t.display.lastIndexOf(u'/');
    switch (role) {
    case TagRole: return t.tag;
    case DisplayRole: return t.display;
    case CountRole: return t.count;
    case PageCountRole: return t.pageCount;
    case DepthRole: return int(t.tag.count(u'/'));
    case LeafRole: return slash >= 0 ? t.display.mid(slash + 1) : t.display;
    case ParentRole: return slash >= 0 ? t.tag.left(t.tag.lastIndexOf(u'/')) : QString();
    case MatchesRole: return t.matches;
    case PageRole: return t.page;
    case PageTitleRole: return t.page.isEmpty() ? QString() : paths::pageTitle(t.page);
    }
    return {};
}

QHash<int, QByteArray> TagListModel::roleNames() const
{
    return {{TagRole, "tag"},     {DisplayRole, "display"},    {CountRole, "count"},   {PageCountRole, "pageCount"},
            {DepthRole, "depth"}, {LeafRole, "leaf"},          {ParentRole, "parent"}, {MatchesRole, "matches"},
            {PageRole, "page"},   {PageTitleRole, "pageTitle"}};
}

QVariantMap TagListModel::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_rows.size())
        return out;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        out.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return out;
}

} // namespace brulion::content
