// PageController: the block selection and the clipboard (docs/UI.md "Selecting and copying") —
// see the "Block selection" part of pagecontroller.h.
#include "pagecontroller.h"

#include "blocklistmodel.h"
#include "blocktext.h"
#include "editorhost.h"
#include "queryhost.h"

#include "markdown/blockclipboard.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::editor {

using markdown::BlockDocument;
using markdown::EditResult;
using markdown::RowRange;

namespace {

/// The text of the last cut in this app and the block ids it took away: pasting that text is a
/// move, which keeps the ids (on any page — the vault's index still lists them until the cut page
/// is saved and read again). The page they were cut from is asked whether it has them again (the
/// cut undone).
struct LastCut {
    QString text;
    QSet<QString> ids;
    QPointer<PageController> source;
    QString path;
};
LastCut &lastCut()
{
    static LastCut cut;
    return cut;
}

/// The text this app put on the clipboard last: its HTML is never converted back (the text is
/// the Markdown it was made from).
QString &lastCopied()
{
    static QString text;
    return text;
}

/// Pasted text as compared with a cut: LF line breaks, no line breaks at the end.
QString normalizedPaste(const QString &text)
{
    QString t = text;
    t.replace(u"\r\n"_s, u"\n"_s).replace(u'\r', u'\n');
    while (t.endsWith(u'\n'))
        t.chop(1);
    return t;
}

/// Does clipboard text look like Markdown (then its HTML is not converted: the text is what the
/// other app — Obsidian, a code editor, this app — meant)? Also what only this dialect writes and
/// an HTML rendering loses: block ids, `key:: value` properties, task and reminder signifiers,
/// display formulas, footnotes.
bool looksLikeMarkdown(const QString &text)
{
    static const QRegularExpression block(
            u"(^|\\n)[ \\t]*([-*+][ \\t]|\\d{1,9}[.)][ \\t]|#{1,6}[ \\t]|>|```|~~~|\\||[A-Za-z0-9_-]+::)"_s);
    static const QRegularExpression inl(
            u"\\[\\[|\\]\\(|\\*\\*|__|~~|==|\\(\\(|`|\\$\\$|\\[\\^|(^|\\s)\\^[A-Za-z0-9-]+($|\\n)|[📅📆🗓⏳⌛🛫➕✅❌🔁⏰🔺⏫🔼🔽⏬]"_s);
    return block.match(text).hasMatch() || inl.match(text).hasMatch();
}

/// HTML this app put on the clipboard (io::clipboardHtml marks it with io::ClipboardHtmlMarker;
/// the editor does not link the io module): its text is the Markdown it was made from — also
/// after a restart, when lastCopied() no longer knows it.
bool isOwnHtml(const QString &html)
{
    return html.contains(u"<meta name=\"generator\" content=\"Brulion\">"_s);
}

QVariantMap failure(const QString &reason)
{
    return {{u"ok"_s, false}, {u"reason"_s, reason}, {u"count"_s, 0}};
}

} // namespace

// --- State ------------------------------------------------------------------------------------------

RowRange PageController::rangeOf(int anchor, int focus) const
{
    if (!validRow(anchor) || !validRow(focus))
        return {};
    const BlockDocument &doc = m_model->document();
    const int first = std::min(anchor, focus);
    const int last = std::max(anchor, focus);
    // Subtrees nest: a row inside an earlier row's subtree ends within it.
    int end = first;
    for (int r = first; r <= last; ++r) {
        if (r >= end) {
            const RowRange sub = doc.subtreeRange(r);
            end = std::max(end, sub.first + sub.count);
        }
    }
    return {first, end - first};
}

RowRange PageController::selectionRange() const
{
    return rangeOf(selectionAnchorRow(), selectedRow());
}

