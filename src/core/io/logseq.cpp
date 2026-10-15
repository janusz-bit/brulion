#include "io/logseq_p.h"

#include "dates/names.h"
#include "dates/recurrence.h"
#include "io/links_p.h"
#include "markdown/inline.h"
#include "markdown/properties.h"
#include "markdown/tasks.h"
#include "markdown/tokens.h"
#include "text/text.h"

#include <QCryptographicHash>
#include <QLocale>
#include <QUuid>

#include <algorithm>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

const QStringList &monthNamesLong()
{
    static const QStringList names = {u"January"_s,   u"February"_s, u"March"_s,    u"April"_s,
                                      u"May"_s,       u"June"_s,     u"July"_s,     u"August"_s,
                                      u"September"_s, u"October"_s,  u"November"_s, u"December"_s};
    return names;
}

const QStringList &monthNamesShort()
{
    static const QStringList names = {u"Jan"_s, u"Feb"_s, u"Mar"_s, u"Apr"_s, u"May"_s, u"Jun"_s,
                                      u"Jul"_s, u"Aug"_s, u"Sep"_s, u"Oct"_s, u"Nov"_s, u"Dec"_s};
    return names;
}

int monthFromName(const QString &name)
{
    const QString n = name.toLower();
    for (int m = 0; m < 12; ++m) {
        if (monthNamesLong()[m].toLower() == n || monthNamesShort()[m].toLower() == n)
            return m + 1;
    }
    return n == u"sept" ? 9 : 0;
}

// --- block lines -------------------------------------------------------------------------------

struct Bullet {
    qsizetype indentEnd = 0;    ///< end of the indentation
    qsizetype contentStart = 0; ///< after the bullet and the whitespace following it
    int column = 0;             ///< visual column of the bullet (tab = 4)
};

int visualColumn(QStringView line, qsizetype end)
{
    int col = 0;
    for (qsizetype i = 0; i < end && i < line.size(); ++i)
        col = line[i] == u'\t' ? col + 4 - (col % 4) : col + 1;
    return col;
}

qsizetype indentEnd(QStringView line)
{
    qsizetype i = 0;
    while (i < line.size() && (line[i] == u' ' || line[i] == u'\t'))
        ++i;
    return i;
}

bool isThematicBreak(QStringView trimmed)
{
    if (trimmed.size() < 3)
        return false;
    const QChar c = trimmed.front();
    if (c != u'-' && c != u'*' && c != u'_')
        return false;
    int count = 0;
    for (const QChar ch : trimmed) {
        if (ch == c)
            ++count;
        else if (ch != u' ' && ch != u'\t')
            return false;
    }
    return count >= 3;
}

std::optional<Bullet> matchBullet(QStringView line)
{
    const qsizetype i = indentEnd(line);
    if (i >= line.size())
        return std::nullopt;
    const QChar c = line[i];
    if (c != u'-' && c != u'*' && c != u'+')
        return std::nullopt;
    if (i + 1 < line.size() && line[i + 1] != u' ' && line[i + 1] != u'\t')
        return std::nullopt;
    if (isThematicBreak(line.sliced(i).trimmed()))
        return std::nullopt;
    Bullet b;
    b.indentEnd = i;
    b.column = visualColumn(line, i);
    qsizetype j = i + 1;
    while (j < line.size() && (line[j] == u' ' || line[j] == u'\t'))
        ++j;
    b.contentStart = j;
    return b;
}

/// A fence opening at the start of `content` ("```js", "~~~"): the fence run, else empty.
QString fenceOpen(QStringView content)
{
    const QStringView t = content.trimmed();
    if (!t.startsWith(u"```") && !t.startsWith(u"~~~"))
        return {};
    qsizetype n = 0;
    while (n < t.size() && t[n] == t[0])
        ++n;
    return t.first(n).toString();
}

bool isFenceClose(QStringView line, const QString &fence)
{
    const QStringView t = line.trimmed();
    if (!t.startsWith(fence))
        return false;
    for (const QChar c : t) {
        if (c != fence[0])
            return false;
    }
    return true;
}

