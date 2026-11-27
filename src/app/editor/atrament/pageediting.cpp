// PageView's editing (docs/ATRAMENT.md "The block editor in Atrament (M2)", step 2 "Editing text
// rows"): the controller's editing row shown as a TextInput over its `editText`, the editor's keys
// as PageController calls, clicks and ↑/↓ placing the caret through the rendered text's
// display ↔ source map. The QML editor's counterparts: InlineEditor.qml (typing, line breaks from
// input methods, pasted Markdown) and BlockEditor.handleEditorKeyCode (the keys).
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "editstyles.h"
#include "pagecontroller.h"
#include "renderedtextitem.h"

#include <QVariantMap>

#include <algorithm>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

namespace {

QString toQString(std::u16string_view s)
{
    return QString(reinterpret_cast<const QChar *>(s.data()), qsizetype(s.size()));
}

std::u16string toU16(const QString &s)
{
    return std::u16string(u16View(s));
}

// The changed range of `before` → `after`: [start, endAfter) of after replaced [start, endBefore)
// of before (EditorInput::changedRange).
struct ChangedRange {
    int start = 0;
    int endAfter = 0;
    int endBefore = 0;
};

ChangedRange changedRange(std::u16string_view before, std::u16string_view after)
{
    const int a = int(before.size());
    const int b = int(after.size());
    int start = 0;
    while (start < a && start < b && before[std::size_t(start)] == after[std::size_t(start)])
        ++start;
    int tail = 0;
    while (tail < a - start && tail < b - start
           && before[std::size_t(a - 1 - tail)] == after[std::size_t(b - 1 - tail)])
        ++tail;
    return {start, b - tail, a - tail};
}

bool plainKey(const at::Keystroke &k, std::u16string_view name)
{
    return k.key == name && !k.modifiers.any();
}

} // namespace

bool PageView::editableView(const QString &view, bool drawableText) const
{
    return drawableText && (view == u"text" || view == u"heading" || view == u"quote" || view == u"callout");
}

bool PageView::editsRow(int row) const
{
    if (!m_model || !m_controller || row < 0 || row >= m_model->rowCount())
        return false;
    if (isPanel(row) || editsSource(row)) // code, a calculation, a rule; a diagram's or a formula's source
        return true;
    if (const std::optional<bool> media = drawsMediaRow(row); media && !*media)
        return false; // its pictures, embeds or query drawn by the QML delegate: its row to edit
    const QModelIndex index = m_model->index(row, 0);
    const auto text = index.data(BlockListModel::RenderedTextRole).value<RenderedTextRef>();
    return editableView(index.data(BlockListModel::ViewRole).toString(), text.drawable());
}

PageController *PageView::controller() const
{
    return m_controller.data();
}

int PageView::editedRow() const
{
    if (!m_model || !m_editUid)
        return -1;
    return m_model->rowOfUid(m_editUid);
}

void PageView::setController(PageController *controller, at::Context<PageView> &cx)
{
    for (const QMetaObject::Connection &c : m_controllerConnections)
        QObject::disconnect(c);
    m_controllerConnections.clear();
    m_controller = controller;
    m_editUid = 0;
    m_app = &cx.app();
    connectTables(cx); // (tablerows.cpp)
    if (!controller)
        return;
    at::App *app = &cx.app();
    const at::EntityId self = cx.entityId();
    m_controllerConnections.push_back(
            QObject::connect(controller, &PageController::editingRowChanged, controller, [app, self] {
                app->updateEntity<PageView>(
                        self, [](PageView &v, at::Context<PageView> &c) { v.controllerEditingChanged(c); });
            }));
    m_controllerConnections.push_back(QObject::connect(
            controller, &PageController::editTextReset, controller, [app, self](int row, const QString &text, int cursor) {
                app->updateEntity<PageView>(self, [&](PageView &v, at::Context<PageView> &c) {
                    v.controllerTextReset(row, text, cursor, c);
                });
            }));
    // Another page opens at its top, wherever the last one was scrolled.
    m_controllerConnections.push_back(QObject::connect(controller, &PageController::pathChanged, controller, [app, self] {
        app->updateEntity<PageView>(self, [](PageView &v, at::Context<PageView> &c) {
            v.m_handle.scrollToTop();
            c.notify();
        });
    }));
    // A zoom's change brings back (or hides) the rows around the edited one: it stays in view.
    m_controllerConnections.push_back(QObject::connect(controller, &PageController::zoomChanged, controller, [app, self] {
        app->updateEntity<PageView>(self, [](PageView &v, at::Context<PageView> &c) {
            if (v.editedRow() < 0)
                return;
            v.m_revealEdited = true;
            c.notify();
        });
    }));
    // The reminder chips' words are in the UI's language, relative to today.
    auto restyle = [app, self] {
        app->updateEntity<PageView>(self, [](PageView &v, at::Context<PageView> &c) { v.restyleInput(c); });
    };
    m_controllerConnections.push_back(QObject::connect(controller, &PageController::localeChanged, controller, restyle));
    m_controllerConnections.push_back(QObject::connect(controller, &PageController::todayChanged, controller, restyle));
    controllerEditingChanged(cx);
}

