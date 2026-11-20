// What brulion:// addresses write into a note, as pure functions (content in → content out): a
// block added at the end or the start of a page, of a heading's section or of a block's children,
// and the task line an address describes (natural-language dates through dates::parseNaturalDate).
// Every byte the edit does not touch stays as it was — line endings, a BOM, the frontmatter.
// Part of the automation module — see automation/automation.h.
//
//   insertBlock("# Zakupy\n\n## Dziś\n- chleb\n\n## Jutro\n", {"- mleko", End, "Dziś"})
//       -> "# Zakupy\n\n## Dziś\n- chleb\n- mleko\n\n## Jutro\n"
//   buildTask({text: "Zadzwonić do mamy", remind: "jutro o 18"}, 2026-11-20 10:00)
//       -> "- [ ] Zadzwonić do mamy ⏰ 2026-11-21 18:00"
#pragma once

#include "dates/names.h"
#include "types.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QTime>

#include <optional>

namespace brulion::automation {

/// Where in the page, the section or among the children the block goes.
enum class Place { End, Start };

struct InsertSpec {
    /// The block, lines joined with "\n" (a list item "- …" with its continuation lines indented
    /// by two spaces: notebook::blockFromText(), buildTask()).
    QString block;
    Place place = Place::End;
    /// A heading of the page ("Dziś", "## Dziś", an Obsidian path "Plan#Dziś": markdown::
    /// findHeading()): the block ends (Start: begins) its section. "" = the page itself.
    QString heading;
    /// A block id (with or without "^" / "(( ))"): the block becomes its last (Start: first) child.
    QString blockId;
};

struct Inserted {
    bool ok = false;
    QString content; ///< the new text (ok)
    int line = -1;   ///< the block's first line in it (0-based)
    /// "heading_not_found", "block_not_found", "empty" (a blank block) when not ok.
    QString error;
};

/// `content` with `spec.block` added (see InsertSpec):
///   * the page's end: notebook::appendBlock() (a template's empty last bullet is filled, trailing
///     blank lines go, the note's own line ending);
///   * the page's start: the first block after the frontmatter, the page's properties and a title
///     heading (a level-1 heading that opens the page);
///   * a heading: the end of its section (before the next heading of its level or higher) or right
///     under the heading;
///   * a block: its last or first child (one level deeper).
Inserted insertBlock(const QString &content, const InsertSpec &spec);

/// The task an address describes. A date phrase is ISO ("2026-11-21", "2026-11-21 18:00") or
/// natural (Polish or English: "jutro o 18", "next friday"); words around it are ignored.
struct TaskSpec {
    QString text;
    QString due;       ///< 📅
    QString scheduled; ///< ⏳
    QString start;     ///< 🛫
    QString remind;    ///< ⏰ (a date without a time gets the notebook's default reminder time)
    bool alarm = false; ///< the reminder rings as an alarm (⏰!)
    QString every;     ///< 🔁 a recurrence ("co tydzień", "every weekday")
    QString priority;  ///< highest … lowest (najwyższy … najniższy)
    /// No date given: a date phrase in the text makes the reminder (with a time) or the due date,
    /// as quick capture reads it (notebook::buildCaptureLine()).
    bool parseText = true;
};

struct BuiltTask {
    QString block; ///< "- [ ] Text 📅 …" ("" on error)
    /// "missing_text", "date_not_understood", "recurrence_not_understood", "bad_priority" when the
    /// block is empty.
    QString error;
    QString detail;          ///< the parameter or phrase that was wrong
    DateSpec reminder;       ///< the reminder written (invalid: none)
    bool alarm = false;      ///< it is an alarm
    std::optional<QDate> due; ///< the due date written
};

/// The task line of `spec` at `now` (dates relative to it). `defaultTime`: a reminder's time when
/// its phrase has none.
BuiltTask buildTask(const TaskSpec &spec, const QDateTime &now, Qt::DayOfWeek weekStart, dates::UiLocale locale,
                    QTime defaultTime = QTime(9, 0));

/// A date phrase that names a day, nothing else ("dziś", "wczoraj", "w piątek", "2026-11-21",
/// "today"); nullopt when it has other words or names no day.
std::optional<QDate> dayOf(QStringView phrase, const QDateTime &now, Qt::DayOfWeek weekStart,
                           dates::UiLocale locale);

} // namespace brulion::automation
