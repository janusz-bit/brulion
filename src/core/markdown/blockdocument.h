// BlockDocument — the editable model behind the block editor (docs/UI.md "The block editor").
// Part of the markdown module — see markdown/markdown.h for the module overview.
//
// A note is a flat list of blocks in document order: every list item (at any depth) is a block,
// and every other top-level block (paragraph, heading, code, table, quote, callout, rule, HTML,
// frontmatter) is a block of depth 0 with an empty prefix. A callout or quote is one block
// together with everything inside it. Each block owns a contiguous range of lines: its own text
// (`prefix` + `body`) and the blank lines after it (`trailing`).
//
// Empty paragraphs (blank blocks: Enter at the start of a paragraph, a document's last empty
// paragraph) exist only in memory: in the text they are blank lines, which a re-read puts into
// the blank lines of the block before. blankBlocks() / restoreBlankBlocks() carry them across
// a re-read of the same text.
//
// Round trip: toString() reproduces the original content byte for byte when nothing changed
// (any mix of blocks, tabs/spaces/numbering, CRLF, BOM, missing final newline). Operations
// rewrite only the blocks they touch; everything else keeps its exact bytes. The only edits to
// neighbours are the ones Markdown needs to keep the structure: a blank line between blocks
// that would otherwise merge (e.g. a paragraph inserted after a list item), renumbering of an
// affected ordered list, the final line break when the last block changes, removing the
// indentation of a non-list block that follows a list item (it would continue the item), a blank
// line before a `---` rule that becomes the first line (it would open YAML frontmatter), and a
// blank line after a block with a `$$` line that opens a display formula it does not close, when a
// later line that is only `$$` would close it (the formula would take the blocks in between).
//
// Every operation returns an EditResult: ordered row changes (for QAbstractItemModel
// begin/end calls) plus a suggested focus block and caret position. Folding is device-local and
// not part of the document; operations that depend on it take a `collapsed` flag.
//
// Versions and undo: block uids are unique in the process (a copy of a document shares them), so
// a block keeps its identity across documents. version() is a cheap snapshot (each block's uid and
// exact text, shared with the document); diffFrom(version) after an edit is a Delta — the smallest
// run of blocks that differs, a block in both versions with only its changed range of text. An
// undo step keeps that Delta: revert() / reapply() turn the document into the other version
// exactly (text, empty paragraphs, uids), parsing only the blocks the step changes, and report
// the row changes like any operation. composed() joins two consecutive deltas (typing).
#pragma once

#include "types.h"

#include <QDate>
#include <QList>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace brulion::markdown {

namespace detail {
struct BlockEdges;
struct BlockSource;
struct BlockVersionData;
struct BlockDeltaData;
} // namespace detail

/// One block (one row of the block editor).
struct BlockEntry {
    quint64 uid = 0; ///< unique in the process (survives moves; undo / redo restore the uids of
                     ///< the blocks they bring back; a copy of the document shares them)
    BlockKind kind = BlockKind::Paragraph;
    QString prefix;  ///< indentation + list marker + spacing ("\t- ", "  1. "); "" for non-list blocks
    QString body;    ///< editable text without the prefix, lines joined by "\n"; list item
                     ///< continuation lines without their indentation
    int depth = 0;   ///< outline depth (list nesting); 0 for non-list blocks
    int parent = -1; ///< index of the parent list item, -1 for top-level blocks
    int headingLevel = 0;
    QString lang; ///< fenced code language ("calc", "query", …)
    std::optional<TaskStatus> taskStatus;
    QString blockId;  ///< `^id` / `id::` of the block, empty if none
    QString trailing; ///< blank lines after the block, verbatim (kept for round trip)
    int line = 0;     ///< first document line of the block in the current text
};

