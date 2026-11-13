// brulion::qr — the text of a note's QR codes (docs/UI.md "QR code"): what the codes of a note, or
// of blocks copied from one, carry — the Markdown as written or the plain text "Copy as plain
// text" gives, the title first — and that text cut into as many codes as it needs when one cannot
// hold it. Pure functions: text in, text out, any thread.
#pragma once

#include "qr/qrcode.h"

#include <QString>
#include <QStringList>
#include <QStringView>

#include <functional>

namespace brulion::qr {

/// The most codes one text is cut into: a longer text is too long for QR codes (scanning that many
/// takes longer than sending the note another way).
inline constexpr int MaxCodes = 20;

/// How a note goes into its codes.
enum class TextFormat {
    /// As written: a notes app on the other side (Brulion, Obsidian, Logseq) gets the note whole —
    /// links, tasks, properties and block ids included.
    Markdown,
    /// What the note shows, as "Copy as plain text" gives it (markdown::blocksPlainText()): for
    /// anyone else.
    PlainText,
};

/// Options of noteText().
struct NoteTextOptions {
    TextFormat format = TextFormat::Markdown;
    /// The note's title, first with a blank line after it ("" for none): in Markdown a level-1
    /// heading after the YAML frontmatter and the `key:: value` page properties (they have to stay
    /// first), in plain text the title alone. Left out when the note starts with a level-1 heading
    /// of its own (hasOwnTitle(): the page shows that one as its title). Line breaks in it become
    /// spaces.
    QString title;
    /// Plain text: the text of the block a `((id))` reference names ("" unknown: the reference
    /// stays as written).
    std::function<QString(const QString &id)> blockText;
};

/// The text of the codes of `markdown` — a note, or blocks copied from one:
///   * Markdown: the text as written, its line breaks "\n" (CRLF and CR become LF: a byte less
///     each), without a byte order mark, blank lines before the first line or trailing blanks;
///   * PlainText: markdown::blocksPlainText() of every block — no frontmatter, page properties or
///     block ids; list items with "• ", tasks with ☐ / ☑ / ☒, headings without their "#", links as
///     their labels.
/// "" when there is nothing to share, whatever the title: no block besides the frontmatter and the
/// page properties (a blank note, a new day of a template with properties only).
QString noteText(const QString &markdown, const NoteTextOptions &options = {});

/// `markdown` starts with a level-1 heading of its own, after its frontmatter and page properties:
/// noteText() leaves the title out.
bool hasOwnTitle(const QString &markdown);

/// The UTF-8 bytes of `text` — what its code holds (qr::encode()) —, counted without converting
/// it. A lone surrogate counts 3: encode() writes it as U+FFFD.
qsizetype utf8Size(QStringView text);

/// `text` cut into parts of at most `maxBytes` UTF-8 bytes each (taken as at least 4, so that any
/// character fits), one code each: [] for "", [text] when it fits one. As few parts as hold it, of
/// about the same size (two codes of 1500 bytes scan more easily than one of 2953 and one of 47):
/// each ends after a line break near its even share, else after a space or tab, else between two
/// characters — never inside one (a surrogate pair, a letter with its combining marks, an emoji
/// sequence; inside a cluster only when the cluster alone is larger than `maxBytes`). Joined, the
/// parts are `text` again, character for character.
QStringList splitText(const QString &text, qsizetype maxBytes = MaxBytes);

} // namespace brulion::qr