void PageController::updateSelection()
{
    if (m_selectionHold > 0)
        return; // a selection command runs: its end updates once
    // An end that is gone (deleted, an undo, an external change) takes the other one's place.
    if (m_selectedUid && m_model->rowOfUid(m_selectedUid) < 0)
        m_selectedUid = 0;
    if (m_anchorUid && m_model->rowOfUid(m_anchorUid) < 0)
        m_anchorUid = 0;
    if (!m_anchorUid)
        m_anchorUid = m_selectedUid;
    if (!m_selectedUid)
        m_selectedUid = m_anchorUid;
    const RowRange range = selectionRange();
    QSet<quint64> uids;
    int count = 0;
    int first = -1;
    int last = -1;
    for (int r = range.first; r < range.first + range.count; ++r) {
        uids.insert(m_model->uidAt(r));
        if (m_model->isRowVisible(r)) {
            ++count;
            if (first < 0)
                first = r;
            last = r;
        }
    }
    m_model->setSelectedUids(uids);
    // (Visibility is current only now: a lone selected note-mode frontmatter shows when selected.)
    if (range.count == 1 && count == 0 && m_model->isRowVisible(range.first)) {
        count = 1;
        first = last = range.first;
    }
    const int anchorRow = selectionAnchorRow();
    const int focusRow = selectedRow();
    if (count == m_selectionCount && first == m_selectionFirst && last == m_selectionLast
        && anchorRow == m_selectionAnchorRow && focusRow == m_selectionFocusRow)
        return;
    m_selectionCount = count;
    m_selectionFirst = first;
    m_selectionLast = last;
    m_selectionAnchorRow = anchorRow;
    m_selectionFocusRow = focusRow;
    Q_EMIT selectedRowChanged();
    Q_EMIT selectionChanged();
}

void PageController::selectRange(int anchor, int focus)
{
    if (!validRow(anchor) || !validRow(focus)) {
        clearSelection();
        return;
    }
    if (anchor != focus) {
        // The frontmatter and page properties are never part of a range (they stay at the top).
        const BlockDocument &doc = m_model->document();
        int pinned = 0;
        while (pinned < doc.count() && doc.at(pinned).kind == BlockKind::Frontmatter)
            ++pinned;
        anchor = std::max(anchor, pinned);
        focus = std::max(focus, pinned);
        if (!validRow(anchor) || !validRow(focus)) {
            clearSelection();
            return;
        }
    }
    if (m_editingUid)
        stopEditing();
    m_anchorUid = m_model->uidAt(anchor);
    m_selectedUid = m_model->uidAt(focus);
    updateSelection();
}

void PageController::extendSelectionTo(int row)
{
    if (!validRow(row))
        return;
    int anchor = selectionAnchorRow();
    if (anchor < 0)
        anchor = editingRow() >= 0 ? editingRow() : row;
    selectRange(anchor, row);
}

bool PageController::extendSelection(int step)
{
    int anchor = selectionAnchorRow();
    int focus = selectedRow();
    if (focus < 0 || anchor < 0) {
        if (editingRow() < 0)
            return false;
        anchor = focus = editingRow();
    }
    const RowRange current = rangeOf(anchor, focus);
    const auto selectable = [&](int r) { return r >= 0 && m_model->document().at(r).kind != BlockKind::Frontmatter; };
    int target = -1;
    if (step > 0) {
        if (focus >= anchor) {
            // Down past what is selected (the selected subtrees, folded children too).
            target = m_model->nextVisibleRow(current.first + current.count - 1);
        } else {
            target = m_model->nextVisibleRow(focus); // the top end comes down
        }
    } else if (focus <= anchor) {
        target = m_model->previousVisibleRow(std::min(focus, current.first));
        if (!selectable(target))
            target = -1;
    } else {
        // The bottom end goes up until the selection changes (a row inside a selected parent's
        // subtree changes nothing); past the anchor it extends above it.
        target = focus;
        do {
            target = m_model->previousVisibleRow(target);
        } while (target >= 0 && selectable(target) && rangeOf(anchor, target).count == current.count
                 && rangeOf(anchor, target).first == current.first);
        if (!selectable(target))
            target = -1;
    }
    if (target < 0)
        return false;
    selectRange(anchor, target);
    return true;
}

void PageController::selectAll()
{
    const BlockDocument &doc = m_model->document();
    int first = zoomBlock();
    if (first < 0) {
        first = m_model->firstVisibleRow();
        while (first >= 0 && doc.at(first).kind == BlockKind::Frontmatter)
            first = m_model->nextVisibleRow(first);
    }
    const int last = m_model->lastVisibleRow();
    if (first < 0 || last < first)
        return;
    selectRange(first, last);
}

void PageController::clearSelection()
{
    if (!m_selectedUid && !m_anchorUid && m_selectionCount == 0)
        return;
    resetSelection();
    updateSelection();
}

QVariantList PageController::selectedRows() const
{
    QVariantList out;
    const RowRange range = selectionRange();
    for (int r = range.first; r < range.first + range.count; ++r)
        out.append(r);
    return out;
}

QList<quint64> PageController::selectedTaskUids() const
{
    QList<quint64> out;
    const int anchor = selectionAnchorRow();
    const int focus = selectedRow();
    if (anchor < 0 || focus < 0)
        return out;
    // From one end to the other: not the children the last block brings along.
    for (int r = std::min(anchor, focus); r <= std::max(anchor, focus); ++r) {
        const BlockKind kind = m_model->document().at(r).kind;
        if (m_model->isRowVisible(r) && (kind == BlockKind::ListItem || kind == BlockKind::Paragraph))
            out.append(m_model->uidAt(r));
    }
    return out;
}

