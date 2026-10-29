// VaultIndex internals shared by the index module's sources (vaultindex*.cpp, queryrun.cpp).
#pragma once

#include "index/resolve_p.h"
#include "index/searchindex.h"
#include "index/vaultindex.h"

#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <optional>
#include <unordered_map>

namespace brulion {

namespace index_detail {

/// Quiet time after the last change before the search index catches up in the background.
inline constexpr int SearchIdleMs = 300;
/// Time the search index may take per background step (event loop turn) to catch up: the UI
/// stays responsive (also single-threaded in the browser); at least one document per step.
inline constexpr int SearchSliceMs = 8;

/// A path-based link of a note (wiki link, embed, Markdown link to a file) and its resolution.
struct PathLink {
    int link = 0;     ///< index into ParsedNote::links
    QString key;      ///< linkKey() of the target; empty for same-page links (`[[#^id]]`)
    VaultPath target; ///< resolved file, empty when unresolved
};

/// One name of a page for suggestions: its folded title or alias.
struct NameCandidate {
    QString folded;
    QString alias; ///< alias as written; empty for the title / path
};

/// Per-note record of the index.
struct Entry {
    VaultPath path;
    QString content;
    ParsedNote note;
    quint64 seq = 0;            ///< update sequence number (higher = more recently updated)
    QList<PathLink> links;      ///< path-based links
    QList<int> refs;            ///< indices into note.links of `((id))` / `!((id))` references
    QList<NameCandidate> names; ///< folded title, then aliases
    QString foldedPath;         ///< foldKey(path without .md)

    /// NFC, lower-cased content (pre-filter of unlinked references).
    const QString &lowerContent() const;
    /// foldKey() of every block's text (index = block index).
    const QStringList &foldedBlockTexts() const;
    /// foldKey(title + "\n" + content) (query text filter on pages).
    const QString &foldedTitleAndContent() const;
    /// Tag key -> spelling as first written in this note.
    const QHash<QString, QString> &tagSpellings() const;
    /// pageLevelTags() of the note (the tags carried by the page as a whole).
    const QStringList &pageTags() const;

private:
    mutable std::optional<QString> m_lowerContent;
    mutable std::optional<QStringList> m_foldedBlocks;
    mutable std::optional<QString> m_foldedContent;
    mutable std::optional<QHash<QString, QString>> m_tagSpellings;
    mutable std::optional<QStringList> m_pageTags;
};

/// Access to VaultIndex internals for the module's own free functions (runQuery).
struct IndexAccess {
    static const Entry *entry(const VaultIndex &index, const VaultPath &path);
};

/// Journals first (newest first), then other notes by path.
bool relevanceLess(const Entry &a, const Entry &b);

/// The tags a note carries as a whole page: frontmatter `tags`, the leading `tags::` block, and
/// tags found in no ordinary block (keys, lower-case NFC).
QStringList pageLevelTags(const ParsedNote &note);

/// A tag key cleaned for resolution: '#', `[[ ]]`, trailing '/' and blanks stripped (NFC).
QString tagName(const QString &tag);

/// The note (`content` and its parseNote()) has a ```query block — also a fence in a list item
/// (findQueryNames() looks into them).
bool hasQueryBlocks(const QString &content, const ParsedNote &note);

/// The tags that name notes (VaultIndex::resolveTag() of every tag in use), both ways.
struct TagTargets {
    QHash<QString, VaultPath> byTag;      ///< tag key -> note it names (resolved tags only)
    QHash<VaultPath, QStringList> byPage; ///< note -> tag keys naming it (sorted)
};

} // namespace index_detail

/// Derived lists, dropped on every change.
struct IndexCaches {
    std::optional<QList<const index_detail::Entry *>> sorted;      ///< by path
    std::optional<QList<const index_detail::Entry *>> byRelevance; ///< relevanceLess
    std::optional<QList<VaultPath>> paths;
    std::optional<QList<VaultPath>> attachments;
    std::optional<QList<TagCount>> tags;
    std::optional<QList<TaskRef>> tasks;
    std::optional<QList<ReminderRef>> reminders;
    std::optional<QList<JournalRef>> journals;
    std::optional<QList<MissingPage>> missing;
    std::optional<index_detail::TagTargets> tagTargets; ///< one resolve per tag in use, lazily
    std::optional<QList<VaultPath>> queryNotes;         ///< notes with a ```query block, by path
};

struct VaultIndex::Private {
    using Entry = index_detail::Entry;

