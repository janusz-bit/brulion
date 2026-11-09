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

/// A result (CreatedPage, Relocation, …) that only carries a problem.
template<typename Result>
Result refused(PathProblem::Kind kind, const QString &name = QString(), const QString &detail = QString())
{
    Result out;
    out.problem = problem(kind, name, detail);
    return out;
}

/// The segments of a typed name sanitised like file names; blank ones dropped.
QStringList sanitizedSegments(const QStringList &segments)
{
    QStringList out;
    for (const QString &segment : segments) {
        if (const QString trimmed = segment.trimmed(); !trimmed.isEmpty())
            out.append(paths::sanitizeFileName(trimmed));
    }
    return out;
}

/// The page of the segments `parts` in `folder` ("" or null: the notebook root).
VaultPath pagePath(const QString &folder, const QStringList &parts)
{
    const QString base = paths::normalize(folder);
    const QString relative = parts.join(u'/') + u".md"_s;
    return base.isEmpty() ? relative : paths::join(base, relative);
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
        {"weeklyFolder", "weekly", &VaultConfig::weeklyFolder},                // weekly notes
        {"monthlyFolder", "monthly", &VaultConfig::monthlyFolder},             // monthly notes
        {"weeklyTemplate", "weeklyTemplate", &VaultConfig::weeklyTemplate},    // a file
        {"monthlyTemplate", "monthlyTemplate", &VaultConfig::monthlyTemplate}, // a file
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
    const VaultConfig &config = vault.config();
    // [[2026-10-20]]: a day without a note yet opens that day's journal page.
    const QString name = withoutMdExtension(target).trimmed();
    QDate day = QDate::fromString(name, config.journalFileFormat);
    if (!day.isValid())
        day = QDate::fromString(name, Qt::ISODate);
    if (day.isValid())
        return markdown::journalPathForDate(day, config);
    // SYNTAX.md §2: an unresolved link points to a page in newPagesFolder ("Projekty/Dom" keeps
    // its folders). The page is not created here: the editor creates it with its first content.
    const QStringList parts = sanitizedSegments(name.split(u'/', Qt::SkipEmptyParts));
    if (parts.isEmpty())
        return {};
    // Folders as the vault spells them: [[projekty/Dom]] next to "Projekty/" is a page in it.
    return vault.existingCase(pagePath(config.newPagesFolder, parts));
}

