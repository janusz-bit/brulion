#include "pagecontroller.h"

#include "blocktext.h"
#include "dayclock.h"
#include "editorhost.h"
#include "queryhost.h"

#include "calc/table.h"
#include "dates/local.h"
#include "dates/recurrence.h"
#include "drawing/drawing.h"
#include "drawing/svgwrite.h"
#include "markdown/inline.h"
#include "markdown/journal.h"
#include "markdown/linkformat.h"
#include "markdown/notemode.h"
#include "markdown/templates.h"
#include "markdown/tokens.h"
#include "notebook/links.h"
#include "text/text.h"
#include "util/paths.h"
#include "webpicture/webpicture.h"

#ifdef BRULION_EDITOR_HAVE_SYNC_MERGE
#include "sync/conflicts.h"
#include "sync/merge.h"
#endif

#include <QBuffer>
#include <QClipboard>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSysInfo>
#include <QUndoCommand>
#include <QUndoStack>
#include <QUrl>

#include "pickedfiles.h"
#ifdef Q_OS_WASM
#include "wasm/wasmfiles.h"
#endif

#include <algorithm>
#include <cstdlib>

using namespace Qt::StringLiterals;

namespace brulion::editor {

using markdown::BlockDocument;
using markdown::EditResult;

/// One undo step: the change of the document (BlockDocument::Delta: the run of blocks it changed,
/// a block in both versions with only its changed range of text — copies of a long page per step
/// added up to hundreds of megabytes over an editing session) plus where the caret was. Undo and
/// redo turn the document into the other version as a BlockDocument operation (applyStep()): only
/// the changed blocks are parsed again, every block keeps its uid and row (a changed one gets
/// dataChanged), empty paragraphs come back as the blocks they were. Every change of the document
/// goes through a command or clears the stack, so the document is always the version a step
/// expects; a mismatch is refused, never patched. Typing commands (typingUid != 0) merge while
/// the user keeps typing in the same block.
class EditCommand : public QUndoCommand {
public:
    /// Made right after the edit: `delta` leads from the version before it to the document.
    EditCommand(PageController *controller, BlockDocument::Delta delta, PageController::Focus focusBefore,
                PageController::Focus focusAfter, quint64 typingUid)
        : m_controller(controller)
        , m_delta(std::move(delta))
        , m_focusBefore(std::move(focusBefore))
        , m_focusAfter(std::move(focusAfter))
        , m_typingUid(typingUid)
    {
    }

    void undo() override { m_controller->applyStep(m_delta, PageController::StepDirection::Undo, m_focusBefore); }
    void redo() override
    {
        if (m_first) {
            m_first = false; // the edit was already done when the command was pushed
            return;
        }
        m_controller->applyStep(m_delta, PageController::StepDirection::Redo, m_focusAfter);
    }
    int id() const override { return m_typingUid ? 1 : -1; }
    bool mergeWith(const QUndoCommand *other) override
    {
        const auto *o = static_cast<const EditCommand *>(other);
        if (!m_typingUid || o->m_typingUid != m_typingUid || !o->m_mergeable)
            return false;
        // QUndoStack merges right after `o`'s edit: the document is its result. Bounded: a merge
        // costs the blocks of the joined run at every keystroke, so a step that also changed blocks
        // far away (typing at the top renumbered a loose ordered list below) stays one of its own.
        const std::optional<BlockDocument::Delta> merged =
                m_controller->m_model->document().composed(m_delta, o->m_delta, true);
        if (!merged)
            return false;
        m_delta = *merged;
        m_focusAfter = o->m_focusAfter;
        return true;
    }
    void setMergeable(bool mergeable) { m_mergeable = mergeable; }
    /// The document, the result of this step, changed once more by `amendment` (the step stays
    /// one): false when the two do not chain.
    bool amendResult(const BlockDocument::Delta &amendment)
    {
        const std::optional<BlockDocument::Delta> amended =
                m_controller->m_model->document().composed(m_delta, amendment);
        if (!amended)
            return false;
        m_delta = *amended;
        return true;
    }
    /// Characters held by the step.
    qsizetype size() const { return m_delta.size(); }
    /// Unique among the steps of the app's run (PageController::undoToken(): an edit undone and
    /// replaced by another at the same place in the history is another step).
    quint64 serial() const { return m_serial; }

private:
    static inline quint64 s_serials = 0;
    const quint64 m_serial = ++s_serials;
    PageController *m_controller;
    BlockDocument::Delta m_delta;
    PageController::Focus m_focusBefore;
    PageController::Focus m_focusAfter;
    quint64 m_typingUid = 0;
    bool m_first = true;
    bool m_mergeable = false;
};

namespace {

QString colorString(const QVariant &v, const QString &fallback)
{
    if (!v.isValid())
        return fallback;
    QColor c;
    if (v.metaType() == QMetaType::fromType<QColor>())
        c = v.value<QColor>();
    else
        c = QColor::fromString(v.toString());
    if (!c.isValid())
        return fallback;
    return c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb);
}

int lineCount(const QString &s)
{
    return int(text::splitLines(s).size());
}

/// Markdown of a heading level `n` for an edit text that may already start with one.
QString withHeading(const QString &text, int level)
{
    static const QRegularExpression re(u"^\\s{0,3}#{1,6}(\\s+|$)"_s);
    QString rest = text;
    rest.remove(re);
    return QString(level, u'#') + u' ' + rest;
}

/// Whether `from`..`to` of a text block's body lies between the fence lines of code written in
/// that block ("```js" typed on a new line got its closing fence at the end of the block): every
/// fence of the body is closed, but the text before or after the range leaves one open. Enter and
/// paste work there as in a code block. A fence that was open before (written elsewhere) does not
/// count.
bool insideOwnFence(QStringView body, qsizetype from, qsizetype to)
{
    from = std::clamp<qsizetype>(from, 0, body.size());
    to = std::clamp<qsizetype>(to, from, body.size());
    return missingClosingFence(body).isEmpty()
           && (!missingClosingFence(body.first(from)).isEmpty() || !missingClosingFence(body.sliced(to)).isEmpty());
}

/// Two text blocks joined by a merge (Backspace at the start of `next`, Delete at the end of
/// `prev`) with a line break between them when `prev` ends with the closing fence of its code:
/// "```" + "b" would be "```b", which closes nothing, and the code would run on into the
/// following blocks. nullopt when the plain join keeps every fence closed.
std::optional<QString> joinedAfterFence(const QString &prev, const QString &next)
{
    if (next.isEmpty() || !missingClosingFence(prev).isEmpty() || !missingClosingFence(next).isEmpty()
        || missingClosingFence(QString(prev + next)).isEmpty())
        return std::nullopt;
    return QString(prev + u'\n' + next);
}

} // namespace

PageController::PageController(QObject *parent) : QObject(parent), m_model(new BlockListModel(this)), m_undo(new QUndoStack(this))
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { save(); });
    connect(m_undo, &QUndoStack::canUndoChanged, this, &PageController::undoStateChanged);
    connect(m_undo, &QUndoStack::canRedoChanged, this, &PageController::undoStateChanged);
    // The leading "# Title" (titleRow) follows every change of the first blocks.
    connect(m_model, &QAbstractItemModel::modelReset, this, &PageController::updateTitleRow);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &PageController::updateTitleRow);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, &PageController::updateTitleRow);
    connect(m_model, &QAbstractItemModel::rowsMoved, this, &PageController::updateTitleRow);
    connect(m_model, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &from) {
        if (from.row() <= std::max(1, m_titleRow))
            updateTitleRow();
        if (from.row() <= 1)
            updatePageProperties();
    });
    connect(this, &PageController::zoomChanged, this, &PageController::updateTitleRow);
    connect(this, &PageController::pathChanged, this, &PageController::updateTitleRow);
    // The page's properties (pageProperties) follow every change of the first blocks too.
    connect(m_model, &QAbstractItemModel::modelReset, this, &PageController::updatePageProperties);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &PageController::updatePageProperties);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, &PageController::updatePageProperties);
    connect(m_model, &QAbstractItemModel::rowsMoved, this, &PageController::updatePageProperties);
    connect(this, &PageController::zoomChanged, this, &PageController::updatePageProperties);
    connect(this, &PageController::sourceModeChanged, this, &PageController::updatePageProperties);
    // Leaving the foreground (Android may then kill the process without running destructors)
    // and quitting flush the page; the QML tear-down after the event loop may come too late
    // (hosts owned elsewhere can be gone by then).
    if (qGuiApp) {
        connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive)
                flush();
        });
    }
    if (QCoreApplication::instance())
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &PageController::flush);
    // Midnight: "jutro 17:30" becomes "dziś 17:30", overdue dates turn red, `today` queries move on.
    connect(DayClock::instance(), &DayClock::dayChanged, this, &PageController::refreshToday);
#ifdef BRULION_EDITOR_HAVE_SYNC_MERGE
    m_merge = [](const QString &base, const QString &local, const QString &remote) {
        const sync::MergeResult r = sync::merge3(base, local, remote);
        return MergeOutcome{r.text, r.clean};
    };
#endif
    rebuildPalette();
}

PageController::~PageController()
{
    flush();
    showFile(nullptr, QString());
}

void PageController::flush()
{
    // An unresolved conflict keeps the local text in memory only: it goes to a conflict copy
    // (never over the other version in the file). The save itself may find one: its write only
    // replaces the text last read here, and another program's change nobody reported yet can
    // turn out not to merge — the page is about to go (another page, the editor closing, the app
    // quitting or sent to the background), so that local text needs its copy now too.
    if (!m_conflict)
        save();
    if (m_conflict)
        writeConflictCopy();
}

// --- Properties ---------------------------------------------------------------------------------

void PageController::setHost(EditorHost *host)
{
    if (m_host == host)
        return;
    flush(); // load() below replaces the text: unsaved edits are written (or copied) first
    if (m_host)
        disconnect(m_host, nullptr, this, nullptr);
    m_host = host;
    m_model->setHost(host);
    showFile(m_host, m_path);
    if (m_host) {
        connect(m_host, &EditorHost::fileChanged, this, &PageController::onFileChanged);
        connect(m_host, &EditorHost::indexChanged, m_model, &BlockListModel::invalidateRendering);
        connect(m_host, &EditorHost::indexChanged, this, [this] { m_notebookTypes.reset(); });
        connect(m_host, &EditorHost::webPictureChanged, this, &PageController::onWebPictureChanged);
        connect(m_host, &QObject::destroyed, this, [this] { m_host = nullptr; });
    }
    Q_EMIT hostChanged();
    if (m_host && !m_path.isEmpty())
        load();
}

void PageController::setPath(const QString &path)
{
    if (m_path == path)
        return;
    if (m_loaded)
        Q_EMIT pathAboutToChange();
    flush();
    m_path = path;
    m_webDownloads.clear(); // the pictures of the other page are not stored into this one
    m_webStored = 0;
    m_webFailed.clear();
    m_model->setPath(path);
    showFile(m_host, m_path);
    Q_EMIT pathChanged();
    if (m_host && !m_path.isEmpty())
        load();
}

QString PageController::title() const
{
    return paths::pageTitle(m_path);
}

int PageController::zoomBlock() const
{
    return m_model->zoomUid() ? m_model->rowOfUid(m_model->zoomUid()) : -1;
}

QVariantList PageController::breadcrumb() const
{
    QVariantList out;
    const int zoom = zoomBlock();
    if (zoom < 0)
        return out;
    QList<int> chain;
    for (int i = zoom; i >= 0; i = m_model->document().parent(i))
        chain.prepend(i);
    for (const int i : std::as_const(chain))
        out.append(QVariantMap{{u"row"_s, i}, {u"title"_s, plainTextOf(m_model->document().at(i), 40)}});
    return out;
}

int PageController::editingRow() const
{
    return m_editingUid ? m_model->rowOfUid(m_editingUid) : -1;
}

void PageController::setEditingRow(int row)
{
    if (row < 0)
        stopEditing();
    else
        startEditing(row, m_cursor);
}

void PageController::setCursorPosition(int cursor)
{
    if (m_cursor == cursor)
        return;
    m_cursor = cursor;
    Q_EMIT cursorPositionChanged();
}

int PageController::selectedRow() const
{
    return m_selectedUid ? m_model->rowOfUid(m_selectedUid) : -1;
}

void PageController::setSelectedRow(int row)
{
    const quint64 uid = validRow(row) ? m_model->uidAt(row) : 0;
    if (uid == m_selectedUid && uid == m_anchorUid && m_pickedUids.isEmpty())
        return;
    m_pickedUids.clear();
    m_selectedUid = uid;
    m_anchorUid = uid;
    updateSelection();
}

int PageController::selectionAnchorRow() const
{
    return m_anchorUid ? m_model->rowOfUid(m_anchorUid) : -1;
}

qsizetype PageController::undoHistorySize() const
{
    qsizetype size = 0;
    for (int i = 0; i < m_undo->count(); ++i)
        size += static_cast<const EditCommand *>(m_undo->command(i))->size();
    return size;
}

bool PageController::canUndo() const
{
    return m_undo->canUndo();
}

bool PageController::canRedo() const
{
    return m_undo->canRedo();
}

void PageController::setPalette(const QVariantMap &palette)
{
    if (m_palette == palette)
        return;
    m_palette = palette;
    rebuildPalette();
    Q_EMIT paletteChanged();
}

void PageController::rebuildPalette()
{
    markdown::Palette p;
    const auto get = [this](const char *key, const QString &fallback) {
        return colorString(m_palette.value(QString::fromLatin1(key)), fallback);
    };
    p.link = get("link", p.link);
    p.unresolved = get("unresolved", p.unresolved);
    p.muted = get("muted", p.muted);
    p.codeBackground = get("codeBackground", p.codeBackground);
    p.highlightBackground = get("highlightBackground", p.highlightBackground);
    const auto chip = [&](const char *name, markdown::ChipColors &c) {
        c.foreground = get(QByteArray(QByteArray(name) + "Fg").constData(), c.foreground);
        c.background = get(QByteArray(QByteArray(name) + "Bg").constData(), c.background);
    };
    chip("tag", p.tag);
    chip("due", p.due);
    chip("overdue", p.overdue);
    chip("scheduled", p.scheduled);
    chip("reminder", p.reminder);
    chip("alarm", p.alarm);
    chip("recurrence", p.recurrence);
    chip("done", p.done);
    chip("neutral", p.neutral);
    m_model->setPalette(p);
}

void PageController::setLocale(const QString &locale)
{
    const bool english = locale == u"en";
    if (english == m_english)
        return;
    m_english = english;
    m_model->setEnglish(english);
    Q_EMIT localeChanged();
}

void PageController::setNow(const QString &now)
{
    if (m_fixedNow == now)
        return;
    m_fixedNow = now;
    const QDate before = m_model->today();
    m_model->setToday(today());
    Q_EMIT nowChanged();
    if (m_model->today() != before)
        Q_EMIT todayChanged();
}

void PageController::refreshToday()
{
    const QDate day = today();
    if (day == m_model->today())
        return;
    m_model->setToday(day);
    Q_EMIT todayChanged();
}

QDateTime PageController::currentDateTime() const
{
    if (!m_fixedNow.isEmpty()) {
        if (const auto spec = dates::parseDateSpec(m_fixedNow))
            return spec->toLocalDateTime();
    }
    return QDateTime::currentDateTime();
}

QDate PageController::today() const
{
    return currentDateTime().date();
}

void PageController::setSaveDelay(int ms)
{
    ms = std::max(0, ms);
    if (ms == m_saveTimer.interval())
        return;
    m_saveTimer.setInterval(ms);
    Q_EMIT saveDelayChanged();
}

void PageController::setAllowNewBlockIds(bool allow)
{
    if (allow == m_allowNewBlockIds)
        return;
    m_allowNewBlockIds = allow;
    Q_EMIT allowNewBlockIdsChanged();
}

QVariantList PageController::foldState() const
{
    QList<int> lines;
    for (const quint64 uid : m_model->collapsedUids()) {
        const int row = m_model->rowOfUid(uid);
        if (row >= 0)
            lines.append(m_model->document().at(row).line);
    }
    std::sort(lines.begin(), lines.end());
    QVariantList out;
    for (const int l : std::as_const(lines))
        out.append(l);
    return out;
}

// --- Loading and saving ------------------------------------------------------------------------------

void PageController::load()
{
    if (!m_host || m_path.isEmpty())
        return;
    m_saveTimer.stop();
    m_loading = true;
    Q_EMIT loadingChanged();
    bool ok = false;
    const QString content = m_host->read(m_path, &ok);
    const bool hadZoom = m_model->zoomUid() != 0;
    m_model->setToday(today());
    m_model->resetContent(ok ? content : QString());
    m_savedText = ok ? content : QString();
    m_undo->clear();
    ++m_undoGeneration;
    m_typingOpen = false;
    m_editingUid = 0;
    resetSelection();
    for (auto it = m_tracked.begin(); it != m_tracked.end(); ++it)
        it.value() = 0; // another file (or a reload): tracked blocks are gone
    m_cursor = -1;
    setConflict(false);
    setDirty(false);
    if (m_sourceMode) {
        m_sourceText = m_savedText;
        Q_EMIT sourceTextChanged();
    }
    // Logseq's `collapsed:: true` property folds a block initially (device-local afterwards).
    QSet<quint64> folded;
    static const QRegularExpression collapsedRe(u"(^|\\n)\\s*collapsed::\\s*true\\s*($|\\n)"_s,
                                                QRegularExpression::CaseInsensitiveOption);
    const BlockDocument &doc = m_model->document();
    for (int i = 0; i < doc.count(); ++i) {
        if (doc.hasChildren(i) && collapsedRe.match(doc.at(i).body).hasMatch())
            folded.insert(doc.at(i).uid);
    }
    if (!folded.isEmpty())
        m_model->setCollapsedUids(folded);
    resolveMode(true);
    m_loaded = true;
    if (m_exists != ok) {
        m_exists = ok;
        Q_EMIT existsChanged();
    }
    m_loading = false;
    Q_EMIT loadingChanged();
    Q_EMIT editingRowChanged();
    Q_EMIT selectedRowChanged();
    updateSelection();
    if (hadZoom)
        Q_EMIT zoomChanged();
    Q_EMIT foldStateChanged();
}

