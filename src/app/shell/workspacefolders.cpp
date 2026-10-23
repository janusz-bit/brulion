// Workspace: folders, renames and moves (docs/SYNTAX.md §1 "Folders", §2 "Rewriting on rename
// and move"). Every rename and move goes through relocate(): the index plans which notes to
// rewrite so that each link and tag keeps naming the file it named (VaultIndex::planRelocation()),
// the vault moves the files (history, watches and the per-file Renamed changes that move
// favourites, recent pages, fold state and routes along), the planned notes are written (one
// history step each), and settings naming a moved special folder follow it.
#include "workspace.h"

#include "index/vaultindex.h"
#include "markdown/markdown.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

/// A page title typed with its extension ("Notatka.md") names the page "Notatka".
QString withoutMd(const QString &title)
{
    const QString t = title.trimmed();
    if (t.size() > 3 && t.endsWith(u".md"_s, Qt::CaseInsensitive) && t.at(t.size() - 4) != u'/')
        return t.chopped(3);
    return t;
}

/// Is `inner` `outer` itself or inside it?
bool isWithin(const QString &inner, const QString &outer)
{
    return !outer.isEmpty() && (inner == outer || inner.startsWith(outer + u'/'));
}

/// A folder name the vault would hide: a leading '.' or an ignored folder (logseq, .git, …).
bool hiddenName(const QString &segment)
{
    return segment.startsWith(u'.') || paths::isIgnored(segment);
}

/// A folder name typed in a rename that the vault would hide (a page's own name is sanitised
/// instead), else "". With `slashMoves` a "/" moves: every segment before the last names a folder;
/// without it the whole name is one (its "/" sanitised).
QString hiddenTypedSegment(const QString &newName, bool isDir, bool slashMoves)
{
    QString unified = QString(newName).replace(u'\\', u'/');
    const QStringList segments =
            slashMoves ? unified.split(u'/', Qt::SkipEmptyParts) : QStringList{unified.replace(u'/', u' ')};
    for (qsizetype i = 0; i < segments.size(); ++i) {
        const QString segment = segments.at(i).trimmed();
        if ((isDir || i + 1 < segments.size()) && hiddenName(segment))
            return segment;
    }
    return {};
}

/// The settings of the notebook that name folders, with their keys.
struct SpecialFolder {
    const char *key;
    const char *special; ///< folderInfo()'s name
};
constexpr SpecialFolder kSpecialFolders[] = {
        {"journalsFolder", "journals"},
        {"templatesFolder", "templates"},
        {"attachmentsFolder", "attachments"},
        {"newPagesFolder", "newPages"},
};

} // namespace

int Workspace::pageCount() const
{
    return m_vault ? int(m_vault->markdownFiles().size()) : 0;
}

int Workspace::pagesWithoutJournals() const
{
    // The index's notes are the pages without conflict copies; its lists are cached until the
    // next change (this is read on every revision).
    if (!m_index)
        return 0;
    return int(m_index->paths().size() - m_index->journals().size());
}

QString Workspace::linkTarget(const QString &path, const QString &fromPath) const
{
    if (!m_index)
        return {};
    return m_index->linkTarget(path, fromPath).value_or(QString());
}

// ---- Folders --------------------------------------------------------------------------------

QString Workspace::createFolder(const QString &rawPath)
{
    if (!m_vault)
        return {};
    QStringList segments;
    const QString unified = QString(rawPath).replace(u'\\', u'/');
    for (const QString &raw : unified.split(u'/', Qt::SkipEmptyParts)) {
        const QString segment = raw.trimmed().normalized(QString::NormalizationForm_C);
        if (segment.isEmpty() || segment == u"." || segment == u"..")
            continue;
        if (hiddenName(segment)) {
            Q_EMIT errorOccurred(tr("A folder named “%1” would be hidden — choose another name.").arg(segment));
            return {};
        }
        segments.append(paths::sanitizeFileName(segment));
    }
    if (segments.isEmpty())
        return {};
    // Folders as the vault spells them ("projekty/Nowy" goes into "Projekty/").
    const QString folder = m_vault->existingCase(segments.join(u'/'));
    if (const auto existing = m_vault->stat(folder); existing && !existing->isDir) {
        Q_EMIT errorOccurred(tr("“%1” is a file, not a folder.").arg(paths::basename(folder)));
        return {};
    }
    if (!m_vault->createFolder(folder)) {
        Q_EMIT errorOccurred(tr("Could not create the folder: %1").arg(m_vault->errorString()));
        return {};
    }
    notifyFolderChange({folder});
    return folder;
}

