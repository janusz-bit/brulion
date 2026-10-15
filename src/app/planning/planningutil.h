// Helpers shared by the planning models (calendar, tasks, graph): ISO date strings for QML,
// UI locale, the renderer palette from QML colours, task/priority names, human date labels,
// the lines written by "Dodaj zadanie" / "Dodaj przypomnienie" / quick add, and the pure
// "append a block to a page" edit. GUI-free (QtCore + core modules). Namespace brulion::planning.
#pragma once

#include "dates/names.h"
#include "markdown/inlinerenderer.h"
#include "types.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>

namespace brulion {
class VaultIndex;
}

namespace brulion::planning {

/// Strict "YYYY-MM-DD" -> QDate (invalid QDate otherwise).
QDate parseIsoDate(const QString &text);
/// QDate -> "YYYY-MM-DD" ("" when invalid).
QString isoDate(QDate date);
/// "HH:mm" ("" without a time).
QString clockTime(const std::optional<QTime> &time);

/// "en" -> English, anything else Polish (the default UI language).
dates::UiLocale uiLocale(const QString &locale);

/// Renderer palette from a QML map of colours (keys as BlockEditor.rendererPalette: link,
/// unresolved, muted, codeBackground, highlightBackground, <chip>Fg / <chip>Bg); missing keys keep
/// the light defaults.
markdown::Palette paletteFromMap(const QVariantMap &map);

/// Inline Markdown of a task / reminder (links, tags, emphasis) as escaped rich text for QML
/// `Text { textFormat: Text.RichText }`; links resolve against `index` seen from `fromPath`.
QString renderInline(const VaultIndex *index, const QString &markdown, const VaultPath &fromPath, QDate today,
                     dates::UiLocale locale, const markdown::Palette &palette);

/// Plain text of inline Markdown for labels and accessibility: wiki links show their display
/// text (or target), Markdown links their text, emphasis markers are removed, whitespace is
/// collapsed.
QString plainText(const QString &markdown);

/// "todo" | "doing" | "done" | "cancelled" | "deferred".
QString statusName(TaskStatus status);
std::optional<TaskStatus> statusFromName(const QString &name);
/// Done or cancelled.
bool isClosed(TaskStatus status);

/// "" (none) | "lowest" | "low" | "medium" | "high" | "highest".
QString priorityName(Priority priority);
/// Inverse of priorityName(); Priority::None for anything unknown.
Priority priorityFromName(const QString &name);

/// Human label relative to `today` ("dziś 09:00", "jutro", "pt 17 paź"), see dates::formatHuman.
QString humanDate(const DateSpec &spec, dates::UiLocale locale, QDate today);

/// The task line written by "Dodaj zadanie" / quick add: "- [ ] <text>" plus " 📅 <due>" and
/// " ⏰ <reminder>" when given. The text is trimmed and its line breaks become spaces; a list
/// marker or checkbox typed by the user is not doubled.
QString taskLine(const QString &text, const std::optional<DateSpec> &due = std::nullopt,
                 const std::optional<DateSpec> &reminder = std::nullopt);

/// The reminder line written by "Dodaj przypomnienie": "- <text> ⏰ <at>" (`⏰!` for alarms) and
/// " 🔁 <recurrence>" when given (canonical English, as BReminderDialog returns it).
QString reminderLine(const QString &text, const DateSpec &at, bool alarm = false, const QString &recurrence = {});

/// Append `block` (one or more lines; continuation lines get a two-space indent under a list
/// item) at the end of `content`, keeping its line endings. A trailing empty bullet ("- ", e.g.
/// left by a journal template) is replaced instead of kept above the new block; trailing blank
/// lines are dropped. An empty block returns `content` unchanged.
QString appendBlock(const QString &content, const QString &block);

/// Result of parsing quick-add input ("Zadzwonić do Ani jutro o 9 #praca").
struct QuickAdd {
    QString text;   ///< the task text with the date phrase removed ("Zadzwonić do Ani #praca")
    std::optional<DateSpec> when; ///< the recognised date (and time); unset when none / in the past
    QString phrase; ///< the matched phrase ("jutro o 9"), "" when none
    QString line;   ///< the Markdown line to write (taskLine(text, due, reminder))
    bool ok = false; ///< there is text to add
};

/// Natural-language quick add (TasksView): the input always becomes a task. The first date
/// phrase (dates::parseNaturalDate) is taken out of the text — with a time it becomes a ⏰
/// reminder, without one a 📅 due date — the same convention as the shell's quick capture
/// (shell/workspacecapture.h). Dates before today stay text. Without a phrase the task gets no
/// date.
QuickAdd parseQuickAdd(const QString &input, const QDateTime &now, Qt::DayOfWeek weekStart = Qt::Monday,
                       dates::UiLocale locale = dates::UiLocale::Polish);

/// Quote a query value when needed (spaces, quotes, commas): `praca` -> `praca`,
/// `do przeczytania` -> `"do przeczytania"`.
QString queryValue(const QString &value);

/// Locale-aware, case- and diacritic-insensitive comparison of names (Polish collation).
int compareNames(const QString &a, const QString &b);

} // namespace brulion::planning
