// The Vim engine's command line (vim.h): typing a `:` command or a `/` `?` search, and running it —
// :w :q :wq :x, a line number, :[range]s/old/new/[flags] (plain text, never a pattern), :d :y :>
// :< :j over a range, :[range]g/text/command and :v (the lines that have the text — plain text too —
// or not), :[range]normal keys, :noh.
#include "vim.h"
#include "vim_p.h"

#include <QScopedValueRollback>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::vim {

using namespace detail;

namespace {

bool isAsciiDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

void skipBlanks(const QString &s, int &i)
{
    while (i < s.size() && (s[i] == u' ' || s[i] == u'\t'))
        ++i;
}

int readNumber(const QString &s, int &i)
{
    int n = 0;
    while (i < s.size() && isAsciiDigit(s[i])) {
        n = std::min(9999999, n * 10 + (s[i].unicode() - u'0'));
        ++i;
    }
    return n;
}

/// The part of `s` from `i` up to the next `delimiter` not escaped by a backslash; `i` after the
/// delimiter (or at the end). Escapes stay as they are (the callers read them).
QString untilDelimiter(const QString &s, int &i, QChar delimiter)
{
    QString out;
    while (i < s.size()) {
        if (s[i] == u'\\' && i + 1 < s.size()) {
            out += s[i];
            out += s[i + 1];
            i += 2;
            continue;
        }
        if (s[i] == delimiter) {
            ++i;
            return out;
        }
        out += s[i++];
    }
    return out;
}

/// A pattern of plain text: `\x` is x (`\/`, `\\`, `\.`), a backslash before a letter or a digit
/// stays (the text has it).
QString unescapePattern(const QString &s)
{
    QString out;
    for (int i = 0; i < s.size(); ++i) {
        if (s[i] == u'\\' && i + 1 < s.size() && !s[i + 1].isLetterOrNumber()) {
            out += s[++i];
            continue;
        }
        out += s[i];
    }
    return out;
}

Qt::CaseSensitivity smartCase(const QString &pattern)
{
    for (const QChar c : pattern)
        if (c.isUpper())
            return Qt::CaseSensitive;
    return Qt::CaseInsensitive;
}

/// The replacement for one match: & is the match, \& an ampersand, \r and \n a line break, \t a
/// tab, \\ a backslash, \x x.
QString replacementFor(const QString &rep, const QString &match)
{
    QString out;
    for (int i = 0; i < rep.size(); ++i) {
        const QChar c = rep[i];
        if (c == u'&') {
            out += match;
            continue;
        }
        if (c == u'\\' && i + 1 < rep.size()) {
            const QChar d = rep[++i];
            if (d == u'r' || d == u'n')
                out += u'\n';
            else if (d == u't')
                out += u'\t';
            else
                out += d;
            continue;
        }
        out += c;
    }
    return out;
}

} // namespace

// --- Typing the command line -------------------------------------------------------------------

bool Engine::feedCommandLine(const QString &token)
{
    const QChar type = m_cmdType;
    QStringList &history = type == u':' ? m_shared->commandHistory : m_shared->searchHistory;
    const auto cancel = [&] {
        m_cmdType = QChar();
        m_cmdText.clear();
        m_cmdOperator = false;
        if (m_cmdFromVisual) {
            m_cmdFromVisual = false;
        }
    };
    if (token == tok(Esc)) {
        cancel();
        return true;
    }
    if (token == tok(Enter)) {
        const QString text = m_cmdText;
        m_cmdType = QChar();
        m_cmdText.clear();
        if (!text.isEmpty()) {
            history.removeAll(text);
            history.append(text);
            while (history.size() > 50)
                history.removeFirst();
        }
        runCommandLine(type, text);
        m_cmdOperator = false;
        m_cmdFromVisual = false;
        return true;
    }
    if (token == tok(Backspace) || token == tok(ctrl('H'))) {
        if (m_cmdText.isEmpty()) {
            cancel();
            return true;
        }
        m_cmdText.truncate(previousBoundary(m_cmdText, int(m_cmdText.size())));
        return true;
    }
    if (token == tok(ctrl('U'))) {
        m_cmdText.clear();
        return true;
    }
    if (token == tok(ctrl('W'))) {
        int i = int(m_cmdText.size());
        while (i > 0 && m_cmdText[i - 1] == u' ')
            --i;
        const int cls = i > 0 ? charClass(m_cmdText[i - 1]) : 0;
        while (i > 0 && charClass(m_cmdText[i - 1]) == cls && cls != 0)
            --i;
        m_cmdText.truncate(i);
        return true;
    }
    if (token == tok(Up) || token == tok(Down)) {
        if (history.isEmpty())
            return true;
        if (token == tok(Up))
            m_historyIndex = m_historyIndex < 0 ? int(history.size()) - 1 : std::max(0, m_historyIndex - 1);
        else if (m_historyIndex >= 0)
            m_historyIndex = m_historyIndex + 1 >= history.size() ? -1 : m_historyIndex + 1;
        m_cmdText = m_historyIndex < 0 ? QString() : history.at(m_historyIndex);
        return true;
    }
    if (token.isEmpty() || token[0].unicode() < 0x20 || isSpecial(token[0]) || token[0] == QChar(Delete))
        return true; // (the command line has no caret to move)
    m_cmdText += token;
    return true;
}

