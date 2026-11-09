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
        const QList<TagCount> all = idx->tags();
        total = int(all.size());
        for (const TagCount &t : all) {
            const QString display = t.display.isEmpty() ? t.tag : t.display;
            if (!containsAll(display, terms))
                continue;
            const QList<int> positions = matchPositions(display, terms, false);
            next.append(Tag{.tag = t.tag,
                            .display = display,
                            .count = t.count,
                            .pageCount = int(t.paths.size()),
                            .matches = QVariantList(positions.cbegin(), positions.cend()),
                            .page = idx->resolveTag(t.tag).value_or(QString())});
        }
        if (m_sortKey == u"name") {
            // Segment by segment, so "praca/projekt" sorts right after "praca" (not after "praca-dom").
            const auto before = [](const QString &a, const QString &b) { return compareNames(a, b) < 0; };
            std::ranges::stable_sort(next, [before](const Tag &a, const Tag &b) {
                return std::ranges::lexicographical_compare(a.display.split(u'/'), b.display.split(u'/'), before);
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
    static const QHash<int, QByteArray> names{
            {TagRole, "tag"},     {DisplayRole, "display"},    {CountRole, "count"},   {PageCountRole, "pageCount"},
            {DepthRole, "depth"}, {LeafRole, "leaf"},          {ParentRole, "parent"}, {MatchesRole, "matches"},
            {PageRole, "page"},   {PageTitleRole, "pageTitle"}};
    return names;
}

QVariantMap TagListModel::get(int row) const
{
    return rowMap(row);
}

} // namespace brulion::content
