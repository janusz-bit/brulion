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
//
// An answer the user reported (docs/UI.md "AI chat": Report response) or hid is listed by its
// number — the page's messages counted from 1 — in the frontmatter, `zgłoszone: [2]` / `ukryte:
// [4, 6]` (`reported:` / `hidden:` in an English notebook): the chat shows it folded. Another app,
// and an older Brulion, reads the frontmatter as page properties and shows every message.
//
//   ---
//   zadania: ignoruj
//   zgłoszone: [2]
//   ---
//
// The list of conversations reads two marks of the frontmatter (setMark()): `przypięta: tak` /
// `pinned: true` puts the conversation among the pinned ones on top, `zarchiwizowana: tak` /
// `archived: true` out of the list into its folded "Archived" (either language's key read, the
// values true / yes / tak / 1 / on). They only sort: a page that says so by itself is harmless.
//
// A side chat (`/btw`, docs/UI.md "AI chat"): a question asked beside a conversation without
// adding it to it — a page of its own, a copy of that conversation with the question after it
// (sideChat()), its frontmatter naming the conversation, `side: "[[AI/…]]"`, and where its own
// messages start, `side-from: 5` (the page's messages counted from 1; the keys the same in every
// language). The chat shows it only inside its conversation, never in the list.
//
//   ---
//   zadania: ignoruj
//   side: "[[AI/2026-11-15 Plan wyjazdu]]"
//   side-from: 3
//   ---
//   - 🧑 Plan wyjazdu do Zakopanego
//   - 🤖 Pociąg o 7:15 …
//   - 🧑 Ile kosztuje bilet ulgowy?
//
// A sub-conversation started with "wake" (docs/UI.md "Agents delegate") wakes the conversation it
// came from when its turn ends: a user's message there that starts with "↩ " and the link to the
// sub-conversation (wakeMessage(), wakeSource()), its answer quoted, the reason it failed, the
// question it waits on, or that it was stopped:
//
//   - 🧑 ↩ Podrozmowa [[AI/2026-11-22 Druga opinia]] odpowiedziała:
//
//     > Plan ma sens, ale pociąg o 7:15 jest pełny w soboty.
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

/// What the user marked a message as: reported (sent as a report of an offensive or wrong
/// answer) or hidden (folded, nothing sent).
enum class Flag { None, Reported, Hidden };

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
    /// Reported or hidden: its number in the frontmatter's `zgłoszone:` / `ukryte:` list
    /// (`reported:` / `hidden:`; both: reported).
    Flag flag = Flag::None;
    /// A user's message that wakes its conversation: the sub-conversation it comes from
    /// (wakeSource(): "AI/2026-11-22 Druga opinia"); "" for any other message.
    QString from{};
};

struct Conversation {
    QList<Message> messages;
    /// The page the conversation is about: the first message's trailing link, `([[Dom]])` →
    /// "Dom" (as written: a path part stays); "" when it names none.
    QString page;
    /// Who answers: the frontmatter's `partner:` ("pirat/rozmowa": an extension and its chat
    /// command, "pirat": its first one); "" the built-in assistant.
    QString partner;
    /// The conversation it was forked from: the frontmatter's `fork: "[[AI/…]]"` ("AI/…"; ""
    /// none) — forked().
    QString fork{};
    /// The conversation this one was started from: the frontmatter's `parent:` link as written,
    /// `"[[AI/2026-11-15 Plan]]"` → "AI/2026-11-15 Plan" (no alias, heading or block part); ""
    /// none.
    QString parent{};
    /// The frontmatter's marks (setMark()): pinned on top of the list / archived out of it.
    bool pinned = false;
    bool archived = false;
    /// A side chat: the conversation it is beside, the frontmatter's `side:` link as written
    /// ("AI/2026-11-15 Plan"; "" none) — sideChat() …
    QString side{};
    /// … and the index of its first own message (the frontmatter's `side-from:`, counted from 1
    /// there; 0 when it says none): the messages before it are the copy of that conversation.
    int sideFrom = 0;
};

/// What the turn of a sub-conversation that wakes its parent came to (wakeMessage()).
enum class Wake {
    Answered, ///< its answer was written
    Failed,   ///< its turn failed
    Asking,   ///< it waits for the user's answer (a permission, a change to allow)
    Stopped   ///< the user stopped it
};

/// Why a wake message waits for the user in its conversation's queue instead of starting a turn.
enum class WakeHeld {
    No,
    Limit,   ///< the conversation was woken too often in the last hour
    Archived ///< the conversation is archived
};

/// The longest text of a sub-conversation a wake message quotes (the rest is in its page).
inline constexpr int MaxWakeChars = 4000;