bool PageController::save()
{
    m_saveTimer.stop();
    if (!m_host || m_path.isEmpty() || !m_loaded || m_saving)
        return false;
    if (m_conflict)
        return false; // never overwrite the other version before the conflict is resolved
    if (!m_dirty)
        return true;
    QString content;
    // Another program (an agent's brulion-cli, Syncthing) may have changed the file a moment ago,
    // before its change reached this editor: the host writes only over the text last read or
    // written here. Otherwise the other version is taken in first — a clean page reloads, a dirty
    // one is merged three-way, a real conflict waits for the user — and the result is saved, so
    // neither side's text is lost.
    for (int attempt = 0;; ++attempt) {
        content = m_sourceMode ? m_sourceText : textToSave();
        if (!m_exists) {
            // A new page: an empty page or a lone empty bullet is no reason to create a file.
            static const QRegularExpression blank(u"^[\\s\\-*+]*$"_s);
            if (blank.match(content).hasMatch()) {
                setDirty(false);
                return true;
            }
        }
        m_saving = true;
        const EditorHost::WriteResult result = m_host->writeIfUnchanged(m_path, content, m_savedText);
        m_saving = false;
        if (result == EditorHost::WriteResult::Written)
            break;
        if (result == EditorHost::WriteResult::Failed || !m_host) {
            Q_EMIT saveFailed();
            return false;
        }
        onFileChanged(m_path); // reload / merge / conflict (a no-op when the host reported it already)
        m_saveTimer.stop();
        if (m_conflict)
            return false;
        if (!m_dirty)
            return true; // the other version is ours
        if (attempt >= 2) {
            m_saveTimer.start(); // the file keeps changing: try again a little later
            return false;
        }
    }
    m_savedText = content;
    if (!m_exists) {
        m_exists = true;
        Q_EMIT existsChanged();
    }
    setDirty(false);
    Q_EMIT saved();
    return true;
}

void PageController::followRename(const QString &oldPath, const QString &newPath)
{
    if (oldPath.isEmpty() || oldPath != m_path || newPath.isEmpty() || newPath == m_path)
        return;
    m_path = newPath;
    m_model->setPath(newPath);
    m_conflictCopyPath.clear();
    showFile(m_host, m_path);
    Q_EMIT pathChanged();
    if (!m_host || !m_loaded)
        return;
    // The renamed file normally holds what we last saved; anything else is an external change.
    bool ok = false;
    const QString content = m_host->read(newPath, &ok);
    if (ok && !m_exists) {
        m_exists = true;
        Q_EMIT existsChanged();
    }
    if (ok && content != m_savedText)
        onFileChanged(newPath);
    else if (m_dirty && !m_conflict)
        m_saveTimer.start();
}

QString PageController::textToSave() const
{
    const BlockDocument &doc = m_model->document();
    std::optional<BlockDocument> closed;
    const auto setBody = [&](int row, const QString &body) {
        if (!closed)
            closed = doc;
        closed->setBody(row, body);
    };
    for (int i = 0; i + 1 < doc.count(); ++i) {
        const QString body = loneFenceClosed(doc.at(i));
        if (!body.isEmpty() && loneFenceRunsOn(doc, i))
            setBody(i, body);
    }
    const int spaced = m_editorSpaceUid ? m_model->rowOfUid(m_editorSpaceUid) : -1;
    if (validRow(spaced)) {
        const QString body = bodyWithoutEditorSpace(doc.at(spaced));
        if (!body.isEmpty())
            setBody(spaced, body);
    }
    return closed ? closed->toString() : text();
}

void PageController::rememberEditorSpace(int row, const QString &tail)
{
    if (!validRow(row) || editingRow() != row || !tail.endsWith(u' '))
        return;
    m_editorSpaceUid = m_model->uidAt(row);
    m_editorSpaceTail = tail;
}

QString PageController::bodyWithoutEditorSpace(const markdown::BlockEntry &entry) const
{
    if (!m_editorSpaceUid || entry.uid != m_editorSpaceUid || !entry.body.endsWith(m_editorSpaceTail))
        return {};
    return entry.body.chopped(1);
}

void PageController::dropEditorSpace(quint64 uid)
{
    if (!uid || uid != m_editorSpaceUid)
        return;
    const int row = m_model->rowOfUid(uid);
    const QString body = validRow(row) ? bodyWithoutEditorSpace(m_model->document().at(row)) : QString();
    m_editorSpaceUid = 0;
    m_editorSpaceTail.clear();
    if (body.isEmpty() || m_undoing || m_sourceMode)
        return;
    if (inCommand()) { // part of the enclosing undo step
        applyOp([&](BlockDocument &d) { return d.setBody(row, body); });
        return;
    }
    // The file never had the space (textToSave()): the step that wrote it, when it is the last
    // one, ends without it — undo needs no step of its own for a change nobody saw saved.
    const BlockDocument::Version before = m_model->document().version();
    const Focus focusBefore = currentFocus();
    if (!applyOp([&](BlockDocument &d) { return d.setBody(row, body); }).changed())
        return;
    const BlockDocument::Delta delta = m_model->document().diffFrom(before);
    const int last = m_undo->index() - 1;
    auto *step = last >= 0 && last + 1 == m_undo->count()
                         ? static_cast<EditCommand *>(const_cast<QUndoCommand *>(m_undo->command(last)))
                         : nullptr;
    if (!step || !step->amendResult(delta))
        m_undo->push(new EditCommand(this, delta, focusBefore, Focus{-1, -1}, 0));
    m_typingOpen = false;
    markDirty();
}

void PageController::closeLoneFence(quint64 uid)
{
    if (m_undoing || m_sourceMode || !uid)
        return;
    const int row = m_model->rowOfUid(uid);
    if (!validRow(row))
        return;
    const markdown::BlockEntry &entry = m_model->document().at(row);
    const QString body = loneFenceClosed(entry);
    if (body.isEmpty())
        return;
    // A fence line that came with the file and was only looked at stays as it is (at the end of
    // the page it runs on over nothing).
    const bool edited = m_focusedUid != uid || m_focusedBody != entry.body;
    if (!edited && !loneFenceRunsOn(m_model->document(), row))
        return;
    if (inCommand()) { // part of the enclosing undo step
        applyOp([&](BlockDocument &d) { return d.setBody(row, body); });
        return;
    }
    // The fence was typed: one undo step with the typing. Undo restores the text as it was before
    // (a snapshot with the lone fence line would be read again as code running on over the blocks
    // after it), redo the closed fence.
    const bool continueTyping =
            m_typingOpen && m_typingUid == uid && m_typingCell.y() == -2 && m_undo->canUndo() && !m_undo->canRedo();
    const BlockDocument::Version before = m_model->document().version();
    const Focus focusBefore = currentFocus();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.setBody(row, body); });
    if (!r.changed())
        return;
    auto *cmd = new EditCommand(this, m_model->document().diffFrom(before), focusBefore, Focus{-1, -1}, uid);
    cmd->setMergeable(continueTyping);
    m_undo->push(cmd);
    m_typingOpen = false;
    markDirty();
}

void PageController::markDirty()
{
    // What save() would write: a lone fence being typed is saved closed (textToSave()), so the
    // text compared with the file must be closed too, or every later check would call it dirty.
    const QString current = m_sourceMode ? m_sourceText : textToSave();
    setDirty(current != m_savedText);
    if (m_dirty && !m_conflict) {
        if (m_saveTimer.interval() == 0 && !m_saving)
            save();
        else
            m_saveTimer.start();
    } else if (!m_dirty) {
        m_saveTimer.stop();
    }
    // The note's own mode key may have changed (undo of a switch, source mode, typing in the
    // properties); anything else keeps the mode the page was opened with.
    resolveMode(false, &current);
}

void PageController::setDirty(bool dirty)
{
    if (m_dirty == dirty)
        return;
    m_dirty = dirty;
    Q_EMIT dirtyChanged();
}

void PageController::setConflict(bool conflict, const QString &remote)
{
    if (!conflict) {
        // The next conflict gets a copy of its own.
        m_conflictCopyPath.clear();
        m_conflictCopyText.clear();
    }
    if (m_conflict == conflict && m_conflictText == remote)
        return;
    m_conflict = conflict;
    m_conflictText = conflict ? remote : QString();
    Q_EMIT conflictChanged();
}

void PageController::writeConflictCopy()
{
    if (!m_host || m_path.isEmpty() || !m_conflict)
        return;
    const QString local = m_sourceMode ? m_sourceText : textToSave();
    // One copy per conflict, rewritten when the local text changed (suspend, quit, close).
    if (!m_conflictCopyPath.isEmpty() && local == m_conflictCopyText)
        return;
    if (m_conflictCopyPath.isEmpty()) {
#ifdef BRULION_EDITOR_HAVE_SYNC_MERGE
        m_conflictCopyPath = sync::conflictCopyPath(m_path, QSysInfo::machineHostName(), QDateTime::currentDateTime());
#else
        const QString ext = paths::extension(m_path);
        m_conflictCopyPath = paths::stripMarkdownExtension(m_path) + u".conflict-local-"_s
                             + QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss"_s) + (ext.isEmpty() ? u".md"_s : ext);
#endif
    }
    if (m_host->write(m_conflictCopyPath, local))
        m_conflictCopyText = local;
}

void PageController::showFile(EditorHost *host, const QString &path)
{
    if (path.isEmpty())
        host = nullptr;
    const QString shown = host ? path : QString();
    if (host == m_shownHost && shown == m_shownPath)
        return;
    if (m_shownHost)
        m_shownHost->editorClosed(m_shownPath);
    m_shownHost = host;
    m_shownPath = shown;
    if (m_shownHost)
        m_shownHost->editorOpened(m_shownPath);
}

void PageController::onFileChanged(const QString &path)
{
    // A drawing saved again (an Obsidian Excalidraw plugin drawing is a Markdown file): its embeds
    // load it again.
    if (path != m_path && m_model && (!paths::isMarkdown(path) || (m_host && m_host->isDrawing(path))))
        m_model->attachmentChanged(path);
    if (path != m_path || !m_host || !m_loaded)
        return;
    bool ok = false;
    const QString remote = m_host->read(path, &ok);
    if (!ok) {
        Q_EMIT fileRemoved();
        return;
    }
    if (!m_exists) {
        // A new page created elsewhere meanwhile (quick capture into today's note, sync).
        m_exists = true;
        Q_EMIT existsChanged();
    }
    // The local version as it would be saved: merged with an open lone fence line ("```js" being
    // typed), the result would read as code running on over the blocks after it.
    const QString local = m_sourceMode ? m_sourceText : textToSave();
    // While a conflict is open every later change of the file decides again (it may merge now,
    // equal ours, or go back to the base): the conflict state and the "other version" offered by
    // takeRemote() never go stale.
    if (remote == m_savedText) {
        // Our own write coming back, or nothing new — or, during a conflict, the other side went
        // back to the common base: the local text can simply be saved.
        if (m_conflict) {
            setConflict(false);
            markDirty();
        }
        return;
    }
    if (remote == local) {
        setConflict(false);
        m_savedText = remote;
        setDirty(false);
        m_saveTimer.stop();
        return;
    }
    const auto applyRemoteText = [&](const QString &newText) {
        if (m_sourceMode) {
            m_sourceText = newText;
            Q_EMIT sourceTextChanged();
        }
        const int row = editingRow();
        const bool editing = row >= 0;
        const QHash<quint64, quint64> mapping = m_model->replaceContent(newText);
        remapBlocks(mapping);
        if (editing && !m_editingUid && m_model->count() > 0)
            m_editingUid = m_model->uidAt(std::clamp(row, 0, m_model->count() - 1));
        m_model->setEditingUid(m_editingUid);
        updateSelection();
        m_undo->clear();
        ++m_undoGeneration;
        m_typingOpen = false;
        Q_EMIT editingRowChanged();
        Q_EMIT selectedRowChanged();
        Q_EMIT zoomChanged();
        Q_EMIT foldStateChanged();
        emitEditTextReset();
        resolveMode(false); // another device may have switched the mode
    };
    if (!m_dirty) {
        setConflict(false);
        applyRemoteText(remote);
        m_savedText = remote;
        setDirty(false);
        Q_EMIT externallyChanged(false);
        return;
    }
    if (m_merge) {
        // Base: the text last read/written (also during a conflict: both sides derive from it).
        const MergeOutcome merged = m_merge(m_savedText, local, remote);
        if (merged.clean) {
            setConflict(false);
            applyRemoteText(merged.text);
            m_savedText = remote;
            markDirty();
            Q_EMIT externallyChanged(true);
            return;
        }
    }
    m_saveTimer.stop();
    setConflict(true, remote);
}

void PageController::resolveConflict(const QString &text)
{
    // A copy: `text` may be m_conflictText itself (takeRemote), which setConflict() clears —
    // "use the other version" used to save an empty page.
    const QString resolved = text;
    const QString remote = m_conflictText;
    const bool wasConflict = m_conflict;
    setConflict(false);
    if (wasConflict)
        m_savedText = remote; // the file holds the other version now (possibly empty)
    if (resolved != this->text())
        applyText(resolved, currentFocus());
    if (m_sourceMode) {
        m_sourceText = resolved;
        Q_EMIT sourceTextChanged();
    }
    markDirty();
    save();
}

void PageController::keepLocal()
{
    resolveConflict(m_sourceMode ? m_sourceText : textToSave());
}

void PageController::takeRemote()
{
    const QString remote = m_conflictText;
    resolveConflict(remote);
}

// --- Command machinery ------------------------------------------------------------------------------

bool PageController::validRow(int row) const
{
    return row >= 0 && row < m_model->count();
}

PageController::Focus PageController::currentFocus() const
{
    Focus focus{editingRow(), m_cursor, selectionAnchorRow(), selectedRow()};
    for (const quint64 uid : m_pickedUids)
        focus.selectionPicks.append(m_model->rowOfUid(uid));
    return focus;
}

void PageController::beginCommand()
{
    if (m_commandDepth++ == 0) {
        m_commandBefore = m_model->document().version();
        m_commandFocusBefore = currentFocus();
        m_typingOpen = false;
    }
}

void PageController::endCommand(const Focus &after)
{
    if (--m_commandDepth > 0)
        return;
    BlockDocument::Delta delta = m_model->document().diffFrom(m_commandBefore);
    m_commandBefore = {};
    if (!delta.isEmpty()) {
        m_undo->push(new EditCommand(this, std::move(delta), m_commandFocusBefore, after, 0));
        markDirty();
    }
}

EditResult PageController::applyOp(const std::function<EditResult(BlockDocument &)> &op)
{
    const EditResult r = m_model->apply(op);
    if (m_selectedUid || m_anchorUid)
        updateSelection(); // the selected subtrees may have changed
    return r;
}

void PageController::applyStep(const BlockDocument::Delta &delta, StepDirection direction, const Focus &focus)
{
    const QScopedValueRollback undoing(m_undoing, true);
    m_typingOpen = false;
    const quint64 zoomBefore = m_model->zoomUid();
    const QVariantList crumbsBefore = breadcrumb();
    const QVariantList foldsBefore = foldState();
    bool ok = false;
    applyOp([&](BlockDocument &d) {
        return direction == StepDirection::Undo ? d.revert(delta, &ok) : d.reapply(delta, &ok);
    });
    if (!ok) {
        qWarning("PageController: undo step does not match the document, ignored");
        return;
    }
    dropStaleUids();
    // Enter made in a zoom recorded the zoom of both versions (split()): undo zooms back in (also
    // when the step ended the zoom), redo restores the zoom after it. Other steps leave the zoom as
    // it is (it follows its block; a step that removes the zoomed block ends it). A document has no
    // zoom.
    if (focus.zoom != Focus::KeepZoom)
        m_model->setZoomUid(validRow(focus.zoom) && !documentMode() ? m_model->uidAt(focus.zoom) : 0);
    // Never a caret or a block selection in a hidden block: a zoom that does not hold the restored
    // block ends — one chosen after the step (the breadcrumb, then Undo), or the zoom brought back
    // above for an Enter next to a step made on the page — and the folds over it open, as when a
    // hidden block is edited (startEditing()).
    const bool selects = focus.row < 0 && validRow(focus.selectionAnchor) && validRow(focus.selectionFocus);
    if (validRow(focus.row)) {
        reveal(focus.row);
    } else if (selects) {
        reveal(focus.selectionFocus);
        reveal(focus.selectionAnchor);
        for (const int row : focus.selectionPicks)
            reveal(row);
    } else {
        reveal(editingRow()); // the block edited now stays edited
    }
    if (selects) {
        // The block selection of that version (an undone deletion selects its blocks again) —
        // with no block edited: the two never go together (another block edited meanwhile stops).
        m_anchorUid = m_model->uidAt(focus.selectionAnchor);
        m_selectedUid = m_model->uidAt(focus.selectionFocus);
        m_pickedUids.clear();
        for (const int row : focus.selectionPicks) {
            if (validRow(row))
                m_pickedUids.append(m_model->uidAt(row));
        }
        m_editingUid = 0;
    }
    updateSelection();
    if (focus.row >= 0 && validRow(focus.row)) {
        setFocus(focus, true);
    } else {
        m_model->setEditingUid(m_editingUid);
        Q_EMIT editingRowChanged();
        emitEditTextReset();
    }
    // Only what changed: the zoom (or the rows and titles of its breadcrumb), the lines of folded
    // blocks (the shell stores them).
    if (m_model->zoomUid() != zoomBefore || breadcrumb() != crumbsBefore)
        Q_EMIT zoomChanged();
    if (foldState() != foldsBefore)
        Q_EMIT foldStateChanged();
    markDirty();
}

void PageController::dropStaleUids()
{
    const auto present = [this](quint64 uid) { return uid && m_model->rowOfUid(uid) >= 0 ? uid : 0; };
    m_editingUid = present(m_editingUid);
    m_selectedUid = present(m_selectedUid);
    m_anchorUid = present(m_anchorUid);
    m_editorSpaceUid = present(m_editorSpaceUid);
    m_pickedUids.removeIf([this](quint64 uid) { return m_model->rowOfUid(uid) < 0; });
}

PageController::Focus PageController::focusFromResult(const EditResult &r, int fallbackRow) const
{
    Focus f;
    f.row = r.focus >= 0 ? r.focus : fallbackRow;
    if (!validRow(f.row))
        return {-1, -1};
    const QString edit = m_model->editTextAt(f.row);
    f.cursor = r.cursor >= 0 ? toEditCursor(f.row, r.cursor) : int(edit.size());
    return f;
}

