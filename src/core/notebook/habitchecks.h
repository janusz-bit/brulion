// A habit's check in a daily note (docs/SYNTAX.md §5 "Habits"), as the app's Habits view
// and `brulion-cli habit set` write it: pure content edits (content in → content out) — the rest of
// the note keeps its bytes (line endings, a BOM, other lines). Part of the notebook module — see
// notebook/notebook.h.
#pragma once

#include "dates/names.h"
#include "habits/habits.h"
#include "types.h"

#include <QDate>
#include <QString>

namespace brulion::notebook {

/// What setHabitCheck() did.
struct HabitCheckEdit {
    QString content;                          ///< the note after the edit (the input when nothing changes)
    bool changed = false;                     ///< content differs from the input
    bool created = false;                     ///< a new check line was written
    int line = -1;                            ///< the check's line after the edit (-1: the note has none)
    habits::Mark before = habits::Mark::Open; ///< the day's mark before (Open without a check)
    bool hadCheck = false;                    ///< the note had a check of the habit
    bool refused = false;                     ///< the name cannot be written as a check (it would read back as another)
};

/// The check line Brulion writes for habit `name` (before its status is set): "- [ ] Medytacja
/// #nawyk" (`tag` without '#', habits::checkTag()); `pageLink`: the name as a link to the habit's
/// page, "- [ ] [[Medytacja]] #nawyk".
QString habitCheckLine(const QString &name, const QString &tag, bool pageLink = false);

/// Set the mark of habit `name` (compared by habits::habitKey()) in the daily note `content` of
/// `date` at `path` (parsed with `config`):
///  * Done / Skipped: when a check of the habit already has that mark nothing changes; else the
///    first check of the habit (document order) gets it (markdown::setTaskStatus: `✅ today` /
///    `❌ today`, a Logseq keyword stays a keyword); without a check a new line is written — right
///    after the last check of any habit in the note (its indentation), else as the note's last
///    block (notebook::appendBlock(): a template's empty last bullet is filled);
///  * Open: every done or skipped check of the habit becomes `[ ]` again (its ✅ / ❌ date goes);
///    no line is written when there is none.
/// The new line's tag is `tag` ("nawyk" | "habit"); `pageLink` as in habitCheckLine().
HabitCheckEdit setHabitCheck(const QString &content, const VaultPath &path, const VaultConfig &config, QDate date,
                             const QString &name, habits::Mark mark, QDate today, const QString &tag,
                             bool pageLink = false);

/// A new habit's definition block: "- Medytacja\n  habit:: codziennie" (schedule as given, trimmed;
/// empty: every day, "- Medytacja\n  habit:: codziennie" in Polish / "every day" in English).
QString habitDefinitionBlock(const QString &name, const QString &schedule, dates::UiLocale locale);

/// The page new habits go to when no page defines one yet: "Nawyki" / "Habits".
QString habitsPageTitle(dates::UiLocale locale);

} // namespace brulion::notebook
