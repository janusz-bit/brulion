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
    auto add = [&](const QString &name) {
        QString n = nfc(name.trimmed());
        if (n.startsWith(u'#'))
            n.remove(0, 1);
        while (n.endsWith(u'/'))
            n.chop(1);
        const QString key = n.toLower();
        if (!n.isEmpty() && note.tags.contains(key) && !out.contains(key))
            out.insert(key, n);
    };
    const QStringList frontmatterTags =
            markdown::namesFrom(markdown::frontmatterGet(note.frontmatter, {u"tags"_s, u"tag"_s}), true);
    for (const QString &name : frontmatterTags)
        add(name);
    for (const Block &b : note.blocks) {
        if (b.tags.isEmpty())
            continue;
        // Logseq `tags::` properties, then inline #tags as written.
        const QStringList propertyTags = markdown::namesFrom(b.properties.value(u"tags"_s), false)
                                         + markdown::namesFrom(b.properties.value(u"tag"_s), false);
        for (const QString &name : propertyTags)
            add(name);
        const QStringList inlineTags = markdown::extractInline(b.raw).tags;
        for (const QString &name : inlineTags)
            add(name);
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

std::unique_ptr<Entry> makeEntry(const VaultPath &path, const QString &content, ParsedNote note)
{
    auto e = std::make_unique<Entry>();
    e->path = path;
    e->content = content.isNull() ? QStringLiteral("") : content; // null means "unknown" in content()
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
        const QString key = l.target.trimmed().isEmpty() ? QString() : linkKey(l.target, TargetForm::Parsed);
        e->links.append(PathLink{int(i), key, {}});
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

} // namespace index_detail

// ---------------------------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------------------------

VaultIndex::Private::Private(VaultIndex *owner, const VaultConfig &cfg) : q(owner), config(cfg)
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
    return it->min([](const VaultPath &a, const VaultPath &b) { return a < b; });
}

const QList<const VaultIndex::Private::Entry *> &VaultIndex::Private::sortedEntries()
{
    if (!cache.sorted) {
        QList<const Entry *> list;
        list.reserve(qsizetype(entries.size()));
        for (const auto &[path, e] : entries)
            list.append(e.get());
        std::sort(list.begin(), list.end(), [](const Entry *a, const Entry *b) { return a->path < b->path; });
        cache.sorted = std::move(list);
    }
    return *cache.sorted;
}

const QList<const VaultIndex::Private::Entry *> &VaultIndex::Private::entriesByRelevance()
{
    if (!cache.byRelevance) {
        QList<const Entry *> list = sortedEntries();
        std::stable_sort(list.begin(), list.end(),
                         [](const Entry *a, const Entry *b) { return relevanceLess(*a, *b); });
        cache.byRelevance = std::move(list);
    }
    return *cache.byRelevance;
}

void VaultIndex::Private::markChanged()
{
    ++version;
    cache = IndexCaches();
    notifyPending = true;
    if (batchDepth == 0 && !notifyTimer->isActive())
        notifyTimer->start();
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
    relink({fileNameKey(path)}, {});
    markChanged();
}

bool VaultIndex::Private::removeAttachment(const VaultPath &path)
{
    if (!attachmentSet.remove(path))
        return false;
    names.removeAttachment(path);
    return true;
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
    const std::unique_ptr<Entry> prev = detachNote(path, false);
    e->seq = keepSeq.value_or(++seq);
    const QStringList aliases = e->note.aliases;
    QSet<QString> keys = prev ? QSet<QString>() : entryKeys(*e);
    attachNote(std::move(e));
    if (updateSearch)
        markSearch(path);
    if (prev && prev->note.aliases != aliases) {
        for (const QString &a : prev->note.aliases + aliases) {
            for (const QString &k : aliasLinkKeys(a))
                keys.insert(k);
        }
    }
    return keys;
}

void VaultIndex::Private::attachNote(std::unique_ptr<Entry> owned)
{
    Entry &e = *owned;
    const VaultPath path = e.path;
    entries[path] = std::move(owned);
    names.addNote(path, e.note.aliases);
    for (const int r : std::as_const(e.refs))
        multiAdd(blockRefSources, e.note.links[r].blockId, path);
    for (auto it = e.note.blockIds.cbegin(); it != e.note.blockIds.cend(); ++it)
        multiAdd(blockOwners, it.key(), path);
    if (!e.note.tags.isEmpty()) {
        QHash<QString, int> perTag;
        for (const Block &b : std::as_const(e.note.blocks)) {
            for (const QString &t : b.tags)
                ++perTag[t];
        }
        for (const QString &tag : std::as_const(e.note.tags))
            tagIndex[tag].set(path, std::max(1, perTag.value(tag)));
    }
    resolveLinks(e);
}

