#pragma once
// Where the transcript of a recording goes (part of the media module, media/media.h): right under
// the block that embeds it, as text of its own the user edits like any other (docs/SYNTAX.md §2
// "Voice notes and transcripts", docs/UI.md "Voice notes"), and how it is written: what a
// recognizer heard, its segments made sentences and the sentences paragraphs, each paragraph of a
// longer recording starting with a link to its moment (`[[Nagranie.m4a#t=83|1:23]]`) that plays the
// recording from there. Content in, content out (markdown::BlockDocument): every other byte of the
// note stays.
//
//   const QList<media::TimedText> paragraphs = media::transcriptParagraphs(segments);
//   media::TranscriptPlace p = media::placeTranscript(content, u"Nagranie 2026-11-20 18.30.m4a", line,
//                                                     paragraphs, audioMs);
//   if (p.line >= 0) write(p.content);

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace brulion::media {

/// A stretch of a recording and what was said in it: a recognizer's segment (speech::Segment is
/// this), a sentence, a paragraph. Times in milliseconds from the recording's start.
struct TimedText {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
    friend bool operator==(const TimedText &, const TimedText &) = default;
};

/// `text` ends a sentence: its last character (before closing quotes and brackets) is `.`, `!`,
/// `?`, `…` (or a full-width one).
bool endsSentence(QStringView text);

/// Segments as sentences: a segment whose text does not end a sentence runs on into the next one —
/// Whisper cuts a long sentence where it likes —, a sentence starting where its first segment does
/// and ending where its last one does, the texts joined by single spaces (trimmed). A run without an
/// end of a sentence (speech Whisper writes without punctuation) is cut at the first segment's end
/// `longestMs` after its start. Blank segments are left out.
QList<TimedText> transcriptSentences(const QList<TimedText> &segments, qint64 longestMs = 60000);

/// When transcriptParagraphs() starts a new paragraph.
struct ParagraphRules {
    qint64 pauseMs = 2000;    ///< a silence this long after a sentence's end
    qint64 lengthMs = 30000;  ///< at a sentence's end this long after the paragraph's start
    int lengthChars = 600;    ///< at a sentence's end once the paragraph has this many characters
    qint64 longestMs = 60000; ///< at a segment's end this long after its start, sentence or not
};

/// What a recognizer heard as paragraphs of whole sentences (transcriptSentences()): a new one after
/// a pause, or once the paragraph is long (ParagraphRules) — about one every half minute of speech.
/// Each one's start and end are its first and last segments'; its text the raw words (not yet
/// transcriptText()).
QList<TimedText> transcriptParagraphs(const QList<TimedText> &segments, const ParagraphRules &rules = {});

/// How a transcript links its moments to the recording: the way the embed above it names it.
struct MomentLinks {
    /// `[[target#t=83|1:23]]` (an embed `![[…]]`); false: `[1:23](target#t=83)` (an embed `![](…)`,
    /// the target percent-encoded where Markdown needs it).
    bool wiki = true;
    QString target;         ///< the recording as the embed names it, without a fragment
    qint64 durationMs = -1; ///< its length: the times have hours when it is an hour or longer
};

/// The link to the moment `ms` of the recording, in whole seconds: `[[Nagranie.m4a#t=83|1:23]]`,
/// `[1:23](assets/Nagranie%201.m4a#t=83)`; the start is `#t=0`. "" without a target.
QString momentLink(const MomentLinks &links, qint64 ms);

/// The blocks of a transcript of `paragraphs` (transcriptParagraphs()): one paragraph — its text
/// alone (transcriptText()), as a short voice note has always been written; more — each one its
/// moment link, a space and its text. Blank paragraphs are left out.
QStringList transcriptBlocks(const QList<TimedText> &paragraphs, const MomentLinks &links);

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

/// placeTranscript() of a transcript with its times: the blocks of transcriptBlocks() right under
/// the block that embeds `target` (the same one as above), in order — a list item's children (its
/// first ones), else paragraphs after it — their moment links written the way that embed names the
/// recording (`![[x.m4a]]`: `[[x.m4a#t=83|1:23]]`; `![](assets/x%201.m4a)`:
/// `[1:23](assets/x%201.m4a#t=83)`). `durationMs` is the recording's length (-1: the last
/// paragraph's end). `line` is the first block's.
TranscriptPlace placeTranscript(const QString &content, QStringView target, int nearLine,
                                const QList<TimedText> &paragraphs, qint64 durationMs);

} // namespace brulion::media
