// BlockDocument — the editable model behind the block editor (docs/UI.md "The block editor").
// Part of the markdown module — see markdown/markdown.h for the module overview.
//
// A note is a flat list of blocks in document order: every list item (at any depth) is a block,
// and every other top-level block (paragraph, heading, code, table, quote, callout, rule, HTML,
// frontmatter) is a block of depth 0 with an empty prefix. A callout or quote is one block
// together with everything inside it. Each block owns a contiguous range of lines: its own text
// (`prefix` + `body`) and the blank lines after it (`trailing`).
//
// Round trip: toString() reproduces the original content byte for byte when nothing changed
// (any mix of blocks, tabs/spaces/numbering, CRLF, BOM, missing final newline). Operations
// rewrite only the blocks they touch; everything else keeps its exact bytes. The only edits to
// neighbours are the ones Markdown needs to keep the structure: a blank line between blocks
// that would otherwise merge (e.g. a paragraph inserted after a list item), renumbering of an
// affected ordered list, the final line break when the last block changes, removing the
// indentation of a non-list block that follows a list item (it would continue the item), and a
// blank line before a `---` rule that becomes the first line (it would open YAML frontmatter).
//
// Every operation returns an EditResult: ordered row changes (for QAbstractItemModel
// begin/end calls) plus a suggested focus block and caret position. Folding is device-local and
// not part of the document; operations that depend on it take a `collapsed` flag.
#pragma once

#include "types.h"

#include <QDate>
#include <QList>
#include <QString>

#include <functional>
#include <optional>

namespace brulion::markdown {

/// One block (one row of the block editor).
struct BlockEntry {
    quint64 uid = 0; ///< stable identity while the document lives (survives moves)
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

/// Result of BlockDocument::ensureBlockId().
struct BlockIdResult {
    QString id;      ///< empty when the block cannot carry an id
    EditResult edit; ///< the change when an id had to be added
};

class BlockDocument {
public:
    BlockDocument();
    explicit BlockDocument(const QString &content);

    /// Replace the whole document (all uids are new).
    void setContent(const QString &content);
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

    /// Enter at `cursor` in block `i` (list items, paragraphs, headings): the text after the
    /// cursor moves to a new block; line breaks right at the split point are dropped (no blank
    /// lines at the edges of either block). List items: same indentation and marker (numbered lists
    /// continue: 3. -> 4.), tasks continue with `[ ] ` (or TODO/LATER); a block with children
    /// gets the new block as its first child unless `collapsed`. Paragraphs/headings: the new
    /// block is a paragraph (separated by a blank line). Cursor 0 in a non-empty block inserts an
    /// empty block before it instead. Focus: the new block (caret after a task marker), or the
    /// original block for cursor 0.
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
    };
    struct Snapshot {
        QList<quint64> uids;
        QList<int> versions;
    };

    Item makeItem(const QString &own, const QString &eol, const QString &trailing);
    void derive(int i);
    void setOwn(int i, const QString &own);
    void rederivePositional();
    void recompute();
    void touch(int i);
    Snapshot snapshot() const;
    EditResult finish(const Snapshot &before, int focus, int cursor);

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
    quint64 m_nextUid = 1;
};

} // namespace brulion::markdown
