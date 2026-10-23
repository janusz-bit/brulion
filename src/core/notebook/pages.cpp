#include "notebook/pages.h"

#include "index/vaultindex.h"
#include "markdown/journal.h"
#include "markdown/notemode.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"
#include "vault/vaultconfig.h"

#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::notebook {

namespace {

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

PathProblem problem(PathProblem::Kind kind, const QString &name = QString(), const QString &detail = QString())
{
    return PathProblem{kind, name, detail};
}

/// The settings of the notebook that name folders (and the journal template), with their keys.
struct SpecialSetting {
    const char *key;
    const char *special;
    QString VaultConfig::*field;
};
const SpecialSetting kSpecialSettings[] = {
        {"journalsFolder", "journals", &VaultConfig::journalsFolder},          // the daily notes
        {"templatesFolder", "templates", &VaultConfig::templatesFolder},       // the templates offered
        {"attachmentsFolder", "attachments", &VaultConfig::attachmentsFolder}, // pasted pictures
        {"newPagesFolder", "newPages", &VaultConfig::newPagesFolder},          // new pages
        {"journalTemplate", "journalTemplate", &VaultConfig::journalTemplate}, // a file
};

} // namespace

QString withoutMdExtension(const QString &title)
{
    const QString t = title.trimmed();
    if (t.size() > 3 && t.endsWith(u".md"_s, Qt::CaseInsensitive) && t.at(t.size() - 4) != u'/')
        return t.chopped(3);
    return title;
}

VaultPath newPagePath(const Vault &vault, const QString &target)
{
    const VaultConfig config = vault.config();
    // [[2026-10-20]]: a day without a note yet opens that day's journal page.
    const QString name = withoutMdExtension(target).trimmed();
    QDate day = QDate::fromString(name, config.journalFileFormat);
    if (!day.isValid())
        day = QDate::fromString(name, Qt::ISODate);
    if (day.isValid())
        return markdown::journalPathForDate(day, config);
    // SYNTAX.md §2: an unresolved link points to a page in newPagesFolder ("Projekty/Dom" keeps
    // its folders). The page is not created here: the editor creates it with its first content.
    QStringList parts;
    for (const QString &part : name.split(u'/', Qt::SkipEmptyParts)) {
        if (!part.trimmed().isEmpty())
            parts.append(paths::sanitizeFileName(part.trimmed()));
    }
    if (parts.isEmpty())
        return {};
    const QString base = paths::normalize(config.newPagesFolder);
    const QString relative = parts.join(u'/') + u".md"_s;
    // Folders as the vault spells them: [[projekty/Dom]] next to "Projekty/" is a page in it.
    return vault.existingCase(base.isEmpty() ? relative : paths::join(base, relative));
}

CreatedPage plannedPage(const Vault &vault, const QString &title, const QString &folder)
{
    CreatedPage out;
    // "Projekty/Dom" creates the page "Dom" in the folder "Projekty".
    QStringList parts;
    for (const QString &part : withoutMdExtension(title).split(u'/', Qt::SkipEmptyParts)) {
        if (!part.trimmed().isEmpty())
            parts.append(paths::sanitizeFileName(part));
    }
    if (parts.isEmpty()) {
        out.problem = problem(PathProblem::NoName);
        return out;
    }
    QString base = folder.isNull() ? vault.config().newPagesFolder : folder;
    base = paths::normalize(base);
    const QString relative = parts.join(u'/') + u".md"_s;
    const QString wanted = base.isEmpty() ? relative : paths::join(base, relative);
    out.wanted = wanted;
    out.path = wanted;
    // A page whose name differs only in letter case ("dom" next to "Dom"): one file on Android's
    // shared storage and other case-insensitive folders a synced vault may land in, and one page
    // for links — that page is the result instead of a second one.
    const VaultPath spelled = vault.existingCase(wanted);
    if (const auto existing = spelled != wanted ? vault.stat(spelled) : std::nullopt; existing && !existing->isDir) {
        out.path = spelled;
        out.existing = true;
    }
    return out;
}

