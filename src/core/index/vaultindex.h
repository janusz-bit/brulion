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

/// A block or page that carries a tag naming a page (see VaultIndex::tagReferences()).
struct TagReference {
    VaultPath path;      ///< the note carrying the tag
    int blockIndex = -1; ///< the tagged block (index into ParsedNote::blocks); -1: the page itself
                         ///< (frontmatter `tags:`, leading `tags::` properties)
    QString tag;         ///< the tag key (lower-case, NFC) that names the page

    friend bool operator==(const TagReference &, const TagReference &) = default;
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

/// A `#` completion (VaultIndex::suggestTags()): a tag in use, or a page no tag in use names yet.
struct TagSuggestion {
    QString tag;       ///< the name to write, without '#' (as first written; a page's tagTarget())
    int count = 0;     ///< tagged blocks (0 for a page)
    bool page = false; ///< a page no tag names yet (else a tag in use)
    VaultPath path;    ///< the page the tag names ("" when none)
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

/// A file moved or renamed (VaultIndex::planRelocation()).
struct FileMove {
    VaultPath from;
    VaultPath to;

    friend bool operator==(const FileMove &, const FileMove &) = default;
};

/// What moving or renaming files changes in the notes, planned before the move by
/// VaultIndex::planRelocation() (docs/SYNTAX.md §2 "Rewriting on rename and move"): the notes to
/// rewrite so that every link and every tag keeps naming the file it named before. A link to a
/// moved file follows it as renameLinks() would write it (a bare name keeps the new base name, a
/// path-qualified link gets the new path, a rename in place keeps the written folder prefix); a
/// link the move would turn to another file — a bare [[Dom]] that a shorter Dom.md now takes, a
/// relative link of a moved note — gets the shortest target that still reaches its file
/// (VaultIndex::linkTarget(): [[Projekty/Dom]]); tags the same way (VaultIndex::tagTarget():
/// #Projekty/Dom). The filters of ```query blocks that name them follow too (findQueryNames()):
/// `tag:` / `#tag` exactly that tag (not its sub-tags) as the tags, `ref:` / `[[…]]` and `page:` a
/// moved page as links, `path:` a prefix whose every file moved by one folder change (and whose
/// new prefix matches no other file). Links that could not be kept (no written form reaches the
/// file) stay as they are. A plan stays valid after the index changed: it keeps the lookup tables
/// of before and after the move.
class RelocationPlan {
public:
    struct File {
        VaultPath path;    ///< the note, where it is before the move
        VaultPath newPath; ///< where it is after the move (path unless it moves itself)
        QString before;    ///< its content the plan was made from
        QString after;     ///< ... and with the links and tags rewritten
        int links = 0;     ///< links rewritten
        int tags = 0;      ///< tag occurrences rewritten
        int queries = 0;   ///< ```query filter values rewritten (tag:, ref:, page:, path:)
        int queryTags = 0; ///< … of them tag: / #tag values
    };

    RelocationPlan();
    ~RelocationPlan();
    RelocationPlan(const RelocationPlan &);
    RelocationPlan &operator=(const RelocationPlan &);

    /// The moves planned (files the index knows; no-ops dropped).
    QList<FileMove> moves() const;
    /// The notes to rewrite, by path.
    const QList<File> &files() const;
    /// Links / tag occurrences rewritten in all notes — the ```query filters that name a moved
    /// page or folder (ref:, [[…]], page:, path:) counted as links, their tag: / #tag filters as
    /// tags, as the rename and move dialogs say it.
    int links() const;
    int tags() const;
    /// The ```query filter values among them.
    int queries() const;
    /// The plan for note `path` (its path before the move) applied to `content` — the note as
    /// read after the move, when it no longer equals File::before (an edit saved meanwhile).
    /// Notes outside the plan get their links checked too. Returns `content` when nothing changes.
    QString rewrite(const VaultPath &path, const QString &content) const;

private:
    friend class VaultIndex;
    friend class RelocationPlanner;
    struct Private;
    std::shared_ptr<const Private> d;
    QList<File> m_files;
};

class VaultIndex;

/// VaultIndex::planRelocation() a few notes at a time, for a caller that must stay responsive
/// (the dragged row's ghost asks for every folder it rests on): the lookup tables and the notes
/// to look at are prepared at once, step() plans notes until its time is up. The plan equals
/// planRelocation()'s; it holds only while the index does not change (valid(); a planner whose
/// index changed is dropped and made anew). The index must outlive the planner.
class RelocationPlanner {
public:
    RelocationPlanner(const VaultIndex &index, const QList<FileMove> &moves);
    ~RelocationPlanner();
    RelocationPlanner(const RelocationPlanner &) = delete;
    RelocationPlanner &operator=(const RelocationPlanner &) = delete;

    /// Plan notes for about `budgetMs` milliseconds (at least one note while there are any);
    /// true once every note is planned. Does nothing when !valid().
    bool step(int budgetMs);
    bool done() const;
    /// The index has not changed since the planner was made.
    bool valid() const;
    /// The plan (complete once done()).
    RelocationPlan plan() const;

private:
    struct State;
    std::unique_ptr<State> s;
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
/// the background once changes pause (300 ms; a few milliseconds per event-loop turn, so nothing
/// blocks), or at the latest by the next search().
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
    /// Notes whose full-text document is not built yet: the background catches up in steps of a
    /// few milliseconds per event-loop turn once changes pause (after opening a vault: soon after
    /// it is indexed), so the first search() is fast. 0 when up to date.
    qsizetype pendingSearchDocuments() const;

    // ---- notes ----------------------------------------------------------------------------------