/// One step of the row changes made by an operation, for QAbstractItemModel. Steps are applied
/// in order; each step's indices refer to the rows as they are after the previous steps.
struct BlockChange {
    enum class Type {
        Removed,  ///< rows [first, first + count) were removed
        Moved,    ///< rows [first, first + count) moved before row `destination` (beginMoveRows convention)
        Inserted, ///< rows [first, first + count) were inserted
        Changed,  ///< rows [first, first + count) changed their data
    };
    Type type = Type::Changed;
    int first = 0;
    int count = 0;
    int destination = -1;

    friend bool operator==(const BlockChange &, const BlockChange &) = default;
};

/// What an operation changed and where the caret should go.
struct EditResult {
    QList<BlockChange> changes; ///< empty when nothing changed
    int focus = -1;             ///< block that should hold the caret, -1 = keep the current one
    int cursor = -1;            ///< caret position in that block's body, -1 = keep / end

    bool changed() const { return !changes.isEmpty(); }
};

/// Rows [first, first + count).
struct RowRange {
    int first = 0;
    int count = 0;
    int last() const { return first + count - 1; }
    friend bool operator==(const RowRange &, const RowRange &) = default;
};

/// An empty paragraph (a blank block) of a document: its first line (BlockEntry::line) and how
/// many lines it spans (more than one with line breaks typed into it).
struct BlankBlock {
    int line = -1;
    int lines = 1;
    friend bool operator==(const BlankBlock &, const BlankBlock &) = default;
};

/// Result of BlockDocument::ensureBlockId().
struct BlockIdResult {
    QString id;      ///< empty when the block cannot carry an id
    EditResult edit; ///< the change when an id had to be added
};

class BlockDocument {
public:
    /// The document's blocks at one moment — their uids and exact text, and the document's edges —
    /// to diff against after an edit (diffFrom()). Taking one costs a reference count per block
    /// string, no copy of text.
    class Version {
    public:
        Version();
        bool isNull() const { return !d; }

    private:
        friend class BlockDocument;
        std::shared_ptr<const detail::BlockVersionData> d;
    };

    /// What an edit changed: the smallest run of blocks that differs between two versions (uid or
    /// text: own lines, line terminator, blank lines after), the document's edges in both (BOM,
    /// leading blank lines, line terminator, final newline) and the block counts. A block in both
    /// versions keeps only the changed range of its text, a block in one of them its whole text —
    /// what an undo step keeps instead of the page (editor::PageController).
    class Delta {
    public:
        Delta();
        bool isEmpty() const;
        /// Characters held.
        qsizetype size() const;

    private:
        friend class BlockDocument;
        std::shared_ptr<const detail::BlockDeltaData> d;
    };

    BlockDocument();
    explicit BlockDocument(const QString &content, const BlockDocument *reuse = nullptr);

    /// Replace the whole document (all uids are new). A block of `reuse` (may be this document)
    /// with the same text and the same position-dependent parse options lends its parse instead
    /// of parsing the text again: undo / redo re-read a long page that is mostly unchanged. The
    /// result is the same as without `reuse`.
    void setContent(const QString &content, const BlockDocument *reuse = nullptr);
    /// The document text; byte-identical to the original when nothing changed.
    QString toString() const;

    int count() const { return int(m_items.size()); }
    bool isEmpty() const { return m_items.isEmpty(); }
    /// Block `i` (0 <= i < count()).
    const BlockEntry &at(int i) const { return m_items[i].entry; }
    /// Raw Markdown of block `i` without its trailing blank lines (prefix + body as written).
    QString blockSource(int i) const;
    /// Index of the block with `uid`, -1 if none.
    int indexOfUid(quint64 uid) const;
    /// Index of the block containing document line `line` (incl. its trailing blank lines), -1.
    int indexAtLine(int line) const;
    /// Line terminator used for new lines ("\n" or "\r\n", detected from the content).
    QString eol() const { return m_eol; }

    int parent(int i) const;
    /// The block and all its descendants (list items nested under it).
    RowRange subtreeRange(int i) const;
    bool hasChildren(int i) const;

    /// Replace the body of block `i`. Continuation lines of a list item get the block's
    /// indentation; "\r\n" in `text` is accepted. The block's kind follows its new text (typing
    /// "# " makes a heading, "- " a list item, …).
    EditResult setBody(int i, const QString &text);