QString newPageText(const Vault &vault, const VaultPath &path, std::optional<NoteMode> mode, const QString &content)
{
    if (!mode)
        return content;
    // The mode chosen for the page is written only when the new page would not get it anyway
    // (docs/SYNTAX.md §4 "Tryb notatki": an empty page takes the notebook's page mode).
    const VaultConfig config = vault.config();
    return markdown::withNoteModeChoice(content, *mode, markdown::journalDateFromPath(path, config).has_value(),
                                        config);
}

CreatedPage createPage(Vault &vault, const QString &title, const QString &folder, std::optional<NoteMode> mode,
                       const QString &content, ChangeSource source)
{
    CreatedPage out = plannedPage(vault, title, folder);
    if (out.path.isEmpty() || out.existing)
        return out;
    out.path = vault.create(out.wanted, newPageText(vault, out.wanted, mode, content), source);
    if (out.path.isEmpty())
        out.problem = problem(PathProblem::Failed, QString(), vault.errorString());
    return out;
}

CreatedFolder createFolder(Vault &vault, const QString &rawPath)
{
    CreatedFolder out;
    QStringList segments;
    const QString unified = QString(rawPath).replace(u'\\', u'/');
    for (const QString &raw : unified.split(u'/', Qt::SkipEmptyParts)) {
        const QString segment = raw.trimmed().normalized(QString::NormalizationForm_C);
        if (segment.isEmpty() || segment == u"." || segment == u"..")
            continue;
        // Hidden as typed (".notatki": the leading dot would be dropped) or once sanitised
        // ("logseq?" becomes "logseq").
        const QString clean = paths::sanitizeFileName(segment);
        if (hiddenName(segment) || hiddenName(clean)) {
            out.problem = problem(PathProblem::HiddenFolder, hiddenName(segment) ? segment : clean);
            return out;
        }
        segments.append(clean);
    }
    if (segments.isEmpty()) {
        out.problem = problem(PathProblem::NoName);
        return out;
    }
    // Folders as the vault spells them ("projekty/Nowy" goes into "Projekty/").
    const QString folder = vault.existingCase(segments.join(u'/'));
    if (const auto existing = vault.stat(folder); existing && !existing->isDir) {
        out.problem = problem(PathProblem::NotAFolder, paths::basename(folder));
        return out;
    }
    if (!vault.createFolder(folder)) {
        out.problem = problem(PathProblem::Failed, QString(), vault.errorString());
        return out;
    }
    out.path = folder;
    return out;
}