void PageController::setFocus(const Focus &target, bool resetText)
{
    if (!validRow(target.row)) {
        stopEditing();
        return;
    }
    const quint64 uid = m_model->uidAt(target.row);
    const bool rowChanged = uid != m_editingUid;
    Focus focus = target;
    if (rowChanged && m_editingUid) {
        closeLoneFence(m_editingUid);
        dropEditorSpace(m_editingUid);
        focus.row = m_model->rowOfUid(uid); // the same row: only that block's body changed
        if (!validRow(focus.row))
            return;
    }
    if (rowChanged) {
        m_typingOpen = false;
        m_focusedUid = uid;
        m_focusedBody = m_model->document().at(focus.row).body;
    }
    m_editingUid = uid;
    const int len = int(m_model->editTextAt(focus.row).size());
    m_cursor = focus.cursor < 0 ? len : snapToCharacter(m_model->editTextAt(focus.row), focus.cursor);
    m_model->setEditingUid(uid);
    if (m_selectedUid || m_anchorUid) {
        resetSelection();
        updateSelection();
    }
    Q_EMIT editingRowChanged();
    Q_EMIT cursorPositionChanged();
    if (resetText)
        emitEditTextReset();
}

void PageController::emitEditTextReset()
{
    const int row = editingRow();
    if (row < 0)
        return;
    const QString edit = m_model->editTextAt(row);
    m_cursor = m_cursor < 0 ? int(edit.size()) : snapToCharacter(edit, m_cursor);
    Q_EMIT editTextReset(row, edit, m_cursor);
}

int PageController::toBodyCursor(int row, int editCursor) const
{
    const BlockShape s = m_model->shapeAt(row);
    return s.editOffset + std::clamp(editCursor < 0 ? int(s.editText.size()) : editCursor, 0, int(s.editText.size()));
}

int PageController::toEditCursor(int row, int bodyCursor) const
{
    const BlockShape s = m_model->shapeAt(row);
    return std::clamp(bodyCursor - s.editOffset, 0, int(s.editText.size()));
}

void PageController::editBody(int row, const QString &body, int focusCursor)
{
    if (!validRow(row) || m_model->document().at(row).body == body)
        return;
    const bool editingThis = editingRow() == row;
    const int oldLen = int(m_model->editTextAt(row).size());
    beginCommand();
    applyOp([&](BlockDocument &d) { return d.setBody(row, body); });
    if (editingThis) {
        const int newLen = int(m_model->editTextAt(row).size());
        if (focusCursor >= -1)
            m_cursor = focusCursor;
        else if (m_cursor >= oldLen)
            m_cursor = newLen; // the caret was at the end: keep it there
    }
    endCommand(currentFocus());
    if (editingThis)
        emitEditTextReset();
}

void PageController::applyText(const QString &newText, const Focus &after)
{
    if (newText == text())
        return;
    beginCommand();
    const QHash<quint64, quint64> mapping = m_model->replaceContent(newText); // the step: the changed blocks
    remapBlocks(mapping);
    m_model->setEditingUid(m_editingUid);
    updateSelection();
    endCommand(after);
    if (after.row >= 0)
        setFocus(after, true);
    else
        Q_EMIT editingRowChanged();
}

// --- Note mode ------------------------------------------------------------------------------------------

QString PageController::mode() const
{
    return markdown::noteModeName(m_mode);
}

QString PageController::modeSource() const
{
    switch (markdown::NoteModeSource(m_modeSource)) {
    case markdown::NoteModeSource::Frontmatter: return u"frontmatter"_s;
    case markdown::NoteModeSource::JournalDefault: return u"journal"_s;
    case markdown::NoteModeSource::Heuristic: return u"heuristic"_s;
    case markdown::NoteModeSource::PageDefault: break;
    }
    return u"default"_s;
}

void PageController::resolveMode(bool force, const QString *current)
{
    if (!force && !m_loaded)
        return;
    const QString content = current ? *current : m_sourceMode ? m_sourceText : text();
    const std::optional<NoteMode> key = markdown::frontmatterNoteMode(content);
    if (force)
        m_keylessSource = -1; // a new session: the content decides again
    else if (key == m_modeKey)
        return;
    m_modeKey = key;
    if (key) {
        applyMode(*key, int(markdown::NoteModeSource::Frontmatter));
        return;
    }
    // Without the key: the mode this session had without it (undoing a switch, or another device
    // removing the key, brings back the mode the page was opened with), resolved from the content
    // only the first time the page has no key.
    if (m_keylessSource < 0) {
        const VaultConfig config = vaultConfigFromMap(m_host ? m_host->vaultConfig() : QVariantMap());
        const bool journal = !m_path.isEmpty() && markdown::journalDateFromPath(m_path, config).has_value();
        const markdown::ResolvedNoteMode resolved = markdown::resolveNoteMode(content, journal, config);
        m_keylessMode = resolved.mode;
        m_keylessSource = int(resolved.source);
    }
    applyMode(m_keylessMode, m_keylessSource);
}

void PageController::applyMode(NoteMode mode, int source)
{
    const bool changed = mode != m_mode;
    if (!changed && source == m_modeSource)
        return;
    m_mode = mode;
    m_modeSource = source;
    m_model->setFoldingEnabled(mode == NoteMode::Outline);
    if (changed) {
        if (mode == NoteMode::Document && zoomBlock() >= 0)
            zoomTo(-1);
        // Back in the outline the folds hide children again: the block being edited stays in view.
        const int editing = editingRow();
        if (editing >= 0 && !m_model->isRowVisible(editing))
            reveal(editing);
    }
    Q_EMIT modeChanged();
}

void PageController::refreshMode()
{
    // Only a mode taken from the notebook's defaults follows them. The content is not looked at
    // again (any setting, also from another device, gives configChanged): what was written since
    // the page was opened never flips it.
    if (!m_loaded || m_keylessSource < 0)
        return;
    const auto source = markdown::NoteModeSource(m_keylessSource);
    if (source != markdown::NoteModeSource::JournalDefault && source != markdown::NoteModeSource::PageDefault)
        return;
    const VaultConfig config = vaultConfigFromMap(m_host ? m_host->vaultConfig() : QVariantMap());
    m_keylessMode = source == markdown::NoteModeSource::JournalDefault ? config.journalMode : config.pageMode;
    if (!m_modeKey)
        applyMode(m_keylessMode, m_keylessSource);
}

bool PageController::setMode(const QString &name)
{
    const std::optional<NoteMode> mode = markdown::parseNoteMode(name);
    if (!mode || !m_loaded)
        return false;
    const QString current = m_sourceMode ? m_sourceText : text();
    const QString updated = markdown::setNoteModeKey(current, *mode);
    // Frontmatter that cannot be edited safely (malformed YAML) is left alone: no switch then.
    if (markdown::frontmatterNoteMode(updated) != mode)
        return false;
    if (updated != current) {
        if (m_sourceMode) {
            m_sourceText = updated;
            m_sourceEdited = true;
            Q_EMIT sourceTextChanged();
            markDirty();
        } else {
            // The edited block keeps the caret: it follows its block (rows move down when the
            // frontmatter block appears), so no row is given here.
            applyText(updated, Focus{-1, -1});
        }
    }
    resolveMode(false);
    save();
    return true;
}

bool PageController::toggleMode()
{
    return setMode(markdown::noteModeName(documentMode() ? NoteMode::Outline : NoteMode::Document));
}

// --- Editing commands --------------------------------------------------------------------------------

void PageController::startEditing(int row, int cursor)
{
    if (!validRow(row))
        return;
    reveal(row);
    setFocus({row, cursor}, false);
}

void PageController::stopEditing()
{
    if (!m_editingUid)
        return;
    closeLoneFence(m_editingUid);
    if (!m_editingUid)
        return; // closing it stopped editing already
    dropEditorSpace(m_editingUid);
    m_editingUid = 0;
    m_typingOpen = false;
    m_model->setEditingUid(0);
    Q_EMIT editingRowChanged();
}

void PageController::setBody(int row, const QString &body)
{
    editBody(row, body);
}

void PageController::setEditText(int row, const QString &edit, int cursor)
{
    if (!validRow(row))
        return;
    if (cursor >= 0 && editingRow() == row)
        m_cursor = cursor;
    if (edit == m_model->editTextAt(row))
        return;
    const markdown::BlockEntry &entry = m_model->document().at(row);
    const quint64 uid = entry.uid;
    const QString body = bodyWithEditText(entry, edit);
    const BlockDocument::Version before = m_model->document().version();
    const Focus focusBefore = currentFocus();
    const bool continueTyping = m_typingOpen && m_typingUid == uid && m_typingCell.y() == -2 && m_undo->canUndo()
                                && !m_undo->canRedo();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.setBody(row, body); });
    if (!r.changed())
        return;
    auto *cmd = new EditCommand(this, m_model->document().diffFrom(before), focusBefore, Focus{row, cursor}, uid);
    cmd->setMergeable(continueTyping);
    m_undo->push(cmd);
    m_typingOpen = true;
    m_typingUid = uid;
    m_typingCell = QPoint(-1, -2);
    markDirty();
    // Normalisation: "[ ] " typed at the start turns into a checkbox, a closing fence is added
    // after a fence typed in a text block, etc. The caret keeps its place when the text before
    // it did not change; otherwise it moves with the text after it.
    const QString now = m_model->editTextAt(row);
    if (now != edit && editingRow() == row) {
        const int at = std::clamp(cursor >= 0 ? cursor : int(edit.size()), 0, int(edit.size()));
        int same = 0;
        while (same < at && same < now.size() && edit[same] == now[same])
            ++same;
        const int delta = int(edit.size() - now.size());
        m_cursor = same == at ? at : std::clamp(at - delta, 0, int(now.size()));
        Q_EMIT editTextReset(row, now, m_cursor);
    }
}

QVariantMap PageController::split(int row, int cursor)
{
    const auto result = [](int r, int c) { return QVariantMap{{u"row"_s, r}, {u"cursor"_s, c}}; };
    if (!validRow(row))
        return result(-1, -1);
    const BlockShape shape = m_model->shapeAt(row);
    const markdown::BlockEntry &entry = m_model->document().at(row);
    if (shape.view != BlockView::Text && shape.view != BlockView::Heading)
        return result(row, cursor);
    const int len = int(shape.editText.size());
    cursor = cursor < 0 ? len : snapToCharacter(shape.editText, cursor);
    // Enter made in a zoom records the zoomed row of both versions (Focus::zoom): undo and redo
    // bring that zoom back also when the step ended the zoom (a zoomed heading or paragraph).
    const int zoom = zoomBlock();
    const auto finish = [&](Focus f) {
        if (zoom >= 0) {
            if (m_commandDepth == 1)
                m_commandFocusBefore.zoom = zoom; // the zoomed row of the text before this step
            f.zoom = zoomBlock();
        }
        endCommand(f);
        setFocus(f, true);
        return f;
    };

    // "```calc" + Enter opens a code block (any fence length and info string Markdown accepts).
    // Not after a task marker: "- [ ] ```js" is a task's text, not a fence.
    const QString opening = shape.editText.trimmed();
    if (cursor == len && !shape.task && !opening.contains(u'\n')) {
        if (const auto fence = parseFence(opening); fence && !fence->closed) {
            const QString closing(fence->length, fence->marker);
            beginCommand();
            applyOp([&](BlockDocument &d) {
                return d.setBody(row, bodyWithEditText(entry, opening + u"\n\n"_s + closing));
            });
            const Focus f = finish({row, 0});
            return result(f.row, f.cursor);
        }
    }

    // Enter between the fence lines of code written in this text block: a line break, as in a
    // code block. A split would leave an unclosed fence in one of the halves, and in the saved
    // file that code would run on into the blocks after it.
    if (const int at = toBodyCursor(row, cursor); insideOwnFence(entry.body, at, at)) {
        const QString updated =
                bodyWithEditText(entry, shape.editText.left(cursor) + u'\n' + shape.editText.mid(cursor));
        beginCommand();
        applyOp([&](BlockDocument &d) { return d.setBody(row, updated); });
        const Focus f = finish({row, cursor + 1});
        return result(f.row, m_cursor);
    }

    // Document mode: Enter on an empty list item leaves the list (Obsidian) — a nested item moves
    // one level out, a top-level one becomes a paragraph.
    if (len == 0 && entry.kind == BlockKind::ListItem && documentMode()) {
        const QVariantMap left = leaveList(row);
        if (left.value(u"row"_s).toInt() >= 0)
            return left;
    }

    // Enter on an empty nested item outdents it (common outliner behaviour).
    if (len == 0 && !shape.task && entry.kind == BlockKind::ListItem && entry.depth > 0
        && (zoom < 0 || entry.parent != zoom)) {
        if (outdent(row))
            return result(editingRow(), 0);
    }

    // A zoomed list item keeps what Enter makes inside the zoom: a new first child at its end or
    // start (an item above the zoom root would be outside the zoom, out of sight), its tail as the
    // first child in the middle (the zoom root's children are shown whatever its fold).
    const bool listRoot = row == zoom && entry.kind == BlockKind::ListItem;
    beginCommand();
    EditResult r;
    if (listRoot && (cursor == len || cursor == 0)) {
        r = applyOp([&](BlockDocument &d) { return d.insertAfter(row, QString(), entry.depth + 1); });
    } else {
        const bool collapsed = !listRoot && m_model->isCollapsed(row);
        const int bodyCursor = toBodyCursor(row, cursor);
        r = applyOp([&](BlockDocument &d) { return d.split(row, bodyCursor, collapsed); });
        if (listRoot && r.changed() && r.focus > row && m_model->document().parent(r.focus) != row) {
            // No children: the tail became the next sibling, outside the zoom — its first child
            // instead (its body, so the caret after a continued task marker, stays as it is).
            const quint64 uid = m_model->uidAt(r.focus);
            applyOp([&](BlockDocument &d) { return d.indent(m_model->rowOfUid(uid)); });
            r.focus = m_model->rowOfUid(uid);
        }
    }
    const Focus f = r.changed() ? focusFromResult(r, row) : Focus{row, cursor};
    // Never leave the caret in a hidden block. A zoomed heading or paragraph has no children
    // (BlockDocument: only list items nest), so the block Enter made below it (at its end, in its
    // middle, on an empty one) cannot be inside the zoom: the zoom ends, the page shows it, and
    // Enter has done what it does on the page; undo zooms back in. At its start the empty
    // paragraph goes above, out of sight, and the caret stays in the zoom root.
    if (zoom >= 0 && validRow(f.row) && !m_model->isRowVisible(f.row))
        reveal(f.row); // leaves the zoom (zoomChanged); a heading or paragraph has no folds to open
    const Focus done = finish(f);
    return result(done.row, done.cursor);
}

QVariantMap PageController::mergeWithPrevious(int row)
{
    const auto result = [](int r, int c) { return QVariantMap{{u"row"_s, r}, {u"cursor"_s, c}}; };
    if (!validRow(row) || row == zoomBlock())
        return result(-1, -1);
    const int prev = m_model->previousVisibleRow(row);
    if (prev < 0)
        return result(-1, -1);
    const BlockDocument &doc = m_model->document();
    if (prev != row - 1) {
        // The previous block in the file is folded away: unfold it first.
        for (int p = doc.parent(row - 1); p >= 0; p = doc.parent(p)) {
            if (m_model->isCollapsed(p))
                m_model->setCollapsed(p, false);
        }
        Q_EMIT foldStateChanged();
    }
    const std::optional<QString> joined = joinedAfterFence(doc.at(row - 1).body, doc.at(row).body);
    beginCommand();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.mergeWithPrevious(row); });
    Focus f;
    if (r.changed()) {
        f = focusFromResult(r, row - 1);
        if (joined) {
            applyOp([&](BlockDocument &d) { return d.setBody(f.row, *joined); });
            ++f.cursor;
        }
    } else {
        // The previous block is not a text block (code, table…): move the caret there.
        f = {r.focus >= 0 ? r.focus : row - 1, -1};
    }
    endCommand(f);
    setFocus(f, true);
    return result(f.row, f.cursor);
}

QVariantMap PageController::mergeNext(int row)
{
    const auto result = [](int r, int c) { return QVariantMap{{u"row"_s, r}, {u"cursor"_s, c}}; };
    if (!validRow(row))
        return result(-1, -1);
    const int next = m_model->nextVisibleRow(row);
    if (next != row + 1)
        return result(-1, -1);
    const BlockView nextView = m_model->shapeAt(next).view;
    if (nextView != BlockView::Text && nextView != BlockView::Heading)
        return result(-1, -1);
    const int cursor = int(m_model->editTextAt(row).size());
    const BlockDocument &doc = m_model->document();
    const std::optional<QString> joined = joinedAfterFence(doc.at(row).body, doc.at(next).body);
    beginCommand();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.mergeWithPrevious(next); });
    Focus f = r.changed() ? focusFromResult(r, row) : Focus{row, cursor};
    if (r.changed() && joined) {
        applyOp([&](BlockDocument &d) { return d.setBody(f.row, *joined); });
        ++f.cursor;
    }
    endCommand(f);
    setFocus(f, true);
    return result(f.row, f.cursor);
}

QVariantMap PageController::leaveList(int row)
{
    const auto result = [](int r, int c) { return QVariantMap{{u"row"_s, r}, {u"cursor"_s, c}}; };
    if (!validRow(row) || row == zoomBlock())
        return result(-1, -1);
    const markdown::BlockEntry &entry = m_model->document().at(row);
    if (entry.kind != BlockKind::ListItem)
        return result(-1, -1);
    const bool editingThis = editingRow() == row;
    if (entry.depth > 0) {
        const quint64 uid = entry.uid;
        if (!outdent(row))
            return result(-1, -1);
        return result(m_model->rowOfUid(uid), editingThis ? m_cursor : 0);
    }
    beginCommand();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.convertToParagraph(row); });
    const Focus f = r.changed() ? focusFromResult(r, row) : Focus{row, 0};
    endCommand(editingThis ? f : currentFocus());
    if (!r.changed())
        return result(-1, -1);
    if (editingThis)
        setFocus(f, true);
    return result(f.row, f.cursor);
}

QVariantMap PageController::toggleList(int row)
{
    const auto result = [](int r, int c) { return QVariantMap{{u"row"_s, r}, {u"cursor"_s, c}}; };
    if (!validRow(row))
        return result(-1, -1);
    const BlockKind kind = m_model->document().at(row).kind;
    if (kind == BlockKind::ListItem)
        return leaveList(row);
    if (kind != BlockKind::Paragraph)
        return result(-1, -1);
    const bool editingThis = editingRow() == row;
    beginCommand();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.convertToList(row); });
    // The edit text is the same (the marker is drawn, not edited): the caret stays where it was.
    const Focus f{row, editingThis ? m_cursor : 0};
    endCommand(editingThis ? f : currentFocus());
    if (!r.changed())
        return result(-1, -1);
    if (editingThis)
        setFocus(f, true);
    return result(row, f.cursor);
}

