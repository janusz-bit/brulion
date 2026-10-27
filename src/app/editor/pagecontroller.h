// PageController — one page in the block editor (docs/UI.md "The block editor"): loads the file
// through an EditorHost, owns the BlockListModel, executes every editing command on the
// BlockDocument (each command is one undo step storing only the changed text; typing is
// coalesced per block / table cell), saves debounced (400 ms) and immediately on focus loss /
// app suspend / quit / path change / destruction (an unresolved conflict goes to a conflict copy
// instead, never over the other version), and handles changes made outside the editor (reload
// when clean, three-way merge when dirty, a conflict that follows later changes; renames via
// followRename()). Saves are compare-and-swap (EditorHost::writeIfUnchanged): a change another
// program wrote a moment before a save — an agent's brulion-cli, Syncthing — is merged first,
// never overwritten.
//
// Rows are BlockListModel rows (= document block indices). Cursor positions given to and
// returned by the controller are positions in the row's *edit text* (BlockListModel role
// `editText`: the body without the task marker / fence lines the delegate draws itself).
//
// The editing state lives here so that delegates can be destroyed and recreated by the
// ListView at any time: `editingRow` + `cursorPosition`. Whenever the controller changes the
// edit text of the row being edited by anything other than typing (undo/redo, completions,
// tokens, external merges, normalisation such as "[ ] " turning into a checkbox) it emits
// editTextReset(row, text, cursor) so the delegate's TextEdit can follow.
//
// Folding is device-local: kept in memory and reported by foldStateChanged() as the lines of
// folded blocks (`foldState`) for the shell to persist; restore with setFoldState().
//
// Block selection (docs/UI.md "Selecting and copying"): `selectionAnchorRow` and `selectedRow` (the
// focus end) are the two ends of a range of visible rows; the selection is that range with the
// whole subtree of every block in it (folded children too) — a set of uids in the model
// (BlockListModel::setSelectedUids). Blocks picked one by one (toggleSelected(): Ctrl+click, the
// touch tools' "Pojedynczo") make a selection with gaps: the picked blocks with their subtrees,
// both ends on the last one picked; a range (Shift) replaces it. All of it is uids: the
// selection survives edits, undo / redo (the undo steps restore it) and external merges. It is
// copied (Markdown text/plain plus the host's text/html, or plain text), cut, deleted, indented,
// moved (without gaps only) and its tasks' statuses set as one undo step each; a paste replaces
// it. Copies never claim success the clipboard did not take, and refuse more than
// ClipboardLimit bytes (never cut short). A paste drops block ids (`^id`, `id::`) that already
// resolve elsewhere — `((id))` is vault-wide —, except right after a cut (a move).
//
// Beyond the page itself: tasks shown in ```query results and embeds are checked through
// setTaskStatusAt() (this page: an undoable edit; other files: the host's QueryHost), pictures
// and files are pasted / dropped / picked as attachments (EditorHost::saveAttachment, embedded
// as `![[name]]`), and `titleRow` names a leading "# Title" the editor shows above the page
// properties.
//
// Note mode (docs/UI.md "Note modes"): `mode` is "outline" (Logseq-like) or "document"
// (Obsidian-like), resolved by markdown::resolveNoteMode when the page is loaded (the `brulion:`
// frontmatter key, else the vault's journal / page default and the content). It stays while the
// page is open — typing a list into a document does not turn it into an outline — and follows
// only a change of the key itself (undo, source mode, another device; without the key again the
// page has the mode it had without it) and, for a mode taken from the vault's defaults, a change
// of those (refreshMode()). setMode() writes the key (one undo step, saved at once). The
// document mode changes a few commands: Enter on an empty list item and Backspace at its start
// leave the list (leaveList()), Tab never makes a list item of a paragraph, new blocks after text
// are paragraphs, and there is no zoom and no folding (folds are kept for the outline mode).
#pragma once

#include "blocklistmodel.h"
#include "lazyhtmlmimedata.h"
#include "markdown/tokenedit.h"

#include <QDateTime>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>

class QUndoStack;

namespace brulion {
class EditorHost;
}

namespace brulion::editor {

/// Result of a three-way merge of external changes (see PageController::setMergeFunction).
struct MergeOutcome {
    QString text;
    bool clean = true;
};

class PageController : public QObject {
    Q_OBJECT
    Q_PROPERTY(brulion::EditorHost *host READ host WRITE setHost NOTIFY hostChanged FINAL)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged FINAL)
    /// Page title (file base name without .md).
    Q_PROPERTY(QString title READ title NOTIFY pathChanged FINAL)
    Q_PROPERTY(brulion::editor::BlockListModel *blocks READ blocks CONSTANT FINAL)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)
    /// The page was loaded (false before load()). A missing file loads as an empty new page.
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadingChanged FINAL)
    /// The file exists. A new page is created by the first save with content (an empty page or
    /// a lone empty bullet never creates a file).
    Q_PROPERTY(bool exists READ exists NOTIFY existsChanged FINAL)
    /// Row of the zoomed block (-1: whole page).
    Q_PROPERTY(int zoomBlock READ zoomBlock NOTIFY zoomChanged FINAL)
    /// Zoom path: [{ row, title }] from the outermost ancestor to the zoomed block.
    Q_PROPERTY(QVariantList breadcrumb READ breadcrumb NOTIFY zoomChanged FINAL)
    Q_PROPERTY(bool sourceMode READ sourceMode WRITE setSourceMode NOTIFY sourceModeChanged FINAL)
    /// The whole file (source mode edits it; leaving source mode applies it to the blocks).
    /// Text written without any "\r" (what a TextEdit reports) keeps the file's line endings:
    /// untouched lines keep their terminators, new line breaks get the document's.
    Q_PROPERTY(QString sourceText READ sourceText WRITE setSourceText NOTIFY sourceTextChanged FINAL)
    Q_PROPERTY(int editingRow READ editingRow WRITE setEditingRow NOTIFY editingRowChanged FINAL)
    Q_PROPERTY(int cursorPosition READ cursorPosition WRITE setCursorPosition NOTIFY cursorPositionChanged FINAL)
    /// Block selected with Esc (not editing), -1 when none — the focus end of a block selection
    /// (Shift+↑/↓ move it); setting it selects that one block.
    Q_PROPERTY(int selectedRow READ selectedRow WRITE setSelectedRow NOTIFY selectedRowChanged FINAL)
    /// The other end of the block selection (-1: nothing selected).
    Q_PROPERTY(int selectionAnchorRow READ selectionAnchorRow NOTIFY selectionChanged FINAL)
    /// Visible rows selected (0: none) and the first / last of them (-1: none).
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged FINAL)
    Q_PROPERTY(int selectionFirstRow READ selectionFirstRow NOTIFY selectionChanged FINAL)
    Q_PROPERTY(int selectionLastRow READ selectionLastRow NOTIFY selectionChanged FINAL)
    /// More than one visible row is selected.
    Q_PROPERTY(bool multiSelection READ multiSelection NOTIFY selectionChanged FINAL)
    /// Two or more blocks were picked one by one (toggleSelected()): Ctrl+Enter, Tab, Alt+↑/↓ act
    /// on the selection, not on its focus end alone.
    Q_PROPERTY(bool selectionPicked READ selectionPicked NOTIFY selectionChanged FINAL)
    /// The selected rows are one run (no gaps between picked blocks): only such a selection is
    /// indented, outdented or moved. True with nothing selected.
    Q_PROPERTY(bool selectionContiguous READ selectionContiguous NOTIFY selectionChanged FINAL)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged FINAL)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged FINAL)
    /// External changes could not be merged: the local text is kept (not saved until resolved);
    /// `conflictText` holds the other version (for BMergeView).
    Q_PROPERTY(bool conflict READ conflict NOTIFY conflictChanged FINAL)
    Q_PROPERTY(QString conflictText READ conflictText NOTIFY conflictChanged FINAL)
    /// Theme colours for the renderer: { link, unresolved, muted, codeBackground,
    /// highlightBackground, tagFg, tagBg, dueFg, dueBg, overdueFg, overdueBg, scheduledFg,
    /// scheduledBg, reminderFg, reminderBg, alarmFg, alarmBg, recurrenceFg, recurrenceBg,
    /// doneFg, doneBg, neutralFg, neutralBg } (QML colors or colour strings).
    Q_PROPERTY(QVariantMap palette READ palette WRITE setPalette NOTIFY paletteChanged FINAL)
    /// UI language of rendered dates and calculations: "pl" (default) or "en".
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged FINAL)
    /// Fixed clock "YYYY-MM-DD HH:mm" (tests, screenshots); empty = the real clock.
    Q_PROPERTY(QString now READ now WRITE setNow NOTIFY nowChanged FINAL)
    /// Debounce of automatic saves in ms (default 400; 0 saves on every change).
    Q_PROPERTY(int saveDelay READ saveDelay WRITE setSaveDelay NOTIFY saveDelayChanged FINAL)
    /// Lines of folded blocks (for the shell to persist per device).
    Q_PROPERTY(QVariantList foldState READ foldState NOTIFY foldStateChanged FINAL)
    /// Row of a leading "# Title" heading that repeats the page title — the first block after
    /// the page properties (-1 when there is none, and while zoomed). The editor shows the
    /// properties panel right after it; the page header need not show the title again.
    Q_PROPERTY(int titleRow READ titleRow NOTIFY titleRowChanged FINAL)
    /// The host also runs queries and changes tasks in other files (an EditorHost that is a
    /// QueryHost, see queryhost.h): live ```query results, checkable tasks in results and embeds.
    Q_PROPERTY(bool queriesAvailable READ queriesAvailable NOTIFY hostChanged FINAL)
    /// Name filters for picking a picture (FileDialog.nameFilters).
    Q_PROPERTY(QStringList imageNameFilters READ imageNameFilters CONSTANT FINAL)
    /// Note mode: "outline" | "document" (see the file comment).
    Q_PROPERTY(QString mode READ mode NOTIFY modeChanged FINAL)
    /// Where `mode` comes from: "frontmatter" (the note's `brulion:` key), "journal" (the vault's
    /// journal mode), "heuristic" (the page's content), "default" (the vault's page mode).
    Q_PROPERTY(QString modeSource READ modeSource NOTIFY modeChanged FINAL)
    /// mode === "document".
    Q_PROPERTY(bool documentMode READ documentMode NOTIFY modeChanged FINAL)
    /// Copying a link may write a new block id (` ^abc123`) into the page (default). The
    /// documentation notebook sets false: its guide pages stay as shipped, so they keep being
    /// brought up to date (docs/UI.md "The documentation notebook"). Links to headings and pages,
    /// and to blocks that already have an id, are copied either way.
    Q_PROPERTY(
            bool allowNewBlockIds READ allowNewBlockIds WRITE setAllowNewBlockIds NOTIFY allowNewBlockIdsChanged FINAL)

