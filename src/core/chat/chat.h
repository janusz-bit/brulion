// Conversations with the AI assistant (docs/SYNTAX.md §14 "Conversations with AI"): each one a
// page of the notebook in the folder `AI/`, an outline of messages that any app reads as notes and
// that syncs like them. GUI-free and without the vault: what a conversation page says, what a new
// one starts with, a message appended to it (content in → content out, the rest byte for byte),
// its name and the journal's line that links to it. The app's chat view and its turns:
// src/app/shell/workspacechats.h; the agent that answers: src/core/extensions/builtin/asystent.luau.
//
//   ---
//   zadania: ignoruj
//   ---
//   - 🧑 Co mam jutro do zrobienia? ([[Dom]])
//   - 🤖 Jutro masz trzy zadania:
//
//     - zapłacić czynsz — [[Dom]]
//     - oddać raport — [[Praca]]
//   - 🧑 Przesuń czynsz na piątek
//
// A message is a top-level list item (`- `, `* ` or `+ ` at the start of a line) whose text starts
// with 🧑 (the user's) or 🤖 (the AI's), and every line after it up to the next such item: its
// text as written, the lines under the first without the indentation they have in common (two
// spaces as written here, a tab in a Logseq outline). Blank lines and fenced code stay. What comes
// before the first message (the frontmatter, a title) is no message; another top-level line
// between messages belongs to the message before it, so a line typed into the page shows there.
// `tasks: ignore` (`zadania: ignoruj`) keeps the checkboxes of answers out of the task lists;
// reminders in a message (`⏰ 2026-11-16 09:00`, `(@…)`) are written inside a code span, so a
// conversation never rings.
#pragma once

#include <QDate>
#include <QList>
#include <QString>
#include <QStringView>
#include <QVariantList>

namespace brulion::chat {

enum class Role { User, Assistant };

struct Message {
    Role role = Role::User;
    /// Markdown as written: the first line after its marker, then the lines under it without
    /// their common indentation. Trailing blank lines are left out.
    QString text;
    int line = 0;    ///< its first line in the page (0-based)
    int endLine = 0; ///< its last line, inclusive (trailing blank lines not counted)
};

struct Conversation {
    QList<Message> messages;
    /// The page the conversation is about: the first message's trailing link, `([[Dom]])` →
    /// "Dom" (as written: a path part stays); "" when it names none.
    QString page;
};

/// "AI": the folder of conversation pages (the same in every language).
QString folder();
/// The page `title` ("AI/2026-11-15 …", a vault path without ".md" or with it) is in folder().
bool inFolder(QStringView title);

/// The messages of a conversation page; none for a page that has none.
Conversation parse(QStringView content);

/// The lines of a message as a page holds them: `- 🧑 ` / `- 🤖 ` and its first line, the others
/// indented by two spaces (blank lines empty), reminders in a code span; `newline` between lines
/// and after the last one. Blank lines around the text are dropped; an empty text is the bare
/// marker.
QString formatMessage(Role role, QStringView markdown, QStringView newline = u"\n");

/// `content` with the message appended as its last one: the line ending the page uses ("\r\n" or
/// "\n"), a page without a final line break gets one first, everything before stays as it was.
QString appendMessage(const QString &content, Role role, QStringView markdown);

/// A new conversation page: the frontmatter that keeps its tasks out of the task lists, in the
/// notebook's language ("pl": `zadania: ignoruj`, else `tasks: ignore`), and the user's first
/// message — `question`, and ` ([[page]])` after its first line when it is about a page.
QString newConversation(QStringView question, QStringView page, QStringView language);

/// The name of a new conversation's page in folder(): "AI/2026-11-15 Co mam jutro do zrobienia" —
/// the date and the first words of the question (at most 60 characters, cut at a word; what a
/// file name cannot hold, links, code and trailing punctuation left out), "Rozmowa" /
/// "Conversation" when nothing is left.
QString pageTitle(QStringView question, QDate date, QStringView language);

/// The line the day's journal gets for a new conversation: "- 🤖 [[AI/2026-11-15 …]]".
QString journalEntry(QStringView title);

/// The conversation as b.ai's `messages` ({role = "user" | "assistant", content}): messages of one
/// role in a row joined into one, the AI's messages before the user's first left out, so the list
/// starts with the user's turn; empty messages skipped.
QVariantList history(const Conversation &conversation);

/// One line of plain text for a list of conversations: the message's text with Markdown's marks
/// taken out ([[Page|alias]] → alias, `code` → code, **bold** → bold, list markers, a page link at
/// the end), whitespace collapsed, at most `max` characters with "…".
QString preview(QStringView markdown, int max = 120);

} // namespace brulion::chat
