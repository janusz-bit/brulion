#include "syncviews.h"

#include "texthelper.h"
#include "workspacesync.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::ioviews {

SyncRunInfo::SyncRunInfo(QObject *parent) : QObject(parent) { }

void SyncRunInfo::setSync(shell::WorkspaceSync *sync)
{
    if (m_sync == sync)
        return;
    m_sync = sync;
    Q_EMIT syncChanged();
    refresh();
}

void SyncRunInfo::setNumber(int number)
{
    if (m_number == number)
        return;
    m_number = number;
    Q_EMIT numberChanged();
    refresh();
}

void SyncRunInfo::refresh()
{
    // Like the dialog's former binding `sync && number > 0 ? sync.runDetails(number) : {}`: read
    // when the run changes, not on every history change.
    QVariantMap details = m_sync && m_number > 0 ? m_sync->runDetails(m_number) : QVariantMap();
    if (details == m_details)
        return;
    m_details = std::move(details);
    Q_EMIT detailsChanged();
}

QVariantMap SyncRunInfo::stats() const
{
    return m_details.value(u"stats"_s).toMap();
}

QVariantMap SyncRunInfo::counts() const
{
    return m_details.value(u"counts"_s).toMap();
}

QVariantList SyncRunInfo::phases() const
{
    return m_details.value(u"phases"_s).toList();
}

QVariantList SyncRunInfo::errors() const
{
    return m_details.value(u"errors"_s).toList();
}

QVariantList SyncRunInfo::conflicts() const
{
    return m_details.value(u"conflicts"_s).toList();
}

QVariantList SyncRunInfo::slowest() const
{
    return stats().value(u"slowest"_s).toList();
}

IoLists::IoLists(QObject *parent) : QObject(parent) { }

int IoLists::countWhere(const QVariantList &rows, const QString &key, const QString &value) const
{
    return static_cast<int>(std::ranges::count_if(
            rows, [&key, &value](const QVariant &row) { return row.toMap().value(key).toString() == value; }));
}

QStringList IoLists::column(const QVariantList &rows, const QString &key) const
{
    QStringList out;
    out.reserve(rows.size());
    for (const QVariant &row : rows)
        out.append(row.toMap().value(key).toString());
    return out;
}

QVariantList IoLists::folderItems(const QString &query, const QStringList &folders, int limit) const
{
    // The ranking itself, not TextHelper::fuzzyRank()'s maps of it: this runs at every keystroke.
    const QList<ui::FuzzyRanked> ranked = ui::fuzzyRankList(query, folders, limit, {});
    QVariantList out;
    out.reserve(ranked.size());
    for (const ui::FuzzyRanked &r : ranked) {
        if (r.index < 0 || r.index >= folders.size())
            continue;
        const QString &path = folders.at(r.index);
        const qsizetype slash = path.lastIndexOf(u'/');
        out.append(QVariantMap{
                {u"title"_s, slash >= 0 ? path.mid(slash + 1) : path},
                {u"subtitle"_s, slash >= 0 ? path.left(slash) : QString()},
                {u"icon"_s, u"folder"_s},
                {u"path"_s, path},
        });
    }
    return out;
}

} // namespace brulion::ioviews
