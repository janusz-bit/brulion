// Shared data model of Brulion's core library. Everything in src/core depends only on QtCore
// (QtGui is allowed in src/core/io for HTML conversion). See docs/ARCHITECTURE.md and
// docs/SYNTAX.md (the Markdown dialect — single source of truth for parsing/rendering).
#pragma once

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QAtomicInt>
#include <QVariant>

#include <optional>
#include <utility>

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

/// Page or block properties: lower-case key -> QString or QStringList (frontmatter values may be
/// other QVariants). A QMap (= QVariantMap): most blocks with properties have one or two
/// (`collapsed::`, a Logseq `id::`, a callout's type), and a QHash allocated ~2.9 KB on its first
/// insert (docs/PERFORMANCE.md, L8). Iteration is by key (deterministic), not as written.
using Properties = QMap<QString, QVariant>;

/// An optional value on the heap, shared by copies until one of them changes it (copy on
/// write), read like std::optional. Block::task: most blocks are no tasks, and an in-place
/// std::optional<Task> made every Block ~200 bytes bigger (docs/PERFORMANCE.md, L8). The
/// reference count is atomic: Blocks travel between threads (the index's workers). Non-const
/// access (operator->, operator*, value(), emplace()) detaches a shared value first.
template<typename T>
class Boxed {
public:
    Boxed() noexcept = default;
    Boxed(std::nullopt_t) noexcept { }
    Boxed(const T &value) : m(new Holder(value)) { }
    Boxed(T &&value) : m(new Holder(std::move(value))) { }
    Boxed(const std::optional<T> &value) : m(value ? new Holder(*value) : nullptr) { }
    Boxed(const Boxed &other) noexcept : m(other.m)
    {
        if (m)
            m->ref.ref();
    }
    Boxed(Boxed &&other) noexcept : m(std::exchange(other.m, nullptr)) { }
    ~Boxed() { reset(); }

    Boxed &operator=(const Boxed &other) noexcept
    {
        Boxed copy(other);
        std::swap(m, copy.m);
        return *this;
    }
    Boxed &operator=(Boxed &&other) noexcept
    {
        if (this != &other) {
            reset();
            m = std::exchange(other.m, nullptr);
        }
        return *this;
    }
    Boxed &operator=(std::nullopt_t) noexcept
    {
        reset();
        return *this;
    }
    Boxed &operator=(const T &value) { return *this = Boxed(value); }
    Boxed &operator=(T &&value) { return *this = Boxed(std::move(value)); }
    Boxed &operator=(const std::optional<T> &value) { return *this = Boxed(value); }

    bool has_value() const noexcept { return m != nullptr; }
    explicit operator bool() const noexcept { return m != nullptr; }

    const T &operator*() const { return m->value; }
    const T *operator->() const { return &m->value; }
    const T &value() const { return m->value; }
    T &operator*() { return detached(); }
    T *operator->() { return &detached(); }
    T &value() { return detached(); }

    template<typename... A>
    T &emplace(A &&...args)
    {
        reset();
        m = new Holder(T(std::forward<A>(args)...));
        return m->value;
    }
    void reset() noexcept
    {
        if (m && !m->ref.deref())
            delete m;
        m = nullptr;
    }

    /// A copy as std::optional (TaskRef, ReminderRef, QueryItem keep std::optional<Task>).
    operator std::optional<T>() const { return m ? std::optional<T>(m->value) : std::nullopt; }

    /// Do both hold the same object (no copy was made)? For tests.
    bool isSharedWith(const Boxed &other) const noexcept { return m && m == other.m; }

private:
    struct Holder {
        explicit Holder(const T &v) : value(v) { }
        explicit Holder(T &&v) : value(std::move(v)) { }
        QAtomicInt ref{1};
        T value;
    };
    T &detached()
    {
        if (m->ref.loadRelaxed() != 1) {
            Holder *copy = new Holder(m->value);
            reset();
            m = copy;
        }
        return m->value;
    }

    Holder *m = nullptr;
};