    /// The blank blocks — empty paragraphs, which exist only in memory: in the text they are blank
    /// lines, so a document read from text has none. Their lines and line counts, ascending.
    QList<BlankBlock> blankBlocks() const;
    /// Make blank blocks of the blank lines at `blocks` (as blankBlocks() returned them for this
    /// text) again: the text stays byte for byte, only the blocks change (other uids stay). A block
    /// whose lines are not all blank lines before the first block or after a block is ignored.
    /// Returns how many were made. Meant for a document just read from text.
    int restoreBlankBlocks(const QList<BlankBlock> &blocks);

    // --- Versions and undo (see the file comment) ---------------------------------------------------

    Version version() const;
    /// The change from `older` (a version of this document) to the document as it is; empty when
    /// nothing differs.
    Delta diffFrom(const Version &older) const;
    /// Undo / redo: turn the document from the delta's newer version back into the older one
    /// (revert) or the older into the newer (reapply), exactly (text, empty paragraphs, uids); the
    /// parse of unchanged blocks is kept, changed and restored blocks are parsed again. No
    /// normalisation runs (a blank line before a leading `---` rule, after an open `$$`, line
    /// ends): the version comes back as it was. Row changes like every operation (a block in both
    /// versions whose text differs: Changed); no focus. When the document is not that version
    /// (block count, edges, the run's uids or text differ) nothing changes, the result is empty and
    /// `*ok` (optional) is false.
    EditResult revert(const Delta &delta, bool *ok = nullptr);
    EditResult reapply(const Delta &delta, bool *ok = nullptr);
    /// `first` (A -> B) followed by `second` (B -> C) as one delta A -> C, where this document is
    /// C. nullopt when they do not chain (counts, edges, the runs' blocks) or when more than
    /// MaxComposeGap untouched blocks lie between their runs (the caller keeps two steps).
    std::optional<Delta> composed(const Delta &first, const Delta &second) const;
    static constexpr int MaxComposeGap = 64;

    /// Give blocks the uids of another version of this document (BlockListModel::replaceContent:
    /// the blocks it keeps keep their identity). `uids`: block index -> uid; each uid at most once
    /// and none used by another block of this document.
    void adoptUids(const QList<std::pair<int, quint64>> &uids);

    /// Enter at `cursor` in block `i` (list items, paragraphs, headings): the text after the
    /// cursor moves to a new block; line breaks right at the split point are dropped (no blank
    /// lines at the edges of either block). List items: same indentation and marker (numbered lists
    /// continue: 3. -> 4.), tasks continue with `[ ] ` (or TODO/LATER); a block with children
    /// gets the new block as its first child unless `collapsed`. Paragraphs/headings: the new
    /// block is a paragraph (separated by a blank line). Cursor 0 in a non-empty block, or a
    /// cursor within or right after the task marker of a task with text, inserts an empty block
    /// (for a task an empty task: `[ ] ` / `TODO `) before it instead; the original keeps its
    /// status, children and uid. Focus: the new block (caret after a task marker), or the
    /// original block for an insertion before it (caret after its task marker, else 0).
    EditResult split(int i, int cursor, bool collapsed = false);

    /// Insert a new block after block `i` (-1 = at the start). A list item at `depth` (clamped to
    /// 0 … depth(i) + 1): deeper than `i` makes it the first child of `i`, otherwise it follows
    /// the subtree of `i`'s ancestor at that depth; its marker style follows its siblings. With
    /// `listItem` false the block is a top-level block written as given (`body` is Markdown) after
    /// the top-level subtree of `i`. A block is never inserted before or between the frontmatter /
    /// page-property blocks at the top (it goes right after them).
    EditResult insertAfter(int i, const QString &body, int depth, bool listItem = true);

    /// Backspace at the start of block `i`: its body is appended to the previous block (list
    /// items, paragraphs, headings); an empty block is just removed. Children stay in place
    /// (they follow the previous block). Focus: the previous block, caret at the join. When the
    /// previous block is not a text block (code, table, …) nothing changes; focus points at it.
    EditResult mergeWithPrevious(int i);