    Private(VaultIndex *owner, const VaultConfig &cfg);

    VaultIndex *q;
    VaultConfig config;
    std::unordered_map<VaultPath, std::unique_ptr<Entry>> entries;
    QSet<VaultPath> attachmentSet;
    index_detail::NameTable names;
    // Compact per-key sets (smallsets_p.h): nearly every key names one or two notes.
    QHash<QString, index_detail::PathSet> sourcesByKey;    ///< link key -> notes with such links
    QHash<VaultPath, index_detail::PathCounts> incoming;   ///< target -> source -> link count
    QHash<QString, index_detail::PathCounts> unresolved;   ///< link key -> source -> count
    QHash<QString, index_detail::PathSet> blockOwners;     ///< block id -> notes defining it
    QHash<QString, index_detail::PathSet> blockRefSources; ///< block id -> notes with ((id))
    QHash<QString, index_detail::PathCounts> tagIndex;     ///< tag -> note -> occurrences

    SearchIndex search;
    QSet<VaultPath> searchDirty;
    qsizetype searchBuilt = 0; ///< documents built since the last SearchIndex::squeeze()
    QTimer *searchTimer = nullptr;
    QElapsedTimer lastChange;

    QTimer *notifyTimer = nullptr;
    int batchDepth = 0;
    bool notifyPending = false;
    int version = 0;
    quint64 seq = 0;
    IndexCaches cache;

    // ---- lookups --------------------------------------------------------------------------------
    const Entry *entry(const VaultPath &path) const;
    Entry *mutableEntry(const VaultPath &path);
    /// Smallest path defining block `id`, empty when none.
    VaultPath blockOwner(const QString &id) const;
    const QList<const Entry *> &sortedEntries();
    const QList<const Entry *> &entriesByRelevance();
    /// The note `tag` names (VaultIndex::resolveTag()); empty when none.
    VaultPath resolveTagPath(const QString &tag) const;
    const index_detail::TagTargets &tagTargets();
    /// Notes with a ```query block (VaultIndex::notesWithQueries()).
    const QList<VaultPath> &queryNotes();
    /// The tag keys naming `path`: resolved ones for a note, for a missing page the unresolved
    /// ones that would name it (see VaultIndex::tagReferences()).
    QStringList tagKeysFor(const VaultPath &path);

    // ---- maintenance ----------------------------------------------------------------------------
    void markChanged();
    void emitIfPending();
    void addAttachment(const VaultPath &path);
    bool removeAttachment(const VaultPath &path);
    /// Parse and (re)index a note; returns the link keys whose resolution in *other* notes may
    /// have changed (the note's names when new, aliases when they changed).
    QSet<QString> putNote(const VaultPath &path, const QString &content, bool updateSearch,
                          std::optional<quint64> keepSeq = std::nullopt);
    std::unique_ptr<Entry> makeEntry(const VaultPath &path, const QString &content, ParsedNote note, quint64 seq) const;
    void attachNote(std::unique_ptr<Entry> e);
    /// Remove a note from every map; returns it (nullptr when unknown).
    std::unique_ptr<Entry> detachNote(const VaultPath &path, bool discardSearch);
    QSet<QString> entryKeys(const Entry &e) const;
    void fileLink(const VaultPath &source, const index_detail::PathLink &link, bool add);
    void resolveLinks(Entry &e);
    void unlinkOutgoing(const Entry &e);
    /// Re-resolve the links filed under one of `keys` in every note except `skip`.
    void relink(const QSet<QString> &keys, const VaultPath &skip);

    // ---- search ---------------------------------------------------------------------------------
    void markSearch(const VaultPath &path);
    void searchTick();
    void indexSearchDoc(const VaultPath &path);
    void flushSearch();
};

} // namespace brulion
