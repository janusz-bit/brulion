// Metadata tokens (docs/SYNTAX.md §4-6): Obsidian Tasks emoji signifiers, Brulion reminders
// (`⏰`, `⏰!`), obsidian-reminder `(@date)`, Logseq priorities `[#A]` and planning items
// (`SCHEDULED: <…>`, `DEADLINE: <…>`), block ids (` ^abc123`). Part of the markdown module —
// see markdown/markdown.h for the module overview.
//
// Token detection runs on *masked* text (code spans / comments blanked, see maskInline in
// markdown/inline.h), so tokens inside inline code are ignored; offsets are valid in the
// original text.
#pragma once

#include "types.h"

#include <QDate>
#include <QList>
#include <QString>
#include <QStringView>
#include <QTime>

#include <optional>

namespace brulion::markdown {

/// Emoji signifiers written by Brulion (Obsidian Tasks compatible) — SYNTAX §5-6.
namespace tokens {
inline constexpr QStringView Due = u"📅";
inline constexpr QStringView Scheduled = u"⏳";
inline constexpr QStringView Start = u"🛫";
inline constexpr QStringView Created = u"➕";
inline constexpr QStringView Done = u"✅";
inline constexpr QStringView Cancelled = u"❌";
inline constexpr QStringView Recurrence = u"🔁";
inline constexpr QStringView Reminder = u"⏰";
inline constexpr QStringView Alarm = u"⏰!";
/// Obsidian Tasks task id / dependency / on-completion signifiers (recognised and stripped).
inline constexpr QStringView TaskId = u"🆔";
inline constexpr QStringView DependsOn = u"⛔";
inline constexpr QStringView OnCompletion = u"🏁";
inline constexpr QStringView PriorityHighest = u"🔺";
inline constexpr QStringView PriorityHigh = u"⏫";
inline constexpr QStringView PriorityMedium = u"🔼";
inline constexpr QStringView PriorityLow = u"🔽";
inline constexpr QStringView PriorityLowest = u"⏬";

/// Emoji of a priority (empty for Priority::None).
QStringView priorityEmoji(Priority priority);
} // namespace tokens

enum class TokenKind {
    Due,            ///< 📅 (also 📆, 🗓)
    Scheduled,      ///< ⏳ (also ⌛)
    Start,          ///< 🛫
    Created,        ///< ➕
    Done,           ///< ✅
    Cancelled,      ///< ❌
    Recurrence,     ///< 🔁 rule text
    Priority,       ///< 🔺 ⏫ 🔼 🔽 ⏬
    Reminder,       ///< ⏰ / ⏰!
    ReminderCompat, ///< (@2026-10-15 09:00)
    TaskId,         ///< 🆔 id
    DependsOn,      ///< ⛔ id1,id2
    OnCompletion,   ///< 🏁 delete
    LogseqPriority, ///< [#A] [#B] [#C]
    Planning,       ///< SCHEDULED: <…> / DEADLINE: <…>
};

/// A metadata token. Offsets are relative to the scanned string.
struct Token {
    TokenKind kind = TokenKind::Due;
    qsizetype from = 0;       ///< start of the whole token (emoji / bracket)
    qsizetype to = 0;         ///< end of the whole token (exclusive)
    qsizetype valueFrom = -1; ///< value range inside the token: date(-time), rule, id(s)
    qsizetype valueTo = -1;
    QDate date;                ///< date tokens, reminders and planning items
    std::optional<QTime> time; ///< optional wall-clock time after the date
    QString value;             ///< rule text (Recurrence), ids (TaskId, DependsOn), action (OnCompletion)
    QString canonical;         ///< Recurrence: canonical English rule; empty when unparseable
    bool alarm = false;        ///< `⏰!`
    brulion::Priority priority = brulion::Priority::None; ///< Priority / LogseqPriority
    TokenKind field = TokenKind::Due;                     ///< Planning: Due (DEADLINE) or Scheduled (SCHEDULED)
    QString repeater;                                     ///< Planning: Logseq repeater such as "+1w" or ".+1d"

    DateSpec spec() const { return DateSpec{date, time}; }
    /// Due, Scheduled, Start, Created, Done, Cancelled, Reminder, ReminderCompat or Planning.
    bool hasDate() const;
};

/// All metadata tokens of `masked` (a line or a block's text with code spans and comments
/// masked), in order, never overlapping. Malformed dates (`📅 2026-13-45`) are not tokens; an
/// invalid time ("25:00") is left out of the token.
QList<Token> findTokens(QStringView masked);

/// Recurrence rule of a Recurrence token: canonical form when parseable, else the raw text.
QString recurrenceOf(const Token &token);

/// A Logseq timestamp `<2026-10-15 Thu 10:00 .+1w>` (the text between the angle brackets).
struct LogseqTimestamp {
    QDate date;
    std::optional<QTime> time;
    QString repeater; ///< "+1w", "++2d", ".+1m" or empty
};
std::optional<LogseqTimestamp> parseLogseqTimestamp(QStringView inner);

/// A block id (` ^abc-123`) at the end of a single line: the id and the range to remove
/// (including the whitespace before the caret).
struct BlockIdMatch {
    QString id;
    qsizetype from = 0;
    qsizetype to = 0;
};
std::optional<BlockIdMatch> blockIdAtEnd(QStringView line);

/// The id when the whole line is just a block id (`  ^tbl1  `), else an empty string.
QString standaloneBlockId(QStringView line);

/// True for a valid block id: one or more of [A-Za-z0-9-].
bool isValidBlockId(QStringView id);

/// Remove all metadata tokens (task emoji with their values, reminders, Logseq priorities and
/// planning items, block ids at line ends) from `text`. Tokens inside inline code are kept.
/// Trailing whitespace of each line and of the whole text is trimmed; line breaks are kept.
QString stripTokens(QStringView text);

/// `⏰ 2026-10-15 09:00` / `⏰! 2026-10-15 06:30` (a spec without time gives `⏰ 2026-10-15`).
QString formatReminderToken(const DateSpec &at, bool alarm = false);

} // namespace brulion::markdown
