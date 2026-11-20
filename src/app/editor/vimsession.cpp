#include "vimsession.h"

#include "editorhost.h"
#include "pagecontroller.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QQuickItem>

using namespace Qt::StringLiterals;

namespace brulion::editor {

vim::Shared *VimSession::shared()
{
    static vim::Shared *s = [] {
        auto *shared = new vim::Shared;
        shared->readClipboard = [] {
            QClipboard *clipboard = qGuiApp ? QGuiApplication::clipboard() : nullptr;
            return clipboard ? clipboard->text() : QString();
        };
        shared->writeClipboard = [](const QString &text) {
            if (QClipboard *clipboard = qGuiApp ? QGuiApplication::clipboard() : nullptr)
                clipboard->setText(text);
        };
        return shared;
    }();
    return s;
}

VimSession::VimSession(QObject *parent) : QObject(parent), m_engine(std::make_unique<vim::Engine>(&m_page, shared()))
{
    m_page.onTextSelection = [this](int row, int anchor, int cursor) {
        Q_EMIT textSelectionRequested(row, anchor, cursor);
    };
    m_page.onMode = [this](vim::Mode) { publish(); };
    m_page.onScroll = [this](int row, vim::ScrollTo where) {
        Q_EMIT scrollRequested(row, -1, where == vim::ScrollTo::Top ? -1 : where == vim::ScrollTo::Bottom ? 1 : 0);
    };
    m_page.onQuit = [this](bool force) { Q_EMIT quitRequested(force); };
    m_source.onMode = [this](vim::Mode) { publish(); };
    m_source.onQuit = [this](bool force) { Q_EMIT quitRequested(force); };
    m_source.onSave = [this] { return m_controller && m_controller->save(); };
    m_source.onScroll = [this](int position, vim::ScrollTo where) {
        Q_EMIT scrollRequested(-1, position, where == vim::ScrollTo::Top ? -1 : where == vim::ScrollTo::Bottom ? 1 : 0);
    };
}

VimSession::~VimSession()
{
    // The buffers go with the session; the engine must not reach them after.
    m_engine.reset();
}

void VimSession::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    m_engine->reset();
    Q_EMIT enabledChanged();
    publish();
}

void VimSession::setController(PageController *controller)
{
    if (m_controller == controller)
        return;
    QObject::disconnect(m_sourceModeConnection);
    QObject::disconnect(m_pathConnection);
    m_engine->reset();
    m_controller = controller;
    m_page.setController(controller);
    if (controller) {
        m_sourceModeConnection = connect(controller, &PageController::sourceModeChanged, this, [this] {
            updateBuffer();
            publish();
        });
        // Another page: normal mode, no marks of the last one.
        m_pathConnection = connect(controller, &PageController::pathChanged, this, [this] {
            m_engine->setBuffer(activeBuffer());
            publish();
        });
    }
    updateBuffer();
    Q_EMIT controllerChanged();
    publish();
}

void VimSession::setSourceEdit(QQuickItem *edit)
{
    if (m_sourceEdit == edit)
        return;
    m_sourceEdit = edit;
    m_source.setEdit(edit);
    updateBuffer();
    Q_EMIT sourceEditChanged();
    publish();
}

void VimSession::setPageLines(int lines)
{
    lines = std::max(1, lines);
    if (m_pageLines == lines)
        return;
    m_pageLines = lines;
    m_page.setPageLines(lines);
    m_source.setPageLines(lines);
    Q_EMIT pageLinesChanged();
}

vim::Buffer *VimSession::activeBuffer()
{
    if (m_controller && m_controller->sourceMode() && m_sourceEdit)
        return &m_source;
    return &m_page;
}

void VimSession::updateBuffer()
{
    vim::Buffer *b = activeBuffer();
    if (m_engine->buffer() != b)
        m_engine->setBuffer(b);
}

QString VimSession::mode() const
{
    if (!m_enabled)
        return {};
    switch (m_engine->mode()) {
    case vim::Mode::Normal: return u"normal"_s;
    case vim::Mode::Insert: return u"insert"_s;
    case vim::Mode::Replace: return u"replace"_s;
    case vim::Mode::Visual: return u"visual"_s;
    case vim::Mode::VisualLine: return u"visualLine"_s;
    }
    return {};
}

