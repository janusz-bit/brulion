// PageView's Vim keys (docs/ATRAMENT.md "The block editor in Atrament (M2)", step 2 and "Open
// questions"; docs/UI.md "Vim mode"): the edited block's keys go to the editor's VimSession first,
// by BlockEditor.handleEditorKey's rules — Esc with nothing of Vim's to end goes on to the editor, a
// text selected with the mouse in normal mode becomes the visual selection, a key Vim took is
// followed by onVimKey (BlockEditor.afterVimKey), and in a command mode every key but the editor's
// shortcuts (Ctrl, Alt, Tab, function keys, Shift+arrows: vimPasses) is Vim's or dropped. The
// field follows the mode: in normal and visual mode a steady block caret, in replace mode an
// underline, the field taking no typing and no input method (InlineEditor's readOnly), in insert
// mode the bar. Vim's visual selection in the block is the field's selection; zt / zz / zb scroll
// the list, H M L are told the rows in view (VimSession::setView).
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"
#include "vimsession.h"

#include <algorithm>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

namespace {

// A key as Qt gives it (Qt::Key, Qt::KeyboardModifiers, its text): what VimSession::key takes.
struct QtKey {
    int key = 0;
    int modifiers = 0;
    QString text;
};

struct KeyName {
    const char16_t *name;
    int key;
};
constexpr KeyName kKeys[] = {
        {u"escape", Qt::Key_Escape}, {u"enter", Qt::Key_Return},     {u"tab", Qt::Key_Tab},
        {u"backspace", Qt::Key_Backspace}, {u"delete", Qt::Key_Delete}, {u"insert", Qt::Key_Insert},
        {u"home", Qt::Key_Home},     {u"end", Qt::Key_End},          {u"pageup", Qt::Key_PageUp},
        {u"pagedown", Qt::Key_PageDown}, {u"left", Qt::Key_Left},    {u"right", Qt::Key_Right},
        {u"up", Qt::Key_Up},         {u"down", Qt::Key_Down},        {u"space", Qt::Key_Space},
        {u"menu", Qt::Key_Menu},     {u"back", Qt::Key_Back},
};

// Atrament's keystroke as Qt's key event (qthost.cpp's keystrokeFromQt the other way round).
QtKey qtKeyOf(const at::Keystroke &k)
{
    QtKey q;
    Qt::KeyboardModifiers mods;
    if (k.modifiers.control)
        mods |= Qt::ControlModifier;
    if (k.modifiers.alt)
        mods |= Qt::AltModifier;
    if (k.modifiers.shift)
        mods |= Qt::ShiftModifier;
    if (k.modifiers.platform)
        mods |= Qt::MetaModifier;
    q.modifiers = int(mods.toInt());
    for (const KeyName &n : kKeys) {
        if (k.key == n.name) {
            q.key = n.key;
            break;
        }
    }
    if (q.key == Qt::Key_Tab && k.modifiers.shift)
        q.key = Qt::Key_Backtab;
    if (!q.key && k.key.size() > 1 && k.key[0] == u'f') {
        bool ok = false;
        const int n = QStringView(reinterpret_cast<const QChar *>(k.key.data() + 1), qsizetype(k.key.size() - 1)).toInt(&ok);
        if (ok && n >= 1 && n <= 35)
            q.key = Qt::Key_F1 + n - 1;
    }
    if (!q.key && k.key.size() == 1) {
        // A letter as its capital (Qt's key codes), any other character as itself.
        const QChar c(k.key[0]);
        q.key = c.isLetter() ? int(c.toUpper().unicode()) : int(c.unicode());
    } else if (!q.key && !k.keyChar.empty()) {
        // A character outside the Basic Multilingual Plane, or one only the text names.
        const QString t = QString(reinterpret_cast<const QChar *>(k.keyChar.data()), qsizetype(k.keyChar.size()));
        if (t.size() == 1)
            q.key = int(t[0].toUpper().unicode());
    }
    q.text = QString(reinterpret_cast<const QChar *>(k.keyChar.data()), qsizetype(k.keyChar.size()));
    // Ctrl+letter types its control character (as a QKeyEvent's text has it).
    if (k.modifiers.control && !k.modifiers.alt && k.key.size() == 1 && k.key[0] >= u'a' && k.key[0] <= u'z')
        q.text = QString(QChar(char16_t(k.key[0] - u'a' + 1)));
    return q;
}

// The editor's shortcuts, which pass Vim's command modes (BlockEditor.vimPasses).
bool vimPasses(const QtKey &q)
{
    if ((q.modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) != 0)
        return true;
    switch (q.key) {
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
    case Qt::Key_Back:
    case Qt::Key_Menu: return true;
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Home:
    case Qt::Key_End: return (q.modifiers & Qt::ShiftModifier) != 0; // (Shift+arrows select)
    default: break;
    }
    return q.key >= Qt::Key_F1 && q.key <= Qt::Key_F35;
}

} // namespace

VimSession *PageView::vim() const
{
    return m_vim.data();
}

