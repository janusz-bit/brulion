// One entry of the event log and its text form (the file, the export, "copy line").
#pragma once

#include <QList>
#include <QString>
#include <QStringView>
#include <QtGlobal>

#include <optional>

namespace brulion::logging {

enum class Level : quint8 { Debug, Info, Warning, Error, Fatal };

Level levelFromQt(QtMsgType type); ///< Debug, Info, Warning, Critical -> Error, Fatal
QChar levelLetter(Level level);    ///< 'D' 'I' 'W' 'E' 'F'
std::optional<Level> levelFromLetter(QChar letter);
QString levelName(Level level); ///< "debug" "info" "warning" "error" "fatal"
std::optional<Level> levelFromName(QStringView name);

/// Longest message kept (longer ones end with "… [+N chars]").
inline constexpr int kMaxMessageChars = 4000;

struct Entry {
    quint64 seq = 0; ///< 1, 2, 3 … in this process; 0 = read from a file (an earlier session)
    qint64 time = 0; ///< ms since the epoch (an instant; shown in local time)
    Level level = Level::Info;
    QString category;    ///< "brulion.sync", "qt.network.ssl", "qml", "default"
    QString message;     ///< redacted, at most kMaxMessageChars, may contain '\n'
    int repeat = 1;      ///< identical consecutive messages collapsed into this entry
    qint64 lastTime = 0; ///< time of the last repeat (repeat > 1)

    friend bool operator==(const Entry &, const Entry &) = default;
};

/// `message` cut to kMaxMessageChars ("… [+N chars]" appended), never inside a surrogate pair.
QString truncateMessage(const QString &message);

/// Local-time stamp of the log lines: "2026-10-18 14:03:22.517".
QString formatTime(qint64 msecsSinceEpoch);

/// One entry as file / export text, local time:
///   "2026-10-18 14:03:22.517\tW\tbrulion.sync\t#42 PUT Dziennik/2026-10-18.md failed: HTTP 423"
/// Continuation lines of a multi-line message start with "\t"; a collapsed repeat adds a line
///   "2026-10-18 14:05:01.002\tW\tbrulion.sync\t(previous message repeated 36 more times)".
/// No trailing newline.
QString formatLine(const Entry &entry);
/// The repeat note of an entry with repeat > 1 ("" otherwise).
QString formatRepeatLine(const Entry &entry);

/// Parse file text back into entries (seq 0). Never fails: a line that does not start with a
/// timestamp continues the previous entry (or becomes an entry of category "log"). Accepts CRLF,
/// a BOM, truncated last lines; repeat notes become `repeat` of the entry before them.
QList<Entry> parseLines(QStringView text);

} // namespace brulion::logging