QString VimSession::modeLabel() const
{
    if (!m_enabled)
        return {};
    switch (m_engine->mode()) {
    case vim::Mode::Normal: return tr("NORMAL");
    case vim::Mode::Insert: return tr("INSERT");
    case vim::Mode::Replace: return tr("REPLACE");
    case vim::Mode::Visual: return tr("VISUAL");
    case vim::Mode::VisualLine: return tr("VISUAL LINE");
    }
    return {};
}

bool VimSession::commands() const
{
    return m_enabled && m_engine->mode() != vim::Mode::Insert;
}

bool VimSession::typing() const
{
    return m_enabled && m_engine->mode() == vim::Mode::Insert;
}

QString VimSession::pending() const
{
    return m_enabled ? m_engine->pending() : QString();
}

QString VimSession::commandLine() const
{
    return m_enabled ? m_engine->commandLine() : QString();
}

QString VimSession::status() const
{
    if (!m_enabled)
        return {};
    if (m_engine->commandLineActive())
        return m_engine->commandLine();
    QString out = u"-- "_s + modeLabel() + u" --"_s;
    if (m_engine->mode() == vim::Mode::Normal)
        out = modeLabel();
    if (!pending().isEmpty())
        out += u"  "_s + pending();
    else if (!m_message.isEmpty())
        out += u"  "_s + m_message;
    return out;
}

QString VimSession::messageText(vim::Message m, const QString &arg, int count, int count2)
{
    switch (m) {
    case vim::Message::None: return {};
    case vim::Message::PatternNotFound: return arg.isEmpty() ? tr("Not found") : tr("Not found: %1").arg(arg);
    case vim::Message::SearchHitBottom: return tr("Search hit the bottom, continuing at the top");
    case vim::Message::SearchHitTop: return tr("Search hit the top, continuing at the bottom");
    case vim::Message::Substituted:
        return tr("%n substitution(s)", "", count) + u' ' + tr("on %n line(s)", "substitutions", count2);
    case vim::Message::NotAnEditorCommand: return tr("Not an editor command: %1").arg(arg);
    case vim::Message::NothingToUndo: return tr("Already at the oldest change");
    case vim::Message::NothingToRedo: return tr("Already at the newest change");
    case vim::Message::RegisterEmpty: return tr("Nothing in register %1").arg(arg);
    case vim::Message::MarkNotSet: return tr("Mark not set: %1").arg(arg);
    case vim::Message::InvalidRange: return tr("Invalid range");
    case vim::Message::NotSaved: return tr("Could not save");
    case vim::Message::Saved: return tr("Saved");
    case vim::Message::Unsupported: return tr("Not supported: %1").arg(arg);
    case vim::Message::ReadOnly: return tr("This text cannot be changed here");
    }
    return {};
}

void VimSession::publish()
{
    const vim::Message m = m_engine->message();
    if (m != vim::Message::None) {
        m_message = messageText(m, m_engine->messageArg(), m_engine->messageCount(), m_engine->messageCount2());
        m_messageError = m != vim::Message::Substituted && m != vim::Message::Saved
                         && m != vim::Message::SearchHitBottom && m != vim::Message::SearchHitTop;
        m_engine->clearMessage();
    }
    const QString state = mode() + u'\x1f' + pending() + u'\x1f' + commandLine() + u'\x1f' + m_message;
    if (state == m_lastState)
        return;
    m_lastState = state;
    Q_EMIT stateChanged();
}

bool VimSession::key(int key, int modifiers, const QString &text)
{
    if (!m_enabled || !m_controller)
        return false;
    updateBuffer();
    // A message stays until the next key.
    m_message.clear();
    m_messageError = false;
    const bool taken = m_engine->feed(vim::Key{key, Qt::KeyboardModifiers(modifiers), text});
    publish();
    return taken;
}

