// Shared data model of Brulion's core library. Everything in src/core depends only on QtCore
// (QtGui is allowed in src/core/io for HTML conversion). See docs/ARCHITECTURE.md and
// docs/SYNTAX.md (the Markdown dialect — single source of truth for parsing/rendering).
#pragma once

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMap>
#include <QSharedDataPointer>
#include <QString>
#include <QStringBuilder>
#include <QStringList>
#include <QTime>
#include <QVariant>

#include <compare>
#include <iterator>
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
        const QString day = date.toString(QStringLiteral("yyyy-MM-dd"));
        return time ? day + QLatin1Char(' ') + time->toString(QStringLiteral("HH:mm")) : day;
    }
    friend bool operator==(const DateSpec &, const DateSpec &) = default;
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
    /// Obsidian Tasks dependencies (docs/SYNTAX.md §5 "Dependencies"): the task's `🆔 id` (the
    /// first one on its first line; empty if none) and the ids of its `⛔ a,b` tokens (written
    /// order, duplicates removed).
    QString id;
    QStringList dependsOn;
    /// Time tracking (SYNTAX §5 "Time tracking"): seconds of the closed `CLOCK:` entries of the
    /// block's `:LOGBOOK:` drawer (overlapping entries counted once) and the start of a running
    /// one (local wall-clock time; invalid when no clock runs).
    qint64 clockedSeconds = 0;
    QDateTime clockStart;
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

    Boxed &operator=(std::nullopt_t) noexcept
    {
        reset();
        return *this;
    }
    Boxed &operator=(const T &value) { return *this = Boxed(value); }
    Boxed &operator=(T &&value) { return *this = Boxed(std::move(value)); }
    Boxed &operator=(const std::optional<T> &value) { return *this = Boxed(value); }

    bool has_value() const noexcept { return m.constData() != nullptr; }
    explicit operator bool() const noexcept { return has_value(); }

    const T &operator*() const { return m->value; }
    const T *operator->() const { return &m->value; }
    const T &value() const { return m->value; }
    T &operator*() { return m->value; }
    T *operator->() { return &m->value; }
    T &value() { return m->value; }

    template<typename... A>
    T &emplace(A &&...args)
    {
        m.reset(new Holder(T(std::forward<A>(args)...)));
        return m->value;
    }
    void reset() noexcept { m.reset(); }

    /// A copy as std::optional (TaskRef, ReminderRef, QueryItem keep std::optional<Task>).
    operator std::optional<T>() const { return has_value() ? std::optional<T>(m->value) : std::nullopt; }

    /// Do both hold the same object (no copy was made)? For tests.
    bool isSharedWith(const Boxed &other) const noexcept { return has_value() && m.constData() == other.m.constData(); }

private:
    struct Holder : QSharedData {
        explicit Holder(const T &v) : value(v) { }
        explicit Holder(T &&v) : value(std::move(v)) { }
        T value;
    };
    /// Copies share the Holder; QSharedDataPointer's non-const operator->() copies it first when
    /// it is shared (so test it only through constData(): its non-const conversions detach).
    QSharedDataPointer<Holder> m;
};

/// ParsedNote::tasks: the tasks of the note's blocks in document order, each the very object its
/// block's Boxed<Task> holds (no copy: a Task is ~280 bytes, docs/PERFORMANCE.md). Reads like a
/// const QList<Task> — iteration, at(), size(), std::ranges algorithms; toList() copies.
class TaskList {
    using Items = QList<Boxed<Task>>;

public:
    class const_iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using iterator_concept = std::random_access_iterator_tag;
        using value_type = Task;
        using difference_type = qsizetype;
        using pointer = const Task *;
        using reference = const Task &;

        const_iterator() = default;
        explicit const_iterator(Items::const_iterator it) : m_it(it) { }

