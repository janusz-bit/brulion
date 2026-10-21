// PageController — one page in the block editor (docs/UI.md "The block editor"): loads the file
// through an EditorHost, owns the BlockListModel, executes every editing command on the
// BlockDocument (each command is one undo step storing only the changed text; typing is
// coalesced per block / table cell), saves debounced (400 ms) and immediately on focus loss /
// app suspend / quit / path change / destruction (an unresolved conflict goes to a conflict copy
// instead, never over the other version), and handles changes made outside the editor (reload
// when clean, three-way merge when dirty, a conflict that follows later changes; renames via
// followRename()).
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
    /// Block selected with Esc (not editing), -1 when none.
    Q_PROPERTY(int selectedRow READ selectedRow WRITE setSelectedRow NOTIFY selectedRowChanged FINAL)
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
    /// Write now when dirty (also flushes a pending debounced save). False when the write failed.
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
    /// The block id of a row, adding ` ^id` when missing (ids used elsewhere in the vault are
    /// avoided). "" when the block cannot carry an id.
    Q_INVOKABLE QString ensureBlockRef(int row);
    /// ensureBlockRef() and put "((id))" on the clipboard. Returns the id.
    Q_INVOKABLE QString copyBlockRef(int row);
    /// Add a reminder (`⏰`/`⏰!` at `at`, "YYYY-MM-DD HH:mm"), or replace the reminder token at
    /// body range [from, to) (a clicked chip). `recurrence` (canonical rule) sets the block's 🔁
    /// token; an empty rule removes it when editing an existing reminder.
    Q_INVOKABLE void setReminder(int row, const QString &at, bool alarm, const QString &recurrence,
                                 int from = -1, int to = -1);
    /// Remove the token at body range [from, to).
    Q_INVOKABLE void removeToken(int row, int from, int to);
    /// Set (or remove with an empty spec) the date token of `field` ("due" 📅, "scheduled" ⏳,
    /// "start" 🛫) on the row's first line.
    Q_INVOKABLE void setDateToken(int row, const QString &field, const QString &spec);
    /// Set (or remove with "") the 🔁 rule of a row (any phrasing; written canonical).
    Q_INVOKABLE void setRecurrence(int row, const QString &rule);
    /// The reminder at body range [from, to): { at, alarm, recurrence, context } for
    /// BReminderDialog.edit(); empty when there is no reminder there.
    Q_INVOKABLE QVariantMap reminderAt(int row, int from, int to) const;
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
    /// A link of the rendered text was activated (page:, tag:, block:, ext:, task:, reminder:).
    /// Returns "" when handled, or { action: "reminder", from, to } for the reminder dialog.
    Q_INVOKABLE QVariantMap activateLink(int row, const QString &url, bool newPane = false);

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

    // --- Undo -------------------------------------------------------------------------------------
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

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
    /// An attachment could not be read or stored (file name for the message).
    void attachmentFailed(const QString &name);
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
    /// Restore a text snapshot (undo/redo) and focus.
    void restore(const QString &text, const Focus &focus);
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
    /// Save now, or write the conflict copy while a conflict is unresolved.
    void flush();
    void updateTitleRow();
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
    QTimer m_saveTimer;
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
    quint64 m_selectedUid = 0;
    QHash<int, quint64> m_tracked; ///< trackBlock() handles
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

    // Command bookkeeping.
    int m_commandDepth = 0;
    QString m_commandBefore;
    Focus m_commandFocusBefore;
    bool m_typingOpen = false;
    quint64 m_typingUid = 0;
    QPoint m_typingCell{-1, -2}; ///< table cell (column, table row) of a table typing session
    bool m_restoring = false;
};

} // namespace brulion::editor