QStringList Workspace::folderPaths() const
{
    QStringList out;
    if (!m_vault)
        return out;
    for (const FileStat &f : m_vault->folders()) {
        if (!paths::isIgnored(f.path))
            out.append(f.path);
    }
    // Polish order segment by segment: "Projekty" before "Projekty 2" and "Projekty/Dom".
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
        const QStringList sa = a.split(u'/');
        const QStringList sb = b.split(u'/');
        for (qsizetype i = 0; i < std::min(sa.size(), sb.size()); ++i) {
            if (const int c = text::compareNames(sa.at(i), sb.at(i)); c != 0)
                return c < 0;
        }
        return sa.size() < sb.size();
    });
    return out;
}

QVariantMap Workspace::folderInfo(const QString &rawPath) const
{
    const QString path = paths::normalize(rawPath);
    int pages = 0;
    int attachments = 0;
    int folders = 0;
    QString special;
    QString specialPath;
    bool exists = false;
    if (m_vault && !path.isEmpty()) {
        const auto stat = m_vault->stat(path);
        exists = stat && stat->isDir;
        for (const FileStat &f : m_vault->files()) {
            if (!f.path.startsWith(path + u'/'))
                continue;
            if (!paths::isMarkdown(f.path))
                ++attachments;
            else if (!paths::isConflictCopy(f.path))
                ++pages;
        }
        for (const FileStat &f : m_vault->folders()) {
            if (f.path.startsWith(path + u'/'))
                ++folders;
        }
        const QVariantMap config = vaultConfig();
        for (const SpecialFolder &s : kSpecialFolders) {
            const QString value = paths::normalize(config.value(QString::fromLatin1(s.key)).toString());
            if (special.isEmpty() && isWithin(value, path)) {
                special = QString::fromLatin1(s.special);
                specialPath = value;
            }
        }
    }
    return {{u"exists"_s, exists},   {u"pages"_s, pages},     {u"attachments"_s, attachments},
            {u"folders"_s, folders}, {u"special"_s, special}, {u"specialPath"_s, specialPath}};
}

QVariantList Workspace::subfolders(const QString &rawFolder) const
{
    QVariantList out;
    if (!m_vault)
        return out;
    const QString folder = paths::normalize(rawFolder);
    const QString prefix = folder.isEmpty() ? QString() : folder + u'/';
    QHash<QString, int> pages;     // direct subfolder -> pages at any depth
    QSet<QString> withAttachments; // direct subfolders holding attachments
    const auto childOf = [&](const QString &path) -> QString {
        if (!path.startsWith(prefix))
            return {};
        const qsizetype slash = path.indexOf(u'/', prefix.size());
        return slash < 0 ? QString() : path.left(slash);
    };
    for (const FileStat &f : m_vault->files()) {
        const QString child = childOf(f.path);
        if (child.isEmpty())
            continue;
        if (!paths::isMarkdown(f.path)) {
            withAttachments.insert(child);
            continue;
        }
        // The index's notes: no conflict copies; daily notes left out (cheap lookups, no parsing).
        const ParsedNote *note = m_index ? m_index->note(f.path) : nullptr;
        if (note && !note->journalDate)
            pages[child] += 1;
    }
    QStringList children;
    for (const FileStat &f : m_vault->folders()) {
        if (f.path.startsWith(prefix) && !f.path.mid(prefix.size()).contains(u'/') && !paths::isIgnored(f.path)
            && (pages.contains(f.path) || !withAttachments.contains(f.path)))
            children.append(f.path);
    }
    std::sort(children.begin(), children.end(), [](const QString &a, const QString &b) {
        return text::compareNames(paths::basename(a), paths::basename(b)) < 0;
    });
    for (const QString &c : std::as_const(children))
        out.append(QVariantMap{{u"path"_s, c}, {u"name"_s, paths::basename(c)}, {u"pages"_s, pages.value(c)}});
    return out;
}

