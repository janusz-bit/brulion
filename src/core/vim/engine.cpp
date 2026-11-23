// The Vim engine (vim.h): keys → commands → motions and edits of the buffer.
#include "vim.h"
#include "vim_p.h"

#include <QScopedValueRollback>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::vim {

using namespace detail;

namespace {

constexpr int MaxCount = 99999;

bool isAsciiDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

bool isAsciiLower(QChar c)
{
    return c >= u'a' && c <= u'z';
}

bool isAsciiUpper(QChar c)
{
    return c >= u'A' && c <= u'Z';
}

/// The first character of `s` from `i` (a surrogate pair whole).
QString charAt(const QString &s, int i)
{
    if (i >= s.size())
        return {};
    if (s[i].isHighSurrogate() && i + 1 < s.size())
        return s.mid(i, 2);
    return s.mid(i, 1);
}

QString toggled(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s)
        out += c.isUpper() ? c.toLower() : c.isLower() ? c.toUpper() : c;
    return out;
}

bool isTextObjectKey(QChar c)
{
    return u"wWpsbB()[]{}<>\"'`"_s.contains(c);
}

} // namespace

// --- Key ---------------------------------------------------------------------------------------

Key Key::fromText(const QString &text)
{
    Key k;
    k.text = text;
    if (text.size() == 1) {
        const QChar c = text[0];
        if (isAsciiUpper(c))
            k.key = c.unicode();
        else if (isAsciiLower(c))
            k.key = c.toUpper().unicode();
        else if (c.unicode() < 0x80)
            k.key = c.unicode();
    }
    return k;
}

Key Key::special(int key, Qt::KeyboardModifiers modifiers)
{
    Key k;
    k.key = key;
    k.modifiers = modifiers;
    return k;
}

QList<Key> keysFromNotation(const QString &s)
{
    QList<Key> out;
    for (int i = 0; i < s.size();) {
        if (s[i] == u'<') {
            const int j = int(s.indexOf(u'>', i + 1));
            if (j > i + 1) {
                const QString name = s.mid(i + 1, j - i - 1).toLower();
                Key k;
                bool ok = true;
                if (name == u"esc")
                    k = Key::special(Qt::Key_Escape);
                else if (name == u"cr" || name == u"enter" || name == u"return")
                    k = Key::special(Qt::Key_Return);
                else if (name == u"bs")
                    k = Key::special(Qt::Key_Backspace);
                else if (name == u"del")
                    k = Key::special(Qt::Key_Delete);
                else if (name == u"tab")
                    k = Key::special(Qt::Key_Tab);
                else if (name == u"s-tab")
                    k = Key::special(Qt::Key_Backtab, Qt::ShiftModifier);
                else if (name == u"left")
                    k = Key::special(Qt::Key_Left);
                else if (name == u"right")
                    k = Key::special(Qt::Key_Right);
                else if (name == u"up")
                    k = Key::special(Qt::Key_Up);
                else if (name == u"down")
                    k = Key::special(Qt::Key_Down);
                else if (name == u"home")
                    k = Key::special(Qt::Key_Home);
                else if (name == u"end")
                    k = Key::special(Qt::Key_End);
                else if (name == u"pageup")
                    k = Key::special(Qt::Key_PageUp);
                else if (name == u"pagedown")
                    k = Key::special(Qt::Key_PageDown);
                else if (name == u"insert")
                    k = Key::special(Qt::Key_Insert);
                else if (name == u"lt")
                    k = Key::fromText(u"<"_s);
                else if (name == u"space")
                    k = Key::fromText(u" "_s);
                else if (name == u"c-[")
                    k = Key::special(Qt::Key_BracketLeft, Qt::ControlModifier);
                else if (name.size() == 3 && (name.startsWith(u"c-") || name.startsWith(u"a-"))
                         && isAsciiLower(name[2])) {
                    k = Key::special(Qt::Key_A + (name[2].unicode() - u'a'),
                                     name[0] == u'c' ? Qt::ControlModifier : Qt::AltModifier);
                    if (name[0] == u'c')
                        k.text = QChar(char16_t(name[2].unicode() - u'a' + 1));
                } else
                    ok = false;
                if (ok) {
                    out.append(k);
                    i = j + 1;
                    continue;
                }
            }
        }
        const QString ch = charAt(s, i);
        out.append(Key::fromText(ch));
        i += int(ch.size());
    }
    return out;
}

QString keysToNotation(const QList<Key> &keys)
{
    static const QHash<int, QString> names{
            {Qt::Key_Escape, u"<Esc>"_s},   {Qt::Key_Return, u"<CR>"_s},     {Qt::Key_Enter, u"<CR>"_s},
            {Qt::Key_Backspace, u"<BS>"_s}, {Qt::Key_Delete, u"<Del>"_s},    {Qt::Key_Tab, u"<Tab>"_s},
            {Qt::Key_Backtab, u"<S-Tab>"_s}, {Qt::Key_Left, u"<Left>"_s},    {Qt::Key_Right, u"<Right>"_s},
            {Qt::Key_Up, u"<Up>"_s},        {Qt::Key_Down, u"<Down>"_s},     {Qt::Key_Home, u"<Home>"_s},
            {Qt::Key_End, u"<End>"_s},      {Qt::Key_PageUp, u"<PageUp>"_s}, {Qt::Key_PageDown, u"<PageDown>"_s},
            {Qt::Key_Insert, u"<Insert>"_s}};
    QString out;
    for (const Key &k : keys) {
        const Qt::KeyboardModifiers mods = k.modifiers & ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
        const bool ctrlDown = mods & Qt::ControlModifier;
        const bool alt = mods & Qt::AltModifier;
        if (k.key == Qt::Key_BracketLeft && ctrlDown) {
            out += u"<C-[>"_s;
        } else if (k.key >= Qt::Key_A && k.key <= Qt::Key_Z && (ctrlDown || alt) && !(ctrlDown && alt)) {
            out += u'<' + QString(ctrlDown ? u'C' : u'A') + u'-' + QChar(char16_t(u'a' + (k.key - Qt::Key_A))) + u'>';
        } else if (const auto it = names.constFind(k.key); it != names.cend() && (k.text.isEmpty() || k.text[0].unicode() < 0x20)) {
            out += *it;
        } else if (!k.text.isEmpty() && k.text[0].unicode() >= 0x20 && k.text[0].unicode() != 0x7F) {
            for (const QChar c : k.text)
                out += c == u'<' ? u"<lt>"_s : QString(c);
        }
    }
    return out;
}

// --- Shared ------------------------------------------------------------------------------------

bool Shared::isRegister(QChar name)
{
    return isWritable(name) || name == u'.' || name == u'/' || name == u':';
}

bool Shared::isWritable(QChar name)
{
    return name == u'"' || isAsciiDigit(name) || isAsciiLower(name) || isAsciiUpper(name) || name == u'-'
           || name == u'+' || name == u'*' || name == u'_';
}

Register Shared::get(QChar name) const
{
    if (name.isNull())
        name = u'"';
    if (isAsciiUpper(name))
        name = name.toLower();
    if (name == u'.')
        return Register{lastInserted, false, {}};
    if (name == u'/')
        return Register{lastSearch, false, {}};
    if (name == u':')
        return Register{lastCommand, false, {}};
    if (name == u'_')
        return {};
    if ((name == u'+' || name == u'*') && readClipboard) {
        QString text = readClipboard();
        Register r;
        if (text.endsWith(u'\n')) {
            text.chop(1);
            if (text.endsWith(u'\r'))
                text.chop(1);
            r.linewise = true;
            r.blocks = text;
        }
        r.text = text;
        r.text.replace(u"\r\n"_s, u"\n"_s);
        r.blocks.replace(u"\r\n"_s, u"\n"_s);
        return r;
    }
    return m_registers.value(name);
}

void Shared::store(QChar name, const Register &reg)
{
    if (name == u'_')
        return;
    if (isAsciiUpper(name)) {
        const QChar lower = name.toLower();
        Register r = m_registers.value(lower);
        if (r.isEmpty()) {
            r = reg;
        } else if (r.linewise || reg.linewise) {
            r.text = r.text + u'\n' + reg.text;
            r.blocks = r.blocks.isEmpty() && reg.blocks.isEmpty()
                               ? QString()
                               : (r.blocks.isEmpty() ? r.text : r.blocks) + u'\n'
                                         + (reg.blocks.isEmpty() ? reg.text : reg.blocks);
            r.linewise = true;
        } else {
            r.text += reg.text;
        }
        m_registers.insert(lower, r);
        return;
    }
    if ((name == u'+' || name == u'*') && writeClipboard)
        writeClipboard(reg.linewise ? (reg.blocks.isEmpty() ? reg.text : reg.blocks) + u'\n' : reg.text);
    m_registers.insert(name, reg);
}

