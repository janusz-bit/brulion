#include "log/entry.h"

#include <QDateTime>

using namespace Qt::StringLiterals;

namespace brulion::logging {

namespace {

constexpr QStringView kTimeFormat = u"yyyy-MM-dd HH:mm:ss.zzz";
constexpr int kTimeLength = 23; // "2026-10-18 14:03:22.517"

/// "(previous message repeated 36 more times)" -> 36.
std::optional<int> repeatCount(QStringView message)
{
    static constexpr QStringView prefix = u"(previous message repeated ";
    static constexpr QStringView suffix = u" more times)";
    if (!message.startsWith(prefix) || !message.endsWith(suffix))
        return std::nullopt;
    bool ok = false;
    const int n = message.mid(prefix.size(), message.size() - prefix.size() - suffix.size()).toInt(&ok);
    return ok && n > 0 ? std::optional<int>(n) : std::nullopt;
}

std::optional<qint64> parseTime(QStringView text)
{
    if (text.size() != kTimeLength)
        return std::nullopt;
    const QDateTime time = QDateTime::fromString(text, kTimeFormat);
    if (!time.isValid())
        return std::nullopt;
    return time.toMSecsSinceEpoch();
}

/// "<time>\t<L>\t<category>\t", the start of every line of an entry.
QString linePrefix(const Entry &entry, qint64 time)
{
    return formatTime(time) + u'\t' + levelLetter(entry.level) + u'\t'
           + (entry.category.isEmpty() ? u"default"_s : entry.category) + u'\t';
}

} // namespace

Level levelFromQt(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return Level::Debug;
    case QtInfoMsg: return Level::Info;
    case QtWarningMsg: return Level::Warning;
    case QtCriticalMsg: return Level::Error;
    case QtFatalMsg: return Level::Fatal;
    }
    return Level::Info;
}

QChar levelLetter(Level level)
{
    switch (level) {
    case Level::Debug: return u'D';
    case Level::Info: return u'I';
    case Level::Warning: return u'W';
    case Level::Error: return u'E';
    case Level::Fatal: return u'F';
    }
    return u'I';
}

std::optional<Level> levelFromLetter(QChar letter)
{
    switch (letter.unicode()) {
    case u'D': return Level::Debug;
    case u'I': return Level::Info;
    case u'W': return Level::Warning;
    case u'E': return Level::Error;
    case u'F': return Level::Fatal;
    default: return std::nullopt;
    }
}

QString levelName(Level level)
{
    switch (level) {
    case Level::Debug: return u"debug"_s;
    case Level::Info: return u"info"_s;
    case Level::Warning: return u"warning"_s;
    case Level::Error: return u"error"_s;
    case Level::Fatal: return u"fatal"_s;
    }
    return u"info"_s;
}

std::optional<Level> levelFromName(QStringView name)
{
    for (Level level : {Level::Debug, Level::Info, Level::Warning, Level::Error, Level::Fatal}) {
        if (name.compare(levelName(level), Qt::CaseInsensitive) == 0)
            return level;
    }
    return std::nullopt;
}

QString truncateMessage(const QString &message)
{
    if (message.size() <= kMaxMessageChars)
        return message;
    qsizetype cut = kMaxMessageChars;
    if (message.at(cut - 1).isHighSurrogate())
        --cut;
    return message.left(cut) + u"… [+%1 chars]"_s.arg(message.size() - cut);
}

QString formatTime(qint64 msecsSinceEpoch)
{
    return QDateTime::fromMSecsSinceEpoch(msecsSinceEpoch).toString(kTimeFormat);
}

QString formatLine(const Entry &entry)
{
    QString message = entry.message;
    message.replace(u"\r\n"_s, u"\n"_s);
    message.replace(u'\r', u'\n');
    message.replace(u'\n', u"\n\t"_s);
    QString line = linePrefix(entry, entry.time) + message;
    if (entry.repeat > 1)
        line += u'\n' + formatRepeatLine(entry);
    return line;
}

QString formatRepeatLine(const Entry &entry)
{
    if (entry.repeat <= 1)
        return {};
    return linePrefix(entry, entry.lastTime ? entry.lastTime : entry.time)
           + u"(previous message repeated %1 more times)"_s.arg(entry.repeat - 1);
}

QList<Entry> parseLines(QStringView text)
{
    QList<Entry> out;
    // A line that is no entry of its own continues the previous one (or starts one of category "log").
    const auto continueLast = [&out](QStringView line) {
        if (out.isEmpty())
            out.append(Entry{.category = u"log"_s, .message = line.toString()});
        else
            out.last().message.append(u'\n').append(line);
    };
    if (text.startsWith(QChar(0xFEFF)))
        text = text.mid(1);
    qsizetype pos = 0;
    while (pos < text.size()) {
        qsizetype end = text.indexOf(u'\n', pos);
        if (end < 0)
            end = text.size();
        QStringView line = text.mid(pos, end - pos);
        pos = end + 1;
        if (line.endsWith(u'\r'))
            line.chop(1);
        if (line.isEmpty())
            continue;
        if (line.startsWith(u'\t')) {
            continueLast(line.mid(1));
            continue;
        }
        // "<time>\t<L>\t<category>\t<message>"
        const qsizetype t1 = line.indexOf(u'\t');
        const qsizetype t2 = t1 < 0 ? -1 : line.indexOf(u'\t', t1 + 1);
        const qsizetype t3 = t2 < 0 ? -1 : line.indexOf(u'\t', t2 + 1);
        const std::optional<qint64> time = t1 > 0 ? parseTime(line.left(t1)) : std::nullopt;
        const std::optional<Level> level = t2 == t1 + 2 ? levelFromLetter(line.at(t1 + 1)) : std::nullopt;
        if (!time || !level || t3 < 0) {
            continueLast(line);
            continue;
        }
        Entry entry{.time = *time,
                    .level = *level,
                    .category = line.mid(t2 + 1, t3 - t2 - 1).toString(),
                    .message = line.mid(t3 + 1).toString()};
        if (const std::optional<int> n = repeatCount(entry.message);
            n && !out.isEmpty() && out.last().category == entry.category && out.last().level == entry.level) {
            out.last().repeat += *n;
            out.last().lastTime = entry.time;
            continue;
        }
        out.append(std::move(entry));
    }
    return out;
}

} // namespace brulion::logging