// --- Copying -----------------------------------------------------------------------------------------

QString PageController::selectionMarkdown() const
{
    const RowRange range = selectionRange();
    if (range.count <= 0)
        return {};
    const BlockDocument &doc = m_model->document();
    return markdown::blocksMarkdown(doc, doc.rangeTops(range.first, range.last()));
}

QString PageController::selectionPlainText() const
{
    const RowRange range = selectionRange();
    if (range.count <= 0)
        return {};
    const BlockDocument &doc = m_model->document();
    markdown::PlainTextOptions options;
    const QPointer<EditorHost> host = m_host;
    options.blockText = [host](const QString &id) { return host ? host->blockText(id) : QString(); };
    return markdown::blocksPlainText(doc, doc.rangeTops(range.first, range.last()), options);
}

QVariantMap PageController::writeClipboard(const QString &text, const QString &html)
{
    const qsizetype bytes = text.toUtf8().size();
    if (bytes > ClipboardLimit)
        return failure(u"tooLarge"_s);
    QClipboard *clipboard = qGuiApp ? QGuiApplication::clipboard() : nullptr;
    if (!clipboard)
        return failure(u"noClipboard"_s);
    auto *mime = new QMimeData;
    mime->setText(text);
    // The formatted flavour only while both fit (the Markdown alone is never cut short).
    if (!html.isEmpty() && bytes + html.toUtf8().size() <= ClipboardLimit)
        mime->setHtml(html);
    clipboard->setMimeData(mime);
    // X11 / Wayland: the primary selection too (a middle click pastes it).
    if (clipboard->supportsSelection())
        clipboard->setText(text, QClipboard::Selection);
    // Read back: a platform without a clipboard (or one that refused) must not be told "copied".
    if (clipboard->text() != text)
        return failure(u"noClipboard"_s);
    lastCopied() = text;
    return {{u"ok"_s, true}, {u"reason"_s, QString()}, {u"count"_s, 1}};
}

QVariantMap PageController::copySelection(bool plainText)
{
    const QString text = plainText ? selectionPlainText() : selectionMarkdown();
    if (text.isEmpty())
        return failure(u"empty"_s);
    const QString html = plainText || !m_host || text.toUtf8().size() > ClipboardLimit
                                 ? QString()
                                 : m_host->renderHtml(text, m_path);
    QVariantMap result = writeClipboard(text, html);
    if (result.value(u"ok"_s).toBool())
        result.insert(u"count"_s, std::max(1, m_selectionCount));
    return result;
}

QVariantMap PageController::cutSelection()
{
    const int count = m_selectionCount;
    QVariantMap result = copySelection(false);
    if (!result.value(u"ok"_s).toBool())
        return result;
    // A paste of this text is a move: the ids of the cut blocks stay theirs.
    QSet<QString> ids;
    const RowRange range = selectionRange();
    for (int r = range.first; r < range.first + range.count; ++r) {
        if (const QString id = m_model->document().at(r).blockId; !id.isEmpty())
            ids.insert(id);
    }
    recordCut(selectionMarkdown(), ids);
    result.insert(u"row"_s, deleteSelection());
    result.insert(u"count"_s, std::max(1, count));
    return result;
}

QVariantMap PageController::copyEditText(int row, int from, int to, bool cut)
{
    if (!validRow(row))
        return failure(u"empty"_s);
    const QString edit = m_model->editTextAt(row);
    from = snapToCharacter(edit, std::clamp(from, 0, int(edit.size())));
    to = snapToCharacter(edit, std::clamp(to, 0, int(edit.size())));
    if (to < from)
        std::swap(from, to);
    if (from == to)
        return failure(u"empty"_s);
    const QString text = edit.mid(from, to - from);
    const QVariantMap result = writeClipboard(text, QString());
    if (cut && result.value(u"ok"_s).toBool()) {
        // The block's id cut with its text (`… ^abc`) is moved, not copied, by its next paste.
        QSet<QString> ids;
        const QString own = m_model->document().at(row).blockId;
        if (!own.isEmpty() && text.contains(own) && markdown::blockIdsOf(text).contains(own))
            ids.insert(own);
        recordCut(text, ids);
    }
    return result;
}

QVariantMap PageController::copyText(const QString &text)
{
    if (text.isEmpty())
        return failure(u"empty"_s);
    return writeClipboard(text, QString());
}