bool PageController::indent(int row)
{
    if (!validRow(row) || row == zoomBlock())
        return false;
    // Document mode: Tab indents list items only (a paragraph never becomes an outline block).
    if (documentMode() && m_model->document().at(row).kind != BlockKind::ListItem)
        return false;
    beginCommand();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.indent(row); });
    // The new parent must show its new child.
    const int parent = m_model->document().parent(m_model->rowOfUid(m_model->uidAt(row)));
    if (r.changed() && parent >= 0 && m_model->isCollapsed(parent)) {
        m_model->setCollapsed(parent, false);
        Q_EMIT foldStateChanged();
    }
    endCommand(currentFocus());
    if (r.changed())
        emitEditTextReset();
    return r.changed();
}

bool PageController::outdent(int row)
{
    if (!validRow(row) || row == zoomBlock())
        return false;
    const BlockDocument &doc = m_model->document();
    const int zoom = zoomBlock();
    if (zoom >= 0 && doc.parent(row) == zoom)
        return false; // would leave the zoomed subtree
    beginCommand();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.outdent(row); });
    endCommand(currentFocus());
    if (r.changed())
        emitEditTextReset();
    return r.changed();
}

bool PageController::moveUp(int row)
{
    if (!validRow(row) || row == zoomBlock())
        return false;
    beginCommand();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.moveUp(row); });
    endCommand(currentFocus());
    Q_EMIT editingRowChanged();
    return r.changed();
}

bool PageController::moveDown(int row)
{
    if (!validRow(row) || row == zoomBlock())
        return false;
    beginCommand();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.moveDown(row); });
    endCommand(currentFocus());
    Q_EMIT editingRowChanged();
    return r.changed();
}

void PageController::toggleTask(int row)
{
    if (!validRow(row))
        return;
    const QDate day = today();
    beginCommand();
    applyOp([&](BlockDocument &d) { return d.toggleTask(row, day); });
    endCommand(currentFocus());
    Q_EMIT editingRowChanged();
    emitEditTextReset();
}

void PageController::cycleTask(int row)
{
    if (!validRow(row))
        return;
    const std::optional<TaskStatus> status = m_model->shapeAt(row).task;
    if (!status)
        setTaskStatus(row, u"todo"_s);
    else if (*status == TaskStatus::Todo)
        setTaskStatus(row, u"doing"_s);
    else if (*status == TaskStatus::Doing)
        setTaskStatus(row, u"done"_s);
    else
        setTaskStatus(row, QString());
}

namespace {

/// Where a caret at `cursor` of `before` is in `after`, the same text edited in one place (a
/// task's ✅ date added or removed, its marker gone): before the edit it stays, after it it moves
/// with the text, inside it it goes to the end of the new text there.
int caretThroughEdit(const QString &before, const QString &after, int cursor)
{
    const TextDelta d = TextDelta::between(before, after);
    if (cursor <= d.from)
        return cursor;
    if (cursor >= d.from + d.removed.size())
        return cursor + int(d.inserted.size() - d.removed.size());
    return int(d.from + d.inserted.size());
}

} // namespace

std::optional<QString> PageController::slashTaskStatus(const QString &command)
{
    if (command == u"task")
        return u"todo"_s;
    if (command == u"taskDoing")
        return u"doing"_s;
    if (command == u"taskDone")
        return u"done"_s;
    if (command == u"taskWaiting")
        return u"deferred"_s;
    if (command == u"taskCancelled")
        return u"cancelled"_s;
    if (command == u"taskNone")
        return QString();
    return std::nullopt;
}

void PageController::applyTaskStatus(int row, const QString &status)
{
    const QDate day = today();
    if (status.isEmpty()) {
        // Back to a plain block: reopen first (removes ✅/❌ dates), then drop the marker.
        if (!m_model->shapeAt(row).task)
            return;
        const quint64 uid = m_model->uidAt(row);
        applyOp([&](BlockDocument &d) { return d.setTaskStatus(row, TaskStatus::Todo, day); });
        const int r = m_model->rowOfUid(uid);
        if (r >= 0) {
            const QString body = withoutTaskMarker(m_model->document().at(r));
            applyOp([&](BlockDocument &d) { return d.setBody(r, body); });
        }
    } else if (const auto s = taskStatusFromName(status)) {
        applyOp([&](BlockDocument &d) { return d.setTaskStatus(row, *s, day); });
    }
}

void PageController::setTaskStatus(int row, const QString &status)
{
    if (!validRow(row))
        return;
    // The caret of the block being edited stays in its words when the text before it changes.
    const quint64 uid = m_model->uidAt(row);
    const bool editingThis = m_editingUid == uid;
    const QString editBefore = editingThis ? m_model->editTextAt(row) : QString();
    beginCommand();
    applyTaskStatus(row, status);
    const int r = m_model->rowOfUid(uid);
    if (editingThis && r >= 0 && m_cursor >= 0)
        m_cursor = caretThroughEdit(editBefore, m_model->editTextAt(r), m_cursor);
    endCommand(currentFocus());
    Q_EMIT editingRowChanged();
    emitEditTextReset();
}

void PageController::removeTaskMarker(int row)
{
    if (validRow(row) && m_model->shapeAt(row).task)
        setTaskStatus(row, QString());
}

int PageController::deleteBlock(int row)
{
    if (!validRow(row))
        return -1;
    const bool wasZoom = row == zoomBlock();
    const int prev = m_model->previousVisibleRow(row);
    const bool editingThis = editingRow() == row;
    const bool hadSelection = m_selectedUid != 0;
    beginCommand();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.remove(row, true); });
    int focusRow = -1;
    if (r.changed()) {
        focusRow = prev >= 0 ? prev : (r.focus >= 0 && validRow(r.focus) ? r.focus : -1);
        if (focusRow >= 0 && !m_model->isRowVisible(focusRow))
            focusRow = m_model->firstVisibleRow();
    }
    const Focus f{focusRow, -1};
    endCommand(editingThis ? f : currentFocus());
    if (wasZoom)
        zoomTo(-1);
    if (editingThis) {
        if (focusRow >= 0)
            setFocus(f, true);
        else
            stopEditing();
    }
    if (hadSelection && editingRow() < 0)
        setSelectedRow(focusRow);
    return focusRow;
}

int PageController::insertBlockAfter(int row, const QString &body)
{
    const BlockDocument &doc = m_model->document();
    int depth = 0;
    if (validRow(row))
        depth = doc.at(row).depth;
    else
        row = -1;
    // Document mode: a paragraph after text, a list item only inside a list.
    const bool listItem = !documentMode() || (row >= 0 && doc.at(row).kind == BlockKind::ListItem);
    beginCommand();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.insertAfter(row, body, depth, listItem); });
    const Focus f = r.changed() ? focusFromResult(r, row) : Focus{row, -1};
    endCommand(f);
    if (r.changed())
        setFocus(f, true);
    return f.row;
}

int PageController::appendEmptyBlock()
{
    const BlockDocument &doc = m_model->document();
    const int zoom = zoomBlock();
    if (zoom >= 0) {
        const markdown::RowRange range = doc.subtreeRange(zoom);
        const int last = range.last();
        if (last != zoom && m_model->editTextAt(last).isEmpty() && doc.at(last).kind == BlockKind::ListItem) {
            setFocus({last, 0}, true);
            return last;
        }
        beginCommand();
        const EditResult r = applyOp(
                [&](BlockDocument &d) { return d.insertAfter(last, QString(), doc.at(zoom).depth + 1); });
        const Focus f = focusFromResult(r, last);
        endCommand(f);
        setFocus(f, true);
        return f.row;
    }
    const int last = m_model->count() - 1;
    if (documentMode()) {
        // A document ends with an empty paragraph to type into (an empty list item there is used).
        if (last >= 0 && m_model->isRowVisible(last) && m_model->shapeAt(last).view == BlockView::Text
            && m_model->editTextAt(last).isEmpty()
            && (doc.at(last).kind == BlockKind::Paragraph || doc.at(last).kind == BlockKind::ListItem)) {
            setFocus({last, 0}, true);
            return last;
        }
        beginCommand();
        const EditResult r = applyOp([&](BlockDocument &d) { return d.insertAfter(last, QString(), 0, false); });
        const Focus f = r.changed() ? focusFromResult(r, last) : Focus{last, -1};
        endCommand(f);
        setFocus(f, true);
        return f.row;
    }
    if (last >= 0 && doc.at(last).kind == BlockKind::ListItem && doc.at(last).depth == 0
        && m_model->editTextAt(last).isEmpty() && m_model->isRowVisible(last)) {
        setFocus({last, 0}, true);
        return last;
    }
    beginCommand();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.insertAfter(last, QString(), 0); });
    const Focus f = r.changed() ? focusFromResult(r, last) : Focus{last, -1};
    endCommand(f);
    setFocus(f, true);
    return f.row;
}

int PageController::insertMarkdown(int row, const QString &markdown)
{
    QString md = markdown;
    md.replace(u"\r\n"_s, u"\n"_s);
    while (md.endsWith(u'\n'))
        md.chop(1);
    if (md.isEmpty())
        return -1;
    const BlockDocument &doc = m_model->document();
    const QString eol = doc.eol();
    const QString content = text();
    QList<text::Line> lines = text::splitLines(content);
    const QString mdEol = QString(md).replace(u"\n"_s, eol);
    int firstLine = 0;
    QString result;
    const auto join = [&](qsizetype from, qsizetype to) {
        QString s;
        for (qsizetype k = from; k < to && k < lines.size(); ++k)
            s += lines[k].text + lines[k].eol;
        return s;
    };
    if (validRow(row) && m_model->editTextAt(row).trimmed().isEmpty()) {
        // Replace the empty block.
        const int start = doc.at(row).line;
        const int end = start + lineCount(doc.blockSource(row));
        const QString lastEol = end - 1 < lines.size() ? lines[end - 1].eol : QString();
        result = join(0, start) + mdEol + lastEol + join(end, lines.size());
        firstLine = start;
    } else {
        int top = validRow(row) ? row : m_model->count() - 1;
        while (top >= 0 && doc.parent(top) >= 0)
            top = doc.parent(top);
        const int last = top >= 0 ? doc.subtreeRange(top).last() : -1;
        const int insertLine = last + 1 < doc.count() ? doc.at(last + 1).line : int(lines.size());
        QString before = join(0, insertLine);
        if (insertLine >= lines.size() && !lines.isEmpty() && !lines.last().text.isEmpty())
            before = content; // no final newline: everything
        if (!before.isEmpty() && !before.endsWith(u'\n') && !before.endsWith(u'\r'))
            before += eol;
        const bool listAfterList = md.startsWith(u"- "_s) && last >= 0 && doc.at(last).kind == BlockKind::ListItem;
        if (!before.isEmpty() && !listAfterList && !before.endsWith(eol + eol))
            before += eol;
        QString after = join(insertLine, lines.size());
        result = before + mdEol + eol + (after.isEmpty() || after.startsWith(eol) || listAfterList ? QString() : eol) + after;
        firstLine = lineCount(before) - (before.endsWith(u'\n') ? 1 : 0);
    }
    const BlockDocument preview(result);
    const int focusRow = std::max(0, preview.indexAtLine(firstLine));
    applyText(result, Focus{focusRow, -1});
    return focusRow;
}

int PageController::insertTemplate(int row, const QString &templatePath)
{
    if (!m_host)
        return -1;
    bool ok = false;
    const QString tpl = m_host->read(templatePath, &ok);
    if (!ok)
        return -1;
    markdown::TemplateVars vars;
    vars.now = currentDateTime();
    vars.title = title();
    vars.locale = m_english ? dates::UiLocale::English : dates::UiLocale::Polish;
    const markdown::TemplateResult filled = markdown::applyTemplate(tpl, vars);
    return insertMarkdown(row, filled.text);
}

int PageController::startNewPage(const QString &templatePath)
{
    if (!m_host || !m_loaded || m_exists || m_sourceMode || m_conflict || m_model->count() > 0 || m_dirty
        || templatePath.trimmed().isEmpty())
        return appendEmptyBlock();
    bool ok = false;
    const QString tpl = m_host->read(paths::normalize(templatePath), &ok);
    if (!ok)
        return appendEmptyBlock();
    markdown::TemplateVars vars;
    vars.now = currentDateTime();
    // A daily note's template is filled for its own day ({{date}} of yesterday's note is yesterday).
    if (const auto day = markdown::journalDateFromPath(m_path, vaultConfigFromMap(m_host->vaultConfig())))
        vars.now = QDateTime(*day, vars.now.time());
    vars.title = title();
    vars.locale = m_english ? dates::UiLocale::English : dates::UiLocale::Polish;
    const QString filled = markdown::applyTemplate(tpl, vars).text;
    if (filled.trimmed().isEmpty())
        return appendEmptyBlock();
    // The template is the page's starting text, like the content of a loaded file: no undo step
    // and nothing to save until the user changes something.
    m_model->resetContent(filled);
    resolveMode(true); // the template may name the mode
    int row = -1;
    for (int r = 0; r < m_model->count() && row < 0; ++r) {
        if (m_model->document().at(r).kind == BlockKind::ListItem && m_model->shapeAt(r).view == BlockView::Text
            && m_model->editTextAt(r).trimmed().isEmpty() && m_model->isRowVisible(r))
            row = r;
    }
    if (row < 0) {
        // No empty block in the template: one to type into at the end (part of the start too) — a
        // paragraph in a document.
        const int last = m_model->count() - 1;
        const bool listItem = !documentMode();
        const EditResult r =
                m_model->apply([&](BlockDocument &d) { return d.insertAfter(last, QString(), 0, listItem); });
        row = r.changed() ? focusFromResult(r, last).row : last;
    }
    m_savedText = text();
    m_undo->clear();
    ++m_undoGeneration;
    m_typingOpen = false;
    m_saveTimer.stop();
    setDirty(false);
    updateTitleRow();
    Q_EMIT foldStateChanged();
    return row;
}

// --- Zoom and folding ------------------------------------------------------------------------------

void PageController::zoomInto(int row)
{
    if (!validRow(row) || documentMode()) // a document has no bullets to zoom into
        return;
    m_model->setZoomUid(m_model->uidAt(row));
    const int editing = editingRow();
    if (editing >= 0 && !m_model->isRowVisible(editing))
        stopEditing();
    clearSelection(); // its rows may be outside the zoom
    Q_EMIT zoomChanged();
}

void PageController::zoomOut()
{
    const int zoom = zoomBlock();
    if (zoom < 0)
        return;
    zoomTo(m_model->document().parent(zoom));
}

void PageController::zoomTo(int row)
{
    setZoomRow(row);
    // The block being edited stays shown. The zoom root's children are shown whatever its fold:
    // leaving a folded zoom root (or a zoom below a fold) opens the folds over the block, as when
    // a hidden block is edited; a zoom that does not hold the block stops editing (zoomInto()).
    const int editing = editingRow();
    bool unfolded = false;
    if (editing >= 0 && !m_model->isRowVisible(editing)) {
        const BlockDocument &doc = m_model->document();
        const int zoom = zoomBlock();
        const markdown::RowRange range = zoom >= 0 ? doc.subtreeRange(zoom) : markdown::RowRange{0, doc.count()};
        if (editing >= range.first && editing <= range.last()) {
            for (int p = doc.parent(editing); p >= 0 && p != zoom; p = doc.parent(p)) {
                if (m_model->isCollapsed(p)) {
                    m_model->setCollapsed(p, false);
                    unfolded = true;
                }
            }
        }
        if (!m_model->isRowVisible(editing))
            stopEditing();
    }
    Q_EMIT zoomChanged();
    if (unfolded) {
        updateSelection();
        Q_EMIT foldStateChanged();
    }
}

void PageController::setZoomRow(int row)
{
    const quint64 uid = validRow(row) ? m_model->uidAt(row) : 0;
    if (uid != m_model->zoomUid())
        clearSelection(); // its rows may be outside the zoom
    m_model->setZoomUid(uid);
}

void PageController::toggleFold(int row)
{
    if (!validRow(row) || documentMode())
        return;
    setFolded(row, !m_model->isCollapsed(row));
}

void PageController::setFolded(int row, bool folded)
{
    // Document mode: folds are kept for the outline mode but none are made or undone.
    if (!validRow(row) || documentMode() || !m_model->document().hasChildren(row)
        || m_model->isCollapsed(row) == folded)
        return;
    m_model->setCollapsed(row, folded);
    const int editing = editingRow();
    if (folded && editing >= 0 && !m_model->isRowVisible(editing))
        setFocus({row, -1}, true);
    updateSelection(); // rows shown changed (a fold keeps a selected parent's children selected)
    Q_EMIT foldStateChanged();
}

void PageController::foldAll(bool folded)
{
    if (documentMode())
        return;
    QSet<quint64> uids;
    const BlockDocument &doc = m_model->document();
    if (folded) {
        for (int i = 0; i < doc.count(); ++i) {
            if (doc.hasChildren(i))
                uids.insert(doc.at(i).uid);
        }
    }
    m_model->setCollapsedUids(uids);
    const int editing = editingRow();
    if (editing >= 0 && !m_model->isRowVisible(editing))
        stopEditing();
    updateSelection();
    Q_EMIT foldStateChanged();
}

void PageController::setFoldState(const QVariantList &lines)
{
    QSet<quint64> uids;
    const BlockDocument &doc = m_model->document();
    for (const QVariant &v : lines) {
        const int i = doc.indexAtLine(v.toInt());
        if (i >= 0 && doc.at(i).line == v.toInt() && doc.hasChildren(i))
            uids.insert(doc.at(i).uid);
    }
    m_model->setCollapsedUids(uids);
    updateSelection();
    Q_EMIT foldStateChanged();
}

void PageController::reveal(int row)
{
    if (!validRow(row) || m_model->isRowVisible(row))
        return;
    const BlockDocument &doc = m_model->document();
    const int zoom = zoomBlock();
    if (zoom >= 0) {
        const markdown::RowRange range = doc.subtreeRange(zoom);
        if (row < range.first || row > range.last()) {
            // (Not zoomTo(): the block edited until now need not stay shown, `row` is shown next.)
            setZoomRow(-1);
            Q_EMIT zoomChanged();
        }
    }
    for (int p = doc.parent(row); p >= 0; p = doc.parent(p)) {
        if (m_model->isCollapsed(p))
            m_model->setCollapsed(p, false);
    }
    updateSelection();
    Q_EMIT foldStateChanged();
}

