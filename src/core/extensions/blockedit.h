#pragma once
// Pure edits of one block line for b.replaceBlock (content in → content out; the other lines, the
// line endings and the line's indentation, list marker and trailing fields stay byte for byte).
//
//   blockLineText(u"  - [x] [[Przysiad]] 100 kg 5×5 ✅ 2026-11-04") == u"[[Przysiad]] 100 kg 5×5 ✅ 2026-11-04"
//   replaceBlockText(content, 2, u"[[Przysiad]] 100 kg 4×5", u"x"_s, today)
//
// replaceBlockText keeps what the text the extension writes leaves out: Obsidian Tasks' date
// fields (✅ ❌ ➕ 📅 ⏳ 🛫, a due, scheduled or start date with its time) wherever they stand in the
// old text — before a tag too, not in code — and a block id ("^abc123") at its end, unless the new
// text has its own; they go after the new text, in their order. A status (one checkbox character)
// is set with markdown::setTaskStatus (✅ / ❌ dates, a plain line becomes a task, a Logseq keyword
// stays a keyword).

#include <QDate>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::extensions {

/// The text of a block's first line after its indentation, list marker and checkbox or Logseq task
/// keyword, verbatim otherwise; a paragraph's line trimmed.
QString blockLineText(QStringView line);

/// The status character of a task line ("x", " ", "-", "/", ">"…; a Logseq keyword as its
/// checkbox character), nullopt for a line that is no task.
std::optional<QString> taskCharacter(QStringView line);

/// `content` with the text of line `line` (0-based) replaced by `text` and, when `task` is set and
/// differs, the task's status changed; nullopt when the line does not exist or `text` has a line
/// break.
std::optional<QString> replaceBlockText(const QString &content, int line, const QString &text,
                                        const std::optional<QString> &task, QDate today);

/// The words of a line as a reader takes them (b.plain, a block's `plain`): `[[links]]` as their
/// labels (the alias, else the target's last part without its heading or ".md"), tags left out,
/// white space single; only the first line of `text`.
QString plainWords(QStringView text);

/// Where insertBlockLines() puts new lines: before the block, after it (and its children), or as
/// its last child.
enum class Placement { Before, After, Child };

/// `content` with the lines of `markdown` (line breaks "\n", "\r\n" or "\r"; trailing empty
/// lines dropped) inserted around the block whose first line is `line` and whose last line, its
/// children included, is `endLine`. Every line gets the block's indentation; as a child also
/// `childIndent` after it (the indentation of the block's first child beyond its own, or one level:
/// a tab where the block is indented with tabs, else two spaces, when "" is given). New lines end
/// as the note's lines do; a note without a final line break keeps having none. `firstLine`: the
/// first new line's number afterwards. nullopt when the lines do not exist or `markdown` is empty.
std::optional<QString> insertBlockLines(const QString &content, int line, int endLine, const QString &markdown,
                                        Placement where, const QString &childIndent = QString(),
                                        int *firstLine = nullptr);

/// `content` without lines `line` … `endLine` (a block with its children); a note that had no
/// final line break still has none. nullopt when the lines do not exist.
std::optional<QString> removeBlockLines(const QString &content, int line, int endLine);

/// `content` with the property `key` of the block at `line` (its own lines up to `ownEndLine`) set
/// to `value`: an existing `key:: …` line (any letter case) gets the new value, else a line
/// `key:: value` is added after the block's last property line — or right after its first line —
/// indented as its other property lines, else under its text ("- " → two spaces). An empty
/// `value` removes the property's lines. nullopt when the line does not exist, the key is no
/// property name (letters, digits, "-", "_") or `value` has a line break.
std::optional<QString> setBlockProperty(const QString &content, int line, int ownEndLine, const QString &key,
                                        const QString &value);

// Lines as written (b.replaceLine, b.insertLine, b.removeLine): no indentation, marker or field is
// added or kept — the script gives each whole line; every other line and every line ending stays.

/// `content` with line `line` (0-based) replaced by `text`, its line ending kept; nullopt when the
/// line does not exist or `text` has a line break.
std::optional<QString> replaceLineText(const QString &content, int line, const QString &text);

/// `content` with the lines of `text` (line breaks "\n", "\r\n" or "\r"; nothing dropped — an
/// empty line stays one) inserted before (`before`) or after line `line`, as written. New lines end
/// as the note's lines do; a note without a final line break keeps having none. `firstLine`: the
/// first new line's number afterwards. nullopt when the line does not exist.
std::optional<QString> insertLinesAt(const QString &content, int line, const QString &text, bool before,
                                     int *firstLine = nullptr);

/// `content` without line `line` (a note that had no final line break still has none); nullopt
/// when the line does not exist.
std::optional<QString> removeLineAt(const QString &content, int line);

} // namespace brulion::extensions
