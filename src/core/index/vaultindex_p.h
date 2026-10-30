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

#include <deque>
#include <memory>
#include <optional>
#include <string_view>
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
    quint64 contentKey = 0;     ///< VaultIndex::contentKey(content)
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

/// Access to VaultIndex internals for the module's own free functions (runQuery, the index
/// cache's writer) and tests.
struct IndexAccess {
    static const Entry *entry(const VaultIndex &index, const VaultPath &path);
    static const SearchIndex &search(const VaultIndex &index);
    /// Is the full-text index up to date and complete (nothing pending, not built elsewhere)?
    static bool searchComplete(const VaultIndex &index);
    /// How often a derived list was built from scratch (tests: an edit patches the lists instead):
    /// "sorted", "byRelevance", "paths", "attachments", "tags", "tasks", "reminders", "journals",
    /// "missing", "tagTargets", "queryNotes", "propertyKeys"; -1 for another name.
    static int buildCount(const VaultIndex &index, std::string_view list);
};

/// One change of the index in its change log (VaultIndex::changesSince()).
struct ChangeRecord {
    int version = 0;       ///< the version the change made
    QSet<VaultPath> notes; ///< see IndexChanges::notes
    bool names = false;    ///< see IndexChanges::names
};

/// Paths the change log keeps at most (over all its records); older records are dropped.
inline constexpr qsizetype MaxLoggedPaths = 16384;
/// Changes after which lists that were not read meanwhile are dropped instead of patched (a bulk
/// load: building them once at the next read is cheaper than patching each change in).
inline constexpr int MaxUnreadPatches = 32;

/// How often each derived list was built from scratch (IndexAccess::buildCount()).
struct BuildCounts {
    int sorted = 0;
    int byRelevance = 0;
    int paths = 0;
    int attachments = 0;
    int tags = 0;
    int tasks = 0;
    int reminders = 0;
    int journals = 0;
    int missing = 0;
    int tagTargets = 0;
    int queryNotes = 0;
    int propertyKeys = 0;
};

/// The tasks / reminders of a note as VaultIndex::tasks() / reminders() list them.
void appendTasks(const Entry &e, QList<TaskRef> &out);
void appendReminders(const Entry &e, QList<ReminderRef> &out);

/// Journals first (newest first), then other notes by path.
bool relevanceLess(const Entry &a, const Entry &b);

/// A note's record from its text and parse (pure: any thread); `seq` 0.
std::unique_ptr<Entry> makeEntry(const VaultPath &path, const QString &content, ParsedNote note);

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

/// Derived lists, built on first use and then patched with every change of a note (or dropped
/// when the change touches what they list; see VaultIndex::Private::noteChanged()).
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
    std::optional<QList<PropertyKeyInfo>> propertyKeys; ///< VaultIndex::propertyKeys()
    /// Folded key -> all values of the page property, sorted (VaultIndex::propertyValues()).
    QHash<QString, QList<PropertyValueCount>> propertyValues;
    /// Changes patched in since a list was last read (MaxUnreadPatches).
    int unreadPatches = 0;

    /// A list is read: patching the next changes in pays off again.
    void read() { unreadPatches = 0; }
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
    qsizetype searchBuilt = 0;    ///< documents built since the last SearchIndex::squeeze()
    bool searchElsewhere = false; ///< setSearchBuiltElsewhere()
    QTimer *searchTimer = nullptr;
    QElapsedTimer lastChange;

    QTimer *notifyTimer = nullptr;
    int batchDepth = 0;
    bool notifyPending = false;
    int version = 0;
    quint64 seq = 0;
    IndexCaches cache;
    index_detail::BuildCounts builds;

    // ---- change log (VaultIndex::changesSince()) -------------------------------------------------
    const quint64 serial;                        ///< VaultIndex::serial()
    std::deque<index_detail::ChangeRecord> changeLog; ///< oldest first
    int logStart = 0;          ///< the log covers the versions after this one
    qsizetype loggedPaths = 0; ///< paths in changeLog's records
    index_detail::ChangeRecord pending; ///< the change being applied (markChanged() logs it)

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
    /// A change was applied: the version grows, `pending` goes to the change log, changed() is
    /// scheduled.
    void markChanged();
    /// A change of everything (clear(), a re-parse): every list dropped, the log restarted (every
    /// consumer computes everything again).
    void markEverythingChanged();
    /// A note changed (`before` null: added; `after` null: removed; both: updated in place —
    /// `before` already detached, `after` attached): logs it and the notes showing its block
    /// ids, and patches or drops the derived lists.
    void noteChanged(const Entry *before, const Entry *after);
    /// Names changed (a note or attachment added, removed or renamed, aliases): logged, and the
    /// lists that resolve names dropped.
    void namesChanged();
    void emitIfPending();
    void addAttachment(const VaultPath &path);
    bool removeAttachment(const VaultPath &path);
    /// Parse and (re)index a note; returns the link keys whose resolution in *other* notes may
    /// have changed (the note's names when new, aliases when they changed).
    QSet<QString> putNote(const VaultPath &path, const QString &content, bool updateSearch,
                          std::optional<quint64> keepSeq = std::nullopt);
    /// putNote() of a prepared record (its seq is set here).
    QSet<QString> putEntry(std::unique_ptr<Entry> e, bool updateSearch, std::optional<quint64> keepSeq);
    void attachNote(std::unique_ptr<Entry> e);
    /// Remove a note from every map; returns it (nullptr when unknown).
    std::unique_ptr<Entry> detachNote(const VaultPath &path, bool discardSearch);
    QSet<QString> entryKeys(const Entry &e) const;
    void fileLink(const VaultPath &source, const index_detail::PathLink &link, bool add);
    void resolveLinks(Entry &e);
    void unlinkOutgoing(const Entry &e);
    /// Re-resolve the links filed under one of `keys` in every note except `skip`; the notes
    /// whose links now resolve elsewhere are logged in `pending`.
    void relink(const QSet<QString> &keys, const VaultPath &skip);

    // ---- search ---------------------------------------------------------------------------------
    void markSearch(const VaultPath &path);
    void searchTick();
    void indexSearchDoc(const VaultPath &path);
    void flushSearch();
};

} // namespace brulion
