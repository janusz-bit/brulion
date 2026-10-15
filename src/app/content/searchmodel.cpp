#include "searchmodel.h"

#include "contentutil.h"
#include "index/vaultindex.h"
#include "util/paths.h"
#include "workspace.h"

#include <QHash>

#include <algorithm>

namespace brulion::content {

using namespace Qt::StringLiterals;

namespace {

QVariantList toVariantList(const QList<int> &list)
{
    QVariantList out;
    out.reserve(list.size());
    for (const int v : list)
        out.append(v);
    return out;
}

} // namespace

SearchResultModel::SearchResultModel(QObject *parent) : WorkspaceListModel(parent)
{
    setRefreshDelay(300);
}

void SearchResultModel::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    Q_EMIT queryChanged();
    scheduleRefresh(0);
}

void SearchResultModel::setScope(const QString &scope)
{
    const QString s = scope == u"pages" || scope == u"journals" ? scope : u"all"_s;
    if (m_scope == s)
        return;
    m_scope = s;
    Q_EMIT scopeChanged();
    scheduleRefresh(0);
}

void SearchResultModel::setFolder(const QString &folder)
{
    const QString f = paths::normalize(folder);
    if (m_folder == f)
        return;
    m_folder = f;
    Q_EMIT folderChanged();
    scheduleRefresh(0);
}

void SearchResultModel::setLimit(int limit)
{
    limit = std::clamp(limit, 1, 1000);
    if (m_limit == limit)
        return;
    m_limit = limit;
    Q_EMIT limitChanged();
    scheduleRefresh(0);
}

void SearchResultModel::refresh()
{
    QList<Hit> next;
    int total = 0;
    QHash<QString, int> folderCounts;
    const QStringList terms = searchTerms(m_query);
    const VaultIndex *idx = vaultIndex();
    if (idx && workspace() && workspace()->isOpen() && !m_query.trimmed().isEmpty()) {
        const bool filtered = m_scope != u"all" || !m_folder.isEmpty();
        // Filters drop hits: ask for more so the list still fills up.
        const int want = filtered ? std::min(1000, m_limit * 4) : m_limit;
        for (const SearchHit &hit : idx->search(m_query, want)) {
            ++total;
            const ParsedNote *note = idx->note(hit.path);
            const bool journal = note && note->journalDate;
            const QString folder = paths::dirname(hit.path);
            folderCounts[folder.section(u'/', 0, 0)] += 1;
            if ((m_scope == u"pages" && journal) || (m_scope == u"journals" && !journal))
                continue;
            if (!m_folder.isEmpty() && folder != m_folder && !folder.startsWith(m_folder + u'/'))
                continue;
            if (next.size() >= m_limit)
                continue;
            Hit h;
            h.path = hit.path;
            h.title = hit.title.isEmpty() ? paths::pageTitle(hit.path) : hit.title;
            h.folder = folder;
            h.line = hit.line;
            // Readable text: [[Strona|tekst]] shows as "tekst" (positions are computed on this).
            h.excerpt = oneLine(plainText(hit.excerpt, idx), 260);
            h.excerptMatches = toVariantList(matchPositions(h.excerpt, terms));
            h.titleMatches = toVariantList(matchPositions(h.title, terms));
            h.journalDate = journal ? isoDate(*note->journalDate) : QString();
            h.score = hit.score;
            next.append(h);
        }
    }
    applyRows(m_rows, next, [](const Hit &h) { return h.path; }, [](const Hit &a, const Hit &b) { return a == b; });

    QVariantList folders;
    QList<std::pair<QString, int>> sorted;
    for (auto it = folderCounts.cbegin(); it != folderCounts.cend(); ++it)
        sorted.append({ it.key(), it.value() });
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return a.second != b.second ? a.second > b.second : compareNames(a.first, b.first) < 0;
    });
    for (const auto &[folder, count] : sorted)
        folders.append(QVariantMap{ { u"folder"_s, folder }, { u"count"_s, count } });
    if (total != m_total || terms != m_terms || folders != m_folders) {
        m_total = total;
        m_terms = terms;
        m_folders = folders;
        Q_EMIT resultsChanged();
    }
}

QVariant SearchResultModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Hit &h = m_rows.at(index.row());
    switch (role) {
    case PathRole: return h.path;
    case TitleRole: return h.title;
    case FolderRole: return h.folder;
    case LineRole: return h.line;
    case ExcerptRole: return h.excerpt;
    case ExcerptMatchesRole: return h.excerptMatches;
    case TitleMatchesRole: return h.titleMatches;
    case JournalDateRole: return h.journalDate;
    case ScoreRole: return h.score;
    }
    return {};
}

QHash<int, QByteArray> SearchResultModel::roleNames() const
{
    return { { PathRole, "path" },           { TitleRole, "title" },
             { FolderRole, "folder" },       { LineRole, "line" },
             { ExcerptRole, "excerpt" },     { ExcerptMatchesRole, "excerptMatches" },
             { TitleMatchesRole, "titleMatches" }, { JournalDateRole, "journalDate" },
             { ScoreRole, "score" } };
}

QVariantMap SearchResultModel::get(int row) const
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