        reference operator*() const { return **m_it; }
        pointer operator->() const { return &**m_it; }
        reference operator[](difference_type n) const { return *m_it[n]; }
        const_iterator &operator++()
        {
            ++m_it;
            return *this;
        }
        const_iterator operator++(int)
        {
            const_iterator t = *this;
            ++m_it;
            return t;
        }
        const_iterator &operator--()
        {
            --m_it;
            return *this;
        }
        const_iterator operator--(int)
        {
            const_iterator t = *this;
            --m_it;
            return t;
        }
        const_iterator &operator+=(difference_type n)
        {
            m_it += n;
            return *this;
        }
        const_iterator &operator-=(difference_type n)
        {
            m_it -= n;
            return *this;
        }
        friend const_iterator operator+(const_iterator a, difference_type n) { return a += n; }
        friend const_iterator operator+(difference_type n, const_iterator a) { return a += n; }
        friend const_iterator operator-(const_iterator a, difference_type n) { return a -= n; }
        friend difference_type operator-(const_iterator a, const_iterator b) { return a.m_it - b.m_it; }
        friend bool operator==(const_iterator a, const_iterator b) { return a.m_it == b.m_it; }
        friend auto operator<=>(const_iterator a, const_iterator b) { return (a.m_it - b.m_it) <=> 0; }

    private:
        Items::const_iterator m_it{};
    };
    using iterator = const_iterator;
    using value_type = Task;
    using size_type = qsizetype;
    using difference_type = qsizetype;
    using const_reference = const Task &;
    using reference = const Task &;

    TaskList() = default;

    qsizetype size() const noexcept { return m_items.size(); }
    qsizetype count() const noexcept { return m_items.size(); }
    qsizetype length() const noexcept { return m_items.size(); }
    bool isEmpty() const noexcept { return m_items.isEmpty(); }
    bool empty() const noexcept { return m_items.isEmpty(); }

    const Task &at(qsizetype i) const { return *m_items.at(i); }
    const Task &operator[](qsizetype i) const { return *m_items.at(i); }
    const Task &first() const { return *m_items.first(); }
    const Task &last() const { return *m_items.last(); }
    const Task &constFirst() const { return *m_items.constFirst(); }
    const Task &constLast() const { return *m_items.constLast(); }
    const Task &front() const { return first(); }
    const Task &back() const { return last(); }

    const_iterator begin() const { return const_iterator(m_items.cbegin()); }
    const_iterator end() const { return const_iterator(m_items.cend()); }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }
    const_iterator constBegin() const { return begin(); }
    const_iterator constEnd() const { return end(); }

    /// Shares the block's task (nothing when it holds none).
    void append(const Boxed<Task> &task)
    {
        if (task)
            m_items.append(task);
    }
    /// A task of its own (not a block's).
    void append(const Task &task) { m_items.append(Boxed<Task>(task)); }
    void clear() { m_items.clear(); }
    void reserve(qsizetype n) { m_items.reserve(n); }
    void squeeze() { m_items.squeeze(); }

    /// A copy of every task.
    QList<Task> toList() const
    {
        QList<Task> out;
        out.reserve(size());
        for (const Task &t : *this)
            out.append(t);
        return out;
    }
    /// Is task `i` the object `block` holds (no copy)? For tests.
    bool isSharedWith(qsizetype i, const Boxed<Task> &block) const { return m_items.at(i).isSharedWith(block); }

private:
    Items m_items;
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