void PageView::setEditStyler(at::TextInput::Styler styler, at::Context<PageView> &cx)
{
    m_editStyler = std::move(styler);
    applyEditStyler(cx);
}

void PageView::applyEditStyler(at::Context<PageView> &cx)
{
    if (!m_input)
        return;
    // What the field shows: a code block's code or a text block's Markdown.
    const int row = editedRow();
    const bool code = row >= 0 && editsAsCode(row);
    const bool calc = row >= 0 && isCalc(row);
    const QString lang = code ? m_model->index(row, 0).data(BlockListModel::LangRole).toString() : QString();
    m_inputCode = code || calc;
    m_inputLanguage = calc ? QStringLiteral("calc") : lang;
    // The highlighter's spans (EditStyler's rules for the row's kind, else the plain styler), the
    // find bar's matches, the misspelled words and the reminder chips over them (editSpans()); a
    // calculation's by the code rules (its comments) in the note's face (CalcBlockView's
    // highlightMode "code").
    at::TextInput::Styler styler;
    const bool luau = lang == u"luau" || lang == u"lua";
    const bool asCode = code || calc;
    if (m_styles || (m_editStyler && !asCode) || !m_find.query.isEmpty() || (!asCode && (editSpellActive() || m_reminderChips)))
        styler = [this, asCode, luau, calc](std::u16string_view text) {
            std::vector<at::TextSpan> spans = editSpans(text, asCode, luau);
            if (calc)
                for (at::TextSpan &span : spans)
                    span.style.fontFamily.reset();
            return spans;
        };
    m_input->update(cx.app(), [&styler](at::TextInput &t, at::Context<at::TextInput> &c) { t.setStyler(styler, c); });
}

std::vector<at::TextSpan> PageView::editSpans(std::u16string_view text, bool code, bool luau)
{
    std::vector<at::TextSpan> spans;
    EditStyler::Extras extras;
    extras.chips = !code && m_reminderChips;
    if (m_controller) {
        extras.chipLocale = m_controller->locale() == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish;
        extras.chipToday = m_model ? m_model->today() : QDate();
    }
    const bool spelled = !code && editSpellActive();
    const bool wantsExtras = spelled || extras.chips;
    const MarkdownMode mode = code ? MarkdownMode::Code : MarkdownMode::Inline;
    if (m_styles) {
        spans = m_styles->spans(text, mode, luau, wantsExtras ? &extras : nullptr);
    } else {
        if (m_editStyler && !code)
            spans = m_editStyler(text);
        if (wantsExtras) {
            // The lines' prose and chips by the same rules, without a palette's colours.
            static const EditStyler plain(QVariantMap{}, std::u16string());
            (void)plain.spans(text, mode, luau, &extras);
        }
    }
    appendFindSpans(QStringView(text.data(), qsizetype(text.size())), spans);
    m_editSpell.wrong.clear();
    m_editSpell.suppressed = -1;
    if (spelled)
        appendEditSpelling(text, extras.prose, spans);
    m_editChips.clear();
    m_editChipsText.assign(text);
    if (extras.chips)
        appendEditChips(extras.chipsFound, spans);
    return spans;
}

void PageView::restyleInput(at::Context<PageView> &cx)
{
    if (m_input)
        m_input->update(cx.app(), [](at::TextInput &t, at::Context<at::TextInput> &c) { t.restyle(c); });
    cx.notify();
}

