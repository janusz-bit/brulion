// The editor's AI assistant (docs/UI.md "AI in the editor"): the selection, or the block or
// paragraph at the caret, rewritten after an instruction ("Rewrite…": shorter, clearer, spelling
// and grammar fixed, more formal or casual, translated), continued ("Continue writing") or summed
// up ("Summarize") by the model the user chose (ai/ai.h: Claude, an OpenAI-compatible API, the
// model on the device). GUI-free and without the vault, all of it pure:
//
//   writing::Input in{writing::Action::Rewrite, writing::presetInstruction(u"shorter"), text, before, after, page};
//   const ai::Request request = writing::request(in);          // → ai::Client::send
//   const QString result = writing::clean(reply.text, in);      // what goes into the note
//   const writing::Edit edit = writing::edit(blockText, from, to, result, in.action);
//   const QString after = writing::apply(blockText, edit);       // content in → content out
//   writing::diff(text, result)                                  // the change, word by word
//
// Nothing is sent but on the user's action (the app's menu, shortcut or palette), and nothing is
// written but on their Accept: the app shows the answer as it comes, with the change, and applies
// edit() as one undo step (docs/UI.md).
#pragma once

#include "ai/ai.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace brulion::writing {

enum class Action { Rewrite, Continue, Summarize };

/// "rewrite" | "continue" | "summarize" and back (Rewrite for an unknown name).
QString actionName(Action action);
Action actionFrom(QStringView name);

/// The presets of "Rewrite…", in the menu's order: "shorter", "clearer", "grammar" (spelling and
/// grammar), "formal", "casual", "english", "polish" (translated).
QStringList presets();
/// What the model is told for a preset; "" for an unknown one.
QString presetInstruction(QStringView preset);

/// The most characters of a selection sent (a script's b.ai may send more; the editor keeps a
/// request a model on the device can still read).
inline constexpr int MaxText = 20000;
/// The text around it sent along, at most this much on each side (cut at a word).
inline constexpr int MaxContext = 1500;

struct Input {
    Action action = Action::Rewrite;
    /// Rewrite: what to do with the text — the user's words or a preset's instruction.
    QString instruction;
    /// The text to rewrite or sum up; Continue: the text written so far (up to the caret), whose
    /// end is continued. Markdown as the note holds it.
    QString text;
    /// What comes before it and after it in the note (context; cut to MaxContext by request()).
    QString before;
    QString after;
    /// The page's name.
    QString page;
    /// The text is part of one block's line(s) in the outline (`true`: no list marker or heading
    /// of its own is wanted back), not whole blocks or the source's paragraphs.
    bool inline_ = false;
};

/// Why `input` cannot be sent ("" when it can): no text, a text over MaxText, a Rewrite without an
/// instruction.
QString problem(const Input &input);

/// The request for the model: a system text that says to give back the new text alone, keeping
/// the note's Markdown (links, tags, tasks, dates, reminders, the list's structure) and its
/// language; the instruction, the page's name, the context and the text in one user message.
/// Effort "low" (an answer at once; the model on the device does not think first), the answer's
/// length after the text's.
ai::Request request(const Input &input);

/// The model's answer as it goes into the note: trimmed; a fence around all of it, a preamble
/// line ("Here is the rewritten text:", "Oto …:") and quotes around it that the text has not
/// taken off; an inline text without the list marker it did not have, on one line when it was on
/// one; a continuation without the end of the text it repeated. Also for an answer still coming
/// (`partial`: a fence opened but not closed yet goes too).
QString clean(QStringView answer, const Input &input, bool partial = false);

/// A change of a text: [from, to) replaced by `text`.
struct Edit {
    qsizetype from = 0;
    qsizetype to = 0;
    QString text;
    friend bool operator==(const Edit &, const Edit &) = default;
};

/// Where `result` goes in `text` whose [from, to) was sent: Rewrite — in place of it, the
/// whitespace around the selection kept; Continue — at `to`, after a space or a line break as the
/// text there wants; Summarize — after `to` as a paragraph of its own (a blank line before it,
/// and after it when text follows). Positions are clamped to the text.
Edit edit(QStringView text, qsizetype from, qsizetype to, QStringView result, Action action);

/// `text` with `edit` made.
QString apply(QStringView text, const Edit &edit);

/// A part of a change, word by word.
struct Change {
    enum Kind { Same, Removed, Added };
    Kind kind = Same;
    QString text;
    friend bool operator==(const Change &, const Change &) = default;
};

/// What changed from `before` to `after`, by words (and the spaces and punctuation between them):
/// runs of the same kind joined, removals before additions; two texts too long to compare word
/// for word are all removed and all added.
QList<Change> diff(QStringView before, QStringView after);

} // namespace brulion::writing