void VimSession::keys(const QString &notation)
{
    if (!m_enabled || !m_controller)
        return;
    for (const vim::Key &k : vim::keysFromNotation(notation)) {
        if (key(k.key, int(k.modifiers), k.text) || m_engine->mode() != vim::Mode::Insert)
            continue;
        // Insert mode: the editor's typing.
        vim::Buffer *b = activeBuffer();
        const vim::Position c = b->cursor();
        if (k.key == Qt::Key_Return) {
            if (b == &m_page && m_controller->editingRow() >= 0) {
                m_controller->split(m_controller->editingRow(), m_controller->cursorPosition());
            } else {
                b->replace(c, c, u"\n"_s);
                b->setCursor(vim::Position{c.line + 1, 0});
            }
        } else if (k.key == Qt::Key_Backspace) {
            if (c.column > 0) {
                const vim::Position from{c.line, vim::previousBoundary(b->line(c.line), c.column)};
                b->replace(from, c, {});
                b->setCursor(from);
            }
        } else if (!k.text.isEmpty() && k.text[0].unicode() >= 0x20) {
            if (b == &m_page && m_controller->editingRow() >= 0) {
                const int row = m_controller->editingRow();
                const int at = m_controller->cursorPosition();
                m_controller->replaceRange(row, at, at, k.text);
                m_controller->placeCaret(row, at + int(k.text.size()));
            } else {
                b->replace(c, c, k.text);
                b->setCursor(vim::Position{c.line, c.column + int(k.text.size())});
            }
        }
    }
}

void VimSession::reset()
{
    m_engine->reset();
    publish();
}

void VimSession::startInsert()
{
    if (!m_enabled || !m_controller)
        return;
    updateBuffer();
    m_engine->startInsert();
    publish();
}

bool VimSession::ownsBlockSelection() const
{
    if (!m_enabled || m_engine->buffer() != &m_page)
        return false;
    // The caret on a table selects it; a visual selection over blocks is a band of Vim's own.
    return m_page.ownsBlockSelection()
           || (m_page.showsBlocks()
               && (m_engine->mode() == vim::Mode::Visual || m_engine->mode() == vim::Mode::VisualLine));
}

void VimSession::adoptBlockSelection()
{
    if (!m_enabled || !m_controller || ownsBlockSelection() || m_engine->mode() == vim::Mode::Insert)
        return;
    updateBuffer();
    const int anchorRow =
            m_controller->selectionAnchorRow() >= 0 ? m_controller->selectionAnchorRow() : m_controller->selectedRow();
    const int focusRow = m_controller->selectedRow();
    if (focusRow < 0)
        return;
    const int a = m_page.lineOf(anchorRow);
    const int f = m_page.lineOf(focusRow);
    if (a < 0 || f < 0)
        return;
    // The whole blocks: from the first line of the upper one to the last line of the lower one.
    int last = std::max(a, f);
    while (last + 1 < m_page.lineCount() && m_page.place(last + 1).row == m_page.place(std::max(a, f)).row)
        ++last;
    const int first = std::min(a, f);
    if (f >= a)
        m_engine->adoptSelection(vim::Position{first, 0}, vim::Position{last, 0}, true);
    else
        m_engine->adoptSelection(vim::Position{last, 0}, vim::Position{first, 0}, true);
    publish();
}

void VimSession::adoptTextSelection(int row, int from, int to)
{
    if (!m_enabled || !m_controller || from == to)
        return;
    updateBuffer();
    const int la = m_page.lineOf(row, from);
    const int lc = m_page.lineOf(row, std::max(from, to - 1));
    if (la < 0 || lc < 0)
        return;
    const vim::Position anchor{la, from - m_page.place(la).offset};
    const int end = std::max(from, to - 1);
    const vim::Position cursor{lc, end - m_page.place(lc).offset};
    m_engine->adoptSelection(anchor, cursor, false);
    publish();
}

void VimSession::adoptSourceSelection(int from, int to)
{
    if (!m_enabled || !m_sourceEdit || from == to)
        return;
    updateBuffer();
    // (A text position is the source buffer's anchor.)
    const vim::Position anchor = m_source.anchorPosition(QVariant(from));
    const vim::Position cursor = m_source.anchorPosition(QVariant(std::max(from, to - 1)));
    m_engine->adoptSelection(anchor, cursor, false);
    publish();
}

} // namespace brulion::editor