/// Block::text: a block's content with marker, checkbox, id, properties and tokens stripped.
/// Shares the note's text where it is one piece of it — a line's content, a paragraph without
/// indentation: nearly every block (docs/PERFORMANCE.md) —, else holds its own string. Reads like
/// a const QString: converts implicitly to QString (a slice is copied then: hot paths read view()
/// instead) and to QStringView, has QString's usual const functions, compares with strings and
/// joins them in QStringBuilder expressions (`a + block.text`). A function overloaded for both
/// QString and QStringView (QRegularExpression::match()) needs one named: `block.text.view()`
/// or `toString()`. An empty text is never a slice, so isNull() is as the parser made it.
class BlockText {
public:
    BlockText() = default;
    /// Explicit: `cond ? aQString : block.text` stays a QString (assignment from a QString works).
    explicit BlockText(const QString &own) : m_text(own) { }
    explicit BlockText(QString &&own) noexcept : m_text(std::move(own)) { }
    /// The text document[from, to) — the document is shared, not copied (an empty range: an
    /// empty, not null, text of its own).
    BlockText(const QString &document, qsizetype from, qsizetype to)
    {
        if (to > from) {
            m_text = document;
            m_from = int(from);
            m_to = int(to);
        } else {
            m_text = QStringLiteral("");
        }
    }
    BlockText &operator=(const QString &own)
    {
        m_text = own;
        m_from = 0;
        m_to = -1;
        return *this;
    }
    BlockText &operator=(QString &&own) noexcept
    {
        m_text = std::move(own);
        m_from = 0;
        m_to = -1;
        return *this;
    }

    QStringView view() const
    {
        return m_to < 0 ? QStringView(m_text) : QStringView(m_text).sliced(m_from, m_to - m_from);
    }
    operator QStringView() const { return view(); }
    QString toString() const { return m_to < 0 ? m_text : view().toString(); }
    operator QString() const { return toString(); }

    /// Is this a slice of the note's text (tests, the index cache)?
    bool isSlice() const { return m_to >= 0; }
    qsizetype from() const { return m_to < 0 ? 0 : m_from; }
    qsizetype to() const { return m_to < 0 ? m_text.size() : m_to; }
    /// The QString the text lives in: the whole note for a slice.
    const QString &buffer() const { return m_text; }

    // ---- QString's const API --------------------------------------------------------------------
    qsizetype size() const { return m_to < 0 ? m_text.size() : m_to - m_from; }
    qsizetype length() const { return size(); }
    bool isEmpty() const { return size() == 0; }
    bool isNull() const { return m_to < 0 && m_text.isNull(); }
    QChar at(qsizetype i) const { return view().at(i); }
    QChar operator[](qsizetype i) const { return view().at(i); }
    QChar front() const { return view().front(); }
    QChar back() const { return view().back(); }
    QStringView::const_iterator begin() const { return view().begin(); }
    QStringView::const_iterator end() const { return view().end(); }
    QStringView::const_iterator cbegin() const { return view().cbegin(); }
    QStringView::const_iterator cend() const { return view().cend(); }

    template<typename Needle>
    bool contains(const Needle &s, Qt::CaseSensitivity cs = Qt::CaseSensitive) const
    {
        return view().contains(s, cs);
    }
    template<typename Needle>
    bool startsWith(const Needle &s, Qt::CaseSensitivity cs = Qt::CaseSensitive) const
    {
        return view().startsWith(s, cs);
    }
    template<typename Needle>
    bool endsWith(const Needle &s, Qt::CaseSensitivity cs = Qt::CaseSensitive) const
    {
        return view().endsWith(s, cs);
    }
    template<typename Needle>
    qsizetype indexOf(const Needle &s, qsizetype from = 0, Qt::CaseSensitivity cs = Qt::CaseSensitive) const
    {
        return view().indexOf(s, from, cs);
    }
    template<typename Needle>
    qsizetype lastIndexOf(const Needle &s, Qt::CaseSensitivity cs = Qt::CaseSensitive) const
    {
        return view().lastIndexOf(s, cs);
    }
    template<typename Needle>
    qsizetype count(const Needle &s, Qt::CaseSensitivity cs = Qt::CaseSensitive) const
    {
        return view().count(s, cs);
    }
    int compare(QStringView other, Qt::CaseSensitivity cs = Qt::CaseSensitive) const
    {
        return view().compare(other, cs);
    }

