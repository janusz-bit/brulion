// PageView's menu of the edited text (docs/ATRAMENT.md "The block editor in Atrament (M2)", step 2;
// InlineEditor.qml's right-click TapHandler, BlockEditor.openTextMenu): a right-click inside the
// edited block's field asks the host for the text menu — Cut, Copy, Paste, Highlight…, the AI's,
// Select all, and on a misspelled word its corrections first — after the caret went to the pointer
// (unless the pointer is inside the selection: the menu acts on it), the input method's word
// committed first. Its commands (textCommand) do what the field's keys do: the controller's exact
// copy and cut (no-break spaces kept), its paste (Markdown into blocks, HTML as Markdown, a picture
// as an attachment; a source takes the text as it is), the field's whole text selected. On touch
// UIs the platform's selection handles and its floating toolbar do the same through the input
// method (Qt's Android input context sends the toolbar's Cut / Copy / Paste / Select all as
// Ctrl+X / C / V / A to the field: editorKey's).
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"

#include <QVariantMap>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

bool PageView::textMenuAt(int row, at::Point p, at::Context<PageView> &cx)
{
    if (!onTextMenu || !m_input || row < 0 || row != editedRow())
        return false;
    const at::TextInput &input = m_input->read(cx.app());
    const std::optional<at::Rect> bounds = input.bounds();
    if (!bounds || !bounds->contains(p))
        return false; // beside the text (the bullet, the margin): the block's menu
    // The misspelled word under the pointer as the text is styled now (before the caret moves).
    const std::optional<Misspelling> spelling = misspelledAt(row, p);
    const std::optional<int> at = input.offsetForPoint(p);
    commitComposition(cx);
    if (at) {
        // A click outside the selected text moves the caret there first (BlockEditor.openTextMenu).
        const at::Range selected = m_input->read(cx.app()).selection().range();
        if (selected.isEmpty() || *at < selected.start || *at > selected.end)
            m_input->update(cx.app(), [&](at::TextInput &t, at::Context<at::TextInput> &c) { t.setSelection(*at, *at, c); });
    }
    onTextMenu(row, spelling, p);
    return true;
}

bool PageView::copyEdited(bool cut, at::Context<PageView> &cx)
{
    const int row = editedRow();
    if (!m_input || row < 0 || !m_controller)
        return false;
    const at::Range selected = m_input->read(cx.app()).selection().range();
    if (selected.isEmpty())
        return false;
    // The exact text (no-break spaces kept) and the copy's HTML: the controller's.
    const QVariantMap r = m_controller->copyEditText(row, selected.start, selected.end, cut);
    if (!r.value(QStringLiteral("ok")).toBool())
        return false;
    if (cut)
        m_input->update(cx.app(), [&](at::TextInput &t, at::Context<at::TextInput> &c) { t.replace(selected, u"", c); });
    return true;
}

bool PageView::pasteEdited(at::Context<PageView> &cx, QVariantMap *result)
{
    const int row = editedRow();
    if (!m_input || row < 0 || !m_controller)
        return false;
    commitComposition(cx);
    const at::Range selected = m_input->read(cx.app()).selection().range();
    // Markdown splits into blocks, HTML from other apps comes as Markdown, a copied picture goes in
    // as an attachment (PageController::pasteClipboard / pasteImage).
    if (m_controller->clipboardHasImage()) {
        const QVariantMap r = m_controller->pasteImage(row, selected.start, selected.end);
        if (r.value(QStringLiteral("row"), -1).toInt() >= 0) {
            if (result)
                *result = r;
            return true;
        }
    }
    const QVariantMap r = m_controller->pasteClipboard(row, selected.start, selected.end);
    if (result)
        *result = r;
    return r.value(QStringLiteral("row"), -1).toInt() >= 0;
}

bool PageView::textCommand(TextCommand command, at::Context<PageView> &cx, QVariantMap *result)
{
    const int row = editedRow();
    if (!m_input || row < 0 || !m_controller)
        return false;
    commitComposition(cx);
    switch (command) {
    case TextCommand::Copy:
    case TextCommand::Cut: {
        const bool cut = command == TextCommand::Cut;
        if (m_input->read(cx.app()).selection().range().isEmpty())
            return false;
        if (cut && m_controller->readOnly())
            return false;
        if (!copyEdited(cut, cx)) // (the controller could not: the field copies it itself)
            m_input->update(cx.app(), [cut](at::TextInput &t, at::Context<at::TextInput> &c) {
                if (cut)
                    t.cut(c);
                else
                    t.copy(c.app());
            });
        return true;
    }
    case TextCommand::Paste: {
        if (m_controller->readOnly())
            return false;
        // A source (code, a diagram's, a formula's) takes the text as it is: the field's paste.
        const QString view = m_model->index(row, 0).data(BlockListModel::ViewRole).toString();
        if (view == u"code" || editsSource(row)) {
            m_input->update(cx.app(), [](at::TextInput &t, at::Context<at::TextInput> &c) { t.paste(c); });
            return true;
        }
        return pasteEdited(cx, result);
    }
    case TextCommand::SelectAll:
        m_input->update(cx.app(), [](at::TextInput &t, at::Context<at::TextInput> &c) { t.selectAll(c); });
        return true;
    }
    return false;
}

} // namespace brulion::editor::atr