// --- Editing the selection ---------------------------------------------------------------------------

int PageController::deleteSelection()
{
    const RowRange range = selectionRange();
    if (range.count <= 0)
        return -1;
    const int zoom = zoomBlock();
    const bool removesZoom = zoom >= range.first && zoom <= range.last();
    const int prev = m_model->previousVisibleRow(range.first);
    beginCommand();
    ++m_selectionHold; // from the deleted blocks to the one before them, nothing empty between
    const EditResult r = applyOp([&](BlockDocument &d) { return d.removeRange(range.first, range.last()); });
    int focusRow = -1;
    if (r.changed()) {
        focusRow = prev >= 0 ? prev : (r.focus >= 0 && validRow(r.focus) ? r.focus : -1);
        if (focusRow >= 0 && !m_model->isRowVisible(focusRow))
            focusRow = m_model->firstVisibleRow();
        if (focusRow >= 0 && m_model->document().at(focusRow).kind == BlockKind::Frontmatter)
            focusRow = -1; // not the page's properties: nothing selected then
    }
    // The block before the range is selected (the undo step brings the deleted ones back selected).
    m_anchorUid = m_selectedUid = focusRow >= 0 ? m_model->uidAt(focusRow) : 0;
    --m_selectionHold;
    updateSelection();
    endCommand(currentFocus());
    if (removesZoom)
        zoomTo(-1);
    return selectedRow();
}

bool PageController::canIndentSelection() const
{
    const RowRange range = selectionRange();
    if (range.count <= 0)
        return false;
    const BlockDocument &doc = m_model->document();
    const int zoom = zoomBlock();
    for (const int t : doc.rangeTops(range.first, range.last())) {
        if (t == zoom || (documentMode() && doc.at(t).kind != BlockKind::ListItem))
            return false;
    }
    return true;
}

bool PageController::indentSelection()
{
    if (!canIndentSelection())
        return false;
    const RowRange range = selectionRange();
    const BlockDocument &doc = m_model->document();
    const QList<int> tops = doc.rangeTops(range.first, range.last());
    QList<quint64> topUids;
    for (const int t : tops)
        topUids.append(m_model->uidAt(t));
    beginCommand();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.indentRange(range.first, range.last()); });
    // The new parents must show their new children.
    bool unfolded = false;
    for (const quint64 uid : std::as_const(topUids)) {
        const int parent = m_model->document().parent(m_model->rowOfUid(uid));
        if (r.changed() && parent >= 0 && m_model->isCollapsed(parent)) {
            m_model->setCollapsed(parent, false);
            unfolded = true;
        }
    }
    if (unfolded) {
        updateSelection();
        Q_EMIT foldStateChanged();
    }
    endCommand(currentFocus());
    return r.changed();
}

bool PageController::outdentSelection()
{
    const RowRange range = selectionRange();
    if (range.count <= 0)
        return false;
    const BlockDocument &doc = m_model->document();
    const int zoom = zoomBlock();
    for (const int t : doc.rangeTops(range.first, range.last())) {
        // Out of the zoomed subtree, or the zoomed block itself: not here.
        if (t == zoom || (zoom >= 0 && doc.parent(t) == zoom))
            return false;
    }
    beginCommand();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.outdentRange(range.first, range.last()); });
    endCommand(currentFocus());
    return r.changed();
}

bool PageController::moveSelectionUp()
{
    const RowRange range = selectionRange();
    const int zoom = zoomBlock();
    if (range.count <= 0 || (zoom >= range.first && zoom <= range.last()))
        return false;
    beginCommand();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.moveRangeUp(range.first, range.last()); });
    endCommand(currentFocus());
    return r.changed();
}

bool PageController::moveSelectionDown()
{
    const RowRange range = selectionRange();
    const int zoom = zoomBlock();
    if (range.count <= 0 || (zoom >= range.first && zoom <= range.last()))
        return false;
    beginCommand();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.moveRangeDown(range.first, range.last()); });
    endCommand(currentFocus());
    return r.changed();
}

void PageController::setSelectionTaskStatus(const QString &status)
{
    const QList<quint64> uids = selectedTaskUids();
    if (uids.isEmpty())
        return;
    beginCommand();
    for (const quint64 uid : uids) {
        const int row = m_model->rowOfUid(uid);
        if (row < 0)
            continue;
        const std::optional<TaskStatus> now = m_model->shapeAt(row).task;
        // Only what changes (a recurring task completed again would get another occurrence).
        if ((status.isEmpty() && !now) || (now && taskStatusFromName(status) == now))
            continue;
        applyTaskStatus(row, status);
    }
    endCommand(currentFocus());
}

