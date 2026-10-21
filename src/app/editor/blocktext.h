// Pure helpers of the block editor (no QObject, no QML): how a block of a BlockDocument is shown
// and edited inline, token edits for reminders/dates/recurrence, slash command texts and the
// detection of autocompletion triggers while typing. Everything is "text in -> text out" and
// unit-tested in tests/app/editor/tst_editor_blocktext.cpp.
//
// Inline editing never shows the parts of a block's body that the delegate draws itself:
//   * the task marker of a list item ("[ ] ", "TODO ") — the delegate draws a checkbox;
//   * the opening and closing fence lines of code/calc/query/math blocks and the `---` lines of
//     YAML frontmatter — the delegate draws a frame with the language label.
// EditMapping describes that split: body = body.left(offset) + editText + body.right(suffix).
#pragma once

#include "markdown/blockdocument.h"
#include "markdown/tokens.h"
#include "types.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace brulion::editor {

/// The delegate a block is shown with.
enum class BlockView { Text, Heading, Code, Calc, Query, Math, Table, Quote, Callout, Rule, Frontmatter, Html };

/// "text", "heading", "code", "calc", "query", "math", "table", "quote", "callout", "rule",
/// "frontmatter", "html" (the `view` role of BlockListModel).
QString viewName(BlockView view);
/// "listItem", "paragraph", "heading", "code", "table", "quote", "callout", "rule", "html",
/// "frontmatter" (the `kind` role).
QString kindName(BlockKind kind);
/// "todo", "doing", "done", "cancelled", "deferred"; "" for none.
QString taskStatusName(std::optional<TaskStatus> status);
std::optional<TaskStatus> taskStatusFromName(QStringView name);

/// A fenced code block body (```` ```lang ```` … ```` ``` ````, also `~~~`).
struct Fence {
    QString openLine;     ///< "```calc"
    QString closeLine;    ///< "```"; empty when the fence is not closed
    QString lang;         ///< first word of the info string, lower-case ("calc", "query", "js")
    QString inner;        ///< content lines joined by '\n'
    int innerOffset = 0;  ///< index of `inner` in the body
    QChar marker;         ///< '`' or '~'
    int length = 3;       ///< number of markers in the opening fence
    bool closed = false;

    friend bool operator==(const Fence &, const Fence &) = default;
};

/// The fence when the body's first line opens a fenced code block (up to 3 spaces, 3+ backticks
/// or tildes); the last line closes it when it is a matching closing fence.
std::optional<Fence> parseFence(QStringView body);

/// How a block is shown and which part of its body is edited inline.
struct BlockShape {
    BlockView view = BlockView::Text;
    int headingLevel = 0; ///< headings, also "# " list items (Logseq)
    QString lang;         ///< code language ("calc", "query", …)
    int editOffset = 0;   ///< body characters hidden before the edit text
    int editSuffix = 0;   ///< body characters hidden after the edit text
    QString editText;     ///< the text edited inline
    std::optional<TaskStatus> task;
    QString calloutType;  ///< "note", "tip", "warning", … (callouts)
    bool calloutFolded = false; ///< "> [!note]-"
};

/// Shape of a block entry (list items, paragraphs, headings, code, tables, quotes, …). List
/// items whose body is a fence, a table, a heading or a quote (Logseq style "- ```calc") get the
/// matching view.
BlockShape shapeOf(const markdown::BlockEntry &entry);

/// The new body for `editText` (the inverse of shapeOf().editText): the hidden prefix and
/// suffix of the current body are kept. A task marker without a trailing space gets one when
/// text follows it.
QString bodyWithEditText(const markdown::BlockEntry &entry, const QString &editText);

/// Body without the leading task marker (and its spacing); unchanged when there is none.
QString withoutTaskMarker(const markdown::BlockEntry &entry);

/// Plain one-line text of a body for breadcrumbs, dialog context and accessibility: markers,
/// tokens, ids and Markdown punctuation of links removed, whitespace collapsed, at most
/// `maxLength` characters (… appended when cut).
QString plainTextOf(const markdown::BlockEntry &entry, int maxLength = 80);

// ---------------------------------------------------------------------------------------------
// Derived block data for special delegates
// ---------------------------------------------------------------------------------------------

/// `query` role: { source, filters: [{ key, value, negated }], terms: [text] }.
QVariantMap queryInfo(QStringView source);