void Workspace::notifyFolderChange(const QStringList &changed)
{
    ++m_revision;
    Q_EMIT revisionChanged();
    m_trashCount = -1;
    Q_EMIT trashChanged();
    Q_EMIT filesChanged(changed);
}

// ---- Renames and moves ----------------------------------------------------------------------

QString Workspace::spelledFolders(const QString &path) const
{
    if (!m_vault || !path.contains(u'/'))
        return path;
    const QString folder = m_vault->existingCase(paths::dirname(path));
    return paths::join(folder, paths::basename(path));
}

QString Workspace::renamedPath(const QString &rawPath, const QString &newName, bool isDir, bool slashMoves) const
{
    const QString path = paths::normalize(rawPath);
    if (path.isEmpty())
        return {};
    const bool note = !isDir && paths::isMarkdown(path);
    const QString ext = isDir ? QString() : (paths::extension(path).isEmpty() ? u".md"_s : paths::extension(path));
    // Typed with the extension ("Notatka.md"): one extension.
    const QString typed = note ? withoutMd(newName) : newName.trimmed();
    // A title: its "/" is sanitised like the other characters a file name cannot have ("TCP/IP"
    // -> "TCP IP"); in the Rename dialog it separates folders.
    const bool moves = slashMoves && typed.contains(u'/');
    QStringList parts;
    for (const QString &part :
         moves ? QString(typed).replace(u'\\', u'/').split(u'/', Qt::SkipEmptyParts) : QStringList{typed}) {
        if (!part.trimmed().isEmpty())
            parts.append(paths::sanitizeFileName(part.trimmed()));
    }
    if (parts.isEmpty())
        return {};
    QString name = parts.takeLast();
    // An attachment keeps its type ("plan" -> "plan.png").
    if (!isDir && !note && !ext.isEmpty() && !name.endsWith(ext, Qt::CaseInsensitive))
        name += ext;
    else if (note)
        name += ext;
    // "/" in the name: a path from the notebook root; else the item stays in its folder.
    const QString folder = moves ? parts.join(u'/') : paths::dirname(path);
    return folder.isEmpty() ? name : folder + u'/' + name;
}

QString Workspace::relocationError(const QString &from, const QString &to) const
{
    if (!m_vault)
        return tr("No notebook is open.");
    const auto source = m_vault->stat(from);
    if (from.isEmpty() || !source)
        return tr("“%1” no longer exists.").arg(paths::pageTitle(from));
    if (to.isEmpty())
        return tr("Enter a name.");
    if (isWithin(to, from) && to != from)
        return tr("A folder cannot be moved into itself.");
    // Never into a folder the notebook ignores (.git, logseq, .trash, …); new names the vault
    // would hide are refused (folders that exist already, like a scanned ".notatki", are fine).
    if (paths::isIgnored(to))
        return tr("“%1” would be hidden — choose another name.").arg(paths::basename(to));
    const QStringList segments = to.split(u'/');
    for (qsizetype i = 0; i < segments.size(); ++i) {
        const QString prefix = segments.mid(0, i + 1).join(u'/');
        const bool last = i + 1 == segments.size();
        const bool exists = !last && m_vault->stat(m_vault->existingCase(prefix)).has_value();
        const bool renamed = last && segments.at(i) != paths::basename(from);
        if ((renamed || (!last && !exists)) && hiddenName(segments.at(i)))
            return tr("A folder named “%1” would be hidden — choose another name.").arg(segments.at(i));
    }
    // A page into itself as a folder ("Dom.md" into "Dom.md/") is no folder.
    for (QString parent = paths::dirname(to); !parent.isEmpty(); parent = paths::dirname(parent)) {
        const auto st = m_vault->stat(m_vault->existingCase(parent));
        if (st && !st->isDir)
            return tr("“%1” is a file, not a folder.").arg(paths::basename(parent));
    }
    const bool caseOnly = from.compare(to, Qt::CaseInsensitive) == 0;
    if (!caseOnly) {
        // The name taken, also in another letter case ("dom" next to "Dom": one file on
        // case-insensitive storage, and links do not tell them apart).
        const QString spelled = m_vault->existingCase(to);
        if (m_vault->exists(to) || m_vault->exists(spelled)) {
            const QString name = paths::basename(m_vault->exists(spelled) ? spelled : to);
            if (source->isDir)
                return tr("A folder named “%1” already exists here.").arg(name);
            if (paths::isMarkdown(from))
                return tr("A page named “%1” already exists here.").arg(paths::pageTitle(name));
            return tr("A file named “%1” already exists here.").arg(name);
        }
        // Another page whose path differs only in case (the index knows every note).
        if (m_index && paths::isMarkdown(to)) {
            for (const VaultPath &other : m_index->paths()) {
                if (other.compare(to, Qt::CaseInsensitive) == 0 && other.compare(from, Qt::CaseInsensitive) != 0)
                    return tr("A page named “%1” already exists here.").arg(paths::pageTitle(other));
            }
        }
    }
    return {};
}

