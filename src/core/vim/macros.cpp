// Macros of the Vim engine (vim.h): q{register} … q records the keys the engine is given (the
// key events, not the text they typed: a macro runs on whatever the text is then), @{register}
// plays them, @@ again, @: the last command line, a count repeats; the same machinery runs the
// keys of :normal. A macro stops at the first command that fails (a motion that cannot move, a
// search that finds nothing), as in Vim, so a recursive one (qaq qa…@aq) ends where its text does,
// and its changes are one step of undo.
#include "vim.h"
#include "vim_p.h"

#include <QScopedValueRollback>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::vim {

namespace {

/// The most keys one @ plays (a recursive macro that never fails would never end).
constexpr int MaxMacroKeys = 400000;

} // namespace

void Engine::startRecording(QChar reg)
{
    if (!m_recording.isNull())
        return;
    m_recording = reg;
    m_recorded.clear();
}

void Engine::finishRecording()
{
    // The q that ends it was recorded with the rest.
    if (!m_recorded.isEmpty())
        m_recorded.removeLast();
    m_shared->recorded(m_recording, m_recorded);
    m_recording = QChar();
    m_recorded.clear();
}

void Engine::stopRecording()
{
    if (m_recording.isNull())
        return;
    m_shared->recorded(m_recording, m_recorded);
    m_recording = QChar();
    m_recorded.clear();
}

bool Engine::commandFailed() const
{
    if (m_failed)
        return true;
    switch (m_message) {
    case Message::PatternNotFound:
    case Message::NotAnEditorCommand:
    case Message::NothingToUndo:
    case Message::NothingToRedo:
    case Message::RegisterEmpty:
    case Message::MarkNotSet:
    case Message::InvalidRange:
    case Message::Unsupported:
    case Message::ReadOnly: return true;
    default: return false;
    }
}

void Engine::runMacro(QChar reg, int count)
{
    const int n = std::max(1, count);
    if (reg == u'@') {
        reg = m_shared->lastMacro;
        if (reg.isNull()) {
            setMessage(Message::RegisterEmpty, u"@"_s);
            return;
        }
    }
    if (reg == u':') {
        const QString command = m_shared->lastCommand;
        if (command.isEmpty()) {
            setMessage(Message::RegisterEmpty, u":"_s);
            return;
        }
        m_shared->lastMacro = reg;
        for (int k = 0; k < n; ++k) {
            clearMessage();
            runExCommand(command);
            if (commandFailed())
                break;
        }
        return;
    }
    const QList<Key> keys = m_shared->macroKeys(reg);
    if (keys.isEmpty()) {
        setMessage(Message::RegisterEmpty, QString(reg));
        return;
    }
    m_shared->lastMacro = reg;
    QList<Key> all;
    all.reserve(keys.size() * std::min(n, 1000));
    for (int k = 0; k < n; ++k)
        all += keys;
    playKeys(all);
}

void Engine::playKeys(const QList<Key> &keys)
{
    if (m_playing) {
        // A macro inside a macro (a recursive one): its keys come next, as typeahead does.
        m_queue = keys + m_queue;
        return;
    }
    const QScopedValueRollback<bool> playing(m_playing, true);
    m_queue = keys;
    m_keyBudget = MaxMacroKeys;
    m_buffer->beginChange(); // everything it does is one step of undo
    drainKeys();
    m_buffer->endChange();
}

void Engine::runKeysNow(const QList<Key> &keys)
{
    // :normal — the keys at once, whatever a macro around it still has to play.
    if (!m_playing)
        m_keyBudget = MaxMacroKeys;
    const QScopedValueRollback<bool> playing(m_playing, true);
    const QList<Key> outer = m_queue;
    m_queue = keys;
    drainKeys();
    // An unfinished command ends as if Esc were typed.
    for (int guard = 0; guard < 4
                        && (m_mode != Mode::Normal || !m_pending.isEmpty() || !m_cmdType.isNull());
         ++guard)
        feed(Key::special(Qt::Key_Escape));
    m_queue = outer;
}

void Engine::drainKeys()
{
    while (!m_queue.isEmpty()) {
        if (--m_keyBudget < 0) {
            m_queue.clear();
            setMessage(Message::MacroStopped);
            return;
        }
        const Key key = m_queue.takeFirst();
        m_failed = false;
        if (!feed(key) && m_mode == Mode::Insert)
            typeIntoBuffer(key);
        if (commandFailed()) {
            m_queue.clear();
            return;
        }
    }
}

/// What the editor does with a key insert mode leaves to it, for the keys played here.
void Engine::typeIntoBuffer(const Key &key)
{
    const Position c = m_buffer->cursor();
    const QString s = lineText(c.line);
    switch (key.key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_buffer->replace(c, c, u"\n"_s))
            m_buffer->setCursor(Position{c.line + 1, 0});
        return;
    case Qt::Key_Backspace:
        if (c.column > 0) {
            const Position from = prevChar(c);
            if (m_buffer->replace(from, c, {}))
                m_buffer->setCursor(from);
        } else if (c.line > 0) {
            const Position from{c.line - 1, lineLength(c.line - 1)};
            if (m_buffer->replace(from, c, {}))
                m_buffer->setCursor(from);
        }
        return;
    case Qt::Key_Delete:
        if (c.column < s.size())
            m_buffer->replace(c, afterChar(c), {});
        else if (c.line < lastLine())
            m_buffer->replace(c, Position{c.line + 1, 0}, {});
        return;
    case Qt::Key_Left:
        if (c.column > 0)
            m_buffer->setCursor(prevChar(c));
        return;
    case Qt::Key_Right:
        if (c.column < s.size())
            m_buffer->setCursor(afterChar(c));
        return;
    case Qt::Key_Home: m_buffer->setCursor(Position{c.line, 0}); return;
    case Qt::Key_End: m_buffer->setCursor(Position{c.line, int(s.size())}); return;
    default: break;
    }
    // (Ctrl+Alt is Windows' AltGr: it types.)
    const bool ctrlDown = key.modifiers & Qt::ControlModifier;
    const bool alt = key.modifiers & Qt::AltModifier;
    const bool meta = key.modifiers & Qt::MetaModifier;
    const bool types = !meta && ((!ctrlDown && !alt) || (ctrlDown && alt));
    if (types && !key.text.isEmpty() && key.text[0].unicode() >= 0x20 && key.text[0].unicode() != 0x7F) {
        if (m_buffer->replace(c, c, key.text))
            m_buffer->setCursor(Position{c.line, c.column + int(key.text.size())});
    }
}

} // namespace brulion::vim
