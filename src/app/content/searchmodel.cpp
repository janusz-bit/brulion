#include "searchmodel.h"

#include "attachmentindexer.h"
#include "attachments/searchfilters.h"
#include "contentutil.h"
#include "index/vaultindex.h"
#include "semantic/fusion.h"
#include "semantic/notes.h"
#include "semanticsearch.h"
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
    m_meaningTimer.setSingleShot(true);
    m_meaningTimer.setInterval(MeaningWaitMs);
    connect(&m_meaningTimer, &QTimer::timeout, this, [this] { scheduleRefresh(0); });
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
    disconnect(m_semanticContent);
    disconnect(m_semanticQuery);
    disconnect(m_semanticState);
    if (workspace() && workspace()->semantic()) {
        shell::SemanticSearch *semantic = workspace()->semantic();
        // Notes embedded (the index being built), the model turned on or off: the results follow.
        const auto follow = [this] {
            if (!m_query.trimmed().isEmpty())
                markContentChanged();
        };
        m_semanticContent = connect(semantic, &shell::SemanticSearch::contentChanged, this, follow);
        m_semanticState = connect(semantic, &shell::SemanticSearch::stateChanged, this, follow);
        // The query's vector: the list sorted by meaning at once.
        m_semanticQuery = connect(semantic, &shell::SemanticSearch::queryReady, this, [this](const QString &text) {
            if (text == attachments::parseSearchFilters(m_query).text.simplified())
                scheduleRefresh(0);
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
        // By meaning too (Workspace.semantic): the hits fused with the notes closest in meaning, a
        // few notes without the words among them. Its vector a moment late: the list waits for it
        // (MeaningWaitMs), else shows the words' order and is sorted again when it comes.
        shell::SemanticSearch *semantic = workspace()->semantic();
        if (semantic && semantic->active() && (filters.scopes & attachments::NoteScopes) && m_scope != u"attachments") {
            const QString key = text.simplified();
            const std::optional<semantic::Vector> vector = semantic->queryVector(key);
            if (!vector && m_meaningWait != key) {
                m_meaningWait = key;
                m_meaningTimer.start();
                return; // (the rows stay as they are a moment)
            }
            if (!vector && m_meaningTimer.isActive())
                return;
            if (vector) {
                m_meaningTimer.stop();
                QList<semantic::ScoredId> lexical;
                lexical.reserve(found.size());
                QHash<QString, qsizetype> byPath;
                for (qsizetype i = 0; i < found.size(); ++i) {
                    lexical.append({found.at(i).path, found.at(i).score});
                    byPath.insert(found.at(i).path, i);
                }
                const semantic::VectorIndex &vectors = semantic->index();
                const QList<semantic::HybridHit> fused = semantic::hybridSearch(
                        lexical, vectors, *vector, vectors.calibration().appliesTo(key), want);
                QList<Hit> sorted;
                sorted.reserve(fused.size());
                for (const semantic::HybridHit &h : fused) {
                    if (const auto it = byPath.constFind(h.id); it != byPath.cend()) {
                        Hit row = found.at(it.value());
                        row.score = h.score;
                        sorted.append(std::move(row));
                        continue;
                    }
                    // Found by meaning alone: where its closest chunk starts.
                    const ParsedNote *note = idx->note(h.id);
                    if (!note)
                        continue;
                    const bool journal = note->journalDate.has_value();
                    if (!filters.scopes.testFlag(journal ? Scope::Journals : Scope::Pages))
                        continue;
                    Hit row;
                    row.path = h.id;
                    row.title = note->title.isEmpty() ? paths::pageTitle(h.id) : note->title;
                    row.folder = paths::dirname(h.id);
                    row.line = h.line;
                    row.excerpt = oneLine(plainText(semantic::passageExcerpt(idx->content(h.id), h.line, 400), idx), 260);
                    row.journalDate = journal ? isoDate(*note->journalDate) : QString();
                    row.score = h.score;
                    row.byMeaning = true;
                    sorted.append(std::move(row));
                }
                found = std::move(sorted);
            }
        }
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
    case ByMeaningRole: return h.byMeaning;
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
                                              {FindRole, "find"},
                                              {ByMeaningRole, "byMeaning"}};
    return names;
}

QVariantMap SearchResultModel::get(int row) const
{
    return rowMap(row);
}

} // namespace brulion::content
