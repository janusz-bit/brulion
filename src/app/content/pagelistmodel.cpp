#include "pagelistmodel.h"

#include "contentutil.h"
#include "index/vaultindex.h"
#include "util/paths.h"
#include "vault/vault.h"
#include "workspace.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <compare>

namespace brulion::content {

using namespace Qt::StringLiterals;

namespace {

/// Backlinks: distinct other pages linking to each page or carrying a tag that names it
/// (docs/SYNTAX.md §3), one walk over all links and tags.
QHash<QString, int> backlinkCounts(const VaultIndex &idx, const QList<VaultPath> &all)
{
    QHash<QString, int> backlinks;
    QHash<QString, QString> tagTargets; // tag -> page it names ("" when none)
    for (const VaultPath &source : all) {
        QSet<QString> targets;
        const QList<ResolvedLink> links = idx.outgoingLinks(source);
        for (const ResolvedLink &l : links) {
            if (!l.target.isEmpty() && l.target != source)
                targets.insert(l.target);
        }
        if (const ParsedNote *note = idx.note(source)) {
            for (const QString &tag : note->tags) {
                auto it = tagTargets.find(tag);
                if (it == tagTargets.end())
                    it = tagTargets.insert(tag, idx.resolveTag(tag).value_or(QString()));
                if (!it->isEmpty() && *it != source)
                    targets.insert(*it);
            }
        }
        for (const QString &t : std::as_const(targets))
            ++backlinks[t];
    }
    return backlinks;
}

} // namespace

PageListModel::PageListModel(QObject *parent) : WorkspaceListModel(parent)
{
    // Rebuilding walks every note: wait for a burst of changes (sync, indexing) to settle.
    setRefreshDelay(400);
}

void PageListModel::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    Q_EMIT filterChanged();
    scheduleRefresh(0);
}

void PageListModel::setFolder(const QString &folder)
{
    const QString f = paths::normalize(folder);
    if (m_folder == f)
        return;
    m_folder = f;
    Q_EMIT folderChanged();
    m_resetNext = true;
    scheduleRefresh(0);
}

void PageListModel::setSortKey(const QString &key)
{
    static const QStringList keys{ u"title"_s, u"folder"_s, u"modified"_s, u"backlinks"_s, u"tags"_s };
    const QString k = keys.contains(key) ? key : u"title"_s;
    if (m_sortKey == k)
        return;
    m_sortKey = k;
    m_resetNext = true;
    Q_EMIT sortChanged();
    scheduleRefresh(0);
}

void PageListModel::setDescending(bool on)
{
    if (m_descending == on)
        return;
    m_descending = on;
    m_resetNext = true;
    Q_EMIT sortChanged();
    scheduleRefresh(0);
}

void PageListModel::setIncludeJournals(bool on)
{
    if (m_includeJournals == on)
        return;
    m_includeJournals = on;
    Q_EMIT includeJournalsChanged();
    scheduleRefresh(0);
}

void PageListModel::setRecursive(bool on)
{
    if (m_recursive == on)
        return;
    m_recursive = on;
    Q_EMIT recursiveChanged();
    scheduleRefresh(0);
}

void PageListModel::setLocale(const QString &locale)
{
    if (m_locale == locale)
        return;
    m_locale = locale;
    Q_EMIT localeChanged();
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), { ModifiedLabelRole });
}

void PageListModel::setToday(const QString &today)
{
    if (m_today == today)
        return;
    m_today = today;
    Q_EMIT todayChanged();
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), { ModifiedLabelRole });
}