bool PageView::insertAtCaret(std::u16string text, bool separate, at::Context<PageView> &cx)
{
    if (!m_input || editedRow() < 0)
        return false;
    commitComposition(cx);
    const at::TextInput &input = m_input->read(cx.app());
    const at::Range sel = input.selection().range();
    if (separate && sel.start > 0) {
        const char16_t before = input.text()[std::size_t(sel.start - 1)];
        if (before != u' ' && before != u'\n')
            text.insert(text.begin(), u' ');
    }
    m_input->update(cx.app(), [&](at::TextInput &t, at::Context<at::TextInput> &c) { t.replace(sel, text, c); });
    return true;
}

bool PageView::selectInEdited(int row, int from, int to, at::Context<PageView> &cx)
{
    if (!m_input || row < 0 || editedRow() != row)
        return false;
    m_input->update(cx.app(), [&](at::TextInput &t, at::Context<at::TextInput> &c) { t.setSelection(from, to, c); });
    return true;
}

void PageView::setEditLook(at::Color caret, at::Color selection, std::u16string placeholder, std::u16string label,
                           at::Context<PageView> &cx)
{
    m_editCaret = caret;
    m_editSelection = selection;
    m_editPlaceholder = std::move(placeholder);
    if (!label.empty())
        m_editLabel = std::move(label);
    if (m_input)
        m_input->update(cx.app(), [this](at::TextInput &t, at::Context<at::TextInput> &c) {
            t.setColors(m_editCaret, m_editSelection, c);
            t.setLabel(m_editLabel, c);
        });
    cx.notify();
}

void PageView::controllerEditingChanged(at::Context<PageView> &cx)
{
    const int row = m_controller ? m_controller->editingRow() : -1;
    if (row < 0 || !editsRow(row)) {
        // Not editing, or a row the QML delegate edits (code, a table…).
        if (m_editUid) {
            m_editUid = 0;
            cx.notify();
        }
        return;
    }
    const quint64 uid = m_model->uidAt(row);
    const bool started = uid != m_editUid;
    loadInput(row, m_controller->cursorPosition(), cx);
    m_focusInput = true;
    m_revealEdited = true;
    cx.notify();
    if (started && onEditingStarted)
        onEditingStarted();
}

void PageView::loadInput(int row, int cursor, at::Context<PageView> &cx)
{
    if (!m_input) {
        at::TextInputOptions options;
        options.multiline = true; // the block's soft line breaks (Shift+Enter) and its wrapping
        options.caretColor = m_editCaret;
        options.selectionColor = m_editSelection;
        options.placeholder = m_editPlaceholder;
        options.label = m_editLabel;
        m_input = cx.newEntity<at::TextInput>(std::move(options));
        m_inputSubscriptions.push_back(cx.subscribe<at::TextInput::Changed>(
                *m_input, [](PageView &v, const at::TextInput::Changed &, at::Context<PageView> &c) { v.inputChanged(c); }));
        m_inputSubscriptions.push_back(cx.subscribe<at::TextInput::SelectionChanged>(
                *m_input, [](PageView &v, const at::TextInput::SelectionChanged &, at::Context<PageView> &c) {
                    v.inputSelectionChanged(c);
                }));
        applyEditStyler(cx);
    }
    m_editUid = m_model->uidAt(row);
    m_editSpell = {};
    {
        const bool code = editsAsCode(row);
        const bool calc = isCalc(row);
        const QString lang = calc ? QStringLiteral("calc")
                                  : code ? m_model->index(row, 0).data(BlockListModel::LangRole).toString() : QString();
        if (code + calc != int(m_inputCode) || lang != m_inputLanguage)
            applyEditStyler(cx);
    }
    const QString text = m_model->editTextAt(row);
    m_lastText = toU16(text);
    m_softBreak = false;
    const std::u16string value = m_lastText;
    // "Type / for commands" in an empty paragraph or list item (as BlockText's), none in a heading.
    const QModelIndex index = m_model->index(row, 0);
    const bool prompts = index.data(BlockListModel::ViewRole).toString() == u"text";
    const std::u16string placeholder = prompts ? m_editPlaceholder : std::u16string();
    const int caret = cursor < 0 ? int(value.size()) : std::clamp(cursor, 0, int(value.size()));
    m_editSpell.caret = caret;
    m_input->update(cx.app(), [&](at::TextInput &t, at::Context<at::TextInput> &c) {
        t.setPlaceholder(placeholder, c);
        if (t.text() != value)
            t.setText(value, c);
        t.setSelection(caret, caret, c);
        // The page's undo stack is the controller's: the field keeps none of its own.
        t.clearUndo();
        t.restyle(c); // (the spelling and the chips follow the caret and the day, not only the text)
    });
    // Vim's mode (the caret's shape, no typing in its commands) and its visual selection here.
    applyVimLook(cx);
    if (m_vimSelection.row == row)
        vimSelectionRequested(m_vimSelection.row, m_vimSelection.anchor, m_vimSelection.cursor, cx);
}

