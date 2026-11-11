#pragma once
// Pure edits of one block line for b.replaceBlock (content in → content out; the other lines, the
// line endings and the line's indentation, list marker and trailing fields stay byte for byte).
//
//   blockLineText(u"  - [x] [[Przysiad]] 100 kg 5×5 ✅ 2026-11-04") == u"[[Przysiad]] 100 kg 5×5 ✅ 2026-11-04"
//   replaceBlockText(content, 2, u"[[Przysiad]] 100 kg 4×5", u"x"_s, today)
//
// replaceBlockText keeps what the text the extension writes leaves out: Obsidian Tasks' date
// fields (✅ ❌ ➕ 📅 ⏳ 🛫) and a block id ("^abc123") at the end of the old text, unless the new
// text has its own. A status (one checkbox character) is set with markdown::setTaskStatus (✅ / ❌
// dates, a plain line becomes a task, a Logseq keyword stays a keyword).

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

} // namespace brulion::extensions