void PageListModel::refresh()
{
    QList<Page> next;
    int total = 0;
    bool journalsListed = false;
    shell::Workspace *ws = workspace();
    const VaultIndex *idx = vaultIndex();
    if (ws && idx && ws->isOpen() && ws->vault()) {
        // Listing the journals folder (or a year inside it): its daily notes are its pages.
        const QString journalsFolder = paths::normalize(idx->config().journalsFolder);
        journalsListed = !m_folder.isEmpty() && !journalsFolder.isEmpty()
                         && (m_folder.compare(journalsFolder, Qt::CaseInsensitive) == 0
                             || m_folder.startsWith(journalsFolder + u'/', Qt::CaseInsensitive));
        const bool withJournals = m_includeJournals || journalsListed;
        QHash<QString, FileStat> stats;
        const QList<FileStat> files = ws->vault()->markdownFiles();
        stats.reserve(files.size());
        for (const FileStat &f : files)
            stats.insert(f.path, f);
        QHash<QString, QString> tagDisplay;
        const QList<TagCount> tags = idx->tags();
        for (const TagCount &t : tags)
            tagDisplay.insert(t.tag, t.display.isEmpty() ? t.tag : t.display);
        const QList<VaultPath> all = idx->paths();
        const QHash<QString, int> backlinks = backlinkCounts(*idx, all);

        const QStringList terms = searchTerms(m_filter);
        for (const VaultPath &path : all) {
            const ParsedNote *note = idx->note(path);
            if (!note)
                continue;
            const bool journal = note->journalDate.has_value();
            if (journal && !withJournals)
                continue;
            const QString folder = paths::dirname(path);
            if (!m_recursive && folder != m_folder)
                continue;
            if (!m_folder.isEmpty() && folder != m_folder && !folder.startsWith(m_folder + u'/'))
                continue;
            ++total;
            if (!terms.isEmpty()
                && !containsAll(note->title + u' ' + note->aliases.join(u' ') + u' ' + folder, terms))
                continue;
            Page p;
            p.path = path;
            p.title = note->title;
            p.folder = folder;
            p.aliases = note->aliases;
            const FileStat st = stats.value(path);
            p.modified = st.mtime;
            p.size = st.size;
            p.backlinks = backlinks.value(path);
            for (const QString &t : note->tags)
                p.tags.append(tagDisplay.value(t, t));
            if (journal)
                p.journalDate = isoDate(*note->journalDate);
            const QList<int> positions = matchPositions(p.title, terms, false);
            for (const int pos : positions)
                p.titleMatches.append(pos);
            next.append(std::move(p));
        }
        sortPages(next);
    }

    if (m_resetNext) {
        m_resetNext = false;
        beginResetModel();
        m_rows = next;
        endResetModel();
    } else {
        applyRows(m_rows, next, [](const Page &p) { return p.path; }, [](const Page &a, const Page &b) { return a == b; });
    }
    if (total != m_total) {
        m_total = total;
        Q_EMIT totalCountChanged();
    }
    if (journalsListed != m_journalsFolderListed) {
        m_journalsFolderListed = journalsListed;
        Q_EMIT journalsFolderListedChanged();
    }
}

void PageListModel::sortPages(QList<Page> &pages) const
{
    const auto byTitle = [](const Page &a, const Page &b) {
        // Daily notes in date order, whatever their file name format ("16.05.2026").
        const int c = !a.journalDate.isEmpty() && !b.journalDate.isEmpty() ? a.journalDate.compare(b.journalDate)
                                                                           : compareNames(a.title, b.title);
        return c != 0 ? c < 0 : a.path < b.path;
    };
    const auto byKey = [this](const Page &a, const Page &b) -> std::strong_ordering {
        if (m_sortKey == u"folder")
            return compareNames(a.folder, b.folder) <=> 0;
        if (m_sortKey == u"modified")
            return a.modified <=> b.modified;
        if (m_sortKey == u"backlinks")
            return a.backlinks <=> b.backlinks;
        if (m_sortKey == u"tags")
            return a.tags.size() <=> b.tags.size();
        return std::strong_ordering::equal;
    };
    const bool reverseTitle = m_descending && m_sortKey == u"title";
    std::ranges::stable_sort(pages, [&](const Page &a, const Page &b) {
        if (const auto c = byKey(a, b); c != 0)
            return m_descending ? c > 0 : c < 0;
        // Ties (and the title sort itself) by title; reversed only when sorting by title.
        return reverseTitle ? byTitle(b, a) : byTitle(a, b);
    });
}

QVariant PageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Page &p = m_rows.at(index.row());
    switch (role) {
    case PathRole: return p.path;
    case TitleRole: return p.title;
    case FolderRole: return p.folder;
    case AliasesRole: return p.aliases;
    case ModifiedRole: return p.modified;
    case ModifiedLabelRole: return humanTime(p.modified, effectiveToday(m_today, workspace()), uiLocale(m_locale));
    case SizeRole: return p.size;
    case BacklinksRole: return p.backlinks;
    case TagsRole: return p.tags;
    case TagCountRole: return int(p.tags.size());
    case JournalDateRole: return p.journalDate;
    case TitleMatchesRole: return p.titleMatches;
    }
    return {};
}

QHash<int, QByteArray> PageListModel::roleNames() const
{
    static const QHash<int, QByteArray> names{{PathRole, "path"},
                                              {TitleRole, "title"},
                                              {FolderRole, "folder"},
                                              {AliasesRole, "aliases"},
                                              {ModifiedRole, "modified"},
                                              {ModifiedLabelRole, "modifiedLabel"},
                                              {SizeRole, "size"},
                                              {BacklinksRole, "backlinks"},
                                              {TagsRole, "tags"},
                                              {TagCountRole, "tagCount"},
                                              {JournalDateRole, "journalDate"},
                                              {TitleMatchesRole, "titleMatches"}};
    return names;
}

QVariantMap PageListModel::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_rows.size())
        return out;
    const QHash<int, QByteArray> names = roleNames();
    for (const auto &[role, name] : names.asKeyValueRange())
        out.insert(QString::fromLatin1(name), data(index(row), role));
    return out;
}

int PageListModel::rowOf(const QString &path) const
{
    return rowIndex(m_rows, std::ranges::find(m_rows, path, &Page::path));
}

} // namespace brulion::content
