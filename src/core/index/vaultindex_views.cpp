// VaultIndex: read-only views (notes, links, backlinks, unlinked references, tags, tasks,
// reminders, journals, search, suggestions, graph). Maintenance lives in vaultindex.cpp.
#include "index/vaultindex.h"
#include "index/vaultindex_p.h"

#include "index/suggest_p.h"
#include "index/textutil_p.h"
#include "index/unlinked_p.h"
#include "markdown/inline.h"
#include "markdown/parse.h"
#include "markdown/properties.h"
#include "text/text.h"
#include "util/paths.h"

#include <QSet>

#include <algorithm>

namespace brulion {

using namespace index_detail;
using namespace Qt::StringLiterals;

namespace {

/// First line after the frontmatter (YAML or leading `key:: value` lines), else 0.
int bodyStartLine(const ParsedNote &note)
{
    if (note.blocks.isEmpty() || note.blocks.first().kind != BlockKind::Frontmatter)
        return 0;
    return note.blocks.first().endLine + 1;
}

/// Can a block be offered for `((` completion? (Runs for every block on every key press.)
bool suggestable(const Block &block)
{
    return block.kind != BlockKind::Frontmatter && !QStringView(block.text).trimmed().isEmpty();
}

struct Excerpt {
    int line = -1;
    QString text;
};

/// First line of `content` at or after `fromLine` containing one of the folded `terms` as a
/// whole token, with an excerpt around the earliest such term.
std::optional<Excerpt> findExcerpt(const QString &content, const QStringList &terms, int fromLine)
{
    if (terms.isEmpty())
        return std::nullopt;
    const QList<text::Line> lines = text::splitLines(content);
    for (qsizetype i = std::max(0, fromLine); i < lines.size(); ++i) {
        const QString &line = lines[i].text;
        const text::FoldedText folded = text::foldKeyWithMap(line);
        qsizetype best = -1;
        qsizetype bestLength = 0;
        for (const QString &t : terms) {
            const qsizetype k = indexOfToken(folded.text, t);
            if (k >= 0 && (best < 0 || k < best)) {
                best = k;
                bestLength = t.size();
            }
        }
        if (best < 0)
            continue;
        const qsizetype start = folded.toOriginal(best);
        const qsizetype end = folded.toOriginal(best + bestLength);
        return Excerpt{int(i), lineExcerpt(line, start, end)};
    }
    return std::nullopt;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Tags as page links (SYNTAX §3)
// ---------------------------------------------------------------------------------------------

namespace index_detail {

QString tagName(const QString &tag)
{
    QString t = tag.trimmed();
    if (t.startsWith(u'#'))
        t.remove(0, 1);
    if (t.startsWith(u"[["_s) && t.endsWith(u"]]"_s) && t.size() >= 4)
        t = t.sliced(2, t.size() - 4);
    t = t.trimmed();
    while (t.endsWith(u'/'))
        t.chop(1);
    return nfc(t.trimmed());
}

QStringList pageLevelTags(const ParsedNote &note)
{
    // Frontmatter `tags`, the leading `tags::` block, and tags not found in any ordinary block.
    QSet<QString> inBlocks;
    QSet<QString> inFront;
    for (const Block &b : note.blocks) {
        for (const QString &t : b.tags)
            (b.kind == BlockKind::Frontmatter ? inFront : inBlocks).insert(t);
    }
    QSet<QString> fm;
    const QVariant fmTags = markdown::frontmatterGet(note.frontmatter, {u"tags"_s, u"tag"_s});
    for (QString name : markdown::namesFrom(fmTags, true)) {
        name = name.trimmed();
        if (name.startsWith(u'#'))
            name.remove(0, 1);
        fm.insert(nfc(name).toLower());
    }
    QStringList out;
    for (const QString &t : note.tags) {
        if (inFront.contains(t) || fm.contains(t) || !inBlocks.contains(t))
            out.append(t);
    }
    return out;
}

} // namespace index_detail

VaultPath VaultIndex::Private::resolveTagPath(const QString &tag) const
{
    const QString name = tagName(tag);
    if (name.isEmpty())
        return {};
    // Like [[name]] written at the vault root, among notes: one tag names one page wherever it is
    // written, and a file of that name that is no note never hides one.
    const VaultPath hit = names.resolveNote(name, TargetForm::Parsed);
    return entry(hit) ? hit : VaultPath();
}

const QList<VaultPath> &VaultIndex::Private::queryNotes()
{
    if (!cache.queryNotes) {
        QList<VaultPath> out;
        for (const Entry *e : sortedEntries()) {
            if (hasQueryBlocks(e->content, e->note))
                out.append(e->path);
        }
        cache.queryNotes = std::move(out);
    }
    return *cache.queryNotes;
}

const TagTargets &VaultIndex::Private::tagTargets()
{
    if (!cache.tagTargets) {
        TagTargets t;
        for (auto it = tagIndex.cbegin(); it != tagIndex.cend(); ++it) {
            const VaultPath page = resolveTagPath(it.key());
            if (page.isEmpty())
                continue;
            t.byTag.insert(it.key(), page);
            t.byPage[page].append(it.key());
        }
        for (QStringList &keys : t.byPage)
            std::sort(keys.begin(), keys.end());
        cache.tagTargets = std::move(t);
    }
    return *cache.tagTargets;
}

QStringList VaultIndex::Private::tagKeysFor(const VaultPath &path)
{
    if (path.isEmpty() || !paths::isMarkdown(path)) // tags name notes, never attachments
        return {};
    const TagTargets &targets = tagTargets();
    if (entry(path))
        return targets.byPage.value(path);
    // A page that does not exist yet: the unresolved tags that would name it once written.
    const QString key = pathKey(paths::stripMarkdownExtension(paths::normalize(path)));
    if (key.isEmpty())
        return {};
    QStringList out;
    for (auto it = tagIndex.cbegin(); it != tagIndex.cend(); ++it) {
        const QString &tag = it.key();
        if (!targets.byTag.contains(tag) && (key == tag || key.endsWith(u'/' + tag)))
            out.append(tag);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ---------------------------------------------------------------------------------------------
// Notes
// ---------------------------------------------------------------------------------------------

const ParsedNote *VaultIndex::note(const VaultPath &path) const
{
    const Private::Entry *e = d->entry(path);
    return e ? &e->note : nullptr;
}

QList<VaultPath> VaultIndex::paths() const
{
    if (!d->cache.paths) {
        QList<VaultPath> out;
        for (const Private::Entry *e : d->sortedEntries())
            out.append(e->path);
        d->cache.paths = std::move(out);
    }
    return *d->cache.paths;
}

QList<VaultPath> VaultIndex::attachments() const
{
    if (!d->cache.attachments) {
        QList<VaultPath> out(d->attachmentSet.cbegin(), d->attachmentSet.cend());
        std::sort(out.begin(), out.end());
        d->cache.attachments = std::move(out);
    }
    return *d->cache.attachments;
}

QList<VaultPath> VaultIndex::notesWithQueries() const
{
    return d->queryNotes();
}

bool VaultIndex::contains(const VaultPath &path) const
{
    return d->entry(path) || d->attachmentSet.contains(path);
}

QString VaultIndex::content(const VaultPath &path) const
{
    const Private::Entry *e = d->entry(path);
    return e ? e->content : QString();
}

// ---------------------------------------------------------------------------------------------
// Links
// ---------------------------------------------------------------------------------------------

std::optional<VaultPath> VaultIndex::resolveLink(const QString &target, const VaultPath &from) const
{
    const VaultPath hit = d->names.resolve(target, from);
    if (hit.isEmpty() || !contains(hit))
        return std::nullopt;
    return hit;
}

QList<ResolvedLink> VaultIndex::outgoingLinks(const VaultPath &path) const
{
    QList<ResolvedLink> out;
    const Private::Entry *e = d->entry(path);
    if (!e)
        return out;
    for (const PathLink &l : e->links)
        out.append(ResolvedLink{e->note.links[l.link], l.target});
    for (const int r : e->refs) {
        const Link &link = e->note.links[r];
        out.append(ResolvedLink{link, d->blockOwner(link.blockId)});
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const ResolvedLink &a, const ResolvedLink &b) { return a.link.from < b.link.from; });
    return out;
}

QList<Backlink> VaultIndex::backlinks(const VaultPath &path) const
{
    struct Found {
        const Private::Entry *entry;
        const Link *link;
    };
    QList<Found> found;
    if (!d->entry(path) && !d->attachmentSet.contains(path) && !path.isEmpty()) {
        // A page that does not exist yet: the links that would resolve to it.
        const QString nameKey = noteNameKey(path);
        const QString key = pathKey(paths::stripMarkdownExtension(paths::normalize(path)));
        if (const auto un = d->unresolved.constFind(nameKey); un != d->unresolved.constEnd()) {
            for (auto it = un->cbegin(); it != un->cend(); ++it) {
                const Private::Entry *src = d->entry(it.key());
                if (!src)
                    continue;
                for (const PathLink &l : src->links) {
                    if (!l.target.isEmpty() || l.key != nameKey)
                        continue;
                    const QString &target = src->note.links[l.link].target;
                    if (!isPageTarget(target, TargetForm::Parsed))
                        continue;
                    const QString written = pathKey(
                            paths::stripMarkdownExtension(paths::normalize(cleanTarget(target, TargetForm::Parsed))));
                    if (written == key || key.endsWith(u'/' + written))
                        found.append(Found{src, &src->note.links[l.link]});
                }
            }
        }
    }
    if (const auto in = d->incoming.constFind(path); in != d->incoming.constEnd()) {
        for (auto it = in->cbegin(); it != in->cend(); ++it) {
            if (it.key() == path)
                continue;
            const Private::Entry *src = d->entry(it.key());
            if (!src)
                continue;
            for (const PathLink &l : src->links) {
                if (l.target == path)
                    found.append(Found{src, &src->note.links[l.link]});
            }
        }
    }
    if (const Private::Entry *e = d->entry(path)) {
        for (auto id = e->note.blockIds.cbegin(); id != e->note.blockIds.cend(); ++id) {
            if (d->blockOwner(id.key()) != path)
                continue;
            const auto sources = d->blockRefSources.constFind(id.key());
            if (sources == d->blockRefSources.constEnd())
                continue;
            for (const VaultPath &s : *sources) {
                const Private::Entry *src = s == path ? nullptr : d->entry(s);
                if (!src)
                    continue;
                for (const int r : src->refs) {
                    if (src->note.links[r].blockId == id.key())
                        found.append(Found{src, &src->note.links[r]});
                }
            }
        }
    }
    std::sort(found.begin(), found.end(), [](const Found &a, const Found &b) {
        if (a.entry != b.entry)
            return relevanceLess(*a.entry, *b.entry);
        return a.link->from < b.link->from;
    });
    QList<Backlink> out;
    out.reserve(found.size());
    for (const Found &f : std::as_const(found))
        out.append(Backlink{f.entry->path, *f.link, markdown::blockAtLine(f.entry->note, f.link->line)});
    return out;
}

QList<VaultPath> VaultIndex::linksTo(const VaultPath &path) const
{
    QList<VaultPath> out;
    const auto in = d->incoming.constFind(path);
    if (in == d->incoming.constEnd())
        return out;
    for (auto it = in->cbegin(); it != in->cend(); ++it) {
        if (it.key() == path) {
            const Private::Entry *self = d->entry(path);
            const bool byName = self && std::any_of(self->links.cbegin(), self->links.cend(), [&](const PathLink &l) {
                                    return l.target == path && !l.key.isEmpty();
                                });
            if (!byName)
                continue;
        }
        out.append(it.key());
    }
    std::sort(out.begin(), out.end());
    return out;
}

QList<UnlinkedRef> VaultIndex::unlinkedReferences(const VaultPath &path) const
{
    QList<UnlinkedRef> out;
    const Private::Entry *e = d->entry(path);
    if (!e)
        return out;
    const QList<MentionMatcher> matchers = mentionMatchers(QStringList{e->note.title} + e->note.aliases);
    if (matchers.isEmpty())
        return out;
    for (const Private::Entry *other : d->entriesByRelevance()) {
        if (other->path == path)
            continue;
        const QString &lower = other->lowerContent();
        const bool candidate = std::any_of(matchers.cbegin(), matchers.cend(),
                                           [&](const MentionMatcher &m) { return lower.contains(m.firstWord); });
        if (!candidate)
            continue;
        const QList<Mention> mentions =
                findMentions(other->content, other->note, matchers, MaxUnlinkedReferences - out.size());
        for (const Mention &m : mentions)
            out.append(UnlinkedRef{other->path, m.line, m.excerpt});
        if (out.size() >= MaxUnlinkedReferences)
            break;
    }
    return out;
}

QList<MissingPage> VaultIndex::missingPages() const
{
    if (d->cache.missing)
        return *d->cache.missing;
    QList<MissingPage> out;
    for (auto it = d->unresolved.cbegin(); it != d->unresolved.cend(); ++it) {
        QList<VaultPath> sources = it->keys();
        std::sort(sources.begin(), sources.end());
        // Spelling of the first unresolved link with this key (sources in path order).
        QString name;
        bool page = false;
        for (const VaultPath &s : std::as_const(sources)) {
            const Private::Entry *e = d->entry(s);
            if (!e)
                continue;
            const auto l = std::find_if(e->links.cbegin(), e->links.cend(),
                                        [&](const PathLink &pl) { return pl.key == it.key() && pl.target.isEmpty(); });
            if (l == e->links.cend())
                continue;
            const QString &target = e->note.links[l->link].target;
            page = isPageTarget(target, TargetForm::Parsed);
            name = nfc(cleanTarget(target, TargetForm::Parsed));
            break;
        }
        if (page && !name.isEmpty())
            out.append(MissingPage{name, sources});
    }
    std::sort(out.begin(), out.end(), [](const MissingPage &a, const MissingPage &b) {
        if (a.sources.size() != b.sources.size())
            return a.sources.size() > b.sources.size();
        return compareNames(a.name, b.name) < 0;
    });
    d->cache.missing = out;
    return out;
}

// ---------------------------------------------------------------------------------------------
// Blocks, tags, tasks, reminders, journals
// ---------------------------------------------------------------------------------------------

std::optional<BlockRef> VaultIndex::blockById(const QString &id) const
{
    QString clean = id.trimmed();
    if (clean.startsWith(u"((") && clean.endsWith(u"))") && clean.size() >= 4)
        clean = clean.sliced(2, clean.size() - 4).trimmed();
    if (clean.startsWith(u'^'))
        clean.remove(0, 1);
    if (clean.isEmpty())
        return std::nullopt;
    const VaultPath owner = d->blockOwner(clean);
    const Private::Entry *e = owner.isEmpty() ? nullptr : d->entry(owner);
    if (!e)
        return std::nullopt;
    const int index = e->note.blockIds.value(clean, -1);
    if (index < 0)
        return std::nullopt;
    return BlockRef{owner, index};
}

QList<TagCount> VaultIndex::tags() const
{
    if (d->cache.tags)
        return *d->cache.tags;
    QList<TagCount> out;
    out.reserve(d->tagIndex.size());
    for (auto it = d->tagIndex.cbegin(); it != d->tagIndex.cend(); ++it) {
        TagCount t;
        t.tag = it.key();
        for (auto p = it->cbegin(); p != it->cend(); ++p) {
            t.count += p.value();
            t.paths.append(p.key());
        }
        std::sort(t.paths.begin(), t.paths.end());
        for (const VaultPath &p : std::as_const(t.paths)) {
            const Private::Entry *e = d->entry(p);
            const QString spelled = e ? e->tagSpellings().value(t.tag) : QString();
            if (!spelled.isEmpty()) {
                t.display = spelled;
                break;
            }
        }
        if (t.display.isEmpty())
            t.display = t.tag;
        out.append(std::move(t));
    }
    std::sort(out.begin(), out.end(), [](const TagCount &a, const TagCount &b) {
        if (a.count != b.count)
            return a.count > b.count;
        return compareNames(a.tag, b.tag) < 0;
    });
    d->cache.tags = out;
    return out;
}

std::optional<VaultPath> VaultIndex::resolveTag(const QString &tag) const
{
    const VaultPath page = d->resolveTagPath(tag);
    if (page.isEmpty())
        return std::nullopt;
    return page;
}

QStringList VaultIndex::tagsNaming(const VaultPath &path) const
{
    return d->tagKeysFor(path);
}

QList<TagReference> VaultIndex::tagReferences(const VaultPath &path) const
{
    struct Found {
        const Private::Entry *entry;
        int block;
        QString tag;
    };
    QList<Found> found;
    QHash<const Private::Entry *, QStringList> pageLevel;
    for (const QString &key : d->tagKeysFor(path)) {
        const auto notes = d->tagIndex.constFind(key);
        if (notes == d->tagIndex.constEnd())
            continue;
        for (auto it = notes->cbegin(); it != notes->cend(); ++it) {
            const Private::Entry *e = it.key() == path ? nullptr : d->entry(it.key());
            if (!e)
                continue;
            auto level = pageLevel.find(e);
            if (level == pageLevel.end())
                level = pageLevel.insert(e, pageLevelTags(e->note));
            if (level->contains(key))
                found.append(Found{e, -1, key});
            const QList<Block> &blocks = e->note.blocks;
            for (qsizetype i = 0; i < blocks.size(); ++i) {
                if (blocks[i].kind != BlockKind::Frontmatter && blocks[i].tags.contains(key))
                    found.append(Found{e, int(i), key});
            }
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const Found &a, const Found &b) {
        if (a.entry != b.entry)
            return relevanceLess(*a.entry, *b.entry);
        return a.block < b.block;
    });
    QList<TagReference> out;
    out.reserve(found.size());
    for (const Found &f : std::as_const(found)) {
        if (!out.isEmpty() && out.last().path == f.entry->path && out.last().blockIndex == f.block)
            continue; // one entry per block (or page), whichever tags name the page
        out.append(TagReference{f.entry->path, f.block, f.tag});
    }
    return out;
}

QList<VaultPath> VaultIndex::tagsTo(const VaultPath &path) const
{
    QSet<VaultPath> files;
    for (const QString &key : d->tagKeysFor(path)) {
        const auto notes = d->tagIndex.constFind(key);
        if (notes == d->tagIndex.constEnd())
            continue;
        for (auto it = notes->cbegin(); it != notes->cend(); ++it)
            files.insert(it.key());
    }
    QList<VaultPath> out(files.cbegin(), files.cend());
    std::sort(out.begin(), out.end());
    return out;
}

QList<TaskRef> VaultIndex::tasks() const
{
    if (d->cache.tasks)
        return *d->cache.tasks;
    QList<TaskRef> out;
    for (const Private::Entry *e : d->sortedEntries()) {
        if (e->note.tasks.isEmpty())
            continue;
        const QList<Block> &blocks = e->note.blocks;
        for (qsizetype i = 0; i < blocks.size(); ++i) {
            if (blocks[i].task)
                out.append(TaskRef{e->path, int(i), *blocks[i].task});
        }
    }
    d->cache.tasks = out;
    return out;
}

QList<ReminderRef> VaultIndex::reminders() const
{
    if (d->cache.reminders)
        return *d->cache.reminders;
    QList<ReminderRef> out;
    for (const Private::Entry *e : d->sortedEntries()) {
        if (e->note.reminders.isEmpty())
            continue;
        const QList<Block> &blocks = e->note.blocks;
        for (qsizetype i = 0; i < blocks.size(); ++i) {
            for (const Reminder &r : blocks[i].reminders)
                out.append(ReminderRef{e->path, int(i), r, blocks[i].task});
        }
    }
    d->cache.reminders = out;
    return out;
}

QList<JournalRef> VaultIndex::journals() const
{
    if (d->cache.journals)
        return *d->cache.journals;
    QList<JournalRef> out;
    for (const auto &[path, e] : d->entries) {
        if (e->note.journalDate)
            out.append(JournalRef{*e->note.journalDate, path});
    }
    std::sort(out.begin(), out.end(), [](const JournalRef &a, const JournalRef &b) {
        if (a.date != b.date)
            return a.date > b.date;
        return a.path < b.path;
    });
    d->cache.journals = out;
    return out;
}

// ---------------------------------------------------------------------------------------------
// Search and suggestions
// ---------------------------------------------------------------------------------------------

QList<SearchHit> VaultIndex::search(const QString &query, int limit) const
{
    QList<SearchHit> out;
    if (limit <= 0 || query.trimmed().isEmpty())
        return out;
    d->flushSearch();
    for (const SearchIndex::Hit &raw : d->search.search(query, limit)) {
        const Private::Entry *e = d->entry(raw.id);
        if (!e)
            continue;
        SearchHit hit{raw.id, e->note.title, raw.score, -1, {}};
        // Prefer a match in the body over one in the frontmatter (aliases, tags).
        const int body = bodyStartLine(e->note);
        std::optional<Excerpt> found = body > 0 ? findExcerpt(e->content, raw.terms, body) : std::nullopt;
        if (!found)
            found = findExcerpt(e->content, raw.terms, 0);
        if (found) {
            hit.line = found->line;
            hit.excerpt = found->text;
        } else {
            const auto first = std::find_if(e->note.blocks.cbegin(), e->note.blocks.cend(), suggestable);
            if (first != e->note.blocks.cend())
                hit.excerpt = excerptStart(first->text);
        }
        out.append(std::move(hit));
    }
    return out;
}

QList<PageSuggestion> VaultIndex::suggestPages(const QString &prefix, int limit) const
{
    QList<PageSuggestion> out;
    if (limit <= 0)
        return out;
    const QString q = foldQuery(prefix);
    if (q.isEmpty()) {
        QList<const Private::Entry *> recent = d->sortedEntries();
        std::stable_sort(recent.begin(), recent.end(),
                         [](const Private::Entry *a, const Private::Entry *b) { return a->seq > b->seq; });
        for (qsizetype i = 0; i < std::min<qsizetype>(limit, recent.size()); ++i)
            out.append(PageSuggestion{recent[i]->path, recent[i]->note.title, {}});
        return out;
    }
    const bool withPath = q.contains(u'/');
    const QStringList queryWords = words(q);
    const QString noAlias;
    struct Scored {
        PageSuggestion s;
        NameRank rank;
        bool alias;
        QString name;
        quint64 seq;
    };
    QList<Scored> scored;
    for (const auto &[path, e] : d->entries) {
        NameRank best = NameRank::None;
        const QString *bestName = nullptr;
        const QString *bestAlias = nullptr;
        for (const NameCandidate &c : std::as_const(e->names)) {
            const NameRank r = rankName(c.folded, q, queryWords);
            if (r > best) { // titles win ties against aliases (listed first)
                best = r;
                bestName = &c.folded;
                bestAlias = &c.alias;
            }
        }
        if (withPath) {
            const NameRank r = rankName(e->foldedPath, q, queryWords);
            if (r > best) {
                best = r;
                bestName = &e->foldedPath;
                bestAlias = &noAlias;
            }
        }
        if (best == NameRank::None || !bestName)
            continue;
        scored.append(Scored{PageSuggestion{path, e->note.title, *bestAlias}, best, !bestAlias->isEmpty(), *bestName,
                             e->seq});
    }
    std::sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) {
        if (a.rank != b.rank)
            return a.rank > b.rank;
        if (a.alias != b.alias)
            return !a.alias;
        if (a.name.size() != b.name.size())
            return a.name.size() < b.name.size();
        // Same name: the page `[[name]]` resolves to (shortest path) first; else most recent.
        if (a.name == b.name) {
            if (a.s.path.size() != b.s.path.size())
                return a.s.path.size() < b.s.path.size();
        } else if (a.seq != b.seq) {
            return a.seq > b.seq;
        }
        if (const int c = compareNames(a.s.title, b.s.title); c != 0)
            return c < 0;
        return a.s.path < b.s.path;
    });
    for (qsizetype i = 0; i < std::min<qsizetype>(limit, scored.size()); ++i)
        out.append(scored[i].s);
    return out;
}

QList<TagSuggestion> VaultIndex::suggestTags(const QString &prefix, int limit) const
{
    QList<TagSuggestion> out;
    if (limit <= 0)
        return out;
    const QStringView typed = prefix.startsWith(u'#') ? QStringView(prefix).mid(1) : QStringView(prefix);
    const QString key = text::foldKey(typed); // what was typed matches without diacritics …
    // … but a tag names a page with them (#zolw is not Żółw): one entry per tag key.
    const auto tagKey = [](const QString &name) { return nfc(name).toLower(); };
    QSet<QString> seen; // tag keys offered so far
    const auto tagItem = [&](const TagCount &t) {
        TagSuggestion s;
        s.tag = t.display.isEmpty() ? t.tag : t.display;
        s.count = t.count;
        s.path = resolveTag(t.tag).value_or(QString());
        seen.insert(t.tag);
        return s;
    };
    // Tags in use (most used first) that start with the text, then pages no tag names yet, then
    // tags that contain the text. A page whose tag is in use is offered as that tag.
    QHash<QString, const TagCount *> contains;
    QList<const TagCount *> containsOrder;
    const QList<TagCount> all = tags();
    for (const TagCount &t : all) {
        const QString folded = text::foldKey(t.tag);
        if (key.isEmpty() || folded.startsWith(key)) {
            out.append(tagItem(t));
        } else if (folded.contains(key)) {
            contains.insert(t.tag, &t);
            containsOrder.append(&t);
        }
        if (out.size() >= limit)
            return out;
    }
    for (const PageSuggestion &s : suggestPages(typed.toString(), limit)) {
        // What was typed is in the name (no fuzzy matches among the tags).
        if (!key.isEmpty() && !text::foldKey(s.alias.isEmpty() ? s.title : s.alias).contains(key)
            && !text::foldKey(s.path).contains(key))
            continue;
        // The name of the tag: the alias that matched or the title — or, when that name is another
        // page's (a duplicate base name), the shortest tag that names this one.
        QString name = s.alias.isEmpty() ? s.title : s.alias;
        if (resolveTag(name) != s.path)
            name = tagTarget(s.path).value_or(QString());
        const QString k = tagKey(name);
        if (name.isEmpty() || seen.contains(k) || markdown::tagSyntax(name).isEmpty())
            continue;
        if (const TagCount *inUse = contains.value(k)) {
            out.append(tagItem(*inUse));
        } else {
            seen.insert(k);
            TagSuggestion page;
            page.tag = name;
            page.page = true;
            page.path = s.path;
            out.append(page);
        }
        if (out.size() >= limit)
            return out;
    }
    for (const TagCount *t : std::as_const(containsOrder)) {
        if (out.size() >= limit)
            break;
        if (!seen.contains(t->tag))
            out.append(tagItem(*t));
    }
    return out;
}

QList<BlockRef> VaultIndex::suggestBlocks(const QString &text, int limit) const
{
    QList<BlockRef> out;
    if (limit <= 0)
        return out;
    const QStringList wordsOfQuery = foldQuery(text).split(u' ', Qt::SkipEmptyParts);
    QList<const Private::Entry *> byRecency = d->sortedEntries();
    std::stable_sort(byRecency.begin(), byRecency.end(),
                     [](const Private::Entry *a, const Private::Entry *b) { return a->seq > b->seq; });
    if (wordsOfQuery.isEmpty()) {
        QList<BlockRef> rest;
        for (const Private::Entry *e : std::as_const(byRecency)) {
            const QList<Block> &blocks = e->note.blocks;
            for (qsizetype i = 0; i < blocks.size(); ++i) {
                if (!suggestable(blocks[i]))
                    continue;
                if (!blocks[i].id.isEmpty())
                    out.append(BlockRef{e->path, int(i)});
                else if (rest.size() < limit)
                    rest.append(BlockRef{e->path, int(i)});
            }
            if (out.size() >= limit)
                break;
        }
        out.append(rest);
        out.resize(std::min<qsizetype>(limit, out.size()));
        return out;
    }
    struct Scored {
        BlockRef ref;
        int score;
        qsizetype rank;
        int line;
    };
    QList<Scored> scored;
    const QString &first = wordsOfQuery.first();
    for (qsizetype rank = 0; rank < byRecency.size(); ++rank) {
        const Private::Entry *e = byRecency[rank];
        const QStringList &folded = e->foldedBlockTexts();
        const QList<Block> &blocks = e->note.blocks;
        for (qsizetype i = 0; i < blocks.size(); ++i) {
            // The word test first: it rejects almost every block, without touching the Block.
            const QString &f = folded[i];
            const bool all = std::all_of(wordsOfQuery.cbegin(), wordsOfQuery.cend(),
                                         [&](const QString &w) { return f.contains(w); });
            if (!all || !suggestable(blocks[i]))
                continue;
            const int quality = f.startsWith(first) ? 2 : (indexOfWordStart(f, first) >= 0 ? 1 : 0);
            const int score = (blocks[i].id.isEmpty() ? 0 : 4) + quality;
            scored.append(Scored{BlockRef{e->path, int(i)}, score, rank, blocks[i].line});
        }
    }
    std::stable_sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.rank != b.rank)
            return a.rank < b.rank;
        return a.line < b.line;
    });
    for (qsizetype i = 0; i < std::min<qsizetype>(limit, scored.size()); ++i)
        out.append(scored[i].ref);
    return out;
}