void Shared::yanked(QChar name, const Register &reg)
{
    if (name == u'_')
        return;
    if (name.isNull() || name == u'"') {
        m_registers.insert(u'0', reg);
        m_registers.insert(u'"', reg);
        return;
    }
    store(name, reg);
    m_registers.insert(u'"', isAsciiUpper(name) ? m_registers.value(name.toLower()) : reg);
}

void Shared::deleted(QChar name, const Register &reg)
{
    if (name == u'_')
        return;
    if (name.isNull() || name == u'"') {
        if (reg.linewise || reg.text.contains(u'\n')) {
            for (char16_t d = u'9'; d > u'1'; --d)
                m_registers.insert(QChar(d), m_registers.value(QChar(char16_t(d - 1))));
            m_registers.insert(u'1', reg);
        } else {
            m_registers.insert(u'-', reg);
        }
        m_registers.insert(u'"', reg);
        return;
    }
    store(name, reg);
    m_registers.insert(u'"', isAsciiUpper(name) ? m_registers.value(name.toLower()) : reg);
}

void Shared::recorded(QChar name, const QList<Key> &keys)
{
    if (!(isAsciiLower(name) || isAsciiUpper(name) || isAsciiDigit(name)))
        return;
    const QChar target = isAsciiUpper(name) ? name.toLower() : name;
    QList<Key> all = isAsciiUpper(name) ? macroKeys(target) : QList<Key>();
    all += keys;
    m_macros.insert(target, all);
    Register r;
    r.text = keysToNotation(all);
    m_registers.insert(target, r);
}

QList<Key> Shared::macroKeys(QChar name) const
{
    if (isAsciiUpper(name))
        name = name.toLower();
    const Register r = get(name);
    if (r.isEmpty())
        return {};
    const auto it = m_macros.constFind(name);
    if (it != m_macros.cend() && keysToNotation(*it) == r.text)
        return *it;
    QList<Key> keys;
    const QStringList parts = r.text.split(u'\n');
    for (int i = 0; i < parts.size(); ++i) {
        keys += keysFromNotation(parts[i]);
        if (i + 1 < parts.size() || r.linewise)
            keys.append(Key::special(Qt::Key_Return));
    }
    return keys;
}

// --- Engine: state -----------------------------------------------------------------------------

Engine::Engine(Buffer *buffer, Shared *shared) : m_buffer(buffer), m_shared(shared) { }

void Engine::setBuffer(Buffer *buffer)
{
    reset();
    m_buffer = buffer;
    m_marks.clear();
    m_hasLastVisual = false;
}

QString Engine::pending() const
{
    QString out;
    for (const QChar c : m_pending) {
        if (c.unicode() < 0x20)
            out += u'^' + QChar(char16_t(c.unicode() + 0x40));
        else if (!isSpecial(c))
            out += c;
    }
    return out;
}

QString Engine::commandLine() const
{
    return m_cmdType.isNull() ? QString() : QString(m_cmdType) + m_cmdText;
}

void Engine::clearMessage()
{
    m_message = Message::None;
    m_messageArg.clear();
    m_messageCount = m_messageCount2 = 0;
}

void Engine::setMessage(Message m, const QString &arg, int count, int count2)
{
    m_message = m;
    m_messageArg = arg;
    m_messageCount = count;
    m_messageCount2 = count2;
}

void Engine::setMode(Mode m)
{
    if (m_mode == m)
        return;
    m_mode = m;
    if (m_buffer)
        m_buffer->modeChanged(m);
}

void Engine::reset()
{
    m_pending.clear();
    m_cmdType = QChar();
    m_cmdText.clear();
    m_cmdOperator = false;
    m_cmdFromVisual = false;
    if (m_changeOpen && m_buffer) {
        m_changeOpen = false;
        m_buffer->endChange();
    }
    m_changeOpen = false;
    m_replaced.clear();
    if (m_mode != Mode::Normal) {
        m_mode = Mode::Normal;
        if (m_buffer)
            m_buffer->modeChanged(Mode::Normal);
    }
}

void Engine::beginChange()
{
    if (m_changeOpen)
        return;
    m_changeOpen = true;
    m_buffer->beginChange();
}

void Engine::endChange()
{
    if (!m_changeOpen)
        return;
    m_changeOpen = false;
    m_buffer->endChange();
}

// --- Helpers -----------------------------------------------------------------------------------

int Engine::lineLength(int line) const
{
    return int(m_buffer->line(line).size());
}

QString Engine::lineText(int line) const
{
    return m_buffer->line(line);
}

int Engine::lastLine() const
{
    return std::max(0, m_buffer->lineCount() - 1);
}

Position Engine::clampNormal(Position p) const
{
    p.line = std::clamp(p.line, 0, lastLine());
    const QString s = lineText(p.line);
    if (s.isEmpty())
        return Position{p.line, 0};
    p.column = std::clamp(p.column, 0, int(s.size()));
    if (p.column >= s.size())
        p.column = previousBoundary(s, int(s.size()));
    else if (p.column > 0 && (s[p.column].isLowSurrogate() || s[p.column].isMark()))
        p.column = previousBoundary(s, p.column + 1);
    return p;
}

Position Engine::firstNonBlank(int line) const
{
    const QString s = lineText(line);
    int i = 0;
    while (i < s.size() && (s[i] == u' ' || s[i] == u'\t'))
        ++i;
    if (i >= s.size() && i > 0)
        i = previousBoundary(s, int(s.size()));
    return Position{line, i};
}

QString Engine::textOf(Position from, Position to) const
{
    if (to <= from)
        return {};
    if (from.line == to.line)
        return lineText(from.line).mid(from.column, to.column - from.column);
    QString out = lineText(from.line).mid(from.column);
    for (int l = from.line + 1; l < to.line; ++l)
        out += u'\n' + lineText(l);
    out += u'\n' + lineText(to.line).left(to.column);
    return out;
}

Position Engine::nextChar(Position p) const
{
    const QString s = lineText(p.line);
    const int n = nextBoundary(s, p.column);
    if (n >= s.size())
        return p;
    return Position{p.line, n};
}

Position Engine::prevChar(Position p) const
{
    return Position{p.line, previousBoundary(lineText(p.line), p.column)};
}

Position Engine::afterChar(Position p) const
{
    return Position{p.line, nextBoundary(lineText(p.line), p.column)};
}

void Engine::moveTo(Position p, bool keepWant)
{
    const Position c =
            (m_mode == Mode::Visual || m_mode == Mode::VisualLine || m_mode == Mode::Normal) ? clampNormal(p) : p;
    if (!keepWant) {
        m_want = c.column;
        m_wantEnd = false;
    }
    m_wantAt = c;
    if (m_mode == Mode::Visual || m_mode == Mode::VisualLine)
        m_buffer->showSelection(m_visualAnchor, c,
                                m_mode == Mode::VisualLine ? SelectionShape::Lines : SelectionShape::Characters);
    else
        m_buffer->setCursor(c);
}

void Engine::syncWant(Position cur)
{
    // The caret moved by something else (a click, the editor's keys, an edit): its column is the
    // one to keep.
    if (cur != m_wantAt) {
        m_want = cur.column;
        m_wantEnd = false;
    }
}

void Engine::setMark(QChar name, Position p)
{
    m_marks.insert(name, m_buffer->anchor(p));
}

bool Engine::markPosition(QChar name, Position &p)
{
    if (name == u'\'')
        name = u'`';
    if (isAsciiUpper(name))
        name = name.toLower();
    if (name == u'<' || name == u'>') {
        if (!m_hasLastVisual)
            return false;
        p = name == u'<' ? m_lastVisualStart : m_lastVisualEnd;
        if (m_lastVisualLines)
            p.column = name == u'<' ? 0 : std::max(0, lineLength(std::min(p.line, lastLine())) - 1);
        p.line = std::min(p.line, lastLine());
        return true;
    }
    const auto it = m_marks.constFind(name);
    if (it == m_marks.cend())
        return false;
    p = m_buffer->anchorPosition(*it);
    return p.line >= 0;
}

void Engine::jumpMarkHere()
{
    setMark(u'`', m_buffer->cursor());
}

// --- Keys --------------------------------------------------------------------------------------