public:
    explicit PageController(QObject *parent = nullptr);
    ~PageController() override;

    EditorHost *host() const { return m_host; }
    void setHost(EditorHost *host);
    QString path() const { return m_path; }
    void setPath(const QString &path);
    QString title() const;
    BlockListModel *blocks() const { return m_model; }
    bool dirty() const { return m_dirty; }
    bool loading() const { return m_loading; }
    bool loaded() const { return m_loaded; }
    bool exists() const { return m_exists; }
    int zoomBlock() const;
    QVariantList breadcrumb() const;
    bool sourceMode() const { return m_sourceMode; }
    void setSourceMode(bool on);
    QString sourceText() const;
    void setSourceText(const QString &text);
    int editingRow() const;
    void setEditingRow(int row);
    int cursorPosition() const { return m_cursor; }
    void setCursorPosition(int cursor);
    int selectedRow() const;
    void setSelectedRow(int row);
    int selectionAnchorRow() const;
    int selectionCount() const { return m_selectionCount; }
    int selectionFirstRow() const { return m_selectionFirst; }
    int selectionLastRow() const { return m_selectionLast; }
    bool multiSelection() const { return m_selectionCount > 1; }
    bool selectionPicked() const { return !m_pickedUids.isEmpty(); }
    bool selectionContiguous() const { return m_selectionContiguous; }
    /// The most a copy puts on the clipboard (UTF-8 bytes): Android's clipboard takes about 1 MB
    /// per transaction. A larger copy is refused ("tooLarge"), never cut short.
    static constexpr qsizetype ClipboardLimit = 800 * 1024;
    /// Put `text` on the clipboard (refused over ClipboardLimit; on Linux the primary selection too;
    /// read back: { ok, reason: "tooLarge" | "noClipboard" }) with a text/html flavour that `html`
    /// renders when an application asks for it (LazyHtmlMimeData; none without `html`). Android and
    /// the browser take the data at once (clipboardReadsAtOnce()): rendered here, kept only while
    /// both flavours fit ClipboardLimit. Also the journal stream's copy (StreamSelection).
    static QVariantMap writeClipboard(const QString &text, const LazyHtmlMimeData::Render &html);
    bool canUndo() const;
    bool canRedo() const;
    bool conflict() const { return m_conflict; }
    QString conflictText() const { return m_conflictText; }
    QVariantMap palette() const { return m_palette; }
    void setPalette(const QVariantMap &palette);
    QString locale() const { return m_english ? QStringLiteral("en") : QStringLiteral("pl"); }
    void setLocale(const QString &locale);
    QString now() const { return m_fixedNow; }
    void setNow(const QString &now);
    int saveDelay() const { return m_saveTimer.interval(); }
    void setSaveDelay(int ms);
    QVariantList foldState() const;
    int titleRow() const { return m_titleRow; }
    bool queriesAvailable() const;
    QStringList imageNameFilters() const;
    QString mode() const;
    QString modeSource() const;
    bool documentMode() const { return m_mode == NoteMode::Document; }
    NoteMode noteMode() const { return m_mode; }
    bool allowNewBlockIds() const { return m_allowNewBlockIds; }
    void setAllowNewBlockIds(bool allow);
    /// Colours the renderer uses (from `palette`).
    const markdown::Palette &rendererPalette() const { return m_model->palette(); }
    /// The status a checkbox click gives: todo/doing/deferred -> "done", done/cancelled -> "todo".
    static QString toggledStatus(const QString &status);
    /// The status a task slash command sets ("task" -> "todo", "taskDoing" -> "doing", "taskDone",
    /// "taskWaiting" -> "deferred", "taskCancelled", "taskNone" -> "" = no status); nullopt for
    /// other commands.
    static std::optional<QString> slashTaskStatus(const QString &command);

    /// Three-way merge used for external changes while dirty (base, local, remote). Defaults to
    /// sync::merge3 when the sync module is available; without one, conflicts keep local.
    using MergeFunction = std::function<MergeOutcome(const QString &, const QString &, const QString &)>;
    void setMergeFunction(MergeFunction merge) { m_merge = std::move(merge); }
    /// Current document text.
    QString text() const { return m_model->text(); }
    /// The text last read from / written to the file (merge base).
    QString savedText() const { return m_savedText; }
    /// Characters held by the undo history (diagnostics, tests).
    qsizetype undoHistorySize() const;
    QDateTime currentDateTime() const;

    // --- Loading and saving ------------------------------------------------------------------------
    /// The day changed (midnight: DayClock, connected by the constructor): "dziś"/"jutro" chips
    /// and overdue colours are rendered again for the new date and todayChanged() tells the
    /// ```query results. Nothing happens on the same day.
    Q_INVOKABLE void refreshToday();
    /// (Re)load the page from the host (discards unsaved changes: save() first).
    Q_INVOKABLE void load();
    /// Write now when dirty (also flushes a pending debounced save). False when the write failed
    /// or a conflict with another program's change waits for the user. The write only replaces
    /// the text this editor last read or wrote; a newer one on disk is taken in first (reload or
    /// three-way merge) and the result saved.
    Q_INVOKABLE bool save();
    /// The file was renamed/moved (the shell's rename, sync): connect the host's rename signal
    /// here (Workspace.pathRenamed). The page follows to `newPath` keeping edits, undo and the
    /// caret; unsaved edits are saved there — the old path is never written again (setting
    /// `path` instead would first save to the old path and bring the page back as a duplicate).
    Q_INVOKABLE void followRename(const QString &oldPath, const QString &newPath);

    // --- Note mode -------------------------------------------------------------------------------
    /// Switch to "outline" or "document": writes the frontmatter key `brulion` (always — an explicit
    /// choice; markdown::setNoteModeKey changes nothing else in the file) as one undo step and
    /// saves. Also in source mode (the source text gets the key). False for an unknown mode, before
    /// loading, or when the note's frontmatter cannot be edited safely (nothing changed then).
    Q_INVOKABLE bool setMode(const QString &mode);
    /// setMode() with the other mode.
    Q_INVOKABLE bool toggleMode();
    /// The vault's journal / page mode may have changed (Workspace.configChanged): a page whose
    /// mode came from that default follows it. The content is not resolved again — a page never
    /// flips because of what was written since it was opened.
    Q_INVOKABLE void refreshMode();

    // --- Editing ---------------------------------------------------------------------------------
    Q_INVOKABLE void startEditing(int row, int cursor = -1);
    Q_INVOKABLE void stopEditing();
    /// Replace the whole body of a row (raw Markdown).
    Q_INVOKABLE void setBody(int row, const QString &body);
    /// Typing: replace the edit text of a row; `cursor` (edit text position) is remembered.
    Q_INVOKABLE void setEditText(int row, const QString &text, int cursor = -1);
    /// Enter: split at `cursor`. Returns { row, cursor } of the block that gets the caret.
    Q_INVOKABLE QVariantMap split(int row, int cursor);
    /// Backspace at the start: merge with the previous visible block. Returns { row, cursor }
    /// (row -1 when nothing happened).
    Q_INVOKABLE QVariantMap mergeWithPrevious(int row);
    /// Delete at the end: merge the next visible block into this one. Returns { row, cursor }.
    Q_INVOKABLE QVariantMap mergeNext(int row);
    /// Leave the list from list item `row` (document mode: Backspace at its start, Enter on an
    /// empty one): a nested item moves one level out, a top-level one becomes a paragraph in its
    /// place (BlockDocument::convertToParagraph: its marker and checkbox go, its children move out).
    /// Returns { row, cursor } of the caret (row -1 when nothing changed).
    Q_INVOKABLE QVariantMap leaveList(int row);
    /// The document mode's list button (touch toolbar): a paragraph becomes a list item "- …"
    /// (BlockDocument::convertToList; the caret keeps its place in the text), a list item leaves
    /// the list (leaveList()). Returns { row, cursor } (row -1 when nothing changed).
    Q_INVOKABLE QVariantMap toggleList(int row);
    Q_INVOKABLE bool indent(int row);
    Q_INVOKABLE bool outdent(int row);
    Q_INVOKABLE bool moveUp(int row);
    Q_INVOKABLE bool moveDown(int row);
    /// Checkbox click: todo/doing/deferred -> done, done/cancelled -> todo (recurring tasks get
    /// their next occurrence inserted above).
    Q_INVOKABLE void toggleTask(int row);
    /// Ctrl+Enter: none -> todo -> doing -> done -> none.
    Q_INVOKABLE void cycleTask(int row);
    /// Set a task status ("todo", "doing", "done", "cancelled", "deferred"; "" removes the marker)
    /// as one undo step: the status menu, Ctrl+Enter. A block that is no task becomes one. The
    /// caret of the block being edited stays in its words.
    Q_INVOKABLE void setTaskStatus(int row, const QString &status);
    /// Remove the task marker (the block stays a plain bullet).
    Q_INVOKABLE void removeTaskMarker(int row);
    /// Delete a block with its children. Returns the row that should get focus (-1: none).
    Q_INVOKABLE int deleteBlock(int row);
    /// Insert a new list item after `row` (its subtree) at the same depth; -1 = at the start.
    /// Document mode: after a block that is not a list item (and at the start) a paragraph.
    /// Returns the new row.
    Q_INVOKABLE int insertBlockAfter(int row, const QString &text = QString());
    /// Insert Markdown (templates, pasted text): replaces `row` when it is empty, else goes after
    /// its top-level block. Returns the first new row.
    Q_INVOKABLE int insertMarkdown(int row, const QString &markdown);
    /// Fill the template at vault path `templatePath` ({{date}}, {{title}}, …) and insert it
    /// like insertMarkdown(). Returns the first new row (-1 when the template cannot be read).
    Q_INVOKABLE int insertTemplate(int row, const QString &templatePath);
    /// Make sure the page ends with an empty block to type into (click below the last block): a
    /// list item, in document mode a paragraph (an empty list item at the end is used as it is).
    /// Returns its row.
    Q_INVOKABLE int appendEmptyBlock();
    /// Start writing in a page that does not exist yet (a day without a note): the template at
    /// vault path `templatePath` (filled for this page — a daily note for its own day; "" or
    /// unreadable: none) becomes the page's starting text. That is no edit: the file is created
    /// by the first change, with the template, and a page left untouched writes nothing — never
    /// a note just for looking at a day. Returns the row to edit: the template's first empty
    /// bullet, else an empty one added at the end. A page that exists or has text:
    /// appendEmptyBlock().
    Q_INVOKABLE int startNewPage(const QString &templatePath);

    // --- Zoom and folding -------------------------------------------------------------------------
    Q_INVOKABLE void zoomInto(int row);
    Q_INVOKABLE void zoomOut();
    /// Zoom to the breadcrumb entry `row` (-1: whole page).
    Q_INVOKABLE void zoomTo(int row);
    Q_INVOKABLE void toggleFold(int row);
    Q_INVOKABLE void setFolded(int row, bool folded);
    /// Fold everything with children (true) or unfold all (false).
    Q_INVOKABLE void foldAll(bool folded);
    /// Restore folds from lines (foldState of an earlier session).
    Q_INVOKABLE void setFoldState(const QVariantList &lines);
    /// Make a row visible: leave a zoom that does not contain it, unfold its ancestors.
    Q_INVOKABLE void reveal(int row);
    /// Row of the block containing document line `line` (-1 when none) — routes with `line`.
    Q_INVOKABLE int rowOfLine(int line) const;
    /// Row of the block with id `blockId` (-1 when none) — routes with `blockId`.
    Q_INVOKABLE int rowOfBlockId(const QString &blockId) const;
    /// Remember a block for something that acts on it later (a dialog): rows shift when the page
    /// changes meanwhile (sync, undo, edits). Returns a handle for trackedRow(); untrack() it.
    Q_INVOKABLE int trackBlock(int row);
    /// Current row of a tracked block; -1 when it is gone (removed, or itself changed by an
    /// external change or undo — it is not guessed which block it became).
    Q_INVOKABLE int trackedRow(int handle) const;
    Q_INVOKABLE void untrack(int handle);
    Q_INVOKABLE int nextVisibleRow(int row) const;
    Q_INVOKABLE int previousVisibleRow(int row) const;
    Q_INVOKABLE int lastVisibleRow() const;

    // --- Inline insertions --------------------------------------------------------------------------
    /// Insert a token (e.g. "📅 2026-10-15") at `cursor` of the row's edit text with spacing.
    /// Returns the new cursor.
    Q_INVOKABLE int insertToken(int row, int cursor, const QString &token);
    /// Replace [from, to) of the row's edit text. Returns the new cursor (end of `text`).
    Q_INVOKABLE int replaceRange(int row, int from, int to, const QString &text);
    /// Text with line breaks pasted / dropped / committed into [from, to) of a row's edit text
    /// (text and heading blocks): several Markdown blocks (a list, paragraphs) become blocks
    /// after this one (the first joins the text before the caret, relative nesting kept, the
    /// text after the caret goes to the end of the last one); a single multi-line block is
    /// inserted as is. One undo step. Returns { row, cursor } of the caret.
    Q_INVOKABLE QVariantMap pasteText(int row, int from, int to, const QString &text);
    /// The block id of a row, adding ` ^id` when missing as one undo step (ids any block of the
    /// vault has — EditorHost::blockIdTaken() — are avoided). "" when the block cannot carry an id.
    Q_INVOKABLE QString ensureBlockRef(int row);
    /// copyLink(row, "blockRef"). Returns the id ("" when nothing was copied).
    Q_INVOKABLE QString copyBlockRef(int row);

    // --- Block selection (docs/UI.md "Selecting and copying") --------------------------------------
    /// Select the visible rows from `anchor` to `focus` (in either order) with their subtrees;
    /// editing stops. The frontmatter / page properties are never part of a range (a lone one can
    /// be selected). Invalid rows clear the selection.
    Q_INVOKABLE void selectRange(int anchor, int focus);
    /// Move the focus end to `row` (Shift+click, a drag, a tap in the touch selection mode): from
    /// the anchor, or from the block being edited, else `row` alone.
    Q_INVOKABLE void extendSelectionTo(int row);
    /// Ctrl+click (a tap with the touch tools' "Pojedynczo"): `row` with its subtree into the
    /// selection or out of it, the rest stays — the selection may get gaps. A range becomes its
    /// top blocks first; the block being edited joins (editing stops). A row selected only as part
    /// of a selected block's subtree stays (it goes with that block). Both ends move to `row` when
    /// it was added, else to the block picked last before it. The page properties are never
    /// picked. Returns "added", "removed", "inParent" (nothing changed) or "" (not a row).
    Q_INVOKABLE QString toggleSelected(int row);
    /// Shift+↓ (`step` 1) / Shift+↑ (-1): the focus end goes to the next / previous visible row
    /// that changes the selection (it jumps over rows already inside the selected subtrees); with
    /// nothing selected the block being edited is the anchor. False when nothing changed.
    Q_INVOKABLE bool extendSelection(int step);
    /// Ctrl+A: every visible block (no frontmatter, no hidden note-mode row), in a zoom the zoomed
    /// block's subtree. Editing stops.
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    /// The selected rows (subtrees included, ascending).
    Q_INVOKABLE QVariantList selectedRows() const;
    /// The selection as Markdown (markdown::blocksMarkdown) / plain text (blocksPlainText): its
    /// top blocks in document order, each with its subtree (a selection with gaps too).
    Q_INVOKABLE QString selectionMarkdown() const;
    Q_INVOKABLE QString selectionPlainText() const;
    /// Copy the selection: Markdown as text/plain plus the host's text/html (EditorHost::renderHtml,
    /// rendered when another application asks for it: writeClipboard()), or `plainText`; on Linux also the primary selection. Returns { ok, count (visible blocks),
    /// reason: "empty" | "tooLarge" | "noClipboard" }.
    Q_INVOKABLE QVariantMap copySelection(bool plainText = false);
    /// Copy, then delete the selection (one undo step). The cut blocks' ids are kept by the next
    /// paste of this text (a move). copySelection()'s result plus { row } (selected afterwards).
    Q_INVOKABLE QVariantMap cutSelection();
    /// Delete the selection with its subtrees (one undo step, which restores the selection); the
    /// block before it is selected. Returns its row (-1: none).
    Q_INVOKABLE int deleteSelection();
    /// Tab / Shift+Tab on the selection (BlockDocument::indentRange — all or nothing — /
    /// outdentRange). The document mode indents only when every top block is a list item. A
    /// selection with gaps is neither indented nor moved (selectionContiguous).
    Q_INVOKABLE bool indentSelection();
    /// The selection could be indented / outdented at all: not the zoomed block, and in the
    /// document mode only list items (its paragraphs and headings have no outline). The
    /// selection tools offer "Zwiększ / Zmniejsz wcięcie" only then. (Gaps are not asked about
    /// here: the tools say why such a selection does not move.)
    Q_INVOKABLE bool canIndentSelection() const;
    Q_INVOKABLE bool outdentSelection();
    /// Alt+↑ / Alt+↓: the selected sibling blocks move together (BlockDocument::moveRangeUp/Down).
    Q_INVOKABLE bool moveSelectionUp();
    Q_INVOKABLE bool moveSelectionDown();
    /// Ctrl+Enter: every visible list item and paragraph the selection spans gets the status that
    /// follows the first one's (none → todo → doing → done → none). One undo step. The rows from
    /// one end to the other — the picked blocks of a selection with gaps: a parent selected alone
    /// changes, not the children it brings along (they are copied, moved and deleted with it).
    Q_INVOKABLE void cycleSelectionTasks();
    /// Set the task status of those blocks ("" removes it).
    Q_INVOKABLE void setSelectionTaskStatus(const QString &status);
    /// Ctrl+V with blocks selected: the selection is replaced by `text` (Markdown, split into
    /// blocks like pasteText(); the selection's own ids are kept; a pasted note's YAML
    /// frontmatter is left out, Logseq page properties become a block). The page's own properties
    /// are never replaced: selected, the paste goes in below them. One undo step; the pasted
    /// blocks are selected. Returns { row, count } (row -1: nothing pasted, nothing changed).
    Q_INVOKABLE QVariantMap replaceSelection(const QString &text);
    /// replaceSelection() with the clipboard's text (clipboardText()).
    Q_INVOKABLE QVariantMap pasteOverSelection();
    /// Copy [from, to) of a row's edit text exactly (no-break spaces kept — a TextEdit's own copy
    /// turns them into spaces): in-block Ctrl+C / Ctrl+X. { ok, reason } like copySelection().
    /// `cut`: the caller removes the text next (Ctrl+X) — its block ids go with it (a move, like
    /// cutSelection()).
    Q_INVOKABLE QVariantMap copyEditText(int row, int from, int to, bool cut = false);
    /// What a paste inserts: the clipboard's text, or — when the clipboard has HTML (a browser,
    /// an office app) and its text does not look like Markdown — that HTML as Markdown
    /// (EditorHost::htmlToMarkdown). This app's own copies (its last one, or HTML marked as made
    /// by Brulion — EditorHost::renderHtml — also after a restart) are their text: the Markdown.
    /// "" when there is no text.
    Q_INVOKABLE QString clipboardText() const;
    /// In-block Ctrl+V: pasteText() of clipboardText() into [from, to). { row, cursor }, row -1
    /// when the clipboard has no text.
    Q_INVOKABLE QVariantMap pasteClipboard(int row, int from, int to);

    // --- Links to this page, its headings and blocks (docs/SYNTAX.md §2 "Copied links") -----------
    /// The text of a link of `kind` to `row`:
    ///   "blockRef"  ((id))           — Logseq style, resolved in the whole notebook;
    ///   "blockLink" [[Page#^id]]     — Obsidian style;
    ///   "heading"   [[Page#Heading]] — `row` is a heading; a name another heading has too: an
    ///                                  Obsidian heading path [[Page#Parent#Heading]] when that is
    ///                                  unique (markdown::uniqueHeadingFragment), else [[Page#^id]];
    ///   "embed"     a heading's section ![[Page#Heading]] (the same fallbacks: ![[Page#^id]]),
    ///               any other block !((id));
    ///   "page"      [[Page]] (`row` is ignored).
    /// "Page" is linkTarget(). The block id kinds add an id when the block has none (one undo
    /// step, ensureBlockRef()); the heading and page kinds change nothing. Returns { ok, text,
    /// reason }; reason when not ok: "noPage" (no path), "noRow", "notHeading", "sourceMode" (the
    /// blocks are not being edited), "readOnly" (a new id is needed but allowNewBlockIds is
    /// false), "noId" (the block cannot carry an id: frontmatter, an unclosed code fence).
    Q_INVOKABLE QVariantMap linkText(int row, const QString &kind);
    /// linkText() put on the clipboard. Not ok with reason "noClipboard" (and the text) when the
    /// platform has no clipboard or it did not take the text — success is never claimed then.
    Q_INVOKABLE QVariantMap copyLink(int row, const QString &kind);
    /// The page as every copied link names it (notebook::pageLinkTarget(), the same text as the
    /// page menu and `brulion-cli block link`): a daily note by its file name, else the host's
    /// shortest link target (EditorHost::linkTarget(): the title, or the shortest folder path
    /// that tells it from a page of the same name, "Projekty/Dom").
    Q_INVOKABLE QString linkTarget() const;
    /// The row of the heading `fragment` names ([[Page#Heading]], an Obsidian heading path
    /// [[Page#A#B]], a fragment written by hand; markdown::findHeading — the matcher embeds use
    /// too); -1 when none.
    Q_INVOKABLE int rowOfHeading(const QString &fragment) const;
    /// The row of the heading whose section holds `row`: the last heading at or above it (the
    /// side panel's outline marks the same one, OutlineModel::entryForSourceRow); -1 when none.
    Q_INVOKABLE int headingRowFor(int row) const;
    /// `row` is a heading ("## …", also a Logseq "- ## …" item).
    Q_INVOKABLE bool isHeading(int row) const;
    /// Add a reminder (`⏰`/`⏰!` at `at`, "YYYY-MM-DD HH:mm"), or replace the reminder token at
    /// body range [from, to) (a clicked chip). `recurrence` sets the block's 🔁 rule
    /// (markdown::setBlockRecurrence(): only the rule's text, unchanged when it is the same rule);
    /// an empty rule removes it when editing an existing reminder. A caret at the end of
    /// the edited text that what was written reaches stays at the end, after an added space
    /// (never saved while nothing follows it: rememberEditorSpace()).
    Q_INVOKABLE void setReminder(int row, const QString &at, bool alarm, const QString &recurrence,
                                 int from = -1, int to = -1);
    /// Remove the token at body range [from, to).
    Q_INVOKABLE void removeToken(int row, int from, int to);
    /// Set (or remove with an empty spec) the date token of `field` ("due" 📅, "scheduled" ⏳,
    /// "start" 🛫) on the row's first line.
    Q_INVOKABLE void setDateToken(int row, const QString &field, const QString &spec);
    /// Set (or remove with "") the 🔁 rule of a row (any phrasing; written canonical): only the
    /// rule's text changes, words after it stay (markdown::setBlockRecurrence()).
    Q_INVOKABLE void setRecurrence(int row, const QString &rule);
    /// The reminder at body range [from, to): { at, alarm, recurrence, context } for
    /// BReminderDialog.edit(); empty when there is no reminder there.
    Q_INVOKABLE QVariantMap reminderAt(int row, int from, int to) const;
    /// The token a `date:FROM-TO`, `reminder:FROM-TO`, `recur:FROM-TO`, `prio:FROM-TO` or
    /// `day:FROM-TO` link of the row's rendered text names (its menu, DateChipMenu: a right-click or
    /// long press on it): { from, to } (body range), kind ("due" | "scheduled" | "start" | "created"
    /// | "done" | "cancelled" | "reminder" | "recurrence" | "priority" | "day"), at ("YYYY-MM-DD
    /// [HH:mm]"), date, time ("" without one), alarm, rule, priority, logseq, isTask (editor::
    /// tokenChip()); a day link also url (its page: link, what a click opens) and copyText; empty
    /// when the link names no token.
    Q_INVOKABLE QVariantMap dateChip(int row, const QString &url) const;
    /// One edit of a token menu on body range [from, to) of a row (markdown::applyTokenAction():
    /// "set", "move", "shift", "alarm", "reminder", "remove", "recurrence", "priority",
    /// "blockRecurrence", "blockPriority"; on a [[day]] link "set", "move", "shift") — only the
    /// token's bytes change. One undo step; false when nothing changed.
    Q_INVOKABLE bool applyTokenAction(int row, int from, int to, const QString &action, const QVariant &a = {},
                                      const QVariant &b = {});
    /// The token a link of a block copy's rendered text names: `raw` is the block's Markdown as a
    /// query result or an embed has it (an item of another page, rendered with renderContextFor())
    /// — what dateChip() returns, with `ref` naming the token for editTokenIn(); empty when there is
    /// none (day links are no tokens there: they are page links).
    Q_INVOKABLE QVariantMap tokenChipIn(const QString &raw, const QString &url) const;
    /// A token menu's edit (applyTokenAction()'s actions) on the token `ref` (tokenChipIn()) of the
    /// block on `line` of `path` whose Markdown the copy has as `raw` (a block with another text
    /// there: the nearest one with that text; none: nothing is written, "gone"): on this page an
    /// undoable edit of that block; in another file the
    /// edit is written like a hand edit (markdown::editBlockToken(), only if the file did not change
    /// meanwhile: EditorHost::writeIfUnchanged()) and can be undone with undoTokenEdit(undo).
    /// Returns { ok, undo (0: this page's undo, or nothing to undo), reason ("gone": the block or
    /// its token is not there any more, "changed", "failed", "unchanged") }.
    Q_INVOKABLE QVariantMap editTokenIn(const QString &path, int line, const QString &raw, const QVariantMap &ref,
                                        const QString &action, const QVariant &a = {}, const QVariant &b = {});
    /// Put back what editTokenIn() changed in another file (`undo`: its handle) — only while the
    /// file still holds what that edit wrote. False when it changed since or the handle is gone.
    Q_INVOKABLE bool undoTokenEdit(int undo);
    /// A token link of a table cell's rendered text (`url`, its range in the cell's text): as
    /// dateChip() with the range in the cell (tableRow -1 = header). Empty when there is none.
    Q_INVOKABLE QVariantMap tableCellChip(int row, int tableRow, int column, const QString &url) const;
    /// applyTokenAction() on range [from, to) of a table cell's text: only that cell's text changes
    /// in its row as written (the other cells, padding and pipes stay; a row that would read
    /// differently then is written anew with setTableCell()). One undo step; false when nothing
    /// changed.
    Q_INVOKABLE bool applyTableCellToken(int row, int tableRow, int column, int from, int to, const QString &action,
                                         const QVariant &a = {}, const QVariant &b = {});
    /// The date token at body range [from, to) gets `spec` ("YYYY-MM-DD [HH:mm]") — only its value
    /// changes; an empty spec removes the token. One undo step.
    Q_INVOKABLE void setDateTokenAt(int row, int from, int to, const QString &spec);
    /// The date token at [from, to) moved by `days` and `minutes` from its own date (shiftedSpec;
    /// a reminder without a time starts from the default reminder time).
    Q_INVOKABLE void shiftDateTokenAt(int row, int from, int to, int days, int minutes = 0);
    /// The date token at [from, to) moved to today + `days`, its time kept.
    Q_INVOKABLE void moveDateTokenTo(int row, int from, int to, int days);
    /// The reminder at [from, to) becomes an alarm (`⏰!`) or a plain reminder (`⏰`).
    Q_INVOKABLE void setReminderAlarmAt(int row, int from, int to, bool alarm);
    /// Text of a row for dialog context (tokens and markers stripped).
    Q_INVOKABLE QString blockContext(int row) const;
    /// Current values of a row's date tokens: { due, scheduled, start, recurrence }.
    Q_INVOKABLE QVariantMap blockDates(int row) const;
    /// Journal page link for a date "YYYY-MM-DD": "[[2026-10-15]]" (journal file format of the vault).
    Q_INVOKABLE QString journalLink(const QString &date) const;

    /// Accept a completion: `trigger` from SuggestionModel.detect(), `item` from
    /// SuggestionModel.get(). Returns { cursor, action } — action "" or, for slash commands
    /// that need a dialog: "reminder", "alarm", "due", "scheduled", "recurrence", "date",
    /// "template", or "complete" (a new completion should be started, e.g. after "![[").
    Q_INVOKABLE QVariantMap applyCompletion(int row, const QVariantMap &trigger, const QVariantMap &item);
    /// Run a slash command on [from, to) (the "/query" text) of the row's edit text.
    Q_INVOKABLE QVariantMap applySlashCommand(int row, int from, int to, const QString &command);

    // --- Tables -----------------------------------------------------------------------------------
    /// Set a cell (tableRow -1 = header) and rewrite the table. `typing`: the cell editor calls
    /// this on every change, so nothing typed waits for the cell to be left (suspend, close);
    /// consecutive typing in one cell is one undo step.
    Q_INVOKABLE void setTableCell(int row, int tableRow, int column, const QString &text, bool typing = false);
    /// Esc in a cell editor: undo that cell's typing (its one undo step), so the table is back to
    /// its exact previous text; nothing happens when the cell was not changed.
    Q_INVOKABLE void cancelTableCell(int row, int tableRow, int column);
    Q_INVOKABLE void addTableRow(int row);
    Q_INVOKABLE void addTableColumn(int row);
    Q_INVOKABLE void removeTableRow(int row, int tableRow);

    // --- Links ----------------------------------------------------------------------------------
    /// A link of the rendered text was activated (page:, tag:, block:, ext:, task:, reminder:;
    /// date: chips do nothing here — a click on them edits the text, their menu is dateChip()).
    /// Returns {} when handled; { action: "reminder", from, to } for the reminder dialog;
    /// { action: "scrollTo", row } for a block or heading of this page — `((id))`, `[[#^id]]`,
    /// `[[#Heading]]` and links naming this page (`[[ThisPage#^id]]`, `[[ThisPage#A#B]]`), every
    /// time, also when the route would not change; { action: "notFound", kind: "block" |
    /// "heading" | "ref" (a `((id))`), name } when that block or heading is nowhere (a block moved
    /// to another page is opened there: EditorHost::blockIdTaken once the index is complete, the
    /// shell's route resolution).
    Q_INVOKABLE QVariantMap activateLink(int row, const QString &url, bool newPane = false);
    /// What the menu of a link (LinkMenu: a right-click or long press on a page:, tag:, block: or
    /// ext: link) needs — `fromPath`: the page the link is written on ("" = this one; query
    /// results and embeds of other pages): { kind ("page" | "tag" | "block" | "ext"), unresolved
    /// (a page that does not exist yet), path (the page it opens), tag, copyText (what "Kopiuj"
    /// puts on the clipboard: `[[Page#Heading]]` by the page's shortest link target — it works
    /// pasted on any page —, as written when it does not exist; `#tag` / `#[[tag with spaces]]`;
    /// `((id))`; the address) }; empty for other links.
    Q_INVOKABLE QVariantMap linkMenu(const QString &url, const QString &fromPath = QString()) const;
    /// Put `text` on the clipboard: { ok, text, reason ("noClipboard" when the platform has none
    /// or it did not take the text) } — as copyLink() reports it.
    Q_INVOKABLE QVariantMap copyText(const QString &text);
    /// Open a route through the host (the shell's navigation; `newPane`: the side panel).
    Q_INVOKABLE void navigate(const QVariantMap &route, bool newPane = false);

    // --- Tasks and blocks elsewhere (query results, embeds) ------------------------------------------
    /// Set the status of the task on `line` (0-based) of `path` ("" = no status: the marker goes):
    /// on this page as an editing command (undo), in other files through the host's QueryHost.
    /// False when nothing changed or the host cannot edit other files.
    Q_INVOKABLE bool setTaskStatusAt(const QString &path, int line, const QString &status);
    /// A checkbox click on a task shown elsewhere: toggledStatus(currentStatus) at path:line.
    Q_INVOKABLE bool toggleTaskAt(const QString &path, int line, const QString &currentStatus);
    /// Open `path` at `line` (-1: the page): this page gives { action: "scrollTo", row } for the
    /// editor, other pages are opened through the host (route { view: "page", path, line }).
    Q_INVOKABLE QVariantMap openLocation(const QString &path, int line, bool newPane = false);

    // --- Attachments (pasted / dropped / picked pictures and files) ----------------------------------
    /// The clipboard holds a picture (a screenshot, a copied image) or copied files to paste as
    /// attachments instead of text.
    Q_INVOKABLE bool clipboardHasImage() const;
    /// Paste the clipboard's picture (PNG "Pasted image <yyyyMMddHHmmss>.png") or copied files as
    /// attachments into [from, to) of the row's edit text. Returns { row, cursor } of the caret
    /// (row -1 when there was nothing to paste or saving failed).
    Q_INVOKABLE QVariantMap pasteImage(int row, int from, int to);
    /// Store `data` as the attachment `fileName` (EditorHost::saveAttachment) and insert its embed
    /// `![[name]]` into [from, to) of the row's edit text — or, for row -1 or a block that is not
    /// text, as a new block after the row (at the end). Returns { row, cursor } (row -1: failed).
    Q_INVOKABLE QVariantMap insertAttachment(int row, int from, int to, const QString &fileName, const QByteArray &data);
    /// Dropped or picked files (file:// URLs, Android content:// documents): each is stored as an
    /// attachment and embedded like insertAttachment(). Returns { row, cursor, count }.
    Q_INVOKABLE QVariantMap insertFiles(int row, int from, int to, const QList<QUrl> &urls);
    /// Like insertFiles(), always as a new block after `row` (-1: at the end of the page) — files
    /// dropped on a block that is not being edited.
    Q_INVOKABLE QVariantMap insertFilesAfter(int row, const QList<QUrl> &urls);
    /// Browser (WebAssembly): the browser's file picker for a picture, inserted asynchronously like
    /// insertAttachment() into the row (tracked while the picker is open). False elsewhere — use
    /// a FileDialog and insertFiles() there.
    Q_INVOKABLE bool pickImageInBrowser(int row, int from, int to);
    /// The embed for an attachment at vault path `path`: "![[name]]" when the name finds it,
    /// else "![[path]]".
    Q_INVOKABLE QString embedFor(const QString &path) const;

    // --- Pictures from the internet (`![alt](https://…)`, docs/SYNTAX.md §2) --------------------
    /// `url` is the address of a picture from the internet (http or https with a host).
    Q_INVOKABLE bool isWebPictureUrl(const QString &url) const;
    /// Insert the picture from the internet `url` as `![](url)` into [from, to) of the row's edit
    /// text — or a block of its own after a block that is not text, like insertAttachment() — and,
    /// with `download`, store it in the notebook right away (downloadWebPictures()). Returns
    /// { row, cursor } (row -1: no web address).
    Q_INVOKABLE QVariantMap insertWebPicture(int row, int from, int to, const QString &url, bool download);
    /// Put a picture's address on the clipboard; false when the platform has none.
    Q_INVOKABLE bool copyWebPictureUrl(const QString &url);
    /// Show the picture from the internet `url` (as written): the user's "Pokaż" while pictures
    /// from the internet are shown only when asked (EditorHost::loadWebPicture).
    Q_INVOKABLE void showWebPicture(const QString &url);
    /// Store pictures from the internet as attachments of the notebook and point the page's
    /// `![alt](url)` at them (retargetWebPicture(), one undo step per picture): the picture
    /// `url`, or — "" — every picture from the internet on the page. Fetched first when not
    /// cached (also while pictures are not shown); webPicturesDownloaded() once all are done.
    /// Returns how many pictures are being stored, those already on their way included (0:
    /// none; -1: refused, the page is in source mode). A picture whose fetch ends in source mode, or that
    /// no longer is on the page in a block showing pictures, is not stored (named as failed).
    Q_INVOKABLE int downloadWebPictures(const QString &url = QString());
    /// The pictures from the internet on the page: their URLs as written, each once.
    Q_INVOKABLE QStringList webPictureUrls() const;
    /// Pictures being stored by downloadWebPictures().
    Q_INVOKABLE int webPictureDownloads() const { return int(m_webDownloads.size()); }

    // --- Undo -------------------------------------------------------------------------------------
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    /// Where the undo history stands — taken right after an edit, it names that edit for a
    /// toast's "Cofnij" (undoStep()).
    Q_INVOKABLE QString undoToken() const;
    /// Undo the edit `token` (undoToken() right after it) while it is still the last step of the
    /// history: false — nothing undone — when other edits came after it, it was undone already,
    /// the history was reset (another page, a reload) or the page is in source mode.
    Q_INVOKABLE bool undoStep(const QString &token);

    // --- Conflicts --------------------------------------------------------------------------------
    /// Use `text` (e.g. from BMergeView) and save it.
    Q_INVOKABLE void resolveConflict(const QString &text);
    Q_INVOKABLE void keepLocal();
    Q_INVOKABLE void takeRemote();