/// A block's own source lines joined with '\n' (Block::raw). Shares the note's text where those
/// lines are one piece of it — consecutive lines each ending in "\n", the usual case: no copy —,
/// else holds its own copy (CRLF or lone-CR endings; own lines interrupted by child blocks, e.g.
/// "- a\n  - b\n  c": a's lines are 0 and 2). Read it as a QStringView (implicit) or toString().
class BlockSource {
public:
    BlockSource() = default;
    /// The block's own copy of its text.
    explicit BlockSource(QString ownText) : m_text(std::move(ownText)) { }
    /// The text document[from, to) — the document is shared, not copied.
    BlockSource(const QString &document, qsizetype from, qsizetype to)
        : m_text(document), m_from(int(from)), m_to(int(to))
    { }

    QStringView view() const
    {
        return m_to < 0 ? QStringView(m_text) : QStringView(m_text).sliced(m_from, m_to - m_from);
    }
    operator QStringView() const { return view(); }
    QString toString() const { return m_to < 0 ? m_text : view().toString(); }
    qsizetype size() const { return m_to < 0 ? m_text.size() : m_to - m_from; }
    bool isEmpty() const { return size() == 0; }
    bool startsWith(QStringView s) const { return view().startsWith(s); }

    /// Is this a slice of the note's text (the index cache stores such raw texts as offsets)?
    bool isSlice() const { return m_to >= 0; }
    qsizetype from() const { return m_to < 0 ? 0 : m_from; }
    qsizetype to() const { return m_to < 0 ? m_text.size() : m_to; }
    /// The QString the text lives in: the whole note for a slice (tests).
    const QString &buffer() const { return m_text; }

    friend bool operator==(const BlockSource &a, const BlockSource &b) { return a.view() == b.view(); }
    friend bool operator==(const BlockSource &a, QStringView b) { return a.view() == b; }

private:
    QString m_text; ///< the whole note (a slice) or the own copy
    int m_from = 0; ///< slices only
    int m_to = -1;  ///< -1: m_text is the block's own copy
};

struct Block {
    QString id;             ///< explicit block id (^abc123 or Logseq id::), empty if none
    BlockKind kind = BlockKind::Paragraph;
    int line = 0;           ///< first line (0-based, inclusive)
    int endLine = 0;        ///< last line incl. children
    int ownEndLine = 0;     ///< last line of the block's own content
    int depth = 0;          ///< outline depth, 0 = top level
    BlockSource raw;        ///< own source lines (without children), usually a slice of the note
    QString text;           ///< content with marker/checkbox/id/properties/tokens stripped
    int headingLevel = 0;
    QString lang;           ///< fenced code language ("calc", "query", ...)
    Properties properties;
    Boxed<Task> task;       ///< on the heap, shared by copies (most blocks are no tasks)
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
    /// Block id -> index into blocks. A QMap: most notes have one or two ids, and a QHash
    /// allocates ~1.8 KB on its first insert (docs/PERFORMANCE.md, L8).
    QMap<QString, int> blockIds;
    std::optional<QDate> journalDate;
};

// ---------------------------------------------------------------------------------------------
// Vault configuration (.brulion/config.json, synced between devices)
// ---------------------------------------------------------------------------------------------

/// How a note is shown and edited (docs/SYNTAX.md §4 "Tryb notatki / Note mode"): a Logseq-like
/// outline of blocks or an Obsidian-like continuous document. See markdown/notemode.h.
enum class NoteMode { Outline, Document };

struct VaultConfig {
    QString journalsFolder = QStringLiteral("journals");
    QString journalFileFormat = QStringLiteral("yyyy-MM-dd");  ///< Qt QDate format string
    QString newPagesFolder;
    QString attachmentsFolder = QStringLiteral("assets");
    QString templatesFolder = QStringLiteral("templates");
    QString journalTemplate = QStringLiteral("templates/journal.md");
    QTime defaultReminderTime = QTime(9, 0);
    Qt::DayOfWeek weekStartsOn = Qt::Monday;
    NoteMode journalMode = NoteMode::Outline; ///< journal pages without an explicit `brulion:` mode
    NoteMode pageMode = NoteMode::Document;   ///< new pages; other pages the content does not decide
};

} // namespace brulion