QStringList folderPaths(const Vault &vault)
{
    QStringList out;
    for (const FileStat &f : vault.folders()) {
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

QList<SpecialPath> specialsWithin(const VaultConfig &config, const VaultPath &path)
{
    QList<SpecialPath> out;
    if (path.isEmpty())
        return out;
    for (const SpecialSetting &s : kSpecialSettings) {
        const QString value = paths::normalize(config.*(s.field));
        if (isWithin(value, path))
            out.append(SpecialPath{QString::fromLatin1(s.special), QString::fromLatin1(s.key), value});
    }
    return out;
}

FolderInfo folderInfo(const Vault &vault, const VaultPath &rawPath)
{
    FolderInfo out;
    const QString path = paths::normalize(rawPath);
    if (path.isEmpty())
        return out;
    const auto stat = vault.stat(path);
    out.exists = stat && stat->isDir;
    for (const FileStat &f : vault.files()) {
        if (!f.path.startsWith(path + u'/'))
            continue;
        if (!paths::isMarkdown(f.path))
            ++out.attachments;
        else if (!paths::isConflictCopy(f.path))
            ++out.pages;
    }
    for (const FileStat &f : vault.folders()) {
        if (f.path.startsWith(path + u'/'))
            ++out.folders;
    }
    out.specials = specialsWithin(vault.config(), path);
    return out;
}

VaultPath spelledFolders(const Vault &vault, const VaultPath &path)
{
    if (!path.contains(u'/'))
        return path;
    const QString folder = vault.existingCase(paths::dirname(path));
    return paths::join(folder, paths::basename(path));
}

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

VaultPath renamedPath(const VaultPath &rawPath, const QString &newName, bool isDir, bool slashMoves)
{
    const QString path = paths::normalize(rawPath);
    if (path.isEmpty())
        return {};
    const bool note = !isDir && paths::isMarkdown(path);
    const QString ext = isDir ? QString() : (paths::extension(path).isEmpty() ? u".md"_s : paths::extension(path));
    // Typed with the extension ("Notatka.md"): one extension.
    const QString typed = note ? withoutMdExtension(newName).trimmed() : newName.trimmed();
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

PathProblem relocationProblem(const Vault &vault, const VaultIndex *index, const VaultPath &from, const VaultPath &to)
{
    const auto source = vault.stat(from);
    if (from.isEmpty() || !source)
        return problem(PathProblem::Missing, paths::pageTitle(from));
    if (to.isEmpty())
        return problem(PathProblem::NoName);
    if (isWithin(to, from) && to != from)
        return problem(PathProblem::IntoItself);
    // Never into a folder the notebook ignores (.git, logseq, .trash, …); new names the vault
    // would hide are refused (folders that exist already, like a scanned ".notatki", are fine).
    if (paths::isIgnored(to))
        return problem(PathProblem::HiddenPath, paths::basename(to));
    const QStringList segments = to.split(u'/');
    for (qsizetype i = 0; i < segments.size(); ++i) {
        const QString prefix = segments.mid(0, i + 1).join(u'/');
        const bool last = i + 1 == segments.size();
        const bool exists = !last && vault.stat(vault.existingCase(prefix)).has_value();
        const bool renamed = last && segments.at(i) != paths::basename(from);
        if ((renamed || (!last && !exists)) && hiddenName(segments.at(i)))
            return problem(PathProblem::HiddenFolder, segments.at(i));
    }
    // A page into itself as a folder ("Dom.md" into "Dom.md/") is no folder.
    for (QString parent = paths::dirname(to); !parent.isEmpty(); parent = paths::dirname(parent)) {
        const auto st = vault.stat(vault.existingCase(parent));
        if (st && !st->isDir)
            return problem(PathProblem::NotAFolder, paths::basename(parent));
    }
    const bool caseOnly = from.compare(to, Qt::CaseInsensitive) == 0;
    if (!caseOnly) {
        // The name taken, also in another letter case ("dom" next to "Dom": one file on
        // case-insensitive storage, and links do not tell them apart).
        const QString spelled = vault.existingCase(to);
        if (vault.exists(to) || vault.exists(spelled)) {
            const QString name = paths::basename(vault.exists(spelled) ? spelled : to);
            if (source->isDir)
                return problem(PathProblem::FolderExists, name);
            if (paths::isMarkdown(from))
                return problem(PathProblem::PageExists, paths::pageTitle(name));
            return problem(PathProblem::FileExists, name);
        }
        // Another page whose path differs only in case (the index knows every note).
        if (index && paths::isMarkdown(to)) {
            for (const VaultPath &other : index->paths()) {
                if (other.compare(to, Qt::CaseInsensitive) == 0 && other.compare(from, Qt::CaseInsensitive) != 0)
                    return problem(PathProblem::PageExists, paths::pageTitle(other));
            }
        }
    }
    return {};
}

QList<FileMove> fileMoves(const Vault &vault, const VaultPath &from, const VaultPath &to)
{
    QList<FileMove> moves;
    const auto stat = vault.stat(from);
    if (stat && stat->isDir) {
        const QString prefix = from + u'/';
        for (const FileStat &f : vault.files()) {
            if (f.path.startsWith(prefix))
                moves.append({f.path, to + u'/' + f.path.mid(prefix.size())});
        }
    } else {
        moves.append({from, to});
    }
    return moves;
}

RelocationImpact relocationChecks(const Vault &vault, const VaultIndex *index, const VaultPath &rawFrom,
                                  const VaultPath &rawTo, QList<FileMove> *moves)
{
    RelocationImpact out;
    const QString from = paths::normalize(rawFrom);
    const QString to = spelledFolders(vault, paths::normalize(rawTo));
    out.path = to;
    if (from == to && !from.isEmpty() && vault.exists(from)) {
        out.ok = true;
        out.same = true;
        return out;
    }
    out.problem = relocationProblem(vault, index, from, to);
    if (!out.problem.isOk())
        return out;
    out.ok = true;
    // The settings relocate() changes (special folders, the journal template file).
    for (const SpecialPath &s : specialsWithin(vault.config(), from))
        out.specials.append(s.special);
    if (moves)
        *moves = fileMoves(vault, from, to);
    return out;
}

void addRelocationPlan(RelocationImpact &impact, const RelocationPlan &plan)
{
    impact.links = plan.links();
    impact.tags = plan.tags();
    impact.queries = plan.queries();
    impact.notes = int(plan.files().size());
    impact.rewrites.clear();
    for (const RelocationPlan::File &f : plan.files())
        impact.rewrites.append(PlannedRewrite{f.path, f.newPath, f.before, f.after});
}

RelocationImpact relocationImpact(const Vault &vault, const VaultIndex *index, const VaultPath &from,
                                  const VaultPath &to)
{
    QList<FileMove> moves;
    RelocationImpact out = relocationChecks(vault, index, from, to, index ? &moves : nullptr);
    if (index && out.ok && !out.same)
        addRelocationPlan(out, index->planRelocation(moves));
    return out;
}

Relocation relocate(Vault &vault, const VaultIndex *index, const VaultPath &rawFrom, const VaultPath &rawTo,
                    ChangeSource source, const WriteText &write)
{
    Relocation out;
    const QString from = paths::normalize(rawFrom);
    const QString to = spelledFolders(vault, paths::normalize(rawTo));
    if (!from.isEmpty() && from == to && vault.exists(from)) {
        out.path = from;
        out.folder = vault.stat(from)->isDir;
        return out;
    }
    out.problem = relocationProblem(vault, index, from, to);
    if (!out.problem.isOk())
        return out;
    out.folder = vault.stat(from)->isDir;
    out.moving = paths::dirname(from) != paths::dirname(to);
    // Every note that links to a moved file must be in the index before the plan asks.
    const RelocationPlan plan = index ? index->planRelocation(fileMoves(vault, from, to)) : RelocationPlan();
    if (!vault.rename(from, to, source)) {
        out.problem = problem(PathProblem::Failed, QString(), vault.errorString());
        return out;
    }
    out.path = to;
    // The planned notes, where they are now (also a moved note itself); one write — one history
    // step — each. A note saved since the plan was made gets the plan applied to its text.
    for (const RelocationPlan::File &f : plan.files()) {
        bool ok = false;
        const QString content = vault.readText(f.newPath, &ok);
        if (!ok)
            continue;
        const QString rewritten = plan.rewrite(f.path, content);
        if (rewritten == content)
            continue;
        if (write ? write(f.newPath, rewritten) : vault.writeText(f.newPath, rewritten, source))
            out.rewritten.append(f.newPath);
    }
    // Settings naming `from` or a path in it follow it.
    VaultConfig config = vault.config();
    for (const SpecialSetting &s : kSpecialSettings) {
        const QString value = paths::normalize(config.*(s.field));
        if (!isWithin(value, from))
            continue;
        const QString moved = to + value.mid(from.size());
        config.*(s.field) = moved;
        out.rebased.append(RebasedSetting{QString::fromLatin1(s.special), QString::fromLatin1(s.key), moved});
    }
    if (!out.rebased.isEmpty() && !sameVaultConfig(config, vault.config()) && !vault.saveConfig(config))
        out.configError = vault.errorString();
    return out;
}

Relocation renamePage(Vault &vault, const VaultIndex *index, const VaultPath &rawPath, const QString &newTitle,
                      bool slashMoves, ChangeSource source, const WriteText &write)
{
    Relocation out;
    const QString path = paths::normalize(rawPath);
    if (!vault.exists(path)) {
        out.problem = problem(PathProblem::Missing, paths::pageTitle(path));
        return out;
    }
    if (const QString hidden = hiddenTypedSegment(newTitle, false, slashMoves); !hidden.isEmpty()) {
        out.problem = problem(PathProblem::HiddenFolder, hidden);
        return out;
    }
    const QString to = renamedPath(path, newTitle, false, slashMoves);
    if (to.isEmpty()) {
        out.problem = problem(PathProblem::NoName);
        return out;
    }
    return relocate(vault, index, path, to, source, write);
}

Relocation renameFolder(Vault &vault, const VaultIndex *index, const VaultPath &rawPath, const QString &newName,
                        bool slashMoves, ChangeSource source, const WriteText &write)
{
    Relocation out;
    const QString path = paths::normalize(rawPath);
    if (path.isEmpty() || !vault.exists(path)) {
        out.problem = problem(PathProblem::Missing, paths::basename(path));
        return out;
    }
    if (newName.trimmed().isEmpty()) {
        out.problem = problem(PathProblem::NoName);
        return out;
    }
    // A name the vault would hide is refused (as for a new folder), before it is sanitised.
    if (const QString hidden = hiddenTypedSegment(newName, true, slashMoves); !hidden.isEmpty()) {
        out.problem = problem(PathProblem::HiddenFolder, hidden);
        return out;
    }
    const QString to = renamedPath(path, newName, true, slashMoves);
    if (to.isEmpty()) {
        out.problem = problem(PathProblem::NoName);
        return out;
    }
    return relocate(vault, index, path, to, source, write);
}

Relocation movePath(Vault &vault, const VaultIndex *index, const VaultPath &rawPath, const QString &targetFolder,
                    ChangeSource source, const WriteText &write)
{
    Relocation out;
    const QString path = paths::normalize(rawPath);
    const QString folder = paths::normalize(targetFolder);
    if (path.isEmpty() || !vault.exists(path)) {
        out.problem = problem(PathProblem::Missing, paths::pageTitle(path));
        return out;
    }
    if (!folder.isEmpty()) {
        if (const auto st = vault.stat(vault.existingCase(folder)); st && !st->isDir) {
            out.problem = problem(PathProblem::NotAFolder, paths::basename(folder));
            return out;
        }
    }
    const QString name = paths::basename(path);
    return relocate(vault, index, path, folder.isEmpty() ? name : folder + u'/' + name, source, write);
}

DeletedPath deletePath(Vault &vault, const VaultPath &rawPath, ChangeSource source)
{
    DeletedPath out;
    const QString path = paths::normalize(rawPath);
    if (path.isEmpty() || !vault.exists(path)) {
        out.problem = problem(PathProblem::Missing, paths::pageTitle(path));
        return out;
    }
    QSet<QString> before;
    for (const FileStat &f : vault.trash())
        before.insert(f.path);
    const auto stat = vault.stat(path);
    out.folder = stat && stat->isDir;
    if (!vault.remove(path, source)) {
        out.problem = problem(PathProblem::Failed, paths::pageTitle(path), vault.errorString());
        return out;
    }
    // The new entry in the trash: ".trash/<path with a time stamp>" (a folder: its top entry).
    const int depth = int(path.count(u'/')) + 2; // ".trash" + the path's segments
    for (const FileStat &f : vault.trash()) {
        if (before.contains(f.path))
            continue;
        out.trashPath = f.path.split(u'/').mid(0, depth).join(u'/');
        return out;
    }
    out.trashPath = vaultTrashFolder() + u'/' + path;
    return out;
}

} // namespace brulion::notebook