void Engine::runCommandLine(QChar type, const QString &text)
{
    if (type == u':') {
        m_shared->lastCommand = text;
        runExCommand(text);
        return;
    }
    const bool forward = type == u'/';
    // A trailing delimiter (/foo/) ends the pattern; offsets after it are not supported.
    int i = 0;
    QString pattern = unescapePattern(untilDelimiter(text, i, type));
    if (pattern.isEmpty())
        pattern = m_shared->lastSearch;
    if (pattern.isEmpty())
        return;
    m_shared->lastSearch = pattern;
    m_shared->lastSearchForward = forward;
    m_shared->lastSearchWholeWord = false;
    searchCommand(pattern, forward, std::max(1, m_cmdPending.count));
}

void Engine::searchCommand(const QString &pattern, bool forward, int count)
{
    const Position cur = m_buffer->cursor();
    Position p = cur;
    bool found = false;
    for (int k = 0; k < count; ++k) {
        p = searchFrom(p, pattern, forward, false, true, found);
        if (!found)
            return;
    }
    if (m_cmdOperator && !m_cmdPending.op.isEmpty()) {
        // d/foo: an exclusive motion to the match.
        Motion m;
        m.to = p;
        m.kind = MotionKind::Exclusive;
        m.valid = true;
        const Range range = rangeOf(m, cur, m_cmdPending.op, false);
        setMark(u'`', cur);
        applyOperator(m_cmdPending.op, range, m_cmdPending.reg, 1, false);
        Command recorded = m_cmdPending;
        recorded.keys = u"n"_s; // the repeat goes to the next match
        recorded.all = m_cmdPending.op + (forward == m_shared->lastSearchForward ? u"n"_s : u"N"_s);
        if (m_cmdPending.op == u"c") {
            m_insertCommand = recorded;
            m_insertCount = 1;
            m_insertRecords = true;
        } else if (m_cmdPending.op != u"y") {
            recordChange(recorded, false);
        }
        return;
    }
    setMark(u'`', cur);
    moveTo(p);
}

// --- Ex commands -------------------------------------------------------------------------------

bool Engine::parseLineAddress(const QString &text, int &i, int &line)
{
    skipBlanks(text, i);
    const Position cur = m_buffer->cursor();
    bool have = false;
    if (i < text.size()) {
        const QChar c = text[i];
        if (isAsciiDigit(c)) {
            line = readNumber(text, i) - 1;
            have = true;
        } else if (c == u'.') {
            line = cur.line;
            ++i;
            have = true;
        } else if (c == u'$') {
            line = lastLine();
            ++i;
            have = true;
        } else if (c == u'\'' && i + 1 < text.size()) {
            Position p;
            if (!markPosition(text[i + 1], p)) {
                setMessage(Message::MarkNotSet, QString(text[i + 1]));
                return false;
            }
            line = p.line;
            i += 2;
            have = true;
        }
    }
    // Offsets: +N -N (a bare + or - is 1).
    for (;;) {
        skipBlanks(text, i);
        if (i >= text.size() || (text[i] != u'+' && text[i] != u'-'))
            break;
        const int sign = text[i] == u'+' ? 1 : -1;
        ++i;
        int n = 1;
        if (i < text.size() && isAsciiDigit(text[i]))
            n = readNumber(text, i);
        if (!have) {
            line = cur.line;
            have = true;
        }
        line += sign * n;
    }
    return have;
}