void PageController::cycleSelectionTasks()
{
    const QList<quint64> uids = selectedTaskUids();
    if (uids.isEmpty())
        return;
    const std::optional<TaskStatus> first = m_model->shapeAt(m_model->rowOfUid(uids.first())).task;
    QString next;
    if (!first)
        next = u"todo"_s;
    else if (*first == TaskStatus::Todo)
        next = u"doing"_s;
    else if (*first == TaskStatus::Doing)
        next = u"done"_s;
    setSelectionTaskStatus(next);
}

// --- Pasting -----------------------------------------------------------------------------------------

bool PageController::isOwnCopy(const QString &text) const
{
    return !lastCopied().isEmpty() && normalizedPaste(text) == normalizedPaste(lastCopied());
}

void PageController::recordCut(const QString &text, const QSet<QString> &ids)
{
    lastCut() = {normalizedPaste(text), ids, this, m_path};
}

bool PageController::hasBlockId(const QString &id) const
{
    const BlockDocument &doc = m_model->document();
    for (int i = 0; i < doc.count(); ++i) {
        if (doc.at(i).blockId == id)
            return true;
    }
    return false;
}

QSet<QString> PageController::cutIdsFor(const QString &text) const
{
    const LastCut &cut = lastCut();
    if (cut.text.isEmpty() || cut.ids.isEmpty() || normalizedPaste(text) != cut.text)
        return {};
    // The cut page has them again (the cut was undone): a paste is a copy. Asked while it shows
    // that page, else its saved file (it was saved when it closed).
    const PageController *source = cut.source.data();
    const bool live = source && source->m_path == cut.path;
    QString saved;
    bool savedRead = false;
    QSet<QString> freed;
    for (const QString &id : cut.ids) {
        bool back = false;
        if (live) {
            back = source->hasBlockId(id);
        } else if (m_host && !cut.path.isEmpty()) {
            if (!savedRead) {
                bool ok = false;
                saved = m_host->read(cut.path, &ok);
                savedRead = true;
                if (!ok)
                    saved.clear();
            }
            back = !saved.isEmpty() && markdown::blockIdsOf(saved).contains(id);
        }
        if (!back)
            freed.insert(id);
    }
    return freed;
}

QString PageController::withoutTakenIds(const QString &text, const QSet<QString> &freed, bool indexed)
{
    if (!text.contains(u'^') && !text.contains(u"::"))
        return text;
    const QStringList ids = markdown::blockIdsOf(text);
    if (ids.isEmpty())
        return text;
    QSet<QString> here;
    const BlockDocument &doc = m_model->document();
    for (int i = 0; i < doc.count(); ++i) {
        if (!doc.at(i).blockId.isEmpty())
            here.insert(doc.at(i).blockId);
    }
    const QPointer<EditorHost> host = m_host;
    if (host && !indexed)
        host->finishIndexing(); // every id of the vault known
    const QString out = markdown::withoutBlockIds(text, [&](const QString &id) {
        if (here.contains(id))
            return true;
        if (freed.contains(id))
            return false;
        return host && host->blockIdTaken(id);
    });
    // A move happens once: a second paste of the cut text is a copy.
    if (!freed.isEmpty() && normalizedPaste(text) == lastCut().text)
        lastCut() = {};
    return out;
}

