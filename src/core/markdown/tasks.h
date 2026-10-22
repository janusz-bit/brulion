// Task markers (docs/SYNTAX.md §5): Markdown checkboxes `- [ ]` and Logseq keywords (`TODO`,
// `DOING`, `DONE`, …) with their status mapping in both directions. Part of the markdown
// module — see markdown/markdown.h for the module overview.
#pragma once

#include "types.h"

#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::markdown {

/// Status of a checkbox character; unknown characters (theme "custom statuses") count as todo.
TaskStatus statusFromCheckbox(QChar ch);

/// Status of a Logseq keyword (TODO/LATER, DOING/NOW/IN-PROGRESS, DONE, CANCELED/CANCELLED,
/// WAITING/WAIT); nullopt for anything else.
std::optional<TaskStatus> statusFromKeyword(QStringView word);

/// Checkbox character written by Brulion: ' ', '/', 'x', '-', '>'.
QChar statusChar(TaskStatus status);

/// Checkbox character for `status`, keeping the written `X` / custom character when the status
/// does not change.
QChar checkboxCharFor(TaskStatus status, std::optional<QChar> previous = std::nullopt);

/// Logseq keyword for `status`, following the workflow of `previous` (`LATER`/`NOW` vs
/// `TODO`/`DOING`) and its spelling of "cancelled" / "waiting".
QString keywordFor(TaskStatus status, QStringView previous = {});

/// Open = todo or doing (SYNTAX §10 "open").
inline bool isOpenStatus(TaskStatus status)
{
    return status == TaskStatus::Todo || status == TaskStatus::Doing;
}

/// A task marker found at the start of a list item's content.
struct TaskMarker {
    enum class Kind { Checkbox, Keyword };
    Kind kind = Kind::Checkbox;
    TaskStatus status = TaskStatus::Todo;
    QString marker;             ///< the checkbox character or the keyword
    qsizetype from = 0;         ///< index of '[' or of the keyword
    qsizetype to = 0;           ///< index after the marker
    qsizetype contentStart = 0; ///< index after the marker and the whitespace following it (in `text`)
};

/// Detect a checkbox (`[ ]`, `[x]`, `[?]` …) or a Logseq keyword at `start` of a list item
/// line. `masked` (same length as `text`) is used for detection, `text` for the character.
std::optional<TaskMarker> matchTaskMarker(QStringView text, QStringView masked, qsizetype start);

/// Same with `text` used for both.
inline std::optional<TaskMarker> matchTaskMarker(QStringView text, qsizetype start)
{
    return matchTaskMarker(text, text, start);
}

} // namespace brulion::markdown