int PageController::rowOfLine(int line) const
{
    return m_model->document().indexAtLine(line);
}

int PageController::rowOfBlockId(const QString &blockId) const
{
    if (blockId.isEmpty())
        return -1;
    const BlockDocument &doc = m_model->document();
    for (int i = 0; i < doc.count(); ++i) {
        if (doc.at(i).blockId == blockId)
            return i;
    }
    return -1;
}

int PageController::trackBlock(int row)
{
    if (!validRow(row))
        return 0;
    const int handle = m_nextTrackHandle++;
    m_tracked.insert(handle, m_model->uidAt(row));
    return handle;
}

int PageController::trackedRow(int handle) const
{
    const quint64 uid = m_tracked.value(handle, 0);
    return uid ? m_model->rowOfUid(uid) : -1;
}

void PageController::untrack(int handle)
{
    m_tracked.remove(handle);
}

void PageController::remapBlocks(const QHash<quint64, quint64> &mapping)
{
    m_editingUid = mapping.value(m_editingUid, 0);
    m_selectedUid = mapping.value(m_selectedUid, 0);
    m_anchorUid = mapping.value(m_anchorUid, 0);
    m_editorSpaceUid = mapping.value(m_editorSpaceUid, 0);
    for (quint64 &uid : m_pickedUids)
        uid = mapping.value(uid, 0);
    m_pickedUids.removeAll(0);
    for (auto it = m_tracked.begin(); it != m_tracked.end(); ++it)
        it.value() = mapping.value(it.value(), 0);
}

int PageController::nextVisibleRow(int row) const
{
    return m_model->nextVisibleRow(row);
}

int PageController::previousVisibleRow(int row) const
{
    return m_model->previousVisibleRow(row);
}

int PageController::lastVisibleRow() const
{
    return m_model->lastVisibleRow();
}

// --- Inline insertions ---------------------------------------------------------------------------------

int PageController::insertToken(int row, int cursor, const QString &token)
{
    if (!validRow(row))
        return cursor;
    const QString edit = m_model->editTextAt(row);
    int newCursor = cursor;
    const QString updated = insertTokenAt(edit, cursor < 0 ? int(edit.size()) : snapToCharacter(edit, cursor), token, &newCursor);
    editBody(row, bodyWithEditText(m_model->document().at(row), updated), newCursor);
    return newCursor;
}

int PageController::replaceRange(int row, int from, int to, const QString &replacement)
{
    if (!validRow(row))
        return from;
    const QString edit = m_model->editTextAt(row);
    from = snapToCharacter(edit, from);
    to = std::max(from, snapToCharacter(edit, to));
    const QString updated = edit.left(from) + replacement + edit.mid(to);
    const int cursor = from + int(replacement.size());
    editBody(row, bodyWithEditText(m_model->document().at(row), updated), cursor);
    return cursor;
}

QVariantMap PageController::pasteText(int row, int from, int to, const QString &pasted)
{
    const auto result = [](int r, int c) { return QVariantMap{{u"row"_s, r}, {u"cursor"_s, c}}; };
    if (!validRow(row))
        return result(-1, -1);
    QString text = pasted;
    text.replace(u"\r\n"_s, u"\n"_s).replace(u'\r', u'\n');
    // Block ids that resolve elsewhere would be duplicated (`((id))` is vault-wide): dropped,
    // unless this is the text of the last cut (a move).
    text = withoutTakenIds(text, cutIdsFor(text));
    const BlockShape shape = m_model->shapeAt(row);
    const QString edit = shape.editText;
    from = snapToCharacter(edit, from);
    to = std::max(from, snapToCharacter(edit, to));
    // Between the fence lines of code written in a text block the text goes in as code, as in a
    // code block (blank lines in it would split it into blocks otherwise).
    const bool textBlock =
            (shape.view == BlockView::Text || shape.view == BlockView::Heading)
            && !insideOwnFence(m_model->document().at(row).body, toBodyCursor(row, from), toBodyCursor(row, to));
    QString markdown = text;
    while (markdown.endsWith(u'\n'))
        markdown.chop(1);
    // One list item ("- jeden", "- [ ] Skuć 📅 …": a copied block) pasted into a text block brings
    // its text, not its marker ("- - jeden"). Pasted over the whole text of a list item, a task or
    // a code, table… item becomes that block (its checkbox, its fence); over the whole text of a
    // paragraph, the paragraph becomes that list item. A numbered item only when it is this app's
    // own copy: elsewhere "2025. rok" is more often text than a list.
    if (textBlock) {
        const BlockDocument probe(markdown);
        const auto numbered = [](const markdown::BlockEntry &e) {
            const QString written = e.prefix.trimmed();
            return !written.isEmpty() && written.front().isDigit();
        };
        if (probe.count() == 1 && probe.at(0).kind == BlockKind::ListItem
            && (!numbered(probe.at(0)) || isOwnCopy(pasted))) {
            const markdown::BlockEntry &item = probe.at(0);
            const markdown::BlockEntry &target = m_model->document().at(row);
            const bool whole = from == 0 && to == edit.size();
            const bool itemText = shapeOf(item).view == BlockView::Text;
            if (whole && target.kind == BlockKind::ListItem && (item.taskStatus || !itemText)) {
                QString body = item.body;
                if (const QString closing = missingClosingFence(body); !closing.isEmpty())
                    body += u'\n' + closing;
                editBody(row, body, -1);
                const int cursor = int(m_model->editTextAt(row).size());
                if (editingRow() == row)
                    setFocus({row, cursor}, true);
                return result(row, cursor);
            }
            const bool becomesItem = whole && target.kind == BlockKind::Paragraph && shape.view == BlockView::Text;
            if (!becomesItem && (itemText || !markdown.contains(u'\n')))
                text = markdown = withoutTaskMarker(item);
        }
    }
    const BlockDocument pasteDoc(markdown);
    // One block goes in as text — unless it is a code block, table… pasted between other text.
    const bool oneTextBlock = pasteDoc.count() < 2
                              && (pasteDoc.isEmpty() || shapeOf(pasteDoc.at(0)).view == BlockView::Text
                                  || (from == 0 && to == edit.size()));
    if (!markdown.contains(u'\n') || !textBlock || oneTextBlock) {
        if (textBlock)
            text = markdown; // no trailing line breaks inside a text block
        const int cursor = replaceRange(row, from, to, text);
        if (editingRow() == row)
            setFocus({row, cursor}, true);
        return result(row, cursor);
    }
    const QString prefix = edit.left(from);
    const QString suffix = edit.mid(to);
    const quint64 uid = m_model->uidAt(row);
    // Only plain text joins the text around the caret: a code block, table, quote… glued to it
    // ("a```js", "```bc") would no longer be one.
    const auto isPlainText = [](const markdown::BlockEntry &e) { return shapeOf(e).view == BlockView::Text; };
    const bool joinFirst = prefix.isEmpty() || isPlainText(pasteDoc.at(0));
    beginCommand();
    applyOp([&](BlockDocument &d) {
        return d.setBody(row, bodyWithEditText(d.at(row), joinFirst ? prefix + pasteDoc.at(0).body : prefix));
    });
    int current = m_model->rowOfUid(uid);
    const markdown::BlockEntry &anchor = m_model->document().at(current);
    const int baseDepth = anchor.depth;
    const bool intoList = anchor.kind == BlockKind::ListItem;
    const int firstDepth = pasteDoc.at(0).depth;
    int last = current;
    for (int k = joinFirst ? 1 : 0; k < pasteDoc.count(); ++k) {
        const markdown::BlockEntry &e = pasteDoc.at(k);
        const int depth = baseDepth + std::max(0, e.depth - firstDepth);
        const bool listItem = intoList || e.kind == BlockKind::ListItem;
        // A pasted snippet whose fence is not closed gets its closing fence: on the page, blocks
        // follow it that the fence would turn into code.
        QString body = e.body;
        if (const QString closing = missingClosingFence(body); !closing.isEmpty())
            body += u'\n' + closing;
        const EditResult r = applyOp([&](BlockDocument &d) { return d.insertAfter(last, body, depth, listItem); });
        if (r.focus >= 0)
            last = r.focus;
    }
    int cursor = int(m_model->editTextAt(last).size());
    if (!suffix.isEmpty()) {
        const int target = last;
        if (isPlainText(m_model->document().at(target))) {
            const QString lastEdit = m_model->editTextAt(target);
            applyOp([&](BlockDocument &d) { return d.setBody(target, bodyWithEditText(d.at(target), lastEdit + suffix)); });
        } else {
            const int depth = m_model->document().at(target).depth;
            const EditResult r = applyOp([&](BlockDocument &d) { return d.insertAfter(target, suffix, depth, intoList); });
            if (r.focus >= 0) {
                last = r.focus;
                cursor = 0;
            }
        }
    }
    const Focus f{last, cursor};
    endCommand(f);
    setFocus(f, true);
    return result(f.row, f.cursor);
}

QString PageController::ensureBlockRef(int row)
{
    if (!validRow(row))
        return {};
    const QString existing = m_model->document().at(row).blockId;
    if (!existing.isEmpty())
        return existing;
    const QPointer<EditorHost> host = m_host;
    if (host)
        host->finishIndexing(); // every id of the vault known (not inside the edit: it signals)
    QString id;
    beginCommand();
    applyOp([&](BlockDocument &d) {
        // Any block of the vault with that id counts, also one without text (blockText() is "").
        const markdown::BlockIdResult res = d.ensureBlockId(
                row, [host](const QString &candidate) { return host && host->blockIdTaken(candidate); });
        id = res.id;
        return res.edit;
    });
    endCommand(currentFocus());
    if (editingRow() == row)
        emitEditTextReset();
    return id;
}

QString PageController::copyBlockRef(int row)
{
    const QVariantMap copied = copyLink(row, u"blockRef"_s);
    return copied.value(u"ok"_s).toBool() && validRow(row) ? m_model->document().at(row).blockId : QString();
}

// --- Links to this page, its headings and blocks -------------------------------------------------------

bool PageController::isHeading(int row) const
{
    if (!validRow(row))
        return false;
    const BlockShape shape = m_model->shapeAt(row);
    return shape.view == BlockView::Heading && shape.headingLevel > 0;
}

int PageController::rowOfHeading(const QString &fragment) const
{
    QList<int> rows;
    const int index = markdown::findHeading(documentHeadings(m_model->document(), &rows), fragment);
    return index >= 0 ? rows.at(index) : -1;
}

int PageController::headingRowFor(int row) const
{
    if (!validRow(row))
        return -1;
    // OutlineModel's rule: the last heading with text at or above the row.
    for (int i = row; i >= 0; --i) {
        if (isHeading(i) && !plainTextOf(m_model->document().at(i)).isEmpty())
            return i;
    }
    return -1;
}

QString PageController::linkTarget() const
{
    if (m_path.isEmpty())
        return {};
    // One rule for every copied link (notebook::pageLinkTarget): a daily note by its file name,
    // else the index's shortest target ("Dom", "Projekty/Dom" next to another Dom).
    const VaultConfig config = m_host ? vaultConfigFromMap(m_host->vaultConfig()) : VaultConfig();
    return notebook::pageLinkTarget(
            m_path, config,
            [this](const VaultPath &path) { return m_host ? m_host->linkTarget(path, QString()) : QString(); },
            [this](const QString &title) { return m_host ? m_host->resolveLink(title, QString()) : QString(); });
}

QVariantMap PageController::linkText(int row, const QString &kind)
{
    const auto failed = [](const QString &reason) {
        return QVariantMap{{u"ok"_s, false}, {u"text"_s, QString()}, {u"reason"_s, reason}};
    };
    const auto copied = [](const QString &text) {
        return QVariantMap{
                {u"ok"_s, !text.isEmpty()}, {u"text"_s, text}, {u"reason"_s, text.isEmpty() ? u"noPage"_s : QString()}};
    };
    using markdown::ReferenceKind;
    const QString page = linkTarget();
    if (page.isEmpty())
        return failed(u"noPage"_s);
    if (kind == u"page")
        return copied(markdown::formatReference(ReferenceKind::PageLink, page));
    if (!validRow(row))
        return failed(u"noRow"_s);
    if (m_sourceMode)
        return failed(u"sourceMode"_s);
    const bool heading = isHeading(row);
    const bool headingKind = kind == u"heading" || (kind == u"embed" && heading);
    if (kind == u"heading" && !heading)
        return failed(u"notHeading"_s);
    if (!headingKind && kind != u"blockRef" && kind != u"blockLink" && kind != u"embed")
        return failed(u"noRow"_s);
    if (headingKind) {
        // The heading's name, or a heading path when another heading has that name too.
        QList<int> rows;
        const QList<Heading> all = documentHeadings(m_model->document(), &rows);
        const QString fragment = markdown::uniqueHeadingFragment(all, int(rows.indexOf(row)));
        if (!fragment.isEmpty())
            return copied(markdown::formatReference(
                    kind == u"embed" ? ReferenceKind::HeadingEmbed : ReferenceKind::HeadingLink, page, fragment));
    }
    // The block's id, added when it has none.
    QString id = m_model->document().at(row).blockId;
    if (id.isEmpty()) {
        if (!m_allowNewBlockIds)
            return failed(u"readOnly"_s);
        id = ensureBlockRef(row);
        if (id.isEmpty())
            return failed(u"noId"_s);
    }
    ReferenceKind reference = ReferenceKind::BlockRef;
    if (headingKind)
        reference = kind == u"embed" ? ReferenceKind::BlockLinkEmbed : ReferenceKind::BlockLink;
    else if (kind == u"blockLink")
        reference = ReferenceKind::BlockLink;
    else if (kind == u"embed")
        reference = ReferenceKind::BlockEmbed;
    return copied(markdown::formatReference(reference, page, {}, id));
}

QVariantMap PageController::copyLink(int row, const QString &kind)
{
    QVariantMap result = linkText(row, kind);
    if (!result.value(u"ok"_s).toBool())
        return result;
    const QVariantMap copied = copyText(result.value(u"text"_s).toString());
    if (!copied.value(u"ok"_s).toBool()) {
        result.insert(u"ok"_s, false);
        result.insert(u"reason"_s, copied.value(u"reason"_s));
    }
    return result;
}

void PageController::setReminder(int row, const QString &at, bool alarm, const QString &recurrence, int from, int to)
{
    if (!validRow(row))
        return;
    const auto spec = dates::parseDateSpec(at.trimmed());
    if (!spec)
        return;
    const QString token = markdown::formatReminderToken(*spec, alarm);
    QString body = m_model->document().at(row).body;
    const bool editingExisting = from >= 0 && to > from;
    if (editingExisting)
        body = replaceTokenRange(body, from, to, token);
    else
        body = setFirstLineToken(body, markdown::TokenKind::Reminder, token, true);
    // The block's 🔁 rule as the dialog left it: only the rule's own text changes (words after it
    // stay), and an unchanged rule stays as written (markdown::setBlockRecurrence()).
    QString ruleToken;
    if (!recurrence.trimmed().isEmpty()) {
        QString rule = dates::canonicalRecurrence(recurrence);
        if (rule.isEmpty())
            rule = recurrence.trimmed();
        ruleToken = u"🔁 "_s + rule;
        body = markdown::setBlockRecurrence(body, recurrence);
    } else if (editingExisting) {
        body = markdown::setBlockRecurrence(body, QString());
    }
    // The caret at the end of the edited text stays there, after what was written — with a space
    // after it, as after a completed date: a "/" or "#" typed next starts a command or tag
    // instead of sticking to the token ("/przypomnienie" in an empty block, then "/").
    // That space (also one typed before "/przypomnienie", now after the token) is not saved while
    // nothing follows it (rememberEditorSpace()).
    const bool caretAtEnd = editingRow() == row && m_cursor >= m_model->editTextAt(row).size();
    QString tail;
    for (const QString &written : {token, ruleToken}) {
        if (!caretAtEnd || written.isEmpty())
            continue;
        if (body.endsWith(written))
            body += u' ';
        else if (!body.endsWith(written + u' '))
            continue;
        tail = written + u' ';
        break;
    }
    if (!tail.isEmpty())
        rememberEditorSpace(row, tail); // before the edit saves the page
    editBody(row, body);
}

void PageController::removeToken(int row, int from, int to)
{
    if (!validRow(row))
        return;
    editBody(row, replaceTokenRange(m_model->document().at(row).body, from, to, QString()));
}

void PageController::setDateToken(int row, const QString &field, const QString &spec)
{
    if (!validRow(row))
        return;
    markdown::TokenKind kind = markdown::TokenKind::Due;
    QStringView emoji = markdown::tokens::Due;
    if (field == u"scheduled") {
        kind = markdown::TokenKind::Scheduled;
        emoji = markdown::tokens::Scheduled;
    } else if (field == u"start") {
        kind = markdown::TokenKind::Start;
        emoji = markdown::tokens::Start;
    } else if (field != u"due") {
        return;
    }
    QString token;
    if (!spec.trimmed().isEmpty()) {
        const auto parsed = dates::parseDateSpec(spec.trimmed());
        if (!parsed)
            return;
        token = emoji.toString() + u' ' + parsed->toString();
    }
    editBody(row, setFirstLineToken(m_model->document().at(row).body, kind, token));
}

void PageController::setRecurrence(int row, const QString &rule)
{
    if (!validRow(row))
        return;
    // Only the rule's text changes: words written after it stay (markdown::setBlockRecurrence()).
    editBody(row, markdown::setBlockRecurrence(m_model->document().at(row).body, rule));
}

QVariantMap PageController::reminderAt(int row, int from, int to) const
{
    if (!validRow(row))
        return {};
    const QString body = m_model->document().at(row).body;
    if (from < 0 || to > body.size() || to <= from)
        return {};
    const QString part = body.mid(from, to - from);
    const QList<markdown::Token> toks = markdown::findTokens(part);
    for (const markdown::Token &t : toks) {
        if (t.kind != markdown::TokenKind::Reminder && t.kind != markdown::TokenKind::ReminderCompat)
            continue;
        DateSpec spec = t.spec();
        if (!spec.time) {
            const VaultConfig config = vaultConfigFromMap(m_host ? m_host->vaultConfig() : QVariantMap());
            spec.time = config.defaultReminderTime;
        }
        const QString recurrence = blockDates(row).value(u"recurrence"_s).toString();
        return {{u"at"_s, spec.toString()},
                {u"alarm"_s, t.alarm},
                {u"recurrence"_s, recurrence},
                {u"context"_s, blockContext(row)}};
    }
    return {};
}

