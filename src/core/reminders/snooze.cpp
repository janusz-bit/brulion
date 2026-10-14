// Snoozing reminders (see snooze.h).
#include "reminders/snooze.h"

#include "markdown/blockdocument.h"
#include "markdown/inline.h"
#include "markdown/parse.h"
#include "markdown/tokens.h"
#include "reminders/reminders_p.h"
#include "text/text.h"

#include <algorithm>

namespace brulion {

using namespace Qt::StringLiterals;

namespace {

/// The reminder token of `line` to snooze: its first `⏰`/`⏰!` token, else its first `(@…)`
/// token — among the tokens the parser read as reminders of this line (`onLine`), so not one in
/// inline code or in a comment opened on an earlier line.
std::optional<markdown::Token> reminderToken(const QString &line, const QList<Reminder> &onLine)
{
    const auto parsed = [&onLine](const markdown::Token &t) {
        return std::any_of(onLine.cbegin(), onLine.cend(), [&t](const Reminder &r) {
            return r.alarm == t.alarm && r.at.date == t.date && (!t.time || r.at.time == t.time);
        });
    };
    const QList<markdown::Token> tokens = markdown::findTokens(markdown::maskInlineCode(line));
    std::optional<markdown::Token> compat;
    for (const markdown::Token &t : tokens) {
        if (t.kind == markdown::TokenKind::Reminder && parsed(t))
            return t;
        if (t.kind == markdown::TokenKind::ReminderCompat && !compat && parsed(t))
            compat = t;
    }
    return compat;
}

/// One-off reminder: rewrite the token in place.
QString replaceToken(const QString &content, const text::Line &line, const markdown::Token &token, const DateSpec &at)
{
    const QString replacement = token.kind == markdown::TokenKind::Reminder
                                        ? markdown::formatReminderToken(at, token.alarm)
                                        : u"(@"_s + at.toString() + u')';
    QString out = content;
    out.replace(line.start + token.from, token.to - token.from, replacement);
    return out;
}

/// Recurring reminder: add a one-off `- ⏰ at` bullet as the first child of the block at `line`
/// (or after a non-list block).
QString insertOneOff(const QString &content, int line, const DateSpec &at, bool alarm)
{
    markdown::BlockDocument doc(content);
    const int i = doc.indexAtLine(line);
    if (i < 0)
        return content;
    const markdown::BlockEntry &entry = doc.at(i);
    const QString body = markdown::formatReminderToken(at, alarm);
    const markdown::EditResult edit = entry.kind == BlockKind::ListItem ? doc.insertAfter(i, body, entry.depth + 1)
                                                                        : doc.insertAfter(i, body, 0, true);
    return edit.changed() ? doc.toString() : content;
}

} // namespace

QString snoozeReminder(const QString &content, int line, const QDateTime &newAt)
{
    if (!newAt.isValid() || line < 0)
        return content;
    const QList<text::Line> lines = text::splitLines(content);
    if (line >= lines.size())
        return content;

    // Only reminders the parser sees count (not tokens in code blocks, frontmatter, comments).
    const ParsedNote note = markdown::parseNote(VaultPath(), content);
    const int b = markdown::blockAtLine(note, line);
    if (b < 0)
        return content;
    QList<Reminder> onLine;
    for (const Reminder &r : note.blocks.at(b).reminders) {
        if (r.line == line)
            onLine.append(r);
    }
    const auto token = reminderToken(lines.at(line).text, onLine);
    if (!token)
        return content;

    // An instant: written in local wall-clock time, whatever time spec the caller used.
    const QDateTime local = newAt.toLocalTime();
    const DateSpec at{local.date(), QTime(local.time().hour(), local.time().minute())};
    // The rule is the block's: the same for all its reminders.
    if (reminders_detail::ruleOf(onLine.constFirst().recurrence))
        return insertOneOff(content, line, at, token->alarm);
    return replaceToken(content, lines.at(line), *token, at);
}

} // namespace brulion
