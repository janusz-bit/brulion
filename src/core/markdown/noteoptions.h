// A note's own options (docs/SYNTAX.md §4 "Note options"): how this one note is shown where it
// differs from the device's settings — line numbers beside its code blocks, frames around its
// pictures, a light backing under its pictures in a dark theme, its text width — and whether its tasks are left out of the task lists. They are page
// properties (YAML frontmatter or Logseq `key:: value` lines), so they travel with the file; a
// note without them follows the device's settings. Written through setPageProperty()
// (markdown/pageprops.h), so an edit changes only that property's line. Part of the markdown
// module — see markdown/markdown.h.
#pragma once

#include "types.h"

#include <QString>
#include <QStringList>
#include <QStringView>
#include <QVariant>

#include <optional>

namespace brulion::markdown {

/// The options a note can set.
enum class NoteOption {
    LineNumbers,   ///< line numbers beside code blocks: a flag
    PictureFrames, ///< a frame around pictures: a flag
    TextWidth,     ///< the width text keeps within: a NoteTextWidth
    Tasks,         ///< the note's tasks left out of the task lists (`tasks: ignore`, §5)
    PictureBacking, ///< a light backing under pictures in a dark theme: a flag
};

/// The text widths a note can ask for (the device's choices of Settings → Editor → Text width).
enum class NoteTextWidth { Narrow, Normal, Wide, Full };

/// The option's name in the app ("lineNumbers", "pictureFrames", "textWidth", "tasks",
/// "pictureBacking").
QString noteOptionName(NoteOption option);
std::optional<NoteOption> parseNoteOptionName(QStringView name);

/// The page property keys an option is read from, lower case: the English key first, then the
/// Polish one (`line-numbers`, `numery-linii`; `picture-frames`, `ramki`; `text-width`,
/// `szerokosc-tekstu`; `tasks`, `zadania`; `picture-backing`, `jasne-tlo`). The first key the
/// page has decides.
QStringList noteOptionKeys(NoteOption option);
/// The key Brulion writes for a new option: the English one, or the Polish one (`polish`).
QString noteOptionKey(NoteOption option, bool polish);

/// A flag's value: `true` / `yes` / `tak` / `on` / `1` or `false` / `no` / `nie` / `off` / `0`
/// (any case, a YAML bool or number, a list's first item); anything else is no value.
std::optional<bool> parseNoteFlag(const QVariant &value);
/// A width's value: `narrow` / `wąska`, `normal` / `normalna`, `wide` / `szeroka`, `full` /
/// `pełna` (any case, diacritics optional, a list's first item); anything else is no value.
std::optional<NoteTextWidth> parseNoteTextWidth(const QVariant &value);
/// A width's name in the app and in an English note ("narrow", "normal", "wide", "full").
QString noteTextWidthName(NoteTextWidth width);
std::optional<NoteTextWidth> noteTextWidthFromName(QStringView name);
/// What Brulion writes for a width: the English name, or the Polish one (`wąska` …).
QString noteTextWidthValue(NoteTextWidth width, bool polish);
/// The width in pixels the editor keeps text within, as Settings → Editor → Text width stores it
/// (600, 680, 960; 0 for the full width).
int noteTextWidthPixels(NoteTextWidth width);

/// The options a note sets.
struct NoteOptions {
    std::optional<bool> lineNumbers;
    std::optional<bool> pictureFrames;
    std::optional<NoteTextWidth> textWidth;
    bool tasksIgnored = false;
    std::optional<bool> pictureBacking;
    friend bool operator==(const NoteOptions &, const NoteOptions &) = default;
};

/// The options of a note from its properties as parseNote() keeps them (ParsedNote::properties:
/// keys lower case). For each option the first of its keys the note has decides; a value that is
/// none of the option's is no option (the device's setting holds).
NoteOptions noteOptions(const Properties &properties);

} // namespace brulion::markdown