/// Replace Logseq embed macros outside inline code.
QString convertMacros(const QString &line)
{
    if (!line.contains(u"{{") && !line.contains(u"]([["))
        return line;
    static const QRegularExpression pageEmbed(u"\\{\\{\\s*embed\\s+\\[\\[(.+?)\\]\\]\\s*\\}\\}"_s,
                                              QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression blockEmbed(u"\\{\\{\\s*embed\\s+\\(\\(\\s*([^()\\s]+)\\s*\\)\\)\\s*\\}\\}"_s,
                                               QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression aliasLink(u"\\[([^\\]\\[]+)\\]\\(\\[\\[([^\\]]+)\\]\\]\\)"_s);
    const QString masked = markdown::maskInlineCode(line);
    struct Hit {
        qsizetype from;
        qsizetype to;
        QString text;
    };
    QList<Hit> hits;
    for (auto it = pageEmbed.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        hits.append(Hit{m.capturedStart(), m.capturedEnd(),
                        u"![["_s + line.sliced(m.capturedStart(1), m.capturedLength(1)) + u"]]"_s});
    }
    for (auto it = blockEmbed.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        hits.append(Hit{m.capturedStart(), m.capturedEnd(),
                        u"!(("_s + line.sliced(m.capturedStart(1), m.capturedLength(1)) + u"))"_s});
    }
    for (auto it = aliasLink.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        hits.append(Hit{m.capturedStart(), m.capturedEnd(),
                        u"[["_s + line.sliced(m.capturedStart(2), m.capturedLength(2)) + u'|'
                                + line.sliced(m.capturedStart(1), m.capturedLength(1)) + u"]]"_s});
    }
    if (hits.isEmpty())
        return line;
    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.from < b.from; });
    QString out;
    qsizetype last = 0;
    for (const Hit &h : std::as_const(hits)) {
        if (h.from < last)
            continue;
        out += QStringView(line).sliced(last, h.from - last);
        out += h.text;
        last = h.to;
    }
    out += QStringView(line).sliced(last);
    return out;
}