std::unique_ptr<VaultIndex::Private::Entry> VaultIndex::Private::detachNote(const VaultPath &path, bool discardSearch)
{
    const auto it = entries.find(path);
    if (it == entries.end())
        return nullptr;
    std::unique_ptr<Entry> owned = std::move(it->second);
    entries.erase(it);
    const Entry &e = *owned;
    unlinkOutgoing(e);
    names.removeNote(path, e.note.aliases);
    for (const int r : e.refs)
        multiRemove(blockRefSources, e.note.links[r].blockId, path);
    for (auto b = e.note.blockIds.cbegin(); b != e.note.blockIds.cend(); ++b)
        multiRemove(blockOwners, b.key(), path);
    for (const QString &tag : e.note.tags) {
        const auto t = tagIndex.find(tag);
        if (t == tagIndex.end())
            continue;
        t->remove(path);
        if (t->isEmpty())
            tagIndex.erase(t);
    }
    if (discardSearch)
        markSearch(path);
    return owned;
}

QSet<QString> VaultIndex::Private::entryKeys(const Entry &e) const
{
    QSet<QString> keys{noteNameKey(e.path)};
    for (const QString &a : e.note.aliases) {
        for (const QString &k : aliasLinkKeys(a))
            keys.insert(k);
    }
    return keys;
}

void VaultIndex::Private::fileLink(const VaultPath &source, const PathLink &link, bool add)
{
    if (!link.target.isEmpty()) {
        if (add)
            countAdd(incoming, link.target, source);
        else
            countRemove(incoming, link.target, source);
    } else if (!link.key.isEmpty()) {
        if (add)
            countAdd(unresolved, link.key, source);
        else
            countRemove(unresolved, link.key, source);
    }
}

void VaultIndex::Private::resolveLinks(Entry &e)
{
    for (PathLink &l : e.links) {
        l.target = names.resolve(e.note.links[l.link].target, e.path, TargetForm::Parsed);
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
            const VaultPath next = names.resolve(e->note.links[l.link].target, s, TargetForm::Parsed);
            if (next == l.target)
                continue;
            fileLink(s, l, false);
            l.target = next;
            fileLink(s, l, true);
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
    if (!searchDirty.isEmpty()) {
        searchTimer->start(0);
    } else if (searchBuilt > 1000) {
        searchBuilt = 0;
        search.squeeze(); // no growth slack after a bulk build (docs/PERFORMANCE.md, L8)
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
    searchBuilt += searchDirty.size();
    for (const VaultPath &p : std::as_const(searchDirty))
        indexSearchDoc(p);
    searchDirty.clear();
    if (searchBuilt > 1000) {
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
    QList<VaultPath> all;
    for (const auto &[path, e] : d->entries)
        all.append(path);
    std::sort(all.begin(), all.end());
    for (const VaultPath &p : std::as_const(all)) {
        const Private::Entry *e = d->entry(p);
        const QString content = e->content;
        const quint64 seq = e->seq;
        d->relink(d->putNote(p, content, false, seq), p);
    }
    d->markChanged();
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
    if (const Private::Entry *e = d->entry(path)) {
        const QSet<QString> keys = d->entryKeys(*e);
        d->detachNote(path, true);
        d->relink(keys, {});
        d->markChanged();
        return;
    }
    if (d->removeAttachment(path)) {
        d->relink({fileNameKey(path)}, {});
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
    if (const Private::Entry *old = d->entry(oldPath)) {
        keys.unite(d->entryKeys(*old));
        d->detachNote(oldPath, true);
        touched = true;
    } else if (d->removeAttachment(oldPath)) {
        keys.insert(fileNameKey(oldPath));
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
            keys.insert(fileNameKey(newPath));
            touched = true;
        }
    }
    d->relink(keys, markdown ? newPath : VaultPath());
    if (touched)
        d->markChanged();
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
    d->tagIndex.clear();
    d->searchTimer->stop();
    d->searchDirty.clear();
    d->search.clear();
    d->markChanged();
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
           + (config.defaultReminderTime.isValid() ? config.defaultReminderTime.toString(u"HH:mm:ss.zzz"_s) : QString());
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
    QSet<QString> keys;
    if (prepared.m_parseFingerprint != parseFingerprint(d->config))
        keys = d->putNote(path, content, true, sequence); // prepared with an older config
    else
        keys = d->putEntry(std::move(prepared.m_entry), true, sequence);
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
    QSet<VaultPath> dirty;
    for (const auto &[path, e] : d->entries) {
        if (!built.contains(path))
            dirty.insert(path);
    }
    d->search = std::move(built);
    d->searchDirty = std::move(dirty);
    d->searchBuilt = 0;
    if (!d->searchDirty.isEmpty()) {
        d->lastChange.start();
        if (!d->searchTimer->isActive())
            d->searchTimer->start(SearchIdleMs);
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

void VaultIndex::flushSearch()
{
    d->flushSearch();
}

qsizetype VaultIndex::pendingSearchDocuments() const
{
    return d->searchDirty.size();
}

} // namespace brulion
