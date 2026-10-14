// VaultIndex — incremental in-memory index of all notes of a vault: parsed notes, link
// resolution (docs/SYNTAX.md §2), backlinks, unlinked references, block ids, tags (§3), tasks,
// reminders, journals, attachments, full-text search, suggestions and graph data. Part of the
// index module — see index/index.h for the module overview.
#pragma once

#include "types.h"

#include <QDate>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>
#include <optional>

namespace brulion {

namespace index_detail {
struct IndexAccess;
}

/// A link from another note (see VaultIndex::backlinks()).
struct Backlink {
    VaultPath path;      ///< note containing the link
    Link link;           ///< the link as parsed (kind, offsets, line, display text, …)
    int blockIndex = -1; ///< innermost block of `path` containing the link (index into ParsedNote::blocks)
};

/// A path-based link or block reference of a note with its current resolution.
struct ResolvedLink {
    Link link;
    /// Resolved file (note or attachment; for `((id))` the note defining the block); empty when
    /// the target does not exist (yet).
    VaultPath target;
};

/// A plain-text mention of a page (see VaultIndex::unlinkedReferences()).
struct UnlinkedRef {
    VaultPath path;
    int line = 0;    ///< 0-based line of the mention
    QString excerpt; ///< about 120 characters around it ("…" where cut)
};

/// A block somewhere in the vault.
struct BlockRef {
    VaultPath path;
    int blockIndex = -1; ///< index into ParsedNote::blocks of note(path)

    friend bool operator==(const BlockRef &, const BlockRef &) = default;
};

/// A tag with usage statistics (see VaultIndex::tags()).
struct TagCount {
    QString tag;            ///< canonical form: lower-case, NFC, without '#'
    int count = 0;          ///< tagged blocks, plus one per note tagged only at page level
    QList<VaultPath> paths; ///< notes with the tag, sorted
    QString display;        ///< spelling as first written ("Praca" for #Praca)
};

/// A task with its location.
struct TaskRef {
    VaultPath path;
    int blockIndex = -1;
    Task task;
};

/// A reminder with its location (and the task when the block is one).
struct ReminderRef {
    VaultPath path;
    int blockIndex = -1;
    Reminder reminder;
    std::optional<Task> task;
};

/// A daily note.
struct JournalRef {
    QDate date;
    VaultPath path;

    friend bool operator==(const JournalRef &, const JournalRef &) = default;
};

/// A full-text search result.
struct SearchHit {
    VaultPath path;
    QString title;
    double score = 0;
    int line = -1;   ///< 0-based line of the first match in the body, -1 for e.g. title-only matches
    QString excerpt; ///< text around the match (or the start of the note)
};

/// A page suggestion for `[[` completion / quick switcher.
struct PageSuggestion {
    VaultPath path;
    QString title;
    QString alias; ///< the frontmatter alias that matched, empty when the title/path matched
};

/// A link target that does not exist (yet).
struct MissingPage {
    QString name;             ///< target as first written (fragment stripped, NFC)
    QList<VaultPath> sources; ///< notes linking to it, sorted
};

struct GraphNode {
    QString id; ///< note path, "#tag" for tags, the page name for missing pages
    QString title;
    int degree = 0; ///< number of incident edges
    bool isTag = false;
    bool isMissing = false;
};

struct GraphEdge {
    QString source;
    QString target;
};

struct GraphData {
    QList<GraphNode> nodes;
    QList<GraphEdge> edges;
};

/// Incremental in-memory index of a vault's notes.
///
/// Feed it with update() / remove() / rename() (e.g. from Vault change signals). Markdown files
/// are parsed with markdown::parseNote(); any other path is remembered as an attachment so
/// `![[photo.png]]` resolves. Ignored paths (paths::isIgnored) and conflict copies are skipped.
///
/// Every change is applied incrementally: the note is parsed once, its entries in the lookup
/// maps (base names, aliases, reverse links — also unresolved ones, filed by name so they
/// resolve when the page appears later — block ids, tags) are swapped, and only links whose
/// resolution the change can affect are re-resolved. The full-text index is updated lazily: in
/// the background once changes pause (300 ms), or at the latest by the next search().
///
/// changed() is coalesced: emitted once per event-loop turn after any number of changes (zero
/// timer), or right away by the endBatch() that closes the outermost batch. version() grows
/// with every change. Query results are snapshots; pointers returned by note() stay valid until
/// the next change of the index. Not thread-safe: use it from one thread.
class VaultIndex : public QObject {
    Q_OBJECT

public:
    /// Default number of full-text search hits.
    static constexpr int DefaultSearchLimit = 50;
    /// Most mentions returned by unlinkedReferences().
    static constexpr int MaxUnlinkedReferences = 200;

    explicit VaultIndex(const VaultConfig &config = VaultConfig(), QObject *parent = nullptr);
    ~VaultIndex() override;

    VaultIndex(const VaultIndex &) = delete;
    VaultIndex &operator=(const VaultIndex &) = delete;

    // ---- changes ------------------------------------------------------------------------------

    const VaultConfig &config() const;
    /// Change the vault config; notes are re-parsed when fields that affect parsing changed
    /// (journalsFolder, journalFileFormat, defaultReminderTime).
    void setConfig(const VaultConfig &config);

    /// Index or re-index a file. Unchanged content is a no-op (no version bump, no signal).
    void update(const VaultPath &path, const QString &content);
    /// Forget a note or attachment.
    void remove(const VaultPath &path);
    /// A file moved: forget `oldPath`, index `newPath` with `content` (one change).
    void rename(const VaultPath &oldPath, const VaultPath &newPath, const QString &content);
    /// Forget everything.
    void clear();