QString sanitizeBlockId(const QString &id)
{
    QString out;
    for (const QChar c : id) {
        const bool ok = (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'-';
        out += ok ? c : u'-';
    }
    return out;
}

struct Planning {
    std::optional<DateSpec> scheduled;
    std::optional<DateSpec> deadline;
    QString repeater;
};

/// Parse a planning line ("SCHEDULED: <…>", "DEADLINE: <…>", or both). False — and `planning`
/// untouched — unless the whole line is planning with valid timestamps (the line then stays).
bool parsePlanningLine(QStringView trimmed, Planning &planning)
{
    static const QRegularExpression item(u"(SCHEDULED|DEADLINE):\\s*<([^>\\r\\n]*)>"_s);
    static const QRegularExpression whole(u"^(?:(?:SCHEDULED|DEADLINE):\\s*<[^>\\r\\n]*>\\s*)+$"_s);
    if (!whole.matchView(trimmed).hasMatch())
        return false;
    Planning parsed = planning;
    for (auto it = item.globalMatchView(trimmed); it.hasNext();) {
        const auto m = it.next();
        const auto ts = markdown::parseLogseqTimestamp(m.capturedView(2));
        if (!ts || !ts->date.isValid())
            return false;
        const DateSpec spec{ts->date, ts->time};
        if (m.capturedView(1) == u"SCHEDULED") {
            if (!parsed.scheduled)
                parsed.scheduled = spec;
        } else if (!parsed.deadline) {
            parsed.deadline = spec;
        }
        if (parsed.repeater.isEmpty())
            parsed.repeater = ts->repeater;
    }
    planning = parsed;
    return true;
}

/// An `id:: <value>` property line.
const QRegularExpression &idProperty()
{
    static const QRegularExpression re(u"^\\s*id::\\s*(\\S+)\\s*$"_s, QRegularExpression::CaseInsensitiveOption);
    return re;
}

/// A line of paragraph text that a block id can be appended to (not a heading, quote, table,
/// rule, HTML, indented code, property line, or a line that already ends with an id).
bool isParagraphText(QStringView line)
{
    const QStringView t = line.trimmed();
    if (t.isEmpty() || indentEnd(line) >= 4 || isThematicBreak(t))
        return false;
    const QChar c = t.front();
    if (c == u'#' || c == u'>' || c == u'|' || c == u'<')
        return false;
    return !markdown::matchPropertyLine(line) && !markdown::blockIdAtEnd(line);
}

struct BlockSpan {
    qsizetype first = -1;
    QList<qsizetype> rest;  ///< continuation lines
    QList<bool> restIsCode; ///< continuation line inside a fence
    bool firstOpensFence = false;
};

void convertBlock(QList<text::Line> &lines, QList<bool> &removed, const BlockSpan &block)
{
    static const QRegularExpression marker(
            u"^(TODO|LATER|DOING|NOW|IN-PROGRESS|DONE|CANCELED|CANCELLED|WAITING|WAIT)(?=\\s|$)\\s*"_s);
    static const QRegularExpression priorityRe(u"\\[#([ABCabc])\\]\\s?"_s);
    const QRegularExpression &idLine = idProperty();
    static const QRegularExpression collapsedLine(u"^\\s*collapsed::.*$"_s, QRegularExpression::CaseInsensitiveOption);

    if (block.firstOpensFence) {
        // "- ```lang": the item is a code block; its id:: / planning lines (understood by
        // Brulion as they are) stay.
        for (qsizetype k = 0; k < block.rest.size(); ++k) {
            if (!block.restIsCode[k])
                lines[block.rest[k]].text = convertMacros(lines[block.rest[k]].text);
        }
        return;
    }
    QString id;
    Planning planning;
    qsizetype logbookStart = -1; // index into block.rest of an open :LOGBOOK: drawer
    for (qsizetype k = 0; k < block.rest.size(); ++k) {
        if (block.restIsCode[k])
            continue;
        const qsizetype i = block.rest[k];
        const QString &t = lines[i].text;
        const QStringView trimmed = QStringView(t).trimmed();
        if (logbookStart >= 0) {
            removed[i] = true;
            if (trimmed.compare(u":END:", Qt::CaseInsensitive) == 0)
                logbookStart = -1;
            continue;
        }
        if (trimmed.compare(u":LOGBOOK:", Qt::CaseInsensitive) == 0) {
            removed[i] = true;
            logbookStart = k;
            continue;
        }
        if (const auto m = idLine.match(t); m.hasMatch()) {
            if (id.isEmpty())
                id = sanitizeBlockId(m.captured(1));
            removed[i] = true;
            continue;
        }
        if (collapsedLine.match(t).hasMatch()) {
            removed[i] = true;
            continue;
        }
        if (parsePlanningLine(trimmed, planning)) {
            removed[i] = true;
            continue;
        }
        lines[i].text = convertMacros(t);
    }
    // A drawer without :END: is not a drawer: its lines stay.
    for (qsizetype k = logbookStart; k >= 0 && k < block.rest.size(); ++k) {
        if (!block.restIsCode[k])
            removed[block.rest[k]] = false;
    }

    QString &first = lines[block.first].text;
    const auto bullet = matchBullet(first);
    if (!bullet)
        return;
    const QString prefix = first.first(bullet->contentStart);
    QString content = convertMacros(first.sliced(bullet->contentStart));
    QString checkbox;
    QString priority;
    if (const auto m = marker.match(content); m.hasMatch()) {
        if (const auto status = markdown::statusFromKeyword(m.captured(1))) {
            checkbox = u'[' + QString(markdown::statusChar(*status)) + u"] "_s;
            content.remove(0, m.capturedLength());
            const QString masked = markdown::maskInlineCode(content);
            if (const auto pm = priorityRe.match(masked); pm.hasMatch()) {
                const QChar letter = pm.captured(1).toUpper().front();
                const Priority p = letter == u'A' ? Priority::High : letter == u'B' ? Priority::Medium : Priority::Low;
                priority = markdown::tokens::priorityEmoji(p).toString();
                content.remove(pm.capturedStart(), pm.capturedLength());
            }
        }
    }
    // An existing caret id gives way to the Logseq id (references use it).
    if (!id.isEmpty()) {
        if (const auto caret = markdown::blockIdAtEnd(content))
            content = content.first(caret->from);
    }
    while (!content.isEmpty() && content.back().isSpace())
        content.chop(1);
    QStringList suffix;
    if (!priority.isEmpty())
        suffix.append(priority);
    if (planning.scheduled)
        suffix.append(markdown::tokens::Scheduled.toString() + u' ' + planning.scheduled->toString());
    if (planning.deadline)
        suffix.append(markdown::tokens::Due.toString() + u' ' + planning.deadline->toString());
    if (!planning.repeater.isEmpty()) {
        if (const auto rule = dates::repeaterToRule(planning.repeater))
            suffix.append(markdown::tokens::Recurrence.toString() + u' '
                          + dates::formatRecurrence(*rule, dates::UiLocale::English));
    }
    QString line = prefix + checkbox + content;
    if (!suffix.isEmpty())
        line += (content.isEmpty() && checkbox.isEmpty() ? QString() : u" "_s) + suffix.join(u' ');
    if (!id.isEmpty())
        line += u" ^"_s + id;
    first = line;
}

} // namespace