QVariantMap PageController::replaceSelection(const QString &pasted)
{
    const auto result = [](int row, int count) { return QVariantMap{{u"row"_s, row}, {u"count"_s, count}}; };
    const RowRange range = selectionRange();
    QString text = normalizedPaste(pasted);
    if (range.count <= 0 || text.trimmed().isEmpty())
        return result(-1, 0);
    const int zoom = zoomBlock();
    if (zoom >= range.first && zoom <= range.last())
        return result(-1, 0); // the zoomed block stays (paste into it instead)
    // A pasted note's YAML frontmatter cannot go into the middle of a page (Logseq page
    // properties can: a block of `key:: value` lines, as pasteText() inserts them). Nothing else:
    // nothing changes.
    const auto yaml = [](const markdown::BlockEntry &e) {
        return e.kind == BlockKind::Frontmatter && e.body.startsWith(u"---"_s);
    };
    {
        const BlockDocument probe(text);
        bool any = false;
        for (int k = 0; k < probe.count() && !any; ++k)
            any = !yaml(probe.at(k));
        if (!any)
            return result(-1, 0);
    }
    const BlockDocument &doc = m_model->document();
    // The page's own properties are never replaced: selected, the paste goes in below them.
    int pinned = 0;
    while (pinned < doc.count() && doc.at(pinned).kind == BlockKind::Frontmatter)
        ++pinned;
    const int first = std::max(range.first, pinned);
    const int last = range.last();
    const bool removes = first <= last;
    // The replaced blocks' ids may go on in the pasted text (pasting a block over itself).
    QSet<QString> freed = cutIdsFor(text);
    for (int r = first; r <= last; ++r) {
        if (!doc.at(r).blockId.isEmpty())
            freed.insert(doc.at(r).blockId);
    }
    const bool hasNext = first < doc.count();
    const int baseDepth = removes ? doc.at(first).depth : 0;
    const bool intoList = hasNext && doc.at(first).kind == BlockKind::ListItem;
    const int prev = first - 1;
    // Every id of the vault known before the command (completing the index signals).
    if (m_host && (text.contains(u'^') || text.contains(u"::")))
        m_host->finishIndexing();
    beginCommand();
    ++m_selectionHold; // from the replaced blocks to the pasted ones, nothing empty between
    if (removes)
        applyOp([&](BlockDocument &d) { return d.removeRange(first, last); });
    text = withoutTakenIds(text, freed, true);
    const BlockDocument pasteDoc(text);
    int after = prev;
    quint64 firstUid = 0;
    quint64 lastUid = 0;
    const int firstDepth = pasteDoc.isEmpty() ? 0 : pasteDoc.at(0).depth;
    for (int k = 0; k < pasteDoc.count(); ++k) {
        const markdown::BlockEntry &e = pasteDoc.at(k);
        if (yaml(e))
            continue;
        const int depth = baseDepth + std::max(0, e.depth - firstDepth);
        QString body = e.body;
        if (const QString closing = missingClosingFence(body); !closing.isEmpty())
            body += u'\n' + closing;
        const EditResult r = applyOp([&](BlockDocument &d) {
            return d.insertAfter(after, body, depth, intoList || e.kind == BlockKind::ListItem);
        });
        if (r.focus >= 0) {
            after = r.focus;
            lastUid = m_model->uidAt(after);
            if (!firstUid)
                firstUid = lastUid;
        }
    }
    m_anchorUid = firstUid;
    m_selectedUid = lastUid;
    --m_selectionHold;
    updateSelection();
    endCommand(currentFocus());
    return result(selectedRow(), m_selectionCount);
}

QString PageController::clipboardText() const
{
    QClipboard *clipboard = qGuiApp ? QGuiApplication::clipboard() : nullptr;
    const QMimeData *mime = clipboard ? clipboard->mimeData() : nullptr;
    if (!mime)
        return {};
    const QString text = mime->hasText() ? mime->text() : QString();
    // HTML from another app (a browser, an office suite) whose text is not Markdown: its structure
    // (lists, headings, links, emphasis) as Markdown. Not this app's own copy: its text is the
    // Markdown (the last one compared as pasted: line breaks may have changed on the way).
    if (mime->hasHtml() && m_host && !isOwnCopy(text) && !looksLikeMarkdown(text) && !isOwnHtml(mime->html())) {
        const QString converted = m_host->htmlToMarkdown(mime->html());
        if (!converted.trimmed().isEmpty())
            return converted;
    }
    return text;
}

QVariantMap PageController::pasteClipboard(int row, int from, int to)
{
    const QString text = clipboardText();
    if (text.isEmpty() || !validRow(row))
        return {{u"row"_s, -1}, {u"cursor"_s, -1}};
    return pasteText(row, from, to, text);
}

QVariantMap PageController::pasteOverSelection()
{
    return replaceSelection(clipboardText());
}

// --- The "Zaznacz tekst" view ------------------------------------------------------------------------

namespace {

/// The renderer's chips (dates, reminders…) are padded with no-break spaces and keep their words
/// together with more of them: in a text that is selected and copied, their words are separated
/// by plain spaces and the padding goes (the user's own no-break spaces stay).
QString unpaddedChips(const QString &html)
{
    static const QRegularExpression chip(
            u"(<span style=\"[^\"]*background-color[^\"]*\">)&nbsp;([^<]*)&nbsp;</span>"_s);
    QString out;
    qsizetype at = 0;
    auto it = chip.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += QStringView(html).sliced(at, m.capturedStart() - at);
        out += m.captured(1) + m.captured(2).replace(u"&nbsp;"_s, u" "_s) + u"</span>"_s;
        at = m.capturedEnd();
    }
    return at == 0 ? html : out + QStringView(html).sliced(at);
}

} // namespace

