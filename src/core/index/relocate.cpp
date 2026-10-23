// Moving and renaming without breaking links (docs/SYNTAX.md §2 "Rewriting on rename and move"):
// VaultIndex::linkTarget() / tagTarget() (the shortest name that reaches a file) and
// VaultIndex::planRelocation() — the notes to rewrite so every link and tag keeps its target.
//
// The plan resolves every link that a move can affect twice: with the lookup tables as they are
// (before) and with a copy in which the moved files are at their new paths (after). A link whose
// file after the move is not the file it named before — moved along — is rewritten.
#include "index/vaultindex.h"
#include "index/vaultindex_p.h"

#include "markdown/inline.h"
#include "markdown/parse.h"
#include "markdown/rename.h"
#include "util/paths.h"

#include <algorithm>

namespace brulion {

using namespace index_detail;
using namespace Qt::StringLiterals;

namespace {

bool isNotePath(const VaultPath &path)
{
    return paths::isMarkdown(path);
}

/// What names a file in a link: a note's title, an attachment's file name.
QString nameOf(const VaultPath &path)
{
    return isNotePath(path) ? paths::pageTitle(path) : paths::basename(path);
}

/// A note's path without `.md`, an attachment's path.
QString stemOf(const VaultPath &path)
{
    return isNotePath(path) ? paths::stripMarkdownExtension(path) : path;
}

/// Can a wiki link hold `target` (between `[[` and `#` / `|`)?
bool wikiSafe(const QString &target)
{
    return !target.isEmpty() && target.trimmed() == target && !target.contains(u'#') && !target.contains(u'|')
           && !target.contains(u"[["_s) && !target.contains(u"]]"_s) && !target.contains(u'\n')
           && !target.contains(u'\r');
}

/// The candidates of linkTarget() / tagTarget(): the title, longer path suffixes, the path.
QStringList suffixes(const VaultPath &path)
{
    const QStringList segments = stemOf(path).split(u'/');
    QStringList out;
    for (qsizetype n = 1; n <= segments.size(); ++n)
        out.append(segments.mid(segments.size() - n).join(u'/'));
    return out;
}

std::optional<QString> shortestLinkTarget(const NameTable &table, const VaultPath &path, const VaultPath &from)
{
    for (const QString &candidate : suffixes(path)) {
        if (wikiSafe(candidate) && table.resolve(candidate, from, TargetForm::Parsed) == path)
            return candidate;
    }
    if (isNotePath(path) && wikiSafe(path) && table.resolve(path, from, TargetForm::Parsed) == path)
        return path; // with ".md" (an attachment "Dom" without extension next to it)
    return std::nullopt;
}

std::optional<QString> shortestTagTarget(const NameTable &table, const VaultPath &path)
{
    if (!isNotePath(path))
        return std::nullopt; // a tag names a note, never an attachment
    for (const QString &candidate : suffixes(path)) {
        if (!markdown::tagSyntax(candidate).isEmpty() && table.resolveNote(candidate, TargetForm::Parsed) == path)
            return candidate;
    }
    return std::nullopt;
}

/// Index after the last path separator of a written target (0 when there is none).
qsizetype afterLastSlash(const QString &s)
{
    return std::max(s.lastIndexOf(u'/'), s.lastIndexOf(u'\\')) + 1;
}

bool sameFolder(const VaultPath &a, const VaultPath &b)
{
    return pathKey(paths::dirname(a)) == pathKey(paths::dirname(b));
}

} // namespace

// ---------------------------------------------------------------------------------------------
// RelocationPlan
// ---------------------------------------------------------------------------------------------

struct RelocationPlan::Private {
    NameTable before;
    NameTable after;
    QHash<VaultPath, VaultPath> moved; ///< old path -> new path
    QList<FileMove> moves;
    QSet<QString> keys;               ///< link keys whose resolution the moves can change
    QHash<QString, QString> tagNames; ///< tag key -> the name it is rewritten to