QVariantMap PageController::dateChip(int row, const QString &url) const
{
    if (!validRow(row))
        return {};
    const VaultConfig config = vaultConfigFromMap(m_host ? m_host->vaultConfig() : QVariantMap());
    const BlockShape shape = m_model->shapeAt(row);
    QVariantMap chip = tokenChip(m_model->document().at(row).body, url, m_model->renderOffsetAt(row),
                                 shape.task.has_value(), config);
    if (chip.value(u"kind"_s).toString() == u"day") {
        // What a click on the link opens, and the link "Kopiuj link" copies.
        const QString target = chip.value(u"target"_s).toString();
        const QString pageUrl = u"page:"_s + QString::fromLatin1(QUrl::toPercentEncoding(target));
        chip.insert(u"url"_s, pageUrl);
        chip.insert(u"copyText"_s, linkMenu(pageUrl).value(u"copyText"_s));
    }
    return chip;
}

markdown::TokenEditContext PageController::tokenEditContext() const
{
    markdown::TokenEditContext context;
    context.config = vaultConfigFromMap(m_host ? m_host->vaultConfig() : QVariantMap());
    context.today = today();
    context.defaultReminderTime = context.config.defaultReminderTime;
    return context;
}

bool PageController::applyTokenAction(int row, int from, int to, const QString &action, const QVariant &a,
                                      const QVariant &b)
{
    if (!validRow(row))
        return false;
    const QString body = m_model->document().at(row).body;
    const QString edited = markdown::applyTokenAction(body, from, to, action, a, b, tokenEditContext());
    if (edited == body)
        return false;
    editBody(row, edited);
    return true;
}

namespace {

/// What the reminder dialog shows for a reminder chip of a block copy or a cell (editReminderWith):
/// dialogAt (with the default time when the reminder has none) and context (the text).
void addReminderDialogData(QVariantMap &chip, const QString &context, QTime defaultTime)
{
    if (chip.value(u"kind"_s).toString() != u"reminder")
        return;
    QString at = chip.value(u"at"_s).toString();
    if (chip.value(u"time"_s).toString().isEmpty())
        at += u' ' + defaultTime.toString(u"HH:mm"_s);
    chip.insert(u"dialogAt"_s, at);
    chip.insert(u"context"_s, context);
}

/// The text of a table cell (tableRow -1 = the header), nullopt when there is no such cell.
std::optional<QString> tableCellText(const QString &body, int tableRow, int column)
{
    const auto model = calc::parseTable(body);
    if (!model || column < 0 || column >= model->header.size())
        return std::nullopt;
    if (tableRow < 0)
        return model->header[column];
    if (tableRow >= model->rows.size())
        return std::nullopt;
    const QStringList &cells = model->rows[tableRow];
    return column < cells.size() ? cells[column] : QString();
}

/// The block of `doc` a copy of it is of (`raw`: its Markdown as a query result or an embed has
/// it, `line`: its line there): the block on `line` when it is still that block, else the nearest
/// one with the same text (the copy's line was a moment behind: an edit above it, a sync); -1 when
/// no block has that text any more. The copy's own token may differ from the block's in nothing
/// else: the token menus edit what the copy shows.
int blockOfCopy(const BlockDocument &doc, int line, const QString &raw)
{
    const BlockDocument copy(raw);
    if (copy.isEmpty())
        return -1;
    const QString &body = copy.at(0).body;
    const int at = doc.indexAtLine(line);
    if (at >= 0 && doc.at(at).body == body)
        return at;
    int best = -1;
    for (int i = 0; i < doc.count(); ++i) {
        if (doc.at(i).body != body)
            continue;
        if (best < 0 || std::abs(doc.at(i).line - line) < std::abs(doc.at(best).line - line))
            best = i;
    }
    return best;
}

/// Where the text of a table cell (tableRow -1 = the header) is written in `body`: [from, to) of
/// its trimmed text between the pipes (calc::parseTable()'s rows: the header, the delimiter row,
/// then the body rows); nullopt when the row or the cell is not written.
std::optional<std::pair<qsizetype, qsizetype>> tableCellSpan(const QString &body, int tableRow, int column)
{
    const QList<text::Line> lines = text::splitLines(body);
    qsizetype header = 0;
    while (header < lines.size() && QStringView(lines.at(header).text).trimmed().isEmpty())
        ++header;
    const qsizetype at = tableRow < 0 ? header : header + 2 + tableRow;
    if (column < 0 || at >= lines.size())
        return std::nullopt;
    const QString &line = lines.at(at).text;
    qsizetype begin = 0;
    qsizetype end = line.size();
    while (begin < end && line.at(begin).isSpace())
        ++begin;
    while (end > begin && line.at(end - 1).isSpace())
        --end;
    // The cells between unescaped pipes (`\|` is text), without the empty ones outside outer pipes.
    QList<std::pair<qsizetype, qsizetype>> cells;
    qsizetype start = begin;
    for (qsizetype i = begin; i < end; ++i) {
        if (line.at(i) == u'|' && (i == begin || line.at(i - 1) != u'\\')) {
            cells.append({start, i});
            start = i + 1;
        }
    }
    cells.append({start, end});
    if (!cells.isEmpty() && cells.front().first == cells.front().second)
        cells.removeFirst();
    if (!cells.isEmpty() && cells.back().first == cells.back().second)
        cells.removeLast();
    if (column >= cells.size())
        return std::nullopt;
    auto [from, to] = cells.at(column);
    while (from < to && line.at(from).isSpace())
        ++from;
    while (to > from && line.at(to - 1).isSpace())
        --to;
    return std::pair{lines.at(at).start + from, lines.at(at).start + to};
}

} // namespace

QVariantMap PageController::tokenChipIn(const QString &raw, const QString &url) const
{
    if (url.startsWith(u"day:"_s))
        return {};
    const BlockDocument doc(raw);
    if (doc.isEmpty())
        return {};
    const markdown::BlockEntry &e = doc.at(0);
    const BlockShape shape = shapeOf(e);
    // Where the rendered text starts in the body (detail::renderBodyHtml(): a heading in a list item).
    const int offset = shape.view == BlockView::Heading && e.kind != BlockKind::Heading ? shape.editOffset : 0;
    const VaultConfig config = vaultConfigFromMap(m_host ? m_host->vaultConfig() : QVariantMap());
    QVariantMap chip = tokenChip(e.body, url, offset, shape.task.has_value(), config);
    if (!chip.contains(u"ref"_s))
        return {};
    addReminderDialogData(chip, plainTextOf(e, 120), config.defaultReminderTime);
    return chip;
}

QVariantMap PageController::editTokenIn(const QString &path, int line, const QString &raw, const QVariantMap &refMap,
                                        const QString &action, const QVariant &a, const QVariant &b)
{
    const auto result = [](bool ok, const QString &reason, int undo = 0) {
        return QVariantMap{{u"ok"_s, ok}, {u"reason"_s, reason}, {u"undo"_s, undo}};
    };
    const auto ref = tokenRefFromMap(refMap);
    if (path.isEmpty() || line < 0 || !ref)
        return result(false, u"gone"_s);
    if (path == m_path) {
        // This page: an edit of the block in the editor (its undo step) — the block the copy is
        // of, wherever it is now (blocks typed above it are not in the index yet).
        const int row = blockOfCopy(m_model->document(), line, raw);
        if (!validRow(row))
            return result(false, u"gone"_s);
        const auto range = markdown::findTokenRef(m_model->document().at(row).body, *ref);
        if (!range)
            return result(false, u"gone"_s);
        return applyTokenAction(row, int(range->first), int(range->second), action, a, b)
                       ? result(true, QString())
                       : result(false, u"unchanged"_s);
    }
    if (!m_host)
        return result(false, u"failed"_s);
    bool ok = false;
    const QString before = m_host->read(path, &ok);
    if (!ok)
        return result(false, u"gone"_s);
    const BlockDocument doc(before);
    const int block = blockOfCopy(doc, line, raw);
    if (block < 0)
        return result(false, u"gone"_s); // the block changed or went since the copy was made
    const QString after = markdown::editBlockToken(before, doc.at(block).line, *ref, action, a, b, tokenEditContext());
    if (after == before) {
        // The token is not there, or the action changes nothing.
        const bool there = markdown::findTokenRef(doc.at(block).body, *ref).has_value();
        return result(false, there ? u"unchanged"_s : u"gone"_s);
    }
    switch (m_host->writeIfUnchanged(path, after, before)) {
    case EditorHost::WriteResult::Written: break;
    case EditorHost::WriteResult::Changed: return result(false, u"changed"_s);
    case EditorHost::WriteResult::Failed: return result(false, u"failed"_s);
    }
    // Only the last few edits can be undone (each holds two copies of the file).
    constexpr int MaxForeignEdits = 8;
    const int handle = m_nextForeignEdit++;
    m_foreignEdits.insert(handle, ForeignEdit{path, before, after});
    m_foreignEdits.remove(handle - MaxForeignEdits);
    return result(true, QString(), handle);
}

bool PageController::undoTokenEdit(int undo)
{
    const auto it = m_foreignEdits.constFind(undo);
    if (it == m_foreignEdits.constEnd() || !m_host)
        return false;
    const ForeignEdit edit = *it;
    m_foreignEdits.erase(it);
    // writeIfUnchanged() writes a missing file too: a page renamed or deleted since the edit must
    // not come back under its old name with its old text.
    bool exists = false;
    m_host->read(edit.path, &exists);
    if (!exists)
        return false;
    return m_host->writeIfUnchanged(edit.path, edit.before, edit.after) == EditorHost::WriteResult::Written;
}

QVariantMap PageController::tableCellChip(int row, int tableRow, int column, const QString &url) const
{
    if (!validRow(row) || url.startsWith(u"day:"_s))
        return {};
    const auto cell = tableCellText(m_model->document().at(row).body, tableRow, column);
    if (!cell)
        return {};
    const VaultConfig config = vaultConfigFromMap(m_host ? m_host->vaultConfig() : QVariantMap());
    QVariantMap chip = tokenChip(*cell, url, 0, false, config);
    if (!chip.isEmpty())
        addReminderDialogData(chip, markdown::stripTokens(*cell).simplified(), config.defaultReminderTime);
    return chip;
}

bool PageController::applyTableCellToken(int row, int tableRow, int column, int from, int to, const QString &action,
                                         const QVariant &a, const QVariant &b)
{
    if (!validRow(row))
        return false;
    const auto cell = tableCellText(m_model->document().at(row).body, tableRow, column);
    if (!cell)
        return false;
    const QString edited = markdown::applyTokenAction(*cell, from, to, action, a, b, tokenEditContext());
    if (edited == *cell)
        return false;
    // Like a hand edit: only the cell's text changes in the row as written (the other cells, the
    // padding and the pipes stay) — when the row still reads the same with it; else the table is
    // written anew (setTableCell()).
    const QString body = m_model->document().at(row).body;
    const auto escaped = [](QString text) { return text.replace(u"|"_s, u"\\|"_s); };
    if (const auto span = tableCellSpan(body, tableRow, column);
        span && QStringView(body).sliced(span->first, span->second - span->first) == escaped(*cell)) {
        const QString spliced = body.left(span->first) + escaped(edited) + body.mid(span->second);
        auto expected = calc::parseTable(body);
        if (expected) {
            if (tableRow < 0)
                expected->header[column] = edited.trimmed();
            else
                expected->rows[tableRow][column] = edited.trimmed();
        }
        if (expected && calc::parseTable(spliced) == expected) {
            editBody(row, spliced);
            return true;
        }
    }
    setTableCell(row, tableRow, column, edited);
    return true;
}

void PageController::setDateTokenAt(int row, int from, int to, const QString &spec)
{
    if (!validRow(row))
        return;
    const QString body = m_model->document().at(row).body;
    if (!dateTokenAt(body, from, to))
        return;
    if (spec.trimmed().isEmpty()) {
        editBody(row, replaceTokenRange(body, from, to, QString()));
        return;
    }
    const auto parsed = dates::parseDateSpec(spec.trimmed());
    if (parsed)
        editBody(row, setTokenDate(body, from, to, *parsed));
}

void PageController::shiftDateTokenAt(int row, int from, int to, int days, int minutes)
{
    if (!validRow(row))
        return;
    const QString body = m_model->document().at(row).body;
    const auto t = dateTokenAt(body, from, to);
    if (!t)
        return;
    const VaultConfig config = vaultConfigFromMap(m_host ? m_host->vaultConfig() : QVariantMap());
    editBody(row, setTokenDate(body, from, to, shiftedSpec(t->spec(), days, minutes, config.defaultReminderTime)));
}

void PageController::moveDateTokenTo(int row, int from, int to, int days)
{
    if (!validRow(row))
        return;
    const QString body = m_model->document().at(row).body;
    const auto t = dateTokenAt(body, from, to);
    if (!t)
        return;
    editBody(row, setTokenDate(body, from, to, DateSpec{today().addDays(days), t->time}));
}

void PageController::setReminderAlarmAt(int row, int from, int to, bool alarm)
{
    if (!validRow(row))
        return;
    editBody(row, setReminderAlarm(m_model->document().at(row).body, from, to, alarm));
}

QString PageController::blockContext(int row) const
{
    return validRow(row) ? plainTextOf(m_model->document().at(row), 120) : QString();
}

QVariantMap PageController::blockDates(int row) const
{
    QVariantMap out{{u"due"_s, QString()}, {u"scheduled"_s, QString()}, {u"start"_s, QString()}, {u"recurrence"_s, QString()}};
    if (!validRow(row))
        return out;
    const QString body = m_model->document().at(row).body;
    const qsizetype nl = body.indexOf(u'\n');
    const QString first = nl < 0 ? body : body.left(nl);
    for (const markdown::Token &t : markdown::findTokens(markdown::maskInlineCode(first))) {
        switch (t.kind) {
        case markdown::TokenKind::Due: out[u"due"_s] = t.spec().toString(); break;
        case markdown::TokenKind::Scheduled: out[u"scheduled"_s] = t.spec().toString(); break;
        case markdown::TokenKind::Start: out[u"start"_s] = t.spec().toString(); break;
        default: break;
        }
    }
    // The rule at the start of the 🔁 token's text (words after it are no part of it).
    out[u"recurrence"_s] = markdown::blockRecurrence(body);
    return out;
}

QString PageController::journalLink(const QString &date) const
{
    const QDate d = QDate::fromString(date.left(10), u"yyyy-MM-dd"_s);
    if (!d.isValid())
        return {};
    const VaultConfig config = vaultConfigFromMap(m_host ? m_host->vaultConfig() : QVariantMap());
    return u"[["_s + paths::pageTitle(markdown::journalPathForDate(d, config)) + u"]]"_s;
}

QVariantMap PageController::applyCompletion(int row, const QVariantMap &triggerMap, const QVariantMap &item)
{
    QVariantMap out{{u"cursor"_s, m_cursor}, {u"action"_s, QString()}};
    if (!validRow(row))
        return out;
    const CompletionTrigger t = CompletionTrigger::fromMap(triggerMap);
    if (t.mode == u"slash")
        return applySlashCommand(row, t.from, t.to, item.value(u"command"_s).toString());
    QString edit = m_model->editTextAt(row);
    int from = snapToCharacter(edit, t.from);
    int to = std::max(from, snapToCharacter(edit, t.to));
    const QString insert = item.value(u"insertText"_s).toString();
    QString replacement;
    int cursorAfter = 0;
    beginCommand();
    if (t.mode == u"pages") {
        replacement = insert;
        if (t.closing == u"]]")
            cursorAfter = from + int(replacement.size()) + 2;
        else if (t.closing == u"|")
            cursorAfter = from + int(replacement.size());
        else {
            replacement += u"]]"_s;
            cursorAfter = from + int(replacement.size());
        }
    } else if (t.mode == u"blocks") {
        QString id = item.value(u"blockId"_s).toString();
        const QString path = item.value(u"path"_s).toString();
        const int line = item.value(u"line"_s, -1).toInt();
        if (id.isEmpty() && (path.isEmpty() || path == m_path)) {
            const int target = m_model->document().indexAtLine(line);
            if (target >= 0) {
                const quint64 uid = m_model->uidAt(row);
                const int oldLen = int(edit.size());
                const int firstLineEnd = int(edit.indexOf(u'\n') < 0 ? edit.size() : edit.indexOf(u'\n'));
                id = ensureBlockRef(target);
                row = m_model->rowOfUid(uid);
                const QString updated = m_model->editTextAt(row);
                if (target == row && firstLineEnd <= from) {
                    from += int(updated.size()) - oldLen;
                    to += int(updated.size()) - oldLen;
                }
                edit = updated;
            }
        } else if (id.isEmpty() && m_host) {
            id = m_host->ensureBlockId(path, line);
        }
        if (id.isEmpty()) {
            endCommand(currentFocus());
            return out;
        }
        replacement = id;
        if (t.closing == u"))")
            cursorAfter = from + int(replacement.size()) + 2;
        else {
            replacement += u"))"_s;
            cursorAfter = from + int(replacement.size());
        }
    } else if (t.mode == u"tags") {
        // After the typed '#': the name, in brackets when it is no simple tag (#[[Jan Kowalski]],
        // #[[C++]]: markdown::tagSyntax).
        const QString tag = markdown::tagSyntax(insert);
        replacement = tag.isEmpty() ? insert : tag.mid(1);
        if (to >= edit.size() || !edit[to].isSpace())
            replacement += u' ';
        cursorAfter = from + int(replacement.size());
    } else if (t.mode == u"date") {
        replacement = insert;
        if (to >= edit.size() || !edit[to].isSpace())
            replacement += u' ';
        cursorAfter = from + int(replacement.size());
    } else {
        endCommand(currentFocus());
        return out;
    }
    const QString updated = edit.left(from) + replacement + edit.mid(to);
    if (editingRow() == row)
        m_cursor = cursorAfter;
    applyOp([&](BlockDocument &d) { return d.setBody(row, bodyWithEditText(m_model->document().at(row), updated)); });
    // A space added after a date or tag that ends the block is not saved (rememberEditorSpace()).
    if (replacement.endsWith(u' ') && cursorAfter == int(updated.size()))
        rememberEditorSpace(row, replacement);
    endCommand(Focus{row, cursorAfter});
    if (editingRow() == row)
        emitEditTextReset();
    out[u"cursor"_s] = cursorAfter;
    return out;
}

