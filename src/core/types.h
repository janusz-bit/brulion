// Shared data model of Brulion's core library. Everything in src/core depends only on QtCore
// (QtGui is allowed in src/core/io for HTML conversion). See docs/ARCHITECTURE.md and
// docs/SYNTAX.md (the Markdown dialect — single source of truth for parsing/rendering).
#pragma once

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QVariant>

#include <optional>

namespace brulion {

/// POSIX-style path relative to the vault root, no leading slash: "journals/2026-10-14.md".
using VaultPath = QString;

/// A date as written in a note, optionally with a wall-clock time ("2026-10-15" or
/// "2026-10-15 09:00"). No time zone: reminders fire in local wall-clock time.
struct DateSpec {
    QDate date;
    std::optional<QTime> time;

    bool isValid() const { return date.isValid(); }
    bool hasTime() const { return time.has_value(); }
    /// Local QDateTime (time 00:00 when no time is given).
    QDateTime toLocalDateTime() const { return QDateTime(date, time.value_or(QTime(0, 0))); }
    /// "YYYY-MM-DD" or "YYYY-MM-DD HH:mm" (the canonical on-disk form).
    QString toString() const
    {
        return time ? date.toString(QStringLiteral("yyyy-MM-dd")) + QLatin1Char(' ')
                          + time->toString(QStringLiteral("HH:mm"))
                    : date.toString(QStringLiteral("yyyy-MM-dd"));
    }
    friend bool operator==(const DateSpec &a, const DateSpec &b) { return a.date == b.date && a.time == b.time; }
};

// ---------------------------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------------------------

struct FileStat {
    VaultPath path;
    bool isDir = false;
    qint64 size = 0;
    /// Modification time, ms since epoch.
    qint64 mtime = 0;
};

enum class ChangeType { Created, Modified, Deleted, Renamed };
enum class ChangeSource { User, External, Sync, Import, System };

struct VaultChange {
    ChangeType type = ChangeType::Modified;
    VaultPath path;
    VaultPath oldPath; ///< only for Renamed
    ChangeSource source = ChangeSource::User;
};

// ---------------------------------------------------------------------------------------------
// Parsed Markdown (src/core/markdown)
// ---------------------------------------------------------------------------------------------

enum class BlockKind { ListItem, Paragraph, Heading, Code, Table, Quote, Callout, Rule, Html, Frontmatter };

enum class TaskStatus { Todo, Doing, Done, Cancelled, Deferred };
enum class Priority { None, Lowest, Low, Medium, High, Highest };

struct Task {
    TaskStatus status = TaskStatus::Todo;
    QString marker;  ///< as written: " ", "x", "/", "-", ">", "TODO", "DONE", ...
    QString text;    ///< text without checkbox and metadata tokens
    int line = 0;
    std::optional<DateSpec> due, scheduled, start;
    std::optional<QDate> created, doneDate, cancelledDate;
    QString recurrence;  ///< canonical English rule ("every week on Monday"), empty if none
    Priority priority = Priority::None;
};

struct Reminder {
    DateSpec at;          ///< always has a time after parsing (default time from VaultConfig)
    bool alarm = false;   ///< "⏰!" — loud, persistent
    QString recurrence;   ///< canonical rule from a 🔁 token in the same block
    QString text;         ///< block text with tokens stripped (notification body)
    int line = 0;
};

enum class LinkKind { Wiki, Embed, BlockRef, BlockEmbed, Markdown, Url };

struct Link {
    LinkKind kind = LinkKind::Wiki;
    QString target;   ///< page name / path / url / block id (for block refs)
    QString display;  ///< alias after '|' or markdown link text
    QString heading;  ///< after '#'
    QString blockId;  ///< after '#^', or the id of a ((ref))
    int line = 0;
    int from = 0, to = 0;  ///< character offsets in the whole document (UTF-16 code units)
};

struct Heading {
    int level = 1;
    QString text;
    int line = 0;
};

using Properties = QHash<QString, QVariant>;  ///< QString or QStringList values

struct Block {
    QString id;             ///< explicit block id (^abc123 or Logseq id::), empty if none
    BlockKind kind = BlockKind::Paragraph;
    int line = 0;           ///< first line (0-based, inclusive)
    int endLine = 0;        ///< last line incl. children
    int ownEndLine = 0;     ///< last line of the block's own content
    int depth = 0;          ///< outline depth, 0 = top level
    QString raw;            ///< own source lines (without children)
    QString text;           ///< content with marker/checkbox/id/properties/tokens stripped
    int headingLevel = 0;
    QString lang;           ///< fenced code language ("calc", "query", ...)
    Properties properties;
    std::optional<Task> task;
    QList<Reminder> reminders;
    QStringList tags;       ///< lower-case, without '#'
    QList<int> children;    ///< indices into ParsedNote::blocks
    int parent = -1;        ///< index into ParsedNote::blocks, -1 for roots
};

struct ParsedNote {
    VaultPath path;
    QString title;                ///< file base name without .md
    QVariantMap frontmatter;
    Properties properties;        ///< page-level properties
    QStringList aliases;
    QStringList tags;             ///< page tags + all block tags, lower-case, deduplicated
    QList<Block> blocks;          ///< all blocks in document order (pre-order); tree via parent/children
    QList<Link> links;
    QList<Heading> headings;
    QList<Task> tasks;
    QList<Reminder> reminders;
    QHash<QString, int> blockIds; ///< block id -> index into blocks
    std::optional<QDate> journalDate;
};

// ---------------------------------------------------------------------------------------------
// Vault configuration (.brulion/config.json, synced between devices)
// ---------------------------------------------------------------------------------------------

struct VaultConfig {
    QString journalsFolder = QStringLiteral("journals");
    QString journalFileFormat = QStringLiteral("yyyy-MM-dd");  ///< Qt QDate format string
    QString newPagesFolder;
    QString attachmentsFolder = QStringLiteral("assets");
    QString templatesFolder = QStringLiteral("templates");
    QString journalTemplate = QStringLiteral("templates/journal.md");
    QTime defaultReminderTime = QTime(9, 0);
    Qt::DayOfWeek weekStartsOn = Qt::Monday;
};

} // namespace brulion
