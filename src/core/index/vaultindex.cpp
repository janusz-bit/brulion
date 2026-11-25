// VaultIndex: construction, incremental maintenance, change notification and search upkeep.
// The read-only views live in vaultindex_views.cpp.
#include "index/vaultindex.h"
#include "index/vaultindex_p.h"

#include "canvas/canvas.h"
#include "canvas/indextext.h"
#include "index/textutil_p.h"
#include "markdown/inline.h"
#include "markdown/parse.h"
#include "markdown/properties.h"
#include "markdown/tasks.h"
#include "text/text.h"
#include "util/paths.h"

#include <QMetaObject>

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

/// The text the index keeps of a file: a note's own, a canvas's cards (canvas/indextext.h).
QString indexedText(const VaultPath &path, const QString &content)
{
    return canvas::isCanvasPath(path) ? canvas::indexText(content).text : content;
}

/// The ParsedNote of the text indexedText() kept.
ParsedNote parseIndexed(const VaultPath &path, const QString &text, const VaultConfig &config)
{
    return canvas::isCanvasPath(path) ? canvas::indexNote(path, text, config) : markdown::parseNote(path, text, config);
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
            folded.append(text::foldKey(b.text.view()));
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

std::shared_ptr<const Entry> IndexAccess::sharedEntry(const VaultIndex &index, const VaultPath &path)
{
    const auto it = index.d->entries.find(path);
    return it == index.d->entries.end() ? nullptr : std::shared_ptr<const Entry>(it->second);
}

const SearchIndex &IndexAccess::search(const VaultIndex &index)
{
    return index.d->search;
}

bool IndexAccess::searchBuiltElsewhere(const VaultIndex &index)
{
    return index.d->searchElsewhere;
}

bool IndexAccess::searchComplete(const VaultIndex &index)
{
    return index.d->searchDirty.isEmpty() && index.d->searchInFlight.isEmpty() && !index.d->searchElsewhere;
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

QStringList IndexAccess::describeState(const VaultIndex &index)
{
    const VaultIndex::Private &d = *index.d;
    QStringList out;
    for (const auto &[path, e] : d.entries) {
        QStringList links;
        for (const PathLink &l : e->links)
            links.append(u"%1:%2>%3"_s.arg(l.link).arg(l.key, l.target));
        QStringList refs;
        for (const int r : e->refs)
            refs.append(QString::number(r));
        QStringList names;
        for (const NameCandidate &n : e->names)
            names.append(n.folded + u'/' + n.alias);
        out.append(u"note %1 seq %2 key %3 links [%4] refs [%5] names [%6] folded %7"_s.arg(path)
                           .arg(e->seq)
                           .arg(e->contentKey)
                           .arg(links.join(u' '), refs.join(u' '), names.join(u' '), e->foldedPath));
    }
    for (const VaultPath &a : d.attachmentSet)
        out.append(u"attachment "_s + a);
    for (auto it = d.canvasSources.cbegin(); it != d.canvasSources.cend(); ++it)
        out.append(u"canvas %1 %2"_s.arg(it.key()).arg(VaultIndex::contentKey(*it)));
    for (const QString &line : d.names.describe())
        out.append(u"name "_s + line);
    const auto sets = [&out](const char *what, const QHash<QString, PathSet> &map) {
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            out.append(QLatin1StringView(what) + u' ' + it.key() + u" -> "_s + it->sorted().join(u'|'));
    };
    const auto counts = [&out](const char *what, const auto &map) {
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            QStringList items;
            for (const VaultPath &p : it->keys())
                items.append(p + u'*' + QString::number(it->value(p)));
            out.append(QLatin1StringView(what) + u' ' + it.key() + u" -> "_s + items.join(u'|'));
        }
    };
    sets("sourcesByKey", d.sourcesByKey);
    counts("incoming", d.incoming);
    counts("unresolved", d.unresolved);
    sets("blockOwners", d.blockOwners);
    sets("blockRefSources", d.blockRefSources);
    counts("tag", d.tagIndex);
    sets("taskIdOwners", d.taskIdOwners);
    sets("taskDependents", d.taskDependents);
    for (auto it = d.noteFolders.cbegin(); it != d.noteFolders.cend(); ++it) {
        QStringList others;
        for (const auto &[spelled, notes] : it->others)
            others.append(spelled + u'*' + QString::number(notes));
        out.append(u"folder %1 -> %2*%3 [%4]"_s.arg(it.key(), it->spelled).arg(it->notes).arg(others.join(u'|')));
    }
    out.append(u"seq %1"_s.arg(d.seq));
    out.sort();
    return out;
}

void appendTasks(const Entry &e, QList<TaskRef> &out)
{
    // (A page whose tasks are ignored lists none: tasks(), tasksOf(), SYNTAX §5.)
    if (e.note.tasks.isEmpty() || markdown::tasksIgnored(e.note))
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

QList<FolderKey> noteFolderKeys(const VaultPath &path)
{
    QList<FolderKey> out;
    for (qsizetype slash = path.lastIndexOf(u'/'); slash > 0; slash = path.lastIndexOf(u'/', slash - 1)) {
        VaultPath folder = path.left(slash);
        QString key = index_detail::pathKey(folder);
        out.append({std::move(folder), std::move(key)});
    }
    return out;
}

NoteKeys makeNoteKeys(const Entry &e, bool completePlans)
{
    NoteKeys k;
    k.names = NameTable::NoteNames::of(e.path, e.note.aliases);
    k.folders = noteFolderKeys(e.path);
    k.nameLinkKeys = entryKeys(e);
    if (!e.note.tags.isEmpty()) {
        const QHash<QString, int> perTag = tagBlockCounts(e.note);
        k.tagBlocks.reserve(e.note.tags.size());
        for (const QString &tag : e.note.tags)
            k.tagBlocks.append({tag, std::max(1, perTag.value(tag))});
    }
    k.plans.reserve(e.links.size());
    const ResolvePlan::From from = ResolvePlan::From::of(e.path); // (its folders shared by the plans)
    for (const PathLink &l : e.links) {
        k.plans.append(ResolvePlan(e.note.links.at(l.link).target, from, TargetForm::Parsed));
        if (completePlans)
            k.plans.constLast().complete();
    }
    return k;
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

VaultIndex::Private::Private(VaultIndex *owner, const VaultConfig &cfg)
    : q(owner), config(cfg), fingerprint(VaultIndex::parseFingerprint(cfg)), serial(nextSerial())
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

VaultIndex::Private::~Private()
{
    if (searchShared) {
        std::lock_guard lock(searchShared->mutex);
        searchShared->receiver = nullptr; // running jobs deliver nothing
    }
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

bool VaultIndex::Private::sameText(const Entry &e, const QString &content) const
{
    if (canvas::isCanvasPath(e.path)) {
        const auto it = canvasSources.constFind(e.path);
        return it != canvasSources.cend() && *it == content;
    }
    return e.content == content;
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
        canvasSources.remove(path);
        const std::shared_ptr<Entry> gone = detachNote(path, true);
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
    // (A canvas: the entry holds its cards' text, the file's own is kept beside it.)
    const QString text = indexedText(path, content);
    if (canvas::isCanvasPath(path))
        canvasSources.insert(path, content);
    std::unique_ptr<Entry> e = index_detail::makeEntry(path, text, parseIndexed(path, text, config));
    NoteKeys keys = index_detail::makeNoteKeys(*e, false);
    return putEntry(std::move(e), std::move(keys), updateSearch, keepSeq);
}

QSet<QString> VaultIndex::Private::putEntry(std::unique_ptr<Entry> e, NoteKeys noteKeys, bool updateSearch,
                                            std::optional<quint64> keepSeq)
{
    const VaultPath path = e->path;
    // The same path: its folders keep counting it (the spelling stays put while it is edited).
    const std::shared_ptr<Entry> prev = detachNote(path, false, false);
    e->seq = keepSeq ? *keepSeq : ++seq;
    QSet<QString> keys = prev ? QSet<QString>() : std::move(noteKeys.nameLinkKeys);
    const Entry *added = e.get();
    attachNote(std::move(e), noteKeys, !prev);
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

void VaultIndex::Private::attachNote(std::shared_ptr<Entry> owned, NoteKeys &keys, bool countFolders)
{
    Entry &e = *owned;
    const ParsedNote &note = e.note;
    const VaultPath path = e.path;
    entries[path] = std::move(owned);
    journalNotes += note.journalDate ? 1 : 0;
    names.addNote(path, keys.names);
    if (countFolders)
        countNoteFolders(keys.folders, 1);
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
    for (const auto &[tag, blocks] : keys.tagBlocks) {
        const auto [t, added] = tagIndex.tryEmplace(tag);
        if (added)
            cache.tagTargets.reset(); // a tag in use more
        t->set(path, blocks);
    }
    resolveLinks(e, keys.plans);
}

void VaultIndex::Private::countNoteFolders(const QList<FolderKey> &folders, int delta)
{
    using Spelling = std::pair<VaultPath, int>;
    for (const auto &[folder, key] : folders) {
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

std::shared_ptr<VaultIndex::Private::Entry> VaultIndex::Private::detachNote(const VaultPath &path, bool discardSearch,
                                                                            bool countFolders)
{
    const auto it = entries.find(path);
    if (it == entries.end())
        return nullptr;
    std::shared_ptr<Entry> owned = std::move(it->second);
    entries.erase(it);
    const Entry &e = *owned;
    journalNotes -= e.note.journalDate ? 1 : 0;
    unlinkOutgoing(e);
    names.removeNote(path, e.note.aliases);
    if (countFolders)
        countNoteFolders(noteFolderKeys(path), -1);
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

void VaultIndex::Private::resolveLinks(Entry &e, QList<ResolvePlan> &plans)
{
    Q_ASSERT(plans.size() == e.links.size());
    for (qsizetype i = 0; i < e.links.size(); ++i) {
        PathLink &l = e.links[i];
        l.target = names.resolve(plans[i]);
        if (l.target.isEmpty() && !l.key.isEmpty()) {
            plans[i].complete();
            if (!e.unresolvedPlans)
                e.unresolvedPlans = std::make_unique<QList<std::pair<int, ResolvePlan>>>();
            e.unresolvedPlans->append({int(i), std::move(plans[i])});
        }
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
    // Most changes bring one key (a new note without aliases): compared, not hashed.
    const QString *onlyKey = keys.size() == 1 ? &*keys.cbegin() : nullptr;
    const auto relinkSource = [&](const VaultPath &s) {
        if (s == skip)
            return;
        Entry *e = mutableEntry(s);
        if (!e)
            return;
        for (qsizetype i = 0; i < e->links.size(); ++i) {
            PathLink &l = e->links[i];
            if (onlyKey ? l.key != *onlyKey : !keys.contains(l.key))
                continue;
            // The plan kept while it resolved to nothing, else a new one (kept if it still does).
            QList<std::pair<int, ResolvePlan>> *kept = e->unresolvedPlans.get();
            qsizetype at = -1;
            for (qsizetype k = 0; kept && k < kept->size() && at < 0; ++k)
                at = kept->at(k).first == int(i) ? k : -1;
            std::optional<ResolvePlan> made;
            if (at < 0)
                made.emplace(e->note.links.at(l.link).target, s, TargetForm::Parsed);
            const VaultPath next = names.resolve(at >= 0 ? kept->at(at).second : *made);
            if (next.isEmpty() && at < 0) {
                made->complete();
                if (!kept)
                    kept = (e->unresolvedPlans = std::make_unique<QList<std::pair<int, ResolvePlan>>>()).get();
                kept->append({int(i), std::move(*made)});
            } else if (!next.isEmpty() && at >= 0) {
                if (at != kept->size() - 1) // (the order does not matter: no plans shifted)
                    std::swap((*kept)[at], kept->last());
                kept->removeLast();
                if (kept->isEmpty())
                    e->unresolvedPlans.reset();
            }
            if (next == l.target)
                continue;
            fileLink(s, l, false);
            l.target = next;
            fileLink(s, l, true);
            pending.notes.insert(s);
            cache.missing.reset();
        }
    };
    if (onlyKey) {
        // (Relinking changes no source set: iterated as it is.)
        if (const auto it = sourcesByKey.constFind(*onlyKey); it != sourcesByKey.constEnd())
            it->forEach(relinkSource);
        return;
    }
    QSet<VaultPath> sources;
    for (const QString &k : keys) {
        const auto it = sourcesByKey.constFind(k);
        if (it == sourcesByKey.constEnd())
            continue;
        it->forEach([&](const VaultPath &s) { sources.insert(s); });
    }
    for (const VaultPath &s : std::as_const(sources))
        relinkSource(s);
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
    if (searchWorker) {
        if (searchInFlight.isEmpty())
            startSearchJob(); // else its delivery comes back here
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
        squeezeAfterBulk();
}

void VaultIndex::Private::startSearchJob()
{
    using index_detail::PreparedSearchDoc;
    const quint64 ticket = ++searchTickets;
    auto docs = std::make_shared<QList<PreparedSearchDoc>>();
    qsizetype chars = 0;
    QElapsedTimer slice;
    slice.start();
    for (auto it = searchDirty.begin();
         it != searchDirty.end() && docs->size() < SearchJobDocs && chars < SearchJobChars;) {
        const VaultPath path = *it;
        it = searchDirty.erase(it);
        const Entry *e = entry(path);
        if (!e) {
            search.remove(path); // a note gone: nothing to tokenise
            ++searchBuilt;
            if (slice.elapsed() >= SearchSliceMs)
                break;
            continue;
        }
        // Shared copies: the worker reads them while the record may be replaced here.
        docs->append({path, e->contentKey, e->note.title, e->note.aliases, e->content, {}});
        chars += e->content.size();
        searchInFlight.insert(path, ticket);
    }
    if (docs->isEmpty()) {
        if (!searchDirty.isEmpty())
            searchTimer->start(0);
        else
            squeezeAfterBulk();
        return;
    }
    searchWorker([shared = searchShared, ticket, docs] {
        for (PreparedSearchDoc &doc : *docs) {
            doc.document = SearchIndex::prepare(doc.title, doc.aliases, doc.content);
            doc.content = QString(); // the record keeps the text, the job need not
        }
        std::lock_guard lock(shared->mutex);
        if (VaultIndex *receiver = shared->receiver)
            QMetaObject::invokeMethod(
                    receiver, [receiver, ticket, docs] { receiver->d->applyPrepared(ticket, *docs); },
                    Qt::QueuedConnection);
    });
}

void VaultIndex::Private::applyPrepared(quint64 ticket, QList<index_detail::PreparedSearchDoc> &docs)
{
    for (index_detail::PreparedSearchDoc &doc : docs) {
        const auto it = searchInFlight.constFind(doc.path);
        if (it == searchInFlight.constEnd() || *it != ticket)
            continue; // built meanwhile (flushSearch(), adoptSearch(), clear())
        searchInFlight.erase(it);
        if (searchDirty.contains(doc.path))
            continue; // changed meanwhile: prepared again from its new text
        const Entry *e = entry(doc.path);
        if (!e || e->contentKey != doc.contentKey) {
            markSearch(doc.path); // (every change marks the note: not expected)
            continue;
        }
        search.set(doc.path, std::move(doc.document), doc.contentKey);
        ++searchBuilt;
    }
    if (!searchInFlight.isEmpty())
        return; // (a job of an older ticket cannot be pending: one job at a time)
    if (!searchDirty.isEmpty()) {
        if (!searchTimer->isActive())
            searchTimer->start(0); // searchTick() waits for a quiet moment again
    } else {
        squeezeAfterBulk();
    }
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
    // Documents on the worker are built here too; the job's late delivery is dropped.
    for (auto it = searchInFlight.cbegin(); it != searchInFlight.cend(); ++it)
        searchDirty.insert(it.key());
    searchInFlight.clear();
    searchBuilt += searchDirty.size();
    for (const VaultPath &p : std::as_const(searchDirty))
        indexSearchDoc(p);
    searchDirty.clear();
    squeezeAfterBulk();
}

void VaultIndex::Private::squeezeAfterBulk()
{
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
    d->fingerprint = parseFingerprint(config);
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
        const QString content = canvas::isCanvasPath(p) ? d->canvasSources.value(p) : e->content;
        const quint64 seq = e->seq;
        d->relink(d->putNote(p, content, false, seq), p);
    }
    d->markEverythingChanged();
}

void VaultIndex::update(const VaultPath &path, const QString &content)
{
    if (!indexable(path))
        return;
    if (!readsText(path)) {
        d->addAttachment(path);
        return;
    }
    if (const Private::Entry *e = d->entry(path); e && d->sameText(*e, content))
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
    const bool markdown = readsText(newPath);
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
    d->journalNotes = 0;
    d->attachmentSet.clear();
    d->canvasSources.clear();
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
    d->searchInFlight.clear(); // (their documents are dropped when they come back)
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

QString PreparedNote::text() const
{
    return m_entry ? m_entry->content : QString();
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

bool VaultIndex::readsText(const VaultPath &path)
{
    return paths::isMarkdown(path) || canvas::isCanvasPath(path);
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
    return prepareParsed(path, content, parseIndexed(path, indexedText(path, content), config), config);
}

PreparedNote VaultIndex::prepareSave(const VaultPath &path, const QString &content, const VaultConfig &config)
{
    PreparedNote out = prepare(path, content, config);
    // (Cached in the entry: noteChanged() compares the spellings when the tags and their blocks
    // are the same — typing in a tagged page — and the old version's are cached since its own
    // merge.)
    if (out.m_entry && !out.m_entry->note.tags.isEmpty())
        (void)out.m_entry->tagSpellings();
    return out;
}

PreparedNote VaultIndex::prepareParsed(const VaultPath &path, const QString &content, ParsedNote note,
                                       const VaultConfig &config)
{
    PreparedNote out;
    out.m_indexable = indexable(path); // (the path's checks off the index's thread too)
    out.m_markdown = readsText(path);
    if (canvas::isCanvasPath(path)) {
        // The entry holds the cards' text; the file's own is kept beside it.
        out.m_source = content;
        out.m_entry = index_detail::makeEntry(path, indexedText(path, content), std::move(note));
    } else {
        out.m_entry = index_detail::makeEntry(path, content, std::move(note));
    }
    out.m_keys = std::make_unique<NoteKeys>(index_detail::makeNoteKeys(*out.m_entry, true));
    out.m_parseFingerprint = parseFingerprint(config);
    return out;
}

void VaultIndex::updatePrepared(PreparedNote prepared, std::optional<quint64> sequence)
{
    if (!prepared.m_entry)
        return;
    const VaultPath path = prepared.m_entry->path;
    if (!prepared.m_indexable)
        return;
    if (!prepared.m_markdown) {
        d->addAttachment(path);
        return;
    }
    const bool isCanvas = canvas::isCanvasPath(path);
    const QString content = isCanvas ? prepared.m_source : prepared.m_entry->content; // the file's text
    if (const Private::Entry *e = d->entry(path); e && d->sameText(*e, content))
        return;
    if (isCanvas)
        d->canvasSources.insert(path, content);
    const QSet<QString> keys =
            prepared.m_parseFingerprint != d->fingerprint
                    ? d->putNote(path, content, true, sequence) // prepared with an older config
                    : d->putEntry(std::move(prepared.m_entry), std::move(*prepared.m_keys), true, sequence);
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
    d->searchInFlight.clear(); // dropped when they come back: marked again below when missing
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
    // The same question again before the next change (every editor of the Journal's days, the
    // lists, after one notification): the answer of the first (the log changes only with the
    // version).
    if (d->changesMemo && d->changesMemo->since == version && d->changesMemo->at == d->version)
        return d->changesMemo->changes;
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
    d->changesMemo = Private::ChangesMemo{version, d->version, out};
    return out;
}

void VaultIndex::flushSearch()
{
    d->flushSearch();
}

qsizetype VaultIndex::pendingSearchDocuments() const
{
    qsizetype pending = d->searchDirty.size();
    for (auto it = d->searchInFlight.cbegin(); it != d->searchInFlight.cend(); ++it)
        pending += d->searchDirty.contains(it.key()) ? 0 : 1;
    return pending;
}

void VaultIndex::setSearchWorker(std::function<void(std::function<void()>)> start)
{
    d->searchWorker = std::move(start);
    if (d->searchWorker && !d->searchShared) {
        d->searchShared = std::make_shared<index_detail::SearchWorkerShared>();
        d->searchShared->receiver = this;
    }
    // Without a worker a job still out is dropped when it comes back; its notes are built here.
    if (!d->searchWorker && !d->searchInFlight.isEmpty()) {
        for (auto it = d->searchInFlight.cbegin(); it != d->searchInFlight.cend(); ++it)
            d->searchDirty.insert(it.key());
        d->searchInFlight.clear();
        if (!d->searchTimer->isActive())
            d->searchTimer->start(0);
    }
}

} // namespace brulion