QVariantMap PageController::applySlashCommand(int row, int from, int to, const QString &command)
{
    QVariantMap out{{u"cursor"_s, m_cursor}, {u"action"_s, QString()}, {u"row"_s, row}};
    if (!validRow(row))
        return out;
    const quint64 uid = m_model->uidAt(row);
    QString edit = m_model->editTextAt(row);
    from = snapToCharacter(edit, from);
    to = std::max(from, snapToCharacter(edit, to));
    edit = edit.left(from) + edit.mid(to);
    int cursor = from;
    const auto currentRow = [&] { return m_model->rowOfUid(uid); };
    const auto setEdit = [&](const QString &e) {
        const int r = currentRow();
        applyOp([&](BlockDocument &d) { return d.setBody(r, bodyWithEditText(m_model->document().at(r), e)); });
    };
    const auto insertAt = [&](const QString &s) {
        edit = edit.left(cursor) + s + edit.mid(cursor);
        cursor += int(s.size());
    };

    beginCommand();
    setEdit(edit);
    QString action;
    Focus focus{currentRow(), cursor};
    const markdown::BlockEntry &entry = m_model->document().at(currentRow());
    const bool listItem = entry.kind == BlockKind::ListItem;
    const QString cmd = command;
    if (const auto status = slashTaskStatus(cmd)) {
        // A task's status (a block that is no task becomes one; "" removes the marker), in the
        // same undo step as removing the "/command"; the caret stays in the words around it.
        const QString before = m_model->editTextAt(currentRow());
        applyTaskStatus(currentRow(), *status);
        const int r = currentRow();
        focus = {r, r >= 0 ? caretThroughEdit(before, m_model->editTextAt(r), cursor) : -1};
    } else if (cmd == u"reminder" || cmd == u"alarm" || cmd == u"due" || cmd == u"scheduled" || cmd == u"recurrence"
               || cmd == u"date" || cmd == u"template" || cmd == u"image" || cmd == u"webImage"
               || cmd == u"drawing") {
        action = cmd;
    } else if (cmd == u"today" || cmd == u"tomorrow") {
        insertAt(journalLink(today().addDays(cmd == u"tomorrow" ? 1 : 0).toString(u"yyyy-MM-dd"_s)));
        setEdit(edit);
        focus = {currentRow(), cursor};
    } else if (cmd == u"time") {
        insertAt(currentDateTime().toString(u"HH:mm"_s));
        setEdit(edit);
        focus = {currentRow(), cursor};
    } else if (cmd == u"priorityHigh" || cmd == u"priorityLow") {
        const QString emoji = (cmd == u"priorityHigh" ? markdown::tokens::PriorityHigh : markdown::tokens::PriorityLow).toString();
        const int r = currentRow();
        const int before = int(m_model->editTextAt(r).size());
        applyOp([&](BlockDocument &d) {
            return d.setBody(r, setFirstLineToken(d.at(r).body, markdown::TokenKind::Priority, emoji));
        });
        focus = {r, cursor + (cursor >= before ? int(m_model->editTextAt(r).size()) - before : 0)};
    } else if (cmd == u"h1" || cmd == u"h2" || cmd == u"h3") {
        const int level = cmd.back().digitValue();
        const int r = currentRow();
        const markdown::BlockEntry &e = m_model->document().at(r);
        if (e.kind == BlockKind::ListItem) {
            const QString updated = withHeading(edit, level);
            focus = {r, cursor + int(updated.size() - edit.size())};
            setEdit(updated);
        } else {
            const QString updated = withHeading(e.body, level);
            applyOp([&](BlockDocument &d) { return d.setBody(r, updated); });
            focus = {currentRow(), cursor + int(updated.size() - e.body.size())};
        }
    } else if (cmd == u"quote" || cmd == u"callout") {
        const QString prefix = cmd == u"quote" ? u"> "_s : u"> [!note] "_s;
        const int r = currentRow();
        if (listItem) {
            setEdit(prefix + edit);
        } else {
            const QString body = m_model->document().at(r).body;
            applyOp([&](BlockDocument &d) { return d.setBody(r, prefix + body); });
        }
        focus = {currentRow(), cursor + int(prefix.size())};
        // Quote/callout views edit the raw body.
        focus.cursor = std::clamp(focus.cursor, 0, int(m_model->editTextAt(focus.row).size()));
    } else if (cmd == u"table" || cmd == u"calc" || cmd == u"math" || cmd == u"query" || cmd == u"code"
               || cmd == u"rule") {
        int templateCursor = 0;
        const QString tpl = slashTemplate(cmd, m_english, &templateCursor);
        const int r = currentRow();
        if (cmd == u"rule") {
            const int nr = insertMarkdown(r, tpl); // nested: part of this undo step
            endCommand(Focus{-1, -1});
            stopEditing();
            out[u"row"_s] = nr;
            out[u"cursor"_s] = 0;
            return out;
        }
        int target = r;
        if (edit.trimmed().isEmpty()) {
            if (listItem)
                setEdit(tpl);
            else
                applyOp([&](BlockDocument &d) { return d.setBody(r, tpl); });
        } else {
            const EditResult ins = applyOp(
                    [&](BlockDocument &d) { return d.insertAfter(r, tpl, d.at(r).depth, listItem); });
            target = ins.focus >= 0 ? ins.focus : r;
        }
        const BlockShape shape = m_model->shapeAt(target);
        if (shape.view == BlockView::Table) {
            focus = {-1, -1};
            endCommand(focus);
            stopEditing();
            setSelectedRow(target);
            out[u"row"_s] = target;
            out[u"action"_s] = u"table"_s;
            return out;
        }
        focus = {target, cmd == u"query" ? int(shape.editText.size()) : 0};
    } else if (cmd == u"embed") {
        insertAt(u"![["_s);
        setEdit(edit);
        focus = {currentRow(), cursor};
        action = u"complete"_s;
    } else if (cmd == u"blockRef") {
        insertAt(u"(("_s);
        setEdit(edit);
        focus = {currentRow(), cursor};
        action = u"complete"_s;
    }
    endCommand(focus);
    if (focus.row >= 0)
        setFocus(focus, true);
    out[u"row"_s] = focus.row;
    out[u"cursor"_s] = m_cursor;
    out[u"action"_s] = action;
    return out;
}

// --- Tables ---------------------------------------------------------------------------------------------

void PageController::setTableCell(int row, int tableRow, int column, const QString &value, bool typing)
{
    if (!validRow(row) || column < 0)
        return;
    auto model = calc::parseTable(m_model->document().at(row).body);
    if (!model || column >= model->header.size())
        return;
    QString cell = value;
    cell.replace(u"\r\n"_s, u"\n"_s);
    if (tableRow < 0) {
        model->header[column] = cell;
    } else {
        if (tableRow >= model->rows.size())
            return;
        QStringList &r = model->rows[tableRow];
        while (r.size() <= column)
            r.append(QString());
        r[column] = cell;
    }
    const QString body = calc::serializeTable(*model);
    if (!typing) {
        editBody(row, body);
        return;
    }
    if (body == m_model->document().at(row).body)
        return;
    // Typing in a cell: one undo step per cell, like typing in a block.
    const quint64 uid = m_model->uidAt(row);
    const QPoint where(column, tableRow);
    const bool continueTyping = m_typingOpen && m_typingUid == uid && m_typingCell == where && m_undo->canUndo()
                                && !m_undo->canRedo();
    const BlockDocument::Version before = m_model->document().version();
    const Focus focus = currentFocus();
    const EditResult r = applyOp([&](BlockDocument &d) { return d.setBody(row, body); });
    if (!r.changed())
        return;
    auto *cmd = new EditCommand(this, m_model->document().diffFrom(before), focus, focus, uid);
    cmd->setMergeable(continueTyping);
    m_undo->push(cmd);
    m_typingOpen = true;
    m_typingUid = uid;
    m_typingCell = where;
    markDirty();
}

void PageController::cancelTableCell(int row, int tableRow, int column)
{
    if (!validRow(row))
        return;
    // The open typing session of exactly this cell is the step on top of the stack.
    if (m_typingOpen && m_typingUid == m_model->uidAt(row) && m_typingCell == QPoint(column, tableRow)
        && m_undo->canUndo() && !m_undo->canRedo())
        undo();
}

void PageController::addTableRow(int row)
{
    if (!validRow(row))
        return;
    auto model = calc::parseTable(m_model->document().at(row).body);
    if (!model)
        return;
    QStringList cells;
    for (qsizetype c = 0; c < model->header.size(); ++c)
        cells.append(QString());
    model->rows.append(cells);
    editBody(row, calc::serializeTable(*model));
}

void PageController::addTableColumn(int row)
{
    if (!validRow(row))
        return;
    auto model = calc::parseTable(m_model->document().at(row).body);
    if (!model)
        return;
    model->header.append((m_english ? u"Column %1"_s : u"Kolumna %1"_s).arg(model->header.size() + 1));
    model->aligns.append(calc::TableAlign::None);
    for (QStringList &r : model->rows)
        r.append(QString());
    editBody(row, calc::serializeTable(*model));
}

void PageController::removeTableRow(int row, int tableRow)
{
    if (!validRow(row))
        return;
    auto model = calc::parseTable(m_model->document().at(row).body);
    if (!model || tableRow < 0 || tableRow >= model->rows.size())
        return;
    model->rows.removeAt(tableRow);
    editBody(row, calc::serializeTable(*model));
}

// --- Links ------------------------------------------------------------------------------------------------

QVariantMap PageController::activateLink(int row, const QString &url, bool newPane)
{
    QVariantMap out;
    const qsizetype colon = url.indexOf(u':');
    if (colon <= 0)
        return out;
    const QString scheme = url.left(colon);
    const QString rest = url.mid(colon + 1);
    if (scheme == u"task") {
        if (validRow(row))
            toggleTask(row);
        return out;
    }
    if (scheme == u"reminder") {
        static const QRegularExpression range(u"^(\\d+)-(\\d+)$"_s);
        const auto m = range.match(rest);
        if (m.hasMatch() && validRow(row)) {
            const int offset = m_model->renderOffsetAt(row);
            out[u"action"_s] = u"reminder"_s;
            out[u"from"_s] = m.captured(1).toInt() + offset;
            out[u"to"_s] = m.captured(2).toInt() + offset;
        }
        return out;
    }
    if (scheme == u"date" || scheme == u"recur" || scheme == u"prio")
        return out; // a click edits the text; their menu changes them (dateChip())
    if (scheme == u"day") {
        // A link to a day of the journal: the page its target names.
        const QString page = dateChip(row, url).value(u"url"_s).toString();
        return page.isEmpty() ? out : activateLink(row, page, newPane);
    }
    if (scheme == u"ext") {
        if (m_host)
            m_host->openUrl(QUrl(rest));
        return out;
    }
    if (scheme == u"tag") {
        if (m_host)
            m_host->navigate({{u"view"_s, u"tag"_s}, {u"tag"_s, QUrl::fromPercentEncoding(rest.toUtf8())}}, newPane);
        return out;
    }
    const auto notFound = [](const QString &kind, const QString &name) {
        return QVariantMap{{u"action"_s, u"notFound"_s}, {u"kind"_s, kind}, {u"name"_s, name}};
    };
    const auto scrollTo = [](int target) { return QVariantMap{{u"action"_s, u"scrollTo"_s}, {u"row"_s, target}}; };
    // Right after opening the index may not know the block yet: complete it before saying the
    // block is nowhere.
    const auto nowhere = [this](const QString &id) {
        if (!m_host)
            return true;
        m_host->finishIndexing();
        return !m_host->blockIdTaken(id);
    };
    if (scheme == u"block") {
        if (const int found = rowOfBlockId(rest); found >= 0 && !newPane)
            return scrollTo(found);
        // A ((reference)) is the notebook's: no page has the id.
        if (m_host && nowhere(rest))
            return notFound(u"ref"_s, rest);
        if (m_host)
            m_host->navigate({{u"view"_s, u"page"_s}, {u"blockId"_s, rest}}, newPane);
        return out;
    }
    if (scheme == u"page") {
        QString target = rest;
        QString fragment;
        const qsizetype hash = rest.indexOf(u'#');
        if (hash >= 0) {
            target = rest.left(hash);
            fragment = QUrl::fromPercentEncoding(rest.mid(hash + 1).toUtf8());
        }
        target = QUrl::fromPercentEncoding(target.toUtf8());
        QVariantMap route{{u"view"_s, u"page"_s}};
        QString resolved = m_path;
        if (target.isEmpty()) {
            route[u"path"_s] = m_path;
        } else {
            resolved = m_host ? m_host->resolveLink(target, m_path) : QString();
            route[u"path"_s] = resolved;
            route[u"target"_s] = target;
        }
        // A block or heading of this page — `[[#^id]]`, or a link naming this page as a copied one
        // does — scrolls here every time: the route would not change, so the shell would ignore it.
        const bool here = !newPane && !m_path.isEmpty() && resolved == m_path;
        if (fragment.startsWith(u'^')) {
            const QString id = fragment.mid(1);
            route[u"blockId"_s] = id;
            if (here) {
                if (const int found = rowOfBlockId(id); found >= 0)
                    return scrollTo(found);
                // Moved to another page: the shell opens it there (Workspace::resolveRoute).
                if (nowhere(id))
                    return notFound(u"block"_s, id);
            }
        } else if (!fragment.isEmpty()) {
            route[u"heading"_s] = fragment;
            if (here) {
                const int found = rowOfHeading(fragment);
                return found >= 0 ? scrollTo(found) : notFound(u"heading"_s, fragment);
            }
        }
        if (m_host)
            m_host->navigate(route, newPane);
        return out;
    }
    return out;
}

QVariantMap PageController::linkMenu(const QString &url, const QString &fromPath) const
{
    const qsizetype colon = url.indexOf(u':');
    const QString scheme = colon > 0 ? url.left(colon) : QString();
    if (scheme != u"tag" && scheme != u"block" && scheme != u"ext" && scheme != u"page")
        return {};
    const VaultConfig config = vaultConfigFromMap(m_host ? m_host->vaultConfig() : QVariantMap());
    const notebook::CopiedLink copied = notebook::copiedLink(
            url, fromPath.isEmpty() ? m_path : fromPath, config,
            [this](const QString &target, const QString &from) {
                return m_host ? m_host->resolveLink(target, from) : QString();
            },
            [this](const VaultPath &p) { return m_host ? m_host->linkTarget(p, QString()) : QString(); });
    QVariantMap out{{u"kind"_s, scheme}, {u"copyText"_s, copied.text}, {u"unresolved"_s, false}};
    if (scheme == u"tag")
        out.insert(u"tag"_s, QUrl::fromPercentEncoding(url.mid(colon + 1).toUtf8()));
    if (scheme == u"page")
        out.insert({{u"unresolved"_s, copied.path.isEmpty()}, {u"path"_s, copied.path}});
    return out;
}

QVariantMap PageController::copyText(const QString &text)
{
    QVariantMap result{{u"ok"_s, !text.isEmpty()}, {u"text"_s, text}, {u"reason"_s, QString()}};
    if (text.isEmpty())
        return result;
    QClipboard *clipboard = qGuiApp ? QGuiApplication::clipboard() : nullptr;
    if (clipboard)
        clipboard->setText(text);
    // Read back: a platform without a clipboard (or one that refused) must not be told "copied".
    if (!clipboard || clipboard->text() != text) {
        result.insert(u"ok"_s, false);
        result.insert(u"reason"_s, u"noClipboard"_s);
    }
    return result;
}

void PageController::navigate(const QVariantMap &route, bool newPane)
{
    if (m_host && !route.isEmpty())
        m_host->navigate(route, newPane);
}

// --- Title, tasks elsewhere, locations ----------------------------------------------------------------

void PageController::updateTitleRow()
{
    int row = -1;
    if (zoomBlock() < 0 && !m_path.isEmpty()) {
        const BlockDocument &doc = m_model->document();
        int i = 0;
        while (i < doc.count() && doc.at(i).kind == BlockKind::Frontmatter)
            ++i;
        if (i < doc.count()) {
            const BlockShape shape = m_model->shapeAt(i);
            if (shape.view == BlockView::Heading && shape.headingLevel == 1
                && text::foldKey(plainTextOf(doc.at(i), 400)) == text::foldKey(title().trimmed()))
                row = i;
        }
    }
    if (row == m_titleRow)
        return;
    m_titleRow = row;
    Q_EMIT titleRowChanged();
}

bool PageController::queriesAvailable() const
{
    return qobject_cast<QueryHost *>(m_host.data()) != nullptr;
}

QString PageController::toggledStatus(const QString &status)
{
    if (status == u"done" || status == u"cancelled")
        return u"todo"_s;
    if (status.isEmpty())
        return {};
    return u"done"_s;
}

bool PageController::setTaskStatusAt(const QString &path, int line, const QString &status)
{
    if (path.isEmpty() || line < 0 || (!status.isEmpty() && !taskStatusFromName(status)))
        return false;
    if (path == m_path) {
        const int row = rowOfLine(line);
        if (!validRow(row) || m_model->document().at(row).kind != BlockKind::ListItem)
            return false;
        const BlockShape shape = m_model->shapeAt(row);
        if (!shape.task || taskStatusName(shape.task) == status)
            return false;
        setTaskStatus(row, status);
        return true;
    }
    auto *queries = qobject_cast<QueryHost *>(m_host.data());
    return queries && queries->setTaskStatus(path, line, status);
}

bool PageController::toggleTaskAt(const QString &path, int line, const QString &currentStatus)
{
    const QString next = toggledStatus(currentStatus);
    return !next.isEmpty() && setTaskStatusAt(path, line, next);
}

QVariantMap PageController::openLocation(const QString &path, int line, bool newPane)
{
    if (path.isEmpty())
        return {};
    if (path == m_path && !newPane) {
        const int row = line >= 0 ? rowOfLine(line) : -1;
        if (row >= 0)
            return {{u"action"_s, u"scrollTo"_s}, {u"row"_s, row}};
    }
    if (m_host) {
        QVariantMap route{{u"view"_s, u"page"_s}, {u"path"_s, path}};
        if (line >= 0)
            route.insert(u"line"_s, line);
        m_host->navigate(route, newPane);
    }
    return {};
}

// --- Attachments -------------------------------------------------------------------------------------------

