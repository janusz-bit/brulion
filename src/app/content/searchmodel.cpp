#include "searchmodel.h"

#include "attachmentindexer.h"
#include "attachments/searchfilters.h"
#include "contentutil.h"
#include "index/vaultindex.h"
#include "util/paths.h"
#include "workspace.h"

#include <QHash>

#include <algorithm>
#include <utility>

namespace brulion::content {

using namespace Qt::StringLiterals;

namespace {

QVariantList toVariantList(const QList<int> &list)
{
    return QVariantList(list.cbegin(), list.cend());
}

} // namespace

SearchResultModel::SearchResultModel(QObject *parent) : WorkspaceListModel(parent)
{
    setRefreshDelay(300);
    connect(this, &WorkspaceListModel::workspaceChanged, this, &SearchResultModel::connectAttachments);
}

void SearchResultModel::connectAttachments()
{
    disconnect(m_attachmentsConnection);
    if (workspace() && workspace()->attachments()) {
        // The attachments' text arrives in the background (and OCR's later): the results follow.
        m_attachmentsConnection =
                connect(workspace()->attachments(), &attachsearch::AttachmentIndexer::contentChanged, this, [this] {
                    if (!m_query.trimmed().isEmpty())
                        markContentChanged();
                });
    }
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
    const QString s = scope == u"pages" || scope == u"journals" || scope == u"attachments" ? scope : u"all"_s;
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
    using attachments::Scope;
    QList<Hit> next;
    int total = 0;
    int attachmentTotal = 0;
    QHash<QString, int> folderCounts;
    // The query's own filters (in:pdf …) and its words.
    const attachments::SearchFilters filters = attachments::parseSearchFilters(m_query);
    const QString text = filters.text;
    const QStringList terms = searchTerms(text);
    const VaultIndex *idx = vaultIndex();
    if (idx && workspace() && workspace()->isOpen() && !text.trimmed().isEmpty()) {
        const bool filtered = m_scope != u"all" || !m_folder.isEmpty() || filters.filtered;
        // Filters drop hits: ask for more so the list still fills up.
        const int want = filtered ? std::min(1000, m_limit * 4) : m_limit;
        QList<Hit> found;
        if (filters.scopes & attachments::NoteScopes) {
            for (const SearchHit &hit : idx->search(text, want)) {
                const ParsedNote *note = idx->note(hit.path);
                const bool journal = note && note->journalDate;
                if (!filters.scopes.testFlag(journal ? Scope::Journals : Scope::Pages))
                    continue;
                const QString title = hit.title.isEmpty() ? paths::pageTitle(hit.path) : hit.title;
                // Readable text: [[Page|text]] shows as "text" (positions are computed on this).
                const QString excerpt = oneLine(plainText(hit.excerpt, idx), 260);
                Hit row;
                row.path = hit.path;
                row.title = title;
                row.folder = paths::dirname(hit.path);
                row.line = hit.line;
                row.excerpt = excerpt;
                row.journalDate = journal ? isoDate(*note->journalDate) : QString();
                row.score = hit.score;
                row.literal = hit.literal;
                found.append(std::move(row));
            }
        }
        attachsearch::AttachmentIndexer *attachmentText = workspace()->attachments();
        const attachments::Scopes kinds = filters.scopes & attachments::AttachmentScopes;
        if (attachmentText && kinds) {
            for (const attachments::AttachmentSearch::Hit &hit : attachmentText->search(text, want, kinds)) {
                const bool pdf = hit.kind == attachments::Kind::Pdf;
                Hit row;
                row.path = hit.path;
                row.title = paths::basename(hit.path);
                row.folder = paths::dirname(hit.path);
                row.excerpt = oneLine(hit.excerpt, 260);
                row.score = hit.score;
                row.kind = pdf ? u"pdf"_s : u"picture"_s;
                row.page = pdf ? hit.page : 0;
                row.pageCount = hit.pageCount;
                row.matchedPages = hit.matchedPages;
                row.ocr = hit.ocr;
                row.find = hit.find;
                row.literal = hit.literal;
                found.append(std::move(row));
            }
        }
        // Hits come in each index's order — what the user typed (also as a prefix, diacritics
        // ignored: "zolw" in "żółwia") before typo-tolerant matches ("zolw" ~ "żółć"), see
        // SearchIndex —; notes and attachments merge by that, then by score.
        std::ranges::stable_sort(found, [](const Hit &a, const Hit &b) {
            if (a.literal != b.literal)
                return a.literal > b.literal;
            return a.score > b.score;
        });
        for (Hit &hit : found) {
            ++total;
            const bool attachment = hit.kind != u"note"_s;
            if (attachment)
                ++attachmentTotal;
            folderCounts[hit.folder.section(u'/', 0, 0)] += 1;
            const bool journal = !hit.journalDate.isEmpty();
            if ((m_scope == u"pages" && (journal || attachment)) || (m_scope == u"journals" && !journal)
                || (m_scope == u"attachments" && !attachment))
                continue;
            if (!m_folder.isEmpty() && hit.folder != m_folder && !hit.folder.startsWith(m_folder + u'/'))
                continue;
            if (next.size() >= m_limit)
                continue;
            hit.excerptMatches = toVariantList(matchPositions(hit.excerpt, terms));
            hit.titleMatches = toVariantList(matchPositions(hit.title, terms));
            next.append(std::move(hit));
        }
    }
    applyRows(m_rows, next, [](const Hit &h) { return h.path; }, [](const Hit &a, const Hit &b) { return a == b; });

    QVariantList folders;
    QList<std::pair<QString, int>> sorted;
    sorted.reserve(folderCounts.size());
    for (const auto &[folder, count] : std::as_const(folderCounts).asKeyValueRange())
        sorted.emplaceBack(folder, count);
    std::ranges::sort(sorted, [](const auto &a, const auto &b) {
        return a.second != b.second ? a.second > b.second : compareNames(a.first, b.first) < 0;
    });
    for (const auto &[folder, count] : sorted)
        folders.append(QVariantMap{ { u"folder"_s, folder }, { u"count"_s, count } });
    if (total != m_total || terms != m_terms || folders != m_folders || filters.filtered != m_queryFiltered
        || attachmentTotal != m_attachmentCount) {
        m_total = total;
        m_terms = terms;
        m_folders = folders;
        m_queryFiltered = filters.filtered;
        m_attachmentCount = attachmentTotal;
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
    case KindRole: return h.kind;
    case PageRole: return h.page;
    case PageCountRole: return h.pageCount;
    case MatchedPagesRole: return h.matchedPages;
    case OcrRole: return h.ocr;
    case FindRole: return h.find;
    }
    return {};
}

QHash<int, QByteArray> SearchResultModel::roleNames() const
{
    static const QHash<int, QByteArray> names{{PathRole, "path"},
                                              {TitleRole, "title"},
                                              {FolderRole, "folder"},
                                              {LineRole, "line"},
                                              {ExcerptRole, "excerpt"},
                                              {ExcerptMatchesRole, "excerptMatches"},
                                              {TitleMatchesRole, "titleMatches"},
                                              {JournalDateRole, "journalDate"},
                                              {ScoreRole, "score"},
                                              {KindRole, "kind"},
                                              {PageRole, "page"},
                                              {PageCountRole, "pageCount"},
                                              {MatchedPagesRole, "matchedPages"},
                                              {OcrRole, "ocr"},
                                              {FindRole, "find"}};
    return names;
}

QVariantMap SearchResultModel::get(int row) const
{
    return rowMap(row);
}

} // namespace brulion::content