    /// The new target of `link` (written in the note now at `from`) that reaches `wanted`; it
    /// named `named` before the move. Empty when no written form reaches it.
    QString newTarget(const Link &link, const VaultPath &named, const VaultPath &wanted, const VaultPath &from) const
    {
        const QString written = link.target.trimmed();
        const bool wiki = link.kind != LinkKind::Markdown;
        const bool note = isNotePath(wanted);
        const auto reaches = [&](const QString &target) {
            return !target.isEmpty() && after.resolve(target, from, TargetForm::Parsed) == wanted;
        };
        // The extension as written ("[[Dom.md]]", "[x](Dom.md)") stays.
        QString ext;
        if (note) {
            for (const QString &e : {u".md"_s, u".markdown"_s}) {
                if (written.endsWith(e, Qt::CaseInsensitive))
                    ext = written.right(e.size());
            }
        }
        QString target;
        if (named != wanted) {
            // The link's own file moved: as renameLinks() writes it.
            const qsizetype cut = afterLastSlash(written);
            if (cut == 0)
                target = nameOf(wanted) + ext;
            else if (sameFolder(named, wanted))
                target = written.left(cut) + nameOf(wanted) + ext; // a rename in place: "../b/New"
            else
                target = stemOf(wanted) + ext;
            if (!reaches(target))
                target.clear();
        }
        if (target.isEmpty()) {
            if (wiki) {
                target = shortestLinkTarget(after, wanted, from).value_or(QString());
            } else {
                // A Markdown link: the vault path (other editors read these relative to the note,
                // as renameLinks() writes a move).
                target = stemOf(wanted) + (note && !ext.isEmpty() ? u".md"_s : QString());
                if (!reaches(target))
                    target = wanted;
                if (!reaches(target))
                    target.clear();
            }
        }
        if (wiki && !wikiSafe(target))
            return {};
        return target == written ? QString() : target;
    }