namespace {

/// Largest file stored as an attachment (pictures, PDFs; not whole videos by mistake).
constexpr qint64 MaxAttachmentBytes = 200LL * 1024 * 1024;
/// The pictures of one browser pick in all (pickImageInBrowser()): every one stays in the
/// browser's memory until all are read, and stored attachments live there too.
[[maybe_unused]] constexpr qint64 MaxPickedAttachmentBytes = 512LL * 1024 * 1024;

/// Local files of a copy or drop (file:// URLs; Android content:// documents).
bool isFileUrl(const QUrl &url)
{
    return url.isLocalFile() || url.scheme() == u"content";
}

/// What QFile opens: a local path, or the URL itself for content:// documents.
QString fileSystemPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

/// A copied picture is pasted as a picture unless real text came with it (a browser's "Copy
/// image" adds the image's address, nothing to type into a note).
bool imageWithoutText(const QMimeData *data)
{
    if (!data || !data->hasImage())
        return false;
    const QString text = data->text().trimmed();
    if (text.isEmpty())
        return true;
    static const QRegularExpression address(u"^(https?|file|data):\\S*$"_s);
    return address.match(text).hasMatch();
}

QList<QUrl> copiedFiles(const QMimeData *data)
{
    QList<QUrl> out;
    if (!data || !data->hasUrls())
        return out;
    for (const QUrl &url : data->urls()) {
        if (!isFileUrl(url) || (url.isLocalFile() && !QFileInfo(url.toLocalFile()).isFile()))
            return {};
        out.append(url);
    }
    return out;
}

} // namespace

QStringList PageController::imageNameFilters() const
{
    // The pictures the editor embeds as pictures (one list: paths::pictureExtensions()).
    QStringList patterns;
    for (const QString &ext : paths::pictureExtensions())
        patterns.append(u'*' + ext);
    //: %1: the file name patterns of pictures, e.g. "*.png *.jpg *.jpeg"
    return {tr("Pictures (%1)").arg(patterns.join(u' ')), tr("All files (*)")};
}

bool PageController::clipboardHasImage() const
{
    const QClipboard *clipboard = qGuiApp ? QGuiApplication::clipboard() : nullptr;
    const QMimeData *data = clipboard ? clipboard->mimeData() : nullptr;
    return imageWithoutText(data) || !copiedFiles(data).isEmpty();
}

QString PageController::embedFor(const QString &path) const
{
    const QString name = paths::basename(path);
    const bool byName = !name.isEmpty() && (!m_host || m_host->resolveLink(name, m_path) == path);
    return u"![["_s + (byName ? name : path) + u"]]"_s;
}

QVariantMap PageController::insertEmbeds(int row, int from, int to, const QStringList &stored, bool newBlock)
{
    QStringList embeds;
    for (const QString &path : stored)
        embeds.append(embedFor(path));
    if (embeds.isEmpty())
        return {{u"row"_s, -1}, {u"cursor"_s, -1}};
    return insertEmbedText(row, from, to, embeds.join(u' '), newBlock);
}

QVariantMap PageController::insertEmbedText(int row, int from, int to, const QString &inserted, bool newBlock)
{
    const BlockView view = validRow(row) ? m_model->shapeAt(row).view : BlockView::Rule;
    if (!newBlock && (view == BlockView::Text || view == BlockView::Heading)) {
        const QString edit = m_model->editTextAt(row);
        from = snapToCharacter(edit, from < 0 ? int(edit.size()) : from);
        to = std::max(from, snapToCharacter(edit, to < 0 ? from : to));
        const QString before = from > 0 && !edit[from - 1].isSpace() ? u" "_s : QString();
        const QString after = to < edit.size() && !edit[to].isSpace() ? u" "_s : QString();
        const int cursor = replaceRange(row, from, to, before + inserted + after) - int(after.size());
        if (editingRow() == row)
            setFocus({row, cursor}, true);
        return {{u"row"_s, row}, {u"cursor"_s, cursor}};
    }
    // Not into text: a block of its own after the row, or at the end of the page.
    beginCommand();
    int target = -1;
    if (validRow(row)) {
        target = insertBlockAfter(row, inserted);
    } else {
        target = appendEmptyBlock();
        replaceRange(target, 0, int(m_model->editTextAt(target).size()), inserted);
    }
    endCommand(Focus{-1, -1});
    stopEditing();
    return {{u"row"_s, target}, {u"cursor"_s, -1}};
}

bool PageController::isWebPictureUrl(const QString &url) const
{
    return webpicture::isWebUrl(url);
}

QVariantMap PageController::insertWebPicture(int row, int from, int to, const QString &url, bool download)
{
    return placeWebPicture(row, from, to, url, download, false);
}

QVariantMap PageController::placeWebPicture(int row, int from, int to, const QString &url, bool download, bool inPlace)
{
    const QString target = url.trimmed();
    if (!webpicture::isWebUrl(target))
        return {{u"row"_s, -1}, {u"cursor"_s, -1}};
    // A destination with blanks or parentheses goes between angle brackets (CommonMark).
    const bool angle = target.contains(u' ') || target.contains(u'(') || target.contains(u')');
    QString dest = target;
    if (angle)
        dest = u'<' + dest.replace(u'<', u"%3C"_s).replace(u'>', u"%3E"_s) + u'>';
    const QString picture = u"![]("_s + dest + u')';
    QVariantMap out;
    if (inPlace && validRow(row) && showsEmbeds(m_model->shapeAt(row).view)) {
        // The address is in the text already (a quote or callout too): it becomes the picture there.
        const int cursor = replaceRange(row, from, to, picture);
        if (editingRow() == row)
            setFocus({row, cursor}, true);
        out = {{u"row"_s, row}, {u"cursor"_s, cursor}};
    } else {
        out = insertEmbedText(row, from, to, picture, false);
    }
    if (download && out.value(u"row"_s).toInt() >= 0) {
        for (const QString &onPage : webPictureUrls()) {
            if (webpicture::webUrl(onPage) == webpicture::webUrl(target)) {
                downloadWebPictures(onPage);
                break;
            }
        }
    }
    return out;
}

QVariantMap PageController::urlToPicture(int row, int from, int to, const QString &url, bool download)
{
    const QVariantMap none{{u"row"_s, -1}, {u"cursor"_s, -1}};
    if (!validRow(row) || !webpicture::isWebUrl(url) || from < 0 || to < from)
        return none;
    const QString edit = m_model->editTextAt(row);
    if (to > edit.size() || QStringView(edit).sliced(from, to - from) != url)
        return none;
    return placeWebPicture(row, from, to, url, download, true);
}

bool PageController::copyWebPictureUrl(const QString &url)
{
    QClipboard *clipboard = qGuiApp ? QGuiApplication::clipboard() : nullptr;
    if (!clipboard || url.isEmpty())
        return false;
    clipboard->setText(url);
    return clipboard->text() == url; // a platform without a clipboard must not be told "copied"
}

void PageController::showWebPicture(const QString &url)
{
    if (m_host && webpicture::isWebUrl(url))
        m_host->loadWebPicture(url);
}

QStringList PageController::webPictureUrls() const
{
    QStringList urls;
    const markdown::BlockDocument &doc = m_model->document();
    for (int i = 0; i < doc.count(); ++i) {
        const QString &body = doc.at(i).body;
        if (!body.contains(u"![") || !showsEmbeds(shapeOf(doc.at(i)).view))
            continue;
        for (const Embed &em : embedsOf(body))
            if (em.web && !urls.contains(em.target))
                urls.append(em.target);
    }
    return urls;
}

int PageController::downloadWebPictures(const QString &url)
{
    if (m_sourceMode)
        return -1; // the blocks are not what is saved: the caller says "switch back to blocks"
    if (!m_host)
        return 0;
    const QStringList onPage = webPictureUrls();
    QStringList wanted;
    if (url.isEmpty())
        wanted = onPage;
    else if (onPage.contains(url))
        wanted = {url};
    for (const QString &u : std::as_const(wanted)) {
        if (!m_webDownloads.contains(u))
            m_webDownloads.append(u);
    }
    // Being stored: the new ones and those already on their way (a picture's "Zapisz w
    // notatniku" chosen while it was fetched) — counted before the cached ones finish below.
    const int pending = int(wanted.size());
    // Cached ones are stored at once, the rest once fetched (onWebPictureChanged()).
    for (const QString &u : std::as_const(wanted)) {
        QString format;
        if (!m_webDownloads.contains(u))
            continue;
        if (!m_host->webPictureData(u, &format).isEmpty())
            finishWebDownload(u, true);
        else
            m_host->loadWebPicture(u, true);
    }
    return pending;
}

void PageController::onWebPictureChanged(const QString &url)
{
    m_model->webPictureChanged(url);
    if (!m_host)
        return;
    const QStringList pending = m_webDownloads;
    for (const QString &u : pending) {
        if (!url.isEmpty() && u != url)
            continue;
        const QString state = m_host->webPicture(u).value(u"state"_s).toString();
        if (state == u"ready" || state == u"failed")
            finishWebDownload(u, state == u"ready");
    }
}

void PageController::finishWebDownload(const QString &url, bool ready)
{
    if (!m_webDownloads.removeOne(url))
        return;
    const QUrl web = webpicture::webUrl(url);
    QString format;
    const QByteArray data = ready && m_host ? m_host->webPictureData(url, &format) : QByteArray();
    bool stored = false;
    // The blocks that would point at the attachment, found before it is saved: a picture an edit
    // or a reload removed while it was fetched, or one no block can point elsewhere, leaves no
    // file behind. In source mode the blocks are not what is saved (m_sourceText, which a
    // whole-page retarget could change inside code): the picture is not stored.
    const auto retargeted = [this, &url](const QString &path) {
        QList<std::pair<int, QString>> out;
        const markdown::BlockDocument &doc = m_model->document();
        for (int i = 0; i < doc.count(); ++i) {
            const QString &body = doc.at(i).body;
            if (!body.contains(u"![") || !showsEmbeds(shapeOf(doc.at(i)).view))
                continue;
            const QString changed = retargetWebPicture(body, url, path);
            if (changed != body)
                out.append({i, changed});
        }
        return out;
    };
    const QString name = format.isEmpty() ? QString() : webpicture::attachmentName(web, format);
    if (!data.isEmpty() && !name.isEmpty() && !m_sourceMode && !retargeted(name).isEmpty()) {
        const QString path = m_host->saveAttachment(name, data);
        const QList<std::pair<int, QString>> edits =
                path.isEmpty() ? QList<std::pair<int, QString>>{} : retargeted(path);
        if (!edits.isEmpty()) {
            beginCommand();
            for (const auto &[i, body] : edits)
                editBody(i, body);
            endCommand(currentFocus());
            stored = true;
        }
    }
    if (stored)
        ++m_webStored;
    else
        m_webFailed.append(webpicture::displayName(web));
    if (!m_webDownloads.isEmpty())
        return;
    const int count = m_webStored;
    const QStringList failed = m_webFailed;
    m_webStored = 0;
    m_webFailed.clear();
    Q_EMIT webPicturesDownloaded(count, failed);
}

QVariantMap PageController::insertAttachment(int row, int from, int to, const QString &fileName, const QByteArray &data)
{
    const QString name = fileName.trimmed().isEmpty() ? tr("Attachment") : fileName.trimmed();
    const QString path = m_host && !data.isEmpty() ? m_host->saveAttachment(name, data) : QString();
    if (path.isEmpty()) {
        Q_EMIT attachmentFailed(name);
        return {{u"row"_s, -1}, {u"cursor"_s, -1}};
    }
    return insertEmbeds(row, from, to, {path});
}

QVariantMap PageController::insertDrawing(int row, int from, int to)
{
    const QString name = tr("Drawing %1").arg(currentDateTime().toString(u"yyyy-MM-dd HH.mm"_s)) + u".svg"_s;
    const QString path = m_host ? m_host->saveAttachment(name, drawing::toSvg(drawing::Drawing())) : QString();
    if (path.isEmpty()) {
        Q_EMIT attachmentFailed(name);
        return {{u"row"_s, -1}, {u"cursor"_s, -1}};
    }
    QVariantMap out = insertEmbeds(row, from, to, {path});
    out.insert(u"path"_s, path);
    return out;
}

void PageController::openDrawing(const QString &path)
{
    if (!m_host || path.isEmpty())
        return;
    flush();
    m_host->navigate({{u"view"_s, u"drawing"_s}, {u"path"_s, path}});
}

QVariantMap PageController::insertFiles(int row, int from, int to, const QList<QUrl> &urls)
{
    const QStringList stored = storeFiles(urls);
    QVariantMap out = insertEmbeds(row, from, to, stored);
    out.insert(u"count"_s, stored.size());
    return out;
}

QVariantMap PageController::insertFilesAfter(int row, const QList<QUrl> &urls)
{
    const QStringList stored = storeFiles(urls);
    QVariantMap out = insertEmbeds(validRow(row) ? row : -1, -1, -1, stored, true);
    out.insert(u"count"_s, stored.size());
    return out;
}

QStringList PageController::storeFiles(const QList<QUrl> &urls)
{
    QStringList stored;
    for (const QUrl &url : urls) {
        if (!isFileUrl(url))
            continue;
        const QString fsPath = fileSystemPath(url);
        QString name = QFileInfo(fsPath).fileName();
        if (name.isEmpty())
            name = url.fileName();
        QFile file(fsPath);
        if (!file.open(QIODevice::ReadOnly) || file.size() > MaxAttachmentBytes) {
            Q_EMIT attachmentFailed(name);
            continue;
        }
        const QByteArray data = file.readAll();
        const QString path = m_host && !data.isEmpty() ? m_host->saveAttachment(name, data) : QString();
        if (path.isEmpty()) {
            Q_EMIT attachmentFailed(name);
            continue;
        }
        stored.append(path);
    }
    return stored;
}

QVariantMap PageController::pasteImage(int row, int from, int to)
{
    const QClipboard *clipboard = qGuiApp ? QGuiApplication::clipboard() : nullptr;
    const QMimeData *data = clipboard ? clipboard->mimeData() : nullptr;
    if (const QList<QUrl> files = copiedFiles(data); !files.isEmpty())
        return insertFiles(row, from, to, files);
    if (!imageWithoutText(data))
        return {{u"row"_s, -1}, {u"cursor"_s, -1}};
    const QImage image = qvariant_cast<QImage>(data->imageData());
    QByteArray png;
    QBuffer buffer(&png);
    if (image.isNull() || !buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        return {{u"row"_s, -1}, {u"cursor"_s, -1}};
    const QString name = tr("Pasted image %1").arg(currentDateTime().toString(u"yyyyMMddHHmmss"_s)) + u".png"_s;
    return insertAttachment(row, from, to, name, png);
}

bool PageController::pickImageInBrowser(int row, int from, int to)
{
#ifdef Q_OS_WASM
    // Several pictures at once, as insertFiles() on the desktop; the same size limit per picture
    // and a total for the pick, both checked before a picture is read.
    platform::wasm::openFiles(imageNameFilters(), true, MaxAttachmentBytes, MaxPickedAttachmentBytes,
                              pictureReceiver(row, from, to));
    return true;
#else
    Q_UNUSED(row);
    Q_UNUSED(from);
    Q_UNUSED(to);
    return false;
#endif
}

std::function<void(const QList<platform::PickedFile> &)> PageController::pictureReceiver(int row, int from, int to)
{
    const int handle = trackBlock(row); // the block may move or go while the picker is open
    const QPointer<PageController> self(this);
    return [self, handle, from, to](const QList<platform::PickedFile> &picked) {
        if (!self)
            return;
        const int current = self->trackedRow(handle);
        self->untrack(handle);
        if (picked.isEmpty())
            return; // cancelled
        QStringList stored;
        for (const platform::PickedFile &file : picked) {
            QString name = QFileInfo(file.name).fileName().trimmed();
            if (name.isEmpty())
                name = tr("Attachment");
            // As storeFiles() on the desktop: one too large (or not read: over the pick's total),
            // empty or not stored is refused; the others still come.
            const QString path = self->m_host && file.isRead() && !file.data.isEmpty()
                                         ? self->m_host->saveAttachment(name, file.data)
                                         : QString();
            if (path.isEmpty()) {
                Q_EMIT self->attachmentFailed(name);
                continue;
            }
            stored.append(path);
        }
        if (!stored.isEmpty())
            self->insertEmbeds(current, current >= 0 ? from : -1, current >= 0 ? to : -1, stored);
    };
}

// --- Undo -----------------------------------------------------------------------------------------------

void PageController::undo()
{
    if (m_sourceMode)
        return;
    m_typingOpen = false;
    m_undo->undo();
}

QString PageController::undoToken() const
{
    // The history's generation, the position in it and the step there: after an undo, a new
    // edit takes the same position but is another step.
    const int index = m_undo->index();
    const auto *last = index > 0 ? static_cast<const EditCommand *>(m_undo->command(index - 1)) : nullptr;
    return u"%1:%2:%3"_s.arg(m_undoGeneration).arg(index).arg(last ? last->serial() : 0);
}

bool PageController::undoStep(const QString &token)
{
    // Only while the edit is the last step of this history: undoing it later would undo the
    // edits made after it first (or, after a reload, an unrelated step).
    if (m_sourceMode || token.isEmpty() || token != undoToken() || !m_undo->canUndo())
        return false;
    undo();
    return true;
}

void PageController::redo()
{
    if (m_sourceMode)
        return;
    m_typingOpen = false;
    m_undo->redo();
}

// --- Source mode ------------------------------------------------------------------------------------------

QString PageController::sourceText() const
{
    // Outside source mode: the text as it is saved (the merge dialog's "this device").
    return m_sourceMode ? m_sourceText : textToSave();
}

void PageController::setSourceText(const QString &edited)
{
    // A TextEdit reports every line break as "\n": text without any "\r" gets the file's line
    // terminators back (untouched lines keep theirs, new ones get the document's).
    const QString current = m_sourceMode ? m_sourceText : text();
    const QString sourceText = edited.contains(u'\r')
            ? edited
            : restoreLineEndings(current, edited, m_model->document().eol());
    if (!m_sourceMode) {
        // Outside source mode this replaces the document as one undo step.
        applyText(sourceText, currentFocus());
        Q_EMIT sourceTextChanged();
        return;
    }
    if (m_sourceText == sourceText)
        return;
    m_sourceText = sourceText;
    m_sourceEdited = true;
    markDirty();
    Q_EMIT sourceTextChanged();
}

void PageController::setSourceMode(bool on)
{
    if (m_sourceMode == on)
        return;
    if (on) {
        stopEditing();
        m_sourceText = text();
        m_sourceBefore = m_sourceText;
        m_sourceEdited = false;
        m_sourceMode = true;
    } else {
        m_sourceMode = false;
        const QString edited = m_sourceText;
        m_sourceText.clear();
        if (m_sourceEdited && edited != text())
            applyText(edited, Focus{-1, -1});
        markDirty();
    }
    Q_EMIT sourceModeChanged();
    Q_EMIT sourceTextChanged();
}

} // namespace brulion::editor