QString Engine::tokenOf(const Key &key) const
{
    const Qt::KeyboardModifiers mods = key.modifiers & ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
    const bool ctrlDown = mods & Qt::ControlModifier;
    const bool alt = mods & Qt::AltModifier;
    const bool meta = mods & Qt::MetaModifier;
    const bool shift = mods & Qt::ShiftModifier;
    switch (key.key) {
    case Qt::Key_Escape: return ctrlDown || alt || meta ? QString() : tok(Esc);
    case Qt::Key_Return:
    case Qt::Key_Enter: return ctrlDown || alt || meta || shift ? QString() : tok(Enter);
    case Qt::Key_Backspace: return mods ? QString() : tok(Backspace);
    case Qt::Key_Delete: return mods ? QString() : tok(Delete);
    case Qt::Key_Left: return mods ? QString() : tok(Left);
    case Qt::Key_Right: return mods ? QString() : tok(Right);
    case Qt::Key_Up: return mods ? QString() : tok(Up);
    case Qt::Key_Down: return mods ? QString() : tok(Down);
    case Qt::Key_Home: return mods ? QString() : tok(Home);
    case Qt::Key_End: return mods ? QString() : tok(End);
    case Qt::Key_PageUp: return mods ? QString() : tok(PageUp);
    case Qt::Key_PageDown: return mods ? QString() : tok(PageDown);
    case Qt::Key_Insert: return mods ? QString() : tok(Insert);
    case Qt::Key_Tab:
    case Qt::Key_Backtab: return {}; // the editor's (indenting blocks)
    default: break;
    }
    if (meta)
        return {};
    if (ctrlDown && !alt) {
        if (key.key == Qt::Key_BracketLeft)
            return tok(Esc);
        if (key.key >= Qt::Key_A && key.key <= Qt::Key_Z && !shift)
            return tok(ctrl(char('A' + (key.key - Qt::Key_A))));
        return {};
    }
    if (alt && !ctrlDown)
        return {}; // Alt+key: the editor's (Alt+↑, Alt+Enter, menus)
    // A typed character (Ctrl+Alt: Windows' AltGr typing ą ę ó…).
    if (!key.text.isEmpty() && key.text[0].unicode() >= 0x20 && key.text[0].unicode() != 0x7F)
        return charAt(key.text, 0);
    return {};
}

bool Engine::feed(const Key &key)
{
    if (!m_buffer)
        return false;
    // A modifier pressed alone (Shift for a capital, AltGr for ą) or a dead key is no key of its
    // own: what is pending stays.
    switch (key.key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_Hyper_L:
    case Qt::Key_Hyper_R:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
    case Qt::Key_Mode_switch: return false;
    default:
        if (key.key >= Qt::Key_Dead_Grave && key.key <= Qt::Key_Dead_Longsolidusoverlay && key.text.isEmpty())
            return false;
        break;
    }
    clearMessage();
    // A macro being recorded keeps the keys typed (not those a macro plays); the q that ends it is
    // taken off again.
    if (!m_recording.isNull() && !m_playing)
        m_recorded.append(key);
    const QString t = tokenOf(key);
    if (m_mode == Mode::Insert)
        return feedInsert(t);
    if (t.isEmpty()) {
        m_pending.clear();
        return false;
    }
    if (!m_cmdType.isNull())
        return feedCommandLine(t);
    // Of Ctrl+letter only Vim's keys that clash with no shortcut of the editor in a command mode:
    // Ctrl+R (redo), Ctrl+D / Ctrl+U (half a screen), Ctrl+A / Ctrl+X (a number: the editor's
    // select all and cut give way), Ctrl+[ (Esc, above).
    if (t.size() == 1 && t[0].unicode() < 0x20 && t[0] != QChar(Esc) && t[0] != QChar(Enter) && t[0] != QChar(Backspace)
        && t[0] != QChar(ctrl('R')) && t[0] != QChar(ctrl('D')) && t[0] != QChar(ctrl('U'))
        && !((t[0] == QChar(ctrl('A')) || t[0] == QChar(ctrl('X'))) && m_mode != Mode::Replace)) {
        m_pending.clear();
        return false;
    }
    if (m_mode == Mode::Replace)
        return feedReplace(t);
    return feedToken(t);
}

bool Engine::feedInsert(const QString &token)
{
    if (token != tok(Esc))
        return false;
    finishInsert();
    return true;
}

bool Engine::feedReplace(const QString &token)
{
    if (token == tok(Esc)) {
        finishInsert();
        return true;
    }
    const Position cur = m_buffer->cursor();
    if (token == tok(Backspace) || token == tok(Left)) {
        if (cur.column <= m_insertStart.column && cur.line == m_insertStart.line)
            return true;
        const Position back = prevChar(cur);
        if (token == tok(Backspace) && !m_replaced.isEmpty()) {
            const QString original = m_replaced.takeLast();
            m_buffer->replace(back, afterChar(back), original);
        }
        m_buffer->setCursor(back);
        return true;
    }
    if (token == tok(Enter)) {
        m_buffer->replace(cur, cur, u"\n"_s);
        m_replaced.clear();
        return true;
    }
    if (token.size() > 2 || token[0].unicode() < 0x20 || isSpecial(token[0]))
        return true;
    const QString s = lineText(cur.line);
    const Position end = cur.column < s.size() ? afterChar(cur) : cur;
    m_replaced.append(textOf(cur, end));
    if (m_buffer->replace(cur, end, token))
        m_buffer->setCursor(Position{cur.line, cur.column + int(token.size())});
    return true;
}

bool Engine::isMotionKey(const QString &keys) const
{
    static const QStringList motions{u"h"_s,  u"l"_s,  u"j"_s,  u"k"_s, u"w"_s, u"W"_s, u"b"_s,  u"B"_s,  u"e"_s,
                                     u"E"_s,  u"ge"_s, u"gE"_s, u"0"_s, u"^"_s, u"$"_s, u"g_"_s, u"g0"_s, u"g$"_s,
                                     u"gj"_s, u"gk"_s, u"gg"_s, u"G"_s, u"{"_s, u"}"_s, u"%"_s,  u"|"_s,  u"f"_s,
                                     u"F"_s,  u"t"_s,  u"T"_s,  u";"_s, u","_s, u"n"_s, u"N"_s,  u"*"_s,  u"#"_s,
                                     u"g*"_s, u"g#"_s, u"`"_s,  u"'"_s, u"+"_s, u"-"_s, u"_"_s,  u" "_s,
                                     u"H"_s,  u"M"_s,  u"L"_s,  u"("_s, u")"_s};
    if (motions.contains(keys))
        return true;
    if (keys.size() != 1)
        return false;
    switch (keys[0].unicode()) {
    case Enter:
    case Backspace:
    case Left:
    case Right:
    case Up:
    case Down:
    case Home:
    case End:
    case PageUp:
    case PageDown: return true;
    default: return keys[0] == QChar(ctrl('D')) || keys[0] == QChar(ctrl('U')) || keys[0] == QChar(ctrl('H'));
    }
}

Engine::Parse Engine::parseMotion(const QString &keys, int &i, Command &cmd, bool objects) const
{
    if (i >= keys.size())
        return Parse::Incomplete;
    const QChar c = keys[i];
    if (objects && (c == u'i' || c == u'a')) {
        if (i + 1 >= keys.size())
            return Parse::Incomplete;
        if (!isTextObjectKey(keys[i + 1]))
            return Parse::Invalid;
        cmd.keys = keys.mid(i, 2);
        i += 2;
        return Parse::Done;
    }
    if (c == u'f' || c == u'F' || c == u't' || c == u'T' || c == u'`' || c == u'\'') {
        if (i + 1 >= keys.size())
            return Parse::Incomplete;
        const QString arg = charAt(keys, i + 1);
        if (arg == tok(Esc) || (arg.size() == 1 && isSpecial(arg[0])))
            return Parse::Invalid;
        cmd.keys = c;
        cmd.arg = arg;
        i += 1 + int(arg.size());
        return Parse::Done;
    }
    if (c == u'g') {
        if (i + 1 >= keys.size())
            return Parse::Incomplete;
        const QString two = keys.mid(i, 2);
        if (isMotionKey(two)) {
            cmd.keys = two;
            i += 2;
            return Parse::Done;
        }
        return Parse::Invalid;
    }
    if (c == u'/' || c == u'?') {
        cmd.keys = c;
        i += 1;
        return Parse::Done;
    }
    const QString one = keys.mid(i, 1);
    if (isMotionKey(one)) {
        cmd.keys = one;
        i += 1;
        return Parse::Done;
    }
    return Parse::Invalid;
}