    File planFile(const VaultPath &path, const QString &content) const
    {
        File file;
        file.path = path;
        file.newPath = moved.value(path, path);
        file.before = content;
        const bool movedSource = moved.contains(path);
        QList<markdown::LinkEdit> edits;
        const ParsedNote note = markdown::parseNote(QString(), content);
        for (const Link &link : note.links) {
            if (link.kind != LinkKind::Wiki && link.kind != LinkKind::Embed && link.kind != LinkKind::Markdown)
                continue;
            if (link.kind == LinkKind::Markdown && isExternalTarget(link.target))
                continue;
            if (link.target.trimmed().isEmpty())
                continue; // [[#Heading]]: this note, wherever it is
            if (!movedSource && !keys.contains(linkKey(link.target, TargetForm::Parsed)))
                continue;
            const VaultPath named = before.resolve(link.target, path, TargetForm::Parsed);
            if (named.isEmpty())
                continue; // a missing page stays missing
            const VaultPath wanted = moved.value(named, named);
            if (after.resolve(link.target, file.newPath, TargetForm::Parsed) == wanted)
                continue;
            const QString target = newTarget(link, named, wanted, file.newPath);
            if (target.isEmpty())
                continue;
            edits.append({link.from, link.to, target});
        }
        QString text = markdown::retargetLinks(content, edits);
        if (text != content)
            file.links = int(edits.size());
        if (!tagNames.isEmpty()) {
            const QString tagged = markdown::retargetTags(text, tagNames);
            if (tagged != text) {
                for (const markdown::NoteTag &t : markdown::findNoteTags(text)) {
                    if (tagNames.contains(t.name.toLower()))
                        ++file.tags;
                }
                file.tags = std::max(file.tags, 1);
                text = tagged;
            }
        }
        file.after = text;
        return file;
    }
};

RelocationPlan::RelocationPlan() = default;
RelocationPlan::~RelocationPlan() = default;
RelocationPlan::RelocationPlan(const RelocationPlan &) = default;
RelocationPlan &RelocationPlan::operator=(const RelocationPlan &) = default;

QList<FileMove> RelocationPlan::moves() const
{
    return d ? d->moves : QList<FileMove>();
}

const QList<RelocationPlan::File> &RelocationPlan::files() const
{
    return m_files;
}

int RelocationPlan::links() const
{
    int n = 0;
    for (const File &f : m_files)
        n += f.links;
    return n;
}

int RelocationPlan::tags() const
{
    int n = 0;
    for (const File &f : m_files)
        n += f.tags;
    return n;
}

QString RelocationPlan::rewrite(const VaultPath &path, const QString &content) const
{
    if (!d)
        return content;
    const VaultPath p = paths::normalize(path);
    for (const File &f : m_files) {
        if (f.path == p && f.before == content)
            return f.after;
    }
    const QString out = d->planFile(p, content).after;
    return out == content ? content : out;
}

// ---------------------------------------------------------------------------------------------
// VaultIndex
// ---------------------------------------------------------------------------------------------

std::optional<QString> VaultIndex::linkTarget(const VaultPath &rawPath, const VaultPath &from) const
{
    const VaultPath path = paths::normalize(rawPath);
    if (path.isEmpty() || !contains(path))
        return std::nullopt;
    return shortestLinkTarget(d->names, path, paths::normalize(from));
}

std::optional<QString> VaultIndex::tagTarget(const VaultPath &rawPath) const
{
    const VaultPath path = paths::normalize(rawPath);
    if (path.isEmpty() || !d->entry(path))
        return std::nullopt;
    return shortestTagTarget(d->names, path);
}

RelocationPlan VaultIndex::planRelocation(const QList<FileMove> &moves) const
{
    RelocationPlan plan;
    auto p = std::make_shared<RelocationPlan::Private>();
    struct Known {
        FileMove move;
        QStringList aliases;
        bool note = false;
    };
    QList<Known> known;
    for (const FileMove &m : moves) {
        const VaultPath from = paths::normalize(m.from);
        const VaultPath to = paths::normalize(m.to);
        if (from.isEmpty() || to.isEmpty() || from == to || p->moved.contains(from))
            continue;
        if (const Private::Entry *e = d->entry(from))
            known.append({{from, to}, e->note.aliases, true});
        else if (d->attachmentSet.contains(from))
            known.append({{from, to}, {}, false});
        else
            continue;
        p->moved.insert(from, to);
        p->moves.append({from, to});
    }
    if (known.isEmpty())
        return plan;
    p->before = d->names;
    p->after = d->names;
    // Every name a moved file answers to, before and after: the links filed under these keys are
    // the ones whose resolution can change.
    for (const Known &k : std::as_const(known)) {
        if (k.note) {
            p->after.removeNote(k.move.from, k.aliases);
            p->keys.insert(noteNameKey(k.move.from));
            p->keys.insert(noteNameKey(k.move.to));
            for (const QString &a : k.aliases) {
                for (const QString &key : aliasLinkKeys(a))
                    p->keys.insert(key);
            }
        } else {
            p->after.removeAttachment(k.move.from);
            p->keys.insert(fileNameKey(k.move.from));
            p->keys.insert(fileNameKey(k.move.to));
        }
    }
    for (const Known &k : std::as_const(known)) {
        if (k.note)
            p->after.addNote(k.move.to, k.aliases);
        else
            p->after.addAttachment(k.move.to);
    }

    // Tags resolve the same wherever they are written: one new name per tag.
    QSet<VaultPath> sources;
    for (auto it = d->tagIndex.cbegin(); it != d->tagIndex.cend(); ++it) {
        const QString name = tagName(it.key());
        if (name.isEmpty() || !p->keys.contains(linkKey(name, TargetForm::Parsed)))
            continue;
        const VaultPath named = p->before.resolveNote(name, TargetForm::Parsed);
        if (named.isEmpty() || !d->entry(named))
            continue;
        const VaultPath wanted = p->moved.value(named, named);
        if (p->after.resolveNote(name, TargetForm::Parsed) == wanted)
            continue;
        QString target;
        if (named != wanted) {
            // The tag's own page moved: its new name, or path, as renameTags() writes it.
            if (!name.contains(u'/')) {
                target = paths::pageTitle(wanted);
            } else if (sameFolder(named, wanted)) {
                const QStringList segments = stemOf(wanted).split(u'/');
                target = segments.mid(std::max<qsizetype>(0, segments.size() - name.count(u'/') - 1)).join(u'/');
            } else {
                target = stemOf(wanted);
            }
            if (markdown::tagSyntax(target).isEmpty() || p->after.resolveNote(target, TargetForm::Parsed) != wanted)
                target.clear();
        }
        if (target.isEmpty())
            target = shortestTagTarget(p->after, wanted).value_or(QString());
        if (target.isEmpty())
            continue;
        p->tagNames.insert(it.key(), target);
        for (auto n = it->cbegin(); n != it->cend(); ++n)
            sources.insert(n.key());
    }

    // The notes to look at: those with links filed under the keys, and the moved notes (their
    // relative links).
    for (const QString &key : std::as_const(p->keys)) {
        const auto it = d->sourcesByKey.constFind(key);
        if (it != d->sourcesByKey.cend())
            sources.unite(*it);
    }
    for (const Known &k : std::as_const(known)) {
        if (k.note)
            sources.insert(k.move.from);
    }
    QList<VaultPath> sorted(sources.cbegin(), sources.cend());
    std::sort(sorted.begin(), sorted.end());
    plan.d = p;
    for (const VaultPath &source : std::as_const(sorted)) {
        const Private::Entry *e = d->entry(source);
        if (!e)
            continue;
        RelocationPlan::File f = p->planFile(source, e->content);
        if (f.after != f.before)
            plan.m_files.append(std::move(f));
    }
    return plan;
}

} // namespace brulion