void PageView::controllerTextReset(int row, const QString &text, int cursor, at::Context<PageView> &cx)
{
    if (!m_input || !m_editUid || editedRow() != row)
        return;
    const std::u16string value = toU16(text);
    m_lastText = value;
    const int caret = cursor < 0 ? int(value.size()) : std::clamp(cursor, 0, int(value.size()));
    m_editSpell.caret = caret;
    m_input->update(cx.app(), [&](at::TextInput &t, at::Context<at::TextInput> &c) {
        if (t.text() != value)
            t.setText(value, c);
        t.setSelection(caret, caret, c);
        t.clearUndo();
    });
    cx.notify();
}

void PageView::inputChanged(at::Context<PageView> &cx)
{
    const int row = editedRow();
    if (!m_input || row < 0 || !m_controller)
        return;
    const at::TextInput &input = m_input->read(cx.app());
    // A word the input method is composing is the field's until it is committed (Qt's fields
    // keep the preedit out of their text too): the controller's input rules and normalisation
    // never see half a word.
    if (input.markedRange()) {
        if (onEditStateChanged)
            onEditStateChanged();
        return;
    }
    const std::u16string text = input.text();
    // The text the controller gave (or was given) already: what setText() caused.
    if (text == m_lastText)
        return;
    const int caret = input.selection().focus;
    const QString view = m_model->index(row, 0).data(BlockListModel::ViewRole).toString();
    const bool multiline = view == u"quote" || view == u"callout" || view == u"code" || view == u"calc" || editsSource(row);
    if (!multiline && !m_softBreak) {
        // Line breaks that did not come from Shift+Enter: an input method's Enter (Android commits
        // "\n" as text) splits the block, pasted Markdown becomes blocks.
        const ChangedRange r = changedRange(m_lastText, text);
        const std::u16string_view inserted = std::u16string_view(text).substr(std::size_t(r.start),
                                                                              std::size_t(r.endAfter - r.start));
        if (inserted.find(u'\n') != std::u16string_view::npos) {
            const std::u16string before = m_lastText;
            m_input->update(cx.app(), [&](at::TextInput &t, at::Context<at::TextInput> &c) {
                t.setText(before, c);
                t.setSelection(r.start, r.start, c);
            });
            if (inserted == u"\n")
                m_controller->split(row, r.start);
            else
                m_controller->pasteText(row, r.start, r.endBefore, toQString(inserted));
            return;
        }
    }
    m_softBreak = false;
    m_lastText = text;
    m_controller->setEditText(row, toQString(text), caret);
    if (onEditStateChanged)
        onEditStateChanged();
}

void PageView::inputSelectionChanged(at::Context<PageView> &cx)
{
    if (!m_input || !m_controller || editedRow() < 0)
        return;
    // A caret put inside a reminder chip goes to its end (this runs again for its new place).
    if (snapCaretOutOfChips(cx))
        return;
    const at::TextInput &input = m_input->read(cx.app());
    // (A change of the text tells the controller its caret with the text.)
    if (input.text() == m_lastText)
        m_controller->setCursorPosition(input.selection().focus);
    editCaretMoved(input.selection().focus, cx);
    if (onEditStateChanged)
        onEditStateChanged();
}

PageView::EditState PageView::editState() const
{
    EditState st;
    const int row = editedRow();
    if (!m_input || row < 0 || !m_app)
        return st;
    const at::TextInput &input = m_input->read(*m_app);
    st.row = row;
    st.text = input.text();
    st.cursor = input.selection().focus;
    st.anchor = input.selection().anchor;
    st.composition = input.markedRange();
    st.caret = input.boundsForRange(at::Range{st.cursor, st.cursor}).value_or(at::Rect{});
    st.prose = !(isCode(row) || isCalc(row) || editsSource(row));
    return st;
}