/// `properties` role of frontmatter blocks: [{ key, value }] (YAML frontmatter or `key:: value`
/// page properties; lists joined with ", "). The note mode key `brulion` of YAML frontmatter
/// (markdown/notemode.h) is left out: the page header's mode toggle shows it.
QVariantList frontmatterProperties(const QString &body);
/// The YAML frontmatter `body` (with its `---` lines) holds nothing but the note mode key
/// `brulion` — one line at column 0, blank lines aside (a comment or other text keeps the block
/// visible) — a block the editor does not show (BlockListModel `noteModeOnly`).
bool onlyNoteModeKey(const QString &body);

/// One image or attachment embedded in a block body: `![[photo.png]]`, `![alt](assets/a.png)`.
struct Embed {
    QString target;  ///< as written
    QString alt;     ///< alt text / alias
    bool image = false;
    friend bool operator==(const Embed &, const Embed &) = default;
};
/// Attachment embeds of a body (embeds of pages and blocks are not included).
QList<Embed> embedsOf(QStringView body);
/// True when the body consists of nothing but attachment embeds (and whitespace); with
/// `transclusions` also page, section and block embeds count (transclusionsOf()).
bool onlyEmbeds(QStringView body, bool transclusions = false);

/// A page, section or block embedded in a block body (docs/SYNTAX.md §2, "transclusion"):
/// `![[Page]]`, `![[Page#Heading]]`, `![[Page#^id]]`, same-page `![[#Heading]]` / `![[#^id]]`
/// and Logseq's vault-wide `!((id))`. Attachments (`![[photo.png]]`) are Embeds instead.
struct Transclusion {
    enum class Kind { Page, Heading, Block };
    Kind kind = Kind::Page;
    QString target;  ///< page as written ("" = this page; empty for `!((id))`)
    QString heading; ///< Heading: the heading text after '#'
    QString blockId; ///< Block: the id (without '^')
    QString display; ///< alias after '|'
    bool blockRef = false; ///< written `!((id))`: the id is looked up in the whole vault
    friend bool operator==(const Transclusion &, const Transclusion &) = default;
};
/// Transclusions of a body in order (inline code and comments are skipped).
QList<Transclusion> transclusionsOf(QStringView body);

// ---------------------------------------------------------------------------------------------
// Token edits (SYNTAX §5-6): content in -> content out, first line of the body
// ---------------------------------------------------------------------------------------------

/// Replace the token at [from, to) of `body` with `token`; an empty `token` removes it together
/// with one adjacent space. Out-of-range ranges leave the body unchanged.
QString replaceTokenRange(const QString &body, qsizetype from, qsizetype to, const QString &token);

/// Set the token of `kind` on the body's first line: the first existing token of that kind is
/// replaced (removed when `token` is empty); without one, `token` is appended to the first line
/// (before a trailing block id) — always appended when `append` is true (reminders: a block may
/// carry several). Tokens inside inline code are ignored.
QString setFirstLineToken(const QString &body, markdown::TokenKind kind, const QString &token, bool append = false);

/// Range [from, to) of the first token of `kind` on the first line, or nullopt.
std::optional<std::pair<qsizetype, qsizetype>> findFirstLineToken(const QString &body, markdown::TokenKind kind);

/// Insert `token` at `cursor` of `text` with spaces around it as needed; returns the new text and
/// sets *newCursor to the position after the token (and its trailing space, if one was added).
QString insertTokenAt(const QString &text, int cursor, const QString &token, int *newCursor);

// ---------------------------------------------------------------------------------------------
// Autocompletion triggers
// ---------------------------------------------------------------------------------------------

/// What is being completed at a cursor position of a block's edit text.
struct CompletionTrigger {
    QString mode;    ///< "" (nothing) | "pages" | "blocks" | "tags" | "slash" | "date"
    QString filter;  ///< text typed so far (after `[[`, `((`, `#`, `/`, or the date phrase)
    int from = -1;   ///< start of the text a completion replaces
    int to = -1;     ///< end of the text a completion replaces (>= the cursor)
    QString token;   ///< "[[", "![[", "((", "!((", "#", "/", or for dates "📅", "⏳", "🛫", "⏰", "⏰!", "@"
    QString closing; ///< pages/blocks: "]]" / "))" / "|" already present after the target

    bool active() const { return !mode.isEmpty(); }
    QVariantMap toMap() const;
    static CompletionTrigger fromMap(const QVariantMap &map);
};