bool Engine::parseLineRange(const QString &text, int &i, int &first, int &last, bool &given)
{
    given = false;
    const int cur = m_buffer->cursor().line;
    first = last = cur;
    skipBlanks(text, i);
    if (i < text.size() && text[i] == u'%') {
        first = 0;
        last = lastLine();
        given = true;
        ++i;
        return true;
    }
    int a = cur;
    const int before = i;
    const bool haveA = parseLineAddress(text, i, a);
    if (m_message == Message::MarkNotSet)
        return false;
    if (haveA) {
        first = last = a;
        given = true;
    } else {
        i = before;
    }
    skipBlanks(text, i);
    if (i < text.size() && (text[i] == u',' || text[i] == u';')) {
        ++i;
        int b = cur;
        if (!parseLineAddress(text, i, b)) {
            if (m_message == Message::MarkNotSet)
                return false;
            b = cur;
        }
        if (!haveA)
            first = cur;
        last = b;
        given = true;
    }
    if (given && (first < 0 || last < 0 || first > lastLine() || last > lastLine())) {
        setMessage(Message::InvalidRange);
        return false;
    }
    if (first > last)
        std::swap(first, last);
    return true;
}

void Engine::runExCommand(const QString &input)
{
    QString text = input.trimmed();
    while (text.startsWith(u':'))
        text = text.mid(1).trimmed();
    if (text.isEmpty())
        return;
    int i = 0;
    int first = 0, last = 0;
    bool given = false;
    if (!parseLineRange(text, i, first, last, given))
        return;
    skipBlanks(text, i);
    // The command's name: letters, or one of the signs.
    int nameEnd = i;
    while (nameEnd < text.size() && text[nameEnd].isLetter() && text[nameEnd].unicode() < 0x80)
        ++nameEnd;
    QString name = text.mid(i, nameEnd - i);
    if (name.isEmpty() && i < text.size() && (text[i] == u'>' || text[i] == u'<' || text[i] == u'&')) {
        nameEnd = i + 1;
        while (nameEnd < text.size() && text[nameEnd] == text[i])
            ++nameEnd;
        name = text.mid(i, nameEnd - i);
    }
    bool bang = false;
    if (nameEnd < text.size() && text[nameEnd] == u'!') {
        bang = true;
        ++nameEnd;
    }
    const QString args = text.mid(nameEnd).trimmed();
    const auto is = [&](const QString &shortest, const QString &full) {
        return name.size() >= shortest.size() && full.startsWith(name) && name.startsWith(shortest);
    };

    if (name.isEmpty()) {
        if (!given) {
            setMessage(Message::NotAnEditorCommand, text);
            return;
        }
        // :N — that line.
        setMark(u'`', m_buffer->cursor());
        moveTo(firstNonBlank(std::clamp(last, 0, lastLine())));
        return;
    }
    // :s/…/…/ (also :s#…#…#: any delimiter that is no letter, digit, blank, " or |).
    if (name == u"s" || is(u"s"_s, u"substitute"_s)) {
        substitute(first, last, text.mid(i + int(name.size())));
        return;
    }
    if (name == u"w" || is(u"w"_s, u"write"_s) || name == u"wa" || is(u"wa"_s, u"wall"_s) || name == u"up"
        || is(u"up"_s, u"update"_s)) {
        setMessage(m_buffer->save() ? Message::Saved : Message::NotSaved);
        return;
    }
    if (name == u"q" || is(u"q"_s, u"quit"_s) || name == u"qa" || is(u"qa"_s, u"qall"_s) || is(u"clo"_s, u"close"_s)) {
        m_buffer->quit(bang);
        return;
    }
    if (name == u"wq" || name == u"x" || is(u"x"_s, u"xit"_s) || is(u"exi"_s, u"exit"_s) || name == u"wqa"
        || name == u"xa" || is(u"wqa"_s, u"wqall"_s) || is(u"xa"_s, u"xall"_s)) {
        m_buffer->save();
        m_buffer->quit(bang);
        return;
    }
    if (is(u"noh"_s, u"nohlsearch"_s))
        return;
    if (name == u"g" || is(u"g"_s, u"global"_s) || name == u"v" || is(u"v"_s, u"vglobal"_s)) {
        globalCommand(first, last, given, bang || name.startsWith(u'v'), text.mid(nameEnd).trimmed());
        return;
    }
    if (is(u"norm"_s, u"normal"_s)) {
        QString keys = text.mid(nameEnd);
        while (keys.startsWith(u' ') || keys.startsWith(u'\t'))
            keys.remove(0, 1);
        normalCommand(first, last, given, keys);
        return;
    }
    if (name == u"d" || is(u"d"_s, u"delete"_s) || name == u"y" || is(u"y"_s, u"yank"_s)) {
        QChar reg;
        QString rest = args;
        if (!rest.isEmpty() && !isAsciiDigit(rest[0]) && Shared::isWritable(rest[0])) {
            reg = rest[0];
            rest = rest.mid(1).trimmed();
        }
        if (!rest.isEmpty() && isAsciiDigit(rest[0])) { // :d 3 — three lines from the last one
            int j = 0;
            const int n = readNumber(rest, j);
            first = last;
            last = std::min(lastLine(), first + std::max(1, n) - 1);
        }
        Range r;
        r.from = Position{first, 0};
        r.to = Position{last, 0};
        r.linewise = true;
        r.valid = true;
        applyOperator(name.startsWith(u'd') ? u"d"_s : u"y"_s, r, reg, 1, false);
        return;
    }
    if (name.startsWith(u'>') || name.startsWith(u'<')) {
        beginChange();
        m_buffer->shiftLines(first, last, name.startsWith(u'>') ? int(name.size()) : -int(name.size()));
        endChange();
        moveTo(firstNonBlank(first));
        return;
    }
    if (name == u"j" || is(u"j"_s, u"join"_s)) {
        moveTo(Position{first, 0});
        join(std::max(2, last - first + 1), !bang);
        return;
    }
    if (name == u"u" || is(u"u"_s, u"undo"_s)) {
        undo(1, false);
        return;
    }
    if (is(u"red"_s, u"redo"_s)) {
        undo(1, true);
        return;
    }
    if (name == u"&" || name == u"&&") {
        substitute(first, last, {});
        return;
    }
    setMessage(Message::NotAnEditorCommand, text);
}