QString PageController::queryReadingHtml(const QString &source, int indent) const
{
    auto *queries = m_host ? qobject_cast<QueryHost *>(m_host.data()) : nullptr;
    if (!queries || source.trimmed().isEmpty())
        return {};
    const QVariantMap r = queries->runQuery(source, m_path);
    const QVariantList items = r.value(u"items"_s).toList();
    const QVariantList groups = r.value(u"groups"_s).toList();
    if (!r.value(u"ok"_s).toBool() || (items.isEmpty() && groups.isEmpty()))
        return {};
    const bool pages = r.value(u"type"_s).toString() == u"pages";
    const QString muted = m_model->palette().muted.toHtmlEscaped();
    const QString style = u"margin-top:2px; margin-bottom:2px; margin-left:%1px; line-height:140%;"_s.arg(indent);
    // An item as the block lists it: its checkbox or bullet, its text, the page it is on.
    const auto item = [&](const QVariantMap &it) -> QString {
        QString marker = u"• "_s;
        if (const QVariantMap task = it.value(u"task"_s).toMap(); !task.isEmpty()) {
            const QString status = task.value(u"status"_s).toString();
            marker = status == u"done" ? u"☑ "_s : status == u"cancelled" ? u"☒ "_s : u"☐ "_s;
        }
        const QString title = it.value(u"title"_s).toString();
        const QString text = pages ? title : it.value(u"text"_s).toString();
        QString html = u"<p style=\"%1\">"_s.arg(style) + marker + text.toHtmlEscaped();
        if (!pages && !title.isEmpty())
            html += u" <span style=\"color:%1;\">· %2</span>"_s.arg(muted, title.toHtmlEscaped());
        return html + u"</p>"_s;
    };
    QString out;
    for (const QVariant &g : groups) {
        const QVariantMap group = g.toMap();
        out += u"<p style=\"%1 font-weight:600;\">%2</p>"_s.arg(style,
                                                                group.value(u"label"_s).toString().toHtmlEscaped());
        for (const QVariant &it : group.value(u"items"_s).toList())
            out += item(it.toMap());
    }
    if (groups.isEmpty()) {
        for (const QVariant &it : items)
            out += item(it.toMap());
    }
    return out;
}

