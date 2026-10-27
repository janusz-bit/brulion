// Edits of single metadata tokens and journal links of a block body (docs/SYNTAX.md §2, §5-6):
// what the token menus of the editor, query results, embeds, table cells, Tasks and the
// calendar write (docs/UI.md "Token and link menus"). Part of the markdown module — see
// markdown/markdown.h for the module overview.
//
// Everything is "text in -> text out": only the bytes of the edited token change (its value, or
// the token with one adjacent space when it is removed); a token that is not where it was any
// more leaves the text unchanged. A body is BlockEntry::body (lines joined by "\n"); ranges are
// [from, to) in UTF-16 code units of the body — a chip's range as the renderer's `date:`,
// `reminder:`, `recur:`, `prio:` and `day:` links name it (inlinerenderer.h).
//
// Across copies of a block (an item of a query result, a task of the Tasks view, a calendar
// entry: the block is in another file, its offsets there may differ) a token is named by a
// TokenRef — its kind, its value and which of the equal ones it is — and editBlockToken() makes
// the edit in the file's text at the block's line, byte for byte everywhere else (CRLF too).
#pragma once

#include "dates/recurrence.h"
#include "markdown/tokens.h"
#include "types.h"

#include <QDate>
#include <QString>
#include <QTime>
#include <QVariant>

#include <optional>
#include <utility>