    /// Remove block `i` (and its descendants when `withChildren`; otherwise they move up to the
    /// previous block / parent). Focus: the previous block (caret at its end), or the new first
    /// block when the first one was removed.
    EditResult remove(int i, bool withChildren = true);

    /// Tab: make list item `i` (with its subtree) the last child of its previous sibling. A
    /// paragraph or other non-list block is first converted to a list item (convertToList()).
    EditResult indent(int i);
    /// Shift+Tab: move list item `i` (with its subtree) one level up, in place; its following
    /// siblings become its children (Logseq's default, non-"logical" outdenting).
    EditResult outdent(int i);

    /// Alt+Up / Alt+Down: swap the subtree of `i` with the previous / next sibling subtree
    /// (top-level blocks of any kind are siblings); nothing changes without such a sibling. The
    /// frontmatter / page-property blocks at the top never move and nothing moves above them.
    EditResult moveUp(int i);
    EditResult moveDown(int i);

    /// Toggle / set the status of the task on line `lineInBlock` of block `i` with the semantics
    /// of markdown::toggleTask / setTaskStatus (✅/❌ dates, a non-task becomes a task); completing
    /// a recurring list item inserts its next occurrence as a new block before it (inside a quote
    /// or callout the copy stays in that block).
    EditResult toggleTask(int i, QDate today, int lineInBlock = 0);
    EditResult setTaskStatus(int i, TaskStatus status, QDate today, int lineInBlock = 0);

    /// The block id of `i`, adding one (` ^abc123`, see markdown::ensureBlockId) when missing;
    /// ids already used in this document, and those for which `isTaken` returns true (e.g. ids
    /// used elsewhere in the vault — `((id))` references resolve vault-wide), are avoided.
    BlockIdResult ensureBlockId(int i, const std::function<bool(const QString &)> &isTaken = {});

    /// Turn a non-list block (paragraph, heading, quote, code, …) into a top-level list item
    /// "- …" (continuation lines indented). Frontmatter and rules cannot be converted.
    EditResult convertToList(int i);

    /// The inverse for a top-level list item `i` — the document mode's Backspace at the start of
    /// a list item and Enter on an empty one (docs/UI.md "Note modes"): the block becomes what its
    /// text is without the list (a paragraph, or a heading / code block / quote when the text is
    /// one) in the same place. The marker and a task checkbox go, continuation lines lose the
    /// item's indentation, and its children move one level out (they start a list right after
    /// it). The list items around it stay lists (a blank line separates them from the block).
    /// Nested items and non-list blocks: nothing changes (outdent() a nested item instead).
    /// Focus: the block, caret at the start.
    EditResult convertToParagraph(int i);

    // --- Ranges: a block selection (docs/UI.md "Selecting and copying") -----------------------------
    // Rows [first, last] stand for their top blocks — the rows whose parent is outside the range —
    // each with its whole subtree (folded children too). One EditResult for the whole range.

