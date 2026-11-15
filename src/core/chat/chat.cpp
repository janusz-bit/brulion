#include "chat/chat.h"

#include "util/paths.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>

#include <optional>

using namespace Qt::StringLiterals;

namespace brulion::chat {

namespace {

constexpr QStringView kUser = u"🧑";
constexpr QStringView kAssistant = u"🤖";
constexpr int kTitleLength = 60;

QStringView markerOf(Role role) { return role == Role::User ? kUser : kAssistant; }

/// The page's lines without their line breaks ("\r\n" or "\n").
QList<QStringView> splitLines(QStringView content)
{
    QList<QStringView> lines;
    qsizetype from = 0;
    while (from <= content.size()) {
        const qsizetype end = content.indexOf(u'\n', from);
        QStringView line = content.mid(from, (end < 0 ? content.size() : end) - from);
        if (line.endsWith(u'\r'))
            line.chop(1);
        if (end < 0) {
            if (!line.isEmpty())
                lines.append(line);
            break;
        }
        lines.append(line);
        from = end + 1;
    }
    return lines;
}

bool isBlank(QStringView line) { return line.trimmed().isEmpty(); }

qsizetype indentOf(QStringView line)
{
    qsizetype i = 0;
    while (i < line.size() && (line.at(i) == u' ' || line.at(i) == u'\t'))
        ++i;
    return i;
}

/// A fence that opens or closes fenced code: its character and length, 0 for none. A list
/// marker may come before it (`- ````).
struct Fence {
    QChar ch;
    qsizetype length = 0;
    bool bare = false; ///< nothing but the fence (and whitespace) after it: it may close one
};

Fence fenceOf(QStringView line)
{
    QStringView s = line.mid(indentOf(line));
    static const QRegularExpression marker(u"^(?:[-*+]|\\d{1,9}[.)])[ \\t]+"_s);
    if (const auto m = marker.matchView(s); m.hasMatch())
        s = s.mid(m.capturedLength());
    if (s.isEmpty() || (s.at(0) != u'`' && s.at(0) != u'~'))
        return {};
    qsizetype n = 0;
    while (n < s.size() && s.at(n) == s.at(0))
        ++n;
    if (n < 3)
        return {};
    const QStringView info = s.mid(n);
    if (s.at(0) == u'`' && info.contains(u'`'))
        return {}; // (a backtick fence's info string has no backtick: docs/SYNTAX.md §4)
    return {s.at(0), n, info.trimmed().isEmpty()};
}

/// Follows fenced code line by line: `inside` while a fence is open.
struct FenceTracker {
    QChar ch;
    qsizetype length = 0;
    bool inside() const { return length > 0; }
    void feed(QStringView line)
    {
        const Fence f = fenceOf(line);
        if (!inside()) {
            if (f.length > 0) {
                ch = f.ch;
                length = f.length;
            }
        } else if (f.length >= length && f.ch == ch && f.bare) {
            length = 0;
        }
    }
};

/// The marker a message's first line starts with, its role and where its text starts.
struct Start {
    bool found = false;
    Role role = Role::User;
    qsizetype text = 0;
};

Start startOf(QStringView line)
{
    if (line.size() < 3 || (line.at(0) != u'-' && line.at(0) != u'*' && line.at(0) != u'+'))
        return {};
    qsizetype i = 1;
    if (line.at(i) != u' ' && line.at(i) != u'\t')
        return {};
    while (i < line.size() && (line.at(i) == u' ' || line.at(i) == u'\t'))
        ++i;
    const QStringView rest = line.mid(i);
    Start s;
    if (rest.startsWith(kUser))
        s.role = Role::User;
    else if (rest.startsWith(kAssistant))
        s.role = Role::Assistant;
    else
        return {};
    qsizetype after = i + 2; // (both markers are one character beyond the BMP: two UTF-16 units)
    if (after < line.size() && line.at(after) != u' ' && line.at(after) != u'\t')
        return {}; // "🤖x": text that starts with the emoji, no message
    while (after < line.size() && (line.at(after) == u' ' || line.at(after) == u'\t'))
        ++after;
    s.found = true;
    s.text = after;
    return s;
}

/// The leading whitespace the indented lines of `lines` have in common.
QStringView commonIndent(const QList<QStringView> &lines)
{
    QStringView common;
    bool first = true;
    for (const QStringView line : lines) {
        if (isBlank(line))
            continue;
        const QStringView indent = line.left(indentOf(line));
        if (indent.isEmpty())
            continue; // (a line at the start: another top-level line, kept as it is)
        if (first) {
            common = indent;
            first = false;
            continue;
        }
        qsizetype n = 0;
        while (n < common.size() && n < indent.size() && common.at(n) == indent.at(n))
            ++n;
        common = common.left(n);
    }
    return common;
}

/// The line's reminders (`⏰ 2026-11-16 09:00`, `⏰! …`, `(@2026-11-16 …)`) inside a code span,
/// outside the code spans it has.
QString quietReminders(QStringView line)
{
    if (!line.contains(u"⏰") && !line.contains(u"(@"))
        return line.toString();
    static const QRegularExpression reminder(
            u"⏰!?[ \\t]*\\d{4}-\\d{2}-\\d{2}(?:[ T]\\d{1,2}:\\d{2})?|\\(@\\d{4}-\\d{2}-\\d{2}[^)\\n]*\\)"_s);
    const auto quiet = [](QStringView text) {
        QString out;
        qsizetype at = 0;
        for (auto it = reminder.globalMatchView(text); it.hasNext();) {
            const auto m = it.next();
            out += text.mid(at, m.capturedStart() - at);
            out += u'`' + m.captured() + u'`';
            at = m.capturedEnd();
        }
        out += text.mid(at);
        return out;
    };
    QString out;
    qsizetype i = 0;
    while (i < line.size()) {
        if (line.at(i) == u'`') {
            qsizetype run = 0;
            while (i + run < line.size() && line.at(i + run) == u'`')
                ++run;
            // The closing run of as many backticks, not part of a longer one.
            qsizetype close = -1;
            for (qsizetype j = i + run; j < line.size();) {
                if (line.at(j) != u'`') {
                    ++j;
                    continue;
                }
                qsizetype k = 0;
                while (j + k < line.size() && line.at(j + k) == u'`')
                    ++k;
                if (k == run) {
                    close = j;
                    break;
                }
                j += k;
            }
            if (close < 0) {
                out += line.mid(i, run);
                i += run;
            } else {
                out += line.mid(i, close + run - i);
                i = close + run;
            }
            continue;
        }
        qsizetype end = line.indexOf(u'`', i);
        if (end < 0)
            end = line.size();
        out += quiet(line.mid(i, end - i));
        i = end;
    }
    return out;
}

/// The line opens a block of its own (code, a list, a heading, a table, a quote): a message
/// starting with it has its marker on a line of its own.
bool opensBlock(QStringView line)
{
    static const QRegularExpression block(u"^(?:```|~~~|#{1,6}(?:[ \\t]|$)|\\||>|(?:[-*+]|\\d{1,9}[.)])(?:[ \\t]|$))"_s);
    return block.matchView(line).hasMatch();
}

/// Cuts `text` to at most `max` characters, never inside a surrogate pair.
QString cutAt(const QString &text, qsizetype max)
{
    if (text.size() <= max)
        return text;
    qsizetype n = max;
    if (n > 0 && text.at(n - 1).isHighSurrogate())
        --n;
    return text.left(n);
}

/// Inline Markdown as readable text: links by their shown text, emphasis and code marks out.
QString plainInline(QString s)
{
    static const QRegularExpression wiki(u"!?\\[\\[([^\\]|]*)(?:\\|([^\\]]*))?\\]\\]"_s);
    static const QRegularExpression mdLink(u"!?\\[([^\\]]*)\\]\\([^)]*\\)"_s);
    static const QRegularExpression marks(u"\\*\\*|__|~~|==|`+"_s);
    QString out;
    qsizetype at = 0;
    for (auto it = wiki.globalMatch(s); it.hasNext();) {
        const auto m = it.next();
        out += QStringView(s).mid(at, m.capturedStart() - at);
        const QString alias = m.captured(2);
        QString target = m.captured(1);
        if (const qsizetype hash = target.indexOf(u'#'); hash >= 0)
            target.truncate(hash);
        out += alias.isEmpty() ? target : alias;
        at = m.capturedEnd();
    }
    out += QStringView(s).mid(at);
    out.replace(mdLink, u"\\1"_s);
    out.replace(marks, QString());
    return out;
}

} // namespace

QString folder() { return u"AI"_s; }

bool inFolder(QStringView title)
{
    return title.startsWith(folder(), Qt::CaseInsensitive) && title.size() > folder().size()
           && title.at(folder().size()) == u'/';
}

Conversation parse(QStringView content)
{
    Conversation conversation;
    const QList<QStringView> lines = splitLines(content);
    qsizetype i = 0;
    // The frontmatter is no message, whatever its lines look like.
    if (!lines.isEmpty() && lines.first().trimmed() == u"---") {
        for (qsizetype j = 1; j < lines.size(); ++j) {
            if (lines.at(j).trimmed() == u"---" || lines.at(j).trimmed() == u"...") {
                i = j + 1;
                break;
            }
        }
    }
    struct Open {
        Role role;
        int line;
        QStringView first;
        QList<QStringView> rest;
        QList<int> restLines;
    };
    std::optional<Open> open;
    const auto close = [&] {
        if (!open)
            return;
        while (!open->rest.isEmpty() && isBlank(open->rest.last())) {
            open->rest.removeLast();
            open->restLines.removeLast();
        }
        const QStringView indent = commonIndent(open->rest);
        QStringList text;
        if (!open->first.trimmed().isEmpty() || open->rest.isEmpty())
            text.append(open->first.toString());
        for (const QStringView line : std::as_const(open->rest)) {
            if (isBlank(line))
                text.append(QString());
            else
                text.append((line.startsWith(indent) ? line.mid(indent.size()) : line).toString());
        }
        Message m;
        m.role = open->role;
        m.text = text.join(u'\n');
        m.line = open->line;
        m.endLine = open->restLines.isEmpty() ? open->line : open->restLines.last();
        conversation.messages.append(m);
        open.reset();
    };
    FenceTracker fence;
    for (; i < lines.size(); ++i) {
        const QStringView line = lines.at(i);
        if (!fence.inside()) {
            if (const Start s = startOf(line); s.found) {
                close();
                open = Open{s.role, int(i), line.mid(s.text).trimmed(), {}, {}};
                fence.feed(line.mid(s.text));
                continue;
            }
        }
        fence.feed(line);
        if (open) {
            open->rest.append(line);
            open->restLines.append(int(i));
        }
    }
    close();
    for (const Message &m : std::as_const(conversation.messages)) {
        if (m.role != Role::User)
            continue;
        static const QRegularExpression about(u"\\(\\[\\[([^\\[\\]|#]+)(?:[#|][^\\[\\]]*)?\\]\\]\\)[ \\t]*$"_s);
        const QStringView first = QStringView(m.text).left(m.text.indexOf(u'\n') < 0 ? m.text.size() : m.text.indexOf(u'\n'));
        if (const auto match = about.matchView(first); match.hasMatch())
            conversation.page = match.captured(1).trimmed();
        break;
    }
    return conversation;
}

QString formatMessage(Role role, QStringView markdown, QStringView newline)
{
    QList<QStringView> lines = splitLines(markdown);
    while (!lines.isEmpty() && isBlank(lines.first()))
        lines.removeFirst();
    while (!lines.isEmpty() && isBlank(lines.last()))
        lines.removeLast();
    QString out = u"- "_s + markerOf(role);
    if (lines.isEmpty())
        return out + newline;
    FenceTracker fence;
    qsizetype from = 0;
    if (!opensBlock(lines.first().trimmed())) {
        out += u' ' + quietReminders(lines.first().trimmed());
        fence.feed(lines.first());
        from = 1;
    }
    out += newline;
    for (qsizetype i = from; i < lines.size(); ++i) {
        const QStringView line = lines.at(i);
        const bool code = fence.inside();
        fence.feed(line);
        if (isBlank(line))
            out += newline;
        else
            out += u"  "_s + (code && fence.inside() ? line.toString() : quietReminders(line)) + newline;
    }
    if (fence.inside()) // (an answer cut inside its code: the fence closes, the next message is one)
        out += u"  "_s + QString(fence.length, fence.ch) + newline;
    return out;
}

QString appendMessage(const QString &content, Role role, QStringView markdown)
{
    const QString newline = content.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    QString out = content;
    if (!out.isEmpty() && !out.endsWith(u'\n'))
        out += newline;
    return out + formatMessage(role, markdown, newline);
}

QString newConversation(QStringView question, QStringView page, QStringView language)
{
    QString text = question.trimmed().toString();
    if (!page.trimmed().isEmpty()) {
        const QString link = u"([["_s + page.trimmed() + u"]])"_s;
        const qsizetype end = text.indexOf(u'\n');
        const QStringView first = QStringView(text).left(end < 0 ? text.size() : end);
        if (text.isEmpty())
            text = link;
        else if (opensBlock(first.trimmed()))
            text = link + u'\n' + text;
        else
            text.insert(end < 0 ? text.size() : end, u' ' + link);
    }
    const QString property = language == u"pl" ? u"zadania: ignoruj"_s : u"tasks: ignore"_s;
    return u"---\n"_s + property + u"\n---\n"_s + formatMessage(Role::User, text);
}

QString pageTitle(QStringView question, QDate date, QStringView language)
{
    QString first;
    for (const QStringView line : splitLines(question)) {
        if (!isBlank(line) && fenceOf(line).length == 0) {
            first = line.trimmed().toString();
            break;
        }
    }
    static const QRegularExpression marker(u"^(?:[-*+]|\\d{1,9}[.)]|#{1,6}|>)[ \\t]+"_s);
    first.remove(marker);
    first = plainInline(first);
    QString words;
    if (!first.trimmed().isEmpty()) {
        words = paths::sanitizeFileName(first);
        if (words.size() > kTitleLength) {
            QString cut = cutAt(words, kTitleLength);
            const qsizetype space = cut.lastIndexOf(u' ');
            // (At a word when the cut fell inside one and a word is left before it.)
            if (words.at(cut.size()) != u' ' && space > kTitleLength / 3)
                cut.truncate(space);
            words = cut;
        }
        static const QRegularExpression trailing(u"[\\s.,;:!?…\\-–—()]+$"_s);
        words.remove(trailing);
    }
    if (words.isEmpty() || words == u"Untitled")
        words = language == u"pl" ? u"Rozmowa"_s : u"Conversation"_s;
    return folder() + u'/' + date.toString(u"yyyy-MM-dd"_s) + u' ' + words;
}

QString journalEntry(QStringView title) { return u"- "_s + kAssistant + u" [["_s + title + u"]]"_s; }

QVariantList history(const Conversation &conversation)
{
    QVariantList out;
    QString role, content;
    const auto flush = [&] {
        if (!content.isEmpty())
            out.append(QVariantMap{{u"role"_s, role}, {u"content"_s, content}});
        content.clear();
    };
    bool started = false;
    for (const Message &m : conversation.messages) {
        const QString text = m.text.trimmed();
        if (text.isEmpty())
            continue;
        if (!started && m.role != Role::User)
            continue;
        started = true;
        const QString r = m.role == Role::User ? u"user"_s : u"assistant"_s;
        if (r != role) {
            flush();
            role = r;
        }
        content += (content.isEmpty() ? QString() : u"\n\n"_s) + text;
    }
    flush();
    return out;
}

QString preview(QStringView markdown, int max)
{
    QStringList parts;
    FenceTracker fence;
    static const QRegularExpression marker(u"^(?:[-*+]|\\d{1,9}[.)]|#{1,6}|>)[ \\t]+(?:\\[.\\][ \\t]+)?"_s);
    for (const QStringView line : splitLines(markdown)) {
        const bool code = fence.inside();
        fence.feed(line);
        if (isBlank(line) || fenceOf(line).length > 0)
            continue;
        QString s = line.trimmed().toString();
        if (!code)
            s = plainInline(s.remove(marker));
        parts.append(s);
    }
    QString out = parts.join(u' ').simplified();
    if (out.size() > max)
        out = cutAt(out, std::max(1, max - 1)).trimmed() + u'…';
    return out;
}

} // namespace brulion::chat