QList<FileMove> Workspace::fileMoves(const QString &from, const QString &to) const
{
    QList<FileMove> moves;
    if (!m_vault)
        return moves;
    const auto stat = m_vault->stat(from);
    if (stat && stat->isDir) {
        const QString prefix = from + u'/';
        for (const FileStat &f : m_vault->files()) {
            if (f.path.startsWith(prefix))
                moves.append({f.path, to + u'/' + f.path.mid(prefix.size())});
        }
    } else {
        moves.append({from, to});
    }
    return moves;
}

QVariantMap Workspace::relocationImpact(const QString &rawFrom, const QString &rawTo)
{
    const QString from = paths::normalize(rawFrom);
    const QString to = spelledFolders(paths::normalize(rawTo));
    QVariantMap out{{u"ok"_s, false}, {u"path"_s, to}, {u"links"_s, 0},
                    {u"tags"_s, 0},   {u"notes"_s, 0}, {u"special"_s, QString()}};
    if (from == to && !from.isEmpty() && m_vault && m_vault->exists(from)) {
        out.insert(u"ok"_s, true);
        out.insert(u"same"_s, true);
        return out;
    }
    const QString error = relocationError(from, to);
    if (!error.isEmpty()) {
        out.insert(u"error"_s, error);
        return out;
    }
    out.insert(u"ok"_s, true);
    if (const auto stat = m_vault->stat(from); stat && stat->isDir)
        out.insert(u"special"_s, folderInfo(from).value(u"special"_s));
    if (!m_index)
        return out;
    finishIndexing(); // every linking note must be known
    const RelocationPlan plan = m_index->planRelocation(fileMoves(from, to));
    out.insert(u"links"_s, plan.links());
    out.insert(u"tags"_s, plan.tags());
    out.insert(u"notes"_s, int(plan.files().size()));
    return out;
}

QVariantMap Workspace::moveImpact(const QString &path, const QString &targetFolder)
{
    const QString from = paths::normalize(path);
    const QString folder = paths::normalize(targetFolder);
    const QString name = paths::basename(from);
    return relocationImpact(from, folder.isEmpty() ? name : folder + u'/' + name);
}

QVariantMap Workspace::renameImpact(const QString &path, const QString &newName, bool isDir, bool slashMoves)
{
    if (const QString hidden = hiddenTypedSegment(newName, isDir, slashMoves); !hidden.isEmpty())
        return {{u"ok"_s, false},
                {u"path"_s, QString()},
                {u"error"_s, tr("A folder named “%1” would be hidden — choose another name.").arg(hidden)}};
    const QString to = renamedPath(path, newName, isDir, slashMoves);
    if (to.isEmpty())
        return {{u"ok"_s, false}, {u"path"_s, QString()}, {u"error"_s, tr("Enter a name.")}};
    return relocationImpact(path, to);
}