void PageView::commitComposition(at::Context<PageView> &cx)
{
    if (!m_input || !m_input->read(cx.app()).markedRange())
        return;
    m_input->update(cx.app(), [](at::TextInput &t, at::Context<at::TextInput> &c) { t.unmark(c); });
    inputChanged(cx);
}

bool PageView::editorKey(const at::Keystroke &k, at::Window &window, at::Context<PageView> &cx)
{
    const int row = editedRow();
    if (!m_input || row < 0 || !m_controller)
        return false;
    // Vim's keys first (BlockEditor.handleEditorKey): its commands, and nothing typed in them.
    if (vimKey(k, cx))
        return true;
    PageController *ctl = m_controller.data();
    const at::Modifiers &m = k.modifiers;
    const bool ctrl = m.control || m.platform;
    at::App &app = cx.app();
    // Keys the editor handles itself end a composition first (the word becomes text), so Enter
    // splits after it and Backspace at the "start" does not merge blocks while a word is typed.
    auto commitWord = [&] { commitComposition(cx); };
    // The suggestion list's keys while it is open (BlockEditor moves, accepts, closes it).
    if (onSuggestionKey && !m.any()
        && (k.key == u"up" || k.key == u"down" || k.key == u"enter" || k.key == u"tab" || k.key == u"escape")) {
        if (k.key == u"enter" || k.key == u"tab")
            commitWord();
        if (onSuggestionKey(k.key))
            return true;
    }
    const at::TextInput &input = m_input->read(app);
    const at::InputHandler::Selection selection = input.selection();
    const at::Range selected = selection.range();
    const int caret = selection.focus;
    const int length = int(input.text().size());
    const QModelIndex index = m_model->index(row, 0);
    const QString view = index.data(BlockListModel::ViewRole).toString();
    const QString kind = index.data(BlockListModel::KindRole).toString();
    const bool code = view == u"code" || editsSource(row); // (a diagram's or a formula's source too)
    // (A calculation's lines break as code's: no "> ".)
    const bool calc = view == u"calc";
    const bool multiline = view == u"quote" || view == u"callout" || code || calc;
    const bool document = ctl->documentMode();
    const at::TextLayout *layout = input.layout();
    auto lineOf = [layout](int offset) { return layout ? layout->lineIndex(offset) : 0; };
    auto lineCount = [layout] { return layout ? int(layout->lines().size()) : 1; };

    if (ctrl && !m.alt) {
        if (k.key == u"z" || k.key == u"y") {
            commitWord();
            const bool redo = k.key == u"y" || m.shift;
            if (onUndo)
                onUndo(redo);
            else if (redo)
                ctl->redo();
            else
                ctl->undo();
            return true;
        }
        if (k.key == u"." && !m.shift) {
            ctl->toggleFold(row);
            return true;
        }
        if (k.key == u"enter" && !m.shift) {
            commitWord();
            ctl->cycleTask(row);
            return true;
        }
        if (k.key == u"enter" && m.shift) {
            // The status menu under the caret (BlockEditor.openTaskStatusMenuFor's keys).
            commitWord();
            if (onTaskMenuAtCaret) {
                const at::Rect c = editState().caret;
                onTaskMenuAtCaret(row, at::Point{c.left(), c.bottom()});
            }
            return true;
        }
        if ((k.key == u"c" || k.key == u"x") && !m.shift && !selected.isEmpty()) {
            // The exact text (no-break spaces kept) and the copy's HTML: the controller's.
            const QVariantMap r = ctl->copyEditText(row, selected.start, selected.end, k.key == u"x");
            if (!r.value(QStringLiteral("ok")).toBool())
                return false; // the field copies it itself
            if (k.key == u"x")
                m_input->update(app, [&](at::TextInput &t, at::Context<at::TextInput> &c) { t.replace(selected, u"", c); });
            return true;
        }
        if (k.key == u"v" && !m.shift && !code) { // (a source takes the text as it is: the field's paste)
            commitWord();
            // Markdown splits into blocks, HTML from other apps comes as Markdown, a copied picture
            // goes in as an attachment (PageController::pasteClipboard / pasteImage).
            if (ctl->clipboardHasImage()) {
                const QVariantMap r = ctl->pasteImage(row, selected.start, selected.end);
                if (r.value(QStringLiteral("row"), -1).toInt() >= 0)
                    return true;
            }
            const QVariantMap r = ctl->pasteClipboard(row, selected.start, selected.end);
            return r.value(QStringLiteral("row"), -1).toInt() >= 0;
        }
        if (k.key == u"a" && !m.shift) {
            // Notion's second Ctrl+A: the whole block's text selected already (or none): every
            // block of the page.
            if (length == 0 || (selected.start == 0 && selected.end == length)) {
                if (onSelectAllBlocks)
                    onSelectAllBlocks();
                else
                    ctl->selectAll();
                return true;
            }
            return false; // the field selects its text
        }
        return false;
    }
    if (m.alt && !ctrl) {
        if ((k.key == u"up" || k.key == u"down") && !m.shift) {
            commitWord();
            if (k.key == u"up")
                ctl->moveUp(row);
            else
                ctl->moveDown(row);
            m_revealEdited = true;
            cx.notify();
            return true;
        }
        if (k.key == u"enter") {
            if (onFollowLink) {
                onFollowLink(row, caret, m.shift);
            } else if (onLink) {
                const QString link = ctl->linkAtCursor(row, caret);
                if (!link.isEmpty())
                    onLink(row, link);
            }
            return true;
        }
        return false;
    }

    if (k.key == u"enter" && !m.control && !m.alt && !m.platform) {
        commitWord();
        if (m.shift || multiline) {
            // A line break in the block (a quote's next line carries its "> ", code's none).
            m_softBreak = true;
            const std::u16string breakText = multiline && !code && !calc ? std::u16string(u"\n> ") : std::u16string(u"\n");
            m_input->update(app, [&](at::TextInput &t, at::Context<at::TextInput> &c) { t.insert(breakText, c); });
            return true;
        }
        int at = caret;
        if (!selected.isEmpty()) {
            m_input->update(app, [&](at::TextInput &t, at::Context<at::TextInput> &c) { t.replace(selected, u"", c); });
            at = selected.start;
            // (The removal reaches the controller with the field's Changed before the split.)
            const std::u16string now = m_input->read(app).text();
            m_lastText = now;
            ctl->setEditText(row, toQString(now), at);
        }
        ctl->split(row, at);
        m_revealEdited = true;
        return true;
    }
    if (plainKey(k, u"backspace")) {
        if (caret != 0 || !selected.isEmpty() || m_input->read(app).markedRange())
            return false;
        if (multiline) {
            if (length == 0) {
                const int focus = ctl->deleteBlock(row);
                if (focus >= 0)
                    ctl->startEditing(focus, -1);
            }
            return true;
        }
        if (!index.data(BlockListModel::TaskStatusRole).toString().isEmpty() && kind == u"listItem") {
            ctl->removeTaskMarker(row);
            return true;
        }
        // A document's list item loses its marker (Obsidian), a nested one moves out.
        if (document && kind == u"listItem") {
            ctl->leaveList(row);
            m_revealEdited = true;
            return true;
        }
        ctl->mergeWithPrevious(row);
        m_revealEdited = true;
        return true;
    }
    if (plainKey(k, u"delete")) {
        if (multiline || caret != length || !selected.isEmpty() || m_input->read(app).markedRange())
            return false;
        ctl->mergeNext(row);
        return true;
    }
    if (k.key == u"tab" && !ctrl && !m.alt && code) {
        // Code: a tab in the text (Shift+Tab nothing), as CodeBlockView.
        if (!m.shift)
            m_input->update(app, [](at::TextInput &t, at::Context<at::TextInput> &c) { t.insert(u"\t", c); });
        return true;
    }
    if (k.key == u"tab" && !ctrl && !m.alt) {
        commitWord();
        // A document indents list items only; in a paragraph Tab does nothing (no outline).
        if (!document || kind == u"listItem") {
            if (m.shift)
                ctl->outdent(row);
            else
                ctl->indent(row);
        }
        return true;
    }
    if ((k.key == u"up" || k.key == u"down") && m.shift && !ctrl && !m.alt) {
        // Shift+↑ on the first line / Shift+↓ on the last: the text selection to the block's
        // start / end first, then the blocks (BlockEditor.extendFromEdit).
        const bool up = k.key == u"up";
        if (up ? lineOf(caret) > 0 : lineOf(caret) < lineCount() - 1)
            return false;
        commitWord();
        if (up ? caret > 0 : caret < length) {
            m_input->update(app, [&](at::TextInput &t, at::Context<at::TextInput> &c) {
                t.setSelection(selection.anchor, up ? 0 : length, c);
            });
            return true;
        }
        if (onExtendSelection) {
            onExtendSelection(row, up ? -1 : 1);
        } else {
            if (!ctl->extendSelection(up ? -1 : 1))
                ctl->selectRange(row, row);
        }
        return true;
    }
    if ((k.key == u"up" || k.key == u"down") && !m.any()) {
        const bool up = k.key == u"up";
        if (up ? lineOf(caret) > 0 : lineOf(caret) < lineCount() - 1)
            return false; // the field moves between its own lines
        commitWord();
        const int target = up ? ctl->previousVisibleRow(row) : ctl->nextVisibleRow(row);
        if (target < 0)
            return false; // the page's first / last block: the caret goes to its start / end
        const at::Rect bounds = input.bounds().value_or(at::Rect{});
        const float x = bounds.left() + (layout ? layout->caretX(caret) : 0);
        int cursor = -1;
        if (editsRow(target))
            cursor = editCursorOnLine(target, x, up);
        if (cursor < 0)
            cursor = up ? -1 : 0;
        ctl->startEditing(target, cursor);
        m_revealEdited = true;
        cx.notify();
        return true;
    }
    if (plainKey(k, u"escape")) {
        commitWord();
        if (onEscape)
            onEscape();
        else
            ctl->stopEditing();
        (void)window;
        return true;
    }
    return false;
}