Q_SIGNALS:
    void hostChanged();
    void pathChanged();
    void dirtyChanged();
    void loadingChanged();
    void existsChanged();
    void zoomChanged();
    void sourceModeChanged();
    void sourceTextChanged();
    void editingRowChanged();
    void cursorPositionChanged();
    void selectedRowChanged();
    /// The block selection changed (its ends, its rows, how many are shown).
    void selectionChanged();
    void undoStateChanged();
    void conflictChanged();
    void paletteChanged();
    void localeChanged();
    void nowChanged();
    /// The date the page is rendered for changed (midnight, or a new fixed clock `now`).
    void todayChanged();
    void saveDelayChanged();
    void foldStateChanged();
    void titleRowChanged();
    void modeChanged();
    void allowNewBlockIdsChanged();
    /// An attachment could not be read or stored (file name for the message).
    void attachmentFailed(const QString &name);
    /// downloadWebPictures() finished: `stored` pictures are attachments now, `failed` names the
    /// ones that could not be fetched or stored (webpicture::displayName()).
    void webPicturesDownloaded(int stored, const QStringList &failed);
    /// The edit text of `row` changed by something other than typing in it.
    void editTextReset(int row, const QString &text, int cursor);
    void saved();
    void saveFailed();
    /// The file changed outside and was reloaded / merged.
    void externallyChanged(bool merged);
    /// The file was deleted outside the editor (the text is kept, unsaved).
    void fileRemoved();