    QString left(qsizetype n) const { return view().left(n).toString(); }
    QString right(qsizetype n) const { return view().right(n).toString(); }
    QString mid(qsizetype pos, qsizetype n = -1) const { return view().mid(pos, n).toString(); }
    QString first(qsizetype n) const { return view().first(n).toString(); }
    QString last(qsizetype n) const { return view().last(n).toString(); }
    QString sliced(qsizetype pos) const { return view().sliced(pos).toString(); }
    QString sliced(qsizetype pos, qsizetype n) const { return view().sliced(pos, n).toString(); }
    QString chopped(qsizetype n) const { return view().chopped(n).toString(); }
    QString trimmed() const { return m_to < 0 ? m_text.trimmed() : view().trimmed().toString(); }
    QString simplified() const { return toString().simplified(); }
    QString toLower() const { return toString().toLower(); }
    QString toUpper() const { return toString().toUpper(); }
    QString toCaseFolded() const { return toString().toCaseFolded(); }
    QString normalized(QString::NormalizationForm form) const { return toString().normalized(form); }
    QStringList split(const QString &sep, Qt::SplitBehavior behavior = Qt::KeepEmptyParts,
                      Qt::CaseSensitivity cs = Qt::CaseSensitive) const
    {
        return toString().split(sep, behavior, cs);
    }
    QStringList split(QChar sep, Qt::SplitBehavior behavior = Qt::KeepEmptyParts,
                      Qt::CaseSensitivity cs = Qt::CaseSensitive) const
    {
        return toString().split(sep, behavior, cs);
    }
    QString section(QChar sep, qsizetype start, qsizetype end = -1,
                    QString::SectionFlags flags = QString::SectionDefault) const
    {
        return toString().section(sep, start, end, flags);
    }
    QString section(const QString &sep, qsizetype start, qsizetype end = -1,
                    QString::SectionFlags flags = QString::SectionDefault) const
    {
        return toString().section(sep, start, end, flags);
    }
    QByteArray toUtf8() const { return view().toUtf8(); }

    // ---- comparisons and concatenation ----------------------------------------------------------
    friend bool operator==(const BlockText &a, const BlockText &b) { return a.view() == b.view(); }
    friend bool operator==(const BlockText &a, const QString &b) { return a.view() == QStringView(b); }
    friend bool operator==(const BlockText &a, QStringView b) { return a.view() == b; }
    friend bool operator==(const BlockText &a, QLatin1StringView b) { return a.view() == b; }
    friend bool operator==(const BlockText &a, const char16_t *b) { return a.view() == QStringView(b); }
    friend bool operator<(const BlockText &a, const BlockText &b) { return a.view() < b.view(); }
    friend bool operator<(const BlockText &a, const QString &b) { return a.view() < QStringView(b); }
    friend bool operator<(const QString &a, const BlockText &b) { return QStringView(a) < b.view(); }

private:
    QString m_text; ///< the whole note (a slice) or the own string
    int m_from = 0; ///< slices only
    int m_to = -1;  ///< -1: m_text is the block's own string
};

struct Block {
    QString id;             ///< explicit block id (^abc123 or Logseq id::), empty if none
    BlockKind kind = BlockKind::Paragraph;
    int line = 0;           ///< first line (0-based, inclusive)
    int endLine = 0;        ///< last line incl. children
    int ownEndLine = 0;     ///< last line of the block's own content
    int depth = 0;          ///< outline depth, 0 = top level
    int headingLevel = 0;
    int parent = -1;        ///< index into ParsedNote::blocks, -1 for roots
    BlockSource raw;        ///< own source lines (without children), usually a slice of the note
    BlockText text;         ///< content with marker/checkbox/id/properties/tokens stripped, usually a slice
    QString lang;           ///< fenced code language ("calc", "query", ...)
    Properties properties;
    Boxed<Task> task; ///< on the heap, shared by copies (most blocks are no tasks)
    QList<Reminder> reminders;
    QStringList tags;       ///< lower-case, without '#'
    QList<int> children;    ///< indices into ParsedNote::blocks
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
    TaskList tasks; ///< the blocks' tasks (shared with Block::task), document order
    QList<Reminder> reminders;
    /// Block id -> index into blocks. A QMap: most notes have one or two ids, and a QHash
    /// allocates ~1.8 KB on its first insert (docs/PERFORMANCE.md, L8).
    QMap<QString, int> blockIds;
    std::optional<QDate> journalDate;
};