int PageView::editCursorAt(int row, at::Point p) const
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return -1;
    const quint64 uid = m_model->uidAt(row);
    const auto it = m_rowText.find(uid);
    if (it == m_rowText.end())
        return -1;
    const std::optional<int> display = it->second.offsetAt(p);
    if (!display)
        return -1;
    if (isPanel(row) || editsAsCode(row)) // the code (a calculation, a diagram's source) is shown as it is edited
        return std::clamp(*display, 0, int(m_model->editTextAt(row).size()));
    const QModelIndex index = m_model->index(row, 0);
    const auto rendered = index.data(BlockListModel::RenderedTextRole).value<RenderedTextRef>();
    if (!rendered.d)
        return -1;
    // Display → the source the renderer got → the block's body → its editText.
    const qsizetype source = rendered.d->source.isEmpty() ? *display : rendered.d->sourceAt(*display);
    if (source < 0)
        return -1;
    const int body = int(source) + m_model->renderOffsetAt(row);
    const int edit = body - index.data(BlockListModel::EditOffsetRole).toInt();
    return std::clamp(edit, 0, int(m_model->editTextAt(row).size()));
}

const at::TextLayoutHandle *PageView::rowText(int row) const
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return nullptr;
    const auto it = m_rowText.find(m_model->uidAt(row));
    return it == m_rowText.end() || !it->second.layout() ? nullptr : &it->second;
}