    /// Group changes: changed() is held back until the matching endBatch(), which emits it once
    /// (if anything changed). Batches nest.
    void beginBatch();
    void endBatch();

    /// RAII batch: beginBatch() in the constructor, endBatch() in the destructor.
    class Batch {
    public:
        explicit Batch(VaultIndex &index) : m_index(index) { m_index.beginBatch(); }
        ~Batch() { m_index.endBatch(); }
        Batch(const Batch &) = delete;
        Batch &operator=(const Batch &) = delete;

    private:
        VaultIndex &m_index;
    };

    /// Incremented on every change.
    int version() const;

    /// Build the full-text documents of all changed notes now (otherwise done in the background
    /// or by the next search()).
    void flushSearch();

    // ---- notes ----------------------------------------------------------------------------------

    /// Parsed note, or nullptr. Valid until the next change of the index.
    const ParsedNote *note(const VaultPath &path) const;
    /// All note paths, sorted (code units).
    QList<VaultPath> paths() const;
    /// Known attachment paths (non-Markdown files), sorted.
    QList<VaultPath> attachments() const;
    /// Is `path` an indexed note or attachment?
    bool contains(const VaultPath &path) const;
    /// Raw content of a note as last indexed (null QString when unknown).
    QString content(const VaultPath &path) const;

    // ---- links ----------------------------------------------------------------------------------

    /// Resolve a link target (SYNTAX §2) written in note `from`; `#fragment` / `|display` are
    /// ignored. Attachments are found too (`photo.png`).
    std::optional<VaultPath> resolveLink(const QString &target, const VaultPath &from = {}) const;
    /// Links of a note with their current resolution, in document order: wiki links, embeds,
    /// Markdown links to files (URLs excluded) and block references.
    QList<ResolvedLink> outgoingLinks(const VaultPath &path) const;
    /// Links from other notes to `path`: wiki links, embeds, Markdown links and `((id))` block
    /// references to blocks of `path`. Journals first (newest first), then other notes by path;
    /// links in document order. Links of `path` to itself are excluded.
    QList<Backlink> backlinks(const VaultPath &path) const;
    /// Notes whose path-based links (not `((id))`) resolve to `path`, sorted — the files to
    /// rewrite when `path` is renamed. Includes `path` itself when it links to itself by name.
    QList<VaultPath> linksTo(const VaultPath &path) const;
    /// Plain-text mentions of the page's title or aliases in other notes: whole words (Unicode
    /// boundaries), case-insensitive; not in links, code, URLs, tags or frontmatter. One per
    /// line, journals first (newest first), at most MaxUnlinkedReferences.
    QList<UnlinkedRef> unlinkedReferences(const VaultPath &path) const;
    /// Link targets that do not exist (pages only, not missing attachments), most linked first.
    QList<MissingPage> missingPages() const;

    // ---- blocks, tags, tasks, reminders, journals ---------------------------------------------

    /// Block with explicit id `id` (`^id` or Logseq `id::`) anywhere in the vault; a leading `^`
    /// or surrounding `((…))` is accepted. Duplicated ids: the smallest path wins.
    std::optional<BlockRef> blockById(const QString &id) const;
    /// Tags with usage counts, most used first (ties by name).
    QList<TagCount> tags() const;
    /// All tasks (notes by path, tasks in document order).
    QList<TaskRef> tasks() const;
    /// All reminders (notes by path, document order), with the task when the block is one.
    QList<ReminderRef> reminders() const;
    /// Daily notes, newest first (ties by path).
    QList<JournalRef> journals() const;

    // ---- search and suggestions -----------------------------------------------------------------

    /// Full-text search (see SearchIndex: folded tokens, prefix for the last term, fuzzy, title
    /// x3, aliases x2, AND then OR). Each hit has the first matching body line and an excerpt
    /// around the match. An empty query gives no hits.
    QList<SearchHit> search(const QString &query, int limit = DefaultSearchLimit) const;
    /// Pages for `[[` completion / quick switcher: exact > starts with > a word starts with >
    /// contains > fuzzy, over titles, aliases (then PageSuggestion::alias is set) and — when the
    /// query has a '/' — paths. Case- and diacritic-insensitive. Empty prefix: most recently
    /// updated notes first.
    QList<PageSuggestion> suggestPages(const QString &prefix, int limit = 20) const;
    /// Blocks for `((` completion: blocks whose text contains every word of `text`
    /// (diacritic-insensitive). Blocks with an id come first, then blocks starting with the
    /// query, then blocks of more recently updated notes. Empty text: blocks with ids of the most
    /// recently updated notes, then other blocks.
    QList<BlockRef> suggestBlocks(const QString &text, int limit = 20) const;

    // ---- graph ----------------------------------------------------------------------------------

    /// Note graph: a node per note, a directed edge per (source, target) pair of linked notes
    /// (wiki links, embeds, Markdown links and block references; deduplicated, no self-loops,
    /// attachments skipped; A->B and B->A are two edges). `includeTags`
    /// adds "#tag" nodes linked to the notes using them; `includeMissing` adds nodes for link
    /// targets that do not exist yet. Nodes: notes by path, then the others as first met.
    GraphData graph(bool includeTags = false, bool includeMissing = false) const;

Q_SIGNALS:
    /// The index changed (coalesced, see the class comment).
    void changed();

private:
    friend struct index_detail::IndexAccess;
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace brulion