/// Detect the completion context at `cursor` (only the current line before the cursor matters).
/// Nothing is detected inside inline code. Rules (docs/UI.md "Autocomplete popup"):
///   pages  `[[query` / `![[query` without a closing `]]` before the cursor
///   blocks `((query` / `!((query` (not right after a word character)
///   tags   `#query` after start/whitespace/`(` (a bare `#` at the very start is a heading)
///   slash  `/query` after start/whitespace
///   date   `📅 `, `⏳ `, `🛫 `, `⏰ `, `⏰! ` + phrase (not a canonical date yet), or `@phrase`
CompletionTrigger completionTriggerAt(QStringView text, int cursor);

/// VaultConfig from EditorHost::vaultConfig() (missing keys keep the defaults; journalMode /
/// pageMode as "outline" | "document").
VaultConfig vaultConfigFromMap(const QVariantMap &map);

// ---------------------------------------------------------------------------------------------
// Slash commands
// ---------------------------------------------------------------------------------------------

/// A slash command of the `/` menu.
struct SlashCommand {
    QString id;          ///< "task", "reminder", "alarm", "due", "scheduled", "recurrence", …
    QString namePl;      ///< Polish name ("Przypomnienie…")
    QString nameEn;      ///< English name ("Reminder…")
    QString icon;        ///< Lucide icon name
    QString hint;        ///< syntax preview ("⏰", "```calc")
    QString syntaxPl;    ///< the menu's subtitle: what the command writes, in words ("⏰ data godzina")
    QString syntaxEn;    ///< the same in English ("⏰ date time")
    QStringList keywords; ///< extra search words (any language, folded when matching)
};

/// All slash commands in menu order.
const QList<SlashCommand> &slashCommands();
/// The command with `id`, or nullptr.
const SlashCommand *slashCommand(QStringView id);
/// Commands matching `filter` (diacritic- and case-insensitive; names in both languages and
/// keywords), best first: name prefix, then word prefix, then substring; menu order otherwise.
QList<const SlashCommand *> filterSlashCommands(QStringView filter);

/// Markdown inserted by text-only slash commands for a new/empty block ("calc" -> "```calc\n\n```").
/// `cursor` receives the caret offset inside the returned text. Empty for commands that open a
/// dialog or are not text templates.
QString slashTemplate(QStringView id, bool english, int *cursor);

// ---------------------------------------------------------------------------------------------
// Text coming back from QML TextEdits; undo steps
// ---------------------------------------------------------------------------------------------

/// `pos` clamped to [0, text.size()] and moved off the middle of a UTF-16 surrogate pair (after
/// the pair): an emoji is never cut in two by a stale or computed caret position.
int snapToCharacter(QStringView text, int pos);

/// A QML TextEdit (QTextDocument) turns every "\r\n" and "\r" into "\n". Given the text the
/// TextEdit was filled with (`original`) and the text it reports after an edit (`edited`), returns
/// `edited` with the original line terminators: the unchanged text before and after the edited
/// range keeps its exact bytes, line breaks inside the edited range get `eol`. Returns `original`
/// itself when nothing but the terminators differs.
QString restoreLineEndings(const QString &original, const QString &edited, const QString &eol);

/// The difference between two versions of a text as one replaced range (common prefix and suffix
/// left out): what an undo step stores instead of two copies of the whole page.
struct TextDelta {
    qsizetype from = 0;    ///< start of the replaced range (the same in both versions)
    QString removed;       ///< the range in the older version
    QString inserted;      ///< the range in the newer version
    qsizetype beforeSize = 0;
    qsizetype afterSize = 0;

    static TextDelta between(const QString &before, const QString &after);
    /// `first` (A -> B) followed by `second` (B -> C) as one delta A -> C; `middle` is B.
    static std::optional<TextDelta> compose(const TextDelta &first, const TextDelta &second, const QString &middle);
    /// The newer version from the older one; nullopt when `before` is not the version this delta
    /// was made from (it is never patched into something else).
    std::optional<QString> apply(const QString &before) const;
    /// The older version from the newer one; nullopt when `after` does not match.
    std::optional<QString> revert(const QString &after) const;
    bool isEmpty() const { return removed.isEmpty() && inserted.isEmpty(); }
    /// Characters held by the delta.
    qsizetype size() const { return removed.size() + inserted.size(); }
};

} // namespace brulion::editor
