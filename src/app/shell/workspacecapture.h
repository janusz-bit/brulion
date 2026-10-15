// Quick capture (Workspace::quickCapture): what a captured line becomes in today's journal.
// Pure, so the Polish phrases are unit-tested without a vault.
//
//   "Kupić mleko jutro o 18"      -> "- [ ] Kupić mleko ⏰ 2026-10-15 18:00"   (a reminder)
//   "Oddać książkę w piątek"      -> "- [ ] Oddać książkę 📅 2026-10-16"       (a due date)
//   "Pomysł na prezent"           -> "- Pomysł na prezent"                     (a plain note)
//
// The first natural-language date of the first line (dates::parseNaturalDate, Polish and
// English) is taken out of the text: with a time it becomes a ⏰ reminder, without one a 📅 due
// date, and the line becomes a task (an existing checkbox / TODO keyword is kept). Dates before
// today ("wczoraj byłem u lekarza") are left as text: that is a note about the past. A line that
// already has a ⏰ or 📅 token, or a phrase that is the whole text, is not converted. Further
// lines become the block's continuation lines (indented by two spaces).
#pragma once

#include "dates/names.h"
#include "types.h"

#include <QDateTime>
#include <QString>

namespace brulion::shell {

struct CaptureLine {
    enum class Kind { Empty, Note, Reminder, Due };
    Kind kind = Kind::Empty;
    /// The block to append, lines joined with "\n" (the caller writes the note's line ending).
    QString block;
    /// The text without the date phrase (first line, no list marker / checkbox).
    QString text;
    /// Reminder / due date (invalid for notes).
    DateSpec when;
};

/// "note" | "reminder" | "due" | "" (empty).
QString captureKindName(CaptureLine::Kind kind);

CaptureLine buildCaptureLine(const QString &input, const QDateTime &now, Qt::DayOfWeek weekStart = Qt::Monday,
                             dates::UiLocale locale = dates::UiLocale::Polish);

} // namespace brulion::shell