Engine::Parse Engine::parse(const QString &keys, Command &cmd) const
{
    int i = 0;
    const bool visual = m_mode == Mode::Visual || m_mode == Mode::VisualLine;
    int count1 = 0;
    bool counted = false;
    for (int round = 0; round < 2; ++round) {
        if (i < keys.size() && keys[i] == u'"') {
            if (i + 1 >= keys.size())
                return Parse::Incomplete;
            if (!Shared::isRegister(keys[i + 1]))
                return Parse::Invalid;
            cmd.reg = keys[i + 1];
            i += 2;
        }
        if (i < keys.size() && isAsciiDigit(keys[i]) && keys[i] != u'0' && !counted) {
            while (i < keys.size() && isAsciiDigit(keys[i])) {
                count1 = std::min(MaxCount, count1 * 10 + (keys[i].unicode() - u'0'));
                ++i;
            }
            counted = true;
        }
    }
    if (i >= keys.size())
        return Parse::Incomplete;
    const int start = i;
    const QChar c = keys[i];

    // Operators.
    QString op;
    if (u"dcy<>"_s.contains(c)) {
        op = c;
        ++i;
    } else if (c == u'g') {
        if (i + 1 >= keys.size())
            return Parse::Incomplete;
        if (u"~uU"_s.contains(keys[i + 1])) {
            op = keys.mid(i, 2);
            i += 2;
        }
    }
    if (!op.isEmpty()) {
        cmd.op = op;
        if (visual) {
            cmd.count = count1;
            cmd.all = keys.mid(start, i - start);
            return i == keys.size() ? Parse::Done : Parse::Invalid;
        }
        int count2 = 0;
        bool counted2 = false;
        while (i < keys.size() && isAsciiDigit(keys[i]) && !(keys[i] == u'0' && !counted2)) {
            count2 = std::min(MaxCount, count2 * 10 + (keys[i].unicode() - u'0'));
            counted2 = true;
            ++i;
        }
        if (i >= keys.size())
            return Parse::Incomplete;
        cmd.count = (counted || counted2) ? std::min(MaxCount, std::max(1, count1) * std::max(1, count2)) : 0;
        const QString rest = keys.mid(i);
        const QString last = op.right(1);
        if (rest == last || (op.size() == 2 && rest == op)) {
            cmd.doubled = true;
            cmd.all = keys.mid(start);
            return Parse::Done;
        }
        if (op.size() == 2 && rest == u"g"_s)
            return Parse::Incomplete;
        const Parse p = parseMotion(keys, i, cmd, true);
        if (p != Parse::Done)
            return p;
        cmd.all = keys.mid(start, i - start);
        return i == keys.size() ? Parse::Done : Parse::Invalid;
    }
    cmd.count = counted ? count1 : 0;

    // Text objects in visual mode.
    if (visual && (c == u'i' || c == u'a')) {
        if (i + 1 >= keys.size())
            return Parse::Incomplete;
        if (!isTextObjectKey(keys[i + 1]))
            return Parse::Invalid;
        cmd.keys = keys.mid(i, 2);
        cmd.all = cmd.keys;
        return i + 2 == keys.size() ? Parse::Done : Parse::Invalid;
    }

    // Commands with a character after them.
    if (c == u'r' || c == u'm') {
        if (i + 1 >= keys.size())
            return Parse::Incomplete;
        const QString arg = charAt(keys, i + 1);
        if (arg == tok(Esc))
            return Parse::Invalid;
        if (c == u'm' && !(isAsciiLower(arg[0]) || isAsciiUpper(arg[0]) || arg == u"`"_s || arg == u"'"_s))
            return Parse::Invalid;
        if (c == u'r' && arg.size() == 1 && arg[0].unicode() < 0x20 && arg != tok(Enter))
            return Parse::Invalid;
        cmd.keys = c;
        cmd.arg = arg;
        cmd.all = keys.mid(start, 1 + arg.size());
        return i + 1 + arg.size() == keys.size() ? Parse::Done : Parse::Invalid;
    }
    if (c == u'q' || c == u'@') {
        if (visual)
            return Parse::Invalid;
        if (i + 1 >= keys.size())
            return Parse::Incomplete;
        const QChar arg = keys[i + 1];
        const bool ok = isAsciiLower(arg) || isAsciiUpper(arg) || isAsciiDigit(arg)
                        || (c == u'@' && (arg == u'@' || arg == u':' || arg == u'"'));
        if (!ok)
            return Parse::Invalid;
        cmd.keys = c;
        cmd.arg = QString(arg);
        cmd.all = keys.mid(start, 2);
        return i + 2 == keys.size() ? Parse::Done : Parse::Invalid;
    }
    if (c == u'g' || c == u'Z' || c == u'z') {
        if (i + 1 >= keys.size())
            return Parse::Incomplete;
        const QString two = keys.mid(i, 2);
        static const QStringList actions{u"g\x01"_s, u"g\x18"_s, u"gJ"_s, u"gp"_s, u"gP"_s, u"gi"_s, u"gv"_s, u"ZZ"_s, u"ZQ"_s,
                                         u"zz"_s, u"zt"_s, u"zb"_s, u"za"_s, u"zo"_s, u"zc"_s, u"zR"_s,
                                         u"zM"_s, u"zA"_s, u"zO"_s, u"zC"_s, u"z."_s, u"z-"_s};
        if (actions.contains(two) && i + 2 == keys.size()) {
            cmd.keys = two;
            cmd.all = two;
            return Parse::Done;
        }
    }
    // Motions.
    if (c != u'i' && c != u'a') {
        Command m = cmd;
        int j = i;
        const Parse p = parseMotion(keys, j, m, false);
        if (p == Parse::Incomplete)
            return p;
        if (p == Parse::Done) {
            if (j != keys.size())
                return Parse::Invalid;
            cmd = m;
            cmd.all = keys.mid(start);
            return Parse::Done;
        }
    }
    // Commands of one key.
    static const QString normalOne = u"iaIAoOxXsSR~JpPu.DCYvV:&"_s;
    static const QString visualOne = u"xXsSR~JpPuUDCYvVoO:rIA"_s;
    const QString &ones = visual ? visualOne : normalOne;
    if (ones.contains(c) || c == QChar(ctrl('R')) || c == QChar(ctrl('A')) || c == QChar(ctrl('X'))
        || c == QChar(Delete) || c == QChar(Insert)) {
        cmd.keys = c;
        cmd.all = c;
        return i + 1 == keys.size() ? Parse::Done : Parse::Invalid;
    }
    return Parse::Invalid;
}

bool Engine::feedToken(const QString &token)
{
    const bool visual = m_mode == Mode::Visual || m_mode == Mode::VisualLine;
    if (token == tok(Esc)) {
        if (!m_pending.isEmpty()) {
            m_pending.clear();
            return true;
        }
        if (visual) {
            const Position cur = m_buffer->cursor();
            m_lastVisualStart = std::min(m_visualAnchor, cur);
            m_lastVisualEnd = std::max(m_visualAnchor, cur);
            m_lastVisualLines = m_mode == Mode::VisualLine;
            m_hasLastVisual = true;
            setMode(Mode::Normal);
            m_buffer->setCursor(clampNormal(cur));
        }
        return true;
    }
    if (!m_recording.isNull() && !m_playing && m_pending.isEmpty() && !visual && token == u"q"_s) {
        finishRecording();
        return true;
    }
    m_pending += token;
    Command cmd;
    switch (parse(m_pending, cmd)) {
    case Parse::Incomplete: return true;
    case Parse::Invalid: m_pending.clear(); return true;
    case Parse::Done: break;
    }
    m_pending.clear();
    if (visual)
        executeVisual(cmd, false);
    else
        execute(cmd, false);
    return true;
}

// --- Normal mode -------------------------------------------------------------------------------

Engine::Range Engine::rangeOf(const Motion &m, Position start, const QString &op, bool forced) const
{
    Q_UNUSED(op);
    Range r;
    if (m.kind == MotionKind::Linewise) {
        r.from = Position{std::min(start.line, m.to.line), 0};
        r.to = Position{std::max(start.line, m.to.line), 0};
        r.linewise = true;
        r.valid = true;
        return r;
    }
    Position a = start;
    Position b = m.to;
    if (b < a)
        std::swap(a, b);
    if (m.kind == MotionKind::Inclusive) {
        b = afterChar(b);
    } else if (!forced && b.column == 0 && b.line > a.line) {
        // :h exclusive-linewise — an exclusive motion that ends at a line's start ends at the
        // previous line's end instead, and is linewise when it started in the indentation.
        if (a.column <= firstNonBlank(a.line).column && !lineText(a.line).isEmpty()
            && indentationOf(lineText(a.line)).size() >= a.column) {
            r.from = Position{a.line, 0};
            r.to = Position{b.line - 1, 0};
            r.linewise = true;
            r.valid = true;
            return r;
        }
        b = Position{b.line - 1, lineLength(b.line - 1)};
    }
    r.from = a;
    r.to = b;
    r.valid = true;
    return r;
}

Register Engine::rangeRegister(const Range &range) const
{
    if (range.linewise)
        return m_buffer->linesRegister(range.from.line, range.to.line);
    return Register{textOf(range.from, range.to), false, {}};
}

void Engine::yankRange(const Range &range, QChar reg, bool isDelete)
{
    const Register r = rangeRegister(range);
    if (isDelete)
        m_shared->deleted(reg, r);
    else
        m_shared->yanked(reg, r);
}

void Engine::deleteRange(const Range &range)
{
    if (range.linewise) {
        if (!m_buffer->removeLines(range.from.line, range.to.line))
            setMessage(Message::ReadOnly);
    } else if (range.from != range.to && !m_buffer->replace(range.from, range.to, {})) {
        setMessage(Message::ReadOnly);
    }
}