// --- journal titles ------------------------------------------------------------------------------

JournalTitleFormat::JournalTitleFormat(const QString &dateFnsFormat)
{
    static const QList<std::pair<QString, Token>> known = {
            {u"yyyy"_s, Token::Year4},     {u"MMMM"_s, Token::MonthLong}, {u"EEEE"_s, Token::Weekday},
            {u"MMM"_s, Token::MonthShort}, {u"EEE"_s, Token::Weekday},    {u"yy"_s, Token::Year2},
            {u"MM"_s, Token::Month2},      {u"dd"_s, Token::Day2},        {u"do"_s, Token::DayOrdinal},
            {u"EE"_s, Token::Weekday},     {u"M"_s, Token::Month1},       {u"d"_s, Token::Day1},
            {u"E"_s, Token::Weekday},
    };
    QString pattern = u"^"_s;
    const QString f = dateFnsFormat;
    for (qsizetype i = 0; i < f.size();) {
        if (f[i] == u'\'') {
            const qsizetype close = f.indexOf(u'\'', i + 1);
            const QString literal = f.sliced(i + 1, (close < 0 ? f.size() : close) - i - 1);
            m_tokens.append({Token::Literal, literal});
            pattern += QRegularExpression::escape(literal);
            i = close < 0 ? f.size() : close + 1;
            continue;
        }
        bool matched = false;
        for (const auto &[text, token] : known) {
            if (QStringView(f).sliced(i).startsWith(text)) {
                m_tokens.append({token, text});
                switch (token) {
                case Token::Year4: pattern += u"(\\d{4})"_s; break;
                case Token::Year2: pattern += u"(\\d{2})"_s; break;
                case Token::MonthLong: pattern += u"([A-Za-z]{3,9})"_s; break;
                case Token::MonthShort: pattern += u"([A-Za-z]{3,4})"_s; break;
                case Token::Month2:
                case Token::Day2: pattern += u"(\\d{2})"_s; break;
                case Token::Month1:
                case Token::Day1: pattern += u"(\\d{1,2})"_s; break;
                case Token::DayOrdinal: pattern += u"(\\d{1,2})(?:st|nd|rd|th)"_s; break;
                case Token::Weekday: pattern += u"(?:[A-Za-z]{2,9})"_s; break;
                case Token::Literal: break;
                }
                i += text.size();
                matched = true;
                break;
            }
        }
        if (!matched) {
            m_tokens.append({Token::Literal, QString(f[i])});
            pattern += QRegularExpression::escape(QString(f[i]));
            ++i;
        }
    }
    pattern += u"$"_s;
    m_regex = QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
}

std::optional<QDate> JournalTitleFormat::parse(const QString &title) const
{
    const auto m = m_regex.match(title.trimmed());
    if (!m.hasMatch())
        return std::nullopt;
    int year = 0;
    int month = 0;
    int day = 0;
    int group = 1;
    for (const auto &[token, text] : m_tokens) {
        switch (token) {
        case Token::Year4: year = m.captured(group++).toInt(); break;
        case Token::Year2: year = 2000 + m.captured(group++).toInt(); break;
        case Token::MonthLong:
        case Token::MonthShort: month = monthFromName(m.captured(group++)); break;
        case Token::Month2:
        case Token::Month1: month = m.captured(group++).toInt(); break;
        case Token::DayOrdinal:
        case Token::Day2:
        case Token::Day1: day = m.captured(group++).toInt(); break;
        case Token::Weekday:
        case Token::Literal: break;
        }
    }
    const QDate date(year, month, day);
    if (!date.isValid())
        return std::nullopt;
    return date;
}