// ---------------------------------------------------------------------------------------------
// Graph
// ---------------------------------------------------------------------------------------------

GraphData VaultIndex::graph(bool includeTags, bool includeMissing) const
{
    GraphData out;
    QHash<QString, qsizetype> nodeIndex;
    QSet<QPair<QString, QString>> edgeSet;
    QHash<QString, QString> missingIds; ///< lower-cased name -> node id
    auto addNode = [&](GraphNode node) {
        nodeIndex.insert(node.id, out.nodes.size());
        out.nodes.append(std::move(node));
    };
    auto addEdge = [&](const QString &source, const QString &target) {
        if (source == target)
            return;
        const QPair<QString, QString> key{source, target};
        if (edgeSet.contains(key))
            return;
        edgeSet.insert(key);
        out.edges.append(GraphEdge{source, target});
        ++out.nodes[nodeIndex.value(source)].degree;
        ++out.nodes[nodeIndex.value(target)].degree;
    };
    const QList<const Private::Entry *> &sorted = d->sortedEntries();
    const TagTargets &tagTargets = d->tagTargets();
    for (const Private::Entry *e : sorted)
        addNode(GraphNode{e->path, e->note.title, 0, false, false});
    for (const Private::Entry *e : sorted) {
        for (const PathLink &l : e->links) {
            if (!l.target.isEmpty()) {
                if (d->entry(l.target))
                    addEdge(e->path, l.target);
                continue;
            }
            const QString &target = e->note.links[l.link].target;
            if (!includeMissing || l.key.isEmpty() || !isPageTarget(target, TargetForm::Parsed))
                continue;
            const QString name = nfc(cleanTarget(target, TargetForm::Parsed));
            const QString lower = name.toLower();
            QString id = missingIds.value(lower);
            if (id.isEmpty()) {
                id = nodeIndex.contains(name) ? u"missing:"_s + name : name;
                missingIds.insert(lower, id);
                addNode(GraphNode{id, paths::pageTitle(name), 0, false, true});
            }
            addEdge(e->path, id);
        }
        for (const int r : e->refs) {
            const VaultPath owner = d->blockOwner(e->note.links[r].blockId);
            if (!owner.isEmpty())
                addEdge(e->path, owner);
        }
        // A tag that names a note is a link to it; the others are "#tag" nodes on request.
        for (const QString &t : e->note.tags) {
            if (const auto page = tagTargets.byTag.constFind(t); page != tagTargets.byTag.constEnd()) {
                addEdge(e->path, *page);
                continue;
            }
            if (!includeTags)
                continue;
            const QString id = u"#"_s + t;
            if (!nodeIndex.contains(id))
                addNode(GraphNode{id, id, 0, true, false});
            addEdge(e->path, id);
        }
    }
    return out;
}

} // namespace brulion
