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
//
// The time a message was written is a block property right under its first line, `czas:: 2026-11-15
// 09:12` (`time::` in an English notebook; local wall-clock time, minutes), not part of its text; a
// page without it (written before it existed, or by hand) has messages without a time. A picture
// sent with a message is an embed of the attachment in it, `![[Pasted image 20261115091200.jpg]]`
// (or `![](assets/foto.png)`), shown by any app; the model gets the pictures of the conversation's
// newest messages (at most MaxPictures). Who answers is the frontmatter's `partner:` — an
// extension's chat command (docs/EXTENSIONS.md "Chat partners"); none: the built-in assistant. A
// conversation an AI agent started from another one (a sub-conversation: docs/UI.md "Agents
// delegate") names it in `parent: "[[AI/2026-11-15 …]]"`, a link that follows a rename.
//
//   ---
//   zadania: ignoruj
//   partner: pirat/rozmowa
//   ---
//   - 🧑 Co to za roślina?
//     czas:: 2026-11-15 09:12
//     ![[Pasted image 20261115091200.jpg]]
//
// A conversation too long for the model (a small one on the device) is sent from a later turn on:
// firstSent() — whole turns from the oldest left out, the latest always sent.
#pragma once

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QVariantList>

#include <utility>

namespace brulion::chat {

enum class Role { User, Assistant };

/// The pictures of the conversation the model gets with its messages (ai::MaxRequestImages).
inline constexpr int MaxPictures = 8;
/// What a picture counts for in firstSent()'s budget, in characters (a model reads one as some
/// thousand tokens).
inline constexpr int PictureChars = 3000;

struct Message {
    Role role = Role::User;
    /// Markdown as written: the first line after its marker, then the lines under it without
    /// their common indentation (the time's line not among them). Trailing blank lines are left
    /// out.
    QString text;
    int line = 0;    ///< its first line in the page (0-based)
    int endLine = 0; ///< its last line, inclusive (trailing blank lines not counted)
    /// When it was written (its `czas::` / `time::` line, local time); invalid: not said.
    QDateTime time;
    /// The pictures it embeds, as named there ("Pasted image 2026.jpg", "assets/foto.png"): the
    /// notebook's own files (no address on the internet), by their extension.
    QStringList pictures;
};

struct Conversation {
    QList<Message> messages;
    /// The page the conversation is about: the first message's trailing link, `([[Dom]])` →
    /// "Dom" (as written: a path part stays); "" when it names none.
    QString page;
    /// Who answers: the frontmatter's `partner:` ("pirat/rozmowa": an extension and its chat
    /// command, "pirat": its first one); "" the built-in assistant.
    QString partner;
    /// The conversation this one was started from: the frontmatter's `parent:` link as written,
    /// `"[[AI/2026-11-15 Plan]]"` → "AI/2026-11-15 Plan" (no alias, heading or block part); ""
    /// none.
    QString parent{};
};

/// "AI": the folder of conversation pages (the same in every language).
QString folder();
/// The page `title` ("AI/2026-11-15 …", a vault path without ".md" or with it) is in folder().
bool inFolder(QStringView title);

/// The messages of a conversation page; none for a page that has none.
Conversation parse(QStringView content);

/// The key of a message's time in the notebook's language: "czas" ("pl"), else "time".
QString timeKey(QStringView language);

/// The lines of a message as a page holds them: `- 🧑 ` / `- 🤖 ` and its first line, its time's
/// line when `time` is valid (`  czas:: 2026-11-15 09:12`, the key in `language`), the other lines
/// indented by two spaces (blank lines empty), reminders in a code span; `newline` between lines
/// and after the last one. Blank lines around the text are dropped; an empty text is the bare
/// marker.
QString formatMessage(Role role, QStringView markdown, QStringView newline = u"\n", const QDateTime &time = {},
                      QStringView language = u"pl");

/// `content` with the message appended as its last one: the line ending the page uses ("\r\n" or
/// "\n"), a page without a final line break gets one first, everything before stays as it was.
/// Its time (when valid) is written with the key the page uses already — `czas::`, `time::`,
/// else after its frontmatter's language (`zadania:` Polish), else `language`.
QString appendMessage(const QString &content, Role role, QStringView markdown, const QDateTime &time = {},
                      QStringView language = u"pl");

/// A new conversation page: the frontmatter that keeps its tasks out of the task lists, in the
/// notebook's language ("pl": `zadania: ignoruj`, else `tasks: ignore`), its `partner:` when it
/// is not "", its `parent:` link to the conversation `parent` (a page's name or vault path,
/// ".md" left out) when it is not "", and the user's first message — `question`, and
/// ` ([[page]])` after its first line when it is about a page —, with its time when valid.
QString newConversation(QStringView question, QStringView page, QStringView language, const QDateTime &time = {},
                        QStringView partner = {}, QStringView parent = {});

/// `content` with its frontmatter's `partner:` set to `partner` ("" takes the line out): the
/// line replaced, or added at the frontmatter's end, or a frontmatter made for it; the rest byte
/// for byte, its line endings kept.
QString setPartner(const QString &content, QStringView partner);

/// The pictures a message's Markdown embeds (Message::pictures), in order, each once.
QStringList picturesIn(QStringView markdown);

/// `markdown` without its picture embeds (a line that held nothing else goes too): what the chat
/// shows as text, the pictures apart.
QString withoutPictures(QStringView markdown);

/// The user's message with `pictures` (vault paths, as saveAttachment gave them) added: `text`
/// and under it a line `![[path]]` for each.
QString withPictures(QStringView text, const QStringList &pictures);

/// The name of a new conversation's page in folder(): "AI/2026-11-15 Co mam jutro do zrobienia" —
/// the date and the first words of the question (at most 60 characters, cut at a word; what a
/// file name cannot hold, links, code and trailing punctuation left out), "Rozmowa" /
/// "Conversation" when nothing is left.
QString pageTitle(QStringView question, QDate date, QStringView language);

/// A conversation page's title as {its date, its name}: "2026-11-15 Co mam jutro" → {"2026-11-15",
/// "Co mam jutro"}; a title without a date in front: {"", title}.
std::pair<QString, QString> splitTitle(QStringView title);

/// The line the day's journal gets for a new conversation: "- 🤖 [[AI/2026-11-15 …]]".
QString journalEntry(QStringView title);

/// The conversation as b.ai's `messages` ({role = "user" | "assistant", content, images?}), from
/// the message `from` on (firstSent()): messages of one role in a row joined into one, the AI's
/// messages before the user's first left out, so the list starts with the user's turn; empty
/// messages skipped. The pictures of the newest user's messages (MaxPictures at most, of
/// `withPictures` true) are their `images` and their embeds are out of `content`; older ones stay
/// embeds in the text (the assistant may look at them with its tool).
QVariantList history(const Conversation &conversation, int from = 0, bool withPictures = true);

/// Where the conversation is sent from for a model that reads at most about `budget` characters
/// of it (PictureChars a picture): the first message of the earliest turn — a user's message (a
/// run of them) and the answers after it — from which the rest fits, whole turns from the oldest
/// left out; the latest turn is always sent, whatever its length. 0 (all of it) for `budget` <= 0.
int firstSent(const Conversation &conversation, int budget);

/// What firstSent() counts for the message: its text's length and PictureChars a picture.
int cost(const Message &message);

/// One line of plain text for a list of conversations: the message's text with Markdown's marks
/// taken out ([[Page|alias]] → alias, `code` → code, **bold** → bold, list markers, a page link at
/// the end), whitespace collapsed, at most `max` characters with "…".
QString preview(QStringView markdown, int max = 120);

} // namespace brulion::chat