QString JournalTitleFormat::format(QDate date) const
{
    QString out;
    for (const auto &[token, text] : m_tokens) {
        switch (token) {
        case Token::Year4: out += QString::number(date.year()).rightJustified(4, u'0'); break;
        case Token::Year2: out += QString::number(date.year() % 100).rightJustified(2, u'0'); break;
        case Token::MonthLong: out += monthNamesLong()[date.month() - 1]; break;
        case Token::MonthShort: out += monthNamesShort()[date.month() - 1]; break;
        case Token::Month2: out += QString::number(date.month()).rightJustified(2, u'0'); break;
        case Token::Month1: out += QString::number(date.month()); break;
        case Token::DayOrdinal: out += dates::englishOrdinal(date.day()); break;
        case Token::Day2: out += QString::number(date.day()).rightJustified(2, u'0'); break;
        case Token::Day1: out += QString::number(date.day()); break;
        case Token::Weekday:
            out += dates::weekdayName(date.dayOfWeek(), dates::UiLocale::English,
                                      text == u"EEEE" ? dates::NameForm::Nominative : dates::NameForm::Short);
            break;
        case Token::Literal: out += text; break;
        }
    }
    return out;
}

std::optional<QDate> parseJournalTitle(const QString &title, const QList<JournalTitleFormat> &formats)
{
    for (const JournalTitleFormat &f : formats) {
        if (const auto d = f.parse(title))
            return d;
    }
    const QDate iso = QDate::fromString(title.trimmed(), u"yyyy-MM-dd"_s);
    if (iso.isValid())
        return iso;
    return std::nullopt;
}

// --- page properties -----------------------------------------------------------------------------

LogseqPageProperties takeLogseqPageProperties(const QString &content)
{
    LogseqPageProperties out;
    const QList<text::Line> lines = text::splitLines(content);
    const auto add = [&](QStringView line) -> bool {
        const auto pm = markdown::matchPropertyLine(line);
        if (!pm)
            return false;
        const QString key = pm->key.toLower();
        if (!out.values.contains(key))
            out.keys.append(key);
        out.values.insert(key, line.sliced(pm->valueFrom, pm->valueTo - pm->valueFrom).trimmed().toString());
        return true;
    };
    qsizetype i = 0;
    while (i < lines.size() && markdown::matchPropertyLine(lines[i].text))
        add(lines[i++].text);
    if (i == 0) {
        // A first bullet made only of properties.
        const auto bullet = lines.isEmpty() ? std::nullopt : matchBullet(lines[0].text);
        if (!bullet || bullet->indentEnd != 0
            || !markdown::matchPropertyLine(QStringView(lines[0].text).sliced(bullet->contentStart)))
            return LogseqPageProperties{{}, {}, content};
        qsizetype j = 1;
        while (j < lines.size() && !lines[j].text.trimmed().isEmpty() && !matchBullet(lines[j].text)) {
            if (!markdown::matchPropertyLine(QStringView(lines[j].text).trimmed()))
                return LogseqPageProperties{{}, {}, content};
            ++j;
        }
        add(QStringView(lines[0].text).sliced(bullet->contentStart));
        for (qsizetype k = 1; k < j; ++k)
            add(QStringView(lines[k].text).trimmed());
        i = j;
    }
    while (i < lines.size() && lines[i].text.trimmed().isEmpty())
        ++i;
    out.body = i < lines.size() ? content.sliced(lines[i].start) : QString();
    return out;
}

// --- blocks --------------------------------------------------------------------------------------