int PageView::editCursorOnLine(int row, float x, bool lastLine) const
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return -1;
    const auto it = m_rowText.find(m_model->uidAt(row));
    if (it == m_rowText.end() || !it->second.layout() || it->second.layout()->lines().empty())
        return -1;
    const at::TextLayout &layout = *it->second.layout();
    const at::TextLine &line = lastLine ? layout.lines().back() : layout.lines().front();
    const at::Rect b = it->second.bounds();
    return editCursorAt(row, at::Point{x, b.top() + line.top + line.height / 2});
}

void PageView::rowClicked(std::size_t row, const at::ClickEvent &click, at::Window &, at::Context<PageView> &)
{
    if (!m_controller || click.isKeyboard || click.fromAccessibility)
        return;
    if (m_controller->readOnly()) {
        // Reading mode: the editor around refuses and says why (BlockEditor.startEditing's toast
        // with "Edit"); the page itself edits nothing.
        if (onEditRequest)
            onEditRequest(int(row), -1, click.up.modifiers);
        return;
    }
    // The release of a press that selected blocks by dragging edits nothing.
    if (m_dragSelected) {
        m_dragSelected = false;
        return;
    }
    const int cursor = editCursorAt(int(row), click.up.position);
    if (onEditRequest && onEditRequest(int(row), cursor, click.up.modifiers))
        return;
    m_controller->startEditing(int(row), cursor);
}

} // namespace brulion::editor::atr
