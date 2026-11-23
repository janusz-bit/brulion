#include "chat/chat.h"

#include "util/paths.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace brulion::chat {

namespace {

constexpr QStringView kUser = u"🧑";
constexpr QStringView kAssistant = u"🤖";
constexpr int kTitleLength = 60;
constexpr QStringView kTimeFormat = u"yyyy-MM-dd HH:mm";

QStringView markerOf(Role role) { return role == Role::User ? kUser : kAssistant; }

/// The page a frontmatter's link value names: `[[AI/x|y]]` → "AI/x" (an alias, a heading or a
/// block part left out; a value that is no link as written).
QString linkTarget(const QString &value)
{
    QString target = value.trimmed();
    if (target.startsWith(u"[["_s) && target.endsWith(u"]]"_s))
        target = target.mid(2, target.size() - 4);
    for (const QChar cut : {u'|', u'#', u'^'}) {
        if (const qsizetype at = target.indexOf(cut); at >= 0)
            target.truncate(at);
    }
    target = target.trimmed();
    if (target.endsWith(u".md"_s, Qt::CaseInsensitive))
        target.chop(3);
    return target;
}

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

/// A message's time line (`  czas:: 2026-11-15 09:12`, `time::`, indented): the time, else invalid.
QDateTime timeOfLine(QStringView line)
{
    static const QRegularExpression timeLine(
            u"^[ \\t]+(?:czas|time)::[ \\t]*(\\d{4}-\\d{2}-\\d{2})[ T](\\d{1,2}):(\\d{2})[ \\t]*$"_s,
            QRegularExpression::CaseInsensitiveOption);
    const auto m = timeLine.matchView(line);
    if (!m.hasMatch())
        return {};
    const QDate date = QDate::fromString(m.captured(1), u"yyyy-MM-dd"_s);
    const QTime time(m.captured(2).toInt(), m.captured(3).toInt());
    if (!date.isValid() || !time.isValid())
        return {};
    return QDateTime(date, time); // (local wall-clock time, as written)
}

/// The language a conversation page is written in, by what it has: a time's key, else its
/// frontmatter's word for the tasks; "" when it says nothing.
QString languageOf(QStringView content)
{
    static const QRegularExpression polish(u"^(?:[ \\t]+czas::|zadania:)"_s,
                                           QRegularExpression::MultilineOption
                                                   | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression english(u"^(?:[ \\t]+time::|tasks:)"_s,
                                            QRegularExpression::MultilineOption
                                                    | QRegularExpression::CaseInsensitiveOption);
    const qsizetype pl = polish.matchView(content).capturedStart();
    const qsizetype en = english.matchView(content).capturedStart();
    if (pl < 0 && en < 0)
        return {};
    if (pl < 0)
        return u"en"_s;
    if (en < 0)
        return u"pl"_s;
    return pl < en ? u"pl"_s : u"en"_s;
}

/// The picture embeds of a text: `![[name]]` and `![alt](path)` of the notebook's own pictures.
struct Embed {
    qsizetype start = 0;
    qsizetype length = 0;
    QString name;
};

bool isPictureName(QStringView name)
{
    if (name.isEmpty() || name.contains(u"://") || name.startsWith(u"data:", Qt::CaseInsensitive))
        return false;
    static const QStringList kinds{u".png"_s, u".jpg"_s, u".jpeg"_s, u".gif"_s, u".webp"_s, u".bmp"_s};
    for (const QString &kind : kinds) {
        if (name.endsWith(kind, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

QList<Embed> embedsIn(QStringView markdown)
{
    static const QRegularExpression embed(
            u"!\\[\\[([^\\[\\]|#^\\n]+?)(?:[|#^][^\\[\\]\\n]*)?\\]\\]|!\\[[^\\]\\n]*\\]\\(<?([^)\\s>]+)>?(?:[ \\t]+\"[^\"\\n]*\")?\\)"_s);
    QList<Embed> out;
    FenceTracker fence;
    qsizetype at = 0;
    while (at <= markdown.size()) {
        qsizetype end = markdown.indexOf(u'\n', at);
        if (end < 0)
            end = markdown.size();
        const QStringView line = markdown.mid(at, end - at);
        const bool code = fence.inside();
        fence.feed(line);
        if (!code && fenceOf(line).length == 0 && line.contains(u'!')) {
            for (auto it = embed.globalMatchView(line); it.hasNext();) {
                const auto m = it.next();
                // (Not inside a code span: an odd number of backticks before it.)
                if (line.left(m.capturedStart()).count(u'`') % 2 != 0)
                    continue;
                const QString name = (m.hasCaptured(1) ? m.captured(1) : m.captured(2)).trimmed();
                if (isPictureName(name))
                    out.append({at + m.capturedStart(), m.capturedLength(), name});
            }
        }
        if (end >= markdown.size())
            break;
        at = end + 1;
    }
    return out;
}

/// `markdown` without the embeds of `names` (a line that held nothing else goes too).
QString withoutEmbeds(QStringView markdown, const QStringList &names)
{
    const QList<Embed> embeds = embedsIn(markdown);
    QString out;
    qsizetype at = 0;
    for (const Embed &e : embeds) {
        if (!names.contains(e.name))
            continue;
        out += markdown.mid(at, e.start - at);
        out += QChar(0xFFFF); // (where it was: a line of nothing else goes below)
        at = e.start + e.length;
    }
    out += markdown.mid(at);
    QStringList lines;
    for (const QString &line : out.split(u'\n')) {
        if (!line.contains(QChar(0xFFFF))) {
            lines.append(line);
            continue;
        }
        QString kept = line;
        kept.remove(QChar(0xFFFF));
        while (!kept.isEmpty() && kept.back().isSpace())
            kept.chop(1);
        if (!kept.trimmed().isEmpty())
            lines.append(kept);
    }
    return lines.join(u'\n').trimmed();
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
    // The frontmatter is no message, whatever its lines look like; its `partner:` says who
    // answers.
    if (!lines.isEmpty() && lines.first().trimmed() == u"---") {
        static const QRegularExpression partner(u"^partner:[ \\t]*(.*)$"_s, QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression fork(u"^fork:[ \\t]*[\"']?\\[\\[([^\\[\\]|#]+)(?:[|#][^\\[\\]]*)?\\]\\][\"']?[ \\t]*$"_s,
                                             QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression parent(u"^parent:[ \\t]*(.*)$"_s, QRegularExpression::CaseInsensitiveOption);
        const auto unquoted = [](QString value) {
            value = value.trimmed();
            if (value.size() >= 2 && (value.front() == u'"' || value.front() == u'\'') && value.back() == value.front())
                value = value.mid(1, value.size() - 2).trimmed();
            return value;
        };
        QString named;
        QString from;
        QString forkedFrom;
        for (qsizetype j = 1; j < lines.size(); ++j) {
            if (lines.at(j).trimmed() == u"---" || lines.at(j).trimmed() == u"...") {
                i = j + 1;
                conversation.partner = named;
                conversation.parent = from;
                conversation.fork = forkedFrom;
                break;
            }
            if (const auto m = fork.matchView(lines.at(j)); m.hasMatch())
                forkedFrom = m.captured(1).trimmed();
            if (const auto m = partner.matchView(lines.at(j)); m.hasMatch())
                named = unquoted(m.captured(1));
            if (const auto m = parent.matchView(lines.at(j)); m.hasMatch())
                from = linkTarget(unquoted(m.captured(1)));
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
        // Its time: the line right under its first one (not part of its text).
        QDateTime time;
        if (!open->rest.isEmpty()) {
            time = timeOfLine(open->rest.first());
            if (time.isValid()) {
                open->rest.removeFirst();
                open->restLines.removeFirst();
            }
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
        m.endLine = open->restLines.isEmpty() ? (time.isValid() ? open->line + 1 : open->line) : open->restLines.last();
        m.time = time;
        m.pictures = picturesIn(m.text);
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

QString timeKey(QStringView language)
{
    return language == u"pl" ? u"czas"_s : u"time"_s;
}

QString formatMessage(Role role, QStringView markdown, QStringView newline, const QDateTime &time, QStringView language)
{
    const QString timeLine =
            time.isValid() ? u"  "_s + timeKey(language) + u":: "_s + time.toString(kTimeFormat) + newline : QString();
    QList<QStringView> lines = splitLines(markdown);
    while (!lines.isEmpty() && isBlank(lines.first()))
        lines.removeFirst();
    while (!lines.isEmpty() && isBlank(lines.last()))
        lines.removeLast();
    QString out = u"- "_s + markerOf(role);
    if (lines.isEmpty())
        return out + newline + timeLine;
    FenceTracker fence;
    qsizetype from = 0;
    if (!opensBlock(lines.first().trimmed())) {
        out += u' ' + quietReminders(lines.first().trimmed());
        fence.feed(lines.first());
        from = 1;
    }
    out += newline + timeLine;
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

QString appendMessage(const QString &content, Role role, QStringView markdown, const QDateTime &time,
                      QStringView language)
{
    const QString newline = content.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    QString out = content;
    if (!out.isEmpty() && !out.endsWith(u'\n'))
        out += newline;
    QString lang;
    if (time.isValid()) {
        lang = languageOf(content);
        if (lang.isEmpty())
            lang = language.toString();
    }
    return out + formatMessage(role, markdown, newline, time, lang);
}

QString newConversation(QStringView question, QStringView page, QStringView language, const QDateTime &time,
                        QStringView partner, QStringView parent)
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
    const QString who = partner.trimmed().isEmpty() ? QString() : u"partner: "_s + partner.trimmed() + u'\n';
    // (A link, so a rename of the parent rewrites it; quoted: YAML reads "[[" as a list.)
    QString from = parent.trimmed().toString();
    if (from.endsWith(u".md"_s, Qt::CaseInsensitive))
        from.chop(3);
    from.remove(u'"');
    const QString origin = from.isEmpty() ? QString() : u"parent: \"[["_s + from + u"]]\"\n"_s;
    return u"---\n"_s + property + u'\n' + who + origin + u"---\n"_s
           + formatMessage(Role::User, text, u"\n", time, language == u"pl" ? u"pl" : u"en");
}

QString setPartner(const QString &content, QStringView partner)
{
    const QString value = partner.trimmed().toString();
    const QString newline = content.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    // The frontmatter's lines, with their line breaks (as they are).
    const auto lineEnd = [&](qsizetype from) {
        const qsizetype end = content.indexOf(u'\n', from);
        return end < 0 ? content.size() : end + 1;
    };
    const auto bare = [&](qsizetype from, qsizetype to) {
        QStringView line = QStringView(content).mid(from, to - from);
        while (line.endsWith(u'\n') || line.endsWith(u'\r'))
            line.chop(1);
        return line;
    };
    static const QRegularExpression key(u"^partner:"_s, QRegularExpression::CaseInsensitiveOption);
    if (bare(0, lineEnd(0)).trimmed() == u"---") {
        qsizetype at = lineEnd(0);
        while (at < content.size()) {
            const qsizetype end = lineEnd(at);
            const QStringView line = bare(at, end);
            if (line.trimmed() == u"---" || line.trimmed() == u"...") {
                // Not there yet: before the frontmatter's end.
                if (value.isEmpty())
                    return content;
                return content.left(at) + u"partner: "_s + value + newline + content.mid(at);
            }
            if (key.matchView(line).hasMatch()) {
                if (value.isEmpty())
                    return content.left(at) + content.mid(end);
                const QString ending = content.mid(at + line.size(), end - at - line.size());
                return content.left(at) + u"partner: "_s + value + ending + content.mid(end);
            }
            at = end;
        }
        // (A frontmatter that never closes is no frontmatter: one is made before it.)
    }
    if (value.isEmpty())
        return content;
    return u"---"_s + newline + u"partner: "_s + value + newline + u"---"_s + newline + content;
}

QStringList picturesIn(QStringView markdown)
{
    QStringList out;
    for (const Embed &e : embedsIn(markdown)) {
        if (!out.contains(e.name))
            out.append(e.name);
    }
    return out;
}

QString withoutPictures(QStringView markdown)
{
    return withoutEmbeds(markdown, picturesIn(markdown));
}

QString withPictures(QStringView text, const QStringList &pictures)
{
    QStringList lines;
    if (!text.trimmed().isEmpty())
        lines.append(text.trimmed().toString());
    for (const QString &picture : pictures) {
        if (!picture.trimmed().isEmpty())
            lines.append(u"![["_s + picture.trimmed() + u"]]"_s);
    }
    return lines.join(u'\n');
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

std::pair<QString, QString> splitTitle(QStringView title)
{
    static const QRegularExpression dated(u"^(\\d{4}-\\d{2}-\\d{2})\\s+(.+)$"_s);
    if (const auto m = dated.matchView(title); m.hasMatch())
        return {m.captured(1), m.captured(2)};
    return {QString(), title.toString()};
}

QString journalEntry(QStringView title) { return u"- "_s + kAssistant + u" [["_s + title + u"]]"_s; }

QVariantList history(const Conversation &conversation, int from, bool withPictures)
{
    const QList<Message> &messages = conversation.messages;
    from = std::clamp(from, 0, int(messages.size()));
    // The pictures sent: those of the newest user's messages, MaxPictures at most.
    QHash<int, QStringList> sent;
    if (withPictures) {
        int left = MaxPictures;
        for (int i = int(messages.size()) - 1; i >= from && left > 0; --i) {
            const Message &m = messages.at(i);
            if (m.role != Role::User || m.pictures.isEmpty())
                continue;
            const qsizetype take = std::min<qsizetype>(left, m.pictures.size());
            sent.insert(i, m.pictures.mid(m.pictures.size() - take));
            left -= int(take);
        }
    }
    QVariantList out;
    QString role, content;
    QStringList images;
    const auto flush = [&] {
        if (!content.isEmpty() || !images.isEmpty()) {
            QVariantMap message{{u"role"_s, role}, {u"content"_s, content}};
            if (!images.isEmpty())
                message.insert(u"images"_s, images);
            out.append(message);
        }
        content.clear();
        images.clear();
    };
    bool started = false;
    for (int i = from; i < messages.size(); ++i) {
        const Message &m = messages.at(i);
        const QStringList pictures = sent.value(i);
        const QString text = pictures.isEmpty() ? m.text.trimmed() : withoutEmbeds(m.text, pictures);
        if (text.isEmpty() && pictures.isEmpty())
            continue;
        if (!started && m.role != Role::User)
            continue;
        started = true;
        const QString r = m.role == Role::User ? u"user"_s : u"assistant"_s;
        if (r != role) {
            flush();
            role = r;
        }
        if (!text.isEmpty())
            content += (content.isEmpty() ? QString() : u"\n\n"_s) + text;
        images += pictures;
    }
    flush();
    return out;
}

int cost(const Message &message)
{
    return int(std::min<qsizetype>(message.text.size(), 1 << 28)) + int(message.pictures.size()) * PictureChars;
}

int firstSent(const Conversation &conversation, int budget)
{
    const QList<Message> &messages = conversation.messages;
    if (budget <= 0 || messages.isEmpty())
        return 0;
    // Where each turn starts: a user's message after none or after the AI's.
    QList<int> starts;
    for (int i = 0; i < messages.size(); ++i) {
        if (messages.at(i).role == Role::User && (i == 0 || messages.at(i - 1).role != Role::User))
            starts.append(i);
    }
    if (starts.isEmpty())
        return 0;
    // The cost from each message to the end.
    QList<qint64> tail(messages.size() + 1, 0);
    for (int i = int(messages.size()) - 1; i >= 0; --i)
        tail[i] = tail.at(i + 1) + cost(messages.at(i));
    if (tail.first() <= budget)
        return 0;
    for (const int start : std::as_const(starts)) {
        if (tail.at(start) <= budget)
            return start;
    }
    return starts.last(); // (the latest turn alone, however long)
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

// ---- rewinding and forking ------------------------------------------------------------------------

namespace {

/// Where line `line` (0-based) of `content` starts; content.size() past its last line.
qsizetype lineStart(QStringView content, int line)
{
    qsizetype at = 0;
    for (int i = 0; i < line; ++i) {
        const qsizetype end = content.indexOf(u'\n', at);
        if (end < 0)
            return content.size();
        at = end + 1;
    }
    return at;
}

} // namespace

QString truncated(const QString &content, int from)
{
    const Conversation conversation = parse(content);
    if (from < 0 || from >= conversation.messages.size())
        return content;
    return content.left(lineStart(content, conversation.messages.at(from).line));
}

QString setFrontmatterValue(const QString &content, QStringView key, QStringView value)
{
    const QString name = key.trimmed().toString();
    const QString text = value.trimmed().toString();
    if (name.isEmpty())
        return content;
    const QString newline = content.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    const auto lineEnd = [&](qsizetype from) {
        const qsizetype end = content.indexOf(u'\n', from);
        return end < 0 ? content.size() : end + 1;
    };
    const auto bare = [&](qsizetype from, qsizetype to) {
        QStringView line = QStringView(content).mid(from, to - from);
        while (line.endsWith(u'\n') || line.endsWith(u'\r'))
            line.chop(1);
        return line;
    };
    const QString entry = name + u": "_s + text;
    if (bare(0, lineEnd(0)).trimmed() == u"---") {
        const QRegularExpression own(u"^"_s + QRegularExpression::escape(name) + u"[ \\t]*:"_s,
                                     QRegularExpression::CaseInsensitiveOption);
        qsizetype at = lineEnd(0);
        while (at < content.size()) {
            const qsizetype end = lineEnd(at);
            const QStringView line = bare(at, end);
            if (line.trimmed() == u"---" || line.trimmed() == u"...")
                return text.isEmpty() ? content : content.left(at) + entry + newline + content.mid(at);
            if (own.matchView(line).hasMatch()) {
                if (text.isEmpty())
                    return content.left(at) + content.mid(end);
                return content.left(at) + entry + content.mid(at + line.size(), end - at - line.size())
                       + content.mid(end);
            }
            at = end;
        }
    }
    if (text.isEmpty())
        return content;
    return u"---"_s + newline + entry + newline + u"---"_s + newline + content;
}

QString forkLink(QStringView source)
{
    QString title = source.trimmed().toString();
    if (title.endsWith(u".md"_s, Qt::CaseInsensitive))
        title.chop(3);
    return u"\"[["_s + title + u"]]\""_s;
}

QString forked(const QString &content, int upTo, QStringView source)
{
    const Conversation conversation = parse(content);
    if (upTo < 0 || upTo >= conversation.messages.size())
        return {};
    QString out = content.left(lineStart(content, conversation.messages.at(upTo).endLine + 1));
    if (!out.isEmpty() && !out.endsWith(u'\n'))
        out += content.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    // (A fork is the user's own: no sub-conversation of the source's parent.)
    out = setFrontmatterValue(out, u"parent", {});
    return source.trimmed().isEmpty() ? out : setFrontmatterValue(out, u"fork", forkLink(source));
}

QStringList prompts(const Conversation &conversation)
{
    QStringList out;
    static const QRegularExpression about(u"[ \\t]*\\(\\[\\[[^\\[\\]]+\\]\\]\\)[ \\t]*$"_s);
    for (qsizetype i = conversation.messages.size() - 1; i >= 0; --i) {
        const Message &m = conversation.messages.at(i);
        if (m.role != Role::User)
            continue;
        QString text = withoutPictures(m.text);
        if (i == 0 || std::none_of(conversation.messages.cbegin(), conversation.messages.cbegin() + i,
                                   [](const Message &before) { return before.role == Role::User; })) {
            // (The first question's link to the page it is about went along by itself.)
            const qsizetype end = text.indexOf(u'\n');
            QString first = text.left(end < 0 ? text.size() : end);
            first.remove(about);
            text = end < 0 ? first : first + text.mid(end);
        }
        text = text.trimmed();
        if (!text.isEmpty() && !out.contains(text))
            out.append(text);
    }
    return out;
}

QString quoted(QStringView markdown)
{
    const QString text = withoutPictures(markdown).trimmed();
    if (text.isEmpty())
        return {};
    QStringList lines;
    for (const QStringView line : splitLines(text))
        lines.append(line.trimmed().isEmpty() ? u">"_s : u"> "_s + line.toString());
    return lines.join(u'\n') + u"\n\n"_s;
}

} // namespace brulion::chat