CreatedPage plannedPage(const Vault &vault, const QString &title, const QString &folder)
{
    // "Projekty/Dom" creates the page "Dom" in the folder "Projekty".
    const QStringList parts = sanitizedSegments(withoutMdExtension(title).split(u'/', Qt::SkipEmptyParts));
    if (parts.isEmpty())
        return refused<CreatedPage>(PathProblem::NoName);
    const VaultPath wanted = pagePath(folder.isNull() ? vault.config().newPagesFolder : folder, parts);
    CreatedPage out;
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
    // (docs/SYNTAX.md §4 "Note mode": an empty page takes the notebook's page mode).
    const VaultConfig &config = vault.config();
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

CreatedFolder plannedFolder(const Vault &vault, const QString &rawPath)
{
    QStringList segments;
    const QString unified = QString(rawPath).replace(u'\\', u'/');
    for (const QString &raw : unified.split(u'/', Qt::SkipEmptyParts)) {
        const QString segment = raw.trimmed().normalized(QString::NormalizationForm_C);
        if (segment.isEmpty() || segment == u"." || segment == u"..")
            continue;
        // Hidden as typed (".notatki": the leading dot would be dropped) or once sanitised
        // ("logseq?" becomes "logseq").
        const QString clean = paths::sanitizeFileName(segment);
        if (hiddenName(segment) || hiddenName(clean))
            return refused<CreatedFolder>(PathProblem::HiddenFolder, hiddenName(segment) ? segment : clean);
        segments.append(clean);
    }
    if (segments.isEmpty())
        return refused<CreatedFolder>(PathProblem::NoName);
    // Folders as the vault spells them ("projekty/Nowy" goes into "Projekty/").
    CreatedFolder out;
    out.path = vault.existingCase(segments.join(u'/'));
    if (const auto existing = vault.stat(out.path)) {
        if (!existing->isDir)
            return refused<CreatedFolder>(PathProblem::NotAFolder, paths::basename(out.path));
        out.existing = true;
    }
    return out;
}

CreatedFolder createFolder(Vault &vault, const QString &rawPath)
{
    CreatedFolder out = plannedFolder(vault, rawPath);
    if (out.path.isEmpty() || vault.createFolder(out.path))
        return out;
    return refused<CreatedFolder>(PathProblem::Failed, QString(), vault.errorString());
}

QStringList folderPaths(const Vault &vault)
{
    QStringList out;
    for (const FileStat &f : vault.folders()) {
        if (!paths::isIgnored(f.path))
            out.append(f.path);
    }
    // Polish order segment by segment: "Projekty" before "Projekty 2" and "Projekty/Dom".
    std::ranges::sort(out, [](const QString &a, const QString &b) {
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
    const QString journals = paths::normalize(config.journalsFolder);
    for (const SpecialSetting &s : kSpecialSettings) {
        const QString value = paths::normalize(config.*(s.field));
        // Weekly and monthly notes next to the daily ones: the journals folder names them.
        const QLatin1StringView special(s.special);
        if ((special == "weekly"_L1 || special == "monthly"_L1) && value == journals)
            continue;
        if (isWithin(value, path))
            out.append(SpecialPath{QString::fromLatin1(s.special), QString::fromLatin1(s.key), value});
    }
    return out;
}

namespace {

/// specialsWithin() without a weekly or monthly template that is not there (the defaults name
/// files most notebooks never create).
QList<SpecialPath> presentSpecialsWithin(const Vault &vault, const VaultPath &path)
{
    QList<SpecialPath> out = specialsWithin(vault.config(), path);
    out.removeIf([&](const SpecialPath &s) {
        return (s.special == u"weeklyTemplate" || s.special == u"monthlyTemplate") && !vault.exists(s.path);
    });
    return out;
}

} // namespace

FolderInfo folderInfo(const Vault &vault, const VaultPath &rawPath)
{
    FolderInfo out;
    const QString path = paths::normalize(rawPath);
    if (path.isEmpty())
        return out;
    const auto stat = vault.stat(path);
    out.exists = stat && stat->isDir;
    const QString prefix = path + u'/';
    const auto inside = [&prefix](const FileStat &f) { return f.path.startsWith(prefix); };
    // const: files() shares the vault's cached list, which a non-const loop would copy.
    for (const QList<FileStat> files = vault.files(); const FileStat &f : files) {
        if (!inside(f))
            continue;
        if (!paths::isMarkdown(f.path))
            ++out.attachments;
        else if (!paths::isConflictCopy(f.path))
            ++out.pages;
    }
    out.folders = int(std::ranges::count_if(vault.folders(), inside));
    out.specials = presentSpecialsWithin(vault, path);
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
    QStringList parts = sanitizedSegments(moves ? QString(typed).replace(u'\\', u'/').split(u'/', Qt::SkipEmptyParts)
                                                : QStringList{typed});
    if (parts.isEmpty())
        return {};
    QString name = parts.takeLast();
    // A page gets its extension back, an attachment keeps its type ("plan" -> "plan.png").
    if (!isDir && (note || !name.endsWith(ext, Qt::CaseInsensitive)))
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
        const QString &segment = segments.at(i);
        if (!hiddenName(segment))
            continue;
        // A new name: the item's own when it is renamed, or a folder that is not there yet.
        const bool last = i + 1 == segments.size();
        if (last ? segment != paths::basename(from) : !vault.stat(vault.existingCase(segments.first(i + 1).join(u'/'))))
            return problem(PathProblem::HiddenFolder, segment);
    }
    // A page into itself as a folder ("Dom.md" into "Dom.md/") is no folder.
    for (QString parent = paths::dirname(to); !parent.isEmpty(); parent = paths::dirname(parent)) {
        if (const auto st = vault.stat(vault.existingCase(parent)); st && !st->isDir)
            return problem(PathProblem::NotAFolder, paths::basename(parent));
    }
    if (from.compare(to, Qt::CaseInsensitive) == 0)
        return {}; // only the letter case changes
    // The name taken, also in another letter case ("dom" next to "Dom": one file on
    // case-insensitive storage, and links do not tell them apart).
    const QString spelled = vault.existingCase(to);
    if (const bool spelledTaken = vault.exists(spelled); spelledTaken || vault.exists(to)) {
        const QString name = paths::basename(spelledTaken ? spelled : to);
        if (source->isDir)
            return problem(PathProblem::FolderExists, name);
        if (paths::isMarkdown(from))
            return problem(PathProblem::PageExists, paths::pageTitle(name));
        return problem(PathProblem::FileExists, name);
    }
    // Another page whose path differs only in case (the index knows every note).
    if (index && paths::isMarkdown(to)) {
        // const: paths() shares the index's cached list, which a non-const loop would copy.
        for (const QList<VaultPath> all = index->paths(); const VaultPath &other : all) {
            if (other.compare(to, Qt::CaseInsensitive) == 0 && other.compare(from, Qt::CaseInsensitive) != 0)
                return problem(PathProblem::PageExists, paths::pageTitle(other));
        }
    }
    return {};
}

QList<FileMove> fileMoves(const Vault &vault, const VaultPath &from, const VaultPath &to)
{
    if (const auto stat = vault.stat(from); !stat || !stat->isDir)
        return {FileMove{from, to}};
    QList<FileMove> moves;
    const QString prefix = from + u'/';
    // const: files() shares the vault's cached list, which a non-const loop would copy.
    for (const QList<FileStat> files = vault.files(); const FileStat &f : files) {
        if (f.path.startsWith(prefix))
            moves.append({f.path, to + u'/' + f.path.mid(prefix.size())});
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
    if (from == to && vault.exists(from)) {
        out.ok = true;
        out.same = true;
        return out;
    }
    out.problem = relocationProblem(vault, index, from, to);
    if (!out.problem.isOk())
        return out;
    out.ok = true;
    // The settings relocate() changes (special folders, the journal template file).
    for (const SpecialPath &s : presentSpecialsWithin(vault, from))
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
    if (from == to && vault.exists(from)) {
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

namespace {

/// renamePage() and renameFolder(): `newName` is checked as typed, before it is sanitised.
Relocation renameItem(Vault &vault, const VaultIndex *index, const VaultPath &rawPath, const QString &newName,
                      bool isDir, bool slashMoves, ChangeSource source, const WriteText &write)
{
    const QString path = paths::normalize(rawPath);
    if (!vault.exists(path))
        return refused<Relocation>(PathProblem::Missing, isDir ? paths::basename(path) : paths::pageTitle(path));
    // A name the vault would hide is refused (as for a new folder); a blank one gives no path.
    if (const QString hidden = hiddenTypedSegment(newName, isDir, slashMoves); !hidden.isEmpty())
        return refused<Relocation>(PathProblem::HiddenFolder, hidden);
    const QString to = renamedPath(path, newName, isDir, slashMoves);
    if (to.isEmpty())
        return refused<Relocation>(PathProblem::NoName);
    return relocate(vault, index, path, to, source, write);
}

} // namespace

Relocation renamePage(Vault &vault, const VaultIndex *index, const VaultPath &rawPath, const QString &newTitle,
                      bool slashMoves, ChangeSource source, const WriteText &write)
{
    return renameItem(vault, index, rawPath, newTitle, /*isDir=*/false, slashMoves, source, write);
}

Relocation renameFolder(Vault &vault, const VaultIndex *index, const VaultPath &rawPath, const QString &newName,
                        bool slashMoves, ChangeSource source, const WriteText &write)
{
    return renameItem(vault, index, rawPath, newName, /*isDir=*/true, slashMoves, source, write);
}

Relocation movePath(Vault &vault, const VaultIndex *index, const VaultPath &rawPath, const QString &targetFolder,
                    ChangeSource source, const WriteText &write)
{
    const QString path = paths::normalize(rawPath);
    const QString folder = paths::normalize(targetFolder);
    if (!vault.exists(path))
        return refused<Relocation>(PathProblem::Missing, paths::pageTitle(path));
    if (const auto st = folder.isEmpty() ? std::nullopt : vault.stat(vault.existingCase(folder)); st && !st->isDir)
        return refused<Relocation>(PathProblem::NotAFolder, paths::basename(folder));
    const QString name = paths::basename(path);
    return relocate(vault, index, path, folder.isEmpty() ? name : folder + u'/' + name, source, write);
}

DeletedPath deletePath(Vault &vault, const VaultPath &rawPath, ChangeSource source)
{
    const QString path = paths::normalize(rawPath);
    if (!vault.exists(path))
        return refused<DeletedPath>(PathProblem::Missing, paths::pageTitle(path));
    QSet<QString> before;
    for (const FileStat &f : vault.trash())
        before.insert(f.path);
    DeletedPath out;
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