void Engine::changeCase(const Range &range, const QString &op)
{
    const int first = range.from.line;
    const int last = range.linewise ? range.to.line : range.to.line;
    for (int l = last; l >= first; --l) {
        const QString s = lineText(l);
        int from = 0, to = int(s.size());
        if (!range.linewise) {
            if (l == range.from.line)
                from = range.from.column;
            if (l == range.to.line)
                to = std::min(to, range.to.column);
        }
        if (to <= from)
            continue;
        const QString part = s.mid(from, to - from);
        const QString changed = op == u"gu" ? part.toLower() : op == u"gU" ? part.toUpper() : toggled(part);
        if (changed != part)
            m_buffer->replace(Position{l, from}, Position{l, to}, changed);
    }
}

void Engine::applyOperator(const QString &op, const Range &range, QChar reg, int count, bool visual)
{
    const Position cur = m_buffer->cursor();
    if (op == u"y") {
        yankRange(range, reg, false);
        if (range.linewise)
            m_buffer->setCursor(
                    clampNormal(Position{range.from.line, range.from.line == cur.line ? cur.column : m_want}));
        else
            m_buffer->setCursor(clampNormal(range.from));
        return;
    }
    if (op == u"d") {
        beginChange();
        yankRange(range, reg, true);
        deleteRange(range);
        endChange();
        if (range.linewise)
            moveTo(firstNonBlank(std::min(range.from.line, lastLine())));
        else
            moveTo(clampNormal(range.from));
        return;
    }
    if (op == u"c") {
        beginChange();
        yankRange(range, reg, true);
        Position at = range.from;
        if (range.linewise) {
            const QString indent = indentationOf(lineText(range.from.line));
            if (range.to.line > range.from.line)
                m_buffer->removeLines(range.from.line + 1, range.to.line);
            m_buffer->replace(Position{range.from.line, 0}, Position{range.from.line, lineLength(range.from.line)},
                              indent);
            at = Position{range.from.line, int(indent.size())};
        } else if (range.from != range.to) {
            if (!m_buffer->replace(range.from, range.to, {})) {
                setMessage(Message::ReadOnly);
                endChange();
                return;
            }
        }
        enterInsert(at, u"c"_s);
        return;
    }
    if (op == u">" || op == u"<") {
        beginChange();
        const int levels = visual ? std::max(1, count) : 1;
        if (!m_buffer->shiftLines(range.from.line, range.linewise ? range.to.line : range.to.line,
                                  op == u">" ? levels : -levels))
            setMessage(Message::ReadOnly);
        endChange();
        moveTo(firstNonBlank(std::min(range.from.line, lastLine())));
        return;
    }
    if (op == u"g~" || op == u"gu" || op == u"gU") {
        beginChange();
        changeCase(range, op);
        endChange();
        if (range.linewise)
            moveTo(clampNormal(Position{range.from.line, visual ? 0 : cur.column}));
        else
            moveTo(clampNormal(range.from));
    }
}

void Engine::enterInsert(Position at, const QString &how)
{
    beginChange();
    m_insertStart = at;
    m_buffer->setCursor(at);
    m_replaced.clear();
    setMode(how == u"R" ? Mode::Replace : Mode::Insert);
}

void Engine::startInsert()
{
    if (m_mode == Mode::Insert)
        return;
    m_pending.clear();
    Command cmd;
    cmd.keys = u"i"_s;
    cmd.all = u"i"_s;
    m_insertCommand = cmd;
    m_insertCount = 1;
    m_insertRecords = true;
    enterInsert(m_buffer->cursor(), u"i"_s);
}

void Engine::adoptSelection(Position anchor, Position cursor, bool lines)
{
    if (m_mode == Mode::Insert || m_mode == Mode::Replace)
        finishInsert();
    m_pending.clear();
    m_cmdType = QChar();
    m_cmdText.clear();
    m_visualAnchor = anchor;
    setMode(lines ? Mode::VisualLine : Mode::Visual);
    m_want = cursor.column;
    m_wantEnd = false;
    m_wantAt = cursor;
    m_buffer->showSelection(anchor, cursor, lines ? SelectionShape::Lines : SelectionShape::Characters);
}

void Engine::finishInsert()
{
    const bool replaceMode = m_mode == Mode::Replace;
    Position end = m_buffer->cursor();
    QString inserted = end > m_insertStart ? textOf(m_insertStart, end) : QString();
    const bool lines = m_insertCommand.keys == u"o" || m_insertCommand.keys == u"O";
    if (m_insertCount > 1 && lines && m_insertRecords) {
        // 3o: as many new lines, each with the text.
        for (int k = 1; k < m_insertCount; ++k) {
            const Position at{end.line, lineLength(end.line)};
            m_buffer->replace(at, at, u'\n' + inserted);
            const qsizetype nl = inserted.lastIndexOf(u'\n');
            end = nl < 0 ? Position{at.line + 1, int(inserted.size())}
                         : Position{at.line + 1 + int(inserted.count(u'\n')), int(inserted.size() - nl - 1)};
        }
        m_buffer->setCursor(end);
    } else if (m_insertCount > 1 && !inserted.isEmpty()) {
        for (int k = 1; k < m_insertCount; ++k) {
            if (replaceMode) {
                const int len = lineLength(end.line);
                const Position to{end.line, std::min(len, end.column + int(inserted.size()))};
                m_buffer->replace(end, to, inserted);
            } else {
                m_buffer->replace(end, end, inserted);
            }
            // Where the text ended (it may have line breaks).
            const qsizetype nl = inserted.lastIndexOf(u'\n');
            if (nl < 0)
                end.column += int(inserted.size());
            else
                end = Position{end.line + int(inserted.count(u'\n')), int(inserted.size() - nl - 1)};
        }
        m_buffer->setCursor(end);
    }
    m_insertCount = 1;
    if (!m_repeating) {
        m_shared->lastInserted = inserted;
        if (m_visualChangePending) {
            m_shared->lastChange = m_visualChange;
            m_shared->lastChange.inserted = inserted;
            m_shared->lastChange.insertsText = true;
        } else if (m_insertRecords) {
            Shared::Change &ch = m_shared->lastChange;
            ch = Shared::Change{};
            ch.keys = m_insertCommand.all;
            ch.count = m_insertCommand.count;
            ch.reg = m_insertCommand.reg;
            ch.inserted = inserted;
            ch.insertsText = true;
        }
    }
    setMark(u'^', end);
    setMark(u'.', end);
    m_insertRecords = false;
    m_visualChangePending = false;
    m_replaced.clear();
    endChange();
    setMode(Mode::Normal);
    // Esc puts the caret on the last character typed.
    Position c = m_buffer->cursor();
    if (c.column > 0)
        c = prevChar(c);
    m_buffer->setCursor(clampNormal(c));
    m_want = m_buffer->cursor().column;
    m_wantEnd = false;
}

void Engine::put(QChar reg, int count, bool after, bool cursorAfter)
{
    const Register r = m_shared->get(reg);
    if (r.isEmpty()) {
        setMessage(Message::RegisterEmpty, reg.isNull() ? u"\""_s : QString(reg));
        return;
    }
    const int n = std::max(1, count);
    const Position cur = m_buffer->cursor();
    beginChange();
    if (r.linewise) {
        Register rr = r;
        QStringList texts, blocks;
        for (int k = 0; k < n; ++k) {
            texts.append(r.text);
            if (!r.blocks.isEmpty())
                blocks.append(r.blocks);
        }
        rr.text = texts.join(u'\n');
        rr.blocks = blocks.join(u'\n');
        const int before = m_buffer->lineCount();
        const int first = m_buffer->putLines(cur.line, after, rr);
        endChange();
        if (first < 0) {
            setMessage(Message::ReadOnly);
            return;
        }
        const int added = std::max(1, m_buffer->lineCount() - before);
        if (cursorAfter)
            moveTo(Position{std::min(first + added, lastLine()), 0});
        else
            moveTo(firstNonBlank(first));
        return;
    }
    const QString text = r.text.repeated(n);
    const QString s = lineText(cur.line);
    const Position at = after && !s.isEmpty() ? afterChar(cur) : cur;
    if (!m_buffer->replace(at, at, text)) {
        endChange();
        setMessage(Message::ReadOnly);
        return;
    }
    endChange();
    const qsizetype nl = text.lastIndexOf(u'\n');
    Position end = nl < 0 ? Position{at.line, at.column + int(text.size())}
                          : Position{at.line + int(text.count(u'\n')), int(text.size() - nl - 1)};
    if (cursorAfter) {
        m_buffer->setCursor(end);
        m_want = end.column;
        if (m_mode == Mode::Normal)
            m_buffer->setCursor(Position{end.line, std::min(end.column, std::max(0, lineLength(end.line)))});
        return;
    }
    if (nl < 0)
        moveTo(clampNormal(prevChar(end)));
    else
        moveTo(clampNormal(at));
}