    /// The top blocks of rows [first, last] (see above), ascending; frontmatter / page-property
    /// blocks are left out. Empty for an invalid range.
    QList<int> rangeTops(int first, int last) const;
    /// Remove rows [first, last] with the whole subtrees of their top blocks; ordered lists around
    /// them are renumbered. Focus: the block before the range (caret at its end), else the new
    /// first block.
    EditResult removeRange(int first, int last);
    /// Tab on a range: every top block becomes the last child of its previous sibling (indent()) —
    /// all or nothing: when one of them cannot go one level deeper (the first item of a list, a
    /// block after a non-list block) nothing changes (Logseq).
    EditResult indentRange(int first, int last);
    /// Shift+Tab on a range: every top list item that has a parent moves one level up (outdent()),
    /// the last one first, so the range keeps its order. Nothing changes when none can.
    EditResult outdentRange(int first, int last);
    /// Alt+Up / Alt+Down on a range of sibling blocks (consecutive siblings, each with its subtree):
    /// swapped with the previous / next sibling subtree. Nothing changes without one, when the top
    /// blocks are not consecutive siblings (a range rising through levels) or when it would move
    /// the frontmatter / page-property blocks or something above them.
    EditResult moveRangeUp(int first, int last);
    EditResult moveRangeDown(int first, int last);

private:
    struct Item {
        BlockEntry entry;
        QString own;              ///< exact source of the block's own lines, without the final terminator
        QString eol;              ///< terminator after the last own line ("" at the end of the document)
        QString contIndent;       ///< indentation written before continuation lines (list items)
        int markerCol = -1;       ///< list items: visual column of the marker
        quint64 parentUid = 0;    ///< uid of the parent at the last recompute (0 = top level)
        bool hasChildren = false; ///< at the last recompute
        int version = 0;          ///< bumped whenever the entry's data changes
        // Position-dependent parse options derive() used (frontmatter / page properties are only
        // recognised at the top of the document); recompute() re-derives when they change.
        bool frontmatterAllowed = false;
        bool propertiesAllowed = false;
        /// A `$$` line of the block's text opens a display formula its text does not close
        /// (Structure::displayMathOpenAtEnd): separateDisplayMath() keeps it from later blocks.
        bool displayMathOpen = false;
    };
    struct Snapshot {
        QList<quint64> uids;
        QList<int> versions;
    };

    Item makeItem(const QString &own, const QString &eol, const QString &trailing);
    /// The uid and exact text of blocks [from, to).
    QList<detail::BlockSource> sourcesOf(int from, int to) const;
    detail::BlockEdges edges() const;
    void setEdges(const detail::BlockEdges &edges);
    /// The position-dependent parse options of block `i` (frontmatter / page properties are only
    /// recognised at the top of the document).
    struct PositionalOptions {
        bool frontmatter = false;
        bool properties = false;
    };
    PositionalOptions positionalOptions(int i) const;
    void derive(int i);
    static void takeDerived(Item &to, const Item &from);
    void setOwn(int i, const QString &own);
    void rederivePositional();
    void recompute();
    void touch(int i);
    Snapshot snapshot() const;
    /// The normalisations every edit ends with, recompute() and the row changes since `before`.
    EditResult finish(const Snapshot &before, int focus, int cursor);
    /// The row changes since `before`: removed, moved, inserted rows, then rows whose version
    /// changed.
    EditResult rowChanges(const Snapshot &before, int focus, int cursor) const;
    /// revert() / reapply(): from side `from` of the delta to side `to` (0 older, 1 newer).
    EditResult applyDelta(const Delta &delta, int from, int to, bool *ok);

    int pinnedCount() const;
    bool isListItem(int i) const;
    bool isTextBlock(int i) const;
    int subtreeEnd(int i) const;
    int prevSibling(int i) const;
    int nextSibling(int i) const;
    QString leadingWhitespace(int i) const;
    QString indentUnit() const;
    QString childIndent(int parent) const;
    QString siblingPrefix(int ref, bool increment) const;
    QString childPrefix(int parent) const;
    QString serializeBody(const Item &item, const QString &body) const;
    void shiftIndent(int first, int end, const QString &from, const QString &to);
    void renumberChildren(int parent, quint64 restartUid = 0);
    void renumberAround(quint64 uid);
    void ensureSeparated(int i);
    void separateDisplayMath();
    void protectLeadingRule();
    void fixEnds();
    void insertItem(int pos, Item item, bool takeTrailingFromPrevious);
    void removeItems(int first, int end);
    void swapRanges(int a0, int b0, int b1);
    EditResult applyTaskEdit(int i, int lineInBlock, const QString &newOwn);

    QList<Item> m_items;
    QString m_bom;     ///< U+FEFF at the very start, kept separately
    QString m_leading; ///< blank lines before the first block
    QString m_eol = QStringLiteral("\n");
    bool m_finalNewline = true;
};

} // namespace brulion::markdown
