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

namespace {
/// The path `from` gets in `folder` ("" = the notebook root); both normalised.
QString movedPath(const QString &from, const QString &folder)
{
    const QString name = paths::basename(from);
    return folder.isEmpty() ? name : folder + u'/' + name;
}

/// The key of a relocation impact in Workspace::m_impacts.
QString impactKey(const QString &from, const QString &to)
{
    return paths::normalize(from) + u'\n' + paths::normalize(to);
}

/// Planning time per event-loop turn: half a frame.
constexpr int kImpactSliceMs = 8;
} // namespace

int Workspace::pageCount() const
{
    return m_vault ? int(m_vault->markdownFiles().size()) : 0;
}

int Workspace::pagesWithoutJournals() const
{
    // The index's notes are the pages without conflict copies; its lists are cached until the
    // next change (this is read on every revision).
    return m_index ? int(m_index->paths().size() - m_index->journals().size()) : 0;
}

QString Workspace::linkTarget(const QString &path, const QString &fromPath) const
{
    return m_index ? m_index->linkTarget(path, fromPath).value_or(QString()) : QString();
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

QString Workspace::folderProblem(const QString &rawPath) const
{
    if (!m_vault)
        return {};
    const notebook::CreatedFolder planned = notebook::plannedFolder(*m_vault, rawPath);
    if (planned.existing)
        return pathProblemMessage({notebook::PathProblem::FolderExists, paths::basename(planned.path), QString()});
    return pathProblemMessage(planned.problem);
}

QStringList Workspace::folderPaths() const
{
    return m_vault ? notebook::folderPaths(*m_vault) : QStringList();
}

QVariantMap Workspace::folderInfo(const QString &rawPath) const
{
    const notebook::FolderInfo info = m_vault ? notebook::folderInfo(*m_vault, rawPath) : notebook::FolderInfo();
    QVariantList specials;
    for (const notebook::SpecialPath &s : info.specials)
        specials.append(QVariantMap{{u"special"_s, s.special}, {u"path"_s, s.path}});
    return {{u"exists"_s, info.exists},
            {u"pages"_s, info.pages},
            {u"attachments"_s, info.attachments},
            {u"folders"_s, info.folders},
            {u"specials"_s, specials}};
}

QVariantList Workspace::subfolders(const QString &rawFolder) const
{
    if (!m_vault)
        return {};
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
    const QString journalsFolder = paths::normalize(m_vault->config().journalsFolder);
    const QString journalsPrefix = journalsFolder + u'/';
    const auto inJournals = [&](const QString &path) {
        return !journalsFolder.isEmpty()
               && (path.compare(journalsFolder, Qt::CaseInsensitive) == 0
                   || path.startsWith(journalsPrefix, Qt::CaseInsensitive));
    };
    // Iterated as const: files() shares the vault's own list, which a non-const loop would copy.
    for (const QList<FileStat> files = m_vault->files(); const FileStat &f : files) {
        const QString child = childOf(f.path);
        if (child.isEmpty())
            continue;
        if (!paths::isMarkdown(f.path)) {
            withAttachments.insert(child);
            continue;
        }
        // The index's notes: no conflict copies; daily notes left out (cheap lookups, no
        // parsing) — except in the journals folder, which lists them (AllPagesView).
        const ParsedNote *note = m_index ? m_index->note(f.path) : nullptr;
        if (note && (!note->journalDate || inJournals(child)))
            pages[child] += 1;
    }
    QStringList children;
    for (const QList<FileStat> folders = m_vault->folders(); const FileStat &f : folders) {
        if (f.path.startsWith(prefix) && f.path.indexOf(u'/', prefix.size()) < 0 && !paths::isIgnored(f.path)
            && (pages.contains(f.path) || !withAttachments.contains(f.path)))
            children.append(f.path);
    }
    std::ranges::sort(children, [](const QString &a, const QString &b) {
        return text::compareNames(paths::basename(a), paths::basename(b)) < 0;
    });
    QVariantList out;
    for (const QString &c : std::as_const(children))
        out.append(QVariantMap{{u"path"_s, c}, {u"name"_s, paths::basename(c)}, {u"pages"_s, pages.value(c)}});
    return out;
}

void Workspace::notifyFolderChange(const QStringList &changed, bool byUser)
{
    ++m_revision;
    Q_EMIT revisionChanged();
    m_trashCount = -1;
    Q_EMIT trashChanged();
    Q_EMIT filesChanged(changed);
    if (byUser)
        m_sync->notifyLocalChange();
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

QVariantMap Workspace::impactMap(const notebook::RelocationImpact &impact) const
{
    QVariantMap out{{u"ok"_s, impact.ok},
                    {u"path"_s, impact.path},
                    {u"links"_s, impact.links},
                    {u"tags"_s, impact.tags},
                    {u"queries"_s, impact.queries},
                    {u"notes"_s, impact.notes},
                    {u"specials"_s, QVariant(impact.specials)}};
    if (impact.same)
        out.insert(u"same"_s, true);
    if (!impact.ok)
        out.insert(u"error"_s, pathProblemMessage(impact.problem));
    return out;
}

QVariantMap Workspace::cachedImpact(const QString &from, const QString &to)
{
    if (!m_index)
        return {};
    if (m_impacts.version != m_index->version() || m_impacts.revision != m_revision) {
        // The notebook changed: what was planned may be different now.
        m_impacts = {.version = m_index->version(), .revision = m_revision, .maps = {}};
        return {};
    }
    return m_impacts.maps.value(impactKey(from, to));
}

void Workspace::storeImpact(const QString &from, const QString &to, const QVariantMap &impact)
{
    // Only what a plan found (a refusal is checked again: it is quick, and its message is in the
    // current language).
    if (!m_index || !impact.value(u"ok"_s).toBool())
        return;
    cachedImpact(from, to); // drops what an older state of the notebook planned
    m_impacts.maps.insert(impactKey(from, to), impact);
}

QVariantMap Workspace::relocationImpact(const QString &from, const QString &to)
{
    if (!m_vault)
        return {{u"ok"_s, false}, {u"path"_s, to}, {u"error"_s, tr("No notebook is open.")}};
    finishIndexing(); // every linking note must be known
    if (QVariantMap known = cachedImpact(from, to); !known.isEmpty())
        return known;
    const QVariantMap out = impactMap(notebook::relocationImpact(*m_vault, m_index.get(), from, to));
    storeImpact(from, to, out);
    return out;
}

// ---- The move's impact, a few notes per event-loop turn -------------------------------------

struct Workspace::PendingImpact {
    QString path; ///< as requested (moveImpactReady() hands them back)
    QString folder;
    QString from;
    QString to;
    notebook::RelocationImpact impact;          ///< the checks, then the plan's counts
    std::unique_ptr<RelocationPlanner> planner; ///< once the checks passed
};

void Workspace::requestMoveImpact(const QString &path, const QString &targetFolder)
{
    cancelMoveImpact();
    auto pending = std::make_shared<PendingImpact>();
    pending->path = path;
    pending->folder = targetFolder;
    pending->from = paths::normalize(path);
    pending->to = movedPath(pending->from, paths::normalize(targetFolder));
    m_pendingImpact = std::move(pending);
    m_impactTimer.start(); // the first step on the next turn: never from inside the request
}

void Workspace::cancelMoveImpact()
{
    m_impactTimer.stop();
    m_pendingImpact.reset();
}

void Workspace::moveImpactStep()
{
    const std::shared_ptr<PendingImpact> p = m_pendingImpact;
    if (!p) {
        m_impactTimer.stop();
        return;
    }
    const auto deliver = [this, p](const QVariantMap &impact) {
        cancelMoveImpact();
        Q_EMIT moveImpactReady(p->path, p->folder, impact);
    };
    if (!m_vault || !m_index) {
        deliver(relocationImpact(p->from, p->to)); // "No notebook is open."
        return;
    }
    if (p->planner && !p->planner->valid())
        p->planner.reset(); // the notebook changed meanwhile: from the start
    if (!p->planner) {
        finishIndexing(); // every linking note must be known
        if (m_pendingImpact != p)
            return; // what finishing the index told the views asked for another target
        if (QVariantMap known = cachedImpact(p->from, p->to); !known.isEmpty()) {
            deliver(known);
            return;
        }
        QList<FileMove> moves;
        p->impact = notebook::relocationChecks(*m_vault, m_index.get(), p->from, p->to, &moves);
        if (!p->impact.ok || p->impact.same) {
            deliver(impactMap(p->impact));
            return;
        }
        p->planner = std::make_unique<RelocationPlanner>(*m_index, moves);
        if (!p->planner->done())
            return; // the notes on the next turns
    }
    if (!p->planner->step(kImpactSliceMs))
        return; // more on the next turn
    notebook::addRelocationPlan(p->impact, p->planner->plan());
    const QVariantMap impact = impactMap(p->impact);
    storeImpact(p->from, p->to, impact);
    deliver(impact);
}

QVariantMap Workspace::moveImpact(const QString &path, const QString &targetFolder)
{
    const QString from = paths::normalize(path);
    return relocationImpact(from, movedPath(from, paths::normalize(targetFolder)));
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
    const QString fromPath = paths::normalize(from);
    // Every note that links to a moved file must be indexed before the plan asks (right after
    // opening the index is still being built in the background).
    finishIndexing();
    releasePdfReaders(fromPath);
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
        QString journals;
        for (const notebook::RebasedSetting &r : moved.rebased) {
            if (r.special == u"journals")
                journals = r.value;
        }
        for (const notebook::RebasedSetting &r : moved.rebased) {
            // Weekly and monthly notes next to the daily ones went with them: said once; their
            // templates only when they are there.
            if ((r.special == u"weekly" || r.special == u"monthly") && r.value == journals)
                continue;
            if ((r.special == u"weeklyTemplate" || r.special == u"monthlyTemplate") && !m_vault->exists(r.value))
                continue;
            if (r.special == u"journals")
                said.append(tr("journals folder → “%1”").arg(r.value));
            else if (r.special == u"templates")
                said.append(tr("templates folder → “%1”").arg(r.value));
            else if (r.special == u"attachments")
                said.append(tr("attachments folder → “%1”").arg(r.value));
            else if (r.special == u"newPages")
                said.append(tr("folder for new pages → “%1”").arg(r.value));
            else if (r.special == u"weekly")
                said.append(tr("folder of weekly notes → “%1”").arg(r.value));
            else if (r.special == u"monthly")
                said.append(tr("folder of monthly notes → “%1”").arg(r.value));
            else if (r.special == u"weeklyTemplate")
                said.append(tr("weekly note template → “%1”").arg(r.value));
            else if (r.special == u"monthlyTemplate")
                said.append(tr("monthly note template → “%1”").arg(r.value));
            else
                said.append(tr("journal template → “%1”").arg(r.value));
        }
        Q_EMIT noticeOccurred(tr("Notebook settings updated: %1").arg(said.join(u", "_s)));
    }
    if (moved.folder && moved.path != fromPath) {
        notifyFolderChange({fromPath, moved.path});
        Q_EMIT folderRenamed(fromPath, moved.path);
    }
    return moved.path;
}

QString Workspace::renameTo(const QString &path, const QString &newName, bool isDir, bool slashMoves)
{
    // A name the vault would hide is refused (as for a new folder), before it is sanitised.
    if (const QString hidden = notebook::hiddenTypedSegment(newName, isDir, slashMoves); !hidden.isEmpty()) {
        Q_EMIT errorOccurred(tr("A folder named “%1” would be hidden — choose another name.").arg(hidden));
        return {};
    }
    const QString to = notebook::renamedPath(path, newName, isDir, slashMoves);
    return to.isEmpty() ? QString() : relocate(path, to);
}

QString Workspace::renamePage(const QString &rawPath, const QString &newTitle, bool slashMoves)
{
    const QString path = paths::normalize(rawPath);
    if (!m_vault || !m_vault->exists(path))
        return {};
    return renameTo(path, newTitle, false, slashMoves);
}

QString Workspace::renameFolder(const QString &rawPath, const QString &newName, bool slashMoves)
{
    const QString path = paths::normalize(rawPath);
    if (!m_vault || path.isEmpty() || newName.trimmed().isEmpty())
        return {};
    return renameTo(path, newName, true, slashMoves);
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
    return relocate(path, movedPath(path, folder));
}

} // namespace brulion::shell