// ---------------------------------------------------------------------------------------------
// Vault configuration (.brulion/config.json, synced between devices)
// ---------------------------------------------------------------------------------------------

/// How a note is shown and edited (docs/SYNTAX.md §4 "Note mode"): a Logseq-like
/// outline of blocks or an Obsidian-like continuous document. See markdown/notemode.h.
enum class NoteMode { Outline, Document };

/// The notebook's flashcard settings (config.json "flashcards", docs/SYNTAX.md §15 "Settings of
/// the cards"): the Obsidian Spaced Repetition plugin's own, with its defaults. Only values that
/// differ from them are written to the file.
struct FlashcardsConfig {
    QStringList tags; ///< the tags of decks, lower case without "#"; empty: this device's setting
    QString singleLineSeparator = QStringLiteral("::");
    QString singleLineReversedSeparator = QStringLiteral(":::");
    QString multilineSeparator = QStringLiteral("?");
    QString multilineReversedSeparator = QStringLiteral("??");
    QString multilineEndMarker; ///< empty: a blank line ends a multi-line card
    bool clozeHighlights = true; ///< `==answer==`
    bool clozeBold = false;      ///< `**answer**`
    bool clozeCurly = false;     ///< `{{answer}}`
    bool foldersAsDecks = false; ///< a card's deck is its note's folder
    QStringList reviewTags;      ///< the tags of notes reviewed as a whole, as `tags`; empty: "review"
    // The plugin's note-review algorithm (§15 "Notes to review"), its names and defaults:
    int baseEase = 250;                ///< a new note's ease (130 … 1000)
    double easyBonus = 1.3;            ///< Easy's extra factor (1 … 5)
    double lapsesIntervalChange = 0.5; ///< Hard's factor of the interval (0.1 … 1)
    int maxNoteInterval = 36525;       ///< days (1 … 36525)
    bool loadBalance = true;           ///< a due day moved to a quieter one nearby

    friend bool operator==(const FlashcardsConfig &, const FlashcardsConfig &) = default;
};

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
    // Weekly (ISO week) and monthly notes (periodic/periodic.h, docs/SYNTAX.md §1). A folder key
    // missing from config.json follows journalsFolder.
    QString weeklyFolder = QStringLiteral("journals");
    QString weeklyFileFormat = QStringLiteral("yyyy-'W'ww"); ///< periodic::isUsableFormat(Week): "2026-W45"
    QString weeklyTemplate = QStringLiteral("templates/weekly.md");
    QString monthlyFolder = QStringLiteral("journals");
    QString monthlyFileFormat = QStringLiteral("yyyy-MM"); ///< Qt QDate format of the 1st: "2026-11"
    QString monthlyTemplate = QStringLiteral("templates/monthly.md");
    FlashcardsConfig flashcards;
    /// The notebook's theme page (docs/SYNTAX.md §13 "Theme pages"): its vault path, "" none —
    /// Settings → Appearance picks it, every device that follows the notebook shows it.
    QString themePage;
};

} // namespace brulion

/// `a + block.text` in QStringBuilder expressions (QT_USE_QSTRINGBUILDER): appended without a copy.
template<>
struct QConcatenable<brulion::BlockText> : private QAbstractConcatenable {
    using type = brulion::BlockText;
    using ConvertTo = QString;
    enum { ExactSize = true };
    static qsizetype size(const type &a) { return a.size(); }
    static inline void appendTo(const type &a, QChar *&out)
    {
        const QStringView v = a.view();
        if (!v.isEmpty())
            memcpy(static_cast<void *>(out), v.data(), sizeof(QChar) * size_t(v.size()));
        out += v.size();
    }
};
