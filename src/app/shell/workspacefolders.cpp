// Workspace: folders, renames and moves (docs/SYNTAX.md §1 "Folders", §2 "Rewriting on rename
// and move"). Every rename and move goes through relocate(), i.e. notebook::relocate() (the same
// code as `brulion-cli page move`): the index plans which notes to rewrite so that each link and
// tag keeps naming the file it named (VaultIndex::planRelocation()), the vault moves the files
// (history, watches and the per-file Renamed changes that move favourites, recent pages, fold
// state and routes along), the planned notes are written (one history step each), and settings
// naming a moved special folder follow it. Here: the translated messages, the notices and the
// signals of the views.
#include "workspace.h"

#include "index/vaultindex.h"
#include "markdown/markdown.h"
#include "notebook/pages.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

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
    const notebook::CreatedFolder created = notebook::createFolder(*m_vault, rawPath);
    if (created.path.isEmpty()) {
        if (created.problem.kind != notebook::PathProblem::NoName)
            Q_EMIT errorOccurred(pathProblemMessage(created.problem));
        return {};
    }
    notifyFolderChange({created.path});
    return created.path;
}

QStringList Workspace::folderPaths() const
{
    return m_vault ? notebook::folderPaths(*m_vault) : QStringList();
}

QVariantMap Workspace::folderInfo(const QString &rawPath) const
{
    notebook::FolderInfo info;
    if (m_vault)
        info = notebook::folderInfo(*m_vault, rawPath);
    QVariantList specials;
    for (const notebook::SpecialPath &s : std::as_const(info.specials))
        specials.append(QVariantMap{{u"special"_s, s.special}, {u"path"_s, s.path}});
    return {{u"exists"_s, info.exists},
            {u"pages"_s, info.pages},
            {u"attachments"_s, info.attachments},
            {u"folders"_s, info.folders},
            {u"specials"_s, specials}};
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

QString Workspace::pathProblemMessage(const notebook::PathProblem &problem) const
{
    using P = notebook::PathProblem;
    switch (problem.kind) {
    case P::None: return {};
    case P::Missing: return tr("“%1” no longer exists.").arg(problem.name);
    case P::NoName: return tr("Enter a name.");
    case P::IntoItself: return tr("A folder cannot be moved into itself.");
    case P::HiddenPath: return tr("“%1” would be hidden — choose another name.").arg(problem.name);
    case P::HiddenFolder: return tr("A folder named “%1” would be hidden — choose another name.").arg(problem.name);
    case P::NotAFolder: return tr("“%1” is a file, not a folder.").arg(problem.name);
    case P::FolderExists: return tr("A folder named “%1” already exists here.").arg(problem.name);
    case P::PageExists: return tr("A page named “%1” already exists here.").arg(problem.name);
    case P::FileExists: return tr("A file named “%1” already exists here.").arg(problem.name);
    case P::Failed: return tr("Could not create the folder: %1").arg(problem.detail); // relocate() says its own
    }
    return {};
}

QVariantMap Workspace::relocationImpact(const QString &from, const QString &to)
{
    if (!m_vault)
        return {{u"ok"_s, false}, {u"path"_s, to}, {u"error"_s, tr("No notebook is open.")}};
    finishIndexing(); // every linking note must be known
    const notebook::RelocationImpact impact = notebook::relocationImpact(*m_vault, m_index.get(), from, to);
    QVariantMap out{{u"ok"_s, impact.ok},     {u"path"_s, impact.path},   {u"links"_s, impact.links},
                    {u"tags"_s, impact.tags}, {u"notes"_s, impact.notes}, {u"specials"_s, QVariant(impact.specials)}};
    if (impact.same)
        out.insert(u"same"_s, true);
    if (!impact.ok)
        out.insert(u"error"_s, pathProblemMessage(impact.problem));
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
    if (const QString hidden = notebook::hiddenTypedSegment(newName, isDir, slashMoves); !hidden.isEmpty())
        return {{u"ok"_s, false},
                {u"path"_s, QString()},
                {u"error"_s, tr("A folder named “%1” would be hidden — choose another name.").arg(hidden)}};
    const QString to = notebook::renamedPath(path, newName, isDir, slashMoves);
    if (to.isEmpty())
        return {{u"ok"_s, false}, {u"path"_s, QString()}, {u"error"_s, tr("Enter a name.")}};
    return relocationImpact(path, to);
}

QString Workspace::relocate(const QString &from, const QString &to)
{
    if (!m_vault)
        return {};
    // Every note that links to a moved file must be indexed before the plan asks (right after
    // opening the index is still being built in the background).
    finishIndexing();
    const notebook::Relocation moved = notebook::relocate(*m_vault, m_index.get(), from, to, ChangeSource::User);
    if (moved.path.isEmpty()) {
        if (moved.problem.kind == notebook::PathProblem::Failed) {
            const QString message = moved.folder ? (moved.moving ? tr("Could not move the folder: %1")
                                                                 : tr("Could not rename the folder: %1"))
                                                 : (moved.moving ? tr("Could not move the page: %1")
                                                                 : tr("Could not rename the page: %1"));
            Q_EMIT errorOccurred(message.arg(moved.problem.detail));
        } else {
            Q_EMIT errorOccurred(pathProblemMessage(moved.problem));
        }
        return {};
    }
    // The settings of .brulion/config.json that followed a special folder.
    if (!moved.configError.isEmpty()) {
        Q_EMIT errorOccurred(tr("Could not save the notebook settings: %1").arg(moved.configError));
    } else if (!moved.rebased.isEmpty()) {
        QStringList said;
        for (const notebook::RebasedSetting &r : moved.rebased) {
            if (r.special == u"journals")
                said.append(tr("journals folder → “%1”").arg(r.value));
            else if (r.special == u"templates")
                said.append(tr("templates folder → “%1”").arg(r.value));
            else if (r.special == u"attachments")
                said.append(tr("attachments folder → “%1”").arg(r.value));
            else if (r.special == u"newPages")
                said.append(tr("folder for new pages → “%1”").arg(r.value));
            else
                said.append(tr("journal template → “%1”").arg(r.value));
        }
        Q_EMIT noticeOccurred(tr("Notebook settings updated: %1").arg(said.join(u", "_s)));
    }
    if (moved.folder && moved.path != paths::normalize(from)) {
        notifyFolderChange({paths::normalize(from), moved.path});
        Q_EMIT folderRenamed(paths::normalize(from), moved.path);
    }
    return moved.path;
}

QString Workspace::renamePage(const QString &rawPath, const QString &newTitle, bool slashMoves)
{
    if (!m_vault)
        return {};
    const QString path = paths::normalize(rawPath);
    if (!m_vault->exists(path))
        return {};
    if (const QString hidden = notebook::hiddenTypedSegment(newTitle, false, slashMoves); !hidden.isEmpty()) {
        Q_EMIT errorOccurred(tr("A folder named “%1” would be hidden — choose another name.").arg(hidden));
        return {};
    }
    const QString to = notebook::renamedPath(path, newTitle, false, slashMoves);
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
    if (const QString hidden = notebook::hiddenTypedSegment(newName, true, slashMoves); !hidden.isEmpty()) {
        Q_EMIT errorOccurred(tr("A folder named “%1” would be hidden — choose another name.").arg(hidden));
        return {};
    }
    const QString to = notebook::renamedPath(path, newName, true, slashMoves);
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