/// A mark of a conversation's frontmatter that sorts the list of conversations.
enum class Mark {
    Pinned,  ///< `przypięta: tak` / `pinned: true`
    Archived ///< `zarchiwizowana: tak` / `archived: true`
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

/// The frontmatter's key of `flag` in the notebook's language: "zgłoszone" / "ukryte" ("pl"),
/// else "reported" / "hidden" ("" for Flag::None).
QString flagKey(Flag flag, QStringView language);

/// `content` with its message `number` (1-based, the page's messages in order) marked `flag`
/// (Flag::None: its mark taken away): the number taken out of the frontmatter's other list and put
/// into this one (`zgłoszone: [2, 5]`, the numbers in order) — the entry written again as a flow
/// list in place, added at the frontmatter's end, a list left empty taken out (a second entry of
/// one flag, in the other language, too), a frontmatter made for it —; the rest byte for byte, its
/// line endings kept. A new key in the language the page is written in (its time's key, its
/// `zadania:` / `tasks:`), else `language`. Unchanged for a `number` below 1 or a mark it has.
QString setFlag(const QString &content, int number, Flag flag, QStringView language = u"pl");

/// `content` with the frontmatter's `mark` set (`on`) or taken out: its line — in the page's
/// language (its time's key, else its frontmatter's word for the tasks), else `language` ("pl":
/// `przypięta: tak`, `zarchiwizowana: tak`; else `pinned: true`, `archived: true`) — replaces
/// the mark's line in either language where it is (another one of it goes), or comes at the
/// frontmatter's end, or a frontmatter is made for it; off takes every line of it out. The rest
/// byte for byte, the line endings kept; unchanged when it says so already.
QString setMark(const QString &content, Mark mark, bool on, QStringView language = u"pl");

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

/// `content` without the messages from index `from` on — "Edit from here" takes them back:
/// everything before that message's first line byte for byte; `content` as it is when there is
/// no such message.
QString truncated(const QString &content, int from);

/// A conversation that goes on from `content`'s message `upTo` (its index) in a page of its own
/// ("Fork from here"): `content` up to that message's last line — its frontmatter, `partner:`,
/// times and pictures as they are, line endings kept; `parent:`, the list's marks and a side
/// chat's `side:` / `side-from:` left out — with `fork: "[[source]]"` in its frontmatter
/// (`source`: the page forked, its path with or without ".md"; "" none). "" when there is no such
/// message.
QString forked(const QString &content, int upTo, QStringView source);

/// `fork:`'s value for `source` ("AI/2026-11-15 Dom.md" → `"[[AI/2026-11-15 Dom]]"`).
QString forkLink(QStringView source);

/// A side chat of the conversation `content` (its page `source`, with or without ".md"): a copy of
/// all of it (as forked() of its last message: its frontmatter but `parent:`, `fork:`, the list's
/// marks and an earlier `side:`, its `partner:`, times and pictures as they are, line endings
/// kept) with `side: "[[source]]"` and `side-from:` (the number of its first own message) in its
/// frontmatter, and the user's `question` appended (appendMessage(), its time when valid). "" for
/// a conversation without messages or an empty question.
QString sideChat(const QString &content, QStringView source, QStringView question, const QDateTime &time = {},
                 QStringView language = u"pl");

/// The user's message that wakes a conversation for its sub-conversation `child` (a vault path,
/// with or without ".md") whose turn came to `wake`: its first line "↩ Sub-conversation
/// [[child]] answered:" ("↩ Podrozmowa [[child]] odpowiedziała:" in "pl"; failed, waits
/// for the user's answer, was stopped), then `text` — the answer, why it failed, the question —
/// quoted ("> " lines, pictures out), cut at MaxWakeChars with a line saying how much of it there
/// is; with `held` a last line says why it waits for the user (`limit`: the most wakes an hour).
QString wakeMessage(Wake wake, QStringView child, QStringView text, QStringView language = u"pl",
                    WakeHeld held = WakeHeld::No, int limit = 0);

/// The sub-conversation a message's Markdown wakes its conversation for: its first line starts
/// with "↩" and a space and names a page in folder() as its first link → that page ("AI/…", an
/// alias, heading or ".md" left out); "" otherwise.
QString wakeSource(QStringView markdown);

/// `content` with its frontmatter's `key:` set to `value` ("" takes the line out): the line
/// replaced (its key matched without regard to case), or added at the frontmatter's end, or a
/// frontmatter made for it; the rest byte for byte, its line endings kept.
QString setFrontmatterValue(const QString &content, QStringView key, QStringView value);

/// The user's questions, newest first, as the field gets them back (Up in an empty field): their
/// text without the pictures they embed and without the first question's link to the page it is
/// about, each once.
QStringList prompts(const Conversation &conversation);

/// A message as a quote for the next one: its lines after "> " (pictures out), a blank line after
/// it; "" for a message of nothing but pictures.
QString quoted(QStringView markdown);

/// One line of plain text for a list of conversations: the message's text with Markdown's marks
/// taken out ([[Page|alias]] → alias, `code` → code, **bold** → bold, list markers, a page link at
/// the end), whitespace collapsed, at most `max` characters with "…".
QString preview(QStringView markdown, int max = 120);

} // namespace brulion::chat
