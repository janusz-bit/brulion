#pragma once
// Where the transcript of a recording goes (part of the media module, media/media.h): right under
// the block that embeds it, as text of its own the user edits like any other (docs/SYNTAX.md §2
// "Audio and video", docs/UI.md "Voice notes"). Content in, content out (markdown::BlockDocument):
// every other byte of the note stays.
//
//   const QString text = media::transcriptText(segments);
//   media::TranscriptPlace p = media::placeTranscript(content, u"Nagranie 2026-11-20 18.30.m4a", line, text);
//   if (p.line >= 0) write(p.content);

#include <QString>
#include <QStringList>
#include <QStringView>

namespace brulion::media {

/// What a recognizer heard as one paragraph of note text: its segments joined by single spaces,
/// white space (line breaks too) collapsed, control and format characters dropped, and Markdown at
/// its start escaped so that it stays a plain paragraph — a heading's `#`, a list's `-` / `+` /
/// `*` / `1.` / `1)`, a quote's `>`, a table's `|`, a fence's ``` / `~~~`, a rule, `$$`, `:::`
/// get a backslash. Inside, `[[`, `((`, `{{` and a `#` before a letter are escaped too (a word
/// heard is no link, embed, template or tag). "" for blank text.
QString transcriptText(const QStringList &segments);

struct TranscriptPlace {
    QString content; ///< the note with the transcript ("" when the embed was not found)
    int line = -1;   ///< the first line of the transcript's block in `content` (-1: not placed)
};

/// `content` with `text` (transcriptText()) as a block right under the block that embeds
/// `target` — `![[target]]` or `![…](target)` as written, without a `#t=` fragment; a decoded
/// percent-encoding and the file's name alone also match (a link written another way): a list
/// item's first child (one level deeper, before its other children), a paragraph's (or another
/// top-level block's) next paragraph. Of several blocks that embed it, the one nearest to
/// `nearLine` (the line the recording was inserted at; -1: the first). Not placed: no such
/// embed, or `text` blank.
TranscriptPlace placeTranscript(const QString &content, QStringView target, int nearLine, const QString &text);

} // namespace brulion::media
