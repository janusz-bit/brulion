// VaultIndex: construction, incremental maintenance, change notification and search upkeep.
// The read-only views live in vaultindex_views.cpp.
#include "index/vaultindex.h"
#include "index/vaultindex_p.h"

#include "index/textutil_p.h"
#include "markdown/inline.h"
#include "markdown/parse.h"
#include "markdown/properties.h"
#include "text/text.h"
#include "util/paths.h"

#include <algorithm>
#include <atomic>
#include <utility>

namespace brulion {

using namespace index_detail;
using namespace Qt::StringLiterals;

namespace {

/// Can `path` be indexed at all (not ignored, not a sync conflict copy)?
bool indexable(const VaultPath &path)
{
    return !path.isEmpty() && !paths::isIgnored(path) && !paths::isConflictCopy(path);
}

/// Do the configs differ in fields that change parsing results?
bool parsingDiffers(const VaultConfig &a, const VaultConfig &b)
{
    return VaultIndex::parseFingerprint(a) != VaultIndex::parseFingerprint(b);
}

bool isBlockReference(LinkKind kind)
{
    return kind == LinkKind::BlockRef || kind == LinkKind::BlockEmbed;
}

quint64 nextSerial()
{
    static std::atomic<quint64> counter{0};
    return ++counter;
}

/// Documents built (since the last squeeze) after which flushSearch() squeezes the search index.
constexpr qsizetype BulkSearchBuild = 1000;

// ---- what a change touches (VaultIndex::Private::noteChanged()) ------------------------------

/// Tasks per tag of a note (blocks carrying it; VaultIndex::tags() counts).
QHash<QString, int> tagBlockCounts(const ParsedNote &note)
{
    QHash<QString, int> out;
    for (const Block &b : note.blocks) {
        for (const QString &t : b.tags)
            ++out[t];
    }
    return out;
}

/// Do the two versions of a note (null: absent) contribute the same to VaultIndex::tags():
/// tags, blocks per tag and the spellings as first written?
bool sameTagUse(const Entry *a, const Entry *b)
{
    const bool aTags = a && !a->note.tags.isEmpty();
    const bool bTags = b && !b->note.tags.isEmpty();
    if (!aTags && !bTags)
        return true;
    if (!aTags || !bTags || a->path != b->path || a->note.tags != b->note.tags)
        return false;
    return tagBlockCounts(a->note) == tagBlockCounts(b->note) && a->tagSpellings() == b->tagSpellings();
}

/// The unresolved path-based links of a note as VaultIndex::missingPages() reads them (key and
/// target as written); empty for null.
QStringList unresolvedLinks(const Entry *e)
{
    QStringList out;
    if (!e)
        return out;
    for (const PathLink &l : e->links) {
        if (l.target.isEmpty() && !l.key.isEmpty())
            out.append(l.key + u'\x1f' + e->note.links[l.link].target);
    }
    return out;
}

bool hasPageProperties(const Entry *e)
{
    return e && (!e->note.frontmatter.isEmpty() || !e->note.properties.isEmpty());
}

/// Do the two versions of a note (null: absent) hold the same page properties (frontmatter and
/// leading `key:: value` lines; VaultIndex::propertyKeys() / propertyValues())?
bool samePageProperties(const Entry *a, const Entry *b)
{
    if (!hasPageProperties(a) && !hasPageProperties(b))
        return true;
    if (!a || !b || a->path != b->path)
        return false;
    return a->note.frontmatter == b->note.frontmatter && a->note.properties == b->note.properties;
}

/// Add the link keys of `aliases` (aliasLinkKeys()) to `keys`.
void insertAliasKeys(QSet<QString> &keys, const QStringList &aliases)
{
    for (const QString &a : aliases) {
        for (const QStringList forms = aliasLinkKeys(a); const QString &k : forms)
            keys.insert(k);
    }
}

/// The link keys that name a note: its file name and its aliases.
QSet<QString> entryKeys(const Entry &e)
{
    QSet<QString> keys{noteNameKey(e.path)};
    insertAliasKeys(keys, e.note.aliases);
    return keys;
}

/// Replace `old` by `now` in a sorted list (either may be absent; `old` removed when found).
template<typename T, typename Less>
void replaceSorted(QList<T> &list, const std::optional<T> &old, const std::optional<T> &now, Less less)
{
    if (old) {
        const auto it = std::ranges::lower_bound(list, *old, less);
        if (it != list.end() && !less(*old, *it))
            list.erase(it);
    }
    if (now)
        list.insert(std::ranges::lower_bound(list, *now, less), *now);
}

/// Replace the items of note `before` / `after` in a list ordered by path (then document order)
/// with `after`'s (`append` lists them).
template<typename Ref>
void patchSegment(QList<Ref> &list, const Entry *before, const Entry *after,
                  void (*append)(const Entry &, QList<Ref> &))
{
    const auto segment = [&list](const VaultPath &path) {
        const auto begin = list.begin(); // detaches a shared list first: iterators stay valid
        const auto items = std::ranges::equal_range(begin, list.end(), path, {}, &Ref::path);
        return std::pair<qsizetype, qsizetype>(items.begin() - begin, std::ranges::distance(items));
    };
    if (before && !(after && after->path == before->path)) {
        const auto [at, count] = segment(before->path);
        if (count > 0)
            list.remove(at, count);
    }
    if (!after)
        return;
    QList<Ref> fresh;
    append(*after, fresh);
    const auto [at, count] = segment(after->path);
    if (fresh.isEmpty() && count == 0)
        return;
    if (fresh.size() > count)
        list.insert(at + count, fresh.size() - count, Ref());
    else if (fresh.size() < count)
        list.remove(at + fresh.size(), count - fresh.size());
    for (qsizetype i = 0; i < fresh.size(); ++i)
        list[at + i] = std::move(fresh[i]);
}

std::optional<JournalRef> journalOf(const Entry *e)
{
    if (!e || !e->note.journalDate)
        return std::nullopt;
    return JournalRef{*e->note.journalDate, e->path};
}

/// The indices of the blocks of `note` that `pred` accepts, in document order; `expected` bounds
/// their number (0: there are none).
template<typename Pred>
QList<int> blockIndices(const ParsedNote &note, qsizetype expected, Pred pred)
{
    QList<int> out;
    if (expected == 0)
        return out;
    out.reserve(expected);
    for (qsizetype i = 0; i < note.blocks.size(); ++i) {
        if (pred(note.blocks[i]))
            out.append(int(i));
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Entry (lazy per-note data)
// ---------------------------------------------------------------------------------------------

namespace index_detail {

const QString &Entry::lowerContent() const
{
    if (!m_lowerContent)
        m_lowerContent = nfc(content).toLower();
    return *m_lowerContent;
}

const QStringList &Entry::foldedBlockTexts() const
{
    if (!m_foldedBlocks) {
        QStringList folded;
        folded.reserve(note.blocks.size());
        for (const Block &b : note.blocks)
            folded.append(text::foldKey(b.text));
        m_foldedBlocks = std::move(folded);
    }
    return *m_foldedBlocks;
}

const QString &Entry::foldedTitleAndContent() const
{
    if (!m_foldedContent)
        m_foldedContent = text::foldKey(note.title) + u'\n' + text::foldKey(content);
    return *m_foldedContent;
}

const QHash<QString, QString> &Entry::tagSpellings() const
{
    if (m_tagSpellings)
        return *m_tagSpellings;
    QHash<QString, QString> out;
    const auto add = [&](const QStringList &written) {
        for (const QString &name : written) {
            QString n = nfc(name.trimmed());
            if (n.startsWith(u'#'))
                n.remove(0, 1);
            while (n.endsWith(u'/'))
                n.chop(1);
            const QString key = n.toLower();
            if (!n.isEmpty() && note.tags.contains(key) && !out.contains(key))
                out.insert(key, n);
        }
    };
    add(markdown::namesFrom(markdown::frontmatterGet(note.frontmatter, {u"tags"_s, u"tag"_s}), true));
    for (const Block &b : note.blocks) {
        if (b.tags.isEmpty())
            continue;
        // Logseq `tags::` properties, then inline #tags as written.
        add(markdown::namesFrom(b.properties.value(u"tags"_s), false));
        add(markdown::namesFrom(b.properties.value(u"tag"_s), false));
        add(markdown::extractInline(b.raw).tags);
    }
    m_tagSpellings = std::move(out);
    return *m_tagSpellings;
}

const QStringList &Entry::pageTags() const
{
    if (!m_pageTags)
        m_pageTags = pageLevelTags(note);
    return *m_pageTags;
}

const QList<int> &Entry::taskBlocks() const
{
    if (!m_taskBlocks)
        m_taskBlocks = blockIndices(note, note.tasks.size(), [](const Block &b) { return b.task.has_value(); });
    return *m_taskBlocks;
}

const QList<int> &Entry::reminderBlocks() const
{
    if (!m_reminderBlocks)
        m_reminderBlocks =
                blockIndices(note, note.reminders.size(), [](const Block &b) { return !b.reminders.isEmpty(); });
    return *m_reminderBlocks;
}

const Entry *IndexAccess::entry(const VaultIndex &index, const VaultPath &path)
{
    return index.d->entry(path);
}

const SearchIndex &IndexAccess::search(const VaultIndex &index)
{
    return index.d->search;
}

bool IndexAccess::searchComplete(const VaultIndex &index)
{
    return index.d->searchDirty.isEmpty() && !index.d->searchElsewhere;
}

QList<const Entry *> IndexAccess::sortedEntries(const VaultIndex &index)
{
    return index.d->sortedEntries();
}

int IndexAccess::buildCount(const VaultIndex &index, std::string_view list)
{
    const BuildCounts &b = index.d->builds;
    const std::pair<std::string_view, int> counts[] = {
            {"sorted", b.sorted},
            {"byRelevance", b.byRelevance},
            {"paths", b.paths},
            {"attachments", b.attachments},
            {"tags", b.tags},
            {"tasks", b.tasks},
            {"reminders", b.reminders},
            {"journals", b.journals},
            {"missing", b.missing},
            {"tagTargets", b.tagTargets},
            {"queryNotes", b.queryNotes},
            {"propertyKeys", b.propertyKeys},
    };
    for (const auto &[name, count] : counts) {
        if (name == list)
            return count;
    }
    return -1;
}

void appendTasks(const Entry &e, QList<TaskRef> &out)
{
    if (e.note.tasks.isEmpty())
        return;
    const QList<Block> &blocks = e.note.blocks;
    for (qsizetype i = 0; i < blocks.size(); ++i) {
        if (blocks[i].task)
            out.append(TaskRef{e.path, int(i), *blocks[i].task});
    }
}

void appendReminders(const Entry &e, QList<ReminderRef> &out)
{
    if (e.note.reminders.isEmpty())
        return;
    const QList<Block> &blocks = e.note.blocks;
    for (qsizetype i = 0; i < blocks.size(); ++i) {
        for (const Reminder &r : blocks[i].reminders)
            out.append(ReminderRef{e.path, int(i), r, blocks[i].task});
    }
}

std::unique_ptr<Entry> makeEntry(const VaultPath &path, const QString &content, ParsedNote note)
{
    auto e = std::make_unique<Entry>();
    e->path = path;
    e->content = content.isNull() ? u""_s : content; // null means "unknown" in content()
    e->contentKey = VaultIndex::contentKey(e->content);
    e->note = std::move(note);
    const QList<Link> &links = e->note.links;
    for (qsizetype i = 0; i < links.size(); ++i) {
        const Link &l = links[i];
        if (isBlockReference(l.kind)) {
            if (!l.blockId.isEmpty())
                e->refs.append(int(i));
            continue;
        }
        if (l.kind == LinkKind::Url || (l.kind == LinkKind::Markdown && isExternalTarget(l.target)))
            continue;
        const bool blank = QStringView(l.target).trimmed().isEmpty();
        e->links.append(PathLink{int(i), blank ? QString() : linkKey(l.target, TargetForm::Parsed), {}});
    }
    e->links.squeeze();
    e->refs.squeeze();
    e->names.append(NameCandidate{text::foldKey(e->note.title), {}});
    for (const QString &alias : std::as_const(e->note.aliases))
        e->names.append(NameCandidate{text::foldKey(alias), alias});
    e->foldedPath = text::foldKey(paths::stripMarkdownExtension(path));
    return e;
}

bool relevanceLess(const Entry &a, const Entry &b)
{
    const auto &ja = a.note.journalDate;
    const auto &jb = b.note.journalDate;
    if (ja && jb && *ja != *jb)
        return *ja > *jb;
    if (ja.has_value() != jb.has_value())
        return ja.has_value();
    return a.path < b.path;
}

bool journalLess(const JournalRef &a, const JournalRef &b)
{
    if (a.date != b.date)
        return a.date > b.date;
    return a.path < b.path;
}

} // namespace index_detail

// ---------------------------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------------------------

VaultIndex::Private::Private(VaultIndex *owner, const VaultConfig &cfg) : q(owner), config(cfg), serial(nextSerial())
{
    searchTimer = new QTimer(owner);
    searchTimer->setSingleShot(true);
    QObject::connect(searchTimer, &QTimer::timeout, owner, [this] { searchTick(); });
    notifyTimer = new QTimer(owner);
    notifyTimer->setSingleShot(true);
    notifyTimer->setInterval(0);
    QObject::connect(notifyTimer, &QTimer::timeout, owner, [this] {
        if (batchDepth == 0)
            emitIfPending();
    });
}

const VaultIndex::Private::Entry *VaultIndex::Private::entry(const VaultPath &path) const
{
    const auto it = entries.find(path);
    return it == entries.end() ? nullptr : it->second.get();
}

VaultIndex::Private::Entry *VaultIndex::Private::mutableEntry(const VaultPath &path)
{
    const auto it = entries.find(path);
    return it == entries.end() ? nullptr : it->second.get();
}

VaultPath VaultIndex::Private::blockOwner(const QString &id) const
{
    const auto it = blockOwners.constFind(id);
    if (it == blockOwners.constEnd())
        return {};
    return it->min(std::less<>());
}

const QList<const VaultIndex::Private::Entry *> &VaultIndex::Private::sortedEntries()
{
    return cache.read(cache.sorted, builds.sorted, [this] {
        QList<const Entry *> list;
        list.reserve(qsizetype(entries.size()));
        for (const auto &[path, e] : entries)
            list.append(e.get());
        std::ranges::sort(list, {}, &Entry::path);
        return list;
    });
}

const QList<const VaultIndex::Private::Entry *> &VaultIndex::Private::entriesByRelevance()
{
    return cache.read(cache.byRelevance, builds.byRelevance, [this] {
        QList<const Entry *> list = sortedEntries();
        std::ranges::stable_sort(list, [](const Entry *a, const Entry *b) { return relevanceLess(*a, *b); });
        return list;
    });
}

void VaultIndex::Private::markChanged()
{
    pending.version = ++version;
    loggedPaths += pending.notes.size();
    changeLog.push_back(std::exchange(pending, {}));
    while (!changeLog.empty()
           && (qsizetype(changeLog.size()) > VaultIndex::MaxLoggedChanges || loggedPaths > MaxLoggedPaths)) {
        logStart = changeLog.front().version;
        loggedPaths -= changeLog.front().notes.size();
        changeLog.pop_front();
    }
    notifyPending = true;
    if (batchDepth == 0 && !notifyTimer->isActive())
        notifyTimer->start();
}

void VaultIndex::Private::markEverythingChanged()
{
    cache = IndexCaches();
    markChanged();
    changeLog.clear();
    loggedPaths = 0;
    logStart = version;
}

void VaultIndex::Private::namesChanged()
{
    pending.names = true;
    cache.missing.reset();
    cache.tagTargets.reset();
}

void VaultIndex::Private::noteChanged(const Entry *before, const Entry *after)
{
    // The log: the note, and the notes whose `((id))` show one of its blocks (a new note with a
    // smaller path may also take over an id another note defines).
    // Task dependencies (SYNTAX §5): the notes with tasks waiting for one of its task ids (blocked
    // or not by them now), and the notes defining the ids its tasks wait for (blocking or not).
    const auto logAll = [this](const QHash<QString, PathSet> &map, const QString &key) {
        if (const auto it = map.constFind(key); it != map.constEnd())
            it->forEach([this](const VaultPath &s) { pending.notes.insert(s); });
    };
    for (const Entry *e : {before, after}) {
        if (!e)
            continue;
        pending.notes.insert(e->path);
        for (auto id = e->note.blockIds.cbegin(); id != e->note.blockIds.cend(); ++id)
            logAll(blockRefSources, id.key());
        for (const Task &t : e->note.tasks) {
            if (!t.id.isEmpty())
                logAll(taskDependents, t.id);
            for (const QString &dep : t.dependsOn)
                logAll(taskIdOwners, dep);
        }
    }

    // Lists the change touches are dropped (tagTargets: attachNote() / detachNote()).
    IndexCaches &c = cache;
    if (c.tags && !sameTagUse(before, after))
        c.tags.reset();
    if (c.missing && unresolvedLinks(before) != unresolvedLinks(after))
        c.missing.reset();
    if ((c.propertyKeys || !c.propertyValues.isEmpty()) && !samePageProperties(before, after)) {
        c.propertyKeys.reset();
        c.propertyValues.clear();
    }

    // The others are patched: the note's place by path (or relevance), its tasks and reminders.
    if (!c.sorted && !c.byRelevance && !c.paths && !c.queryNotes && !c.journals && !c.tasks && !c.reminders)
        return;
    if (++c.unreadPatches > MaxUnreadPatches) {
        cache = IndexCaches(); // many changes nobody read (a bulk load): built again when read
        return;
    }
    const auto entryOf = [](const Entry *e) { return e ? std::optional<const Entry *>(e) : std::nullopt; };
    const auto pathOf = [](const Entry *e) { return e ? std::optional<VaultPath>(e->path) : std::nullopt; };
    if (c.sorted) {
        replaceSorted(*c.sorted, entryOf(before), entryOf(after),
                      [](const Entry *a, const Entry *b) { return a->path < b->path; });
    }
    if (c.byRelevance) {
        replaceSorted(*c.byRelevance, entryOf(before), entryOf(after),
                      [](const Entry *a, const Entry *b) { return relevanceLess(*a, *b); });
    }
    if (c.paths && !(before && after && before->path == after->path))
        replaceSorted(*c.paths, pathOf(before), pathOf(after), std::less<VaultPath>());
    if (c.queryNotes) {
        const bool queries = after && hasQueryBlocks(after->content, after->note);
        replaceSorted(*c.queryNotes, pathOf(before), queries ? pathOf(after) : std::nullopt, std::less<VaultPath>());
    }
    if (c.journals)
        replaceSorted(*c.journals, journalOf(before), journalOf(after), journalLess);
    if (c.tasks)
        patchSegment<TaskRef>(*c.tasks, before, after, appendTasks);
    if (c.reminders)
        patchSegment<ReminderRef>(*c.reminders, before, after, appendReminders);
}

void VaultIndex::Private::emitIfPending()
{
    if (!notifyPending)
        return;
    notifyPending = false;
    notifyTimer->stop();
    Q_EMIT q->changed();
}

void VaultIndex::Private::addAttachment(const VaultPath &path)
{
    if (attachmentSet.contains(path))
        return;
    attachmentSet.insert(path);
    names.addAttachment(path);
    cache.attachments.reset();
    namesChanged();
    relink({fileNameKey(path)}, {});
    markChanged();
}

bool VaultIndex::Private::removeAttachment(const VaultPath &path)
{
    if (!attachmentSet.remove(path))
        return false;
    names.removeAttachment(path);
    cache.attachments.reset();
    namesChanged();
    return true;
}

std::optional<QSet<QString>> VaultIndex::Private::forget(const VaultPath &path)
{
    if (const Entry *e = entry(path)) {
        QSet<QString> keys = entryKeys(*e);
        const std::unique_ptr<Entry> gone = detachNote(path, true);
        noteChanged(gone.get(), nullptr);
        namesChanged();
        return keys;
    }
    if (removeAttachment(path))
        return QSet<QString>{fileNameKey(path)};
    return std::nullopt;
}

QSet<QString> VaultIndex::Private::putNote(const VaultPath &path, const QString &content, bool updateSearch,
                                           std::optional<quint64> keepSeq)
{
    return putEntry(index_detail::makeEntry(path, content, markdown::parseNote(path, content, config)), updateSearch,
                    keepSeq);
}

QSet<QString> VaultIndex::Private::putEntry(std::unique_ptr<Entry> e, bool updateSearch, std::optional<quint64> keepSeq)
{
    const VaultPath path = e->path;
    // The same path: its folders keep counting it (the spelling stays put while it is edited).
    const std::unique_ptr<Entry> prev = detachNote(path, false, false);
    e->seq = keepSeq ? *keepSeq : ++seq;
    QSet<QString> keys = prev ? QSet<QString>() : entryKeys(*e);
    const Entry *added = e.get();
    attachNote(std::move(e), !prev);
    if (updateSearch)
        markSearch(path);
    if (prev && prev->note.aliases != added->note.aliases) {
        insertAliasKeys(keys, prev->note.aliases);
        insertAliasKeys(keys, added->note.aliases);
    }
    noteChanged(prev.get(), added); // while `prev` is alive: the lists still point at it
    if (!keys.isEmpty())
        namesChanged();
    return keys;
}

void VaultIndex::Private::attachNote(std::unique_ptr<Entry> owned, bool countFolders)
{
    Entry &e = *owned;
    const ParsedNote &note = e.note;
    const VaultPath path = e.path;
    entries[path] = std::move(owned);
    names.addNote(path, note.aliases);
    if (countFolders)
        countNoteFolders(path, 1);
    for (const int r : std::as_const(e.refs))
        multiAdd(blockRefSources, note.links[r].blockId, path);
    for (auto it = note.blockIds.cbegin(); it != note.blockIds.cend(); ++it)
        multiAdd(blockOwners, it.key(), path);
    for (const Task &t : note.tasks) {
        if (!t.id.isEmpty())
            multiAdd(taskIdOwners, t.id, path);
        for (const QString &dep : t.dependsOn)
            multiAdd(taskDependents, dep, path);
    }
    if (!note.tags.isEmpty()) {
        const QHash<QString, int> perTag = tagBlockCounts(note);
        for (const QString &tag : note.tags) {
            const auto [t, added] = tagIndex.tryEmplace(tag);
            if (added)
                cache.tagTargets.reset(); // a tag in use more
            t->set(path, std::max(1, perTag.value(tag)));
        }
    }
    resolveLinks(e);
}

void VaultIndex::Private::countNoteFolders(const VaultPath &path, int delta)
{
    using Spelling = std::pair<VaultPath, int>;
    for (qsizetype slash = path.lastIndexOf(u'/'); slash > 0; slash = path.lastIndexOf(u'/', slash - 1)) {
        const VaultPath folder = path.left(slash);
        const QString key = index_detail::pathKey(folder);
        if (delta > 0) {
            NoteFolder &f = noteFolders[key];
            if (f.notes == 0 || f.spelled == folder) {
                if (f.notes++ == 0)
                    f.spelled = folder;
            } else if (const auto o = std::ranges::find(f.others, folder, &Spelling::first); o != f.others.end()) {
                ++o->second;
            } else {
                f.others.append({folder, 1});
            }
            continue;
        }
        const auto it = noteFolders.find(key);
        if (it == noteFolders.end())
            continue;
        NoteFolder &f = *it;
        if (f.spelled == folder) {
            if (--f.notes > 0)
                continue;
            if (f.others.isEmpty()) {
                noteFolders.erase(it);
                continue;
            }
            // The spelling with the most notes left takes over (the earliest of equals).
            const auto best = std::ranges::max_element(f.others, {}, &Spelling::second);
            f.spelled = best->first;
            f.notes = best->second;
            f.others.erase(best);
        } else if (const auto o = std::ranges::find(f.others, folder, &Spelling::first);
                   o != f.others.end() && --o->second <= 0) {
            f.others.erase(o);
        }
    }
}

std::unique_ptr<VaultIndex::Private::Entry> VaultIndex::Private::detachNote(const VaultPath &path, bool discardSearch,
                                                                            bool countFolders)
{
    const auto it = entries.find(path);
    if (it == entries.end())
        return nullptr;
    std::unique_ptr<Entry> owned = std::move(it->second);
    entries.erase(it);
    const Entry &e = *owned;
    unlinkOutgoing(e);
    names.removeNote(path, e.note.aliases);
    if (countFolders)
        countNoteFolders(path, -1);
    for (const int r : e.refs)
        multiRemove(blockRefSources, e.note.links[r].blockId, path);
    for (auto b = e.note.blockIds.cbegin(); b != e.note.blockIds.cend(); ++b)
        multiRemove(blockOwners, b.key(), path);
    for (const Task &t : e.note.tasks) {
        if (!t.id.isEmpty())
            multiRemove(taskIdOwners, t.id, path);
        for (const QString &dep : t.dependsOn)
            multiRemove(taskDependents, dep, path);
    }
    for (const QString &tag : e.note.tags) {
        const auto t = tagIndex.find(tag);
        if (t == tagIndex.end())
            continue;
        t->remove(path);
        if (t->isEmpty()) {
            tagIndex.erase(t);
            cache.tagTargets.reset(); // a tag in use less
        }
    }
    if (discardSearch)
        markSearch(path);
    return owned;
}

void VaultIndex::Private::fileLink(const VaultPath &source, const PathLink &link, bool add)
{
    // A resolved link is filed under its file, an unresolved one under its name.
    const bool resolved = !link.target.isEmpty();
    if (!resolved && link.key.isEmpty())
        return;
    QHash<QString, PathCounts> &map = resolved ? incoming : unresolved;
    const QString &key = resolved ? link.target : link.key;
    if (add)
        countAdd(map, key, source);
    else
        countRemove(map, key, source);
}

void VaultIndex::Private::resolveLinks(Entry &e)
{
    for (PathLink &l : e.links) {
        l.target = names.resolve(e.note.links.at(l.link).target, e.path, TargetForm::Parsed);
        if (!l.key.isEmpty())
            multiAdd(sourcesByKey, l.key, e.path);
        fileLink(e.path, l, true);
    }
}

void VaultIndex::Private::unlinkOutgoing(const Entry &e)
{
    for (const PathLink &l : e.links) {
        if (!l.key.isEmpty())
            multiRemove(sourcesByKey, l.key, e.path);
        fileLink(e.path, l, false);
    }
}

void VaultIndex::Private::relink(const QSet<QString> &keys, const VaultPath &skip)
{
    if (keys.isEmpty())
        return;
    QSet<VaultPath> sources;
    for (const QString &k : keys) {
        const auto it = sourcesByKey.constFind(k);
        if (it == sourcesByKey.constEnd())
            continue;
        it->forEach([&](const VaultPath &s) {
            if (s != skip)
                sources.insert(s);
        });
    }
    for (const VaultPath &s : std::as_const(sources)) {
        Entry *e = mutableEntry(s);
        if (!e)
            continue;
        for (PathLink &l : e->links) {
            if (!keys.contains(l.key))
                continue;
            const VaultPath next = names.resolve(e->note.links.at(l.link).target, s, TargetForm::Parsed);
            if (next == l.target)
                continue;
            fileLink(s, l, false);
            l.target = next;
            fileLink(s, l, true);
            pending.notes.insert(s);
            cache.missing.reset();
        }
    }
}

void VaultIndex::Private::markSearch(const VaultPath &path)
{
    searchDirty.insert(path);
    lastChange.start();
    // One timer at a time: re-arming it per update would be costly during bulk indexing.
    if (!searchTimer->isActive())
        searchTimer->start(SearchIdleMs);
}

void VaultIndex::Private::searchTick()
{
    if (searchElsewhere)
        return; // adoptSearch() takes over (and restarts the background for what is missing)
    const qint64 quiet = lastChange.isValid() ? lastChange.elapsed() : SearchIdleMs;
    if (quiet < SearchIdleMs) {
        searchTimer->start(int(SearchIdleMs - quiet));
        return;
    }
    QElapsedTimer slice;
    slice.start();
    for (auto it = searchDirty.begin(); it != searchDirty.end();) {
        indexSearchDoc(*it);
        ++searchBuilt;
        it = searchDirty.erase(it);
        if (slice.elapsed() >= SearchSliceMs)
            break;
    }
    if (!searchDirty.isEmpty())
        searchTimer->start(0);
    else
        flushSearch(); // nothing left to build: only the squeeze after a bulk build
}

void VaultIndex::Private::indexSearchDoc(const VaultPath &path)
{
    if (const Entry *e = entry(path))
        search.set(path, SearchIndex::prepare(e->note.title, e->note.aliases, e->content), e->contentKey);
    else
        search.remove(path);
}

void VaultIndex::Private::flushSearch()
{
    searchTimer->stop();
    searchBuilt += searchDirty.size();
    for (const VaultPath &p : std::as_const(searchDirty))
        indexSearchDoc(p);
    searchDirty.clear();
    if (searchBuilt > BulkSearchBuild) {
        searchBuilt = 0;
        search.squeeze(); // no growth slack after a bulk build (docs/PERFORMANCE.md, L8)
    }
}

// ---------------------------------------------------------------------------------------------
// VaultIndex: changes
// ---------------------------------------------------------------------------------------------

VaultIndex::VaultIndex(const VaultConfig &config, QObject *parent)
    : QObject(parent), d(std::make_unique<Private>(this, config))
{ }

VaultIndex::~VaultIndex() = default;

const VaultConfig &VaultIndex::config() const
{
    return d->config;
}

void VaultIndex::setConfig(const VaultConfig &config)
{
    const bool reparse = parsingDiffers(config, d->config);
    d->config = config;
    if (!reparse || d->entries.empty())
        return;
    d->cache = IndexCaches(); // nothing to patch: every note changes
    QList<VaultPath> all;
    all.reserve(qsizetype(d->entries.size()));
    for (const auto &[path, e] : d->entries)
        all.append(path);
    std::ranges::sort(all);
    for (const VaultPath &p : std::as_const(all)) {
        const Private::Entry *e = d->entry(p);
        const QString content = e->content;
        const quint64 seq = e->seq;
        d->relink(d->putNote(p, content, false, seq), p);
    }
    d->markEverythingChanged();
}

void VaultIndex::update(const VaultPath &path, const QString &content)
{
    if (!indexable(path))
        return;
    if (!paths::isMarkdown(path)) {
        d->addAttachment(path);
        return;
    }
    if (const Private::Entry *e = d->entry(path); e && e->content == content)
        return;
    d->relink(d->putNote(path, content, true), path);
    d->markChanged();
}

void VaultIndex::remove(const VaultPath &path)
{
    if (const std::optional<QSet<QString>> keys = d->forget(path)) {
        d->relink(*keys, {});
        d->markChanged();
    }
}

void VaultIndex::rename(const VaultPath &oldPath, const VaultPath &newPath, const QString &content)
{
    if (oldPath == newPath) {
        update(newPath, content);
        return;
    }
    QSet<QString> keys;
    bool touched = false;
    if (std::optional<QSet<QString>> old = d->forget(oldPath)) {
        keys = std::move(*old);
        touched = true;
    }
    const bool markdown = paths::isMarkdown(newPath);
    if (indexable(newPath)) {
        if (markdown) {
            keys.unite(d->putNote(newPath, content, true));
            touched = true;
        } else if (!d->attachmentSet.contains(newPath)) {
            d->attachmentSet.insert(newPath);
            d->names.addAttachment(newPath);
            d->cache.attachments.reset();
            d->namesChanged();
            keys.insert(fileNameKey(newPath));
            touched = true;
        }
    }
    d->relink(keys, markdown ? newPath : VaultPath());
    if (touched)
        d->markChanged();
    else
        d->pending = ChangeRecord();
}

void VaultIndex::clear()
{
    d->entries.clear();
    d->attachmentSet.clear();
    d->names.clear();
    d->sourcesByKey.clear();
    d->incoming.clear();
    d->unresolved.clear();
    d->blockOwners.clear();
    d->blockRefSources.clear();
    d->taskIdOwners.clear();
    d->taskDependents.clear();
    d->tagIndex.clear();
    d->noteFolders.clear();
    d->searchTimer->stop();
    d->searchDirty.clear();
    d->search.clear();
    d->markEverythingChanged();
}

// ---------------------------------------------------------------------------------------------
// VaultIndex: loading on other threads
// ---------------------------------------------------------------------------------------------

PreparedNote::PreparedNote() = default;
PreparedNote::~PreparedNote() = default;
PreparedNote::PreparedNote(PreparedNote &&) noexcept = default;
PreparedNote &PreparedNote::operator=(PreparedNote &&) noexcept = default;

bool PreparedNote::isValid() const
{
    return m_entry != nullptr;
}

VaultPath PreparedNote::path() const
{
    return m_entry ? m_entry->path : VaultPath();
}

quint64 PreparedNote::contentKey() const
{
    return m_entry ? m_entry->contentKey : 0;
}

const ParsedNote &PreparedNote::note() const
{
    static const ParsedNote empty;
    return m_entry ? m_entry->note : empty;
}

bool VaultIndex::isIndexable(const VaultPath &path)
{
    return indexable(path);
}

QString VaultIndex::parseFingerprint(const VaultConfig &config)
{
    return config.journalsFolder + u'\x1f' + config.journalFileFormat + u'\x1f'
           + (config.defaultReminderTime.isValid() ? config.defaultReminderTime.toString(u"HH:mm:ss.zzz"_s)
                                                   : QString());
}

quint64 VaultIndex::contentKey(QStringView text)
{
    quint64 h = 0xcbf29ce484222325ULL;
    for (const QChar c : text) {
        const char16_t u = c.unicode();
        h ^= quint64(u & 0xff);
        h *= 0x100000001b3ULL;
        h ^= quint64(u >> 8);
        h *= 0x100000001b3ULL;
    }
    return h;
}

PreparedNote VaultIndex::prepare(const VaultPath &path, const QString &content, const VaultConfig &config)
{
    return prepareParsed(path, content, markdown::parseNote(path, content, config), config);
}

PreparedNote VaultIndex::prepareParsed(const VaultPath &path, const QString &content, ParsedNote note,
                                       const VaultConfig &config)
{
    PreparedNote out;
    out.m_entry = index_detail::makeEntry(path, content, std::move(note));
    out.m_parseFingerprint = parseFingerprint(config);
    return out;
}

void VaultIndex::updatePrepared(PreparedNote prepared, std::optional<quint64> sequence)
{
    if (!prepared.m_entry)
        return;
    const VaultPath path = prepared.m_entry->path;
    if (!indexable(path))
        return;
    if (!paths::isMarkdown(path)) {
        d->addAttachment(path);
        return;
    }
    const QString &content = prepared.m_entry->content;
    if (const Private::Entry *e = d->entry(path); e && e->content == content)
        return;
    const QSet<QString> keys = prepared.m_parseFingerprint != parseFingerprint(d->config)
                                       ? d->putNote(path, content, true, sequence) // prepared with an older config
                                       : d->putEntry(std::move(prepared.m_entry), true, sequence);
    d->relink(keys, path);
    d->markChanged();
}

quint64 VaultIndex::reserveSequence(qsizetype count)
{
    const quint64 base = d->seq + 1;
    d->seq += quint64(std::max<qsizetype>(0, count));
    return base;
}

void VaultIndex::adoptSearch(SearchIndex built)
{
    d->searchElsewhere = false;
    // Keep the documents of the notes as they are now; drop the others (edited, renamed or
    // deleted since they were built, or notes not in the index).
    const QStringList ids = built.ids();
    for (const QString &id : ids) {
        const Private::Entry *e = d->entry(id);
        if (!e || built.contentKey(id) != e->contentKey || e->contentKey == 0)
            built.remove(id);
    }
    built.squeeze();
    d->search = std::move(built);
    d->searchDirty.clear();
    d->searchBuilt = 0;
    for (const auto &[path, e] : d->entries) {
        if (!d->search.contains(path))
            d->markSearch(path); // built again in the background, as after an edit
    }
}

void VaultIndex::setSearchBuiltElsewhere(bool on)
{
    d->searchElsewhere = on;
    if (!on && !d->searchDirty.isEmpty() && !d->searchTimer->isActive())
        d->searchTimer->start(SearchIdleMs);
}

void VaultIndex::beginBatch()
{
    ++d->batchDepth;
}

void VaultIndex::endBatch()
{
    if (d->batchDepth == 0)
        return;
    if (--d->batchDepth == 0)
        d->emitIfPending();
}

int VaultIndex::version() const
{
    return d->version;
}

quint64 VaultIndex::serial() const
{
    return d->serial;
}

IndexChanges VaultIndex::changesSince(int version) const
{
    IndexChanges out;
    if (version > d->version || version < d->logStart)
        return out;
    out.complete = true;
    QSet<VaultPath> notes;
    for (auto it = d->changeLog.crbegin(); it != d->changeLog.crend() && it->version > version; ++it) {
        it->notes.forEach([&notes](const VaultPath &path) { notes.insert(path); });
        out.names = out.names || it->names;
    }
    out.notes = QList<VaultPath>(notes.cbegin(), notes.cend());
    std::ranges::sort(out.notes);
    return out;
}

void VaultIndex::flushSearch()
{
    d->flushSearch();
}

qsizetype VaultIndex::pendingSearchDocuments() const
{
    return d->searchDirty.size();
}

} // namespace brulion