QString Workspace::relocate(const QString &rawFrom, const QString &rawTo)
{
    if (!m_vault)
        return {};
    const QString from = paths::normalize(rawFrom);
    const QString to = spelledFolders(paths::normalize(rawTo));
    if (!from.isEmpty() && from == to && m_vault->exists(from))
        return from;
    if (const QString error = relocationError(from, to); !error.isEmpty()) {
        Q_EMIT errorOccurred(error);
        return {};
    }
    const bool folder = m_vault->stat(from)->isDir;
    // Every note that links to a moved file must be indexed before the plan asks (right after
    // opening the index is still being built in the background).
    finishIndexing();
    const RelocationPlan plan = m_index ? m_index->planRelocation(fileMoves(from, to)) : RelocationPlan();
    if (!m_vault->rename(from, to, ChangeSource::User)) {
        const bool moving = paths::dirname(from) != paths::dirname(to);
        const QString message =
                folder ? (moving ? tr("Could not move the folder: %1") : tr("Could not rename the folder: %1"))
                       : (moving ? tr("Could not move the page: %1") : tr("Could not rename the page: %1"));
        Q_EMIT errorOccurred(message.arg(m_vault->errorString()));
        return {};
    }
    // The planned notes, where they are now (also a moved note itself); one write — one history
    // step — each. A note saved since the plan was made gets the plan applied to its text.
    for (const RelocationPlan::File &f : plan.files()) {
        bool ok = false;
        const QString content = m_vault->readText(f.newPath, &ok);
        if (!ok)
            continue;
        const QString rewritten = plan.rewrite(f.path, content);
        if (rewritten != content)
            m_vault->writeText(f.newPath, rewritten, ChangeSource::User);
    }
    rebaseConfig(from, to);
    if (folder) {
        notifyFolderChange({from, to});
        Q_EMIT folderRenamed(from, to);
    }
    return to;
}

void Workspace::rebaseConfig(const QString &from, const QString &to)
{
    const QVariantMap config = vaultConfig();
    QVariantMap changed;
    QStringList said;
    const auto rebase = [&](const char *key, const QString &label) {
        const QString k = QString::fromLatin1(key);
        const QString value = paths::normalize(config.value(k).toString());
        if (!isWithin(value, from))
            return;
        const QString moved = to + value.mid(from.size());
        changed.insert(k, moved);
        said.append(label.arg(moved));
    };
    rebase("journalsFolder", tr("journals folder → “%1”"));
    rebase("templatesFolder", tr("templates folder → “%1”"));
    rebase("attachmentsFolder", tr("attachments folder → “%1”"));
    rebase("newPagesFolder", tr("folder for new pages → “%1”"));
    rebase("journalTemplate", tr("journal template → “%1”"));
    if (changed.isEmpty() || !setConfig(changed))
        return;
    Q_EMIT noticeOccurred(tr("Notebook settings updated: %1").arg(said.join(u", "_s)));
}

QString Workspace::renamePage(const QString &rawPath, const QString &newTitle, bool slashMoves)
{
    if (!m_vault)
        return {};
    const QString path = paths::normalize(rawPath);
    if (!m_vault->exists(path))
        return {};
    if (const QString hidden = hiddenTypedSegment(newTitle, false, slashMoves); !hidden.isEmpty()) {
        Q_EMIT errorOccurred(tr("A folder named “%1” would be hidden — choose another name.").arg(hidden));
        return {};
    }
    const QString to = renamedPath(path, newTitle, false, slashMoves);
    if (to.isEmpty())
        return {};
    return relocate(path, to);
}

QString Workspace::renameFolder(const QString &rawPath, const QString &newName, bool slashMoves)
{
    if (!m_vault)
        return {};
    const QString path = paths::normalize(rawPath);
    if (path.isEmpty() || newName.trimmed().isEmpty())
        return {};
    // A name the vault would hide is refused (as for a new folder), before it is sanitised.
    if (const QString hidden = hiddenTypedSegment(newName, true, slashMoves); !hidden.isEmpty()) {
        Q_EMIT errorOccurred(tr("A folder named “%1” would be hidden — choose another name.").arg(hidden));
        return {};
    }
    const QString to = renamedPath(path, newName, true, slashMoves);
    if (to.isEmpty())
        return {};
    return relocate(path, to);
}

QString Workspace::movePath(const QString &rawPath, const QString &targetFolder)
{
    if (!m_vault)
        return {};
    const QString path = paths::normalize(rawPath);
    const QString folder = paths::normalize(targetFolder);
    if (path.isEmpty() || !m_vault->exists(path))
        return {};
    if (!folder.isEmpty()) {
        if (const auto st = m_vault->stat(m_vault->existingCase(folder)); st && !st->isDir) {
            Q_EMIT errorOccurred(tr("“%1” is a file, not a folder.").arg(paths::basename(folder)));
            return {};
        }
    }
    const QString name = paths::basename(path);
    return relocate(path, folder.isEmpty() ? name : folder + u'/' + name);
}

} // namespace brulion::shell