private:
    friend class EditCommand;
    struct Focus {
        int row = -1;
        int cursor = -1;
        /// The block selection (anchor, focus rows; the picked rows of one with gaps), restored
        /// when no row is being edited.
        int selectionAnchor = -1;
        int selectionFocus = -1;
        QList<int> selectionPicks = {};
    };

    void onFileChanged(const QString &path);
    /// Tell the hosts which file this editor shows (EditorHost::editorOpened/editorClosed).
    void showFile(EditorHost *host, const QString &path);
    void markDirty();
    void setDirty(bool dirty);
    void setConflict(bool conflict, const QString &remote = QString());
    QDate today() const;
    Focus currentFocus() const;
    /// Start/finish a compound command (one undo step).
    void beginCommand();
    void endCommand(const Focus &after);
    bool inCommand() const { return m_commandDepth > 0; }
    /// setTaskStatus() inside the current command (also the task slash commands).
    void applyTaskStatus(int row, const QString &status);
    /// Restore a text snapshot (undo/redo), its empty paragraphs and focus.
    void restore(const QString &text, const Focus &focus, const QList<markdown::BlankBlock> &blankBlocks);
    /// The document's empty paragraphs (an undo step keeps them: the text has none).
    QList<markdown::BlankBlock> blankBlocks() const;
    markdown::EditResult applyOp(const std::function<markdown::EditResult(markdown::BlockDocument &)> &op);
    /// Body edit of one row as one command.
    void editBody(int row, const QString &body, int focusCursor = -2);
    /// Apply a whole new document text as one command (keeps unchanged rows).
    void applyText(const QString &text, const Focus &after);
    /// Move the caret to a document index + body cursor from an EditResult.
    Focus focusFromResult(const markdown::EditResult &r, int fallbackRow) const;
    void setFocus(const Focus &focus, bool resetText);
    void emitEditTextReset();
    void closeTypingSession() { m_typingOpen = false; }
    bool validRow(int row) const;
    int toBodyCursor(int row, int editCursor) const;
    int toEditCursor(int row, int bodyCursor) const;
    void rebuildPalette();
    void writeConflictCopy();
    /// Follow kept blocks after BlockListModel::replaceContent (editing, selection, tracked).
    void remapBlocks(const QHash<quint64, quint64> &mapping);
    /// The selected rows [first, first + count) as one run: the rows from the anchor to the focus
    /// with their subtrees, or the picked blocks when they leave no gap; empty when nothing is
    /// selected or the picked blocks have gaps between them.
    markdown::RowRange selectionRange() const;
    markdown::RowRange rangeOf(int anchor, int focus) const;
    /// The top blocks of the selection (each stands for its subtree), ascending.
    QList<int> selectionTops() const;
    /// Every selected row (subtrees included), ascending.
    QList<int> selectionRowList() const;
    /// Recompute the selection after the document, folds or ends changed: the model's uid set,
    /// the counts; signals when something changed.
    void updateSelection();
    /// Drop the selection's state (no signals: updateSelection() follows).
    void resetSelection()
    {
        m_selectedUid = m_anchorUid = 0;
        m_pickedUids.clear();
    }
    /// `text` (pasted Markdown) without the block ids that already resolve elsewhere; ids in
    /// `freed` (blocks cut or replaced) are kept unless this page still has them. Completes the
    /// host's index first unless `indexed` (it signals: not inside a command).
    QString withoutTakenIds(const QString &text, const QSet<QString> &freed, bool indexed = false);
    /// The ids freed by the last cut when `text` is what it copied (a move), else none — and not
    /// those the cut page has again (the cut undone: a paste would duplicate them).
    QSet<QString> cutIdsFor(const QString &text) const;
    /// `text` is what this app put on the clipboard last (line breaks aside).
    bool isOwnCopy(const QString &text) const;
    /// Remember a cut of `text` from this page that took `ids` with it (cutIdsFor()).
    void recordCut(const QString &text, const QSet<QString> &ids);
    /// A block of this page has `id`.
    bool hasBlockId(const QString &id) const;
    /// The visible rows from one end of the selection to the other (the picked blocks of one with
    /// gaps) that can have a task status (list items, paragraphs).
    QList<quint64> selectedTaskUids() const;
    /// Save now; while a conflict is unresolved (also one this save found) write the conflict
    /// copy instead — before the page's text goes (another page, another host, destruction).
    void flush();
    void updateTitleRow();
    /// The text written to the file: text() with the lone opening fences of text blocks that
    /// would run on over the blocks after them closed (loneFenceClosed(), loneFenceRunsOn()) —
    /// "```js" being typed is saved as a closed, empty code block — and without the space the
    /// editor wrote after a token at the end of the edited block (rememberEditorSpace()).
    QString textToSave() const;
    /// The edited block at `row` is to end with `tail`: a token the editor wrote (a completed date or
    /// tag, a reminder from the dialog) and the space it added after it, so that the caret does
    /// not stick to the token. The space is not saved while the block still ends with `tail`
    /// (textToSave()) and goes when the caret leaves the block (dropEditorSpace()).
    void rememberEditorSpace(int row, const QString &tail);
    /// The body of `entry` without the remembered editor's space, or empty when it has none.
    QString bodyWithoutEditorSpace(const markdown::BlockEntry &entry) const;
    /// Leaving block `uid`: the editor's space after its last token goes (in the undo step that
    /// wrote it when that is the last one, else in the enclosing command or one of its own).
    void dropEditorSpace(quint64 uid);
    /// Leaving block `uid` (the caret moves to another block, editing stops): a body that is only
    /// an opening fence line gets its closing fence (in the undo step of the typing that wrote
    /// it, else in one of its own) when it was edited since the block got the caret or would run
    /// on over the blocks after it.
    void closeLoneFence(quint64 uid);
    /// Insert embeds of stored attachments (see insertAttachment()).
    QVariantMap insertEmbeds(int row, int from, int to, const QStringList &paths, bool newBlock = false);
    /// Read and store files as attachments (attachmentFailed() for each one that fails).
    QStringList storeFiles(const QList<QUrl> &urls);
    /// Resolve the note mode: `force` from scratch (loading), else only when the frontmatter key
    /// changed since the last resolution (edits, undo, source mode, external changes); without
    /// the key the page gets the mode it had without it in this session. `current`: the page's
    /// text when the caller has it.
    void resolveMode(bool force, const QString *current = nullptr);
    void applyMode(NoteMode mode, int source);

    QPointer<EditorHost> m_host;
    QString m_path;
    QPointer<EditorHost> m_shownHost; ///< told by showFile() that this editor shows m_shownPath
    QString m_shownPath;
    BlockListModel *m_model = nullptr;
    QUndoStack *m_undo = nullptr;
    int m_undoGeneration = 0; ///< bumped whenever m_undo is cleared (undoToken())
    QTimer m_saveTimer;
    bool m_saving = false; ///< save() is inside the host's compare-and-swap write
    QString m_savedText;
    bool m_dirty = false;
    bool m_loading = false;
    bool m_loaded = false;
    bool m_exists = false;
    bool m_sourceMode = false;
    QString m_sourceText;
    QString m_sourceBefore;
    bool m_sourceEdited = false;
    quint64 m_editingUid = 0;
    int m_cursor = -1;
    quint64 m_selectedUid = 0; ///< focus end of the block selection
    quint64 m_anchorUid = 0;   ///< anchor end
    int m_selectionCount = 0;
    int m_selectionFirst = -1;
    int m_selectionLast = -1;
    int m_selectionAnchorRow = -1; ///< at the last updateSelection() (signals when rows shift)
    int m_selectionFocusRow = -1;
    /// Blocks picked one by one (toggleSelected()), in the order picked, none inside another's
    /// subtree; empty: the selection is the range from the anchor to the focus. Never just one
    /// (that is a range of one block).
    QList<quint64> m_pickedUids;
    bool m_selectionContiguous = true;
    QHash<int, quint64> m_tracked; ///< trackBlock() handles
    /// editTokenIn() edits of other files, for undoTokenEdit(): handle -> { path, before, after }.
    struct ForeignEdit {
        QString path;
        QString before;
        QString after;
    };
    QHash<int, ForeignEdit> m_foreignEdits;
    int m_nextForeignEdit = 1;
    /// What the token edits need besides the text (today, the default reminder time, journals).
    markdown::TokenEditContext tokenEditContext() const;
    int m_nextTrackHandle = 1;
    bool m_conflict = false;
    QString m_conflictText;
    QString m_conflictCopyPath; ///< conflict copy written for the current conflict
    QString m_conflictCopyText;
    QVariantMap m_palette;
    bool m_english = false;
    QString m_fixedNow;
    MergeFunction m_merge;
    int m_titleRow = -1;
    NoteMode m_mode = NoteMode::Outline;
    int m_modeSource = -1;             ///< markdown::NoteModeSource; -1 before loading
    std::optional<NoteMode> m_modeKey; ///< the frontmatter key at the last resolution
    /// The mode without the key in this session (resolved from the content once, the first time
    /// the page has no key; the defaults follow refreshMode()); source -1: not resolved yet.
    NoteMode m_keylessMode = NoteMode::Outline;
    int m_keylessSource = -1;
    bool m_allowNewBlockIds = true;

    // Command bookkeeping.
    int m_commandDepth = 0;
    /// downloadWebPictures(): the URLs still to store, the results so far.
    QStringList m_webDownloads;
    int m_webStored = 0;
    QStringList m_webFailed;
    void onWebPictureChanged(const QString &url);
    /// Insert `inserted` (embeds) into [from, to) of a text row with blanks around it, else as a
    /// block of its own after the row (at the end for -1); { row, cursor }.
    QVariantMap insertEmbedText(int row, int from, int to, const QString &inserted, bool newBlock);
    /// Store the ready (or failed) picture `url` of m_webDownloads.
    void finishWebDownload(const QString &url, bool ready);
    QString m_commandBefore;
    QList<markdown::BlankBlock> m_commandBlankBefore;
    Focus m_commandFocusBefore;
    bool m_typingOpen = false;
    quint64 m_typingUid = 0;
    QPoint m_typingCell{-1, -2}; ///< table cell (column, table row) of a table typing session
    bool m_restoring = false;
    bool m_undoing = false; ///< restore() runs (undo/redo): moving the caret changes no text
    /// > 0: a selection command runs — updateSelection() waits for its end (no empty selection in
    /// between: the touch selection mode would end).
    int m_selectionHold = 0;
    /// The block that got the caret last and its body then (closeLoneFence()).
    quint64 m_focusedUid = 0;
    QString m_focusedBody;
    /// The block with the editor's space and the token with that space (rememberEditorSpace()).
    quint64 m_editorSpaceUid = 0;
    QString m_editorSpaceTail;
};

} // namespace brulion::editor