void Engine::substitute(int first, int last, const QString &argsIn)
{
    QString &lastPattern = m_shared->lastSubstitute;
    QString &lastReplacement = m_shared->lastReplacement;
    QString pattern, replacement, flags;
    QString args = argsIn;
    if (args.isEmpty() || args[0].isLetterOrNumber() || args[0] == u' ' || args[0] == u'"' || args[0] == u'|') {
        // :s or :s g (repeat the last one with these flags).
        if (lastPattern.isEmpty()) {
            setMessage(Message::PatternNotFound, QString());
            return;
        }
        pattern = lastPattern;
        replacement = lastReplacement;
        flags = args.trimmed();
    } else {
        const QChar delimiter = args[0];
        int i = 1;
        pattern = untilDelimiter(args, i, delimiter);
        replacement = untilDelimiter(args, i, delimiter);
        flags = args.mid(i).trimmed();
        if (pattern.isEmpty())
            pattern = lastPattern.isEmpty() ? m_shared->lastSearch : lastPattern;
        lastPattern = pattern;
        lastReplacement = replacement;
    }
    const QString needle = unescapePattern(pattern);
    if (needle.isEmpty()) {
        setMessage(Message::PatternNotFound, QString());
        return;
    }
    m_shared->lastSearch = needle;
    m_shared->lastSearchWholeWord = false;
    bool global = false, countOnly = false;
    Qt::CaseSensitivity cs = smartCase(needle);
    for (const QChar f : flags) {
        if (f == u'g')
            global = !global;
        else if (f == u'i')
            cs = Qt::CaseInsensitive;
        else if (f == u'I')
            cs = Qt::CaseSensitive;
        else if (f == u'n')
            countOnly = true;
        else if (f == u'c' || f == u'e' || f == u'&' || f == u' ')
            continue; // (no confirmation: every match is replaced)
        else if (isAsciiDigit(f))
            break;
    }
    int substitutions = 0;
    int linesChanged = 0;
    int lastChanged = -1;
    int addedLines = 0; // line breaks put in at or above the last changed line
    if (!countOnly)
        beginChange();
    // From the last line up: a replacement with line breaks moves the lines below it only.
    for (int l = last; l >= first; --l) {
        const QString s = lineText(l);
        int at = int(s.indexOf(needle, 0, cs));
        if (at < 0)
            continue;
        QString out;
        int from = at;
        int pos = 0;
        int count = 0;
        while (at >= 0) {
            out += s.mid(pos, at - pos);
            out += replacementFor(replacement, s.mid(at, needle.size()));
            pos = at + int(needle.size());
            ++count;
            if (!global)
                break;
            at = int(s.indexOf(needle, pos, cs));
        }
        substitutions += count;
        ++linesChanged;
        if (lastChanged < 0)
            lastChanged = l;
        if (countOnly)
            continue;
        // Only the changed part of the line is replaced.
        const QString changed = out.mid(from);
        addedLines += int(changed.count(u'\n'));
        if (!m_buffer->replace(Position{l, from}, Position{l, pos}, changed)) {
            setMessage(Message::ReadOnly);
            break;
        }
    }
    if (!countOnly)
        endChange();
    if (substitutions == 0) {
        setMessage(Message::PatternNotFound, needle);
        return;
    }
    if (!countOnly) {
        moveTo(firstNonBlank(std::min(lastChanged + addedLines, lastLine())));
        Command c;
        c.keys = u"&"_s;
        c.all = u"&"_s;
        recordChange(c, false);
    }
    setMessage(Message::Substituted, needle, substitutions, linesChanged);
}