void Engine::join(int count, bool spaces)
{
    const Position cur = m_buffer->cursor();
    const int joins = std::max(1, count - 1);
    if (cur.line >= lastLine())
        return;
    beginChange();
    Position at = cur;
    for (int k = 0; k < joins && cur.line < lastLine(); ++k) {
        const QString s = lineText(cur.line);
        const QString next = lineText(cur.line + 1);
        int lead = 0;
        if (spaces)
            while (lead < next.size() && (next[lead] == u' ' || next[lead] == u'\t'))
                ++lead;
        QString sep;
        if (spaces && lead < next.size() && !s.isEmpty() && !s.endsWith(u' ') && !s.endsWith(u'\t')
            && next[lead] != u')')
            sep = u" "_s;
        int end = int(s.size());
        if (spaces && sep.isEmpty() && lead >= next.size()) {
            // An empty or blank next line: nothing added.
        }
        if (!m_buffer->replace(Position{cur.line, end}, Position{cur.line + 1, lead}, sep)) {
            setMessage(Message::ReadOnly);
            break;
        }
        at = Position{cur.line, end > 0 && sep.isEmpty() && spaces ? end - (s.endsWith(u' ') ? 1 : 0) : end};
        if (!spaces)
            at = Position{cur.line, std::max(0, end - 1) + (end > 0 ? 1 : 0)};
    }
    endChange();
    moveTo(clampNormal(at));
}

void Engine::replaceChars(int count, const QString &with)
{
    const Position cur = m_buffer->cursor();
    const QString s = lineText(cur.line);
    const int n = std::max(1, count);
    Position end = cur;
    for (int k = 0; k < n; ++k) {
        if (end.column >= s.size())
            return; // fewer characters than the count: Vim does nothing
        end = afterChar(end);
    }
    beginChange();
    if (with == tok(Enter)) {
        m_buffer->replace(cur, end, u"\n"_s);
        endChange();
        moveTo(Position{cur.line + 1, 0});
        return;
    }
    m_buffer->replace(cur, end, with.repeated(n));
    endChange();
    moveTo(clampNormal(Position{cur.line, cur.column + int(with.size()) * (n - 1)}));
}

void Engine::toggleCase(int count)
{
    const Position cur = m_buffer->cursor();
    const QString s = lineText(cur.line);
    if (s.isEmpty())
        return;
    Position end = cur;
    for (int k = 0; k < std::max(1, count) && end.column < s.size(); ++k)
        end = afterChar(end);
    beginChange();
    m_buffer->replace(cur, end, toggled(textOf(cur, end)));
    endChange();
    moveTo(clampNormal(end.column >= s.size() ? prevChar(end) : end));
}

void Engine::undo(int count, bool redo)
{
    for (int k = 0; k < std::max(1, count); ++k) {
        if (!(redo ? m_buffer->redo() : m_buffer->undo())) {
            setMessage(redo ? Message::NothingToRedo : Message::NothingToUndo);
            break;
        }
    }
    const Position c = clampNormal(m_buffer->cursor());
    m_buffer->setCursor(c);
    m_want = c.column;
    m_wantEnd = false;
}

void Engine::recordChange(const Command &cmd, bool insertsText)
{
    if (m_repeating)
        return;
    Shared::Change &ch = m_shared->lastChange;
    ch = Shared::Change{};
    ch.keys = cmd.all;
    ch.count = cmd.count;
    ch.reg = cmd.reg;
    ch.insertsText = insertsText;
    setMark(u'.', m_buffer->cursor());
}

void Engine::repeatLastChange(int count)
{
    const Shared::Change ch = m_shared->lastChange;
    if (ch.keys.isEmpty())
        return;
    const QScopedValueRollback<bool> repeating(m_repeating, true);
    Command cmd;
    if (ch.visual) {
        // A visual operator again over as much text from the caret.
        const Position cur = m_buffer->cursor();
        const Mode saved = m_mode;
        m_mode = ch.visualLines ? Mode::VisualLine : Mode::Visual;
        const Parse p = parse(ch.keys, cmd);
        m_mode = saved;
        if (p != Parse::Done)
            return;
        cmd.reg = ch.reg;
        cmd.count = ch.count;
        Position end;
        if (ch.visualLines) {
            end = Position{std::min(lastLine(), cur.line + ch.visualLineSpan), 0};
        } else if (ch.visualLineSpan == 0) {
            end = cur;
            for (int k = 0; k < ch.visualColumnSpan; ++k)
                end = nextChar(end);
        } else {
            end = clampNormal(Position{std::min(lastLine(), cur.line + ch.visualLineSpan), ch.visualColumnSpan});
        }
        m_visualAnchor = cur;
        m_mode = ch.visualLines ? Mode::VisualLine : Mode::Visual;
        m_buffer->showSelection(cur, end, ch.visualLines ? SelectionShape::Lines : SelectionShape::Characters);
        executeVisual(cmd, true);
        if (ch.insertsText && (m_mode == Mode::Insert || m_mode == Mode::Replace)) {
            const Position at = m_buffer->cursor();
            m_buffer->replace(at, at, ch.inserted);
            const qsizetype nl = ch.inserted.lastIndexOf(u'\n');
            m_buffer->setCursor(
                    nl < 0 ? Position{at.line, at.column + int(ch.inserted.size())}
                           : Position{at.line + int(ch.inserted.count(u'\n')), int(ch.inserted.size() - nl - 1)});
            finishInsert();
        }
        return;
    }
    if (parse(ch.keys, cmd) != Parse::Done)
        return;
    cmd.reg = ch.reg;
    cmd.count = count > 0 ? count : ch.count;
    cmd.all = ch.keys;
    if (count > 0)
        m_shared->lastChange.count = count; // (Vim: the new count stays for the next ".")
    execute(cmd, true);
    if (ch.insertsText && (m_mode == Mode::Insert || m_mode == Mode::Replace)) {
        const Position at = m_buffer->cursor();
        if (m_mode == Mode::Replace) {
            const int len = lineLength(at.line);
            m_buffer->replace(at, Position{at.line, std::min(len, at.column + int(ch.inserted.size()))}, ch.inserted);
        } else {
            m_buffer->replace(at, at, ch.inserted);
        }
        const qsizetype nl = ch.inserted.lastIndexOf(u'\n');
        m_buffer->setCursor(
                nl < 0 ? Position{at.line, at.column + int(ch.inserted.size())}
                       : Position{at.line + int(ch.inserted.count(u'\n')), int(ch.inserted.size() - nl - 1)});
        finishInsert();
    }
}