namespace brulion::markdown {

/// Replace the token at [from, to) of `body` with `token`; an empty `token` removes it together
/// with one adjacent space. Out-of-range ranges leave the body unchanged.
QString replaceTokenRange(const QString &body, qsizetype from, qsizetype to, const QString &token);

/// Range [from, to) of the first token of `kind` on the first line, or nullopt (tokens inside
/// inline code are ignored).
std::optional<std::pair<qsizetype, qsizetype>> findFirstLineToken(const QString &body, TokenKind kind);

/// Set the token of `kind` on the body's first line: the first existing token of that kind is
/// replaced (removed when `token` is empty); without one, `token` is appended to the first line
/// (before a trailing block id) — always appended when `append` is true (reminders: a block may
/// carry several). Tokens inside inline code are ignored.
QString setFirstLineToken(const QString &body, TokenKind kind, const QString &token, bool append = false);

/// The date token at body range [from, to) — a chip's range: 📅 ⏳ 🛫 ➕ ✅ ❌ or a reminder
/// (⏰, ⏰!, `(@…)`), not a Logseq planning line —, its positions in `body`; nullopt when the
/// range holds none (the text changed since the chip was drawn).
std::optional<Token> dateTokenAt(const QString &body, qsizetype from, qsizetype to);

/// Any token with a menu at [from, to): a date token as dateTokenAt() finds it, a 🔁 rule (its
/// range ends where the rule does: "🔁 co tydzień" of "🔁 co tydzień kupić mleko" — the value is
/// the rule's text, `canonical` its canonical form), a priority (🔺 ⏫ 🔼 🔽 ⏬ or Logseq `[#A]`).
/// Positions are the body's; nullopt when the range holds none of them.
std::optional<Token> tokenAt(const QString &body, qsizetype from, qsizetype to);

/// Give the date token at [from, to) another date (and time, or none): only its value is
/// rewritten — the emoji and everything around it stay byte for byte. No token there: unchanged.
QString setTokenDate(const QString &body, qsizetype from, qsizetype to, const DateSpec &spec);

/// Turn the reminder at [from, to) into an alarm (`⏰!`) or back into a plain reminder (`⏰`); a
/// `(@…)` reminder is written as `⏰` / `⏰!`. No reminder there: unchanged.
QString setReminderAlarm(const QString &body, qsizetype from, qsizetype to, bool alarm);

/// `spec` moved by `days` and `minutes` (wall-clock time, never through UTC): a date without a time
/// moves by whole days only, unless `minutes` is non-zero — then it gets `defaultTime` first.
DateSpec shiftedSpec(const DateSpec &spec, int days, int minutes, QTime defaultTime);

/// The recurrence rule of a 🔁 token (`tok` found in `masked`) and where its text ends: the whole
/// value when it parses, else the longest run of leading words that does ("co tydzień" of
/// "co tydzień kupić mleko"); nullopt when no rule starts there (the token is text).
std::optional<std::pair<dates::RecurrenceRule, qsizetype>> recurrencePrefix(QStringView masked, const Token &tok);

/// The 🔁 rule at [from, to) becomes `rule` (any phrasing, written canonical — "co tydzień" ->
/// "every week"; text that is no rule is written as given); "" removes the rule (with one
/// adjacent space; text after the rule stays). No rule there: unchanged.
QString setRecurrenceAt(const QString &body, qsizetype from, qsizetype to, const QString &rule);
/// The block's 🔁 rule (on its first line) becomes `rule` as setRecurrenceAt() writes it, added at
/// the end of the first line when the block has none; "" removes it. The same rule in another
/// phrasing ("co tydzień" for "every week") leaves the body unchanged.
QString setBlockRecurrence(const QString &body, const QString &rule);

/// The block's 🔁 rule (the first on its first line) as the menus show it: the canonical rule at
/// the start of the token's text ("every week" of "🔁 co tydzień kupić mleko"), else the token's
/// text as written; "" when the block has none.
QString blockRecurrence(const QString &body);
/// The block's priority (the first on its first line, emoji or Logseq `[#A]`) and whether it is
/// a Logseq one; {None, false} when it has none.
std::pair<Priority, bool> blockPriority(const QString &body);

/// "highest", "high", "medium", "low", "lowest"; "" for none — and back (unknown: None).
QString priorityName(Priority priority);
Priority priorityFromName(QStringView name);

/// The priority at [from, to) becomes `priority`: an emoji token gets the emoji of the new one, a
/// Logseq `[#A]` / `[#B]` / `[#C]` stays Logseq (highest and high -> A, medium -> B, low and lowest
/// -> C); None removes the token. No priority there: unchanged.
QString setPriorityAt(const QString &body, qsizetype from, qsizetype to, Priority priority);
/// The block's priority (the first one on its first line, emoji or Logseq) set as setPriorityAt()
/// does; a block without one gets the emoji appended to its first line.
QString setBlockPriority(const QString &body, Priority priority);

/// A wiki link to a daily note at [from, to) (`[[2026-10-20]]`, `[[20.10.2026|wtorek]]`,
/// `![[2026-10-20]]`; journalDateFromLinkTarget() decides): its day, range and target as written.
struct JournalLinkMatch {
    QDate date;
    qsizetype from = 0;
    qsizetype to = 0;
    QString target;
};
std::optional<JournalLinkMatch> journalLinkAt(const QString &body, qsizetype from, qsizetype to,
                                              const VaultConfig &config);
/// The journal link at [from, to) names `date` instead: only the date part of its target changes
/// (written in the format it was written in: the vault's journalFileFormat, "yyyy-MM-dd" or
/// "yyyy_MM_dd"; a folder or ".md" kept) — `|alias`, `#heading`, `#^id` and a leading `!` stay.
/// No journal link there or an invalid date: unchanged.
QString setJournalLinkDate(const QString &body, qsizetype from, qsizetype to, QDate date,
                           const VaultConfig &config);

/// What the token edits need besides the text (they never read the clock).
struct TokenEditContext {
    QDate today;
    QTime defaultReminderTime = QTime(9, 0);
    VaultConfig config;
};

/// One edit of a token menu on the token (or journal link) at [from, to) of `body`; `a` and `b`
/// are the action's arguments:
///
///   "set"             a: "YYYY-MM-DD[ HH:mm]" — a date token's or reminder's value, a journal
///                     link's day; "" removes a date token
///   "move"            to today + a days (the time kept)
///   "shift"           a days and b minutes later (shiftedSpec(); a reminder without a time
///                     starts from the default reminder time)
///   "alarm"           a reminder becomes an alarm (a: true) or a plain reminder
///   "reminder"        a reminder gets a ("YYYY-MM-DD HH:mm") and b { alarm, recurrence? }: the
///                     reminder dialog — `recurrence` (when present) is the block's 🔁 rule
///   "remove"          the token goes (with one adjacent space)
///   "recurrence"      a 🔁 rule becomes a (setRecurrenceAt(); "" removes it)
///   "priority"        a priority becomes a (a priority name, "" removes it)
///   "blockRecurrence" the block's 🔁 rule becomes a (the reminder menu's "Powtarzanie")
///   "blockPriority"   the block's priority becomes a (the reminder menu's "Priorytet")
///
/// Unknown actions, invalid arguments or a range that holds no such token: `body` unchanged.
QString applyTokenAction(const QString &body, qsizetype from, qsizetype to, const QString &action,
                         const QVariant &a, const QVariant &b, const TokenEditContext &context);

/// A token named across copies of its block (see the file comment).
struct TokenRef {
    /// Reminder for `⏰`, `⏰!` and `(@…)` alike, Priority for emoji and Logseq priorities.
    TokenKind kind = TokenKind::Due;
    /// Dates and reminders: DateSpec::toString() of the written value ("2026-10-20 09:00"); 🔁:
    /// recurrenceOf() of the whole token; priorities: priorityName().
    QString value;
    /// Which of the block's tokens of that kind and value it is (0 = the first).
    int ordinal = 0;
    /// Reminders written without a time also match `value` with this time (the index fills in
    /// the default reminder time); invalid: only the written value matches.
    QTime defaultTime;
    friend bool operator==(const TokenRef &, const TokenRef &) = default;
};

/// The TokenRef of the token tokenAt() finds at [from, to); nullopt when there is none.
std::optional<TokenRef> tokenRefAt(const QString &body, qsizetype from, qsizetype to);
/// Where the token `ref` names is in `body` now: its chip range (a 🔁 rule's ends with the rule);
/// nullopt when the body has no such token.
std::optional<std::pair<qsizetype, qsizetype>> findTokenRef(const QString &body, const TokenRef &ref);

/// applyTokenAction() on the token `ref` names in the block on `line` (0-based) of a whole file's
/// `content`. Only the edited line changes, byte for byte apart from the token (its line ending,
/// indentation and every other line kept). `content` unchanged when the line holds no block, the
/// block has no such token or the action changes nothing.
QString editBlockToken(const QString &content, int line, const TokenRef &ref, const QString &action,
                       const QVariant &a, const QVariant &b, const TokenEditContext &context);

} // namespace brulion::markdown