void Engine::globalCommand(int first, int last, bool given, bool invert, const QString &args)
{
    if (m_inGlobal) {
        setMessage(Message::NotAnEditorCommand, u"global"_s);
        return;
    }
    QString pattern;
    QString command;
    if (args.isEmpty()) {
        pattern = m_shared->lastSearch;
    } else {
        const QChar delimiter = args[0];
        if (delimiter.isLetterOrNumber() || delimiter == u' ' || delimiter == u'"' || delimiter == u'|') {
            setMessage(Message::NotAnEditorCommand, args);
            return;
        }
        int i = 1;
        pattern = unescapePattern(untilDelimiter(args, i, delimiter));
        command = args.mid(i).trimmed();
        if (pattern.isEmpty())
            pattern = m_shared->lastSearch;
    }
    if (pattern.isEmpty()) {
        setMessage(Message::PatternNotFound, QString());
        return;
    }
    m_shared->lastSearch = pattern;
    m_shared->lastSearchWholeWord = false;
    const Qt::CaseSensitivity cs = smartCase(pattern);
    if (!given) {
        first = 0;
        last = lastLine();
    }
    // The matching lines are marked first, then each one gets the command (a line a command removed
    // is gone from the list; the lines it moves are followed).
    QList<QVariant> marked;
    int lastMatch = -1;
    for (int l = first; l <= last; ++l) {
        if (lineText(l).contains(pattern, cs) != invert) {
            marked.append(m_buffer->lineAnchor(l));
            lastMatch = l;
        }
    }
    if (marked.isEmpty()) {
        setMessage(Message::PatternNotFound, pattern);
        return;
    }
    if (command.isEmpty()) {
        // :g/text/ prints the lines: the caret goes to the last one.
        setMark(u'`', m_buffer->cursor());
        moveTo(firstNonBlank(lastMatch));
        return;
    }
    const QScopedValueRollback<bool> inGlobal(m_inGlobal, true);
    m_buffer->beginChange(); // the whole of it is one step of undo
    for (const QVariant &anchor : std::as_const(marked)) {
        const int line = m_buffer->lineOfAnchor(anchor);
        if (line < 0 || line > lastLine())
            continue;
        m_buffer->setCursor(Position{line, 0});
        clearMessage();
        runExCommand(command);
        if (m_message == Message::NotAnEditorCommand || m_message == Message::InvalidRange)
            break;
    }
    m_buffer->endChange();
    m_buffer->setCursor(clampNormal(m_buffer->cursor()));
}

void Engine::normalCommand(int first, int last, bool given, const QString &keys)
{
    if (keys.isEmpty()) {
        setMessage(Message::NotAnEditorCommand, u"normal"_s);
        return;
    }
    const QList<Key> played = keysFromNotation(keys);
    if (!given) {
        first = last = m_buffer->cursor().line;
    }
    m_buffer->beginChange();
    // Line numbers as Vim has them: the range is counted once, whatever the keys do to the text.
    for (int l = first; l <= last && l <= lastLine(); ++l) {
        m_buffer->setCursor(Position{l, 0});
        runKeysNow(played);
    }
    m_buffer->endChange();
    if (m_mode == Mode::Normal)
        m_buffer->setCursor(clampNormal(m_buffer->cursor()));
}

} // namespace brulion::vim
