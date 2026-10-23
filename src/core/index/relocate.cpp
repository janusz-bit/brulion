// Moving and renaming without breaking links (docs/SYNTAX.md §2 "Rewriting on rename and move"):
// VaultIndex::linkTarget() / tagTarget() (the shortest name that reaches a file) and
// VaultIndex::planRelocation() — the notes to rewrite so every link and tag keeps its target, and
// the ```query filters that name a moved page, folder or a renamed tag (tag:, ref:, page:, path:).
//
// The plan resolves every link that a move can affect twice: with the lookup tables as they are
// (before) and with a copy in which the moved files are at their new paths (after). A link whose
// file after the move is not the file it named before — moved along — is rewritten.
#include "index/query.h"
#include "index/textutil_p.h"
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
    /// Every file before the move (notes, then attachments): what a query's `path:` matches. Only
    /// filled when some note has a ```query block.
    QList<VaultPath> allPaths;

    /// The name a tag (`name`: tagName() of its key) is written with after the move — its page
    /// moved, or a moved page takes the name over —; "" when it keeps naming its page.
    QString newTagName(const QString &name) const
    {
        if (name.isEmpty() || !keys.contains(linkKey(name, TargetForm::Parsed)))
            return {};
        const VaultPath named = before.resolveNote(name, TargetForm::Parsed);
        if (named.isEmpty())
            return {};
        const VaultPath wanted = moved.value(named, named);
        if (after.resolveNote(name, TargetForm::Parsed) == wanted)
            return {};
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
            if (markdown::tagSyntax(target).isEmpty() || after.resolveNote(target, TargetForm::Parsed) != wanted)
                target.clear();
        }
        if (target.isEmpty())
            target = shortestTagTarget(after, wanted).value_or(QString());
        return target;
    }

    /// The new value of a ```query filter value in the note at `path` (at `newPath` after the
    /// move; `movedSource`: it moves itself); "" when it keeps its meaning.
    QString newQueryValue(const QueryNameRef &q, const VaultPath &path, const VaultPath &newPath,
                          bool movedSource) const
    {
        switch (q.kind) {
        case QueryFilterKind::Tag: {
            // Exactly the tag: `tag:old/x` (a sub-tag, a page of its own) is not `old`.
            const QString key = nfc(q.value).toLower();
            if (const auto it = tagNames.constFind(key); it != tagNames.cend())
                return *it;
            return newTagName(tagName(key)); // a tag no note carries yet
        }
        case QueryFilterKind::Ref:
        case QueryFilterKind::Page: {
            const QString value = q.value;
            if (isThisPageValue(value) || (q.kind == QueryFilterKind::Page && value.contains(u'*')))
                return {};
            // Resolved like a link written in the note (runQuery: resolveLink from the query's
            // page); a `#Heading` / `|alias` after the name stays.
            qsizetype cut = value.size();
            for (const QChar c : {u'#', u'|'}) {
                const qsizetype at = value.indexOf(c);
                if (at >= 0)
                    cut = std::min(cut, at);
            }
            const QString target = value.left(cut).trimmed();
            if (target.isEmpty() || (!movedSource && !keys.contains(linkKey(target, TargetForm::Raw))))
                return {};
            const VaultPath named = before.resolve(target, path, TargetForm::Raw);
            if (named.isEmpty())
                return {}; // a missing page stays missing
            const VaultPath wanted = moved.value(named, named);
            if (after.resolve(target, newPath, TargetForm::Raw) == wanted)
                return {};
            if (q.kind == QueryFilterKind::Page) {
                // `page:` names the moved page itself, also by its title or path (folded).
                if (named == wanted)
                    return {};
                const QString folded = text::foldKey(target);
                if (folded == text::foldKey(paths::pageTitle(wanted)) || folded == text::foldKey(stemOf(wanted)))
                    return {};
            }
            Link link;
            link.kind = LinkKind::Wiki;
            link.target = target;
            const QString written = newTarget(link, named, wanted, newPath);
            return written.isEmpty() ? QString() : written + value.mid(cut);
        }
        case QueryFilterKind::Path: return newPathValue(q.value);
        default: return {};
        }
    }

    /// A `path:` prefix every file of which moved by one folder change ("Projekty/" ->
    /// "Archiwum/Projekty/", "Projekty/Dom" -> "Archiwum/Dom" for the one page it matched): the new
    /// prefix; "" when a matched file stays, the files went different ways or nothing matched.
    QString newPathValue(const QString &value) const
    {
        qsizetype skip = 0; // "./" and "/" as written stay
        while (value.sliced(skip).startsWith(u"./"))
            skip += 2;
        while (value.sliced(skip).startsWith(u'/'))
            ++skip;
        const QString prefix = value.mid(skip);
        if (prefix.isEmpty() || prefix.contains(u'*'))
            return {};
        const QString folded = text::foldKey(prefix);
        QString newPrefix;
        bool any = false;
        for (const VaultPath &file : allPaths) {
            if (!text::foldKey(file).startsWith(folded))
                continue; // not matched (runQuery's pathMatches)
            const auto to = moved.constFind(file);
            if (to == moved.cend() || !file.startsWith(prefix, Qt::CaseInsensitive))
                return {}; // a file that stays (or matched only without its diacritics)
            const QString rest = file.mid(prefix.size());
            if (!to->endsWith(rest))
                return {};
            const QString candidate = to->left(to->size() - rest.size());
            if (any && candidate != newPrefix)
                return {};
            newPrefix = candidate;
            any = true;
        }
        if (!any || newPrefix.isEmpty() || newPrefix == prefix)
            return {};
        return value.left(skip) + newPrefix;
    }

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

    /// `parsed`: parseNote() of `content` when the caller has it (the index's own).
    File planFile(const VaultPath &path, const QString &content, const ParsedNote *parsed = nullptr) const
    {
        File file;
        file.path = path;
        file.newPath = moved.value(path, path);
        file.before = content;
        const bool movedSource = moved.contains(path);
        QList<markdown::LinkEdit> edits;
        std::optional<ParsedNote> own;
        if (!parsed)
            own = markdown::parseNote(QString(), content);
        const ParsedNote &note = parsed ? *parsed : *own;
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
        // The ```query filters naming a moved file or a renamed tag (code: no link or tag above
        // touched them).
        QList<QueryNameEdit> queryEdits;
        int queryTags = 0;
        for (const QueryNameRef &q : findQueryNames(text)) {
            const QString value = newQueryValue(q, path, file.newPath, movedSource);
            if (value.isEmpty() || value == q.value || formatQueryName(q, value).isEmpty())
                continue;
            queryEdits.append({q.from, q.to, value});
            if (q.kind == QueryFilterKind::Tag)
                ++queryTags;
        }
        if (!queryEdits.isEmpty()) {
            int applied = 0;
            text = retargetQueryNames(text, queryEdits, &applied);
            file.queries = applied; // each edit was checked above: all of them
            file.queryTags = std::min(queryTags, applied);
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
        n += f.links + f.queries - f.queryTags;
    return n;
}

int RelocationPlan::tags() const
{
    int n = 0;
    for (const File &f : m_files)
        n += f.tags + f.queryTags;
    return n;
}

int RelocationPlan::queries() const
{
    int n = 0;
    for (const File &f : m_files)
        n += f.queries;
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
        const QString target = p->newTagName(name);
        if (target.isEmpty())
            continue;
        p->tagNames.insert(it.key(), target);
        for (auto n = it->cbegin(); n != it->cend(); ++n)
            sources.insert(n.key());
    }
    // The notes with ```query blocks: their filters may name a moved file or a renamed tag.
    const QList<VaultPath> &queryNotes = d->queryNotes();
    if (!queryNotes.isEmpty()) {
        p->allPaths = paths();
        p->allPaths.append(attachments());
        for (const VaultPath &q : queryNotes)
            sources.insert(q);
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
        RelocationPlan::File f = p->planFile(source, e->content, &e->note);
        if (f.after != f.before)
            plan.m_files.append(std::move(f));
    }
    return plan;
}

} // namespace brulion