QString logseqBlocksToBrulion(const QString &content)
{
    QList<text::Line> lines = text::splitLines(content);
    QList<bool> removed(lines.size(), false);
    QString fence; // open fence run, empty outside code
    BlockSpan block;
    int blockColumn = -1;
    qsizetype paragraphLine = -1; // the previous line, when it is text of a top-level paragraph
    const auto finish = [&] {
        if (block.first >= 0)
            convertBlock(lines, removed, block);
        block = BlockSpan();
        blockColumn = -1;
    };
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString &t = lines[i].text;
        if (!fence.isEmpty()) {
            if (block.first >= 0) {
                block.rest.append(i);
                block.restIsCode.append(true);
            }
            if (isFenceClose(t, fence))
                fence.clear();
            continue;
        }
        if (const auto bullet = matchBullet(t)) {
            finish();
            paragraphLine = -1;
            block.first = i;
            blockColumn = bullet->column;
            fence = fenceOpen(QStringView(t).sliced(bullet->contentStart));
            block.firstOpensFence = !fence.isEmpty();
            continue;
        }
        const bool blank = t.trimmed().isEmpty();
        const qsizetype ind = indentEnd(t);
        if (block.first >= 0 && !blank && visualColumn(t, ind) > blockColumn) {
            paragraphLine = -1;
            fence = fenceOpen(t);
            block.rest.append(i);
            block.restIsCode.append(!fence.isEmpty());
            continue;
        }
        finish();
        if (blank) {
            paragraphLine = -1;
            continue;
        }
        fence = fenceOpen(t);
        if (!fence.isEmpty()) {
            paragraphLine = -1;
            continue;
        }
        // `id:: x` right after a paragraph line -> " ^x" on that line (the block id syntax of
        // Brulion and Obsidian; Brulion writes paragraph ids at the end of the last line).
        if (const auto m = idProperty().match(t); m.hasMatch() && ind == 0 && paragraphLine >= 0) {
            lines[paragraphLine].text += u" ^"_s + sanitizeBlockId(m.captured(1));
            removed[i] = true;
            paragraphLine = -1;
            continue;
        }
        lines[i].text = convertMacros(t);
        paragraphLine = isParagraphText(lines[i].text) ? i : -1;
    }
    finish();
    QString out;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (!removed[i])
            out += lines[i].text + lines[i].eol;
    }
    return out;
}

// --- names ---------------------------------------------------------------------------------------

QString logseqPageNameFromFile(const QString &fileName)
{
    QString name = fileName;
    for (const QString &ext : {u".md"_s, u".markdown"_s, u".org"_s}) {
        if (name.endsWith(ext, Qt::CaseInsensitive)) {
            name.chop(ext.size());
            break;
        }
    }
    name.replace(u"___"_s, u"/"_s);
    return percentDecode(name);
}

QString logseqFileNameForPage(const QString &pageName)
{
    QString out;
    const QString name = QString(pageName).replace(u'/', u"___"_s);
    for (qsizetype i = 0; i < name.size(); ++i) {
        const QChar c = name[i];
        const bool reserved = c.unicode() < 0x20 || c == u'<' || c == u'>' || c == u':' || c == u'"' || c == u'|'
                              || c == u'?' || c == u'*' || c == u'\\' || c == u'%' || c == u'#'
                              || (i == 0 && c == u'.');
        if (reserved)
            out += u'%' + QString::number(c.unicode(), 16).toUpper().rightJustified(2, u'0');
        else
            out += c;
    }
    return out;
}

QString uuidFromSeed(const QString &seed)
{
    QByteArray hash = QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha1).first(16);
    hash[6] = char((static_cast<unsigned char>(hash[6]) & 0x0F) | 0x40);
    hash[8] = char((static_cast<unsigned char>(hash[8]) & 0x3F) | 0x80);
    return QUuid::fromRfc4122(hash).toString(QUuid::WithoutBraces);
}

QString logseqTimestamp(const DateSpec &spec, const QString &repeater)
{
    QString out = u'<' + spec.date.toString(u"yyyy-MM-dd"_s) + u' '
                  + QLocale::c().dayName(spec.date.dayOfWeek(), QLocale::ShortFormat);
    if (spec.time)
        out += u' ' + spec.time->toString(u"HH:mm"_s);
    if (!repeater.isEmpty())
        out += u' ' + repeater;
    return out + u'>';
}

} // namespace brulion::io::detail