QString PageController::readingHtml() const
{
    const BlockDocument &doc = m_model->document();
    const markdown::Palette &palette = m_model->palette();
    const auto esc = [](const QString &s) { return s.toHtmlEscaped(); };
    const QString muted = esc(palette.muted);
    const QString codeBg = esc(palette.codeBackground);
    // A paragraph style: spacing like a page of text, the list levels indented.
    const auto block = [](int indent, int top, int bottom) {
        return u"margin-top:%1px; margin-bottom:%2px; margin-left:%3px; line-height:140%;"_s.arg(top).arg(bottom).arg(
                indent);
    };
    static const QStringList bullets{u"•"_s, u"◦"_s, u"▪"_s};
    QString out = u"<html><body>"_s;
    for (int row = 0; row < doc.count(); ++row) {
        const markdown::BlockEntry &e = doc.at(row);
        if (e.kind == BlockKind::Frontmatter)
            continue;
        const QModelIndex index = m_model->index(row);
        const BlockShape shape = m_model->shapeAt(row);
        const bool listItem = e.kind == BlockKind::ListItem;
        const int indent = listItem ? 24 * e.depth : 0;
        QString marker;
        if (listItem) {
            const QString written = e.prefix.trimmed();
            const bool ordered = !written.isEmpty() && written.front().isDigit();
            if (shape.task)
                marker = *shape.task == TaskStatus::Done        ? u"☑"_s
                         : *shape.task == TaskStatus::Cancelled ? u"☒"_s
                                                                : u"☐"_s;
            if (ordered)
                marker = esc(written) + (marker.isEmpty() ? QString() : u' ' + marker);
            else if (marker.isEmpty())
                marker = bullets.at(e.depth % 3);
            marker += u' '; // a space (not a no-break one): a copy reads "• text"
        }
        const int gap = listItem ? 2 : 8;
        switch (shape.view) {
        case BlockView::Heading: {
            const int level = std::clamp(shape.headingLevel, 1, 6);
            out += u"<h%1 style=\"%2\">%3%4</h%1>"_s.arg(level).arg(block(indent, 14, 6), marker,
                                                                    unpaddedChips(m_model->readingHtmlAt(row)));
            break;
        }
        case BlockView::Calc: {
            // Each line with its result, as the block shows it ("bilety = 3 * 120 zł = 360 zł"); not
            // a value the line already ends with ("cena = 89 zł").
            QStringList lines = shape.editText.split(u'\n');
            for (const QVariant &v : m_model->data(index, BlockListModel::CalcResultsRole).toList()) {
                const QVariantMap r = v.toMap();
                const int line = r.value(u"line"_s).toInt();
                const QString error = r.value(u"error"_s).toString();
                const QString value = r.value(u"value"_s).toString();
                if (line < 0 || line >= lines.size() || lines[line].trimmed().isEmpty())
                    continue;
                if (!error.isEmpty())
                    lines[line] += u"  ⚠ "_s + error;
                else if (!value.isEmpty() && lines[line].section(u'=', -1).simplified() != value.simplified())
                    lines[line] += u" = "_s + value;
            }
            out += u"<pre style=\"%1 background-color:%2; white-space:pre-wrap;\">%3</pre>"_s.arg(
                    block(indent + (listItem ? 18 : 0), 6, 6), codeBg, esc(lines.join(u'\n')));
            break;
        }
        case BlockView::Query: {
            const QString results = queryReadingHtml(shape.editText, indent + (listItem ? 18 : 0));
            if (!results.isEmpty()) {
                out += results;
                break;
            }
            out += u"<pre style=\"%1 background-color:%2; white-space:pre-wrap;\">%3</pre>"_s.arg(
                    block(indent + (listItem ? 18 : 0), 6, 6), codeBg, esc(shape.editText));
            break;
        }
        case BlockView::Code:
        case BlockView::Math:
        case BlockView::Html:
            out += u"<pre style=\"%1 background-color:%2; white-space:pre-wrap;\">%3</pre>"_s.arg(
                    block(indent + (listItem ? 18 : 0), 6, 6), codeBg, esc(shape.editText));
            break;
        case BlockView::Table: {
            const QVariantMap table = m_model->readingTableAt(row);
            out += u"<table border=\"1\" cellspacing=\"0\" cellpadding=\"5\" style=\"%1 border-collapse:collapse; "
                   "border-color:%2;\">"_s.arg(block(indent, 6, 6), muted);
            const QVariantList header = table.value(u"headerHtml"_s).toList();
            if (!header.isEmpty()) {
                out += u"<tr>"_s;
                for (const QVariant &cell : header)
                    out += u"<th>"_s + cell.toString() + u"</th>"_s;
                out += u"</tr>"_s;
            }
            for (const QVariant &rowData : table.value(u"rows"_s).toList()) {
                out += u"<tr>"_s;
                for (const QVariant &cell : rowData.toList())
                    out += u"<td>"_s + unpaddedChips(cell.toMap().value(u"html"_s).toString()) + u"</td>"_s;
                out += u"</tr>"_s;
            }
            out += u"</table>"_s;
            break;
        }
        case BlockView::Quote:
        case BlockView::Callout:
            out += u"<p style=\"%1 color:%2; font-style:italic;\">%3%4</p>"_s.arg(
                    block(indent + 24, gap, gap), muted, marker, unpaddedChips(m_model->readingHtmlAt(row)));
            break;
        case BlockView::Rule: out += u"<hr/>"_s; break;
        case BlockView::Frontmatter: break;
        case BlockView::Text: {
            const QString html = unpaddedChips(m_model->readingHtmlAt(row));
            if (!html.isEmpty() || !marker.isEmpty())
                out += u"<p style=\"%1\">%2%3</p>"_s.arg(block(indent, gap, gap), marker, html);
            break;
        }
        }
        // Embedded pictures and pages by name (the view shows text only).
        for (const QVariant &embed : m_model->data(index, BlockListModel::EmbedsRole).toList()) {
            const QVariantMap m = embed.toMap();
            const QString name = m.value(u"alt"_s).toString().isEmpty() ? m.value(u"target"_s).toString()
                                                                        : m.value(u"alt"_s).toString();
            out += u"<p style=\"%1 color:%2;\">[%3]</p>"_s.arg(block(indent, 2, 2), muted, esc(name));
        }
        for (const QVariant &t : m_model->data(index, BlockListModel::TransclusionsRole).toList()) {
            const QVariantMap m = t.toMap();
            const QString name = m.value(u"title"_s).toString().isEmpty() ? m.value(u"target"_s).toString()
                                                                          : m.value(u"title"_s).toString();
            out += u"<p style=\"%1 color:%2;\">⧉ %3</p>"_s.arg(block(indent, 2, 2), muted, esc(name));
        }
    }
    out += u"</body></html>"_s;
    return out;
}

} // namespace brulion::editor