    /// Parsed note, or nullptr. Valid until the next change of the index.
    const ParsedNote *note(const VaultPath &path) const;
    /// All note paths, sorted (code units).
    QList<VaultPath> paths() const;
    /// Known attachment paths (non-Markdown files), sorted.
    QList<VaultPath> attachments() const;
    /// Notes with a ```query block (sorted by path; cached until the next change).
    QList<VaultPath> notesWithQueries() const;
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
    /// links in document order. Links of `path` to itself are excluded. For a page that does not
    /// exist (yet) — a link's or a tag's page opened before it is written — the links that would
    /// resolve to it: unresolved `[[name]]` / `[[folder/name]]` whose target is `path` without
    /// `.md` or a path suffix of it. Tags are not links here: see tagReferences().
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
    /// The shortest wiki-link target that resolves to `path` when written in note `from` (SYNTAX
    /// §2): the title ("Dom", an attachment's file name "plan.png"), else a longer path suffix
    /// ("Projekty/Dom", "A/Projekty/Dom"), else the vault path without `.md` (with it as the last
    /// resort). What `[[` completion and "Kopiuj link" write. nullopt when `path` is not indexed
    /// or no wiki link can name it (a '#', '|', `[[` or `]]` in its name).
    std::optional<QString> linkTarget(const VaultPath &path, const VaultPath &from = {}) const;
    /// The shortest tag name that names note `path` (resolveTag(): "Dom", else "Projekty/Dom", …,
    /// the vault path without `.md`); write it with markdown::tagSyntax(). nullopt for an
    /// attachment, an unknown path or a name no tag can hold.
    std::optional<QString> tagTarget(const VaultPath &path) const;
    /// Plan moving or renaming files (notes and attachments; a folder is all the files in it)
    /// so that links and tags keep their targets: see RelocationPlan. Files the index does not
    /// know are left out. Nothing is changed: apply the plan after the files moved.
    /// RelocationPlanner makes the same plan a few notes at a time.
    RelocationPlan planRelocation(const QList<FileMove> &moves) const;

    // ---- blocks, tags, tasks, reminders, journals ---------------------------------------------

    /// Block with explicit id `id` (`^id` or Logseq `id::`) anywhere in the vault; a leading `^`
    /// or surrounding `((…))` is accepted. Duplicated ids: the smallest path wins.
    std::optional<BlockRef> blockById(const QString &id) const;
    /// Tags with usage counts, most used first (ties by name).
    QList<TagCount> tags() const;
    /// The note a tag names (SYNTAX §3 "Tagi jako linki do stron"): `tag` (with or without '#',
    /// `[[ ]]`, trailing '/') resolved like a `[[link]]` written at the vault root — exact vault
    /// path ± `.md`, base name (shortest path, then alphabetical), aliases; case-insensitive and
    /// NFC, not diacritic-insensitive. Notes only (never attachments); nullopt when no note has
    /// that name. `#Projekty/Dom` names Projekty/Dom.md, `#dom` the shortest Dom.md.
    std::optional<VaultPath> resolveTag(const QString &tag) const;
    /// The tags in use that name `path` (resolveTag(); keys: lower-case NFC, no '#'), sorted —
    /// what tagReferences() and tagsTo() follow, and what a rename of `path` rewrites
    /// (markdown::renameTags()). For a page that does not exist (yet), the unresolved tags that
    /// would name it once written ("praca/raporty" and "raporty" for praca/raporty.md). Empty
    /// for an attachment.
    QStringList tagsNaming(const VaultPath &path) const;
    /// The blocks and pages of other notes carrying a tag that names `path` (resolveTag()), one
    /// entry per block (a block tagged twice is listed once) and one per page tagged as a whole
    /// (blockIndex -1, listed before its blocks); journals first (newest first), then other notes
    /// by path, blocks in document order. Exactly the tag: sub-tags (`#dom/kuchnia`) are other
    /// pages. For a page that does not exist (yet), the tags that would name it once written:
    /// unresolved tags equal to its path without `.md` or to a path suffix of it ("praca" for
    /// Praca.md, "praca/raporty" and "raporty" for praca/raporty.md).
    QList<TagReference> tagReferences(const VaultPath &path) const;
    /// Notes with a tag that names `path`, sorted — with linksTo() the files to rewrite when
    /// `path` is renamed. Includes `path` itself when it carries such a tag.
    QList<VaultPath> tagsTo(const VaultPath &path) const;
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
    /// Completions for `#` (a tag names a page, docs/SYNTAX.md §3): the tags in use that start with
    /// `prefix` (most used first; '#' and diacritics ignored), then pages no tag in use names yet
    /// (suggestPages(), no fuzzy matches; named by the title or alias that matched, or — that
    /// name being another page's — by the shortest tag that names the page, tagTarget(); a page
    /// whose tag is in use is offered as that tag), then the tags that contain `prefix`. One entry
    /// per tag key (case-insensitive, NFC, not diacritic-insensitive: #lodz is not Łódź).
    QList<TagSuggestion> suggestTags(const QString &prefix, int limit = 20) const;

    // ---- graph ----------------------------------------------------------------------------------

    /// Note graph: a node per note, a directed edge per (source, target) pair of linked notes
    /// (wiki links, embeds, Markdown links, block references and tags that name a note —
    /// resolveTag(); deduplicated, no self-loops, attachments skipped; A->B and B->A are two
    /// edges). `includeTags` adds "#tag" nodes for the tags that name no note, linked to the
    /// notes using them; `includeMissing` adds nodes for link targets that do not exist yet
    /// (never for tags). Nodes: notes by path, then the others as first met.
    GraphData graph(bool includeTags = false, bool includeMissing = false) const;

Q_SIGNALS:
    /// The index changed (coalesced, see the class comment).
    void changed();

private:
    friend struct index_detail::IndexAccess;
    friend class RelocationPlanner;
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace brulion