void Engine::execute(const Command &cmd, bool repeating)
{
    Q_UNUSED(repeating);
    const int n = std::max(1, cmd.count);
    const bool hasCount = cmd.count > 0;
    const Position cur = m_buffer->cursor();

    if (!cmd.op.isEmpty()) {
        Range range;
        if (cmd.doubled) {
            range.from = Position{cur.line, 0};
            range.to = Position{std::min(cur.line + n - 1, lastLine()), 0};
            range.linewise = true;
            range.valid = true;
        } else if (cmd.keys.size() == 2 && (cmd.keys[0] == u'i' || cmd.keys[0] == u'a')
                   && isTextObjectKey(cmd.keys[1])) {
            range = textObject(cmd.keys, cmd.count, cur, cur, false);
        } else if (cmd.keys == u"/" || cmd.keys == u"?") {
            m_cmdType = cmd.keys[0];
            m_cmdText.clear();
            m_cmdPending = cmd;
            m_cmdOperator = true;
            m_historyIndex = -1;
            return;
        } else {
            QString keys = cmd.keys;
            Motion m;
            // cw / cW on a non-blank: to the end of the word (Vim's special case: the blank after
            // it stays; on a word's last character only that character).
            const QString s = lineText(cur.line);
            if (cmd.op == u"c" && (keys == u"w" || keys == u"W") && cur.column < s.size()
                && charClass(s[cur.column]) != 0)
                keys = keys == u"w" ? u"ce"_s : u"cE"_s;
            m = motion(keys, cmd.arg, cmd.count, hasCount, true);
            if (!m.valid) {
                m_failed = true;
                m_buffer->setCursor(cur);
                return;
            }
            if (m.jump)
                setMark(u'`', cur);
            range = rangeOf(m, cur, cmd.op, false);
            m_buffer->setCursor(cur);
        }
        if (!range.valid) {
            m_failed = true;
            return;
        }
        applyOperator(cmd.op, range, cmd.reg, n, false);
        if (cmd.op != u"y" && cmd.op != u"c")
            recordChange(cmd, false);
        if (cmd.op == u"c") {
            m_insertCommand = cmd;
            m_insertCount = 1;
            m_insertRecords = true;
        }
        return;
    }

    if (isMotionKey(cmd.keys)) {
        Motion m = motion(cmd.keys, cmd.arg, cmd.count, hasCount, false);
        if (!m.valid) {
            m_failed = true;
            if (m_buffer->cursor() != cur)
                m_buffer->setCursor(cur);
            return;
        }
        if (m.jump)
            setMark(u'`', cur);
        Position to = m.to;
        if (m.keepWant) {
            syncWant(cur);
            const int len = lineLength(to.line);
            to.column = m_wantEnd ? std::max(0, len - 1) : std::min(m_want, std::max(0, len - 1));
            moveTo(to, true);
            return;
        }
        moveTo(to);
        if (m.wantEnd)
            m_wantEnd = true;
        return;
    }

    const QString &k = cmd.keys;
    const auto insertCommand = [&](Position at) {
        m_insertCommand = cmd;
        m_insertCount = n;
        m_insertRecords = true;
        enterInsert(at, cmd.keys);
    };
    if (k == u"i" || k == tok(Insert)) {
        insertCommand(cur);
    } else if (k == u"a") {
        const QString s = lineText(cur.line);
        insertCommand(s.isEmpty() ? cur : afterChar(cur));
    } else if (k == u"I") {
        const QString s = lineText(cur.line);
        insertCommand(Position{cur.line, int(indentationOf(s).size())});
    } else if (k == u"gi") {
        Position p;
        if (!markPosition(u'^', p))
            p = cur;
        insertCommand(Position{p.line, std::min(p.column, lineLength(p.line))});
    } else if (k == u"A") {
        insertCommand(Position{cur.line, lineLength(cur.line)});
    } else if (k == u"o" || k == u"O") {
        beginChange();
        const Position at = m_buffer->openLine(cur.line, k == u"o");
        if (at.line < 0) {
            endChange();
            setMessage(Message::ReadOnly);
            return;
        }
        m_insertCommand = cmd;
        m_insertCount = n;
        m_insertRecords = true;
        enterInsert(at, k);
    } else if (k == u"R") {
        m_insertCommand = cmd;
        m_insertCount = n;
        m_insertRecords = true;
        enterInsert(cur, u"R"_s);
    } else if (k == u"x" || k == tok(Delete) || k == u"X") {
        const QString s = lineText(cur.line);
        if (s.isEmpty() || (k == u"X" && cur.column == 0))
            return;
        Range r;
        if (k == u"X") {
            Position from = cur;
            for (int i = 0; i < n && from.column > 0; ++i)
                from = prevChar(from);
            r.from = from;
            r.to = cur;
        } else {
            Position to = cur;
            for (int i = 0; i < n && to.column < s.size(); ++i)
                to = afterChar(to);
            r.from = cur;
            r.to = to;
        }
        r.valid = true;
        applyOperator(u"d"_s, r, cmd.reg, 1, false);
        recordChange(cmd, false);
    } else if (k == u"D" || k == u"C" || k == u"Y" || k == u"s" || k == u"S") {
        Command c2 = cmd;
        if (k == u"D" || k == u"C") {
            const int line = std::min(cur.line + n - 1, lastLine());
            Range r;
            r.from = cur;
            r.to = Position{line, lineLength(line)};
            r.valid = true;
            if (k == u"D") {
                if (r.from == r.to && line == cur.line) {
                    return;
                }
                applyOperator(u"d"_s, r, cmd.reg, 1, false);
                recordChange(cmd, false);
            } else {
                applyOperator(u"c"_s, r, cmd.reg, 1, false);
                m_insertCommand = cmd;
                m_insertCount = 1;
                m_insertRecords = true;
            }
            return;
        }
        if (k == u"Y") {
            Range r;
            r.from = Position{cur.line, 0};
            r.to = Position{std::min(cur.line + n - 1, lastLine()), 0};
            r.linewise = true;
            r.valid = true;
            applyOperator(u"y"_s, r, cmd.reg, 1, false);
            m_buffer->setCursor(cur);
            return;
        }
        if (k == u"s") {
            const QString s = lineText(cur.line);
            Position to = cur;
            for (int i = 0; i < n && to.column < s.size(); ++i)
                to = afterChar(to);
            Range r;
            r.from = cur;
            r.to = to;
            r.valid = true;
            applyOperator(u"c"_s, r, cmd.reg, 1, false);
        } else {
            Range r;
            r.from = Position{cur.line, 0};
            r.to = Position{std::min(cur.line + n - 1, lastLine()), 0};
            r.linewise = true;
            r.valid = true;
            applyOperator(u"c"_s, r, cmd.reg, 1, false);
        }
        m_insertCommand = c2;
        m_insertCount = 1;
        m_insertRecords = true;
    } else if (k == u"r") {
        replaceChars(cmd.count, cmd.arg);
        recordChange(cmd, false);
    } else if (k == u"~") {
        toggleCase(cmd.count);
        recordChange(cmd, false);
    } else if (k == u"J" || k == u"gJ") {
        join(std::max(2, n), k == u"J");
        recordChange(cmd, false);
    } else if (k == u"p" || k == u"P" || k == u"gp" || k == u"gP") {
        put(cmd.reg, n, k == u"p" || k == u"gp", k.size() == 2);
        recordChange(cmd, false);
    } else if (k == QChar(ctrl('A')) || k == QChar(ctrl('X'))) {
        addToNumber(k == QChar(ctrl('A')) ? n : -qint64(n));
        recordChange(cmd, false);
    } else if (k == u"q") {
        startRecording(cmd.arg[0]);
    } else if (k == u"@") {
        runMacro(cmd.arg[0], n);
    } else if (k == u"u") {
        undo(n, false);
    } else if (k == QChar(ctrl('R'))) {
        undo(n, true);
    } else if (k == u".") {
        repeatLastChange(cmd.count);
    } else if (k == u"v" || k == u"V") {
        m_visualAnchor = cur;
        setMode(k == u"v" ? Mode::Visual : Mode::VisualLine);
        syncWant(cur);
        moveTo(cur, true);
    } else if (k == u"gv") {
        if (!m_hasLastVisual)
            return;
        m_visualAnchor = Position{std::min(m_lastVisualStart.line, lastLine()), m_lastVisualStart.column};
        setMode(m_lastVisualLines ? Mode::VisualLine : Mode::Visual);
        moveTo(Position{std::min(m_lastVisualEnd.line, lastLine()), m_lastVisualEnd.column}, true);
    } else if (k == u"m") {
        setMark(cmd.arg[0], cur);
    } else if (k == u":") {
        m_cmdType = u':';
        m_cmdText = hasCount ? (n == 1 ? u"."_s : u".,.+%1"_s.arg(n - 1)) : QString();
        m_cmdFromVisual = false;
        m_historyIndex = -1;
    } else if (k == u"/" || k == u"?") {
        m_cmdType = k[0];
        m_cmdText.clear();
        m_cmdOperator = false;
        m_cmdPending = cmd;
        m_historyIndex = -1;
    } else if (k == u"ZZ") {
        m_buffer->save();
        m_buffer->quit(false);
    } else if (k == u"ZQ") {
        m_buffer->quit(true);
    } else if (k == u"&") {
        runExCommand(u"s"_s);
    } else if (k.startsWith(u'z')) {
        // Folds: the buffer's (the block editor's blocks); zt zz zb scroll the view.
        switch (k.at(1).unicode()) {
        case 'a':
        case 'A': m_buffer->fold(cur.line, FoldAction::Toggle); break;
        case 'o':
        case 'O': m_buffer->fold(cur.line, FoldAction::Open); break;
        case 'c':
        case 'C': m_buffer->fold(cur.line, FoldAction::Close); break;
        case 'R': m_buffer->fold(cur.line, FoldAction::OpenAll); break;
        case 'M': m_buffer->fold(cur.line, FoldAction::CloseAll); break;
        case 't': m_buffer->scroll(cur.line, ScrollTo::Top); break;
        case 'z':
        case '.': m_buffer->scroll(cur.line, ScrollTo::Center); break;
        case 'b':
        case '-': m_buffer->scroll(cur.line, ScrollTo::Bottom); break;
        default: break;
        }
        m_buffer->setCursor(clampNormal(m_buffer->cursor()));
    }
}

// --- Visual mode -------------------------------------------------------------------------------