void PageView::setVim(VimSession *vim, at::Context<PageView> &cx)
{
    if (vim == m_vim)
        return;
    for (const QMetaObject::Connection &c : m_vimConnections)
        QObject::disconnect(c);
    m_vimConnections.clear();
    m_vim = vim;
    m_vimSelection = {};
    if (vim) {
        at::App *app = &cx.app();
        const at::EntityId self = cx.entityId();
        auto look = [app, self] {
            app->updateEntity<PageView>(self, [](PageView &v, at::Context<PageView> &c) { v.applyVimLook(c); });
        };
        m_vimConnections.push_back(QObject::connect(vim, &VimSession::stateChanged, vim, look));
        m_vimConnections.push_back(QObject::connect(vim, &VimSession::enabledChanged, vim, look));
        m_vimConnections.push_back(QObject::connect(
                vim, &VimSession::textSelectionRequested, vim, [app, self](int row, int anchor, int cursor) {
                    app->updateEntity<PageView>(self, [&](PageView &v, at::Context<PageView> &c) {
                        v.vimSelectionRequested(row, anchor, cursor, c);
                    });
                }));
        // zt zz zb: the row to the view's top, middle or bottom.
        m_vimConnections.push_back(
                QObject::connect(vim, &VimSession::scrollRequested, vim, [app, self](int row, int, int where) {
                    if (row < 0)
                        return;
                    app->updateEntity<PageView>(self, [&](PageView &v, at::Context<PageView> &c) {
                        v.m_revealEdited = false; // (the row is where Vim put it)
                        v.scrollRowTo(std::size_t(row), where < 0    ? at::ListPosition::Start
                                                                 : where == 0 ? at::ListPosition::Center
                                                                              : at::ListPosition::End);
                        c.notify();
                    });
                }));
        // H M L: the rows in view, told before the signal returns.
        m_vimConnections.push_back(QObject::connect(
                vim, &VimSession::viewRequested, vim,
                [app, self] {
                    app->updateEntity<PageView>(self, [](PageView &v, at::Context<PageView> &) { v.answerVimView(); });
                },
                Qt::DirectConnection));
    }
    applyVimLook(cx);
}

void PageView::applyVimLook(at::Context<PageView> &cx)
{
    if (!m_input)
        return;
    const bool commands = m_vim && m_vim->enabled() && m_vim->commands();
    const at::CaretShape shape = !commands                    ? at::CaretShape::Bar
                                 : m_vim->mode() == u"replace" ? at::CaretShape::Underline
                                                               : at::CaretShape::Block;
    m_input->update(cx.app(), [&](at::TextInput &t, at::Context<at::TextInput> &c) {
        t.setCaret(shape, !commands, c);
        t.setReadOnly(commands, c);
    });
    cx.notify();
}

void PageView::vimSelectionRequested(int row, int anchor, int cursor, at::Context<PageView> &cx)
{
    m_vimSelection = {row, anchor, cursor};
    const int edited = editedRow();
    if (!m_input || edited < 0)
        return;
    if (row >= 0 && row == edited) {
        m_input->update(cx.app(), [&](at::TextInput &t, at::Context<at::TextInput> &c) { t.setSelection(anchor, cursor, c); });
    } else if (row < 0 && m_vim && m_vim->commands()) {
        // Deselected: the caret stays where it is.
        m_input->update(cx.app(), [](at::TextInput &t, at::Context<at::TextInput> &c) {
            const int focus = t.selection().focus;
            t.setSelection(focus, focus, c);
        });
    }
}

bool PageView::vimKey(const at::Keystroke &k, at::Context<PageView> &cx)
{
    if (!m_vim || !m_vim->enabled() || !m_input || !m_controller)
        return false;
    const int row = editedRow();
    if (row < 0)
        return false;
    const QtKey q = qtKeyOf(k);
    if (!q.key && q.text.isEmpty())
        return false;
    // Esc with nothing of Vim's to end (normal mode, no command typed): on to the editor (the
    // block stays edited: BlockEditor's Esc selects it).
    if (q.key == Qt::Key_Escape && !k.modifiers.any() && m_vim->mode() == u"normal" && m_vim->pending().isEmpty()
        && m_vim->commandLine().isEmpty())
        return false;
    // A word the input method composes is text before Vim reads the block (insert mode leaves
    // every key but Esc to the field).
    if (m_vim->commands() || q.key == Qt::Key_Escape)
        commitComposition(cx);
    // A text selected with the mouse in normal mode: Vim's visual selection.
    const at::Range selected = m_input->read(cx.app()).selection().range();
    if (m_vim->mode() == u"normal" && m_vim->pending().isEmpty() && !selected.isEmpty())
        m_vim->adoptTextSelection(row, selected.start, selected.end);
    // The edited block (maybe another one now) stays in view — unless the key scrolled the list
    // itself (zt, zz, zb: the scroll request below clears it).
    const bool reveal = m_revealEdited;
    m_revealEdited = true;
    if (m_vim->key(q.key, q.modifiers, q.text)) {
        cx.notify();
        if (onVimKey)
            onVimKey();
        return true;
    }
    m_revealEdited = reveal;
    return m_vim->commands() && !vimPasses(q);
}

void PageView::answerVimView()
{
    const ForeignFrame *header = edgeFrame(ForeignFrame::Kind::Header);
    if (!m_vim || !header)
        return;
    // The rows of the view's first and last lines (BlockEditor.vimReportView).
    const float top = header->mask.top();
    const float bottom = header->mask.bottom();
    const float step = std::max(2.0f, m_metrics.lineHeight / 2);
    int first = -1;
    int last = -1;
    for (float y = top + 1; y < bottom && first < 0; y += step)
        first = rowAtY(y);
    for (float y = bottom - 1; y > top && last < 0; y -= step)
        last = rowAtY(y);
    if (first >= 0)
        m_vim->setView(first, std::max(first, last));
}

} // namespace brulion::editor::atr
