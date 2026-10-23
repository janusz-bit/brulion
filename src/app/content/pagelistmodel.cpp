#include "pagelistmodel.h"

#include "contentutil.h"
#include "index/vaultindex.h"
#include "util/paths.h"
#include "vault/vault.h"
#include "workspace.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace brulion::content {

using namespace Qt::StringLiterals;

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

QDate PageListModel::effectiveToday() const
{
    const QDate fixed = parseIsoDate(m_today);
    if (fixed.isValid())
        return fixed;
    return workspace() ? workspace()->now().date() : QDate::currentDate();
}

void PageListModel::refresh()
{
    QList<Page> next;
    int total = 0;
    shell::Workspace *ws = workspace();
    const VaultIndex *idx = vaultIndex();
    if (ws && idx && ws->isOpen() && ws->vault()) {
        QHash<QString, FileStat> stats;
        for (const FileStat &f : ws->vault()->markdownFiles())
            stats.insert(f.path, f);
        QHash<QString, QString> tagDisplay;
        for (const TagCount &t : idx->tags())
            tagDisplay.insert(t.tag, t.display.isEmpty() ? t.tag : t.display);
        const QList<VaultPath> all = idx->paths();

        // Backlinks: distinct other pages linking to each page or carrying a tag that names it
        // (docs/SYNTAX.md §3), one walk over all links and tags.
        QHash<QString, int> backlinks;
        QHash<QString, QString> tagTargets; // tag -> page it names ("" when none)
        for (const VaultPath &source : all) {
            QSet<QString> targets;
            for (const ResolvedLink &l : idx->outgoingLinks(source)) {
                if (!l.target.isEmpty() && l.target != source)
                    targets.insert(l.target);
            }
            if (const ParsedNote *note = idx->note(source)) {
                for (const QString &tag : note->tags) {
                    auto it = tagTargets.find(tag);
                    if (it == tagTargets.end())
                        it = tagTargets.insert(tag, idx->resolveTag(tag).value_or(QString()));
                    if (!it->isEmpty() && *it != source)
                        targets.insert(*it);
                }
            }
            for (const QString &t : std::as_const(targets))
                backlinks[t] += 1;
        }

        const QStringList terms = searchTerms(m_filter);
        for (const VaultPath &path : all) {
            const ParsedNote *note = idx->note(path);
            if (!note)
                continue;
            const bool journal = note->journalDate.has_value();
            if (journal && !m_includeJournals)
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
            for (const int pos : matchPositions(p.title, terms, false))
                p.titleMatches.append(pos);
            next.append(p);
        }

        const auto byTitle = [](const Page &a, const Page &b) {
            const int c = compareNames(a.title, b.title);
            return c != 0 ? c < 0 : a.path < b.path;
        };
        const bool desc = m_descending;
        const QString key = m_sortKey;
        std::stable_sort(next.begin(), next.end(), [&](const Page &a, const Page &b) {
            int c = 0;
            if (key == u"folder")
                c = compareNames(a.folder, b.folder);
            else if (key == u"modified")
                c = a.modified < b.modified ? -1 : a.modified > b.modified ? 1 : 0;
            else if (key == u"backlinks")
                c = a.backlinks - b.backlinks;
            else if (key == u"tags")
                c = int(a.tags.size()) - int(b.tags.size());
            if (c != 0)
                return desc ? c > 0 : c < 0;
            // Ties (and the title sort itself) by title; reversed only when sorting by title.
            return key == u"title" && desc ? byTitle(b, a) : byTitle(a, b);
        });
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
    case ModifiedLabelRole: return humanTime(p.modified, effectiveToday(), uiLocale(m_locale));
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
    return { { PathRole, "path" },           { TitleRole, "title" },       { FolderRole, "folder" },
             { AliasesRole, "aliases" },     { ModifiedRole, "modified" }, { ModifiedLabelRole, "modifiedLabel" },
             { SizeRole, "size" },           { BacklinksRole, "backlinks" }, { TagsRole, "tags" },
             { TagCountRole, "tagCount" },   { JournalDateRole, "journalDate" }, { TitleMatchesRole, "titleMatches" } };
}

QVariantMap PageListModel::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_rows.size())
        return out;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        out.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return out;
}

int PageListModel::rowOf(const QString &path) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).path == path)
            return i;
    }
    return -1;
}

} // namespace brulion::content