void Engine::executeVisual(const Command &cmd, bool repeating)
{
    const bool lines = m_mode == Mode::VisualLine;
    const Position cur = m_buffer->cursor();
    const QString &k = cmd.keys;
    const int n = std::max(1, cmd.count);

    // Motions move the caret, the anchor stays.
    if (cmd.op.isEmpty() && isMotionKey(k)) {
        if (k == u"/" || k == u"?")
            return;
        Motion m = motion(k, cmd.arg, cmd.count, cmd.count > 0, false);
        if (!m.valid) {
            m_failed = true;
            if (m_buffer->cursor() != cur)
                moveTo(cur, true);
            return;
        }
        if (m.jump)
            setMark(u'`', cur);
        Position to = m.to;
        if (m.keepWant) {
            syncWant(cur);
            const int len = lineLength(to.line);
            to.column = m_wantEnd ? len : std::min(m_want, std::max(0, len - 1));
            if (m_wantEnd) {
                m_buffer->showSelection(m_visualAnchor, to, lines ? SelectionShape::Lines : SelectionShape::Characters);
                m_wantAt = to;
            } else {
                moveTo(to, true);
            }
            return;
        }
        if (m.wantEnd) {
            // $ in visual mode: past the line's last character — the line break is selected.
            to.column = lineLength(to.line);
            m_want = to.column;
            m_wantEnd = true;
            m_wantAt = to;
            m_buffer->showSelection(m_visualAnchor, to, lines ? SelectionShape::Lines : SelectionShape::Characters);
            return;
        }
        moveTo(to);
        return;
    }
    if (cmd.op.isEmpty() && (k == u"/" || k == u"?")) {
        m_cmdType = k[0];
        m_cmdText.clear();
        m_cmdOperator = false;
        m_cmdPending = cmd;
        m_historyIndex = -1;
        return;
    }
    // Text objects grow the selection.
    if (cmd.op.isEmpty() && k.size() == 2 && (k[0] == u'i' || k[0] == u'a') && isTextObjectKey(k[1])) {
        const Range r = textObject(k, cmd.count, m_visualAnchor, cur, true);
        if (!r.valid) {
            m_failed = true;
            return;
        }
        if (r.linewise) {
            m_visualAnchor = Position{r.from.line, 0};
            setMode(Mode::VisualLine);
            moveTo(Position{r.to.line, 0}, true);
        } else {
            m_visualAnchor = r.from;
            moveTo(r.to.column > 0 ? prevChar(r.to) : r.to, false);
        }
        return;
    }
    if (k == u"o" || k == u"O") {
        const Position a = m_visualAnchor;
        m_visualAnchor = cur;
        moveTo(a);
        return;
    }
    if (k == u"v" || k == u"V") {
        const Mode want = k == u"v" ? Mode::Visual : Mode::VisualLine;
        if (m_mode == want) {
            m_lastVisualStart = std::min(m_visualAnchor, cur);
            m_lastVisualEnd = std::max(m_visualAnchor, cur);
            m_lastVisualLines = lines;
            m_hasLastVisual = true;
            setMode(Mode::Normal);
            m_buffer->setCursor(clampNormal(cur));
        } else {
            setMode(want);
            syncWant(cur);
            moveTo(cur, true);
        }
        return;
    }

    // An operator or a command over the selection.
    Position a = std::min(m_visualAnchor, cur);
    Position b = std::max(m_visualAnchor, cur);
    m_lastVisualStart = a;
    m_lastVisualEnd = b;
    m_lastVisualLines = lines;
    m_hasLastVisual = true;
    if (k == u":") {
        setMode(Mode::Normal);
        m_buffer->setCursor(clampNormal(cur));
        m_cmdType = u':';
        m_cmdText = u"'<,'>"_s;
        m_cmdFromVisual = true;
        m_historyIndex = -1;
        return;
    }
    Range r;
    r.valid = true;
    const bool forceLines = lines || k == u"X" || k == u"D" || k == u"Y" || k == u"C" || k == u"S" || k == u"R";
    if (forceLines) {
        r.from = Position{a.line, 0};
        r.to = Position{b.line, 0};
        r.linewise = true;
    } else {
        r.from = a;
        r.to = lineText(b.line).isEmpty() || b.column >= lineLength(b.line) ? Position{b.line, lineLength(b.line)}
                                                                            : afterChar(b);
        // $ selected the line break.
        if (b.column >= lineLength(b.line) && lineLength(b.line) > 0 && b.line < lastLine())
            r.to = Position{b.line + 1, 0};
        // An empty line in the selection takes its line break.
        if (lineText(b.line).isEmpty() && b.line < lastLine() && b != a)
            r.to = Position{b.line + 1, 0};
        else if (lineText(b.line).isEmpty() && b == a)
            r.to = b.line < lastLine() ? Position{b.line + 1, 0} : b;
    }
    // Remember the selection's size for the dot repeat.
    Shared::Change visualChange;
    visualChange.visual = true;
    visualChange.visualLines = forceLines;
    visualChange.visualLineSpan = b.line - a.line;
    visualChange.visualColumnSpan = b.line == a.line ? [&] {
        int chars = 0;
        Position p = a;
        while (p < b) {
            const Position q = nextChar(p);
            if (q == p)
                break;
            p = q;
            ++chars;
        }
        return chars;
    }()
                                                     : b.column;
    visualChange.keys = cmd.op.isEmpty() ? cmd.all : cmd.op;
    visualChange.count = cmd.count;
    visualChange.reg = cmd.reg;

    setMode(Mode::Normal);
    m_buffer->setCursor(clampNormal(a));
    bool record = true;
    bool inserts = false;
    QString op = cmd.op;
    if (op.isEmpty()) {
        if (k == u"x" || k == tok(Delete) || k == u"X" || k == u"D")
            op = u"d"_s;
        else if (k == u"Y")
            op = u"y"_s;
        else if (k == u"s" || k == u"C" || k == u"S" || k == u"R")
            op = u"c"_s;
        else if (k == u"~")
            op = u"g~"_s;
        else if (k == u"u")
            op = u"gu"_s;
        else if (k == u"U")
            op = u"gU"_s;
    }
    if (!op.isEmpty()) {
        m_buffer->setCursor(clampNormal(a));
        if (op == u"y") {
            yankRange(r, cmd.reg, false);
            m_buffer->setCursor(clampNormal(r.linewise ? Position{a.line, std::min(a.column, cur.column)} : a));
            record = false;
        } else {
            applyOperator(op, r, cmd.reg, n, true);
            inserts = op == u"c";
            if (inserts) {
                m_insertCommand = cmd;
                m_insertCommand.all = visualChange.keys;
                m_insertCount = 1;
                m_insertRecords = false; // recorded below with the selection's size
            }
        }
    } else if (k == u"J" || k == u"gJ") {
        m_buffer->setCursor(Position{a.line, 0});
        join(std::max(2, b.line - a.line + 1), k == u"J");
    } else if (k == QChar(ctrl('A')) || k == QChar(ctrl('X')) || k == u"g\x01"_s || k == u"g\x18"_s) {
        const bool up = k.back() == QChar(ctrl('A'));
        addToNumbersInSelection(a, r.linewise ? Position{b.line, lineLength(b.line)} : r.to, r.linewise,
                                up ? n : -qint64(n), k.size() == 2);
        moveTo(clampNormal(a));
    } else if (k == u"r") {
        beginChange();
        for (int l = b.line; l >= a.line; --l) {
            const QString s = lineText(l);
            int from = r.linewise || l > a.line ? 0 : a.column;
            int to = r.linewise || l < b.line ? int(s.size()) : std::min(int(s.size()), afterChar(b).column);
            if (to <= from)
                continue;
            int chars = 0;
            for (int i = from; i < to; i = nextBoundary(s, i))
                ++chars;
            m_buffer->replace(Position{l, from}, Position{l, to}, cmd.arg.repeated(chars));
        }
        endChange();
        moveTo(clampNormal(a));
    } else if (k == u"p" || k == u"P") {
        const Register keep = m_shared->get(cmd.reg);
        if (keep.isEmpty()) {
            setMessage(Message::RegisterEmpty, cmd.reg.isNull() ? u"\""_s : QString(cmd.reg));
            return;
        }
        beginChange();
        const Register removed = rangeRegister(r);
        deleteRange(r);
        if (r.linewise) {
            // The lines are gone: the register's lines (or text, as lines) where they were.
            Register put = keep;
            if (!put.linewise) {
                put.linewise = true;
                put.blocks.clear();
            }
            const int line = std::min(r.from.line, lastLine());
            const bool below = r.from.line > lastLine();
            if (m_buffer->lineCount() == 1 && lineText(0).isEmpty() && r.from.line == 0) {
                m_buffer->putLines(0, false, put);
                m_buffer->removeLines(m_buffer->lineCount() - 1, m_buffer->lineCount() - 1);
            } else {
                m_buffer->putLines(line, below, put);
            }
            moveTo(firstNonBlank(std::min(r.from.line, lastLine())));
        } else {
            const Position at = clampNormal(r.from);
            m_buffer->setCursor(r.from);
            if (keep.linewise) {
                m_buffer->replace(r.from, r.from, u"\n"_s);
                m_buffer->putLines(r.from.line, true, keep);
                moveTo(firstNonBlank(r.from.line + 1));
            } else {
                m_buffer->replace(r.from, r.from, keep.text);
                const qsizetype nl = keep.text.lastIndexOf(u'\n');
                if (nl < 0)
                    moveTo(clampNormal(Position{r.from.line, r.from.column + int(keep.text.size()) - 1}));
                else
                    moveTo(at);
            }
        }
        endChange();
        if (k == u"p")
            m_shared->deleted(QChar(), removed);
    } else {
        record = false;
    }
    if (record && !repeating) {
        if (inserts) {
            // Recorded at Esc with the selection's size (finishInsert() keeps the keys).
            m_visualChange = visualChange;
            m_visualChangePending = true;
        } else {
            m_shared->lastChange = visualChange;
            setMark(u'.', m_buffer->cursor());
        }
    }
}

} // namespace brulion::vim
